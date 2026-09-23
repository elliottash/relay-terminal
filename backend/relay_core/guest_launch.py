# SPDX-License-Identifier: AGPL-3.0-or-later
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

**The model and the reasoning effort** (owner, 2026-09-19: "you should be able to pick the model
and reasoning effort for those"). The pane passes what Options is set to and it travels on the
same command line: claude takes `--model <alias|full name>` and `--effort <low|medium|high|xhigh|
max>`, codex `-m <slug>` and `-c model_reasoning_effort="<level>"`. A sessions row's own `extra`
wins: if it already names one of these, the pane's setting is not added a second time (claude
refuses a repeated `--model`, and a second `-c` for the same key is just noise).

**The stopgap's leftovers.** Until this module, Options › Guests wrote marked entries into
`.claude/settings.local.json`, `~/.claude/settings.json` and `~/.codex/config.toml`. A claude
started with `--settings` *and* a project file still holding those entries would run every hook
twice — two permission questions for one tool call — so every launch first removes exactly the
marked entries from the three files (`guest_install.remove`, `guest_codex.disable`: the retired
installers' own "off"), and reports which files it touched. That migration is the reason those
two modules stay as libraries; their command lines are gone.

Verified against the installed CLIs on 2026-09-19 (Claude Code 2.1.278: `--settings
<file-or-json>`, `--dangerously-skip-permissions`, `-r`, `--fork-session`, `--model <alias>` and
`--effort <level>` with levels low, medium, high, xhigh, max; Codex 0.155.1:
`-c <key=value>` on the plain launch and on `resume` / `fork`, `-m <slug>`,
`--dangerously-bypass-approvals-and-sandbox` and `--dangerously-bypass-hook-trust` on all three).

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
import uuid
from pathlib import Path

from . import guest, guest_codex, guest_install
from .guest_instructions import memory_instructions, memory_mode, own_memory

SETTINGS_DIR = "guest"                        # under the pane's runtime dir, mode 0700
CLAUDE_SETTINGS_NAME = "claude-settings.json"  # what `claude --settings` is handed
CLAUDE_BYPASS = "--dangerously-skip-permissions"
# The codex config key `-c` sets for the reasoning effort, the same one `codex app-server`'s
# `thread/start` takes in its `config` map.
CODEX_EFFORT_KEY = "model_reasoning_effort"
CODEX_BYPASS = "--dangerously-bypass-approvals-and-sandbox"
# Codex stops on a full-screen "Hooks need review" dialog whenever an enabled hook's hash is not
# the one it has on record — any plugin's hooks, not Relay's (Relay adds none; the owner met it
# through the Warp plugin's five). The guest already runs with approvals and the sandbox bypassed
# on the owner's instruction, so its own hooks run without that review too, for this invocation
# only: nothing is written to the trust records in ~/.codex/config.toml.
CODEX_HOOK_TRUST = "--dangerously-bypass-hook-trust"
# `codex resume <id>` / `codex fork <id>`: the subcommand comes first and the flags after it,
# before the id (verified against `codex resume --help` / `codex fork --help`).
CODEX_SUBCOMMANDS = ("resume", "fork")

# "Guests use memory from" (#MEMS; `guest_instructions.MEMORY_MODES`). With `relay`, the guest's own
# memory is switched off for this one launch — never in ~/.claude or ~/.codex. Verified against the
# installed CLIs on 2026-09-22:
# * Claude Code 2.1.280 reads `autoMemoryEnabled` ("When false, Claude will not read from or write to
#   the auto-memory directory") and `autoDreamEnabled` (background consolidation) from any settings
#   source, `--settings` included; `CLAUDE_CODE_DISABLE_AUTO_MEMORY` is checked before the settings
#   and a truthy value turns auto-memory off whatever they say. Both are used: the variable wins over
#   a user's own `CLAUDE_CODE_DISABLE_AUTO_MEMORY=0`, the settings keys cover a shell that drops it.
# * Codex 0.156.0: the `memories` feature flag and the `[memories]` table's `generate_memories` and
#   `use_memories`. All three are typed booleans — `-c memories.use_memories="x"` is refused at
#   startup — whereas an unknown key is silently ignored, so the type check is what proves the
#   names are real.
CLAUDE_MEMORY_OFF_SETTINGS = {"autoMemoryEnabled": False, "autoDreamEnabled": False}
CLAUDE_MEMORY_OFF_ENV = {"CLAUDE_CODE_DISABLE_AUTO_MEMORY": "1"}
CODEX_MEMORY_OFF = {"features.memories": False, "memories.generate_memories": False,
                    "memories.use_memories": False}


def codex_memory_overrides() -> list[str]:
    """The `-c` words that switch codex's own memory off for one invocation."""
    words: list[str] = []
    for key, value in CODEX_MEMORY_OFF.items():
        words += ["-c", f"{key}={json.dumps(value)}"]
    return words


class LaunchError(Exception):
    """The launch cannot be prepared (no runtime dir, an unknown guest, an unwritable file)."""


# ----- Claude Code -----------------------------------------------------------------------------


def claude_settings(keep_user_statusline: bool = False, own_memory: bool = True) -> dict:
    """The settings object one launched claude reads: the retired installer's entries, verbatim,
    plus `CLAUDE_MEMORY_OFF_SETTINGS` when the guest is not to use its own memory (#MEMS).

    `keep_user_statusline` leaves `statusLine` out, because a command-line settings file wins over
    the user's own files and would replace a statusline they wrote themselves (the installer kept
    theirs, so the launch does too; the chip then simply has nothing to show).
    """
    entries = guest_install.relay_entries()
    if keep_user_statusline:
        entries.pop("statusLine", None)
    if not own_memory:
        entries.update(CLAUDE_MEMORY_OFF_SETTINGS)
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


def _write_private(runtime_dir: str, name: str, text: str) -> str:
    """Write `text` to `<runtime_dir>/guest/<name>` and return the path.

    Mode 0600 in a 0700 directory, replaced atomically: the runtime dir is the pane's own and a
    half-written file must never be what a guest reads.
    """
    if not runtime_dir:
        raise LaunchError("a guest launch needs the pane's runtime directory.")
    directory = os.path.join(runtime_dir, SETTINGS_DIR)
    os.makedirs(directory, mode=0o700, exist_ok=True)
    os.chmod(directory, 0o700)
    path = os.path.join(directory, name)
    fd, temporary = tempfile.mkstemp(prefix=name + "-", dir=directory)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            handle.write(text)
        os.chmod(temporary, 0o600)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    return path


def write_claude_settings(runtime_dir: str, cwd: str | None = None, home: str | None = None,
                          own_memory: bool = True) -> str:
    """Write the launch settings file under the pane's runtime dir and return its path."""
    settings = claude_settings(keep_user_statusline=user_statusline_present(cwd, home),
                               own_memory=own_memory)
    return _write_private(runtime_dir, CLAUDE_SETTINGS_NAME, json.dumps(settings, indent=2) + "\n")


# ----- Relay memory for a TUI guest (#MEMS) ----------------------------------------------------

CLAUDE_MEMORY_NAME = "claude-memory.md"   # what `--append-system-prompt-file` is handed
CODEX_MEMORY_NAME = "codex-memory.md"     # what codex's developer instructions point at
CLAUDE_APPEND_FLAGS = ("--append-system-prompt", "--append-system-prompt-file")  # one of, never both


def suggest_command(guest_id: str, python: str | None = None) -> str:
    """The shell line a TUI guest runs to propose a fact about the user.

    A guest in the terminal has no relay_board bridge (that is the harness route's), so no
    `app_user_memory`; this line writes the same pending suggestion (`memory_suggestions.suggest`)
    into the global Board that Globals › User memory lists for Keep / Edit / No. The root is the
    one this launch resolved, so the guest's shell environment cannot send it elsewhere.
    """
    from . import aliases
    words = ["env", f"PYTHONPATH={Path(__file__).resolve().parents[1]}",
             f"RELAY_GLOBAL_SWITCHBOARD={aliases.global_root()}", python or sys.executable,
             "-m", "relay_core.guest_launch", "suggest-memory", "--source", guest_id]
    return " ".join(shlex.quote(word) for word in words)


def tui_memory_instructions(guest_id: str, workspace: str | None, mode,
                            python: str | None = None) -> str:
    """`guest_instructions.memory_instructions` for a guest started in the terminal: the same
    block, with the suggestion made through `suggest_command` instead of the bridge's tool."""
    line = suggest_command(guest_id, python)
    return memory_instructions(workspace or "", mode,
                               suggest=f"propose it by running `{line} '<the fact>'` with your "
                                       "shell tool (one short sentence about the user)")


def codex_user_developer_instructions(home: str | None = None) -> str:
    """The user's own top-level `developer_instructions` in ~/.codex/config.toml, which a `-c` for
    the same key would otherwise replace for this launch. "" when there is none or the file cannot
    be read. `$CODEX_HOME` is where codex itself looks when no `home` is given."""
    codex_home = os.environ.get("CODEX_HOME", "").strip() if home is None else ""
    path = (os.path.join(codex_home, "config.toml") if codex_home
            else guest_codex.settings_path(home))
    try:
        with open(path, encoding="utf-8") as handle:
            value = guest_codex._toml_load(handle.read()).get("developer_instructions")
    except (OSError, ValueError):
        return ""
    return value if isinstance(value, str) else ""


def codex_memory_word(path: str, home: str | None = None) -> list[str]:
    """`-c developer_instructions="…"` for a TUI codex: the user's own developer instructions, then
    a pointer to the memory file. The block itself is not put on the command line: the pane types
    that line into the shell, and 16 KiB of memories would land in the terminal and its history."""
    pointer = ("[Relay memory] Relay keeps what it knows about the user and this project in "
               f"{path}. Read that file with your shell tool before your first answer and follow "
               "its instructions for the rest of the session.")
    own = codex_user_developer_instructions(home).strip()
    text = own + "\n\n" + pointer if own else pointer
    # JSON escaping is a subset of TOML's basic strings; ensure_ascii off, so no surrogate pairs.
    return ["-c", "developer_instructions=" + json.dumps(text, ensure_ascii=False)]


def _names(extra) -> set:
    """The option names `extra` already carries, `--flag=value` counted as `--flag`."""
    return {word.split("=", 1)[0] for word in extra if isinstance(word, str) and word.startswith("-")}


def claude_argv(settings_path: str, extra=(), model: str | None = None,
                effort: str | None = None, memory_file: str | None = None) -> list[str]:
    """`claude --settings <file> --dangerously-skip-permissions [--model M] [--effort E]
    [--append-system-prompt-file F] [extra…]`.

    `extra` is what a sessions row adds (`-r <id>`, `--fork-session`) and is passed through
    untouched; a `--model` or `--effort` already in it is the row's own choice and wins, because
    claude refuses the same option twice. `memory_file` is Relay's memory block (#MEMS), left out
    when the row already appends a system prompt (claude refuses the two append flags together).
    """
    extra = list(extra)
    named = _names(extra)
    flags: list[str] = []
    if model and "--model" not in named:
        flags += ["--model", model]
    if effort and "--effort" not in named:
        flags += ["--effort", effort]
    if memory_file and not named & set(CLAUDE_APPEND_FLAGS):
        flags += ["--append-system-prompt-file", memory_file]
    return ["claude", "--settings", settings_path, CLAUDE_BYPASS, *flags, *extra]


# Flags in `extra` that already say which session this claude is: a new id must not be forced
# beside them (`--session-id` with `--resume` is an error unless the session is being forked).
_CLAUDE_SESSION_FLAGS = ("-r", "--resume", "-c", "--continue", "--session-id", "--fork-session",
                         "--from-pr", "--teleport")


def claude_session(extra=()) -> tuple[list[str], str]:
    """(`extra` to launch with, the session id Relay knows this claude by — "" when it cannot).

    Two claudes in one directory used to be told apart by guessing: the live tail took the newest
    transcript in the cwd's slug, so each pane could end up following the other's (review B7). A
    claude Relay starts is simply *told* its id — `--session-id <uuid4>` — and the transcript is
    then `<slug>/<that id>.jsonl`, known before the first line is written. A resumed session
    already has its id (`-r <id>`); a fork gets a new one from claude that Relay does not know."""
    extra = list(extra)
    named = [word.split("=", 1)[0] for word in extra if word.startswith("-")]
    if not any(flag in named for flag in _CLAUDE_SESSION_FLAGS):
        session = str(uuid.uuid4())
        return ["--session-id", session, *extra], session
    if "--fork-session" in named or "-c" in named or "--continue" in named:
        return extra, ""
    for flag in ("-r", "--resume", "--session-id"):
        for index, word in enumerate(extra):
            if word == flag and index + 1 < len(extra) and not extra[index + 1].startswith("-"):
                return extra, extra[index + 1]
            if word.startswith(flag + "="):
                return extra, word.split("=", 1)[1]
    return extra, ""


# ----- Codex -----------------------------------------------------------------------------------


def codex_overrides(python: str | None = None) -> list[str]:
    """The `-c` overrides one launched codex gets: the `notify` entry pointing at
    `guest_codex.py notify` (an absolute interpreter and an absolute script, because codex execs it
    with no shell) and the notification condition that makes it fire in the focused terminal."""
    command = guest_codex.notify_command(python)
    return ["-c", f"{guest_codex.NOTIFY_KEY}={guest_codex._toml_array(command)}",
            "-c", f"{'.'.join(guest_codex.TUI_TABLE)}.{guest_codex.NOTIFICATION_CONDITION}"
                  f"=\"{guest_codex.NOTIFICATION_CONDITION_VALUE}\""]


def codex_argv(python: str | None = None, extra=(), model: str | None = None,
               effort: str | None = None, memory_off: bool = False,
               memory_words=()) -> list[str]:
    """`codex [resume|fork] -c … [-m M] [-c model_reasoning_effort="E"]
    --dangerously-bypass-approvals-and-sandbox --dangerously-bypass-hook-trust [rest…]`.

    A sessions row's `extra` starts with the subcommand (`resume <id>`, `fork <id>`), which has to
    stay in front of the flags; anything else is appended after them — so the model and the effort
    go with the other flags, before a resumed thread's id, exactly as the overrides do. A row that
    already names the model (`-m`/`--model`) or the effort keeps its own. `memory_off` adds
    `codex_memory_overrides()`, and `memory_words` is `codex_memory_word`'s instructions (#MEMS).
    """
    extra = list(extra)
    rest = extra[1:] if extra and extra[0] in CODEX_SUBCOMMANDS else extra
    named = _names(rest)
    flags = list(codex_overrides(python))
    if model and not named & {"-m", "--model"}:
        flags += ["-m", model]
    if effort and not any(word.startswith(CODEX_EFFORT_KEY + "=") for word in rest):
        flags += ["-c", f'{CODEX_EFFORT_KEY}="{effort}"']
    if memory_off:
        flags += codex_memory_overrides()
    if memory_words and not any(word.startswith("developer_instructions=") for word in rest):
        flags += list(memory_words)
    flags += [CODEX_BYPASS, CODEX_HOOK_TRUST]
    if extra and extra[0] in CODEX_SUBCOMMANDS:
        return ["codex", extra[0], *flags, *rest]
    return ["codex", *flags, *rest]


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
                 extra=(), home: str | None = None, python: str | None = None,
                 model: str | None = None, effort: str | None = None,
                 memory: str | None = None) -> dict:
    """Everything the pane needs to start `guest_id` in its shell, in one payload:

        {"guest", "argv", "env", "command", "settings", "legacy", "session_id"}

    `env` is the IDE bridge's two variables for claude when `port` names a live bridge (empty
    otherwise, and always empty for codex); `command` is the one shell line — the assignments
    prefixed, every word quoted for the shell — that the pane types; `settings` is the claude
    settings file's path (empty for codex); `legacy` lists the stopgap files that were cleaned;
    `session_id` is the guest session this launch will write (`claude_session`; a resumed codex
    thread's id; "" when the guest picks one Relay cannot know in advance).

    `model` and `effort` are what the pane's Options is set to for this guest; each is left out
    when it is empty or when `extra` already names it (26.9, owner 2026-09-19).

    `memory` is Options' "guests use memory from" (#MEMS; unset is `relay`): `relay` turns the
    guest's own memory off for this launch (`CLAUDE_MEMORY_OFF_SETTINGS` in the settings file,
    `codex_memory_overrides()`), and `relay` and `both` give it Relay's memory block — claude by
    `--append-system-prompt-file`, codex by a pointer in its developer instructions. `memory` in
    the payload is the mode used and `memory_file` the block's path ("" for `own`).
    """
    spec = guest.spec(guest_id)   # ValueError for anything the registry does not know
    mode = memory_mode(memory)
    legacy = clean_legacy(cwd, home)
    env: dict[str, str] = {}
    settings = ""
    session_id = ""
    block = tui_memory_instructions(spec.id, cwd, mode, python)
    memory_file = ""
    if spec.id == "claude":
        settings = write_claude_settings(runtime_dir, cwd, home, own_memory=own_memory(mode))
        if block:
            memory_file = _write_private(runtime_dir, CLAUDE_MEMORY_NAME, block + "\n")
        extra, session_id = claude_session(extra)
        argv = claude_argv(settings, extra, model, effort, memory_file or None)
        if port:
            env = guest.bridge_env("claude", port)
    else:
        pointer: list[str] = []
        if block:
            memory_file = _write_private(runtime_dir, CODEX_MEMORY_NAME, block + "\n")
            pointer = codex_memory_word(memory_file, home)
        argv = codex_argv(python, extra, model, effort, memory_off=not own_memory(mode),
                          memory_words=pointer)
        extra = list(extra)
        if len(extra) >= 2 and extra[0] == "resume":
            session_id = extra[-1]      # codex has no flag to choose a new thread's id
    words = [f"{key}={shlex.quote(value)}" for key, value in env.items()] + [shlex.quote(word) for word in argv]
    return {"guest": spec.id, "argv": argv, "env": env, "command": " ".join(words),
            "settings": settings, "legacy": legacy, "session_id": session_id,
            "memory": mode, "memory_file": memory_file}


def suggest_main(argv) -> int:
    """`guest_launch suggest-memory --source <guest> <fact…>`: what `suggest_command` runs. Prints
    the store's answer as JSON — `pending`, or `declined`/`duplicate` with what it matched."""
    parser = argparse.ArgumentParser(prog="guest_launch suggest-memory",
                                     description="Propose a fact about the user to Relay (#MEMS).")
    parser.add_argument("--source", default="guest", help="which guest proposes it")
    parser.add_argument("--name", default=None, help="a stable kebab-case name (optional)")
    parser.add_argument("fact", nargs="+", help="the fact, one short sentence")
    args = parser.parse_args(argv)
    from . import memory_suggestions
    try:
        result = memory_suggestions.suggest(" ".join(args.fact), name=args.name,
                                            source=args.source, origin="terminal guest")
    except (ValueError, OSError) as error:
        print(json.dumps({"ok": False, "error": str(error)}))
        return 1
    print(json.dumps({**result, "ok": True}))
    return 0


def main(argv=None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    if arguments[:1] == ["suggest-memory"]:
        return suggest_main(arguments[1:])
    parser = argparse.ArgumentParser(
        description="Prepare a guest's launch for a Relay pane and print it as JSON (GT7X, 26.9).")
    parser.add_argument("guest", help="claude or codex")
    parser.add_argument("--runtime-dir", required=True, help="the pane's runtime directory")
    parser.add_argument("--cwd", default="", help="the directory the guest starts in")
    parser.add_argument("--port", type=int, default=0, help="the IDE bridge's port, when it is up")
    parser.add_argument("--home", default=None, help="the home directory (tests, alternate homes)")
    parser.add_argument("--python", default=None, help="the interpreter codex's notify entry names")
    parser.add_argument("--model", default=None, help="the model the pane is set to")
    parser.add_argument("--effort", default=None, help="the reasoning effort the pane is set to")
    parser.add_argument("--memory", default=None,
                        help="guests use memory from: relay (the default), own or both")
    # Everything after `--` is the guest's own (`-r <id>`, `resume <id>`), split off before argparse
    # sees it: it would otherwise read `-r` as an option of its own.
    extra: list[str] = []
    if "--" in arguments:
        cut = arguments.index("--")
        arguments, extra = arguments[:cut], arguments[cut + 1:]
    args = parser.parse_args(arguments)
    try:
        result = command_line(args.guest, args.runtime_dir, args.cwd or None, args.port, extra,
                              home=args.home, python=args.python, model=args.model or None,
                              effort=args.effort or None, memory=args.memory or None)
    except (LaunchError, ValueError, OSError) as error:
        print(json.dumps({"ok": False, "error": str(error)}))
        return 1
    print(json.dumps({**result, "ok": True}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
