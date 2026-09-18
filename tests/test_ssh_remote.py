"""Card #S5SH, docs/SSH-AND-MOSH.md sections 4 and 7: the router at a remote prompt, the agent's
`remote_session` context, and `host` on run_command and on the file tools — all over the user's
own ssh connection.

No network: `ssh` is a fake on PATH (one that prints its argv and stdin, one that runs the script
it was handed, as the remote login shell would), and the control socket is a real unix socket bound
in a temporary directory. A directory in that temporary tree stands in for the remote home."""
import hashlib
import os
import socket
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import patch

from relay_core import remote_files, remote_session, tool_labels
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
        self.assertIn("run_command and the file tools", note)
        self.assertIn("work on this local machine, not on filly", note)
        self.assertIn('pass host: "filly" to run_command', note)
        self.assertIn('take the same host: "filly"', note)
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


class RemoteFileToolTests(unittest.TestCase):
    """The file tools on the host (card #S5SH, docs/SSH-AND-MOSH.md section 7).

    Two fake `ssh` scripts, both on PATH and neither touching the network: `echo` prints its argv
    and its stdin, so a test can read the exact command line and see that content travels on stdin;
    `exec` runs the last argument with `sh -c`, so a test can watch a real temp-file-and-mv write
    happen in a directory that stands in for the remote home."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        # The remote side: a home directory, and a "project" the user's shell is in outside it.
        self.home = self.root / "remote-home"
        (self.home / "sub").mkdir(parents=True)
        self.elsewhere = self.root / "srv"
        self.elsewhere.mkdir()
        self.socket_path = str(self.root / "ctl")
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.bind(self.socket_path)
        self.fake_ssh("exec")
        self.env = patch.dict(os.environ, {"PATH": f"{self.bin}:{os.environ.get('PATH', '')}",
                                           "HOME": str(self.home)})
        self.env.start()
        self.events = []
        self.tools = ToolExecutor(self.tmp.name, self.events.append, threading.Event())
        self.session = {**SESSION, "control_path": self.socket_path, "cwd": str(self.home)}
        self.use(self.session)

    def tearDown(self):
        self.env.stop()
        self.sock.close()
        self.tools.shutdown()
        self.tmp.cleanup()

    def use(self, session):
        self.tools.set_remote_session(remote_session.validate(session))

    def fake_ssh(self, kind):
        """`echo`: print argv then stdin. `exec`: run the script, as the remote login shell would."""
        script = ('#!/bin/sh\nfor a in "$@"; do printf "[%s]\\n" "$a"; done\n'
                  'printf -- "--stdin--\\n"\ncat\n' if kind == "echo" else
                  '#!/bin/sh\nfor a in "$@"; do s=$a; done\nexec sh -c "$s"\n')
        (self.bin / "ssh").write_text(script)
        (self.bin / "ssh").chmod(0o755)

    def call(self, name, **args):
        return self.tools.execute(self.tools.prepare(name, {"host": "filly", **args}))

    # ----- the command line, and how content gets there ---------------------------------

    def test_argv_is_run_commands_own(self):
        self.fake_ssh("echo")
        result = self.call("read_file", path="notes.md")
        printed = result["content"]
        self.assertEqual(printed.splitlines()[:11],
                         ["[-S]", f"[{self.socket_path}]", "[-o]", "[ControlMaster=no]", "[-o]",
                          "[BatchMode=yes]", "[-o]", "[ConnectTimeout=10]", "[-T]", "[filly]", "[--]"])
        # A relative path: the script is run in the remote shell's directory, inside `sh -c`.
        self.assertIn(f"[cd {self.home} || exit 1\nsh -c '", printed)
        self.assertIn('cat -- "$p" | head -c 131073', printed)
        self.assertIn("exit 78", printed)

    def test_an_absolute_path_needs_no_cd(self):
        self.fake_ssh("echo")
        result = self.call("read_file", path="/etc/hosts")
        self.assertNotIn("cd ", result["content"].split("--stdin--")[0].splitlines()[-1])

    def test_the_write_script_never_carries_the_content(self):
        awkward = "port = 8080 # $(rm -rf /) 'quoted'\n"
        script = remote_files.write_script("sub/app.conf", str(self.home))
        self.assertNotIn("8080", script)
        self.assertIn('cat > "$t"', script)          # the content is read from ssh's stdin
        self.assertIn('mv -- "$t" "$p"', script)     # and only then does the file change
        self.assertIn('chmod --reference="$p"', script)
        self.assertNotIn(awkward, " ".join(remote_files.argv(
            {**self.session, "host": "filly"}, script, str(self.home))))

    def test_stdin_carries_the_bytes(self):
        seen = {}
        real_run = remote_files.run

        def watch(session, script, **kwargs):
            seen.setdefault("stdin", []).append(kwargs.get("stdin", b""))
            seen.setdefault("script", []).append(script)
            return real_run(session, script, **kwargs)

        with patch.object(remote_files, "run", watch):
            self.call("write_file", path="sub/app.conf", content="alpha\n")
        self.assertEqual(seen["stdin"][-1], b"alpha\n")
        self.assertNotIn("alpha", seen["script"][-1])
        self.assertEqual((self.home / "sub/app.conf").read_text(), "alpha\n")

    # ----- reading ----------------------------------------------------------------------

    def test_read_returns_the_file_and_names_the_host(self):
        (self.home / "notes.md").write_text("# Notes\nbody\n")
        result = self.call("read_file", path="notes.md")
        self.assertEqual(result["content"], "# Notes\nbody\n")
        self.assertEqual(result["host"], "filly")
        self.assertEqual(result["path"], "notes.md")
        self.assertEqual(result["sha256"], hashlib.sha256(b"# Notes\nbody\n").hexdigest())

    def test_read_refuses_what_the_local_read_refuses(self):
        (self.home / "big.bin").write_bytes(b"x" * 200000)
        with self.assertRaisesRegex(ValueError, "128 KiB"):
            self.call("read_file", path="big.bin")
        (self.home / "binary").write_bytes(b"abc\x00def")
        with self.assertRaisesRegex(ValueError, "too large or binary"):
            self.call("read_file", path="binary")
        (self.home / "latin").write_bytes(b"caf\xe9\n")
        with self.assertRaisesRegex(ValueError, "UTF-8"):
            self.call("read_file", path="latin")
        with self.assertRaisesRegex(ValueError, "No such file or directory on filly"):
            self.call("read_file", path="gone.md")
        os.symlink(self.home / "notes.md", self.home / "link.md")
        with self.assertRaisesRegex(ValueError, "symlink"):
            self.call("read_file", path="link.md")
        with self.assertRaisesRegex(ValueError, "Only regular files"):
            self.call("read_file", path="sub")

    def test_list_directory_has_the_local_shape(self):
        (self.home / "a.txt").write_text("a")
        os.symlink(self.home / "a.txt", self.home / "b-link")
        result = self.call("list_directory", path="~")
        self.assertEqual(result["host"], "filly")
        self.assertFalse(result["truncated"])
        self.assertEqual([e["name"] for e in result["entries"]], ["a.txt", "b-link", "sub"])
        self.assertEqual([e["type"] for e in result["entries"]], ["file", "symlink", "directory"])
        for i in range(210):
            (self.home / "sub" / f"f{i:03d}").write_text("")
        full = self.call("list_directory", path="sub")
        self.assertTrue(full["truncated"])
        self.assertEqual(len(full["entries"]), 200)
        with self.assertRaisesRegex(ValueError, "not a directory"):
            self.call("list_directory", path="a.txt")

    # ----- writing ----------------------------------------------------------------------

    def test_write_replaces_in_place_and_keeps_the_mode(self):
        target = self.home / "sub" / "run.sh"
        target.write_text("old\n")
        target.chmod(0o750)
        result = self.call("write_file", path="sub/run.sh", content="new\n")
        self.assertEqual(target.read_text(), "new\n")
        self.assertEqual(oct(target.stat().st_mode & 0o777), oct(0o750))
        self.assertEqual((result["host"], result["created"], result["written_bytes"]), ("filly", False, 4))
        # Nothing left beside it: the temp file is moved, never abandoned.
        self.assertEqual(sorted(p.name for p in (self.home / "sub").iterdir()), ["run.sh"])

    def test_a_new_file_is_private_and_needs_its_parent(self):
        result = self.call("write_file", path="sub/fresh.txt", content="hi\n")
        self.assertTrue(result["created"])
        self.assertEqual(oct((self.home / "sub/fresh.txt").stat().st_mode & 0o777), oct(0o600))
        with self.assertRaisesRegex(ValueError, "Parent directory must already exist"):
            self.call("write_file", path="sub/deeper/tree.txt", content="hi\n")

    def test_edit_matches_the_local_tool(self):
        text = "alpha\nbeta\nalpha\n"
        (self.home / "f.txt").write_text(text)
        local = self.root / "f.txt"
        local.write_text(text)

        def both(**args):
            """The same edit, remote and local, so the errors can be compared byte for byte."""
            remote = self.assertRaises(ValueError)
            with remote:
                self.call("edit_file", path="f.txt", **args)
            here = self.assertRaises(ValueError)
            with here:
                self.tools.execute(self.tools.prepare("edit_file", {"path": "f.txt", **args}))
            return str(remote.exception), str(here.exception)

        missing = both(old_string="gamma", new_string="x")
        self.assertEqual(*missing)
        self.assertIn("was not found in the file", missing[0])
        twice = both(old_string="alpha", new_string="x")
        self.assertEqual(*twice)
        self.assertIn("occurs 2 times", twice[0])
        self.assertEqual(*both(old_string="", new_string="x"))
        self.assertEqual(*both(old_string="beta", new_string="beta"))
        result = self.call("edit_file", path="f.txt", old_string="alpha", new_string="gamma",
                           replace_all=True)
        self.assertEqual((self.home / "f.txt").read_text(), "gamma\nbeta\ngamma\n")
        self.assertEqual((result["replacements"], result["added"], result["removed"], result["host"]),
                         (2, 2, 2, "filly"))
        with self.assertRaisesRegex(ValueError, "needs a file that already exists"):
            self.call("edit_file", path="nothing.txt", old_string="a", new_string="b")

    def test_a_file_that_changes_under_the_diff_is_not_overwritten(self):
        (self.home / "f.txt").write_text("one\n")
        prepared = self.tools.prepare("write_file", {"host": "filly", "path": "f.txt", "content": "two\n"})
        (self.home / "f.txt").write_text("someone else\n")
        with self.assertRaisesRegex(ValueError, "changed while the write was prepared"):
            self.tools.execute(prepared)
        self.assertEqual((self.home / "f.txt").read_text(), "someone else\n")
        prepared = self.tools.prepare("write_file", {"host": "filly", "path": "f.txt", "content": "two\n"})
        (self.home / "f.txt").unlink()
        with self.assertRaisesRegex(ValueError, "appeared or disappeared"):
            self.tools.execute(prepared)

    # ----- the rule that replaces the workspace -----------------------------------------

    def test_secret_looking_paths_are_refused_on_the_host_too(self):
        for path in [".ssh/config", "~/.ssh/id_ed25519", "sub/.env", "sub/.env.production",
                     "deploy.key", "certs/server.pem", ".gnupg/secring", "repo/.git/config"]:
            with self.subTest(path=path):
                with self.assertRaisesRegex(ValueError, "secret-file guard"):
                    self.call("read_file", path=path)
                with self.assertRaisesRegex(ValueError, "secret-file guard"):
                    self.call("write_file", path=path, content="x")

    def test_traversal_and_shapeless_paths_are_refused(self):
        for path in ["../etc/passwd", "sub/../../x", ".."]:
            with self.subTest(path=path):
                with self.assertRaisesRegex(ValueError, r"Parent traversal"):
                    self.call("read_file", path=path)
        with self.assertRaisesRegex(ValueError, "one line"):
            self.call("read_file", path="a\nb")
        with self.assertRaisesRegex(ValueError, "path on the host is required"):
            self.call("read_file", path="   ")

    def test_outside_the_home_is_refused_unless_the_shell_is_there(self):
        (self.elsewhere / "app.conf").write_text("listen 80\n")
        with self.assertRaisesRegex(ValueError, "outside the home directory"):
            self.call("read_file", path=str(self.elsewhere / "app.conf"))
        # The user's shell is in /srv: their own working directory is theirs to work in.
        self.use({**self.session, "cwd": str(self.elsewhere)})
        self.assertEqual(self.call("read_file", path="app.conf")["content"], "listen 80\n")
        self.assertEqual(self.call("read_file", path=str(self.elsewhere / "app.conf"))["content"],
                         "listen 80\n")
        # The home is still allowed while the shell is elsewhere; /etc is still not.
        (self.home / "notes.md").write_text("n\n")
        self.assertEqual(self.call("read_file", path=str(self.home / "notes.md"))["content"], "n\n")
        with self.assertRaisesRegex(ValueError, "outside the home directory"):
            self.call("write_file", path="/etc/hosts", content="x")
        self.assertEqual((self.elsewhere / "app.conf").read_text(), "listen 80\n")

    def test_the_rule_is_checked_on_the_host_where_home_is_known(self):
        # The refusal is the script's, not this machine's idea of a home directory.
        script = remote_files.read_script("/etc/hosts", None, cap=10)
        self.assertIn('case $p in "$HOME"/*|"$HOME") ;; *) exit 78 ;; esac', script)

    # ----- refusals, the schema and the labels -------------------------------------------

    def test_refusals_match_run_commands(self):
        for name, args in [("read_file", {"path": "f"}), ("list_directory", {"path": "."}),
                           ("write_file", {"path": "f", "content": "x"}),
                           ("edit_file", {"path": "f", "old_string": "a", "new_string": "b"})]:
            with self.subTest(name=name):
                with self.assertRaisesRegex(ValueError, r'host "other" is not the host'):
                    self.tools.prepare(name, {**args, "host": "other"})
                self.use({**self.session, "reachable": False})
                with self.assertRaisesRegex(ValueError, "can't be shared"):
                    self.tools.prepare(name, {**args, "host": "filly"})
                self.tools.set_remote_session(None)
                with self.assertRaisesRegex(ValueError, "not logged into any host"):
                    self.tools.prepare(name, {**args, "host": "filly"})
                self.use(self.session)
                with self.assertRaisesRegex(ValueError, "host must be text"):
                    self.tools.prepare(name, {**args, "host": 5})

    def test_a_closed_socket_reads_and_writes_nothing(self):
        (self.home / "f.txt").write_text("one\n")
        prepared = self.tools.prepare("write_file", {"host": "filly", "path": "f.txt", "content": "two\n"})
        os.unlink(self.socket_path)
        with self.assertRaisesRegex(ValueError, "has closed"):
            self.tools.execute(prepared)
        with self.assertRaisesRegex(ValueError, "has closed"):
            self.call("read_file", path="f.txt")
        self.assertEqual((self.home / "f.txt").read_text(), "one\n")

    def test_a_broken_connection_says_so(self):
        (self.bin / "ssh").write_text("#!/bin/sh\necho 'ssh: connect to host filly: Broken pipe' >&2\nexit 255\n")
        with self.assertRaisesRegex(ValueError, "connection to filly failed or closed"):
            self.call("read_file", path="f.txt")

    def test_a_host_without_the_commands_says_which(self):
        (self.bin / "ssh").write_text("#!/bin/sh\necho 'sh: cat: not found' >&2\nexit 127\n")
        with self.assertRaisesRegex(ValueError, "missing a command"):
            self.call("read_file", path="f.txt")

    def test_empty_host_is_a_local_call(self):
        (self.root / "local.txt").write_text("local\n")
        result = self.tools.execute(self.tools.prepare("read_file", {"path": "local.txt", "host": ""}))
        self.assertEqual(result["content"], "local\n")
        self.assertNotIn("host", result)

    def test_host_only_in_the_schema_while_logged_in(self):
        specs = {t["function"]["name"]: t for t in self.tools.tools()}
        for name in ("run_command", "read_file", "list_directory", "write_file", "edit_file"):
            with self.subTest(name=name):
                self.assertIn("host", specs[name]["function"]["parameters"]["properties"])
                self.assertNotIn("host", specs[name]["function"]["parameters"]["required"])
        self.tools.set_remote_session(None)
        local = {t["function"]["name"]: t for t in self.tools.tools()}
        for name in ("run_command", "read_file", "list_directory", "write_file", "edit_file"):
            with self.subTest(name=name):
                self.assertNotIn("host", local[name]["function"]["parameters"]["properties"])

    def test_labels_and_detail_name_the_host(self):
        read = {"path": "/etc/nginx/sites-enabled/relay-terminal/nginx.conf", "host": "filly"}
        self.assertEqual(tool_labels.started_label("read_file", read)["title"], "read nginx.conf on filly")
        self.assertEqual(tool_labels.started_label("read_file", read)["running"], "reading nginx.conf on filly")
        self.assertEqual(tool_labels.started_label("read_file", {"path": "/etc/hosts", "host": "filly"})["title"],
                         "read /etc/hosts on filly")
        self.assertEqual(tool_labels.started_label("read_file", read)["merge"]["plural"], "files on filly")
        self.assertEqual(tool_labels.started_label("list_directory", {"path": "/srv", "host": "filly"})["title"],
                         "listed /srv/ on filly")
        self.assertEqual(tool_labels.result_label("write_file", {"path": "app.conf", "host": "filly"},
                                                  {"created": True, "host": "filly"})["title"],
                         "wrote app.conf on filly")
        self.assertEqual(tool_labels.started_label("edit_file", {"path": "app.conf", "host": "filly"})["title"],
                         "edited app.conf on filly")
        # A click never opens a local file of the same name: the path is on the host.
        done = tool_labels.result_label("read_file", read, {"content": "x\n", "host": "filly"})
        self.assertEqual(done["open"], {"type": "fold"})
        self.assertEqual(done["merge"]["lines"], 1)
        created = tool_labels.result_label("write_file", {"path": "new.txt", "host": "filly"},
                                           {"created": True, "added": 1, "removed": 0, "host": "filly"})
        self.assertEqual(created["open"], {"type": "fold"})
        big = tool_labels.result_label("edit_file", {"path": "app.conf", "host": "filly"},
                                       {"added": 40, "removed": 3, "host": "filly"})
        self.assertEqual(big["open"], {"type": "diff"})
        # Locally it still opens the file.
        self.assertEqual(tool_labels.result_label("read_file", {"path": "a.py"}, {"content": "x"})["open"],
                         {"type": "file", "path": "a.py"})
        sections = tool_labels.detail("read_file", read, {"content": "x"})
        self.assertEqual(sections[0], {"heading": "host", "style": "text",
                                       "text": "filly (over your ssh connection)"})
        # Without a host nothing changes: the local line is exactly what it was.
        self.assertEqual(tool_labels.started_label("read_file", {"path": "a/b.py"})["title"], "read a/b.py")
        self.assertNotIn("host", [s["heading"] for s in tool_labels.detail("read_file", {"path": "a"}, {})])

    def test_the_note_offers_the_file_tools(self):
        note = format_context({"foreground_program": "ssh filly", "remote_session": SESSION})
        self.assertIn("read_file, list_directory, write_file and edit_file take the same host", note)
        self.assertIn("inside the user's home", note)
        self.assertNotIn("Read files on filly with run_command", note)


if __name__ == "__main__":
    unittest.main()
