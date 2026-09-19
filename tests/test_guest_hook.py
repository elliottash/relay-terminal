"""The Claude guest shim (GT7X, protocol 26.3/26.4).

The channel is a spool directory, not a slot: `relay_core.guest_hook` writes one file per event
into `$RELAY_GUEST_EVENT` and the pane deletes each as it handles it. These tests speak the pane's
side of that contract directly — list the directory, sort by name, read, answer — so they fail for
the same reasons the pane would.
"""
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

BACKEND = str(Path(__file__).resolve().parents[1] / "backend")
SHIM = Path(BACKEND) / "relay_core" / "guest_hook.py"
WRITER = Path(__file__).resolve().parents[1] / "shell" / "guest-event.py"
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
def pane(token="tok-1", **extra):
    """A pane's runtime directory and the environment a shell started in it inherits.

    Yields (runtime, events); the spool is the directory the pane exports as RELAY_GUEST_EVENT,
    exactly as Pane::startTerminal does.
    """
    with tempfile.TemporaryDirectory() as root:
        runtime = Path(root)
        events = runtime / "guest-events"
        with relay_env(RELAY_GUEST_EVENT=str(events), RELAY_RUNTIME_DIR=str(runtime),
                       RELAY_SESSION_TOKEN=token, **extra):
            yield runtime, events


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


def spooled(events: Path) -> list[Path]:
    """What the pane would read, in the order it would read it: the directory sorted by name."""
    if not events.exists():
        return []
    return sorted((path for path in events.iterdir() if path.suffix == ".json"), key=lambda p: p.name)


def envelopes(events: Path) -> list[dict]:
    return [json.loads(path.read_text(encoding="utf-8")) for path in spooled(events)]


def one_envelope(test: unittest.TestCase, events: Path) -> dict:
    found = envelopes(events)
    test.assertEqual(1, len(found), found)
    return found[0]


# ----- the channel's one writer (protocol 26.3) -----------------------------------------------


class Writer(unittest.TestCase):
    """`shell/guest-event.py` is where the spool's naming, its atomicity and its size cap live,
    for every surface on the channel: this shim, the IDE bridge sidecar, the codex tail."""

    def run_writer(self, runtime: Path, argv, payload: str, token="tok-1", env=None):
        environment = {"PATH": os.environ.get("PATH", os.defpath),
                       "RELAY_GUEST_EVENT": str(runtime / "guest-events"),
                       "RELAY_RUNTIME_DIR": str(runtime), "RELAY_SESSION_TOKEN": token}
        environment.update(env or {})
        return subprocess.run([sys.executable, str(WRITER), *argv], input=payload, text=True,
                              capture_output=True, env=environment)

    def test_the_command_line_still_works_for_a_caller_that_is_not_python(self):
        with tempfile.TemporaryDirectory() as root:
            runtime = Path(root)
            first = self.run_writer(runtime, ["statusline", "claude"], '{"model": "M"}')
            self.assertEqual((0, "", ""), (first.returncode, first.stdout, first.stderr))
            second = self.run_writer(runtime, ["state", "codex", "seq-42"], '{"busy": true}')
            self.assertEqual(0, second.returncode, second.stderr)
            found = envelopes(runtime / "guest-events")
        self.assertEqual(["statusline", "state"], [e["event"] for e in found])
        self.assertEqual(["claude", "codex"], [e["guest"] for e in found])
        self.assertEqual("seq-42", found[1]["sequence"])   # a caller may pin its own
        self.assertNotEqual(found[0]["sequence"], found[1]["sequence"])

    def test_without_a_pane_it_writes_nowhere(self):
        with tempfile.TemporaryDirectory() as root:
            run = subprocess.run([sys.executable, str(WRITER), "hook", "claude"], input="{}",
                                 text=True, capture_output=True,
                                 env={"PATH": os.environ.get("PATH", os.defpath)})
            self.assertEqual((0, "", ""), (run.returncode, run.stdout, run.stderr))
            self.assertEqual([], list(Path(root).iterdir()))

    def test_an_envelope_past_the_panes_cap_is_written_without_its_data(self):
        """The pane deletes a file over 256 KiB unread, so the writer never makes one: a question
        the user can still answer beats a file that is refused."""
        with tempfile.TemporaryDirectory() as root:
            runtime = Path(root)
            self.run_writer(runtime, ["hook", "claude"], json.dumps({"payload": "y" * 400_000}))
            spool = spooled(runtime / "guest-events")
            self.assertEqual(1, len(spool))
            self.assertLess(spool[0].stat().st_size, 256 * 1024)
            self.assertEqual({"relay_truncated": True}, envelopes(runtime / "guest-events")[0]["data"])

    def test_the_shim_writes_through_it_rather_than_spawning_it(self):
        """Two processes per statusline tick to write one small file was half the shim's cost."""
        with pane() as (_runtime, events), mock.patch.object(guest_hook, "_writer", None):
            with mock.patch.dict(os.environ, {"RELAY_GUEST_WRITER": str(WRITER)}):
                call(["Stop"], "{}")
            self.assertEqual(1, len(spooled(events)))
        self.assertEqual("guest-event.py", Path(guest_hook.WRITER).name)

    def test_a_missing_writer_never_breaks_the_guest_run(self):
        with pane() as (_runtime, events), mock.patch.object(guest_hook, "_writer", None), \
                mock.patch.dict(os.environ, {"RELAY_GUEST_WRITER": "/nowhere/guest-event.py"}):
            self.assertEqual((0, ""), call(["Stop"], "{}"))
            self.assertEqual((0, ""), call(["PermissionRequest"], '{"tool_name": "Bash"}'))
            self.assertEqual([], spooled(events))
        guest_hook._writer = None


# ----- the spool (protocol 26.3) --------------------------------------------------------------


class Spool(unittest.TestCase):
    def test_the_shim_creates_a_private_spool_and_writes_one_file_per_event(self):
        with pane() as (_runtime, events):
            call(["Stop"], "{}")
            call(["statusline"], '{"model": "M"}')
            call(["UserPromptSubmit"], "{}")
            self.assertEqual(0o700, os.stat(events).st_mode & 0o777)
            names = [path.name for path in spooled(events)]
            self.assertEqual(3, len(names))
            self.assertEqual(["hook", "statusline", "hook"], [e["event"] for e in envelopes(events)])
        # The name carries the write time first, so sorting the directory replays the order.
        self.assertEqual(sorted(names), names)
        for name in names:
            stamp, pid, rest = name.split("-", 2)
            self.assertEqual(20, len(stamp))
            self.assertEqual(str(os.getpid()), pid)
            self.assertTrue(rest.endswith(".json"))

    def test_a_statusline_tick_no_longer_overwrites_a_question(self):
        """The whole reason for the spool: with one `guest.json` slot, a statusline landing just
        after a permission request replaced it, and the shim waited out its timeout for an answer
        to a question the pane had never been shown (review of 51587e3)."""
        with pane(RELAY_GUEST_PERMISSION_TIMEOUT="0.3") as (_runtime, events):
            call(["PermissionRequest"], '{"tool_name": "Bash", "tool_input": {"command": "ls"}}')
            call(["statusline"], '{"model": "Opus"}')
            found = envelopes(events)
        self.assertEqual(["hook", "statusline"], [e["event"] for e in found])
        self.assertEqual("PermissionRequest", found[0]["data"]["name"])

    def test_the_envelope_is_the_protocol_one(self):
        with pane(token="tok-9") as (_runtime, events):
            call(["Notification"], '{"session_id": "abc", "message": "waiting for input"}')
            envelope = one_envelope(self, events)
        self.assertEqual({"token", "sequence", "event", "guest", "data"}, set(envelope))
        self.assertEqual(("hook", "claude", "tok-9"),
                         (envelope["event"], envelope["guest"], envelope["token"]))
        self.assertEqual({"name": "Notification",
                          "payload": {"session_id": "abc", "message": "waiting for input"}},
                         envelope["data"])

    def test_every_event_gets_its_own_sequence(self):
        with pane() as (_runtime, events):
            call(["Stop"], "{}")
            call(["Stop"], "{}")
            found = envelopes(events)
        self.assertEqual(2, len({e["sequence"] for e in found}))

    def test_the_guest_id_can_be_another_guest(self):
        with pane(RELAY_GUEST_ID="codex") as (_runtime, events):
            call(["Stop"], "{}")
            self.assertEqual("codex", one_envelope(self, events)["guest"])

    def test_malformed_or_missing_stdin_is_an_empty_payload_not_a_crash(self):
        for payload in ("", "not json", "[1, 2]", "null"):
            with self.subTest(payload=payload), pane() as (_runtime, events):
                code, out = call(["Stop"], payload)
                self.assertEqual((0, ""), (code, out))
                self.assertEqual({}, one_envelope(self, events)["data"]["payload"])

    def test_a_spool_that_cannot_be_written_never_breaks_the_guest_run(self):
        with tempfile.TemporaryDirectory() as root:
            blocked = Path(root) / "not-a-directory"
            blocked.write_text("")
            with relay_env(RELAY_GUEST_EVENT=str(blocked / "events"), RELAY_RUNTIME_DIR=root,
                           RELAY_SESSION_TOKEN="tok-1"):
                self.assertEqual((0, ""), call(["Stop"], "{}"))
                # …and a question whose write failed falls back to claude's own asking at once.
                self.assertEqual((0, ""), call(["PermissionRequest"], '{"tool_name": "Bash"}'))


# ----- the shim's no-op invariant (protocol 26.3) --------------------------------------------


class NoOpInvariant(unittest.TestCase):
    """The hooks live in a settings file every claude started in that project reads, so with no
    channel in the environment they must do nothing at all."""

    def test_hook_prints_nothing_and_writes_nowhere(self):
        with tempfile.TemporaryDirectory() as root, relay_env(RELAY_RUNTIME_DIR=root):
            code, out = call(["PermissionRequest"], '{"tool_name": "Bash"}')
            self.assertEqual(0, code)
            self.assertEqual("", out)
            self.assertEqual([], list(Path(root).iterdir()))

    def test_no_event_argument_is_a_no_op(self):
        with relay_env():
            code, out = call([], "{}")
        self.assertEqual((0, ""), (code, out))

    def test_the_installers_marker_argument_is_not_an_event(self):
        """The settings command ends in `--relay-guest`; the shim must read the event, not it."""
        with pane() as (_runtime, events):
            call(["Stop", "--relay-guest"], "{}")
            self.assertEqual("Stop", one_envelope(self, events)["data"]["name"])

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

    def test_the_shim_runs_as_a_script_without_pythonpath(self):
        """The installed command is `"$RELAY_PYTHON" "$RELAY_BACKEND_DIR/relay_core/guest_hook.py"`,
        by absolute path: nothing may make it depend on PYTHONPATH or on a package import."""
        environment = {"PATH": os.environ.get("PATH", os.defpath)}
        run = subprocess.run([sys.executable, str(SHIM), "statusline", "--relay-guest"],
                             input='{"model": "Opus", "cwd": "/tmp/x"}', text=True,
                             capture_output=True, env=environment)
        self.assertEqual((0, "Opus · x\n", ""), (run.returncode, run.stdout, run.stderr))


# ----- the permission question (protocol 26.4) ------------------------------------------------


class PermissionDecision(unittest.TestCase):
    """`PermissionRequest` — the hook claude sends only when it is really about to ask — is
    answered by the user, never by the shim: the shim asks, waits for the answer file its own
    question names, and prints claude's own decision JSON."""

    def run_question(self, answer: dict | None, timeout: str = "5", token: str = "tok-1"):
        """Ask one permission question in a thread; answer it (or not) from here."""
        with pane(RELAY_GUEST_PERMISSION_TIMEOUT=timeout) as (runtime, events):
            payload = json.dumps({"hook_event_name": "PermissionRequest", "tool_name": "Bash",
                                  "tool_input": {"command": "rm -rf build"}})
            with io_for(payload) as out:
                result: list[int] = []
                thread = threading.Thread(target=lambda: result.append(guest_hook.main(["PermissionRequest"])))
                thread.start()
                sequence = self._wait_for_question(events)
                if answer is not None:
                    self._answer(runtime, sequence, {"token": token, **answer})
                thread.join(10)
                self.assertFalse(thread.is_alive(), "the shim did not return")
                answered = not (runtime / "guest-answers" / (sequence + ".json")).exists()
                return result[0], out.getvalue(), answered

    def _wait_for_question(self, events: Path) -> str:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            found = envelopes(events)
            if found and found[0]["event"] == "hook":
                self.assertEqual("PermissionRequest", found[0]["data"]["name"])
                return found[0]["sequence"]
            time.sleep(0.02)
        self.fail("the shim never asked")

    def _answer(self, runtime: Path, sequence: str, answer: dict) -> None:
        answers = runtime / "guest-answers"
        answers.mkdir(mode=0o700, exist_ok=True)
        (answers / (sequence + ".json")).write_text(json.dumps(answer), encoding="utf-8")

    def test_the_user_allows_it(self):
        code, out, answered = self.run_question({"decision": "allow"})
        self.assertEqual(0, code)
        # Claude Code's PermissionRequest shape: decision.behavior, not PreToolUse's
        # permissionDecision. The two hooks do not share an output schema.
        self.assertEqual({"hookSpecificOutput": {"hookEventName": "PermissionRequest",
                                                 "decision": {"behavior": "allow"}}},
                         json.loads(out))
        self.assertTrue(answered, "the shim must delete the answer file it read")

    def test_the_user_denies_it(self):
        code, out, _answered = self.run_question({"decision": "deny"})
        self.assertEqual(0, code)
        self.assertEqual("deny", json.loads(out)["hookSpecificOutput"]["decision"]["behavior"])

    def test_an_unanswered_question_falls_back_to_claude(self):
        """No answer: no decision JSON at all, so claude asks the way it always does. Nothing is
        ever approved on the user's behalf."""
        code, out, _answered = self.run_question(None, timeout="0.3")
        self.assertEqual((0, ""), (code, out))

    def test_an_answer_from_another_pane_is_ignored(self):
        code, out, _answered = self.run_question({"decision": "allow"}, timeout="0.6", token="another-pane")
        self.assertEqual((0, ""), (code, out))

    def test_an_answer_for_another_question_is_not_read(self):
        """Answers are one file per question id, so a stale one cannot be taken for this one."""
        with pane(RELAY_GUEST_PERMISSION_TIMEOUT="0.3") as (runtime, _events):
            answers = runtime / "guest-answers"
            answers.mkdir()
            (answers / "someone-elses.json").write_text(
                json.dumps({"token": "tok-1", "decision": "allow"}), encoding="utf-8")
            code, out = call(["PermissionRequest"], '{"tool_name": "Bash"}')
            self.assertEqual((0, ""), (code, out))
            self.assertTrue((answers / "someone-elses.json").exists())

    def test_the_other_hooks_never_wait(self):
        """Only PermissionRequest holds claude up; a Stop or a Notification with no pane
        answering it must return at once, not after the permission timeout."""
        with pane(RELAY_GUEST_PERMISSION_TIMEOUT="30") as (_runtime, _events):
            started = time.monotonic()
            for event in ("Stop", "UserPromptSubmit", "Notification", "PreToolUse"):
                self.assertEqual((0, ""), call([event], '{"tool_name": "Bash"}'))
            self.assertLess(time.monotonic() - started, 5)


# ----- what one event may carry (protocol 26.3) ------------------------------------------------


class PayloadCap(unittest.TestCase):
    """The pane deletes an event file over 256 KiB unread, so a `Write` holding a whole file — or
    a hook payload anywhere near the shim's 1 MiB stdin limit — must arrive cut down rather than
    not at all (review of 51587e3)."""

    LIMIT = 256 * 1024

    def send(self, payload: dict) -> tuple[dict, int]:
        with pane() as (_runtime, events):
            call(["Stop"], json.dumps(payload))
            path = spooled(events)[0]
            return json.loads(path.read_text(encoding="utf-8"))["data"]["payload"], path.stat().st_size

    def test_a_long_field_is_cut_with_a_marker(self):
        data, size = self.send({"tool_name": "Write",
                                "tool_input": {"file_path": "/w/x.py", "content": "y" * 900_000}})
        self.assertLess(size, self.LIMIT)
        self.assertEqual("/w/x.py", data["tool_input"]["file_path"])
        self.assertTrue(data["tool_input"]["content"].endswith(guest_hook.TRUNCATED))
        self.assertLess(len(data["tool_input"]["content"]), guest_hook.MAX_FIELD_CHARS + 64)
        self.assertEqual("Write", data["tool_name"])

    def test_many_long_fields_cost_the_tool_input_but_not_the_question(self):
        huge = {"field-%d" % index: "z" * 40_000 for index in range(60)}
        data, size = self.send({"hook_event_name": "PermissionRequest", "tool_name": "Edit",
                                "session_id": "s-1", "tool_input": huge})
        self.assertLess(size, self.LIMIT)
        self.assertNotIn("tool_input", data)
        self.assertTrue(data["relay_truncated"])
        self.assertEqual(("Edit", "s-1"), (data["tool_name"], data["session_id"]))

    def test_a_small_payload_is_untouched(self):
        payload = {"tool_name": "Bash", "tool_input": {"command": "ls -la"}, "cwd": "/w"}
        data, _size = self.send(payload)
        self.assertEqual(payload, data)

    def test_a_payload_past_the_stdin_ceiling_is_still_a_question(self):
        """Whatever the ceiling is, a payload above it arrives as half a JSON document and parses
        as nothing. The question must still appear — with nothing in it — rather than the guest
        blocking on a request the pane never shows."""
        with pane() as (_runtime, events), mock.patch.object(guest_hook, "MAX_STDIN", 64):
            code, out = call(["Stop"], json.dumps({"tool_name": "Write", "tool_input": {"c": "y" * 4000}}))
            self.assertEqual((0, ""), (code, out))
            self.assertEqual({"relay_truncated": True}, one_envelope(self, events)["data"]["payload"])

    def test_deep_nesting_does_not_recurse_without_end(self):
        nested: dict = {"leaf": 1}
        for _ in range(200):
            nested = {"down": nested}
        data, size = self.send({"tool_name": "X", "tool_input": nested})
        self.assertLess(size, self.LIMIT)
        self.assertIn("tool_name", data)


# ----- the statusline fields (protocol 26.3) --------------------------------------------------


class StatuslineFields(unittest.TestCase):
    def fields(self, payload: dict) -> dict:
        with pane() as (_runtime, events):
            call(["statusline"], json.dumps(payload))
            return one_envelope(self, events)["data"]

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
