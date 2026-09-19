# SPDX-License-Identifier: GPL-3.0-or-later
"""Codex as a guest: its marked settings entries and its rollout tail (issue GT7X).

Codex has no IDE bridge (protocol 26.2), so a pane learns about it from two structured
sources only, never from scraping the screen:

* **`notify`** — Codex runs the program named by the top-level `notify` key when a turn
  finishes and hands it a JSON payload *as one extra argv argument* (not on stdin, and with
  all three standard streams closed). Relay's entry points that payload at this module's own
  `notify` subcommand, which turns it into a `hook` event on the shared guest channel
  (protocol 26.3). The entry lives in the user's global `~/.codex/config.toml`, so it fires in
  every terminal — which is exactly why the channel's hard invariant matters: with no
  `RELAY_GUEST_EVENT` in the environment this is a no-op that writes nowhere.
* **The rollout transcript** — `~/.codex/sessions/YYYY/MM/DD/rollout-*.jsonl`, one JSON object
  per line, appended while a turn runs. Tailing the active pane's newest rollout is how the
  pane gets `state` (busy, turn) and statusline-equivalent data (model, token counts) without
  an app-server daemon (protocol 26.6; the daemon stays Tier A).

Both writes to `config.toml` are **additive and marked**: existing entries are preserved
verbatim, Relay's carry the `relay-guest` marker, and turning Guests off removes exactly the
marked entries. TOML is not JSON — it has comments, nested tables, multi-line arrays and
multi-line strings — so this module carries a small TOML *document* model that keeps every
untouched byte untouched. Values are never re-serialized by us: `tomllib` reads them back to
check the result, and the file is written with the same temp-file-and-rename the rest of Relay
uses. The payoff is a property the tests hold us to: enabling and then disabling Guests on a
file returns that file byte for byte.

Protocol: docs/AGENT-SESSIONS-PROTOCOL.md section 26. Card:
issues/features/2026-09-19-claude-codex-guest-integration.md (GT7X).
"""
from __future__ import annotations

import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field
from typing import Callable, Iterable, Iterator, Sequence

GUEST = "codex"                      # the id in `program_state.guest`
MARKER = "relay-guest"               # every entry Relay writes carries this word

NOTIFY_EVENT = "notify"              # this module's subcommand, and the hook's name
HOOK_EVENT = "hook"                  # the channel event a hook becomes (26.3)
STATE_EVENT = "state"                # the channel event the rollout tail emits
STATUSLINE_EVENT = "statusline"      # ... and its statusline-equivalent data

NOTIFY_KEY = "notify"                # a root key: Codex runs it at the end of a turn
TUI_TABLE = ("tui",)                 # `[tui] notification_condition = "always"`
NOTIFICATION_CONDITION = "notification_condition"
NOTIFICATION_CONDITION_VALUE = "always"   # the in-focus opt-in Codex defaults away from

HELPER_TIMEOUT = 5.0                 # seconds for the channel helper; a pane must never wait
MAX_CONFIG_BYTES = 1024 * 1024       # a config.toml larger than this is not one we edit

# The channel helper's variable, written into the pane's environment by the GUI (26.3).
GUEST_EVENT_VAR = "RELAY_GUEST_EVENT"
# The channel's one writer, beside the backend directory this file lives in: an installed Relay
# and a checkout both have `shell/` and `backend/` as siblings. The pane exports the spool
# *directory* as `RELAY_GUEST_EVENT` (26.3), so the writer's path is resolved here and the variable
# is only what says there is a pane to write to at all.
# Three shims carry this constant (guest_hook, guest_codex, guest_slash) and they must not
# be folded into one: two of them are run by absolute path with no PYTHONPATH, so they may
# not import from `relay_core` at all. `tests/test_guest.py` (OneChannelInThreeLanguages) is what
# keeps the copies in step.
WRITER = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                      "shell", "guest-event.py")


def helper_path(environment) -> str:
    """`shell/guest-event.py`, or "" when this process is not inside a Relay pane.

    `RELAY_GUEST_WRITER` overrides the path, which is how a test points at another checkout.
    """
    if not (environment.get(GUEST_EVENT_VAR) or ""):
        return ""
    return environment.get("RELAY_GUEST_WRITER") or WRITER


class CodexError(Exception):
    """Base for this module's failures."""


class SettingsError(CodexError):
    """`~/.codex/config.toml` cannot be read, understood, or edited safely."""


class SettingsConflict(SettingsError):
    """An entry Relay would write is already taken by something the user wrote."""

    def __init__(self, keys: Sequence[str]):
        self.keys = tuple(keys)
        super().__init__("Your own codex config already sets " + ", ".join(self.keys)
                         + "; Relay leaves it alone.")


# ----- the TOML document model ---------------------------------------------------------------
# Line-oriented on purpose. A TOML *value* is kept as the raw text the user wrote and is never
# re-rendered, so comments, alignment, quoting style and nested tables survive untouched; only
# whole entries we own are inserted or deleted. `tomllib` is the judge of what a value means
# (see `_value_of`), so this model never has to agree with TOML's value grammar.


@dataclass
class Line:
    """One parsed item: a table header, a key/value entry (possibly several lines), or text."""
    kind: str                          # "header" | "entry" | "text"
    text: str                          # raw, exactly as it appears in the file
    table: tuple[str, ...] = ()        # the table the line sits in; for a header, the one it opens
    key: str = ""                      # for entries: the key as written (dotted keys allowed)

    @property
    def marked(self) -> bool:
        return MARKER in self.text


_HEADER = re.compile(r"^[ \t]*(?P<open>\[\[?)[ \t]*(?P<path>.*?)[ \t]*(?P<close>\]\]?)[ \t]*(?:#.*)?$")
_KEY = re.compile(r"""^[ \t]*(?P<key>
                       (?:[A-Za-z0-9_-]+|"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*')
                       (?:[ \t]*\.[ \t]*(?:[A-Za-z0-9_-]+|"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*'))*
                       )[ \t]*=(?!=)""", re.VERBOSE)


def split_key(path: str) -> tuple[str, ...]:
    """A dotted TOML key path split into its parts, with quotes removed.

    `projects."/home/u/x"` → `("projects", "/home/u/x")`. Only what a table header or a dotted
    key can hold: no escapes beyond a backslash pair, which is all a path needs.
    """
    parts, current, quote = [], [], None
    index = 0
    while index < len(path):
        char = path[index]
        if quote:
            if char == "\\" and quote == '"' and index + 1 < len(path):
                current.append(path[index + 1])
                index += 2
                continue
            if char == quote:
                quote = None
            else:
                current.append(char)
        elif char in "\"'":
            quote = char
        elif char == ".":
            parts.append("".join(current).strip())
            current = []
        else:
            current.append(char)
        index += 1
    parts.append("".join(current).strip())
    return tuple(part for part in parts if part)


def _value_still_open(text: str) -> bool:
    """True when this text leaves an array, inline table or multi-line string open.

    A comment outside a string runs to the end of *its* line and nothing further, which is what
    TOML says: a `#` on the second line of a multi-line array must not end the scan there, or
    the array would never be seen to close and the rest of the file would be swallowed.
    """
    depth, quote, index = 0, None, 0
    while index < len(text):
        char = text[index]
        if quote in ('"""', "'''"):
            if text.startswith(quote, index):
                quote, index = None, index + 3
                continue
            index += 1
            continue
        if quote:
            if char == "\\" and quote == '"':
                index += 2
                continue
            if char == quote:
                quote = None
            index += 1
            continue
        if text.startswith('"""', index) or text.startswith("'''", index):
            quote, index = text[index:index + 3], index + 3
            continue
        if char in "\"'":
            quote, index = char, index + 1
            continue
        if char == "#":
            while index < len(text) and text[index] != "\n":
                index += 1
            continue
        if char in "[{":
            depth += 1
        elif char in "]}":
            depth -= 1
        index += 1
    return depth > 0 or quote in ('"""', "'''")


@dataclass
class Document:
    """A `config.toml` as its lines, plus the table each one belongs to."""
    lines: list[Line] = field(default_factory=list)

    @classmethod
    def parse(cls, text: str) -> Document:
        """Parse a config file into lines, each with the table it belongs to.

        A multi-line value — an array spread over lines, an inline table, a triple-quoted
        string — is one entry, because `_value_still_open` says when it is finished. Nothing is
        interpreted beyond that: a line is either a header, an entry, or text.
        """
        document = cls()
        table: tuple[str, ...] = ()
        pending: list[str] = []
        for line in text.splitlines(keepends=True):
            if pending:
                pending.append(line)
                if not _value_still_open("".join(pending)):
                    document.lines.append(Line("entry", "".join(pending), table,
                                               _entry_key(pending[0])))
                    pending = []
                continue
            header = _HEADER.match(line.rstrip("\n"))
            key = _KEY.match(line)
            if header and not key:
                table = split_key(header.group("path"))
                document.lines.append(Line("header", line, table))
            elif key:
                if _value_still_open(line):
                    pending = [line]
                else:
                    document.lines.append(Line("entry", line, table, key.group("key")))
            else:
                document.lines.append(Line("text", line, table))
        if pending:                      # a file that ends mid-value: keep it as written
            document.lines.append(Line("entry", "".join(pending), table, _entry_key(pending[0])))
        return document

    def text(self) -> str:
        return "".join(line.text for line in self.lines)

    # -- lookup ------------------------------------------------------------------------------
    def index_of_table(self, table: tuple[str, ...]) -> int | None:
        for position, line in enumerate(self.lines):
            if line.kind == "header" and line.table == table:
                return position
        return None

    def end_of_table(self, position: int) -> int:
        """The index just past the last line of the table whose header is at `position`."""
        for index in range(position + 1, len(self.lines)):
            if self.lines[index].kind == "header":
                return index
        return len(self.lines)

    def entries(self, table: tuple[str, ...], key: str) -> list[int]:
        """Indexes of the entries for `key` in `table`, marked or not."""
        wanted = _normalized_key(key)
        return [index for index, line in enumerate(self.lines)
                if line.kind == "entry" and line.table == table and _normalized_key(line.key) == wanted]

    def value_of(self, index: int):
        """The value of the entry at `index`, as TOML reads it (`tomllib` decides, not us)."""
        text = _strip_comment(self.lines[index].text)
        _, _, value = text.partition("=")
        try:
            return _toml_load(f"value = {value}")["value"]
        except Exception:
            return _UNREADABLE

    # -- editing -----------------------------------------------------------------------------
    def insert(self, position: int, lines: Iterable[str]) -> None:
        self.lines[position:position] = [Line("text", line) for line in lines]

    def remove(self, indexes: Iterable[int]) -> None:
        for index in sorted(set(indexes), reverse=True):
            del self.lines[index]


_UNREADABLE = object()      # a value tomllib would not accept: never equal to anything we write


def _entry_key(line: str) -> str:
    """The key of an entry line, as written."""
    match = _KEY.match(line)
    return match.group("key") if match else line.split("=")[0].strip()


def _normalized_key(key: str) -> str:
    return "".join(part for part in split_key(key)) if key else ""


def _strip_comment(text: str) -> str:
    """`text` without its comments, line by line; a `#` inside a string is not one.

    Comments are cut to the end of their own line rather than to the end of the text, so a value
    spread over several lines with a comment on one of them still reads as the value the user
    wrote — which is what lets `tomllib` be the judge of it.
    """
    kept, quote, index = [], None, 0
    while index < len(text):
        char = text[index]
        if quote in ('"""', "'''"):
            if text.startswith(quote, index):
                quote = None
                kept.append(text[index:index + 3])
                index += 3
                continue
            kept.append(char)
            index += 1
            continue
        if quote:
            if char == "\\" and quote == '"':
                kept.append(text[index:index + 2])
                index += 2
                continue
            if char == quote:
                quote = None
            kept.append(char)
            index += 1
            continue
        if text.startswith('"""', index) or text.startswith("'''", index):
            quote = text[index:index + 3]
            kept.append(text[index:index + 3])
            index += 3
            continue
        if char in "\"'":
            quote = char
            kept.append(char)
            index += 1
            continue
        if char == "#":
            while index < len(text) and text[index] != "\n":
                index += 1
            continue
        kept.append(char)
        index += 1
    return "".join(kept)


def _toml_load(text: str):
    import tomllib
    return tomllib.loads(text)


def _toml_string(value: str) -> str:
    """A Python string as a TOML basic string (JSON escaping is a subset of TOML's)."""
    return json.dumps(value)


def _toml_array(values: Sequence[str]) -> str:
    return "[" + ", ".join(_toml_string(value) for value in values) + "]"


# ----- the settings Relay owns ---------------------------------------------------------------


def settings_path(home: str | None = None) -> str:
    """The user's global Codex config: `~/.codex/config.toml` (it need not exist)."""
    root = home if home is not None else os.path.expanduser("~")
    return os.path.join(root, ".codex", "config.toml")


def notify_command(python: str | None = None, script: str | None = None) -> list[str]:
    """The argv Codex runs at the end of a turn.

    Codex execs this directly — no shell, so no `$VAR` and no quoting — and appends the
    payload as one more argument. That rules out `-m relay_core.guest_codex` (which would need
    an importable package) and any `$RELAY_PYTHON` spelling, so the entry names an absolute
    interpreter and this file's own absolute path; run as a script, this module needs nothing
    but the standard library and the environment. `-S` matches the pane's other helpers.
    """
    interpreter = python or shutil.which("python3") or sys.executable
    target = script or os.path.abspath(__file__)
    return [interpreter, "-S", target, NOTIFY_EVENT]


def _marked_comment(what: str) -> str:
    return f"# {MARKER}: {what} - added by Relay Terminal, removed when Guests is turned off.\n"


# A key whose value is the user's own command. Codex has exactly one `notify`, so Relay cannot
# add its own without taking the user's away: that is reported, never overwritten. The
# notification condition is not one of these - Relay needs `always` there, so a value the user
# set is replaced in place and kept in a restore comment instead (see `_restore_comment`).
CONFLICT_KEYS = (NOTIFY_KEY,)

_RESTORE = re.compile(r"^[ \t]*#[ \t]*" + re.escape(MARKER) + r": restore (?P<line>\S.*)$")


def _restore_comment(original: str) -> str:
    """The user's own line, kept verbatim in a marked comment so it can be put back.

    `notification_condition` cannot be added twice to one table, so enabling Guests means
    replacing the user's line; this comment is where their line waits until Guests is turned
    off, which is what keeps `render_disable(render_enable(text))` byte for byte.
    """
    return f"# {MARKER}: restore {original.removesuffix(chr(10))}\n"


def _restore_text(text: str) -> str | None:
    """The user's original line inside a restore comment, or None when this is not one."""
    match = _RESTORE.match(text.rstrip("\n"))
    if not match:
        return None
    return match.group("line") + "\n"


def _entries_to_write(python: str | None, script: str | None,
                     existing_tables: Iterable[tuple[str, ...]] = ()) -> dict[tuple[str, ...], list[str]]:
    """The two entries Relay writes, keyed by the table they belong to.

    Each entry is its own comment plus the key, and nothing else — no blank lines — so
    `render_disable` can take the file back to the byte. A table that is not in the file yet
    is created by a marked header, which is what lets disabling remove it again.
    """
    command = notify_command(python, script)
    known = set(existing_tables)
    lines: dict[tuple[str, ...], list[str]] = {
        (): [_marked_comment(NOTIFY_KEY), f"{NOTIFY_KEY} = {_toml_array(command)}  # {MARKER}\n"],
        TUI_TABLE: [_marked_comment(f"{'.'.join(TUI_TABLE)}.{NOTIFICATION_CONDITION}"),
                    f'{NOTIFICATION_CONDITION} = "{NOTIFICATION_CONDITION_VALUE}"  # {MARKER}\n'],
    }
    for table in list(lines):
        if table and table not in known:
            lines[table] = [f"[{'.'.join(table)}]  # {MARKER}\n"] + lines[table]
    return lines


def render_enable(text: str, python: str | None = None, script: str | None = None) -> str:
    """`config.toml` with Relay's marked entries added, everything else byte for byte.

    An entry of the user's that already says exactly what Relay needs is left alone rather than
    duplicated. A `notify` the user wrote is theirs and is never taken: that is a
    `SettingsConflict`, raised before anything is written. The notification condition is the
    opposite case — Relay needs `always`, so the user's value is replaced in place with their
    own line kept in a restore comment, ready to be put back verbatim on disable.
    """
    document = Document.parse(text)
    if text and not text.endswith("\n"):
        document.lines[-1].text += "\n"                # a config file ends with a newline
    tables = [line.table for line in document.lines if line.kind == "header"]
    conflicts: list[str] = []
    for table, lines in _entries_to_write(python, script, tables).items():
        key = NOTIFY_KEY if table == () else NOTIFICATION_CONDITION
        wanted = _value_from(lines[-1])
        ours = [index for index in document.entries(table, key) if document.lines[index].marked]
        theirs = [index for index in document.entries(table, key)
                  if not document.lines[index].marked]
        differing = [index for index in theirs if document.value_of(index) != wanted]
        if differing and key in CONFLICT_KEYS:
            conflicts.append(".".join(table + (key,)))
            continue
        if ours:                                        # ours, already there: refresh it in place
            removed = _with_comments(document, ours)
            # A restore comment holds the user's own line, so our entry stands where their line
            # did, not at the end of the table; otherwise the end of the table it is.
            restore_above = (removed[0] > 0
                             and _restore_text(document.lines[removed[0] - 1].text) is not None)
            document.remove(removed)
            position = removed[0] if restore_above else _insertion_point(document, table)
            document.insert(position, lines)
        elif differing:                                 # the user's value, replaced restorably
            position = differing[0]
            original = document.lines[position].text
            # Only an entry Relay wholly understands is replaced: one line, and a value that
            # reads back. Anything else — a value spread over lines, a value that continues on
            # the next line, a value tomllib cannot take — is the user's, reported not guessed.
            if (len(original.splitlines()) != 1
                    or document.value_of(position) is _UNREADABLE):
                conflicts.append(".".join(table + (key,)))
                continue
            document.remove([position])
            document.insert(position, [_restore_comment(original)] + lines)
        elif theirs:                                    # the user's, and identical: leave it alone
            continue
        else:
            document.insert(_insertion_point(document, table), lines)
    if conflicts:
        raise SettingsConflict(conflicts)
    rendered = document.text()
    _verify(rendered, python, script)
    return rendered


def render_disable(text: str) -> str:
    """`config.toml` with exactly Relay's marked entries removed, nothing else touched.

    A `[tui]` table Relay created goes with its entry; a `[tui]` table the user already had keeps
    every other line it holds; a line of the user's that Relay replaced comes back from its
    restore comment. Enabling and then disabling returns the original text byte for byte, which
    is the property `tests/test_guest_codex.py` holds this to.
    """
    document = Document.parse(text)
    kept: list[Line] = []
    for line in document.lines:
        if not line.marked:
            kept.append(line)
            continue
        original = _restore_text(line.text) if line.kind == "text" else None
        if original is not None:
            kept.append(Line("text", original))        # the user's own line, back where it was
    document.lines = kept
    rendered = document.text()
    if not rendered.strip():
        return ""
    if text and not rendered.endswith("\n"):
        rendered += "\n"
    try:
        _toml_load(rendered)      # never leave a file behind that Codex cannot read
    except Exception as error:
        raise SettingsError(f"the result would not be valid TOML ({error}).") from None
    return rendered


def _with_comments(document: Document, indexes: Iterable[int]) -> list[int]:
    """The entry lines plus the marked comment lines that introduce them.

    An entry Relay wrote is its comment and its key, and the two go together: refreshing an
    entry without taking its comment would leave the file collecting a comment per enable. A
    restore comment is not one of those comments — it holds the user's own line and stays until
    Guests is turned off — so the walk stops there.
    """
    all_indexes = set(indexes)
    for index in indexes:
        previous = index - 1
        while (previous >= 0 and document.lines[previous].marked
               and document.lines[previous].kind == "text"
               and _restore_text(document.lines[previous].text) is None):
            all_indexes.add(previous)
            previous -= 1
    return sorted(all_indexes)


def _value_from(entry_line: str):
    text = _strip_comment(entry_line)
    _, _, value = text.partition("=")
    return _toml_load(f"value = {value}")["value"]


def _insertion_point(document: Document, table: tuple[str, ...]) -> int:
    """Where this table's entry goes: at the end of its table, or — for a root key — before the
    first header, because every TOML root key must come before the first table."""
    position = document.index_of_table(table)
    if position is not None:
        return document.end_of_table(position)
    if table:
        if document.lines and not document.lines[-1].text.endswith("\n"):
            document.lines[-1].text += "\n"
        return len(document.lines)          # a table Relay is creating: at the end of the file
    for index, line in enumerate(document.lines):
        if line.kind == "header":
            return index
    return len(document.lines)


def _verify(rendered: str, python: str | None, script: str | None) -> None:
    """Read the result back: it must parse, and mean what we wrote."""
    try:
        data = _toml_load(rendered)
    except Exception as error:
        raise SettingsError(f"the result would not be valid TOML ({error}).") from None
    expected_notify = notify_command(python, script)
    if data.get(NOTIFY_KEY) != expected_notify:
        raise SettingsError("the notify entry did not read back as written.")
    condition = (data.get(TUI_TABLE[0]) or {}).get(NOTIFICATION_CONDITION)
    if condition != NOTIFICATION_CONDITION_VALUE:
        raise SettingsError("the notification condition did not read back as written.")


def _is_our_notify(value, script: str | None = None) -> bool:
    """True when a `notify` value is Relay's entry: our script, invoked with our subcommand.

    Deliberately not an equality check against `notify_command()`: the interpreter named at
    enable time is whatever the GUI found then, and a path that moved between releases must not
    read back as "off" when the entry is in fact Relay's.
    """
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        return False
    if not value or value[-1] != NOTIFY_EVENT:
        return False
    names = {os.path.basename(item) for item in value}
    return os.path.basename(script or __file__) in names


def settings_state(home: str | None = None, path: str | None = None,
                   script: str | None = None) -> dict:
    """What Relay's entries look like in the user's config right now.

    `enabled` means Codex will do what Relay needs — either because our marked entries are
    there, or because the user's own entries already say the same thing. `notes` is prose for a
    person: it never guesses at state the file does not carry.
    """
    target = path or settings_path(home)
    state = {"path": target, "exists": os.path.isfile(target),
             "marked": {NOTIFY_KEY: False, NOTIFICATION_CONDITION: False}, NOTIFY_KEY: None,
             NOTIFICATION_CONDITION: None, "enabled": False, "notes": []}
    if not state["exists"]:
        state["notes"].append("Codex has no config.toml yet.")
        return state
    try:
        text = _read(target)
        document = Document.parse(text)
        data = _toml_load(text)
    except SettingsError as error:
        state["notes"].append(str(error))
        return state
    except Exception as error:
        state["notes"].append(f"config.toml is not valid TOML ({error}).")
        return state
    for table, key in (((), NOTIFY_KEY), (TUI_TABLE, NOTIFICATION_CONDITION)):
        indexes = document.entries(table, key)
        state["marked"][key] = any(document.lines[index].marked for index in indexes)
    state[NOTIFY_KEY] = data.get(NOTIFY_KEY)
    state[NOTIFICATION_CONDITION] = (data.get(TUI_TABLE[0]) or {}).get(NOTIFICATION_CONDITION)
    ours = _is_our_notify(state[NOTIFY_KEY], script)
    state["enabled"] = ours and state[NOTIFICATION_CONDITION] == NOTIFICATION_CONDITION_VALUE
    if state[NOTIFY_KEY] is not None and not ours:
        state["notes"].append("Your own notify command is set; Relay will not replace it.")
    if ours:
        programs = [item for item in state[NOTIFY_KEY] if os.path.isabs(item)]
        if programs and not os.path.isfile(programs[-1]):
            state["notes"].append(f"Relay's notify program is missing ({programs[-1]}); "
                                  "turn the Guests option off and on again.")
    if (data.get(TUI_TABLE[0]) or {}).get("notifications") is False:
        state["notes"].append("Codex's own desktop notifications are off; Relay's notification "
                              "centre reads the hook channel either way.")
    return state


def enable(home: str | None = None, path: str | None = None, python: str | None = None,
           script: str | None = None) -> dict:
    """Add Relay's marked entries to the user's Codex config. Returns what changed."""
    target = path or settings_path(home)
    text = _read(target) if os.path.isfile(target) else ""
    rendered = render_enable(text, python, script)
    changed = rendered != text
    if changed:
        _write(target, rendered, _mode_of(target))
    return {**settings_state(path=target, script=script), "changed": changed}


def disable(home: str | None = None, path: str | None = None) -> dict:
    """Remove exactly Relay's marked entries. Returns what changed.

    A config.toml that Relay itself created is removed rather than left empty, so the user's
    home looks exactly as it did before Guests was turned on.
    """
    target = path or settings_path(home)
    if not os.path.isfile(target):
        return {**settings_state(path=target), "changed": False, "removed": False}
    text = _read(target)
    rendered = render_disable(text)
    changed = rendered != text
    removed = False
    if changed and not rendered:
        try:
            os.unlink(target)               # only when nothing but Relay's entries was in it
            removed = True
        except OSError as error:
            raise SettingsError(f"cannot remove {target} ({error.strerror or error}).") from None
    elif changed:
        _write(target, rendered, _mode_of(target))
    return {**settings_state(path=target), "changed": changed, "removed": removed}


def _read(path: str) -> str:
    try:
        if os.path.getsize(path) > MAX_CONFIG_BYTES:
            raise SettingsError("config.toml is too large for Relay to edit.")
        with open(path, encoding="utf-8") as handle:
            return handle.read()
    except OSError as error:
        raise SettingsError(f"cannot read {path} ({error.strerror or error}).") from None
    except UnicodeDecodeError:
        raise SettingsError("config.toml is not UTF-8 text.") from None


def _mode_of(path: str) -> int:
    try:
        return os.stat(path).st_mode & 0o777
    except OSError:
        return 0o600                # what Codex itself keeps config.toml at


def _write(path: str, text: str, mode: int) -> None:
    """Temp file and rename, the way the rest of Relay writes user files.

    No `.bak` sibling: the entries Relay adds are removable, which is the recovery path — an
    extra file in the user's home that nothing ever cleans up would not be.
    """
    parent = os.path.dirname(path) or "."
    try:
        os.makedirs(parent, mode=0o700, exist_ok=True)
        handle, temporary = tempfile.mkstemp(prefix=".config-", suffix=".toml", dir=parent)
    except OSError as error:
        raise SettingsError(f"cannot write {path} ({error.strerror or error}).") from None
    try:
        os.fchmod(handle, mode)
        with os.fdopen(handle, "w", encoding="utf-8") as output:
            output.write(text)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    except OSError as error:
        raise SettingsError(f"cannot write {path} ({error.strerror or error}).") from None
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


# ----- the guest event channel (26.3) ----------------------------------------------------------


def channel_argv(helper: str, event: str, guest: str = GUEST) -> list[str]:
    """How the channel helper is invoked: `guest-event.py <event> <guest>` (26.3)."""
    return [helper, event, guest]


def emit(event: str, data: dict, env: dict | None = None,
         runner: Callable[..., object] | None = None, guest: str = GUEST) -> bool:
    """Hand one event to the channel helper. False when there is no helper to hand it to.

    The event's JSON goes on the helper's stdin, which is where `shell/guest-event.py` reads it;
    the helper adds the pane token and a fresh `sequence` itself and drops the file on the pane's
    spool. A shim with no `RELAY_GUEST_EVENT` is a no-op (26.3) — it exits 0, prints nothing and
    writes nowhere — so this returns False rather than raising: a terminal that is not Relay must
    be able to run the same code paths without a sound or a message.
    """
    environment = os.environ if env is None else env
    helper = helper_path(environment)
    if not helper:
        return False
    interpreter = environment.get("RELAY_PYTHON") or shutil.which("python3") or sys.executable
    payload = json.dumps(data, ensure_ascii=False)
    call = runner or subprocess.run
    try:
        call([interpreter, "-S"] + channel_argv(helper, event, guest), input=payload, text=True,
             timeout=HELPER_TIMEOUT, capture_output=True)
    except (OSError, subprocess.SubprocessError):
        return False               # a helper that cannot run is not a reason to fail a turn
    return True


def emit_events(events: Iterable[tuple[str, dict]], env: dict | None = None,
                runner: Callable[..., object] | None = None) -> int:
    """Emit each `(event, data)` pair; returns how many reached the channel."""
    return sum(1 for event, data in events if emit(event, data, env=env, runner=runner))


def hook_data(name: str, payload) -> dict:
    """The `data` of a `hook` event: `{name, payload}` (26.3)."""
    return {"name": name, "payload": payload}


# ----- the rollout tail (26.6) -----------------------------------------------------------------


@dataclass(frozen=True)
class Rollout:
    """One rollout transcript, as the sessions directory describes it."""
    path: str
    thread_id: str | None      # from the file name, which Codex builds from the session id
    cwd: str | None            # the workspace Codex was started in, read from `session_meta`
    mtime: float


def rollout_thread_id(path: str) -> str | None:
    """The session id Codex puts in a rollout's file name: `rollout-<stamp>-<id>.jsonl`."""
    stem = os.path.basename(path)
    if not stem.startswith("rollout-") or not stem.endswith(".jsonl"):
        return None
    match = re.search(r"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})$",
                      stem[len("rollout-"):-len(".jsonl")])
    return match.group(1) if match else None


def newest_rollout(sessions_dir: str | None = None, cwd: str | None = None,
                   home: str | None = None, limit: int = 25) -> Rollout | None:
    """The active pane's newest rollout, or the newest one when no `cwd` is given.

    File names carry their timestamp, so the newest are found by sorting names rather than by
    stat-ing a whole history; only the newest `limit` are opened, and only their first line, to
    check the workspace Codex was started in. `cwd` matching is what keeps a pane tailing the
    session that belongs to it when several are running side by side.
    """
    root = sessions_dir if sessions_dir is not None else _sessions_dir(home)
    if not root or not os.path.isdir(root):
        return None
    paths = glob.glob(os.path.join(root, "*", "*", "*", "rollout-*.jsonl"))
    paths += glob.glob(os.path.join(root, "rollout-*.jsonl"))          # a flat layout
    candidates = sorted(paths, key=os.path.basename, reverse=True)[:max(1, limit)]
    found = []
    for path in candidates:
        try:
            mtime = os.stat(path).st_mtime
        except OSError:
            continue
        found.append(Rollout(path, rollout_thread_id(path), _rollout_cwd(path), mtime))
    if not found:
        return None
    if cwd:
        for rollout in sorted(found, key=lambda item: item.mtime, reverse=True):
            if rollout.cwd and os.path.normpath(rollout.cwd) == os.path.normpath(cwd):
                return rollout
    return max(found, key=lambda item: item.mtime)


def _sessions_dir(home: str | None) -> str | None:
    """`guest.codex_sessions_dir()`, imported lazily so this module also runs as a bare script."""
    try:
        return _registry().codex_sessions_dir(home)
    except Exception:
        return None


def _registry():
    global _GUEST_REGISTRY
    if _GUEST_REGISTRY is None:
        if not __package__:      # run as a script: `backend/` is the package's parent
            backend = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
            if backend not in sys.path:
                sys.path.insert(0, backend)
        from relay_core import guest as module
        _GUEST_REGISTRY = module
    return _GUEST_REGISTRY


_GUEST_REGISTRY = None


def _rollout_cwd(path: str) -> str | None:
    """The `cwd` of a rollout's `session_meta`, the first record Codex writes."""
    try:
        with open(path, encoding="utf-8") as handle:
            for _ in range(20):                     # session_meta is first, but never assume
                line = handle.readline()
                if not line:
                    break
                record = _json(line)
                if record and record.get("type") == "session_meta":
                    payload = record.get("payload") or {}
                    return payload.get("cwd") or None
    except (OSError, UnicodeDecodeError):
        return None
    return None


def _json(line: str):
    line = line.strip()
    if not line:
        return None
    try:
        record = json.loads(line)
    except ValueError:
        return None
    return record if isinstance(record, dict) else None


TOKEN_KEYS = ("input_tokens", "cached_input_tokens", "cache_write_input_tokens", "output_tokens",
              "reasoning_output_tokens", "total_tokens")


def _tokens(value) -> dict | None:
    """A usage block, keeping only the counts the rollout actually reported."""
    if not isinstance(value, dict):
        return None
    counts = {key: value[key] for key in TOKEN_KEYS
              if isinstance(value.get(key), int) and not isinstance(value.get(key), bool)}
    return counts or None


@dataclass
class TailState:
    """Everything the tail knows, each field only as the rollout reported it."""
    rollout: str | None = None
    thread_id: str | None = None
    cwd: str | None = None
    model: str | None = None
    turn: str | None = None
    busy: bool = False
    started_at: int | None = None
    context_window: int | None = None
    last_request: dict | None = None       # the last API call's own usage: the live context
    turn_usage: dict | None = None
    thread_usage: dict | None = None       # the whole session's, as Codex accumulates it
    last_record: str | None = None
    updated: float = 0.0                   # mtime of the rollout when it was last read

    def context_pct(self) -> int | None:
        """Context occupancy as a percentage, or None when it cannot be derived.

        The last request's input tokens *are* the context Codex sent, cached part included, so
        that is the numerator; the denominator is the window Codex reported for the turn. The
        session's cumulative input is not a context size — it grows past the window by design.
        """
        if not self.context_window or not self.last_request:
            return None
        used = self.last_request.get("input_tokens")
        if not isinstance(used, int) or used < 0:
            return None
        return max(0, min(100, round(used / self.context_window * 100)))

    def idle_seconds(self, now: float | None = None) -> int:
        moment = time.time() if now is None else now
        return max(0, int(moment - self.updated)) if self.updated else 0


def state_data(state: TailState) -> dict:
    """The `state` event's data: `{busy, turn?}` — nothing else (26.3)."""
    data = {"busy": state.busy}
    if state.turn:
        data["turn"] = state.turn
    return data


def statusline_data(state: TailState, now: float | None = None, stale_after: float | None = None) -> dict:
    """The statusline-equivalent data the pane's chips read, and only what is known.

    A missing key means Codex did not report that number — never zero. `stale` and
    `idle_seconds` are the tail's own two facts: a turn left open in a rollout that has stopped
    growing is how a Codex that was killed looks, and only the caller can say how long is too
    long, so nothing here decides it.
    """
    idle = state.idle_seconds(now)
    data = {"model": state.model, "context_pct": state.context_pct(),
            "context_window": state.context_window, "thread_id": state.thread_id, "cwd": state.cwd,
            "rollout": state.rollout, "turn_tokens": state.turn_usage,
            "thread_tokens": state.thread_usage, "last_request_tokens": state.last_request,
            "idle_seconds": idle, "stale": bool(stale_after is not None and idle > stale_after)}
    return {key: value for key, value in data.items() if value is not None}


VOLATILE_KEYS = ("idle_seconds",)     # changes every second; never a reason to write an event


class RolloutTail:
    """Follows the active pane's newest rollout and reports what changed.

    Reading is incremental — one seek, one read of what was appended — so a poll costs the same
    whether the transcript is a kilobyte or a hundred megabytes. A line that is only half
    written is held until its newline arrives, which is the normal case while Codex is working.
    """

    def __init__(self, sessions_dir: str | None = None, cwd: str | None = None,
                 home: str | None = None, stale_after: float | None = None,
                 rescan_seconds: float = 5.0, limit: int = 25):
        self.sessions_dir = sessions_dir
        self.cwd = cwd
        self.home = home
        self.stale_after = stale_after
        self.rescan_seconds = rescan_seconds
        self.limit = limit
        self.state = TailState()
        self._path: str | None = None
        self._offset = 0
        self._buffer = ""
        self._scanned = 0.0
        self._sent_state: dict | None = None
        self._sent_statusline: dict | None = None

    # -- polling -----------------------------------------------------------------------------
    def poll(self, now: float | None = None) -> list[tuple[str, dict]]:
        """Read whatever is new and return the events it justifies, in the order to send them."""
        moment = time.time() if now is None else now
        if self._path is None or moment - self._scanned >= self.rescan_seconds:
            self._scanned = moment
            self._resolve(moment)
        self._read_new()
        return self._changes(moment)

    def _resolve(self, moment: float) -> None:
        """Follow the newest rollout; a new one is a new session, so nothing carries over."""
        found = newest_rollout(self.sessions_dir, self.cwd, self.home, self.limit)
        path = found.path if found else None
        if path == self._path:
            if found:
                self.state.updated = found.mtime
            return
        self._path, self._offset, self._buffer = path, 0, ""
        self.state = TailState(rollout=path, thread_id=found.thread_id if found else None,
                               cwd=found.cwd if found else None,
                               updated=found.mtime if found else 0.0)
        self._sent_state = self._sent_statusline = None

    def _read_new(self) -> None:
        if not self._path:
            return
        try:
            size = os.path.getsize(self._path)
            mtime = os.stat(self._path).st_mtime
        except OSError:
            return
        if size < self._offset:                       # rewritten or replaced: read it afresh
            self._offset, self._buffer = 0, ""
        if size > self._offset:
            try:
                with open(self._path, encoding="utf-8", errors="replace") as handle:
                    handle.seek(self._offset)
                    chunk = handle.read(size - self._offset)
                    self._offset = size
            except OSError:
                return
            self._buffer += chunk
            lines = self._buffer.split("\n")
            self._buffer = lines.pop()                # a half-written line waits for its newline
            for line in lines:
                record = _json(line)
                if record:
                    self._apply(record)
            self.state.updated = mtime

    def _apply(self, record: dict) -> None:
        kind = record.get("type")
        payload = record.get("payload") if isinstance(record.get("payload"), dict) else {}
        self.state.last_record = kind
        if kind == "session_meta":
            self.state.thread_id = payload.get("session_id") or payload.get("id") or self.state.thread_id
            self.state.cwd = payload.get("cwd") or self.state.cwd
        elif kind == "turn_context":
            self.state.model = payload.get("model") or self.state.model
            self.state.turn = payload.get("turn_id") or self.state.turn
        elif kind == "event_msg":
            self._apply_event(payload)
        elif kind == "token_usage_record":
            self.state.thread_usage = _tokens(payload.get("thread_token_usage")) or self.state.thread_usage
            self.state.turn_usage = _tokens(payload.get("turn_token_usage")) or self.state.turn_usage
            self.state.last_request = _tokens(payload.get("usage")) or self.state.last_request

    def _apply_event(self, payload: dict) -> None:
        name = payload.get("type")
        if name == "task_started":
            self.state.busy = True
            self.state.turn = payload.get("turn_id") or self.state.turn
            self.state.started_at = payload.get("started_at") or self.state.started_at
            self.state.context_window = payload.get("model_context_window") or self.state.context_window
        elif name in ("task_complete", "turn_aborted"):
            # A completion for a turn that is not the open one is a late report: the newer turn
            # is still running, so it does not end it.
            if not payload.get("turn_id") or payload.get("turn_id") == self.state.turn:
                self.state.busy = False
                self.state.turn = payload.get("turn_id") or self.state.turn
        elif name == "token_count":
            info = payload.get("info") if isinstance(payload.get("info"), dict) else {}
            self.state.context_window = info.get("model_context_window") or self.state.context_window
            self.state.thread_usage = _tokens(info.get("total_token_usage")) or self.state.thread_usage
            self.state.last_request = _tokens(info.get("last_token_usage")) or self.state.last_request
        elif name == "thread_settings_applied":
            settings = payload.get("thread_settings") if isinstance(payload.get("thread_settings"), dict) else {}
            self.state.model = settings.get("model") or self.state.model

    def _changes(self, now: float) -> list[tuple[str, dict]]:
        """Only what changed since the last poll: the pane keeps state, it does not need repeats.

        Nothing at all is reported before a rollout has been found: "idle" is a claim about a
        Codex session, and a pane whose guest has not written a transcript yet has no session to
        make that claim about. Silence leaves whatever the pane already believed alone.
        """
        if self._path is None:
            return []
        events = []
        state = state_data(self.state)
        if state != self._sent_state:
            self._sent_state = state
            events.append((STATE_EVENT, state))
        statusline = statusline_data(self.state, now, self.stale_after)
        if _signature(statusline) != _signature(self._sent_statusline):
            self._sent_statusline = statusline
            events.append((STATUSLINE_EVENT, statusline))
        return events


def _signature(data: dict | None) -> dict | None:
    if data is None:
        return None
    return {key: value for key, value in data.items() if key not in VOLATILE_KEYS}


def poll_and_emit(tail: RolloutTail, env: dict | None = None,
                  runner: Callable[..., object] | None = None) -> int:
    """Poll once and send what changed. Returns how many events reached the channel."""
    return emit_events(tail.poll(), env=env, runner=runner)


def watch(tail: RolloutTail, interval: float = 1.0, stop: Callable[[], bool] | None = None,
          env: dict | None = None, runner: Callable[..., object] | None = None,
          sleep: Callable[[float], None] = time.sleep) -> Iterator[tuple[str, dict]]:
    """Poll forever, yielding each event; `stop()` ends it (a pane that closed, a test).

    A generator rather than a thread: whoever runs it decides where the loop lives, and the
    tests drive it with their own clock and their own `stop`.
    """
    while not (stop and stop()):
        for event in tail.poll():
            if emit(event[0], event[1], env=env, runner=runner):
                yield event
        if stop and stop():
            return
        sleep(interval)


# ----- the notify hook and the CLI -------------------------------------------------------------


def notify_event(payload) -> tuple[str, dict] | None:
    """The channel event for a Codex notify payload, or None when it is not one.

    Codex appends the payload as the final argv argument. Only `agent-turn-complete` exists
    today; anything else, and anything that is not a JSON object, is not a hook and is dropped
    rather than invented into one.
    """
    if not isinstance(payload, dict) or payload.get("type") != "agent-turn-complete":
        return None
    return HOOK_EVENT, hook_data(NOTIFY_EVENT, payload)


def notify_main(argv: Sequence[str], env: dict | None = None,
                runner: Callable[..., object] | None = None) -> int:
    """The `notify` entry point. Always 0: Codex closes this program's streams and ignores its
    status, so the only thing it may never do is hang or fail a turn."""
    payload = None
    for argument in reversed(list(argv)):
        parsed = _json(argument)
        if isinstance(parsed, dict):
            payload = parsed
            break
    event = notify_event(payload)
    if event:
        emit(event[0], event[1], env=env, runner=runner)
    return 0


def tail_main(argv: Sequence[str], env: dict | None = None,
              runner: Callable[..., object] | None = None) -> int:
    """The `tail` entry point: follow this pane's rollout and emit `state`/`statusline`.

    Meant to be run with the pane's own environment (RELAY_GUEST_EVENT, RELAY_RUNTIME_DIR and
    RELAY_SESSION_TOKEN), exactly as `notify` is; with no helper in the environment every event
    is dropped and this exits 0 after the first poll, which is what makes it safe to run
    anywhere. `--once` is for a caller that polls from its own loop instead of spawning one.
    """
    options = {"interval": 1.0, "once": False, "cwd": None, "sessions_dir": None, "stale_after": None}
    index = 0
    while index < len(argv):
        argument = argv[index]
        if argument == "--once":
            options["once"] = True
        elif argument == "--cwd" and index + 1 < len(argv):
            index += 1
            options["cwd"] = argv[index]
        elif argument == "--sessions-dir" and index + 1 < len(argv):
            index += 1
            options["sessions_dir"] = argv[index]
        elif argument in ("--interval", "--stale-after") and index + 1 < len(argv):
            index += 1
            try:
                options[argument[2:].replace("-", "_")] = float(argv[index])
            except ValueError:
                return 0
        index += 1
    tail = RolloutTail(options["sessions_dir"], options["cwd"], stale_after=options["stale_after"])
    if options["once"]:
        poll_and_emit(tail, env=env, runner=runner)
        return 0
    try:
        for _ in watch(tail, options["interval"], env=env, runner=runner):
            pass
    except KeyboardInterrupt:
        return 0
    return 0


def settings_main(argv: Sequence[str]) -> int:
    """The settings entry point, which the Options › Guests row calls (GT7X):

        guest_codex.py --enable | --disable | --settings-state [--home DIR] [--python PATH] [--script PATH]

    One JSON object on stdout: the state `enable()` / `disable()` / `settings_state()` returned,
    plus `ok`. A conflict is its own exit code (3) with the taken keys in `conflict`, because the
    GUI must show that one verbatim and leave its toggle alone; any other failure is exit 1 with
    the error in `error`. Reads and writes go through the library functions above and nothing
    else, so the command line can never mean something the tests do not hold the library to.
    """
    options = {"home": None, "python": None, "script": None}
    action = None
    index = 0
    while index < len(argv):
        argument = argv[index]
        if argument in ("--enable", "--disable", "--settings-state"):
            action = argument[2:]
        elif argument in ("--home", "--python", "--script") and index + 1 < len(argv):
            index += 1
            options[argument[2:]] = argv[index]
        else:
            print(json.dumps({"ok": False, "error": f"unknown option {argument!r}."}))
            return 2
        index += 1
    if action is None:
        print(json.dumps({"ok": False, "error": "choose --enable, --disable or --settings-state."}))
        return 2
    try:
        if action == "enable":
            result = enable(home=options["home"], python=options["python"], script=options["script"])
        elif action == "disable":
            result = disable(home=options["home"])
        else:
            result = settings_state(home=options["home"], script=options["script"])
    except SettingsConflict as error:
        print(json.dumps({"ok": False, "conflict": list(error.keys), "error": str(error)}))
        return 3
    except CodexError as error:
        print(json.dumps({"ok": False, "error": str(error)}))
        return 1
    print(json.dumps({**result, "ok": True}))
    return 0


def main(argv: Sequence[str] | None = None, env: dict | None = None,
         runner: Callable[..., object] | None = None) -> int:
    """The script entry point: `guest_codex.py notify <payload>` / `... tail [options]`, and the
    settings flags the Options › Guests row uses (`--enable` / `--disable` / `--settings-state`)."""
    arguments = list(sys.argv[1:] if argv is None else argv)
    command, rest = (arguments[0], arguments[1:]) if arguments else ("", [])
    if command in ("--enable", "--disable", "--settings-state"):
        return settings_main(arguments)
    if command == NOTIFY_EVENT:
        return notify_main(rest, env=env, runner=runner)
    if command == "tail":
        return tail_main(rest, env=env, runner=runner)
    return 0


if __name__ == "__main__":
    sys.exit(main())
