"""A pane's final recap survives worker shutdown and never replaces newer work."""
import dataclasses
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Event, Thread
from types import SimpleNamespace
from unittest.mock import patch

from relay_core import conv_index, final_summary
from relay_core.agent import Agent
from relay_core.provider import ProviderConfig
from relay_core.sessions import SessionStore, new_id, read_meta
from tests.test_sessions import ScriptedProvider


class FinalSummaryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.store = SessionStore(self.root / "sessions", index=False)
        self.session_id = new_id()
        self.data = {"id": self.session_id, "kind": "relay_session", "version": 1,
                     "workspace": str(self.root), "model": "test", "turns": 2, "epoch": 0,
                     "messages": [{"role": "user", "content": "Fix the Sessions table"},
                                  {"role": "assistant", "content": "The table now fills the pane."}],
                     "summary": "", "summary_turn": 0, "summary_success_turn": 0,
                     "checkpoints": {"items": []}, "todos": {"items": []}}
        self.store.save(self.data)

    def job(self):
        return {"session_dir": str(self.store.directory), "session_id": self.session_id,
                "turns": self.data["turns"], "fingerprint": final_summary._fingerprint(self.data),
                "config": dataclasses.asdict(ProviderConfig("http://127.0.0.1:1/v1", "test", "")),
                "stall_timeout": 1.0}

    def test_missing_and_stale_recaps_are_due_but_fresh_is_not(self):
        self.assertTrue(final_summary.needs_final_summary(self.data, {}))
        self.data["summary"] = "An older saved recap."
        self.data["summary_turn"] = 2  # a failed refresh advanced the attempt marker
        self.data["summary_success_turn"] = 1
        self.assertTrue(final_summary.needs_final_summary(self.data, {}))
        self.data["summary_success_turn"] = 2
        self.assertFalse(final_summary.needs_final_summary(self.data, {}))
        self.data["messages"] = [{"role": "user", "content": "No answer yet"}]
        self.assertFalse(final_summary.needs_final_summary(self.data, {}))

    def test_job_saves_metadata_only_and_preserves_previous_recap_on_failure(self):
        before = self.store.path(self.session_id).read_text()
        with patch.object(final_summary.titles, "generate_summary", side_effect=RuntimeError("provider failed")):
            with self.assertRaises(RuntimeError):
                final_summary.run_job(self.job(), provider=object())
        self.assertFalse(read_meta(self.store.directory, self.session_id).get("summary"))
        with patch.object(final_summary.titles, "generate_summary", return_value="Final work completed; all checks pass."):
            self.assertTrue(final_summary.run_job(self.job(), provider=object()))
        self.assertEqual(self.store.path(self.session_id).read_text(), before)
        meta = read_meta(self.store.directory, self.session_id)
        self.assertEqual(meta["summary"], "Final work completed; all checks pass.")
        self.assertEqual(meta["summary_success_turn"], 2)
        with patch.object(final_summary.titles, "generate_summary", return_value=""):
            self.assertFalse(final_summary.run_job(self.job(), provider=object()))
        self.assertEqual(read_meta(self.store.directory, self.session_id)["summary"], meta["summary"])

    def test_job_rejects_a_session_resumed_while_the_model_runs(self):
        def advance(*_args, **_kwargs):
            changed = self.store.load(self.session_id)
            changed["turns"] = 3
            changed["messages"].append({"role": "assistant", "content": "More work after reopening."})
            self.store.save(changed)
            return "A recap of old work that must not land."

        with patch.object(final_summary.titles, "generate_summary", side_effect=advance):
            self.assertFalse(final_summary.run_job(self.job(), provider=object()))
        self.assertFalse(read_meta(self.store.directory, self.session_id).get("summary"))

    def test_job_rejects_a_newer_recap_written_while_the_model_runs(self):
        def newer(*_args, **_kwargs):
            self.store.note_summary(self.session_id, "The newer summary already saved.", turn=2)
            return "An older result that must not land."

        with patch.object(final_summary.titles, "generate_summary", side_effect=newer):
            self.assertFalse(final_summary.run_job(self.job(), provider=object()))
        self.assertEqual(read_meta(self.store.directory, self.session_id)["summary"],
                         "The newer summary already saved.")

    def test_late_recap_survives_a_stale_pane_save(self):
        self.data.update(summary="Earlier summary before final work.", summary_turn=2,
                         summary_success_turn=1)
        self.store.save(self.data)
        self.store.note_summary(self.session_id, "Final recap from the close job.", turn=2)
        self.store.save(self.data)  # an already open pane still has its earlier text
        meta = read_meta(self.store.directory, self.session_id)
        self.assertEqual(meta["summary"], "Final recap from the close job.")
        self.assertEqual(meta["summary_success_turn"], 2)
        index = conv_index.ConversationIndex(self.root / "index.db")
        self.addCleanup(index.close)
        self.assertTrue(index.index_session_file(self.store.path(self.session_id)))
        self.assertEqual(index.conversation(self.session_id)["summary"], "Final recap from the close job.")
        agent = Agent(ProviderConfig("http://127.0.0.1:1/v1", "mock", ""), str(self.root),
                      lambda _event: None, provider=ScriptedProvider(),
                      session_dir=str(self.store.directory))
        self.addCleanup(agent.executor.shutdown)
        agent.resume(self.session_id)
        self.assertEqual(agent.summary, "Final recap from the close job.")
        self.assertEqual(agent.summary_success_turn, 2)
        self.assertGreater(agent.summary_time, 0)

    def test_no_chores_provider_keeps_the_existing_recap(self):
        self.data.update(summary="Previous saved recap remains here.", summary_turn=2,
                         summary_success_turn=1)
        self.store.save(self.data)
        agent = SimpleNamespace(store=self.store, session_id=self.session_id, turns=2,
                                autosave=lambda: None, side_provider=lambda **_kwargs: None)
        self.assertFalse(final_summary.start(agent))
        self.assertEqual(read_meta(self.store.directory, self.session_id)["summary"],
                         "Previous saved recap remains here.")

    def test_close_starts_a_separate_process_that_finishes_after_return(self):
        parent_exited = Event()

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                parent_exited.wait(5)
                body = (b'data: {"choices":[{"delta":{"content":"{\\"summary\\":'
                        b' \\"Final recap saved after closing the pane.\\"}"},"finish_reason":null}]}\n\n'
                        b'data: {"choices":[{"delta":{},"finish_reason":"stop"}]}\n\n'
                        b'data: [DONE]\n\n')
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *_args):
                pass

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        script = """
from types import SimpleNamespace
from relay_core import final_summary
from relay_core.provider import ProviderConfig
from relay_core.sessions import SessionStore
import sys
store = SessionStore(sys.argv[1], index=False)
provider = SimpleNamespace(config=ProviderConfig(sys.argv[3], 'mock', ''), serves_side_calls=True)
agent = SimpleNamespace(store=store, session_id=sys.argv[2], turns=2, stall_timeout_s=5.0,
                        autosave=lambda: None, side_provider=lambda **kwargs: provider)
assert final_summary.start(agent)
"""
        backend = str(Path(__file__).resolve().parents[1] / "backend")
        env = {**os.environ, "PYTHONPATH": backend}
        parent = subprocess.run([sys.executable, "-S", "-c", script, str(self.store.directory),
                                 self.session_id, f"http://127.0.0.1:{server.server_port}/v1"],
                                cwd=self.root, env=env, capture_output=True, text=True, timeout=5)
        self.assertEqual(parent.returncode, 0, parent.stderr)
        parent_exited.set()
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if read_meta(self.store.directory, self.session_id).get("summary"):
                break
            time.sleep(0.02)
        self.assertEqual(read_meta(self.store.directory, self.session_id).get("summary"),
                         "Final recap saved after closing the pane.")


if __name__ == "__main__":
    unittest.main()
