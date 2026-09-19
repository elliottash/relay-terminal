# SPDX-License-Identifier: GPL-3.0-or-later
"""Launch-time configuration for a guest picked in the model picker (GT7X, protocol 26.9).

The owner's direction (2026-09-19): **no per-project setup**. Nothing Relay needs a guest to
carry is written into the project's `.claude/` or the user's `~/.codex/config.toml` any more.
When a pane's model picker is set to Claude Code or Codex, the pane asks this module for the
command line to run in its own shell, and everything the guest has to know travels with that
one command:

* **Claude Code**: a settings file in the pane's runtime directory, handed over with
  `claude --settings <file>`. It carries exactly the hook and statusline entries the retired
  installer used to write (`guest_install.relay_entries()` — one source, so the shim's contract in
  26.4 is unchanged: the same guard, the same absolute path, the same `--relay-guest` token) and
  is read by this one claude only. `--dangerously-skip-permissions` goes with it: Relay's own
  agent runs without per-action approvals (WARP.md), and the owner wants the guest to move around
  the file system the same way. The IDE bridge's two variables are prefixed to the command line
  rather than exported into the shell, so no other program in that shell — and no shell started
  later — ever sees a port that may since have gone away.
* **Codex**: `-c key=value` overrides on the command line — the `notify` entry that turns a
  finished turn into a Relay notification and the `tui.notification_condition` it needs — plus
  `--dangerously-bypass-approvals-and-sandbox`, Codex's spelling of the same rule.

**The stopgap's leftovers.** Until this module, Options › Guests wrote marked entries into
`.claude/settings.local.json`, `~/.claude/settings.json` and `~/.codex/config.toml`. A claude
started with `--settings` *and* a project file still holding those entries would run every hook
twice — two permission questions for one tool call — so every launch first removes exactly the
marked entries from the three files (`guest_install.remove`, `guest_codex.disable`: the retired
installers' own "off"), and reports which files it touched. That migration is the reason those
two modules stay as libraries; their command lines are gone.

Verified against the installed CLIs on 2026-09-19 (Claude Code 2.1.278: `--settings
<file-or-json>`, `--dangerously-skip-permissions`, `-r`, `--fork-session`; Codex 0.155.1:
`-c <key=value>` on the plain launch and on `resume` / `fork`,
`--dangerously-bypass-approvals-and-sandbox` on all three).

Protocol: docs/AGENT-SESSIONS-PROTOCOL.md section 26.9. Card:
issues/features/2026-09-19-claude-codex-guest-integration.md (GT7X).
"""
from __future__ import annotations

import argparse
import json
import os
import shlex
import sys
import tempfile

from . import guest, guest_codex, guest_install

SETTINGS_DIR = "guest"                        # under the pane's runtime dir, mode 0700
CLAUDE_SETTINGS_NAME = "claude-settings.json"  # what `claude --settings` is handed
CLAUDE_BYPASS = "--dangerously-skip-permissions"
CODEX_BYPASS = "--dangerously-bypass-approvals-and-sandbox"
# `codex resume <id>` / `codex fork <id>`: the subcommand comes first and the flags after it,
# before the id (verified against `codex resume --help` / `codex fork --help`).
CODEX_SUBCOMMANDS = ("resume", "fork")


class LaunchError(Exception):
    """The launch cannot be prepared (no runtime dir, an unknown guest, an unwritable file)."""


# ----- Claude Code -----------------------------------------------------------------------------


def claude_settings(keep_user_statusline: bool = False) -> dict:
    """The settings object one launched claude reads: the retired installer's entries, verbatim.

    `keep_user_statusline` leaves `statusLine` out, because a command-line settings file wins over
    the user's own files and would replace a statusline they wrote themselves (the installer kept
    theirs, so the launch does too; the chip then simply has nothing to show).
    """
    entries = guest_install.relay_entries()
    if keep_user_statusline:
        entries.pop("statusLine", None)
    return entries


def user_statusline_present(cwd: str | None, home: str | None = None) -> bool:
    """Whether any of the files a claude in `cwd` reads carries a statusline that is not Relay's:
    `~/.claude/settings.json`, `<cwd>/.claude/settings.json`, `<cwd>/.claude/settings.local.json`.
    A file that cannot be read says nothing."""
    candidates = [guest_install.global_settings_path(home)]
    if cwd:
        candidates.append(guest_install.project_settings_path(cwd).with_name("settings.json"))
        candidates.append(guest_install.project_settings_path(cwd))
    for path in candidates:
        try:
            settings = guest_install.load(path)
        except (guest_install.SettingsError, OSError):
            continue
        statusline = settings.get("statusLine")
        if statusline is not None and not guest_install.marked(statusline):
            return True
    return False


def write_claude_settings(runtime_dir: str, cwd: str | None = None, home: str | None = None) -> str:
    """Write the launch settings file under the pane's runtime dir and return its path.

    Mode 0600 in a 0700 directory, replaced atomically: the file names nothing secret, but the
    runtime dir is the pane's own and a half-written file must never be what claude reads.
    """
    if not runtime_dir:
        raise LaunchError("a guest launch needs the pane's runtime directory.")
    directory = os.path.join(runtime_dir, SETTINGS_DIR)
    os.makedirs(directory, mode=0o700, exist_ok=True)
    os.chmod(directory, 0o700)
    path = os.path.join(directory, CLAUDE_SETTINGS_NAME)
    settings = claude_settings(keep_user_statusline=user_statusline_present(cwd, home))
    fd, temporary = tempfile.mkstemp(prefix=CLAUDE_SETTINGS_NAME + "-", dir=directory)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(settings, handle, indent=2)
            handle.write("\n")
        os.chmod(temporary, 0o600)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    return path


def claude_argv(settings_path: str, extra=()) -> list[str]:
    """`claude --settings <file> --dangerously-skip-permissions [extra…]`. `extra` is what a
    sessions row adds (`-r <id>`, `--fork-session`) and is passed through untouched."""
    return ["claude", "--settings", settings_path, CLAUDE_BYPASS, *extra]


# ----- Codex -----------------------------------------------------------------------------------


def codex_overrides(python: str | None = None) -> list[str]:
    """The `-c` overrides one launched codex gets: the `notify` entry pointing at
    `guest_codex.py notify` (an absolute interpreter and an absolute script, because codex execs it
    with no shell) and the notification condition that makes it fire in the focused terminal."""
    command = guest_codex.notify_command(python)
    return ["-c", f"{guest_codex.NOTIFY_KEY}={guest_codex._toml_array(command)}",
            "-c", f"{'.'.join(guest_codex.TUI_TABLE)}.{guest_codex.NOTIFICATION_CONDITION}"
                  f"=\"{guest_codex.NOTIFICATION_CONDITION_VALUE}\""]


def codex_argv(python: str | None = None, extra=()) -> list[str]:
    """`codex [resume|fork] -c … --dangerously-bypass-approvals-and-sandbox [rest…]`.

    A sessions row's `extra` starts with the subcommand (`resume <id>`, `fork <id>`), which has
    to stay in front of the flags; anything else is appended after them.
    """
    extra = list(extra)
    flags = [*codex_overrides(python), CODEX_BYPASS]
    if extra and extra[0] in CODEX_SUBCOMMANDS:
        return ["codex", extra[0], *flags, *extra[1:]]
    return ["codex", *flags, *extra]


# ----- the stopgap's leftovers ----------------------------------------------------------------


def clean_legacy(cwd: str | None, home: str | None = None) -> list[str]:
    """Remove the retired installers' marked entries from the files a guest started here would
    read, and return the paths that changed. Only Relay's own marked entries go; a file without
    them is not rewritten, and a file that cannot be read is left alone."""
    changed: list[str] = []
    candidates = [guest_install.global_settings_path(home)]
    if cwd:
        candidates.insert(0, guest_install.project_settings_path(cwd))
    for path in candidates:
        try:
            if guest_install.is_installed(path) and guest_install.remove(path).get("changed"):
                changed.append(str(path))
        except (guest_install.SettingsError, OSError):
            continue
    # Marked, not "enabled": the stopgap's entries are Relay's whatever interpreter they name.
    try:
        state = guest_codex.settings_state(home=home)
        if any(state.get("marked", {}).values()) and guest_codex.disable(home=home).get("changed"):
            changed.append(state["path"])
    except (guest_codex.CodexError, OSError):
        pass
    return changed


# ----- the command line ---------------------------------------------------------------------


def command_line(guest_id: str, runtime_dir: str, cwd: str | None = None, port: int = 0,
                 extra=(), home: str | None = None, python: str | None = None) -> dict:
    """Everything the pane needs to start `guest_id` in its shell, in one payload:

        {"guest", "argv", "env", "command", "settings", "legacy"}

    `env` is the IDE bridge's two variables for claude when `port` names a live bridge (empty
    otherwise, and always empty for codex); `command` is the one shell line — the assignments
    prefixed, every word quoted for the shell — that the pane types; `settings` is the claude
    settings file's path (empty for codex); `legacy` lists the stopgap files that were cleaned.
    """
    spec = guest.spec(guest_id)   # ValueError for anything the registry does not know
    legacy = clean_legacy(cwd, home)
    env: dict[str, str] = {}
    settings = ""
    if spec.id == "claude":
        settings = write_claude_settings(runtime_dir, cwd, home)
        argv = claude_argv(settings, extra)
        if port:
            env = guest.bridge_env("claude", port)
    else:
        argv = codex_argv(python, extra)
    words = [f"{key}={shlex.quote(value)}" for key, value in env.items()] + [shlex.quote(word) for word in argv]
    return {"guest": spec.id, "argv": argv, "env": env, "command": " ".join(words),
            "settings": settings, "legacy": legacy}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Prepare a guest's launch for a Relay pane and print it as JSON (GT7X, 26.9).")
    parser.add_argument("guest", help="claude or codex")
    parser.add_argument("--runtime-dir", required=True, help="the pane's runtime directory")
    parser.add_argument("--cwd", default="", help="the directory the guest starts in")
    parser.add_argument("--port", type=int, default=0, help="the IDE bridge's port, when it is up")
    parser.add_argument("--home", default=None, help="the home directory (tests, alternate homes)")
    parser.add_argument("--python", default=None, help="the interpreter codex's notify entry names")
    # Everything after `--` is the guest's own (`-r <id>`, `resume <id>`), split off before argparse
    # sees it: it would otherwise read `-r` as an option of its own.
    arguments = list(sys.argv[1:] if argv is None else argv)
    extra: list[str] = []
    if "--" in arguments:
        cut = arguments.index("--")
        arguments, extra = arguments[:cut], arguments[cut + 1:]
    args = parser.parse_args(arguments)
    try:
        result = command_line(args.guest, args.runtime_dir, args.cwd or None, args.port, extra,
                              home=args.home, python=args.python)
    except (LaunchError, ValueError, OSError) as error:
        print(json.dumps({"ok": False, "error": str(error)}))
        return 1
    print(json.dumps({**result, "ok": True}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
