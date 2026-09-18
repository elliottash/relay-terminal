"""Card #S5SH, docs/SSH-AND-MOSH.md sections 4 and 7: the router at a remote prompt, the agent's
`remote_session` context, and run_command's `host` over the user's ssh connection.

No network: run_command's ssh is a fake `ssh` on PATH that prints its argv, and the control
socket is a real unix socket bound in a temporary directory."""
import os
import socket
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import patch

from relay_core import remote_session, tool_labels
from relay_core.agent import format_context, validate_context
from relay_core.router import classify
from relay_core.tools import ToolExecutor

REMOTE = {"host": "filly"}


class RemoteRouterTests(unittest.TestCase):
    def route(self, text, mode="auto"):
        # An empty PATH and an unrelated cwd: nothing local may matter at a remote prompt.
        return classify(text, mode, known_commands=[], path="/nonexistent", cwd="/", remote=REMOTE)

    def test_command_shaped_lines_are_typed_on_the_host(self):
        for text in ["ls -la /srv", "htop", "sudo systemctl restart nginx", "cp a b", "make all",
                     "docker ps", "vim /etc/nginx/nginx.conf", "cd /srv && git status", "gti status",
                     "journalctl -u nginx", "tail -f /var/log/syslog", "go build ./...", "top", "exit",
                     "kubectl get pods in the namespace", "./deploy.sh --dry-run"]:
            with self.subTest(text=text):
                decision = self.route(text)
                self.assertEqual(decision.route, "shell")
                self.assertTrue(decision.valid)
                self.assertEqual(decision.invalid_reason, "")
                self.assertIn("typed on filly", decision.reason)
                self.assertEqual(decision.remote_host, "filly")
                self.assertEqual(decision.to_dict()["remote_host"], "filly")

    def test_never_not_found_locally(self):
        # htop is not on this (empty) PATH; locally the same line is "command not found".
        self.assertEqual(classify("htop", path="/nonexistent").route, "agent")
        for text in ["htop", "frobnicate --all", "/shell htop", "fi"]:
            with self.subTest(text=text):
                decision = self.route(text)
                self.assertNotIn("not found", decision.reason + decision.invalid_reason)

    def test_sentences_go_to_the_agent(self):
        for text in ["why is the disk full on this box", "continue", "ok", "yes", "Sounds good",
                     "yeah, do it", "don't restart it", "disk is full on this box", "nginx running?",
                     "the build is broken", "thanks", "explain the last error", "can you check the logs"]:
            with self.subTest(text=text):
                self.assertEqual(self.route(text).route, "agent")

    def test_english_command_sentences_still_ask(self):
        for text in ["find the big logs", "sort these by date"]:
            with self.subTest(text=text):
                decision = self.route(text)
                self.assertTrue(decision.needs_assist)
                self.assertEqual(decision.route, "agent")
        # `echo` takes literal text: the guess stays with the shell.
        self.assertEqual(self.route("echo the build is done").route, "shell")

    def test_explicit_prefixes_and_fixed_modes(self):
        self.assertEqual(self.route("/agent ls").route, "agent")
        self.assertIn("nothing is typed on filly", self.route("/agent ls").reason)
        self.assertEqual(self.route("/shell why is this slow").route, "shell")
        self.assertTrue(self.route("/shell why is this slow").agent_signal)
        self.assertEqual(self.route("ls", "agent").route, "agent")
        decision = self.route("htop", "shell")
        self.assertEqual((decision.route, decision.valid), ("shell", True))
        self.assertIn("typed on filly", decision.reason)

    def test_local_decisions_are_unchanged(self):
        for text in ["git status", "why is this failing", "frobnicate", "continue", ""]:
            with self.subTest(text=text):
                self.assertEqual(classify(text).to_dict(), classify(text, remote=None).to_dict())
                self.assertNotIn("remote_host", classify(text).to_dict())

    def test_bad_remote_is_refused(self):
        for remote in ["filly", {"host": ""}, {"host": "a b"}, {"host": 5}, {}]:
            with self.subTest(remote=remote):
                with self.assertRaises(ValueError):
                    classify("ls", remote=remote)

    def test_worker_routes_with_remote(self):
        import json
        import subprocess
        import sys
        root = Path(__file__).resolve().parents[1]
        payload = json.dumps({"type": "route", "id": "r1", "text": "htop", "remote": {"host": "filly"},
                              "path": "/nonexistent"}) + "\n" + json.dumps({"type": "shutdown"}) + "\n"
        proc = subprocess.run([sys.executable, "-S", str(root / "backend/worker.py")], input=payload,
                              text=True, capture_output=True, timeout=20, cwd=root,
                              env={**os.environ, "RELAY_KEYRING": "off"})
        events = [json.loads(line) for line in proc.stdout.splitlines()]
        route = next(e for e in events if e.get("event") == "route")
        self.assertEqual((route["route"], route["remote_host"]), ("shell", "filly"))


SESSION = {"program": "ssh", "host": "filly", "hostname": "65.109.126.152", "user": "elliott", "port": 22,
           "control_path": "/run/user/1000/relay-ssh/956d", "reachable": True, "cwd": "/srv/archive/tracelaw",
           "shell_integration": True, "at_prompt": True}


class RemoteContextTests(unittest.TestCase):
    def test_validation_keeps_known_fields_and_drops_unknown(self):
        clean = validate_context({"remote_session": {**SESSION, "future_field": [1, 2]}})["remote_session"]
        self.assertEqual(clean, SESSION)
        # remote_session alone is a context worth sending.
        self.assertIsNotNone(validate_context({"remote_session": {"host": "filly"}}))

    def test_validation_refuses_wrong_types_and_long_strings(self):
        for bad in [{"host": 5}, {"host": ""}, {"host": "-oProxyCommand=x"}, {"host": "a b"},
                    {**SESSION, "port": "22"}, {**SESSION, "port": 0}, {**SESSION, "port": True},
                    {**SESSION, "reachable": "yes"}, {**SESSION, "cwd": "x" * 4097},
                    {**SESSION, "control_path": "relative/socket"}, {**SESSION, "user": "a\nb"}, "filly"]:
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    validate_context({"remote_session": bad})

    def test_note_says_which_machine_is_which(self):
        note = format_context({"foreground_program": "ssh filly", "terminal_cwd": "/home/elliott",
                               "remote_session": SESSION})
        self.assertIn("logged into elliott@filly (65.109.126.152) via ssh", note)
        self.assertIn("`/srv/archive/tracelaw`", note)
        self.assertIn("Plain run_command runs on this local machine", note)
        self.assertIn('pass host: "filly" to run_command', note)
        self.assertIn("not read_file", note)
        # The old line told the model its run_command "cannot interact with the program" — true of
        # the session's screen, but not of the host any more.
        self.assertNotIn("cannot interact with the program", note)
        self.assertIn("cannot see the ssh session's screen", note)

    def test_note_when_the_connection_cannot_be_shared(self):
        note = format_context({"foreground_program": "mosh filly",
                               "remote_session": {**SESSION, "program": "mosh", "reachable": False, "cwd": None}})
        self.assertIn("cannot share this mosh connection", note)
        self.assertIn("run_in_terminal", note)
        self.assertIn("directory is unknown", note)
        self.assertNotIn('pass host: "filly"', note)

    def test_control_characters_are_stripped(self):
        note = format_context({"remote_session": {**SESSION, "cwd": "/srv/\x1b[31mevil"}})
        self.assertNotIn("\x1b", note)


class RemoteRunCommandTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        bindir = self.root / "bin"
        bindir.mkdir()
        fake = bindir / "ssh"
        fake.write_text('#!/bin/sh\nfor a in "$@"; do printf "[%s]\\n" "$a"; done\nexit "${FAKE_SSH_EXIT:-0}"\n')
        fake.chmod(0o755)
        self.socket_path = str(self.root / "ctl")
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.bind(self.socket_path)
        self.env = patch.dict(os.environ, {"PATH": f"{bindir}:{os.environ.get('PATH', '')}"})
        self.env.start()
        self.events = []
        self.tools = ToolExecutor(self.tmp.name, self.events.append, threading.Event())
        self.session = {**SESSION, "control_path": self.socket_path}
        self.tools.set_remote_session(remote_session.validate(self.session))

    def tearDown(self):
        self.env.stop()
        self.sock.close()
        self.tools.shutdown()
        self.tmp.cleanup()

    def run_remote(self, **args):
        return self.tools.execute(self.tools.prepare("run_command", {"host": "filly", **args}))

    def test_argv_uses_the_users_connection(self):
        result = self.run_remote(command="ls -la | head")
        self.assertEqual(result["exit_code"], 0)
        self.assertEqual(result["host"], "filly")
        self.assertEqual(result["output"].splitlines()[:11],
                         ["[-S]", f"[{self.socket_path}]", "[-o]", "[ControlMaster=no]", "[-o]", "[BatchMode=yes]",
                          "[-o]", "[ConnectTimeout=10]", "[-T]", "[filly]", "[--]"])
        self.assertTrue(result["output"].endswith("[cd /srv/archive/tracelaw || exit 1\nls -la | head]\n"))

    def test_cwd_is_quoted_and_defaults(self):
        self.assertIn("[cd '/srv/it'\"'\"'s here' || exit 1\npwd]",
                      self.run_remote(command="pwd", cwd="/srv/it's here")["output"])
        # No cwd anywhere: the command starts in the remote home.
        self.tools.set_remote_session(remote_session.validate({**self.session, "cwd": None}))
        self.assertTrue(self.run_remote(command="pwd")["output"].endswith("[--]\n[pwd]\n"))

    def test_same_job_machinery(self):
        with patch.dict(os.environ, {"FAKE_SSH_EXIT": "3"}):
            result = self.run_remote(command="false", timeout_seconds=5)
        self.assertEqual(result["exit_code"], 3)
        self.assertIn("job_id", result)
        with patch.dict(os.environ, {"FAKE_SSH_EXIT": "255"}):
            result = self.run_remote(command="true")
        self.assertIn("connection to filly failed or closed", result["note"])

    def test_host_only_in_the_schema_while_logged_in(self):
        props = self.tools.tools()[0]["function"]["parameters"]["properties"]
        self.assertIn("host", props)
        self.tools.set_remote_session(None)
        self.assertNotIn("host", self.tools.tools()[0]["function"]["parameters"]["properties"])

    def test_refusals_the_model_can_act_on(self):
        with self.assertRaisesRegex(ValueError, r'host "other" is not the host .* logged into \(filly\)'):
            self.tools.prepare("run_command", {"command": "ls", "host": "other"})
        self.tools.set_remote_session(remote_session.validate({**self.session, "reachable": False}))
        with self.assertRaisesRegex(ValueError, "can't be shared"):
            self.tools.prepare("run_command", {"command": "ls", "host": "filly"})
        self.tools.set_remote_session(remote_session.validate({**self.session, "control_path": ""}))
        with self.assertRaisesRegex(ValueError, "can't be shared"):
            self.tools.prepare("run_command", {"command": "ls", "host": "filly"})
        self.tools.set_remote_session(None)
        with self.assertRaisesRegex(ValueError, "not logged into any host"):
            self.tools.prepare("run_command", {"command": "ls", "host": "filly"})
        with self.assertRaisesRegex(ValueError, "host must be text"):
            self.tools.prepare("run_command", {"command": "ls", "host": 5})

    def test_a_closed_socket_runs_nothing(self):
        prepared = self.tools.prepare("run_command", {"command": "ls", "host": "filly"})
        os.unlink(self.socket_path)
        with self.assertRaisesRegex(ValueError, "has closed"):
            self.tools.execute(prepared)

    def test_empty_host_is_a_local_run(self):
        result = self.tools.execute(self.tools.prepare("run_command", {"command": "printf here", "host": ""}))
        self.assertEqual(result["output"], "here")
        self.assertNotIn("host", result)

    def test_labels_and_detail_name_the_host(self):
        args = {"command": "ls -la", "host": "filly"}
        self.assertEqual(tool_labels.started_label("run_command", args)["title"], "ran ls -la on filly")
        self.assertEqual(tool_labels.started_label("run_command", args)["running"], "running ls -la on filly")
        self.assertEqual(tool_labels.result_label("run_command", {**args, "background": True},
                                                  {"output": "", "still_running": True, "job_id": "job-1"})["title"],
                         "started job: ls -la on filly")
        sections = tool_labels.detail("run_command", args, {"output": "x", "exit_code": 0})
        self.assertIn("host", [s["heading"] for s in sections])
        self.assertEqual(tool_labels.started_label("run_command", {"command": "ls"})["title"], "ran ls")

    def test_handed_back_jobs_carry_the_host(self):
        # The fake ssh sleeps: an argv-level stand-in for a slow remote command.
        (self.root / "bin" / "ssh").write_text("#!/bin/sh\nsleep 5\n")
        result = self.run_remote(command="sleep 5", timeout_seconds=1)
        self.assertTrue(result["still_running"])
        self.assertEqual(result["host"], "filly")
        self.assertEqual(self.tools.jobs.snapshot()[0]["host"], "filly")


if __name__ == "__main__":
    unittest.main()
