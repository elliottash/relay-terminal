# SPDX-License-Identifier: AGPL-3.0-or-later
"""The Relay-owned home of each guest CLI (card #5A37).

Relay's `main()` points `CLAUDE_CONFIG_DIR` and `CODEX_HOME` at `<data>/relay/guests/claude` and
`<data>/relay/guests/codex` (`guest.relay_home`) for everything it starts: the headless harnesses,
the model picker's launch line, and every pane shell, so a `claude` typed into a pane too. What a
guest writes (transcripts, file history, todos, shell snapshots, its threads database) therefore
lands under Relay, where Relay's own storage rules apply and Claude's 30-day `cleanupPeriodDays`
does not reach it.

The user's own setup still applies, because `ensure()` shares it into that home:

* **Links**, made only where nothing exists yet, for what the user edits and the CLI only reads:
  Claude's `CLAUDE.md`, `skills/`, `agents/`, `commands/`, `plugins/`, `output-styles/`,
  `keybindings.json`; Codex's `config.toml`, `AGENTS.md`, `prompts/`, `skills/`, `rules/`,
  `plugins/`. A link Relay made whose target has gone is removed again.
* **The login**, linked the same way (`.credentials.json`, `auth.json`), so no second sign-in is
  needed (owner, 2026-09-25: "relay can copy over the credential files"). If the CLI rewrites its
  credentials by renaming a new file over them, the link becomes a private copy — the copy the
  owner asked for; if it writes in place, both homes stay on one login.
* **`.claude.json`** (onboarding state, user-scope MCP servers) is copied once: Claude rewrites it
  on every start, so a link would have Relay's claude rewriting the user's file.
* **`settings.json` is generated**, not linked: the user's settings plus `cleanupPeriodDays`
  (`RETENTION_DAYS`), so no claude running in this home, typed or launched, prunes Relay's
  transcripts. Keys that Claude itself changes in the generated file (`/model`, `/config`) are
  remembered as an overlay in `STATE_NAME` and survive the next regeneration.

Nothing under the user's own directories is ever written. Conversations already there stay there:
`guest_sessions` indexes both homes, and `home_for_resume()` tells a launcher that a resumed id
lives only in the user's own directory, so that one launch runs there and continues its own file.

Run as `python -m relay_core.guest_home ensure` (Relay's `main()` does, once at start).
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys
import tempfile

from . import guest

RETENTION_DAYS = 36500          # Relay decides what to forget (#HEY7), not the CLI's 30 days
STATE_NAME = ".relay-home.json"  # {"written": <settings.json as last generated>, "overlay": {...}}
SETTINGS_NAME = "settings.json"

LINKS = {
    "claude": ("CLAUDE.md", "skills", "agents", "commands", "plugins", "output-styles",
               "keybindings.json", ".credentials.json"),
    "codex": ("config.toml", "AGENTS.md", "prompts", "skills", "rules", "plugins", "auth.json"),
}

_SESSION_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]{0,127}$")


# ----- helpers ------------------------------------------------------------------------------------


def _read_json(path: str):
    try:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return None


def _write_private(path: str, data: bytes) -> None:
    """Atomic, 0600, in the same directory."""
    directory = os.path.dirname(path)
    fd, tmp = tempfile.mkstemp(prefix=".relay-", dir=directory)
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(data)
        os.chmod(tmp, 0o600)
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def _dump(data: dict) -> bytes:
    return (json.dumps(data, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def claude_state_file(user_dir: str) -> str:
    """Where the user's claude keeps `.claude.json`: `~/.claude.json` for the default directory,
    inside the directory when `CLAUDE_CONFIG_DIR` named one."""
    default = os.path.join(os.path.expanduser("~"), ".claude")
    if os.path.normpath(user_dir) == os.path.normpath(default):
        return os.path.join(os.path.expanduser("~"), ".claude.json")
    return os.path.join(user_dir, ".claude.json")


# ----- the pieces -----------------------------------------------------------------------------


def link_shared(home: str, user_dir: str, names) -> dict:
    """Link each of `names` from `user_dir` into `home` where `home` has nothing by that name.
    Returns {"linked": [...], "removed": [...]}."""
    linked, removed = [], []
    for name in names:
        source = os.path.join(user_dir, name)
        target = os.path.join(home, name)
        if os.path.islink(target):
            if not os.path.exists(target) and os.readlink(target) == source:
                os.unlink(target)                  # ours, and its target is gone
                removed.append(name)
            continue
        if os.path.lexists(target) or not os.path.exists(source):
            continue                               # the home's own file, or nothing to share
        try:
            os.symlink(source, target)
            linked.append(name)
        except FileExistsError:                    # another ensure() got there first
            pass
    return {"linked": linked, "removed": removed}


def generate_settings(home: str, user_dir: str) -> bool:
    """Write `<home>/settings.json` = the user's settings + the overlay + the retention. True when
    the file changed."""
    path = os.path.join(home, SETTINGS_NAME)
    state_path = os.path.join(home, STATE_NAME)
    state = _read_json(state_path)
    state = state if isinstance(state, dict) else {}
    written = state.get("written") if isinstance(state.get("written"), dict) else None
    overlay = state.get("overlay") if isinstance(state.get("overlay"), dict) else {}

    if os.path.islink(path):
        return False                               # somebody linked it on purpose: theirs
    current = _read_json(path)
    user = _read_json(os.path.join(user_dir, SETTINGS_NAME))
    user = user if isinstance(user, dict) else {}
    if isinstance(current, dict):
        # What the CLI changed since the last generation is the user's choice inside this home.
        # With no record of a generation (a claude ran here before Relay prepared it), only what
        # differs from the user's own file counts as such a choice.
        before = written if written is not None else user
        for key, value in current.items():
            if key == "cleanupPeriodDays":
                continue
            if key not in before or before[key] != value:
                overlay[key] = value
    merged = dict(user)
    merged.update(overlay)
    kept = merged.get("cleanupPeriodDays")
    merged["cleanupPeriodDays"] = max(kept, RETENTION_DAYS) if isinstance(kept, int) else RETENTION_DAYS
    changed = current != merged
    if changed:
        _write_private(path, _dump(merged))
    if changed or state.get("written") != merged or state.get("overlay", {}) != overlay:
        _write_private(state_path, _dump({"written": merged, "overlay": overlay}))
    return changed


def copy_once(source: str, target: str) -> bool:
    if os.path.lexists(target) or not os.path.isfile(source):
        return False
    with open(source, "rb") as handle:
        data = handle.read()
    _write_private(target, data)
    return True


# ----- the entry points ---------------------------------------------------------------------------


def ensure(guest_id: str) -> dict:
    """Prepare this guest's Relay-owned home. {} when Relay does not own the guests' homes."""
    home = guest.relay_home(guest_id)
    if not home:
        return {}
    guest_id = guest.spec(guest_id).id
    user_dir = guest.user_config_dir(guest_id)
    os.makedirs(home, mode=0o700, exist_ok=True)
    for directory in (home, os.path.dirname(home)):   # makedirs gives parents the umask's mode
        os.chmod(directory, 0o700)
    report = {"guest": guest_id, "home": home, "user_dir": user_dir}
    if os.path.normpath(user_dir) == os.path.normpath(home):
        return report                              # nothing to share with itself
    report.update(link_shared(home, user_dir, LINKS[guest_id]))
    if guest_id == "claude":
        report["settings"] = generate_settings(home, user_dir)
        report["state_copied"] = copy_once(claude_state_file(user_dir),
                                           os.path.join(home, ".claude.json"))
    return report


def ensure_all() -> list[dict]:
    reports = []
    for guest_id in ("claude", "codex"):
        try:
            report = ensure(guest_id)
        except OSError as error:
            report = {"guest": guest_id, "error": str(error)}
        if report:
            reports.append(report)
    return reports


def _transcripts(guest_id: str, directory: str, session_id: str) -> list[str]:
    if guest_id == "claude":
        return glob.glob(os.path.join(glob.escape(directory), "projects", "*", session_id + ".jsonl"))
    return glob.glob(os.path.join(glob.escape(directory), "sessions", "*", "*", "*",
                                  f"rollout-*-{session_id}.jsonl"))


def home_for_resume(guest_id: str, session_id) -> str:
    """The user's own directory when `session_id` names a conversation that exists there and not
    in the Relay-owned home, so the launch that resumes it runs there; "" otherwise (resume in the
    Relay home as usual, or Relay does not own the homes)."""
    home = guest.relay_home(guest_id)
    if not home or not isinstance(session_id, str) or not _SESSION_ID.match(session_id):
        return ""
    user_dir = guest.user_config_dir(guest_id)
    if os.path.normpath(user_dir) == os.path.normpath(home):
        return ""
    if _transcripts(guest_id, home, session_id):
        return ""
    return user_dir if _transcripts(guest_id, user_dir, session_id) else ""


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Prepare the guests' Relay-owned homes (#5A37).")
    parser.add_argument("command", choices=["ensure"])
    parser.parse_args(argv)
    print(json.dumps({"ok": True, "homes": ensure_all()}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
