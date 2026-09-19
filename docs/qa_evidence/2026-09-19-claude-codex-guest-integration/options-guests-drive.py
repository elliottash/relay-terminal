#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Drive the Options › Guests section on a live Relay and photograph what it does (GT7X, 26.4).

The section's rows are a front end for two command lines, so this harness drives them the way a
person would and checks both ends: the screen (OCR of each picture, with the run's output as the
evidence's own verification) and the files the rows claim to touch.

The run is as isolated as the channel harness's (claude-hooks-drive.py): a fresh temp root for
HOME/XDG_*/TMPDIR, and a `relay --fresh -w <project>` whose project carries a hook of the user's
own in **both** Claude settings files — `.claude/settings.local.json`, which is the per-developer
file Relay writes (so the "install" pictures show its marked entries landing next to a real file,
not an empty one), and `.claude/settings.json`, the shared source-controlled file Relay must never
touch, checked byte for byte at the end — and whose `$HOME/.codex/config.toml` holds a user's own
`notify`, which is the conflict the Codex row must refuse to take.

Rows are driven by keyboard (the Options search box: type, Enter), which is the path the pane
promises every setting; the Guests tab is reached with the pane's own Left/Right tab keys.

    xvfb-run -a python3 options-guests-drive.py <output-dir>
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

REPO = Path(__file__).resolve().parents[3]
RELAY = REPO / "build" / "relay"

# What each step waits for the QProcess-backed rows to finish their work: python3 -S starts in
# well under a second, and the notice lands with the finished() the harness waits out.
SETTLE = 5.0


def log(message: str) -> None:
    print(f"[drive] {message}", flush=True)


FAILURES: list[str] = []


def check(name: str, ok: bool, what: str) -> None:
    log(f"{'PASS' if ok else 'FAIL'} {name}: {what}")
    if not ok:
        FAILURES.append(f"{name}: {what}")


# ----- the isolated session ------------------------------------------------------------------


def make_isolation() -> dict:
    root = Path(tempfile.mkdtemp(prefix="relay-qa-options-"))
    home = root / "home"
    for name in ("home", "config", "data", "run", "tmp"):
        (root / name).mkdir(parents=True, exist_ok=True)
    os.chmod(root / "run", 0o700)
    # The project the window opens in: a settings file with a hook of the user's own, so the
    # install lands additively next to it and leaves it in place.
    project = root / "project"
    (project / ".claude").mkdir(parents=True, exist_ok=True)
    (project / ".claude" / "settings.local.json").write_text(json.dumps({
        "hooks": {"PreToolUse": [{"matcher": "Bash", "hooks": [
            {"type": "command", "command": "echo my-own-hook >/dev/null"}]}]},
    }, indent=2) + "\n", encoding="utf-8")
    # The shared, source-controlled file. Relay writes the `.local` one precisely so a commit of
    # this one cannot hand a teammate a hook that only works inside a Relay pane (26.3), so the
    # run's last check is that this file came through untouched.
    (project / ".claude" / "settings.json").write_text(json.dumps({
        "hooks": {"Stop": [{"hooks": [
            {"type": "command", "command": "echo shared-hook >/dev/null"}]}]},
    }, indent=2) + "\n", encoding="utf-8")
    # A codex config with the user's own notify: the one entry Relay must never take.
    (home / ".codex").mkdir(parents=True, exist_ok=True)
    (home / ".codex" / "config.toml").write_text(
        'notify = ["my-own-notifier"]\nmodel = "gpt-5.2"\n', encoding="utf-8")
    (root / "config" / "RelayTerminal").mkdir(parents=True, exist_ok=True)
    (root / "config" / "RelayTerminal" / "relay.conf").write_text(
        "[instructions]\nonboarded=true\n", encoding="utf-8")
    environment = dict(os.environ)
    environment.update({
        "HOME": str(home),
        "XDG_CONFIG_HOME": str(root / "config"),
        "XDG_DATA_HOME": str(root / "data"),
        "XDG_RUNTIME_DIR": str(root / "run"),
        "TMPDIR": str(root / "tmp"),
        "RELAY_KEYRING": "off",
        "RELAY_LOCAL_MODELS": str(root / "data" / "local-models.json"),
    })
    for name in ("RELAY_DATA_DIR", "RELAY_ENGINE_CORE", "PYTHONPATH"):
        environment.pop(name, None)
    environment["RELAY_DATA_DIR"] = str(REPO)
    return {"root": root, "env": environment, "project": project, "home": home}


# ----- driving the window --------------------------------------------------------------------


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


def press(window: str, *keys: str) -> None:
    xdo("windowactivate", "--sync", window)
    for name in keys:
        xdo("key", "--window", window, name)
        time.sleep(0.35)


def type_text(window: str, text: str) -> None:
    xdo("windowactivate", "--sync", window)
    xdo("type", "--window", window, "--delay", "40", text)


def search(window: str, text: str) -> None:
    """Put `text` in the Options search box and leave it highlighted: the box keeps the focus the
    pane gives it on open, so typing reaches it and Return activates the first hit."""
    type_text(window, text)
    time.sleep(0.6)


def screenshot(path: Path) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(["import", "-window", "root", str(path)], text=True, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(f"screenshot failed: {result.stderr}")
    log(f"wrote {path.name}")
    return ocr(path)


def ocr(path: Path) -> str:
    return subprocess.run(["tesseract", str(path), "-"], text=True, capture_output=True).stdout


def shows(text: str, *needles: str) -> bool:
    missing = [needle for needle in needles if needle not in text]
    if missing:
        log(f"screen is missing {missing}")
    return not missing


# ----- the run -------------------------------------------------------------------------------


def main() -> int:
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
    output.mkdir(parents=True, exist_ok=True)
    if not RELAY.exists():
        log(f"missing {RELAY}: build the worktree first")
        return 1
    isolation = make_isolation()
    environment = isolation["env"]
    project_settings = isolation["project"] / ".claude" / "settings.local.json"
    shared_settings = isolation["project"] / ".claude" / "settings.json"
    shared_before = shared_settings.read_bytes()
    codex_config = isolation["home"] / ".codex" / "config.toml"
    codex_before = codex_config.read_bytes()
    log(f"isolated session at {isolation['root']}")
    log(f"project {isolation['project']}, codex config {codex_config}")

    process = subprocess.Popen([str(RELAY), "--fresh", "-w", str(isolation["project"])],
                               env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True)
    log(f"relay pid {process.pid}")
    try:
        window = window_id()
        log(f"window {window}")
        time.sleep(6)   # let the pane settle before the keys start

        # Options, then the Guests tab with the pane's own tab keys (search stays empty, so
        # Left/Right move between sections). Arrival is what re-reads --status.
        press(window, "ctrl+shift+o")
        time.sleep(1.0)
        section_text = ""
        probe = isolation["root"] / "tmp" / "probe.png"   # one scratch picture, reused
        for _ in range(12):
            press(window, "Right")
            time.sleep(0.4)
            if subprocess.run(["import", "-window", "root", str(probe)]).returncode == 0:
                section_text = ocr(probe)
            if "Claude Code in this project" in section_text:
                break
        time.sleep(2.5)   # the status reads that arrival fires
        text = screenshot(output / "options-01-guests-section.png")
        check("01-section", shows(text, "Claude Code in this project", "Codex notifications",
                                  "Also in"), "the Guests rows are on screen")
        check("01-bridge-row", shows(text, "as Relay diffs"),
              "the IDE bridge row is on screen (nothing else can turn `guests/claude_bridge` on)")

        # 2. The global row while the project row is off: the second opt-in must refuse, say why,
        #    and leave both rows as they were. (Each later step's Escape clears the previous
        #    search — an empty-search Escape is what closes the pane, so it is never pressed.)
        search(window, "also in")
        press(window, "Return")
        time.sleep(SETTLE)
        text = screenshot(output / "options-02-global-guarded.png")
        # The notice's first words clip at the pane edge in OCR, so the assertion is on its tail.
        check("02-global-guard", shows(text, "add to the project ones"),
              "the guard notice is on screen")
        settings_file = isolation["root"] / "config" / "RelayTerminal" / "relay.conf"
        check("02-global-not-stored",
              "guests/global_install" not in settings_file.read_text(encoding="utf-8"),
              "the refused choice was never written to settings")

        # 3. The project row on, by keyboard: the file is the judge.
        press(window, "Escape")
        search(window, "claude code")
        press(window, "Return")
        time.sleep(SETTLE)
        installed = project_settings.read_text(encoding="utf-8")
        check("03-marker", "--relay-guest" in installed,
              "the marked entries are in the project's settings.local.json")
        check("03-additive", "my-own-hook" in installed, "the user's own hook survived")
        # The file is JSON, so the command's own quotes are escaped in it: the event name follows
        # `guest_hook.py\"`, not `guest_hook.py`.
        marked = [line for line in installed.splitlines() if "--relay-guest" in line]
        def installs(event: str) -> bool:
            return any(f'guest_hook.py\\" {event} ' in line for line in marked)
        check("03-permission-request", installs("PermissionRequest"),
              "the permission hook Relay installed is PermissionRequest")
        check("03-no-pretooluse", not installs("PreToolUse"),
              "PreToolUse is not installed: it fires before every tool call, allowed ones too")
        check("03-shared-untouched", shared_settings.read_bytes() == shared_before,
              "the shared .claude/settings.json is byte-for-byte unchanged")
        text = screenshot(output / "options-03-project-on.png")
        check("03-notice", shows(text, "installed in"), "the notice names the file it wrote")

        # 4. Codex, into a config that has the user's own notify: a conflict is reported, the
        #    user's file is byte-for-byte what it was, and the row goes back to off.
        press(window, "Escape")
        search(window, "codex notifications")
        press(window, "Return")
        time.sleep(SETTLE)
        check("04-config-untouched", codex_config.read_bytes() == codex_before,
              "~/.codex/config.toml is byte-for-byte unchanged")
        text = screenshot(output / "options-04-codex-conflict.png")
        check("04-conflict-notice", shows(text, "already sets notify"),
              "the conflict message is on screen")

        # 5. Off again, to show the other direction on the same file.
        press(window, "Escape")
        search(window, "claude code")
        press(window, "Return")
        time.sleep(SETTLE)
        removed = project_settings.read_text(encoding="utf-8")
        check("05-removed", "--relay-guest" not in removed and "my-own-hook" in removed,
              "the marked entries came out; the user's own hook stayed")
        check("05-shared-still-untouched", shared_settings.read_bytes() == shared_before,
              "the shared .claude/settings.json was never written, in either direction")
        screenshot(output / "options-05-project-off.png")
    finally:
        process.terminate()
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            process.kill()
        log("relay stopped")
    log(f"{'ALL CHECKS PASSED' if not FAILURES else 'FAILURES: ' + '; '.join(FAILURES)}")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
