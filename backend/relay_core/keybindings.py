# SPDX-License-Identifier: AGPL-3.0-or-later
"""Keybinding catalog and the agent's set_keybinding tool.

The GUI owns the action registry and sends the catalog (ids, descriptions, current keys)
plus the path of its keybindings.json. The agent may rebind one action at a time; each
change is written atomically to that single file, which the GUI reloads.
"""
from __future__ import annotations

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
# Every printable ASCII symbol except '+', which separates modifiers. Shifted symbols such as
# ( | % ~ are needed by the Konsole and VS Code presets.
PUNCTUATION = set("!\"#$%&'()*,-./:;<=>?@[\\]^_`{|}~")


class KeybindingError(ValueError):
    pass


def normalize_key(text: str) -> str:
    """Validate a Qt portable key sequence with one chord and normalize modifier order."""
    if not isinstance(text, str) or not text.strip() or len(text) > 64:
        raise KeybindingError("A key must be a non-empty string such as 'Ctrl+Shift+P'.")
    raw = text.strip()
    # Split on '+' but keep a trailing literal '+'-free key; '+' itself is not an allowed key.
    parts = raw.split("+")
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
    def tool_spec(self) -> dict:
        lines = [f"{a.id}: {a.description} [{', '.join(a.keys) or 'unbound'}]" for a in self.actions.values()]
        description = ("Change the keyboard shortcut for one Relay action. Keys use Qt portable format, "
                       "for example 'Ctrl+Shift+P', 'Alt+Left', 'F5'. An empty keys list unbinds the action. "
                       "Writes only Relay's keybindings.json, which Relay reloads automatically.\n"
                       "Actions (id: description [current keys]):\n" + "\n".join(lines))
        return {"type": "function", "function": {"name": "set_keybinding", "description": description,
                "parameters": {"type": "object", "properties": {
                    "action": {"type": "string", "enum": list(self.actions)},
                    "keys": {"type": "array", "items": {"type": "string"}, "maxItems": MAX_KEYS}},
                    "required": ["action", "keys"], "additionalProperties": False}}}

    def prepare(self, args: dict) -> tuple[dict, str]:
        if set(args) - {"action", "keys"} or "action" not in args or "keys" not in args:
            raise KeybindingError("set_keybinding takes exactly 'action' and 'keys'.")
        action = args["action"]
        if not isinstance(action, str) or action not in self.actions:
            raise KeybindingError(f"Unknown action {action!r}. Choose one of the listed action ids.")
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
