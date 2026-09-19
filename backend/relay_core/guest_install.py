# SPDX-License-Identifier: GPL-3.0-or-later
"""The marked settings installer for guest agents (GT7X, protocol 26.3).

Guest integration is **per project and off by default**; today the only way to turn it on is this
module's own command line (there is no Options pane for it yet):

    python -m relay_core.guest_install --project <dir> --on|--off|--status [--global --global-opt-in]

Turning it on writes Relay's hook and statusline commands into that project's
`.claude/settings.local.json` *additively*. **Local, never `settings.json`**: that file is the
shared, source-controlled one, and a commit of it would hand every teammate a hook command whose
`$RELAY_BACKEND_DIR` is empty on their machine. `settings.local.json` is the per-developer file
Claude Code keeps out of git, which is what a Relay pane's wiring is. Every entry already in the
file is preserved, and Relay's own entries carry a marker — the `--relay-guest` token on the
command — so turning it off removes exactly those and nothing else. The user's global
`~/.claude/settings.json` is touched only by an explicit second opt-in.

The file is re-serialised, not patched: the JSON is read, changed and written back with the
indentation it already used (tabs or n spaces, detected from the file) and its original mode. So
"everything else is preserved" means every *entry*, with its value and its order — not the file's
bytes. Comments, which JSON does not have and Claude Code does not read, would not survive.

What is installed, and why (protocol 26.4):

* `hooks.PermissionRequest` — the hook Claude Code fires only when it is actually about to ask
  the user for permission. The shim forwards it and holds it open while the pane asks; nothing is
  ever auto-approved. `PreToolUse` is *not* installed: it fires before every tool call, including
  the ones the user's own permission rules allow silently, and holding each of those open would
  stall the guest on tools it never needed to ask about.
* `hooks.UserPromptSubmit`, `hooks.Stop`, `hooks.Notification` — the turn brackets and the
  guest's own notifications. Each returns immediately; they only move pane state.
* `statusLine` — the same module in statusline mode, so the pane's guest chip gets the model and
  the context share while claude still renders its own statusline (the shim prints one
  passthrough line). `statusLine` holds a single command, so a user's own statusline is *kept*
  rather than overwritten; the chip then simply has nothing to show.

Every command starts with a shell guard, because a settings file is read by every claude started
in that project, including ones with no Relay around them: with `RELAY_GUEST_EVENT` or
`RELAY_BACKEND_DIR` unset the command exits 0 having done nothing. Without the guard the old
`"$RELAY_PYTHON" -m …` form expanded to an empty command — exit 127, and an error on the user's
screen at every tool call. It is also run by absolute path, so no `PYTHONPATH` is needed.

Claude Code's own default hook timeout is 600 s, so each entry carries an explicit `timeout`: the
permission question a little above the shim's own wait, the rest a few seconds.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import sys
import tempfile

MARKER = "--relay-guest"          # the token that marks a Relay-installed entry, in the command
HOOK_EVENTS = ("PermissionRequest", "UserPromptSubmit", "Stop", "Notification")
# Seconds Claude Code waits for each. The permission question outlives the shim's own wait
# (guest_hook.PERMISSION_TIMEOUT_DEFAULT, 120 s) so the shim, not claude, decides when to give up.
HOOK_TIMEOUTS: dict[str, int] = {"PermissionRequest": 180}
HOOK_TIMEOUT_DEFAULT = 10
# The guard, then the shim by absolute path. `$RELAY_BACKEND_DIR` and `$RELAY_GUEST_EVENT` are
# exported by the pane that started the shell (Pane::startTerminal, 26.2).
GUARD = '[ -n "$RELAY_GUEST_EVENT" ] && [ -n "$RELAY_BACKEND_DIR" ] || exit 0'
RUN = 'exec "${RELAY_PYTHON:-python3}" "$RELAY_BACKEND_DIR/relay_core/guest_hook.py"'
STATUSLINE_COMMAND = GUARD + "; " + RUN + " statusline " + MARKER
# The per-developer file, which Claude Code keeps out of git. `settings.json` is shared.
SETTINGS_RELATIVE = Path(".claude") / "settings.local.json"
# The global file has no ".local" variant: ~/.claude/settings.json *is* the user's own.
GLOBAL_SETTINGS_RELATIVE = Path(".claude") / "settings.json"


def hook_command(event: str) -> str:
    """The command one hook entry runs. Not a `.format` template: the command itself holds
    `${RELAY_PYTHON:-python3}`, and braces in a format string are not braces."""
    return GUARD + "; " + RUN + " " + event + " " + MARKER


class SettingsError(Exception):
    """The settings file exists but is not a JSON object; nothing is ever written over it."""


def project_settings_path(project_dir) -> Path:
    return Path(project_dir) / SETTINGS_RELATIVE


def global_settings_path(home=None) -> Path:
    return Path(home if home is not None else os.path.expanduser("~")) / GLOBAL_SETTINGS_RELATIVE


# ----- reading and writing ------------------------------------------------------------------


def load(path: Path) -> dict:
    """The settings object at `path`; `{}` when the file is not there yet."""
    if not path.exists():
        return {}
    try:
        with path.open("r", encoding="utf-8") as handle:
            value = json.load(handle)
    except ValueError as error:
        raise SettingsError(f"{path} is not valid JSON ({error}); Relay left it alone.") from None
    if not isinstance(value, dict):
        raise SettingsError(f"{path} does not hold a JSON object; Relay left it alone.")
    return value


def save(path: Path, settings: dict) -> None:
    """Replace the file atomically, so a claude reading it never sees half a settings object.

    The file's own indentation and its mode are carried over: mkstemp makes a 0600 file, and
    dropping a settings file to 0600 (or re-indenting a two-space file to four) is a change the
    user never asked for and would find in `git status` — or, worse, would not.
    """
    indent, newline, mode = _format_of(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name + "-", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(settings, handle, indent=indent, ensure_ascii=False)
            if newline:
                handle.write("\n")
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


_INDENT = re.compile(r'^([ \t]+)"', re.MULTILINE)


def _format_of(path: Path) -> tuple[int | str, bool, int]:
    """(indent for json.dump, trailing newline, mode) of the file as it is now.

    A file that is not there yet is two-space, newline-terminated and 0666 minus the umask — the
    mode any other tool would have given it.
    """
    try:
        text = path.read_text(encoding="utf-8")
        mode = os.stat(path).st_mode & 0o7777
    except OSError:
        umask = os.umask(0o077)
        os.umask(umask)
        return 2, True, 0o666 & ~umask
    match = _INDENT.search(text)
    if match is None:
        indent: int | str = 2
    elif "\t" in match.group(1):
        indent = "\t"
    else:
        indent = len(match.group(1))
    return indent, text.endswith("\n"), mode


# ----- the marker ---------------------------------------------------------------------------


def marked(entry) -> bool:
    """Whether one hook command object is Relay's own: its command carries the marker token."""
    return (isinstance(entry, dict) and isinstance(entry.get("command"), str)
            and MARKER in entry["command"].split())


def _group_is_ours(group) -> bool:
    commands = group.get("hooks") if isinstance(group, dict) else None
    return (isinstance(commands, list) and bool(commands)
            and all(marked(command) for command in commands))


def is_installed(path: Path) -> bool:
    """Whether Relay's marked entries are present in the settings file at `path`."""
    try:
        settings = load(path)
    except SettingsError:
        return False
    return bool(_marked_hooks(settings)) or marked(settings.get("statusLine"))


def _marked_hooks(settings: dict) -> list[tuple[str, int]]:
    """(event, index) of every matcher group that is entirely Relay's."""
    hooks = settings.get("hooks")
    found: list[tuple[str, int]] = []
    if isinstance(hooks, dict):
        for event, groups in hooks.items():
            if isinstance(groups, list):
                found += [(event, index) for index, group in enumerate(groups) if _group_is_ours(group)]
    return found


# ----- install ------------------------------------------------------------------------------


def install(path: Path) -> dict:
    """Add Relay's marked entries to `path`, preserving everything already there.

    Idempotent: a marked entry already in place is replaced by the current command, so a
    re-install is one entry per event, not a pile. A user's own `statusLine` is kept, because
    there is only one slot for it and it is not Relay's to take.
    """
    settings = load(path)
    hooks = settings.get("hooks")
    if hooks is not None and not isinstance(hooks, dict):
        raise SettingsError(f"{path}: \"hooks\" is not an object; Relay left it alone.")
    if hooks is None:
        hooks = {}
        settings["hooks"] = hooks
    added = []
    for event in HOOK_EVENTS:
        groups = hooks.get(event)
        if groups is None:
            groups = []
            hooks[event] = groups
        if not isinstance(groups, list):
            raise SettingsError(f"{path}: \"hooks.{event}\" is not a list; Relay left it alone.")
        # Drop a marked group from an earlier install, then append the current command.
        hooks[event] = [group for group in groups if not _group_is_ours(group)]
        hooks[event].append({"hooks": [{"type": "command", "command": hook_command(event),
                                        "timeout": HOOK_TIMEOUTS.get(event, HOOK_TIMEOUT_DEFAULT)}]})
        added.append(f"hooks.{event}")
    statusline = settings.get("statusLine")
    if statusline is None or marked(statusline):
        settings["statusLine"] = {"type": "command", "command": STATUSLINE_COMMAND}
        statusline_result = "installed"
        added.append("statusLine")
    else:
        statusline_result = "kept"   # the user's own statusline: Relay does not overwrite it
    save(path, settings)
    return {"path": str(path), "installed": added, "statusline": statusline_result, "ok": True}


# ----- remove -------------------------------------------------------------------------------


def remove(path: Path) -> dict:
    """Remove exactly Relay's marked entries; leave every other entry as it was (the file is
    re-serialised, see `save`, so entries survive and formatting is reproduced, not preserved).

    A group that holds only marked commands goes whole; a marked command inside a group that
    also holds the user's own is dropped from it, and the group is kept.
    """
    if not path.exists():
        return {"path": str(path), "removed": [], "statusline": "none", "ok": True, "changed": False}
    settings = load(path)
    removed = []
    hooks = settings.get("hooks")
    if isinstance(hooks, dict):
        for event in list(hooks):
            groups = hooks.get(event)
            if not isinstance(groups, list):
                continue
            kept_groups = []
            for group in groups:
                if _group_is_ours(group):
                    removed.append(f"hooks.{event}")
                    continue
                commands = group.get("hooks") if isinstance(group, dict) else None
                if isinstance(commands, list) and any(marked(command) for command in commands):
                    group["hooks"] = [command for command in commands if not marked(command)]
                    removed.append(f"hooks.{event}")
                    if not group["hooks"]:
                        continue     # a group Relay emptied is Relay's to drop
                kept_groups.append(group)
            if kept_groups:
                hooks[event] = kept_groups
            else:
                del hooks[event]
        if not hooks:
            del settings["hooks"]
    statusline = settings.get("statusLine")
    if marked(statusline):
        del settings["statusLine"]
        removed.append("statusLine")
        statusline_result = "removed"
    else:
        statusline_result = "kept" if statusline is not None else "none"
    changed = bool(removed)
    if changed:
        save(path, settings)
    return {"path": str(path), "removed": removed, "statusline": statusline_result,
            "ok": True, "changed": changed}


# ----- status -------------------------------------------------------------------------------


def status(path: Path) -> dict:
    try:
        settings = load(path)
    except SettingsError as error:
        return {"path": str(path), "installed": False, "error": str(error), "ok": False}
    events = sorted({event for event, _index in _marked_hooks(settings)})
    return {"path": str(path), "installed": bool(events) or marked(settings.get("statusLine")),
            "events": events, "statusline": marked(settings.get("statusLine")), "ok": True}


# ----- the command line ---------------------------------------------------------------------


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Install or remove Relay's guest hooks (GT7X).")
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument("--project", help="the project directory whose .claude/settings.json to touch")
    target.add_argument("--global", dest="global_", action="store_true",
                        help="the user's ~/.claude/settings.json; needs --global-opt-in")
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--on", action="store_true", help="install the marked entries")
    action.add_argument("--off", action="store_true", help="remove exactly the marked entries")
    action.add_argument("--status", action="store_true", help="report what is installed")
    parser.add_argument("--global-opt-in", action="store_true",
                        help="the explicit second opt-in the global settings file requires")
    parser.add_argument("--home", help="the home directory to use for --global (tests, alternate homes)")
    args = parser.parse_args(argv)

    if args.global_ and not args.global_opt_in:
        print(json.dumps({"ok": False, "error": "The global settings file needs the explicit "
                                                 "--global-opt-in; the project file does not."}))
        return 2
    path = global_settings_path(args.home) if args.global_ else project_settings_path(args.project)
    try:
        if args.on:
            result = install(path)
        elif args.off:
            result = remove(path)
        else:
            result = status(path)
    except SettingsError as error:
        print(json.dumps({"ok": False, "path": str(path), "error": str(error)}))
        return 1
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    sys.exit(main())
