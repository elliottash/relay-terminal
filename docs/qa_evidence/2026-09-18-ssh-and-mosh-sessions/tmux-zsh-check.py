#!/usr/bin/env python3
"""Card #S5SH: the remote integration inside a real tmux and screen, and over ssh into a real zsh.

What the automated tests cannot show on their own is what the session *looks* like: whether the
erase still hides the typed line when tmux owns the screen, and what a user sees when
allow-passthrough is off. This drives the real programs on this machine and prints both the bytes
and the screen.

    python3 docs/qa_evidence/2026-09-18-ssh-and-mosh-sessions/tmux-zsh-check.py

Its output on 2026-09-18 is beside it in tmux-zsh-check.txt.
"""
import os
import pathlib
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests"))
from test_ssh_shell import PtyShell, typed_line  # noqa: E402

HOST = socket.gethostname().encode()
HINT = b'relay: tmux needs "set -g allow-passthrough on" for prompt marks'


def head(title):
    print(f"\n=== {title} " + "=" * max(0, 76 - len(title)))


def shown(raw, needle, label):
    at = raw.find(needle)
    print(f"  {label}: {'yes' if at >= 0 else 'NO'}"
          + (f"  {raw[at:at + len(needle) + 2]!r}" if at >= 0 else ""))


def drain(shell, seconds=1.5):
    """Keep reading for a while: nothing reaches shell.output unless somebody reads it."""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        try:
            shell.read(lambda: False, timeout=0.2)
        except AssertionError:
            pass


def plain(raw):
    """Escape sequences out, so a captured screen reads as the text on it."""
    text = re.sub(rb"\x1b\][^\x07]*\x07", b"", raw)
    return re.sub(rb"\x1b[\[\(][0-9;?]*[A-Za-z]|\x1b[=>]|\r", b"", text).decode(errors="replace")


def rows_for(line, column, columns):
    """What the GUI passes as RELAY_R: the rows the prompt and the echoed line take."""
    return (column + len(line) + columns - 1) // columns


def env_for(home):
    env = dict(os.environ, HOME=str(home), TERM="xterm-256color", PS1="RP> ",
               HISTFILE=str(home / "hist"), LANG="C.UTF-8")
    for name in ("PROMPT_COMMAND", "HISTCONTROL", "PS0", "ZDOTDIR", "RELAY_R", "TMUX", "STY"):
        env.pop(name, None)
    return env


def in_tmux(home, passthrough):
    head(f"tmux 3.x, allow-passthrough {passthrough}")
    env = env_for(home)
    tmux = shutil.which("tmux")
    sock = f"relay-evidence-{passthrough}"
    raw = home / f"pane-{passthrough}.raw"

    def tm(*args, capture=False):
        return subprocess.run([tmux, "-L", sock, *args], env=env, check=True,
                              capture_output=True, timeout=20, text=capture).stdout
    tm("-f", "/dev/null", "new-session", "-d", "bash", "--noprofile", "--norc", "-i")
    try:
        tm("set", "-g", "status", "off")
        tm("set", "-g", "allow-passthrough", passthrough)
        tm("pipe-pane", "-o", f"cat >>'{raw}'")
        client = PtyShell([tmux, "-L", sock, "attach"], env)
        try:
            os.write(client.master, b"echo an earlier command\r")
            time.sleep(0.4)
            width = int(tm("display", "-p", "#{pane_width}", capture=True).strip())
            rows = rows_for(typed_line(9), len("RP> "), width)
            line = typed_line(rows)
            print(f"  pane {width} columns, the line takes {rows} rows: RELAY_R={rows}")
            os.write(client.master, line.encode() + b"\r")
            time.sleep(0.6)
            os.write(client.master, b"cd /tmp\r")
            drain(client, 2.0)
            pane = raw.read_bytes()
            print("  the shell's own bytes, as tmux received them:")
            for mark in (b"7;file://", b"133;A", b"133;C", b"133;D;0"):
                shown(pane, b"\x1bPtmux;\x1b\x1b]" + mark, f"    wrapped {mark.decode()}")
            shown(pane, HINT, "    the one-line hint")
            print("  what the client's terminal (Relay's pane) got:")
            for mark in (b"\x1b]7;file://", b"\x1b]133;A\x07", b"\x1b]133;D;0\x07"):
                shown(client.output, mark, f"    {mark!r}")
            print("  the pane, after the erase — the typed line is gone, the line before it is not:")
            for row in tm("capture-pane", "-p", capture=True).rstrip("\n").split("\n")[:6]:
                print(f"    |{row}")
        finally:
            client.close()
    finally:
        subprocess.run([tmux, "-L", sock, "kill-server"], capture_output=True, timeout=20)


def in_screen(home):
    head("GNU screen")
    env = env_for(home)
    rc = home / "screenrc"
    rc.write_text("startup_message off\ndeflogin off\nhardstatus off\n")
    s = PtyShell([shutil.which("screen"), "-c", str(rc), "-S", "relay-evidence",
                  "bash", "--noprofile", "--norc", "-i"], env)
    try:
        os.write(s.master, typed_line(2).encode() + b"\r")
        time.sleep(1.5)
        os.write(s.master, b"cd /tmp\r")
        drain(s, 2.0)
        print("  what the terminal outside screen got (screen unwrapped the DCS and passed it on):")
        for mark in (b"\x1b]7;file://" + HOST, b"\x1b]133;A\x07", b"\x1b]133;D;0\x07"):
            shown(s.output, mark, f"    {mark!r}")
        shown(s.output, HINT, "    the tmux hint (must not show here)")
    finally:
        s.close()
        subprocess.run([shutil.which("screen"), "-S", "relay-evidence", "-X", "quit"],
                       capture_output=True, timeout=10)


def tmux_started_later(home):
    head("tmux started after the script loaded")
    env = env_for(home)
    s = PtyShell(["bash", "--noprofile", "--norc", "-i"], env)
    try:
        s.run("export PROMPT_COMMAND='user_pc=1'")   # the case that would follow the user in
        s.run(typed_line(2))
        tmux = shutil.which("tmux")
        s.run(f"{tmux} -L relay-later -f /dev/null new-session -d 'bash --noprofile --norc -i'")
        time.sleep(1.0)
        pane = s.run(f"{tmux} -L relay-later capture-pane -p; {tmux} -L relay-later kill-server")
        print("  the shell tmux started, which never ran our script:")
        for row in plain(pane).split("\n"):
            if row.strip() and "capture-pane" not in row:
                print(f"    |{row}")
        print("  (no 'command not found': PROMPT_COMMAND kept its value and lost its export)")
    finally:
        s.close()


def over_ssh(home):
    head("ssh -t localhost zsh -i")
    env = env_for(home)
    zdot = home / "zdot"
    zdot.mkdir(exist_ok=True)
    (zdot / ".zshrc").write_text(f"PS1='RP> '\nHISTFILE={home}/zhist\n")
    os.chmod(home, 0o755)
    s = PtyShell(["ssh", "-t", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=no",
                  "localhost", f"ZDOTDIR={zdot} zsh -i"], env)
    try:
        out = s.run(typed_line(3))
        print(f"  typed: {typed_line(3)[:72]}… ({len(typed_line(3))} bytes)")
        shown(out, b"\x1b[3A\r\x1b[J", "    the erase")
        shown(out, b"\x1b]7;file://" + HOST, "    OSC 7 with this host's name")
        shown(out, b"\x1b]133;A\x07RP> \x1b]133;B\x07", "    A and B around the prompt")
        out = s.run("(exit 6)")
        shown(out, b"\x1b]133;C\x07", "    C when a command starts")
        shown(out, b"\x1b]133;D;6\x07", "    D with the status")
        out = s.run("bindkey '^X^P'; echo \"[$(setopt | grep -c histignorespace)]"
                    " [${RELAY_R-unset}] $precmd_functions\"")
        for line in plain(out).split("\n"):
            if "^X^P" in line or line.startswith("["):
                print(f"    |{line}")
        print("  ([1] = HIST_IGNORE_SPACE is set, [unset] = RELAY_R did not stay behind)")
    finally:
        s.close()


def main():
    print(f"host {HOST.decode()} · {time.strftime('%Y-%m-%d %H:%M')} · "
          f"{subprocess.run(['tmux', '-V'], capture_output=True, text=True).stdout.strip()} · "
          f"{subprocess.run(['zsh', '--version'], capture_output=True, text=True).stdout.strip()}")
    with tempfile.TemporaryDirectory(prefix="relay-evidence-") as temp:
        home = pathlib.Path(temp)
        for passthrough in ("off", "on"):
            in_tmux(home, passthrough)
        in_screen(home)
        tmux_started_later(home)
        over_ssh(home)
    print("\ndone")


if __name__ == "__main__":
    main()
