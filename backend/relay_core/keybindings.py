# SPDX-License-Identifier: AGPL-3.0-or-later
"""Keybinding catalog and the agent's set_keybinding tool.

The GUI owns the action registry and sends the catalog (ids, descriptions, current keys)
plus the path of its keybindings.json. The agent may rebind one action at a time; each
change is written atomically to that single file, which the GUI reloads.

The catalog stays on this side of the wire: it is what `prepare` validates against and what
`app_action_list` answers from, and since #GMCF it is **not** in the tool schema. Listing all
91 actions with their keys there cost 9,837 bytes (2,592 tokens) of every pane request, and it
changed whenever the user rebound a key — which throws away the provider's prompt cache and,
on the Local tier, re-prefills the whole prefix. The model finds an id with `app_action_list`
instead, and an id it guesses wrong comes back with the closest ones (`suggest`).
"""
from __future__ import annotations

import difflib
import json
import os
import re
import tempfile
import threading
from dataclasses import dataclass, field
from pathlib import Path

MAX_ACTIONS = 200
MAX_KEYS = 4
MAX_DESCRIPTION = 200
MAX_FILE = 256 * 1024
ACTION_ID = re.compile(r"^[a-z][a-z0-9]*(\.[a-zA-Z0-9]+)+$")

MODIFIER_ORDER = ("Ctrl", "Alt", "Shift", "Meta")
_MODIFIER_ALIASES = {"ctrl": "Ctrl", "control": "Ctrl", "alt": "Alt", "shift": "Shift", "meta": "Meta"}
NAMED_KEYS = ("Tab", "Backtab", "Return", "Enter", "Escape", "Space", "Backspace", "Delete", "Insert",
              "Home", "End", "PgUp", "PgDown", "Left", "Right", "Up", "Down")
_NAMED = {name.lower(): name for name in NAMED_KEYS}
_NAMED.update({"esc": "Escape", "del": "Delete", "ins": "Insert", "pageup": "PgUp", "pagedown": "PgDown"})
# Every printable ASCII symbol. '+' separates modifiers, so it is spelled by writing it last —
# "Ctrl++" is Ctrl and the plus key, which is what Qt writes and what Relay's own default table
# binds terminal.zoomIn to (#Z00M). Shifted symbols such as ( | % ~ are needed by the Konsole and
# VS Code presets.
PUNCTUATION = set("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~")


class KeybindingError(ValueError):
    pass


def normalize_key(text: str) -> str:
    """Validate a Qt portable key sequence with one chord and normalize modifier order."""
    if not isinstance(text, str) or not text.strip() or len(text) > 64:
        raise KeybindingError("A key must be a non-empty string such as 'Ctrl+Shift+P'.")
    raw = text.strip()
    parts = raw.split("+")
    # The plus key is written last, Qt's way: "Ctrl++" splits to ["Ctrl", "", ""], and the two
    # empty tails are the separator and the key. Put the key back. Relay's own default table binds
    # terminal.zoomIn to "Ctrl++" (#Z00M), and rejecting it failed the whole `configure` that
    # carries the shortcut catalogue — so from the moment that binding landed no pane, helper or
    # console could build an agent at all, with only "Invalid key 'Ctrl++'" in the log to say so.
    if len(parts) > 1 and parts[-1] == "" and parts[-2] == "":
        parts = parts[:-2] + ["+"]
    if any(part == "" for part in parts):
        raise KeybindingError(f"Invalid key '{raw}': empty part. Use a form like 'Ctrl+Shift+P'.")
    *mods, key = [part.strip() for part in parts]
    seen = set()
    for mod in mods:
        canonical = _MODIFIER_ALIASES.get(mod.lower())
        if canonical is None:
            raise KeybindingError(f"Invalid key '{raw}': unknown modifier '{mod}'. Use Ctrl, Alt, Shift or Meta.")
        if canonical in seen:
            raise KeybindingError(f"Invalid key '{raw}': modifier '{canonical}' repeated.")
        seen.add(canonical)
    if len(key) == 1 and key.isascii() and key.isalnum():
        key = key.upper()
    elif len(key) == 1 and key in PUNCTUATION:
        pass
    elif re.fullmatch(r"[Ff]([1-9]|[12][0-9]|3[0-5])", key):
        key = "F" + key[1:]
    elif key.lower() in _NAMED:
        key = _NAMED[key.lower()]
    else:
        raise KeybindingError(
            f"Invalid key '{raw}': '{key}' is not a supported key. Use a letter, digit, F1-F35, "
            f"an ASCII symbol other than +, or one of: {', '.join(NAMED_KEYS)}.")
    return "+".join([m for m in MODIFIER_ORDER if m in seen] + [key])


def _check_keys(keys, *, where: str) -> list[str]:
    if not isinstance(keys, list) or len(keys) > MAX_KEYS:
        raise KeybindingError(f"{where}: keys must be a list of at most {MAX_KEYS} strings.")
    normalized = []
    for key in keys:
        value = normalize_key(key)
        if value not in normalized:
            normalized.append(value)
    return normalized


@dataclass
class Action:
    id: str
    description: str
    keys: list[str] = field(default_factory=list)


class KeybindingCatalog:
    def __init__(self, path: str, actions: list):
        self.path = self._check_path(path)
        self.actions: dict[str, Action] = self._check_actions(actions)
        self._lock = threading.Lock()

    @classmethod
    def from_request(cls, value) -> "KeybindingCatalog | None":
        if value is None:
            return None
        if not isinstance(value, dict):
            raise KeybindingError("keybindings must be an object with path and actions.")
        return cls(value.get("path"), value.get("actions"))

    @staticmethod
    def _check_path(path) -> Path:
        if not isinstance(path, str) or not path or "\x00" in path:
            raise KeybindingError("Keybindings path must be a string.")
        candidate = Path(path)
        if not candidate.is_absolute():
            raise KeybindingError("Keybindings path must be absolute.")
        if candidate.name != "keybindings.json":
            raise KeybindingError("Keybindings file must be named keybindings.json.")
        parent = candidate.parent
        if not parent.is_dir():
            try:
                parent.mkdir(mode=0o700, parents=True, exist_ok=True)
            except OSError as exc:
                raise KeybindingError(f"Cannot create keybindings directory: {exc.strerror}.") from None
        if candidate.exists() and (candidate.is_symlink() or not candidate.is_file()):
            raise KeybindingError("Keybindings path must be a regular file.")
        return candidate

    @staticmethod
    def _check_actions(actions) -> dict[str, Action]:
        if not isinstance(actions, list) or len(actions) > MAX_ACTIONS:
            raise KeybindingError(f"Keybinding actions must be a list of at most {MAX_ACTIONS} entries.")
        result: dict[str, Action] = {}
        for entry in actions:
            if not isinstance(entry, dict):
                raise KeybindingError("Each keybinding action must be an object.")
            action_id, description = entry.get("id"), entry.get("description", "")
            if not isinstance(action_id, str) or not ACTION_ID.match(action_id):
                raise KeybindingError(f"Invalid action id: {action_id!r}.")
            if action_id in result:
                raise KeybindingError(f"Duplicate action id: {action_id}.")
            if not isinstance(description, str) or len(description) > MAX_DESCRIPTION:
                raise KeybindingError(f"Description for {action_id} must be text of at most {MAX_DESCRIPTION} characters.")
            result[action_id] = Action(action_id, description, _check_keys(entry.get("keys", []), where=action_id))
        return result

    # ----- tool -------------------------------------------------------------
    #: Four lines, the same four for every user and every binding: nothing here changes when the
    #: catalog does, so the tool list is byte-identical across a session and caches (#GMCF).
    TOOL_DESCRIPTION = (
        "Change the keyboard shortcut for one Relay action, in Relay itself.\n"
        "Keys are Qt portable text: 'Ctrl+Shift+P', 'Alt+Left', 'F5'; at most "
        f"{MAX_KEYS}, and an empty list unbinds the action.\n"
        "Find the action id with app_action_list, which gives every action with its current "
        "keys; an id that is not one of Relay's is refused with the closest ones.\n"
        "It writes only Relay's keybindings.json, which Relay reloads automatically.")

    def tool_spec(self) -> dict:
        return {"type": "function", "function": {"name": "set_keybinding",
                "description": self.TOOL_DESCRIPTION,
                "parameters": {"type": "object", "properties": {
                    "action": {"type": "string",
                               "description": "The action id, such as pane.splitRight."},
                    "keys": {"type": "array", "items": {"type": "string"}, "maxItems": MAX_KEYS,
                             "description": "The keys to bind it to, or [] to unbind it."}},
                    "required": ["action", "keys"], "additionalProperties": False}}}

    def suggest(self, action, limit: int = 5) -> list[str]:
        """The ids closest to one the model guessed, best first (#GMCF).

        With the 91-entry enum gone from the schema this is how a wrong guess recovers in one
        step, so it is deliberately generous: the whole id, the part after the dot, a substring
        either way round, and finally the words of the action's description.
        """
        text = action.strip().lower() if isinstance(action, str) else ""
        if not text:
            return []
        tail = text.rsplit(".", 1)[-1]
        scored = []
        for candidate in self.actions:
            lowered = candidate.lower()
            score = max(difflib.SequenceMatcher(None, text, lowered).ratio(),
                        difflib.SequenceMatcher(None, tail, lowered.split(".", 1)[-1]).ratio())
            if text in lowered or lowered in text:
                score = max(score, 0.9)
            scored.append((score, candidate))
        scored.sort(key=lambda row: (-row[0], row[1]))
        near = [candidate for score, candidate in scored[:limit] if score >= 0.45]
        if near:
            return near
        # "close the pane" is not close to any id as text, but it is what pane.close is called.
        words = [word for word in re.split(r"[^a-z0-9]+", text) if len(word) > 2]
        return [a.id for a in self.actions.values()
                if words and all(word in a.description.lower() for word in words)][:limit]

    def unknown_action(self, action) -> KeybindingError:
        near = self.suggest(action)
        return KeybindingError(
            f"Relay has no action {action!r}."
            + (f" Did you mean {', '.join(near)}?" if near else "")
            + " app_action_list gives every action id with its current keys.")

    def prepare(self, args: dict) -> tuple[dict, str]:
        if set(args) - {"action", "keys"} or "action" not in args or "keys" not in args:
            raise KeybindingError("set_keybinding takes exactly 'action' and 'keys'.")
        action = args["action"]
        # The schema no longer carries the enum, so this is the only check there is (#GMCF).
        if not isinstance(action, str) or action not in self.actions:
            raise self.unknown_action(action)
        keys = _check_keys(args["keys"], where=action)
        preview = f"SET KEYBINDING\n\n{action}: {', '.join(keys) if keys else 'unbound'}"
        return {"action": action, "keys": keys}, preview

    def apply(self, args: dict) -> dict:
        action, keys = args["action"], list(args["keys"])
        with self._lock:
            data = self._read()
            bindings = data.get("bindings")
            if not isinstance(bindings, dict):
                bindings = {}
            bindings[action] = keys
            data["bindings"] = bindings
            data.setdefault("version", 1)
            self._write(data)
            self.actions[action].keys = keys
            conflicts = sorted(other.id for other in self.actions.values()
                               if other.id != action and set(other.keys) & set(keys))
        return {"action": action, "keys": keys, "path": str(self.path), "conflicts": conflicts,
                "note": "Relay reloads this file automatically."}

    def _read(self) -> dict:
        try:
            if self.path.is_symlink():
                raise KeybindingError("Keybindings path must be a regular file.")
            raw = self.path.read_bytes()
        except FileNotFoundError:
            return {}
        if len(raw) > MAX_FILE:
            raise KeybindingError("keybindings.json is too large.")
        try:
            data = json.loads(raw.decode("utf-8")) if raw.strip() else {}
        except (UnicodeDecodeError, json.JSONDecodeError):
            raise KeybindingError("keybindings.json is not valid JSON; fix or remove it first.") from None
        if not isinstance(data, dict):
            raise KeybindingError("keybindings.json must contain a JSON object.")
        return data

    def _write(self, data: dict) -> None:
        text = json.dumps(data, indent=2, ensure_ascii=False) + "\n"
        fd, temporary = tempfile.mkstemp(prefix=".keybindings-", suffix=".tmp", dir=self.path.parent)
        try:
            os.fchmod(fd, 0o600)
            with os.fdopen(fd, "w", encoding="utf-8") as handle:
                handle.write(text)
                handle.flush()
                os.fsync(handle.fileno())
            os.replace(temporary, self.path)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)
