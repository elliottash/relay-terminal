"""The ssh/mosh wrapper in shell/integration.bash and shell/remote-integration.sh (card #S5SH).

See docs/SSH-AND-MOSH.md, sections 1 and 3. The wrapper runs against a fake `ssh`/`mosh` that
prints its argv; the configuration check (`ssh -G`) goes to the real ssh when one is installed, so
the user-configured ControlMaster/ControlPath cases are judged by OpenSSH itself. The remote script
runs in real interactive bash and zsh on a pty, typed in the exact form the GUI types it.
"""
import base64
import fcntl
import gzip
import os
import pty
import re
import select
import shutil
import signal
import socket
import struct
import subprocess
import tempfile
import termios
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INTEGRATION = ROOT / "shell/integration.bash"
REMOTE = ROOT / "shell/remote-integration.sh"
CONTROL = ["-o", "ControlMaster=auto", "-o", "ControlPath={dir}/%C", "-o", "ControlPersist=600"]

FAKE = """#!/bin/sh
if [ -n "$RELAY_TEST_LOG" ]; then printf '%s\\n' "$*" >> "$RELAY_TEST_LOG"; fi
if [ "$1" = -G ]; then
    shift
    if [ -n "$RELAY_TEST_REAL_SSH" ]; then exec "$RELAY_TEST_REAL_SSH" -G "$@"; fi
    printf 'controlmaster false\\n'
    exit 0
fi
printf '%s\\n' "ARGV $(basename "$0")"
for a in "$@"; do printf 'ARG[%s]\\n' "$a"; done
exit ${RELAY_TEST_EXIT:-7}
"""


def real_ssh():
    for part in os.environ.get("PATH", "").split(os.pathsep):
        candidate = Path(part) / "ssh"
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    return ""


class WrapperTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="relay-ssh-test-")
        root = Path(cls.temp.name)
        cls.bin = root / "bin"
        cls.bin.mkdir()
        for name in ("ssh", "mosh"):
            (cls.bin / name).write_text(FAKE)
            (cls.bin / name).chmod(0o755)
        cls.sockets = root / "s"
        cls.sockets.mkdir(mode=0o700)
        cls.config = root / "config"
        cls.config.write_text("Host shared\n  ControlMaster auto\n  ControlPath ~/.ssh/cm-%C\n"
                              "Host pathonly\n  ControlPath /tmp/elsewhere-%C\n"
                              "Host plain\n  ControlMaster no\n"
                              "Host *\n  User someone\n")
        cls.real_ssh = real_ssh()
        cls.runtime = root / "runtime"
        cls.runtime.mkdir()

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def bash(self, script, wrap="1", ssh_dir=None, before="", exit_code=None, log=None):
        env = dict(os.environ, PATH=f"{self.bin}{os.pathsep}{os.environ.get('PATH', '')}",
                   RELAY_RUNTIME_DIR=str(self.runtime), RELAY_SHELL_EVENT="/nonexistent",
                   RELAY_SESSION_TOKEN="t", RELAY_CLEAN_SHELL="1", RELAY_TEST_REAL_SSH=self.real_ssh,
                   HOME=str(self.temp.name))   # the wrapper reads ~/.ssh/config: never the developer's
        env.pop("RELAY_SSH_WRAP", None)
        env.pop("RELAY_SSH_DIR", None)
        if wrap is not None:
            env["RELAY_SSH_WRAP"] = wrap
        if exit_code is not None:
            env["RELAY_TEST_EXIT"] = str(exit_code)   # what the fake ssh/mosh exits with
        if log is not None:
            env["RELAY_TEST_LOG"] = str(log)          # every call the fake sees, one per line
        dir_ = str(self.sockets) if ssh_dir is None else ssh_dir
        if dir_:
            env["RELAY_SSH_DIR"] = dir_
        full = f"{before}\nsource {INTEGRATION} 2>/dev/null\n{script}"
        return subprocess.run(["bash", "--noprofile", "--norc", "-c", full], env=env,
                              capture_output=True, text=True, timeout=10)

    def argv(self, args, **kw):
        quoted = " ".join("'" + a.replace("'", "'\\''") + "'" for a in args)
        result = self.bash(f"ssh {quoted}; echo \"STATUS $?\"", **kw)
        self.assertEqual(result.stderr, "")
        lines = result.stdout.splitlines()
        self.assertEqual(lines[-1], "STATUS 7", result.stdout)
        return [line[4:-1] for line in lines if line.startswith("ARG[")]

    def control(self):
        return [part.format(dir=self.sockets) for part in CONTROL]

    def assertShared(self, args, **kw):
        self.assertEqual(self.argv(args, **kw), self.control() + args)

    def assertPlain(self, args, **kw):
        self.assertEqual(self.argv(args, **kw), args)

    def test_adds_connection_sharing(self):
        cfg = ["-F", str(self.config)]
        self.assertShared(cfg + ["host"])
        self.assertShared(cfg + ["-tt", "-p22", "user@host", "ls", "-la"])
        self.assertShared(cfg + ["-vp", "2222", "-L", "8080:localhost:80", "host"])
        self.assertShared(cfg + ["host", "-p", "22", "cmd", "-M"])  # -M after the command is its own
        self.assertShared(cfg + ["-o", "ServerAliveInterval=30", "host", "--", "echo", "-O"])

    def test_skips_arguments_that_decide_sharing(self):
        cfg = ["-F", str(self.config)]
        for extra in (["-O", "check"], ["-S", "/tmp/x"], ["-M"], ["-G"], ["-V"], ["-Q", "cipher"],
                      ["-W", "h:22"], ["-tM"], ["-Ocheck"], ["-o", "ControlMaster=no"],
                      ["-oControlPath=none"], ["-o", "controlpersist 5"]):
            with self.subTest(extra=extra):
                self.assertPlain(cfg + extra + ["host"])
        self.assertPlain(cfg + ["host", "-O", "exit"])  # options may follow the destination
        self.assertPlain(cfg)  # no destination: ssh prints its usage
        self.assertPlain(cfg + ["-Z", "host"])  # not an ssh option: let ssh say so

    def test_user_configured_master_is_left_alone(self):
        if not self.real_ssh:
            self.skipTest("OpenSSH is not installed")
        cfg = ["-F", str(self.config)]
        self.assertPlain(cfg + ["shared"])
        self.assertPlain(cfg + ["pathonly"])
        self.assertShared(cfg + ["plain"])

    def test_directory_must_exist_and_be_usable(self):
        args = ["-F", str(self.config), "host"]
        self.assertPlain(args, ssh_dir="")
        self.assertPlain(args, ssh_dir=str(self.sockets / "missing"))
        spaced = Path(self.temp.name) / "a b"
        spaced.mkdir(exist_ok=True)
        self.assertPlain(args, ssh_dir=str(spaced))

    def test_off_unless_the_gui_asks(self):
        for wrap in (None, "0"):
            result = self.bash("type -t ssh mosh", wrap=wrap)
            self.assertEqual(result.stdout.split(), ["file", "file"])

    def test_nounset_no_leaks_no_output_completion(self):
        # The two names the wrapper does keep are its own: whether the user's configuration can
        # matter at all, and the per-host answer, so `ssh -G` is asked once rather than per call.
        names = ("compgen -v | grep -v -x -e _ -e 'BASH_.*' -e PIPESTATUS -e before -e after "
                 "-e __relay_ssh_configured -e __relay_ssh_asked | sort")
        result = self.bash(
            f"before=$({names}); set -u; complete -F _relay_fake ssh\n"
            f"ssh -F {self.config} host >/dev/null; mosh host >/dev/null\n"
            f"after=$({names}); [ \"$before\" = \"$after\" ] && echo SAME\n"
            "complete -p ssh; type -t ssh", exit_code=0)
        self.assertEqual(result.stderr, "")
        self.assertEqual(result.stdout.splitlines(),
                         ["SAME", "complete -F _relay_fake ssh", "function"])

    def test_alias_named_ssh_does_not_break_the_definition(self):
        result = self.bash("ssh -F /dev/null host; echo \"STATUS $?\"",
                           before="shopt -s expand_aliases; alias ssh='ssh -4'")
        self.assertEqual(result.stderr, "")
        self.assertIn("STATUS 7", result.stdout)

    def mosh(self, args, **kw):
        # Exit 0: a mosh that ends normally is not retried, so these are the arguments of the one
        # attempt. test_mosh_retries_plain_when_the_shared_one_fails covers a failure.
        quoted = " ".join("'" + a + "'" for a in args)
        result = self.bash(f"mosh {quoted}; echo \"STATUS $?\"", exit_code=0, **kw)
        self.assertEqual(result.stderr, "")
        self.assertIn("STATUS 0", result.stdout)
        return [line[4:-1] for line in result.stdout.splitlines() if line.startswith("ARG[")]

    def test_ssh_g_is_asked_only_when_the_configuration_could_matter(self):
        # `ssh -G` runs the user's `Match exec` hooks and can resolve names, so it is asked only
        # when something could be configured, and then once per host rather than once per ssh.
        # `ssh -G`'s own output is swallowed by the command substitution that reads it, so the
        # fake writes every call it sees to a log instead.
        calls = Path(self.temp.name) / "calls.log"
        def probes():
            lines = calls.read_text().splitlines() if calls.exists() else []
            calls.unlink(missing_ok=True)
            return len([line for line in lines if line.startswith("-G ")])
        self.bash("ssh host; ssh host; ssh other", exit_code=0, log=calls)
        self.assertEqual(probes(), 0)
        home = Path(self.temp.name) / ".ssh"
        home.mkdir(exist_ok=True)
        (home / "config").write_text("Host *\n  ControlPersist 60\n")
        self.bash("ssh host; ssh host; ssh other", exit_code=0, log=calls)
        self.assertEqual(probes(), 2)   # once for host, once for other: not once per ssh
        # An Include can hold the keyword just as well, and is followed one level.
        (home / "config").write_text("Include conf.d/*.conf\n")
        (home / "conf.d").mkdir(exist_ok=True)
        (home / "conf.d" / "a.conf").write_text("Host *\n  ControlMaster auto\n")
        self.bash("ssh host", exit_code=0, log=calls)
        self.assertEqual(probes(), 1)
        (home / "config").unlink()
        (home / "conf.d" / "a.conf").unlink()

    def test_mosh_retries_plain_when_the_shared_one_fails(self):
        # The server's own address (--experimental-remote-ip=remote) is unreachable behind NAT or
        # a forwarded port. mosh then dies at once, and what the user typed must still work.
        result = self.bash("mosh 'host'; echo \"STATUS $?\"", exit_code=7)
        args = [line[4:-1] for line in result.stdout.splitlines() if line.startswith("ARG[")]
        ssh = "--ssh=ssh " + " ".join(self.control())
        self.assertEqual(args, [ssh, "--experimental-remote-ip=remote", "host", "host"])
        self.assertIn("retrying as you typed it", result.stderr)
        self.assertIn("STATUS 7", result.stdout)   # the plain run's own status, not a Relay one

    def test_mosh(self):
        # A shared mosh that fails inside twenty seconds is run again exactly as the user typed
        # it: the server's own address is unreachable behind NAT or a forwarded port, and a
        # session that worked before Relay must keep working. The fake mosh here exits 0, so the
        # arguments below are the shared attempt; test_mosh_falls_back covers the retry.
        ssh = "--ssh=ssh " + " ".join(self.control())
        self.assertEqual(self.mosh(["host"]), [ssh, "--experimental-remote-ip=remote", "host"])
        self.assertEqual(self.mosh(["--experimental-remote-ip=local", "host"]),
                         [ssh, "--experimental-remote-ip=local", "host"])
        self.assertEqual(self.mosh(["--experimental-remote-ip", "remote", "host"]),
                         [ssh, "--experimental-remote-ip", "remote", "host"])
        for args in (["--ssh=ssh -p 2222", "host"], ["--ssh", "ssh -4", "host"],
                     ["host", "--ssh=ssh"], ["--experimental-remote-ip=proxy", "host"],
                     ["--experimental-remote-ip", "proxy", "host"]):
            with self.subTest(args=args):
                self.assertEqual(self.mosh(args), args)
        self.assertEqual(self.mosh(["host", "--", "tmux", "--ssh"]),
                         [ssh, "--experimental-remote-ip=remote", "host", "--", "tmux", "--ssh"])
        spaced = Path(self.temp.name) / "a b"
        spaced.mkdir(exist_ok=True)
        self.assertEqual(self.mosh(["host"], ssh_dir=str(spaced)), ["host"])


def typed_line(rows):
    """The line the GUI types: gzip, then base64, in one argument, with a leading space.

    The row count rides inside the payload (src/RemoteSession.cpp): a shell that is neither bash
    nor zsh has to be able to parse the line, and a prefix assignment it cannot parse makes it
    print the whole payload back at the user.
    """
    bare = b"".join(row + b"\n" for row in REMOTE.read_bytes().split(b"\n")
                    if row.strip() and not row.strip().startswith(b"#"))
    payload = f"RELAY_R={rows}\n".encode() + bare
    b64 = base64.b64encode(gzip.compress(payload, mtime=0)).decode()
    return f" eval \"$(printf %s '{b64}' | base64 -d | gzip -dc)\""


def _controlling_tty():
    fcntl.ioctl(0, termios.TIOCSCTTY, 0)


class PtyShell:
    PROMPT = b"RP> "

    def __init__(self, argv, env):
        master, slave = pty.openpty()
        # Wide enough that the bootstrap line, a couple of thousand characters, fits on the screen
        # the line editor redraws: a narrow one still runs it, but rewrites the echo as it scrolls.
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 50, 200, 0, 0))
        self.proc = subprocess.Popen(argv, stdin=slave, stdout=slave, stderr=slave, env=env,
                                     start_new_session=True, preexec_fn=_controlling_tty)
        os.close(slave)
        self.master = master
        self.output = b""
        self.wait_prompts(1)

    def read(self, until, timeout=4.0):
        end = time.monotonic() + timeout
        while not until() and time.monotonic() < end:
            if select.select([self.master], [], [], 0.05)[0]:
                try:
                    self.output += os.read(self.master, 65536)
                except OSError:
                    break
        if not until():
            raise AssertionError(f"timed out: {self.output[-1500:]!r}")

    def editing(self):
        """The line editor owns the terminal (non-canonical mode): typing now reaches it."""
        return not termios.tcgetattr(self.master)[3] & termios.ICANON

    def wait_prompts(self, count):
        self.read(lambda: self.output.count(self.PROMPT) >= count and self.editing())

    def run(self, line):
        """Type a line, return what the shell wrote until its next prompt."""
        start, tail = len(self.output), line.encode()[-12:]

        def done():
            echoed = self.output.find(tail, start)
            return (echoed >= 0 and self.output.find(self.PROMPT, echoed) >= 0
                    and self.editing())
        os.write(self.master, line.encode() + b"\r")
        self.read(done)
        # Everything after the echoed line, up to and including the next prompt.
        return self.output[self.output.find(tail, start) + len(tail):]

    def close(self):
        try:
            os.killpg(self.proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        self.proc.wait(timeout=2)
        os.close(self.master)


def osc(body):
    return b"\x1b]" + body + b"\x07"


def dcs(body, tmux=True):
    """The same OSC as a terminal multiplexer passes through: tmux doubles every ESC."""
    return (b"\x1bPtmux;\x1b" if tmux else b"\x1bP") + osc(body) + b"\x1b\\"


TMUX_HINT = b'relay: tmux needs "set -g allow-passthrough on" for prompt marks'


def ssh_to_localhost():
    """`ssh localhost` with a key and no prompt, or "" when this machine cannot ssh to itself."""
    ssh = real_ssh()
    if not ssh:
        return ""
    try:
        done = subprocess.run([ssh, "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=no",
                               "-o", "ConnectTimeout=5", "localhost", "true"],
                              capture_output=True, timeout=20)
    except (OSError, subprocess.SubprocessError):
        return ""
    return ssh if done.returncode == 0 else ""


class RemoteScriptTests(unittest.TestCase):
    PROMPT = PtyShell.PROMPT

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="relay-remote-test-")
        self.home = Path(self.temp.name)
        self.env = dict(os.environ, HOME=str(self.home), TERM="xterm-256color", PS1="RP> ",
                        HISTFILE=str(self.home / "hist"), LANG="C.UTF-8")
        for name in ("PROMPT_COMMAND", "HISTCONTROL", "PS0", "ZDOTDIR", "RELAY_R"):
            self.env.pop(name, None)
        self.hostname = socket.gethostname().encode()

    def tearDown(self):
        self.temp.cleanup()

    def bash(self):
        shell = PtyShell(["bash", "--noprofile", "--norc", "-i"], self.env)
        self.addCleanup(shell.close)
        return shell

    def test_bash(self):
        s = self.bash()
        s.run("set -u; HISTCONTROL=ignoredups; PROMPT_COMMAND='user_pc=$((${user_pc:-0}+1))'")
        s.run("echo before")
        out = s.run(typed_line(3))
        self.assertIn(b"\x1b[3A\r\x1b[J", out)
        self.assertIn(b"\x1b]7;file://" + self.hostname + b"/", out)
        self.assertIn(osc(b"133;A") + b"RP> " + osc(b"133;B"), out)
        self.assertNotIn(b"133;D", out)  # the typed line is not a command the user ran
        self.assertNotIn(b"unbound", out)
        self.assertNotIn(b"\x1bP", out)  # no multiplexer: nothing is wrapped in a DCS

        out = s.run("(exit 3)")
        self.assertIn(osc(b"133;C"), out)
        self.assertIn(osc(b"133;D;3"), out)
        self.assertLess(out.index(osc(b"133;C")), out.index(osc(b"133;D;3")))
        self.assertIn(osc(b"133;D;0"), s.run("true"))

        target = self.home / "dir with space" / "é"
        target.mkdir(parents=True)
        out = s.run(f"cd '{target}'")
        self.assertIn(b"\x1b]7;file://" + self.hostname
                      + str(self.home).encode() + b"/dir%20with%20space/%C3%A9\x07", out)

        out = s.run("echo \"[$HISTCONTROL] [${RELAY_R-unset}] [$user_pc]\"; history")
        self.assertIn(b"[ignoredups:ignorespace] [unset] [", out)
        self.assertNotIn(b"RELAY_R=3", out)  # the typed line left the history
        self.assertIn(b"echo before", out)  # and took nothing else with it
        count = int(re.search(rb"\] \[(\d+)\]", out).group(1))
        self.assertIn(f"[{count + 1}]".encode(), s.run("echo \"[$user_pc]\""))  # user's hook runs

        def state():
            out = s.run("declare -p PROMPT_COMMAND PS1 PS0")
            return re.findall(rb"declare [^\r]*", out)
        before = state()
        s.run(typed_line(1))
        self.assertEqual(before, state())  # a second eval does nothing
        self.assertEqual(b"".join(before).count(b"133;A"), 1)  # PS1 wrapped once
        self.assertIn(b'"\\C-x\\C-p": "__relay_r_redraw"', s.run("bind -X"))

    def test_bash_prompt_array_and_old_bash_debug_trap(self):
        s = self.bash()
        # bash < 4.4 has no PS0: 133;C comes from a DEBUG trap instead.
        s.run("BASH_VERSION=4.3.48; unset PS0; PROMPT_COMMAND=(': one' ': two')")
        s.run(typed_line(2))
        out = s.run("false")
        self.assertIn(osc(b"133;C"), out)
        self.assertIn(osc(b"133;D;1"), out)
        out = s.run("declare -p PROMPT_COMMAND; trap -p DEBUG")
        self.assertIn(b'([0]="__relay_r_pc" [1]=": one" [2]=": two" [3]="__relay_r_ps")', out)
        self.assertIn(b"__relay_r_c", out)
        self.assertEqual(out.count(osc(b"133;C")), 1)

    def test_bash_keeps_an_existing_debug_trap(self):
        s = self.bash()
        s.run("BASH_VERSION=4.2.0; trap ': mine' DEBUG")
        s.run(typed_line(2))
        self.assertIn(b"trap -- ': mine' DEBUG", s.run("trap -p DEBUG"))

    def test_zsh(self):
        if not shutil.which("zsh"):
            self.skipTest("zsh is not installed")
        zdot = self.home / "zdot"
        zdot.mkdir()
        # A user precmd whose own status must not leak into 133;D.
        (zdot / ".zshrc").write_text("PS1='RP> '\nprecmd() { false; }\nuser_hook() { :; }\n"
                                     "precmd_functions+=(user_hook)\n")
        env = dict(self.env, ZDOTDIR=str(zdot))
        env.pop("PS1")
        s = PtyShell(["zsh", "-i"], env)
        self.addCleanup(s.close)
        s.run("setopt nounset")
        out = s.run(typed_line(2))
        self.assertIn(b"\x1b[2A\r\x1b[J", out)
        self.assertIn(b"\x1b]7;file://" + self.hostname + b"/", out)
        self.assertIn(osc(b"133;A") + b"RP> " + osc(b"133;B"), out)
        self.assertNotIn(b"133;D", out)
        self.assertNotIn(b"\x1bP", out)
        out = s.run("(exit 4)")
        self.assertIn(osc(b"133;C"), out)
        self.assertIn(osc(b"133;D;4"), out)
        target = self.home / "a b"
        target.mkdir()
        self.assertIn(b"/a%20b\x07", s.run(f"cd '{target}'"))
        out = s.run("echo \"[${RELAY_R-unset}] $precmd_functions\"; setopt | grep -c histignorespace;"
                    " bindkey '^X^P'; bindkey -M viins '^X^P'")
        self.assertIn(b"[unset] user_hook __relay_r_pc", out)
        self.assertIn(b"\n1", out.replace(b"\r", b""))
        self.assertEqual(out.count(b'"^X^P" __relay_r_redraw'), 2)
        s.run(typed_line(1))
        self.assertIn(b"[unset] user_hook __relay_r_pc\r", s.run("echo \"[${RELAY_R-unset}] $precmd_functions\""))

    def test_exported_prompt_command_does_not_follow_a_later_tmux(self):
        # A tmux started after the script loaded runs a shell that never saw __relay_r_pc: an
        # exported PROMPT_COMMAND naming it would print "command not found" at every prompt there.
        s = self.bash()
        s.run("export PROMPT_COMMAND='user_pc=1'")
        s.run(typed_line(2))
        out = s.run("declare -p PROMPT_COMMAND; env | grep -c '^PROMPT_COMMAND' || true")
        self.assertIn(b"__relay_r_pc", out)
        self.assertNotIn(b"declare -x PROMPT_COMMAND", out)
        self.assertIn(b"\n0", out.replace(b"\r", b""))
        out = s.run("env | sort")
        self.assertEqual([], [n for n in ("__relay_r", "__relay_r_h", "__relay_r_e") if
                              f"\n{n}=".encode() in out])

    def test_zsh_over_ssh(self):
        """The real thing: zsh on the other side of ssh, given the line the GUI types."""
        ssh = ssh_to_localhost()
        if not ssh or not shutil.which("zsh"):
            self.skipTest("no zsh, or this machine cannot ssh to itself without a password")
        zdot = self.home / "zdot"
        zdot.mkdir()
        (zdot / ".zshrc").write_text(f"PS1='RP> '\nHISTFILE={self.home}/zhist\n")
        os.chmod(self.home, 0o755)  # the remote side is this machine, but reached as a login
        s = PtyShell([ssh, "-t", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=no",
                      "localhost", f"ZDOTDIR={zdot} zsh -i"], self.env)
        self.addCleanup(s.close)
        out = s.run(typed_line(3))
        self.assertIn(b"\x1b[3A\r\x1b[J", out)
        self.assertIn(b"\x1b]7;file://" + self.hostname + b"/", out)
        self.assertIn(osc(b"133;A") + b"RP> " + osc(b"133;B"), out)
        self.assertNotIn(b"\x1bP", out)  # no multiplexer in between: nothing is wrapped
        out = s.run("(exit 6)")
        self.assertIn(osc(b"133;C"), out)
        self.assertIn(osc(b"133;D;6"), out)
        self.assertIn(b"\x1b]7;file://" + self.hostname + b"/tmp\x07", s.run("cd /tmp"))
        out = s.run("bindkey '^X^P'; bindkey -M viins '^X^P';"
                    " echo \"[$(setopt | grep -c histignorespace)] [${RELAY_R-unset}]"
                    " $precmd_functions\"")
        self.assertEqual(out.count(b'"^X^P" __relay_r_redraw'), 2)
        self.assertIn(b"[1] [unset] __relay_r_pc", out)

    def multiplexed(self, **env):
        """An interactive bash that believes it is inside tmux or screen."""
        s = PtyShell(["bash", "--noprofile", "--norc", "-i"], dict(self.env, **env))
        self.addCleanup(s.close)
        return s

    def test_tmux_wraps_every_sequence_and_asks_for_passthrough(self):
        # $TMUX set, no server behind it: `tmux show` cannot say the option is on, so the hint shows.
        s = self.multiplexed(TMUX=f"{self.home}/no-such-tmux,1,0")
        out = s.run(typed_line(2))
        self.assertIn(b"\x1b[2A\r\x1b[J", out)  # the erase is a CSI tmux understands: not wrapped
        self.assertIn(dcs(b"133;A") + b"RP> " + dcs(b"133;B"), out)
        self.assertIn(b"\x1bPtmux;\x1b\x1b]7;file://" + self.hostname + b"/", out)
        self.assertEqual(out.count(TMUX_HINT), 1)
        naked = out.replace(b"\x1bPtmux;\x1b\x1b]", b"")  # what was sent without a wrapper
        self.assertNotIn(b"\x1b]133;", naked)
        self.assertNotIn(b"\x1b]7;", naked)
        out = s.run("(exit 5)")
        self.assertIn(dcs(b"133;C"), out)
        self.assertIn(dcs(b"133;D;5"), out)
        self.assertNotIn(TMUX_HINT, s.run(typed_line(1)))  # once per login, not once per eval

    def test_zsh_in_tmux(self):
        if not shutil.which("zsh"):
            self.skipTest("zsh is not installed")
        zdot = self.home / "ztmux"
        zdot.mkdir()
        (zdot / ".zshrc").write_text("PS1='RP> '\n")
        env = dict(self.env, ZDOTDIR=str(zdot), TMUX=f"{self.home}/no-such-tmux,1,0")
        env.pop("PS1")
        s = PtyShell(["zsh", "-i"], env)
        self.addCleanup(s.close)
        out = s.run(typed_line(2))
        self.assertIn(dcs(b"133;A") + b"RP> " + dcs(b"133;B"), out)
        self.assertIn(b"\x1bPtmux;\x1b\x1b]7;file://" + self.hostname + b"/", out)
        self.assertEqual(out.count(TMUX_HINT), 1)
        out = s.run("(exit 4)")
        self.assertIn(dcs(b"133;C"), out)
        self.assertIn(dcs(b"133;D;4"), out)

    def test_screen_wraps_without_doubling_and_skips_a_long_path(self):
        s = self.multiplexed(STY="4242.pts-3.host")
        out = s.run(typed_line(2))
        self.assertIn(dcs(b"133;A", tmux=False) + b"RP> " + dcs(b"133;B", tmux=False), out)
        self.assertIn(b"\x1bP\x1b]7;file://" + self.hostname + b"/", out)
        self.assertNotIn(b"\x1b\x1b", out)  # screen takes the sequence as it is
        self.assertNotIn(TMUX_HINT, out)
        deep = self.home / ("d" * 60) / ("e" * 60) / ("f" * 60)
        deep.mkdir(parents=True)
        out = s.run(f"cd '{deep}'")
        self.assertIn(dcs(b"133;D;0", tmux=False), out)
        self.assertNotIn(b"]7;file://", out)  # past screen's string limit: skipped, not truncated
        self.assertIn(dcs(b"7;file://" + self.hostname + b"/tmp", tmux=False), s.run("cd /tmp"))

    def test_screen_seen_in_term_alone(self):
        out = self.multiplexed(TERM="screen-256color").run(typed_line(2))
        self.assertIn(b"\x1bP\x1b]7;file://" + self.hostname + b"/", out)
        self.assertNotIn(TMUX_HINT, out)

    def test_inside_a_real_tmux(self):
        if not shutil.which("tmux"):
            self.skipTest("tmux is not installed")
        for passthrough in ("on", "off"):
            with self.subTest(allow_passthrough=passthrough):
                self.real_tmux(passthrough)

    def real_tmux(self, passthrough):
        """A real tmux: the shell's own bytes (pipe-pane) against what the client's terminal sees."""
        tmux = shutil.which("tmux")
        socket_name = f"relay-test-{os.getpid()}-{passthrough}"
        raw = self.home / f"pane-{passthrough}.raw"

        def run_tmux(*args):
            subprocess.run([tmux, "-L", socket_name, *args], env=self.env, check=True,
                           capture_output=True, timeout=20)
        run_tmux("-f", "/dev/null", "new-session", "-d", "-x", "80", "-y", "24",
                 "bash", "--noprofile", "--norc", "-i")
        self.addCleanup(lambda: subprocess.run([tmux, "-L", socket_name, "kill-server"],
                                               capture_output=True, timeout=20))
        run_tmux("set", "-g", "status", "off")
        run_tmux("set", "-g", "allow-passthrough", passthrough)
        run_tmux("pipe-pane", "-o", f"cat >>'{raw}'")
        s = PtyShell([tmux, "-L", socket_name, "attach"], self.env)
        self.addCleanup(s.close)
        os.write(s.master, typed_line(2).encode() + b"\r")
        time.sleep(0.5)
        os.write(s.master, b"printf 'RE%sY\\n' AD\r")
        s.read(lambda: b"READY" in s.output, timeout=10)
        # And on to the prompt after it, so the command's D mark and the new A/B have been drawn.
        s.read(lambda: self.PROMPT in s.output[s.output.index(b"READY") + 5:], timeout=10)

        pane = raw.read_bytes()  # what the shell wrote into the tmux pane
        self.assertIn(dcs(b"133;A") + b"RP> " + dcs(b"133;B"), pane)
        self.assertIn(b"\x1bPtmux;\x1b\x1b]7;file://" + self.hostname + b"/", pane)
        self.assertIn(dcs(b"133;C"), pane)
        self.assertIn(dcs(b"133;D;0"), pane)
        if passthrough == "on":
            self.assertNotIn(TMUX_HINT, pane)
            # tmux unwrapped it and passed it on, which is all Relay ever sees.
            self.assertIn(osc(b"133;A"), s.output)
            self.assertIn(osc(b"133;D;0"), s.output)
            self.assertIn(b"\x1b]7;file://" + self.hostname + b"/", s.output)
        else:
            self.assertIn(TMUX_HINT, pane)  # and the user is told what to set
            self.assertNotIn(b"]133;", s.output)  # dropped: no mark reaches Relay
            self.assertNotIn(b"]7;file://", s.output)

    def test_other_shells_do_nothing(self):
        script = REMOTE.read_text()
        shells = [["sh", "-c"], ["bash", "--norc", "-c"]]
        for extra in ("dash", "ksh", "ksh93", "mksh", "zsh", "busybox"):
            path = shutil.which(extra)
            if path:
                shells.append([path, "sh", "-c"] if extra == "busybox" else [path, "-c"])
        for argv in shells:
            with self.subTest(shell=argv[0]):
                # Non-interactive bash/zsh are "other shells" too: the script only acts at a prompt.
                result = subprocess.run(
                    argv + ['RELAY_R=3 eval "$S"; echo "rc=$? [${RELAY_R-unset}]"'],
                    env=dict(self.env, S=script), capture_output=True, text=True, timeout=5)
                self.assertEqual((result.stdout, result.stderr), ("rc=0 [unset]\n", ""))
        dash = shutil.which("dash")
        if dash:  # an interactive dash, as a remote login shell might be
            result = subprocess.run([dash, "-i"], input=f"{typed_line(3)}\necho rc=$?\n",
                                    env=self.env, capture_output=True, text=True, timeout=5)
            self.assertEqual(result.stdout, "rc=0\n")
            self.assertNotIn("\x1b", result.stderr)

    def test_small_enough_to_type(self):
        # What matters is the line typed into the remote shell: it goes in one write, and a tty's
        # input buffer holds 4 KB. The script's own size only bounds that.
        # The file may grow: what is typed is the file without its comments and blank lines.
        self.assertLess(len(REMOTE.read_bytes()), 6144)
        self.assertLess(len(typed_line(10)), 2400)


if __name__ == "__main__":
    unittest.main()
