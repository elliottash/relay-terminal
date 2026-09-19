import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

from relay_core import guest_hook

HELPER = Path(__file__).resolve().parents[1] / "shell" / "guest-event.py"
RELAY_VARS = ("RELAY_GUEST_EVENT", "RELAY_RUNTIME_DIR", "RELAY_SESSION_TOKEN",
              "RELAY_GUEST_ID", "RELAY_GUEST_PERMISSION_TIMEOUT", "RELAY_GUEST_STATUSLINE")


@contextlib.contextmanager
def relay_env(**values):
    """The pane's environment for one call: exactly the RELAY_* variables given, nothing left
    over from the machine the suite runs on."""
    saved = {name: os.environ.pop(name, None) for name in RELAY_VARS}
    os.environ.update({name: str(value) for name, value in values.items() if value is not None})
    try:
        yield
    finally:
        for name, value in saved.items():
            os.environ.pop(name, None)
            if value is not None:
                os.environ[name] = value


@contextlib.contextmanager
def io_for(payload: str):
    """stdin holding `payload`, stdout captured. Both are process-wide, which is fine: the
    suite runs one test at a time."""
    out = io.StringIO()
    with mock.patch.object(sys, "stdin", io.StringIO(payload)), mock.patch.object(sys, "stdout", out):
        yield out


def call(argv, payload: str) -> tuple[int, str]:
    with io_for(payload) as out:
        code = guest_hook.main(argv)
    return code, out.getvalue()


def read_envelope(runtime: Path) -> dict:
    with (runtime / "guest.json").open(encoding="utf-8") as handle:
        return json.load(handle)


# ----- the helper the GUI writes into every pane (protocol 26.3) -----------------------------


class GuestEventHelper(unittest.TestCase):
    """shell/guest-event.py: the writer the shims reach the pane through."""

    def run_helper(self, runtime: Path, argv, payload: str, token="tok-1", env=None):
        environment = {"PATH": os.environ.get("PATH", os.defpath), "RELAY_RUNTIME_DIR": str(runtime),
                       "RELAY_SESSION_TOKEN": token}
        environment.update(env or {})
        return subprocess.run([sys.executable, str(HELPER), *argv], input=payload, text=True,
                              capture_output=True, env=environment)

    def test_envelope_is_written_with_a_fresh_sequence_each_time(self):
        with tempfile.TemporaryDirectory() as root:
            runtime = Path(root)
            first = self.run_helper(runtime, ["statusline", "claude"], '{"model": "M"}')
            self.assertEqual(0, first.returncode, first.stderr)
            self.assertEqual("", first.stdout)
            one = read_envelope(runtime)
            self.assertEqual({"token": "tok-1", "event": "statusline", "guest": "claude",
                              "data": {"model": "M"}}, {k: v for k, v in one.items() if k != "sequence"})
            # A new inode every write, so the pane's stat() sees the change: that is the whole
            # reason the poll can be one stat() instead of an open/read/parse.
            inode = os.stat(runtime / "guest.json").st_ino
            second = self.run_helper(runtime, ["state", "claude"], '{"busy": true}')
            self.assertEqual(0, second.returncode, second.stderr)
            two = read_envelope(runtime)
            self.assertEqual("state", two["event"])
            self.assertTrue(two["data"]["busy"])
            self.assertNotEqual(inode, os.stat(runtime / "guest.json").st_ino)
            self.assertNotEqual(one["sequence"], two["sequence"])

    def test_the_shim_can_pin_the_sequence(self):
        with tempfile.TemporaryDirectory() as root:
            runtime = Path(root)
            self.run_helper(runtime, ["hook", "claude", "seq-42"], '{"name": "PreToolUse"}')
            self.assertEqual("seq-42", read_envelope(runtime)["sequence"])

    def test_malformed_or_missing_stdin_is_an_empty_event_not_a_crash(self):
        with tempfile.TemporaryDirectory() as root:
            runtime = Path(root)
            for payload in ("", "not json", "[1, 2]", "null"):
                with self.subTest(payload=payload):
                    run = self.run_helper(runtime, ["hook", "claude"], payload)
                    self.assertEqual(0, run.returncode, run.stderr)
                    self.assertEqual({}, read_envelope(runtime)["data"])

    def test_without_the_pane_environment_it_is_a_no_op(self):
        """The hard invariant (26.3): no RELAY_* environment, no output and no write anywhere."""
        with tempfile.TemporaryDirectory() as root:
            runtime = Path(root)
            run = subprocess.run([sys.executable, str(HELPER), "hook", "claude"], input="{}", text=True,
                                 capture_output=True, env={"PATH": os.environ.get("PATH", os.defpath)})
            self.assertEqual(0, run.returncode)
            self.assertEqual("", run.stdout)
            self.assertEqual("", run.stderr)
            self.assertEqual([], list(runtime.iterdir()))

    def test_a_vanished_runtime_directory_is_not_an_error(self):
        with tempfile.TemporaryDirectory() as root:
            run = self.run_helper(Path(root) / "gone", ["hook", "claude"], "{}")
            self.assertEqual(0, run.returncode)
            self.assertEqual("", run.stdout)


# ----- the shim's no-op invariant (protocol 26.3) --------------------------------------------


class NoOpInvariant(unittest.TestCase):
    """Hooks installed in a settings file are read by every terminal on the machine, so with no
    channel they must do nothing at all."""

    def test_hook_prints_nothing_and_writes_nowhere(self):
        # The runtime dir is the one thing a shim could write to; with no helper in the
        # environment it stays empty.
        with tempfile.TemporaryDirectory() as root, relay_env(RELAY_RUNTIME_DIR=root):
            code, out = call(["PreToolUse"], '{"tool_name": "Bash"}')
            self.assertEqual(0, code)
            self.assertEqual("", out)
            self.assertEqual([], list(Path(root).iterdir()))

    def test_no_event_argument_is_a_no_op(self):
        with relay_env():
            code, out = call([], "{}")
        self.assertEqual((0, ""), (code, out))

    def test_statusline_still_prints_its_passthrough_line(self):
        payload = json.dumps({"model": {"display_name": "Claude Opus 4.6"}, "workspace": {"current_dir": "/w/proj"}})
        with relay_env(RELAY_GUEST_STATUSLINE="{model} · {dir}"):
            code, out = call(["statusline"], payload)
        self.assertEqual(0, code)
        self.assertEqual("Claude Opus 4.6 · proj\n", out)

    def test_statusline_with_no_model_still_prints_something(self):
        with relay_env():
            code, out = call(["statusline"], "")
        self.assertEqual(0, code)
        self.assertTrue(out.strip())


# ----- hook forwarding (protocol 26.4) --------------------------------------------------------


class HookForwarding(unittest.TestCase):
    def test_a_hook_becomes_one_channel_event(self):
        with tempfile.TemporaryDirectory() as root, relay_env(
                RELAY_GUEST_EVENT=str(HELPER), RELAY_RUNTIME_DIR=root, RELAY_SESSION_TOKEN="tok-1"):
            payload = json.dumps({"session_id": "abc", "message": "waiting for input"})
            code, out = call(["Notification"], payload)
            self.assertEqual(0, code)
            self.assertEqual("", out)          # the hook itself stays quiet
            envelope = read_envelope(Path(root))
        self.assertEqual(("hook", "claude", "tok-1"), (envelope["event"], envelope["guest"], envelope["token"]))
        self.assertEqual({"name": "Notification", "payload": {"session_id": "abc", "message": "waiting for input"}},
                         envelope["data"])

    def test_a_missing_helper_never_breaks_the_guest_run(self):
        with tempfile.TemporaryDirectory() as root, relay_env(
                RELAY_GUEST_EVENT=str(Path(root) / "not-there.py"), RELAY_RUNTIME_DIR=root,
                RELAY_SESSION_TOKEN="tok-1"):
            code, out = call(["Stop"], "{}")
        self.assertEqual((0, ""), (code, out))

    def test_the_guest_id_can_be_another_guest(self):
        with tempfile.TemporaryDirectory() as root, relay_env(
                RELAY_GUEST_EVENT=str(HELPER), RELAY_RUNTIME_DIR=root, RELAY_SESSION_TOKEN="tok-1",
                RELAY_GUEST_ID="codex"):
            call(["Stop"], "{}")
            envelope = read_envelope(Path(root))
        self.assertEqual("codex", envelope["guest"])


# ----- the permission question (protocol 26.4) ------------------------------------------------


class PermissionDecision(unittest.TestCase):
    """PreToolUse is answered by the user, never by the shim: the shim asks, waits for an answer
    that matches this exact question, and prints claude's own decision JSON."""

    def run_question(self, answer: dict | None, timeout: str = "5", token: str = "tok-1"):
        """Ask one permission question in a thread; answer it (or not) from here."""
        with tempfile.TemporaryDirectory() as root:
            runtime = Path(root)
            payload = json.dumps({"tool_name": "Bash", "tool_input": {"command": "rm -rf build"}})
            with relay_env(RELAY_GUEST_EVENT=str(HELPER), RELAY_RUNTIME_DIR=root,
                           RELAY_SESSION_TOKEN=token, RELAY_GUEST_PERMISSION_TIMEOUT=timeout), io_for(payload) as out:
                result: list[int] = []
                thread = threading.Thread(target=lambda: result.append(guest_hook.main(["PreToolUse"])))
                thread.start()
                if answer is not None:
                    self._answer_when_asked(runtime, answer)
                thread.join(10)
                self.assertFalse(thread.is_alive(), "the shim did not return")
                return result[0], out.getvalue()

    def _answer_when_asked(self, runtime: Path, answer: dict) -> None:
        """Wait for the shim's own write, then answer its exact sequence."""
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if (runtime / "guest.json").exists():
                envelope = read_envelope(runtime)
                if envelope["event"] == "hook":
                    break
            time.sleep(0.02)
        else:
            self.fail("the shim never asked")
        with (runtime / "guest-answer.json").open("w", encoding="utf-8") as handle:
            json.dump({"token": "tok-1", "sequence": envelope["sequence"], **answer}, handle)

    def test_the_user_allows_it(self):
        code, out = self.run_question({"decision": "allow"})
        self.assertEqual(0, code)
        self.assertEqual({"hookSpecificOutput": {"hookEventName": "PreToolUse",
                                                 "permissionDecision": "allow",
                                                 "permissionDecisionReason": "Allowed in Relay."}},
                         json.loads(out))

    def test_the_user_denies_it(self):
        code, out = self.run_question({"decision": "deny"})
        self.assertEqual(0, code)
        self.assertEqual("deny", json.loads(out)["hookSpecificOutput"]["permissionDecision"])

    def test_an_unanswered_question_falls_back_to_claude(self):
        """No answer: no decision JSON at all, so claude asks the way it always does. Nothing is
        ever approved on the user's behalf."""
        code, out = self.run_question(None, timeout="0.3")
        self.assertEqual((0, ""), (code, out))

    def test_an_answer_for_another_question_is_ignored(self):
        with tempfile.TemporaryDirectory() as root, relay_env(
                RELAY_GUEST_EVENT=str(HELPER), RELAY_RUNTIME_DIR=root, RELAY_SESSION_TOKEN="tok-1",
                RELAY_GUEST_PERMISSION_TIMEOUT="0.3"):
            # A stale answer from an earlier question, and one for another pane's token: neither
            # may be taken for this question's.
            for answer in ({"token": "tok-1", "sequence": "someone-elses", "decision": "allow"},
                           {"token": "another-pane", "sequence": "x", "decision": "allow"}):
                with (Path(root) / "guest-answer.json").open("w", encoding="utf-8") as handle:
                    json.dump(answer, handle)
                code, out = call(["PreToolUse"], '{"tool_name": "Bash"}')
                self.assertEqual((0, ""), (code, out))


# ----- the statusline fields (protocol 26.3) --------------------------------------------------


class StatuslineFields(unittest.TestCase):
    def fields(self, payload: dict) -> dict:
        with tempfile.TemporaryDirectory() as root, relay_env(
                RELAY_GUEST_EVENT=str(HELPER), RELAY_RUNTIME_DIR=root, RELAY_SESSION_TOKEN="tok-1"):
            call(["statusline"], json.dumps(payload))
            return read_envelope(Path(root))["data"]

    def test_model_and_context_share(self):
        data = self.fields({"model": {"display_name": "Claude Sonnet 4.5"},
                            "context_window": {"used_percentage": 42.4}})
        self.assertEqual({"model": "Claude Sonnet 4.5", "context_pct": 42}, data)

    def test_a_plain_model_string_and_the_older_context_key(self):
        self.assertEqual({"model": "opus", "context_pct": 7}, self.fields({"model": "opus", "context_pct": 7}))

    def test_the_context_key_currently_documented(self):
        self.assertEqual(13, self.fields({"context": {"used_percentage": 13}})["context_pct"])

    def test_unknown_stays_unknown(self):
        data = self.fields({"model": {"display_name": "Claude"}, "cost": {"total_lines_added": 3}})
        self.assertEqual({"model": "Claude"}, data)   # no context_pct at all, not a made-up zero

    def test_past_the_window_reads_as_full(self):
        self.assertEqual(100, self.fields({"exceeds_200k_tokens": True})["context_pct"])

    def test_a_boolean_is_not_a_percentage(self):
        self.assertNotIn("context_pct", self.fields({"context_pct": True}))

    def test_the_share_is_clamped(self):
        self.assertEqual(100, self.fields({"context_pct": 150})["context_pct"])
        self.assertEqual(0, self.fields({"context_pct": -4})["context_pct"])

    def test_the_passthrough_line_can_be_reworded(self):
        payload = {"model": {"display_name": "Opus"}, "workspace": {"project_dir": "/home/me/relay"}}
        with relay_env(RELAY_GUEST_STATUSLINE="{dir} · {model} · {session}"):
            code, out = call(["statusline"], json.dumps(payload))
        self.assertEqual("relay · Opus ·\n", out)   # the empty session leaves no trailing space

    def test_a_broken_template_falls_back_to_the_default(self):
        with relay_env(RELAY_GUEST_STATUSLINE="{model} · {nope}"):
            code, out = call(["statusline"], json.dumps({"model": "Opus", "cwd": "/tmp/x"}))
        self.assertEqual("Opus · x\n", out)


if __name__ == "__main__":
    unittest.main()
