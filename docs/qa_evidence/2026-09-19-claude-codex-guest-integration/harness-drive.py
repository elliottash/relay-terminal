#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tier A end to end: pick Claude Code in a live Relay pane and watch one real turn (GT7X, §29).

This is the one harness run that is *not* a replay: it starts the built `relay` under Xvfb with
isolated XDG/TMPDIR directories but the **real HOME**, because the real `claude` has to find its
login. It then does what a user does — `/model claude` in the prompt box — which, with the worker
reporting the `guest:claude` preset as `harness: true`, configures the pane's worker with the
Claude harness (no TUI, the shell stays a shell), and sends one tiny prompt. The picture is the
proof: Relay's own status line ("Claude Code is this pane's agent (session …)"), the model box on
Claude Code, and the answer printed by Relay's transcript. The run costs exactly one Claude Code
turn on the owner's subscription; the prompt asks for one word.

    xvfb-run -a -s "-screen 0 1600x1000x24" python3 harness-drive.py <output-dir> [claude|codex]

The transcript of that turn lands where claude keeps them (`~/.claude/projects/<cwd-slug>/`) under
the temp workspace's slug; nothing else of the user's is read or written.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

REPO = Path(__file__).resolve().parents[3]
RELAY = REPO / "build" / "relay"
PROMPT = "Reply with the single word ok."
# Which guest to drive: `harness-drive.py <output dir> [claude|codex]`. Both take the same route
# (§29.4): the worker reports a `guest:<id>` preset, the pane configures its worker on it, and no
# TUI is started. Codex costs one turn on the owner's ChatGPT plan, claude one on its own.
GUESTS = {"claude": "Claude Code", "codex": "Codex"}


def log(message: str) -> None:
    print(f"[drive] {message}", flush=True)


def make_isolation() -> dict:
    root = Path(tempfile.mkdtemp(prefix="relay-qa-harn-", dir="/tmp/claude-1000" if Path("/tmp/claude-1000").is_dir() else None))
    for name in ("config", "data", "run", "tmp", "workspace"):
        (root / name).mkdir(parents=True, exist_ok=True)
    os.chmod(root / "run", 0o700)
    (root / "config" / "RelayTerminal").mkdir(parents=True, exist_ok=True)
    (root / "config" / "RelayTerminal" / "relay.conf").write_text("[instructions]\nonboarded=true\n[isolation]\nenabled=false\n", encoding="utf-8")
    environment = dict(os.environ)
    environment.update({
        "XDG_CONFIG_HOME": str(root / "config"),
        "XDG_DATA_HOME": str(root / "data"),
        "XDG_RUNTIME_DIR": str(root / "run"),
        "TMPDIR": str(root / "tmp"),
        "RELAY_KEYRING": "off",
        "RELAY_LOCAL_MODELS": str(root / "data" / "local-models.json"),
    })
    # Relay may itself be started from inside a Claude Code session; the harness strips these for
    # its child, but the drive strips them for Relay too so nothing else in the run inherits them.
    for name in list(environment):
        if name.startswith("CLAUDE_CODE") or name == "CLAUDECODE" or name == "CLAUDE_EFFORT":
            environment.pop(name, None)
    for name in ("RELAY_DATA_DIR", "RELAY_ENGINE_CORE", "PYTHONPATH"):
        environment.pop(name, None)
    environment["RELAY_DATA_DIR"] = str(REPO)
    return {"root": root, "env": environment, "workspace": root / "workspace"}


def xdo(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["xdotool", *args], text=True, capture_output=True, check=False)


def window_id() -> str:
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        ids = [line for line in xdo("search", "--name", "Relay").stdout.split() if line.strip()]
        if ids:
            return ids[-1]
        time.sleep(0.5)
    raise RuntimeError("no Relay window appeared")


def type_text(window: str, text: str) -> None:
    xdo("windowactivate", "--sync", window)
    xdo("type", "--window", window, "--delay", "40", text)


def press(window: str, *keys: str) -> None:
    xdo("windowactivate", "--sync", window)
    for name in keys:
        xdo("key", "--window", window, name)
        time.sleep(0.3)


def screenshot(path: Path) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(["import", "-window", "root", str(path)], text=True, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(f"screenshot failed: {result.stderr}")
    log(f"wrote {path.name}")
    return subprocess.run(["tesseract", str(path), "-"], text=True, capture_output=True).stdout


def report(name: str, text: str, expect: list[str], forbid: list[str] = ()) -> bool:
    found = [needle for needle in expect if needle in text]
    missing = [needle for needle in expect if needle not in text]
    log(f"{name}: found {found or 'nothing expected'}; missing {missing or 'nothing'}")
    for needle in forbid:
        if needle in text:
            log(f"{name}: UNEXPECTED {needle!r} is on screen")
            missing.append(needle)
    return not missing


def main() -> int:
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
    guest = sys.argv[2] if len(sys.argv) > 2 else "claude"
    if guest not in GUESTS:
        log(f"unknown guest {guest!r}: one of {', '.join(GUESTS)}")
        return 1
    name = GUESTS[guest]
    shot = lambda label: output / f"implementer-harness-{guest}-{label}.png" if guest != "claude" else output / f"implementer-harness-{label}.png"
    output.mkdir(parents=True, exist_ok=True)
    if not RELAY.exists():
        log(f"missing {RELAY}: build first")
        return 1
    if not shutil.which(guest):
        log(f"no real {guest} on PATH: this run needs one")
        return 1
    for tool in ("xdotool", "import", "tesseract"):
        if not shutil.which(tool):
            log(f"missing {tool}")
            return 1
    isolation = make_isolation()
    environment = isolation["env"]
    log(f"isolated session at {isolation['root']} (HOME stays {environment['HOME']})")
    ok = True
    process = subprocess.Popen([str(RELAY), "--fresh", "-w", str(isolation["workspace"])],
                               env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        window = window_id()
        time.sleep(8)   # the worker answers `presets`, with the guest rows
        text = screenshot(shot("01-before"))
        # 1. /model claude: with the harness usable, the pane configures its worker on the guest
        #    preset (protocol 29.4) rather than launching a TUI.
        type_text(window, f"/model {guest}")
        press(window, "Return")
        deadline = time.monotonic() + 40
        configured = False
        while time.monotonic() < deadline and not configured:
            time.sleep(2)
            text = screenshot(shot("02-picked"))
            configured = "this pane's agent" in text or name in text
        ok &= report("02-picked", text, [name], ["fake-claude", "--settings", "-c notify="])
        # 2. One prompt through the ordinary agent route (Ctrl+Enter sends to the agent).
        type_text(window, PROMPT)
        press(window, "ctrl+Return")
        deadline = time.monotonic() + 120
        answered = False
        while time.monotonic() < deadline and not answered:
            time.sleep(3)
            text = screenshot(shot("03-answered"))
            lowered = text.lower()
            answered = " ok" in lowered and PROMPT.lower()[:20] in lowered and "relaying" not in lowered
        ok &= report("03-answered", text, [PROMPT[:26]])
        log("answer seen on screen: %s" % answered)
        ok &= answered
        # 3. The shell is still the pane's own: a terminal-mode line runs in it, not in any TUI.
        type_text(window, "echo relay-shell-still-mine")
        press(window, "ctrl+shift+Return")
        time.sleep(4)
        text = screenshot(shot("04-shell"))
        ok &= report("04-shell", text, ["relay-shell-still-mine"])
        log("RESULT: " + ("all checks passed" if ok else "SOME CHECKS FAILED"))
        return 0 if ok else 2
    finally:
        process.terminate()
        try:
            process.wait(10)
        except subprocess.TimeoutExpired:
            process.kill()
        out = process.stdout.read() if process.stdout else ""
        (output / f"harness-run-{guest}.log").write_text(out, encoding="utf-8")
        log(f"relay output saved to {output / f'harness-run-{guest}.log'}")


if __name__ == "__main__":
    sys.exit(main())
