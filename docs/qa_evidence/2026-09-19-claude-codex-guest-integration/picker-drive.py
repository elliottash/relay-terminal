#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Drive the model picker's guest rows on a live Relay pane and photograph what happens (GT7X, 26.9).

The picker route is the front door now (owner, 2026-09-19): "Claude Code" is a row in the pane's
model box, and picking it makes the pane run the CLI itself, in its own shell, configured on the
command line. This harness starts the built `relay` under Xvfb with an isolated HOME/XDG/TMPDIR,
puts a **stand-in `claude`** on PATH — a shell script that prints its argv and the two bridge
variables, then echoes every line it is given, and exits on `/exit` — and then does exactly what a
user does: `/model claude` in the prompt box (the picker row's keyboard twin), a terminal-mode line
and a prompt through the same box, and a pick of a preset to leave. The stand-in makes the
launch's contract visible on the screen and in a file: the `--settings <file>` argument, the
`--dangerously-skip-permissions` flag, `CLAUDE_CODE_SSE_PORT` from a sidecar this run started,
and the `!echo …` / plain lines the composer typed into it (26.8).

Each picture is checked, not just taken: the harness OCRs the screenshot and logs what the strip
and the terminal actually say, and it reads the settings file the launch wrote.

    xvfb-run -a -s "-screen 0 1600x1000x24" python3 picker-drive.py <output-dir>

Nothing of the user's real session is read: the whole run happens under a fresh temp root, with
no real claude anywhere on the PATH the pane's shell gets.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

REPO = Path(__file__).resolve().parents[3]
RELAY = REPO / "build" / "relay"
BACKEND = REPO / "backend"
STRIP_ROWS = (804, 840)

STAND_IN = r'''#!/bin/bash
# A stand-in Claude Code for the picker harness: shows what it was started with, then behaves
# like a TUI that reads lines. Every line it gets is echoed back with a marker; /exit ends it.
# A script runs as `bash /path/claude`, whose argv[0] the pane's classifier (26.1) rightly does
# not read as Claude Code; the real CLI's process is called `claude`. So the first thing it does
# is become one: re-exec under that name, as `exec -a claude` did in the hooks harness.
[ -n "$FAKE_CLAUDE_REEXEC" ] || { export FAKE_CLAUDE_REEXEC=1; exec -a claude bash "$0" "$@"; }
echo "fake-claude argv: $*"
echo "fake-claude CLAUDE_CODE_SSE_PORT=${CLAUDE_CODE_SSE_PORT:-unset} ENABLE_IDE_INTEGRATION=${ENABLE_IDE_INTEGRATION:-unset}"
printf '%s\n' "$*" > "$FAKE_CLAUDE_LOG.argv"
printf 'port=%s ide=%s\n' "${CLAUDE_CODE_SSE_PORT:-unset}" "${ENABLE_IDE_INTEGRATION:-unset}" > "$FAKE_CLAUDE_LOG.env"
while IFS= read -r line; do
  line=${line%$'\e'}   # a /command is followed by Esc, which dismisses the real TUI's popup (26.8)
  printf 'fake-claude got: [%s]\n' "$line"
  printf '%s\n' "$line" >> "$FAKE_CLAUDE_LOG.lines"
  [ "$line" = "/exit" ] && exit 0
done
'''


def log(message: str) -> None:
    print(f"[drive] {message}", flush=True)


# ----- the isolated session -----------------------------------------------------------------


def make_isolation() -> dict:
    root = Path(tempfile.mkdtemp(prefix="relay-qa-pick-", dir="/tmp/claude-1000" if Path("/tmp/claude-1000").is_dir() else None))
    home = root / "home"
    for name in ("home", "config", "data", "run", "tmp", "workspace", "bin"):
        (root / name).mkdir(parents=True, exist_ok=True)
    (home / ".claude").mkdir(parents=True, exist_ok=True)
    os.chmod(root / "run", 0o700)
    # The user's own statusline in their global settings: the launch must keep it (26.9), so the
    # settings file the pane writes must carry the hooks and *no* statusLine.
    (home / ".claude" / "settings.json").write_text(json.dumps({
        "statusLine": {"type": "command", "command": "echo my-own-statusline"},
        "permissions": {"allow": ["Bash(ls:*)"]},
    }, indent=2) + "\n", encoding="utf-8")
    # And the retired installer's marked entries in the workspace's local file: the launch must
    # remove exactly them (they would run every hook twice beside the launch file).
    local = root / "workspace" / ".claude" / "settings.local.json"
    local.parent.mkdir(parents=True)
    local.write_text(json.dumps({"permissions": {"allow": ["Bash(git status:*)"]}}, indent=2) + "\n")
    subprocess.run([sys.executable, "-c",
                    "import sys; from pathlib import Path; from relay_core import guest_install as g; "
                    "g.install(Path(sys.argv[1]))", str(local)],
                   check=True, env={**os.environ, "PYTHONPATH": str(BACKEND)})
    stand_in = root / "bin" / "claude"
    stand_in.write_text(STAND_IN, encoding="utf-8")
    stand_in.chmod(0o755)
    (root / "config" / "RelayTerminal").mkdir(parents=True, exist_ok=True)
    (root / "config" / "RelayTerminal" / "relay.conf").write_text(
        "[instructions]\nonboarded=true\n[isolation]\nenabled=false\n", encoding="utf-8")
    environment = dict(os.environ)
    # The stand-in must be the only `claude` and `codex` the pane's shell can find: the real ones
    # would cost a session. Only directories without either go on PATH, plus the stand-in's.
    path = [entry for entry in os.environ.get("PATH", "").split(":")
            if entry and not (Path(entry) / "claude").exists() and not (Path(entry) / "codex").exists()]
    environment.update({
        "HOME": str(home),
        "XDG_CONFIG_HOME": str(root / "config"),
        "XDG_DATA_HOME": str(root / "data"),
        "XDG_RUNTIME_DIR": str(root / "run"),
        "TMPDIR": str(root / "tmp"),
        "RELAY_KEYRING": "off",
        "RELAY_LOCAL_MODELS": str(root / "data" / "local-models.json"),
        "PATH": ":".join([str(root / "bin"), *path]),
        "FAKE_CLAUDE_LOG": str(root / "fake-claude"),
    })
    for name in ("RELAY_DATA_DIR", "RELAY_ENGINE_CORE", "PYTHONPATH", "CLAUDE_CODE_SSE_PORT", "ENABLE_IDE_INTEGRATION"):
        environment.pop(name, None)
    environment["RELAY_DATA_DIR"] = str(REPO)
    return {"root": root, "env": environment, "workspace": root / "workspace", "local": local}


# ----- talking to the pane ------------------------------------------------------------------


def pane_runtime_dirs(environment: dict) -> list[Path]:
    tmp = Path(environment["TMPDIR"])
    return [entry for entry in sorted(tmp.glob("relay-??????")) if (entry / "owner").exists()]


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
    return ocr(path)


def ocr(path: Path) -> str:
    return subprocess.run(["tesseract", str(path), "-"], text=True, capture_output=True).stdout


def strip_text(path: Path, rows: tuple[int, int]) -> str:
    top, bottom = rows
    result = subprocess.run(["convert", str(path), "-crop", f"1600x{bottom - top}+0+{top}",
                             "+repage", "-resize", "250%", "-colorspace", "Gray", "-normalize", "-"],
                            capture_output=True)
    return subprocess.run(["tesseract", "-", "-", "--psm", "6"], input=result.stdout,
                          capture_output=True).stdout.decode("utf-8", "replace")


def report(name: str, text: str, expect: list[str], forbid: list[str] = ()) -> bool:
    found = [needle for needle in expect if needle in text]
    missing = [needle for needle in expect if needle not in text]
    log(f"{name}: found {found or 'nothing expected'}; missing {missing or 'nothing'}")
    for needle in forbid:
        if needle in text:
            log(f"{name}: UNEXPECTED {needle!r} is on screen")
            missing.append(needle)
    return not missing


def wait_for(path: Path, seconds: float = 30) -> bool:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if path.exists():
            return True
        time.sleep(0.3)
    return False


def lines_of(path: Path) -> list[str]:
    try:
        return path.read_text(encoding="utf-8").splitlines()
    except OSError:
        return []


# ----- the run ------------------------------------------------------------------------------


def main() -> int:
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
    output.mkdir(parents=True, exist_ok=True)
    if not RELAY.exists():
        log(f"missing {RELAY}: build the worktree first")
        return 1
    for tool in ("xdotool", "import", "tesseract", "convert"):
        if not shutil.which(tool):
            log(f"missing {tool}")
            return 1
    isolation = make_isolation()
    environment = isolation["env"]
    root = isolation["root"]
    log(f"isolated session at {root}")
    ok = True

    process = subprocess.Popen([str(RELAY), "--fresh", "-w", str(isolation["workspace"])],
                               env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log(f"relay pid {process.pid}")
    try:
        window = window_id()
        time.sleep(6)
        directories = pane_runtime_dirs(environment)
        if not directories:
            log("no pane runtime directory appeared")
            return 1
        pane = directories[0]
        log(f"pane runtime {pane}")

        # 1. Before: the box on "No stored keys" (no provider in this isolated home), and the
        #    prompt box empty. The guest rows are in the box; the fast path is /model claude.
        text = screenshot(output / "implementer-picker-01-before.png")
        ok &= report("01-before", text, [], ["fake-claude"])

        # 2. /model claude: the pane prepares the launch (settings file, legacy cleanup, the
        #    sidecar's port) and types the command line into its own shell.
        type_text(window, "/model claude")
        press(window, "Return")
        argv_log = root / "fake-claude.argv"
        if not wait_for(argv_log, 40):
            log("the stand-in claude was never started")
            text = screenshot(output / "implementer-picker-02-launch-failed.png")
            log(text[-1500:])
            return 1
        time.sleep(4)   # the program poll classifies it and the chip appears
        argv = lines_of(argv_log)[0] if lines_of(argv_log) else ""
        env = lines_of(root / "fake-claude.env")[0] if lines_of(root / "fake-claude.env") else ""
        log(f"stand-in argv: {argv}")
        log(f"stand-in env: {env}")
        settings_path = pane / "guest" / "claude-settings.json"
        settings = json.loads(settings_path.read_text(encoding="utf-8")) if settings_path.exists() else {}
        log(f"launch settings at {settings_path}: hooks={sorted(settings.get('hooks', {}))} statusLine={'statusLine' in settings}")
        ok &= report("02-argv", argv, ["--settings " + str(settings_path), "--dangerously-skip-permissions"])
        ok &= report("02-bridge-env", env, ["ide=true"], ["port=unset"])
        ok &= report("02-settings-hooks", " ".join(sorted(settings.get("hooks", {}))),
                     ["Notification", "PermissionRequest", "Stop", "UserPromptSubmit"])
        if "statusLine" in settings:
            log("02-settings: UNEXPECTED statusLine in the launch file — the user's own must be kept")
            ok = False
        # The retired installer's marked entries were removed from the workspace's local file, and
        # the user's own entry in it was kept.
        local = json.loads(isolation["local"].read_text(encoding="utf-8"))
        marked = any("--relay-guest" in json.dumps(group) for group in local.get("hooks", {}).values())
        log(f"legacy cleanup: marked entries left={marked}; own permission kept={local.get('permissions', {}).get('allow') == ['Bash(git status:*)']}")
        ok &= not marked and local.get("permissions", {}).get("allow") == ["Bash(git status:*)"]
        # The user's own global statusline is untouched.
        own = json.loads((root / "home" / ".claude" / "settings.json").read_text(encoding="utf-8"))
        ok &= own.get("statusLine", {}).get("command") == "echo my-own-statusline"
        text = screenshot(output / "implementer-picker-02-launched.png")
        strip = strip_text(output / "implementer-picker-02-launched.png", STRIP_ROWS)
        log(f"strip: {strip.strip()!r}")
        ok &= report("02-screen", text, ["fake-claude argv", "dangerously"])
        ok &= report("02-strip", strip, ["Claude"])

        # 3. A terminal-mode line from the prompt box: typed into the guest as `!<command>` (26.8).
        #    Ctrl+Shift+Return is the explicit terminal submit.
        type_text(window, "echo relay-terminal-line")
        press(window, "ctrl+shift+Return")
        time.sleep(3)
        got = lines_of(root / "fake-claude.lines")
        log(f"stand-in received so far: {got}")
        ok &= report("03-bang", "\n".join(got), ["!echo relay-terminal-line"])
        text = screenshot(output / "implementer-picker-03-terminal-line.png")
        report("03-screen", text, ["relay-terminal-line"])   # the picture; the file above is the proof

        # 4. A prompt from the same box, in agent mode (Ctrl+Return sends to the agent): the
        #    guest gets it as itself.
        type_text(window, "summarise this repository")
        press(window, "ctrl+Return")
        time.sleep(3)
        got = lines_of(root / "fake-claude.lines")
        ok &= report("04-prompt", "\n".join(got), ["summarise this repository"], ["!summarise"])
        text = screenshot(output / "implementer-picker-04-prompt.png")
        ok &= report("04-screen", text, ["summarise this repository"])

        # 5. A typed `!` line in auto mode is a terminal line too.
        type_text(window, "!pwd")
        press(window, "Return")
        time.sleep(3)
        got = lines_of(root / "fake-claude.lines")
        ok &= report("05-typed-bang", "\n".join(got), ["!pwd"])

        # 6. Leaving: `/model` with no stored model to switch to says so; picking the guest again
        #    says it is already there. Then the guest's own /exit ends it and the picker returns to
        #    the pane's model (here: no stored keys).
        type_text(window, "/model claude")
        press(window, "Return")
        time.sleep(2)
        text = screenshot(output / "implementer-picker-06-already.png")
        # The "Already on Claude Code." status is a short-lived line the OCR rarely catches; the
        # proof that nothing was relaunched is the stand-in's argv file staying the same one.
        report("06-already", text, ["lready on Claude Code"])
        ok &= len(lines_of(argv_log)) == 1
        type_text(window, "/exit")
        press(window, "Return")
        time.sleep(4)
        got = lines_of(root / "fake-claude.lines")
        ok &= report("06-exit", "\n".join(got), ["/exit"])
        text = screenshot(output / "implementer-picker-07-left.png")
        # The shell is back at its prompt and the box is back on the pane's own model (here the
        # isolated home has no key at all, so the box says so). The composer's height changed
        # with the guest gone, so the whole picture is read rather than the strip band.
        ok &= report("07-left", text, ["No stored keys", "Shell ready"], ["waiting for input"])
        log("RESULT: " + ("all checks passed" if ok else "SOME CHECKS FAILED"))
        return 0 if ok else 2
    finally:
        process.terminate()
        try:
            process.wait(10)
        except subprocess.TimeoutExpired:
            process.kill()
        out = process.stdout.read() if process.stdout else ""
        (output / "picker-run.log").write_text(out, encoding="utf-8")
        log(f"relay output saved to {output / 'picker-run.log'}")


if __name__ == "__main__":
    sys.exit(main())
