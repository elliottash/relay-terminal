# SPDX-License-Identifier: AGPL-3.0-or-later
"""Key accounts: more than one Z.AI Coding Plan or Kimi Code subscription, side by side (#YC0T).

A subscription plan reached with an API key has one key per preset — `glm-coding`, `kimi-code` —
so a second subscription had nowhere to go. An *account* here is a name and a key for one of those
presets, the keyed counterpart of #M8S2's guest accounts: its preset id is ``<preset>:<id>``
(``glm-coding:ethz``), it resolves to the base preset under that id with a label that names the
account, and its key lives in the keyring under that id (``keystore`` accepts ``<builtin>:<slug>``),
or in ``RELAY_GLM_CODING_ETHZ_API_KEY``. The key never touches this file. Everything that already
carries a preset id — the model box, the five lists, a quota hold, the usage poll — then carries the
account, so routing weighs and fails over between two subscriptions of the same plan.

The registry is ``$XDG_CONFIG_HOME/relay/key-accounts.json`` (``RELAY_KEY_ACCOUNTS`` overrides it)::

    {"accounts": [{"id": "ethz", "preset": "glm-coding", "label": "ethz"}]}

The worker answers ``key_accounts`` (the list), ``key_account_save {preset, label, id?, api_key?}``
and ``key_account_delete {id: "glm-coding:ethz"}``; a save or a delete pushes a fresh ``presets``.
"""
from __future__ import annotations

import json
import os
import re
import threading
from dataclasses import dataclass, replace
from pathlib import Path

from . import keystore
from .presets import PRESETS, Preset

ENV_PATH = "RELAY_KEY_ACCOUNTS"
TYPES = {"key_accounts", "key_account_save", "key_account_delete"}
# The subscription plans reached with a key. Pay-as-you-go APIs bill per token, so a second key of
# the same account buys nothing; these two sell capacity per subscription.
PLANS = ("glm-coding", "kimi-code")
MAX_ACCOUNTS = 32
MAX_LABEL = 60

_ID = re.compile(r"^[a-z0-9][a-z0-9-]{0,31}$")
_lock = threading.Lock()


@dataclass(frozen=True)
class Account:
    id: str          # the slug, unique within its preset
    preset: str      # the base preset, one of PLANS
    label: str

    @property
    def preset_id(self) -> str:
        return f"{self.preset}:{self.id}"

    def to_dict(self) -> dict:
        return {"id": self.id, "preset": self.preset, "label": self.label, "preset_id": self.preset_id,
                "has_stored_key": bool(keystore.key_source(self.preset_id)),
                "key_source": keystore.key_source(self.preset_id)}


def split(preset_id) -> tuple[str, str]:
    """("glm-coding", "ethz") for "glm-coding:ethz"; ("", "") for anything that is not one."""
    if not isinstance(preset_id, str) or preset_id.count(":") != 1:
        return "", ""
    base, slug = preset_id.split(":")
    return (base, slug) if base in PLANS and _ID.match(slug) else ("", "")


def is_account_id(preset_id) -> bool:
    return bool(split(preset_id)[0])


def make_id(text: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", (text or "").lower()).strip("-")[:32].strip("-")
    return slug or "account"


def config_path() -> Path:
    override = os.environ.get(ENV_PATH)
    if override:
        return Path(override)
    base = os.environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")
    return Path(base) / "relay" / "key-accounts.json"


def _from_dict(raw) -> Account | None:
    if not isinstance(raw, dict):
        return None
    slug, preset, label = raw.get("id"), raw.get("preset"), raw.get("label")
    if not (isinstance(slug, str) and _ID.match(slug) and preset in PLANS):
        return None
    label = label.strip()[:MAX_LABEL] if isinstance(label, str) and label.strip() else slug
    return Account(slug, preset, label)


def accounts(preset: str | None = None) -> list[Account]:
    """Every saved account, in the order they were added; a malformed file reads as none."""
    try:
        data = json.loads(config_path().read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return []
    rows = data.get("accounts") if isinstance(data, dict) else None
    found, seen = [], set()
    for raw in rows if isinstance(rows, list) else ():
        entry = _from_dict(raw)
        if entry is not None and entry.preset_id not in seen and (preset is None or entry.preset == preset):
            seen.add(entry.preset_id)
            found.append(entry)
    return found


def find(preset_id) -> Account | None:
    base, slug = split(preset_id)
    return next((a for a in accounts(base) if a.id == slug), None) if base else None


def as_preset(preset_id) -> Preset | None:
    """The base preset under the account's own id, labelled with the account's name."""
    entry = find(preset_id)
    if entry is None:
        return None
    base = PRESETS[entry.preset]
    return replace(base, id=entry.preset_id, label=f"{base.label} ({entry.label})")


def _write(items: list[Account]) -> None:
    path = config_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(".tmp")
    temp.write_text(json.dumps({"accounts": [{"id": a.id, "preset": a.preset, "label": a.label}
                                             for a in items]}, indent=2) + "\n", encoding="utf-8")
    os.replace(temp, path)


def save(spec: dict) -> Account:
    """Add or rename an account, and store its key when one is given. Raises ValueError."""
    if not isinstance(spec, dict):
        raise ValueError("An account is an object with a preset and a label.")
    preset = spec.get("preset")
    if preset not in PLANS:
        raise ValueError("Accounts are for the Z.AI Coding Plan and Kimi Code presets.")
    label = spec.get("label")
    if not isinstance(label, str) or not label.strip():
        raise ValueError("An account needs a name.")
    label = label.strip()[:MAX_LABEL]
    slug = spec.get("id") or make_id(label)
    if not isinstance(slug, str) or not _ID.match(slug):
        raise ValueError("Account ids are lowercase letters, digits and dashes.")
    key = spec.get("api_key")
    with _lock:
        items = accounts()
        if spec.get("id") is None:
            # A new account (no id given) whose name slugs to one already taken gets a numbered
            # id; only a save that names the id renames or re-keys that account.
            base, n = slug, 2
            while any(a.preset == preset and a.id == slug for a in items):
                slug, n = f"{base[:29]}-{n}", n + 1
        existing = next((a for a in items if a.preset == preset and a.id == slug), None)
        if existing is None and len(items) >= MAX_ACCOUNTS:
            raise ValueError(f"At most {MAX_ACCOUNTS} accounts.")
        entry = Account(slug, preset, label)
        if isinstance(key, str) and key.strip():
            keystore.store(entry.preset_id, key)       # before the registry: no row without a key
        items = [entry if (a.preset, a.id) == (preset, slug) else a for a in items]
        if existing is None:
            items.append(entry)
        _write(items)
    return entry


def delete(preset_id) -> bool:
    """Forget the account and remove its key from the keyring. False when there was none."""
    base, slug = split(preset_id)
    if not base:
        return False
    with _lock:
        items = accounts()
        kept = [a for a in items if (a.preset, a.id) != (base, slug)]
        if len(kept) == len(items):
            return False
        _write(kept)
    try:
        keystore.remove(preset_id)
    except keystore.KeystoreError:
        pass                                            # the registry row is gone either way
    return True


def handle(request: dict, emit) -> None:
    kind, request_id = request.get("type"), request.get("id")
    try:
        if kind == "key_account_save":
            entry = save(request.get("account") or {})
            emit({"event": "key_account_saved", "id": request_id, "account": entry.to_dict()})
        elif kind == "key_account_delete":
            removed = delete(request.get("key"))
            emit({"event": "key_account_deleted", "id": request_id, "key": request.get("key"),
                  "removed": removed})
        emit({"event": "key_accounts", "id": request_id if kind == "key_accounts" else None,
              "accounts": [a.to_dict() for a in accounts()]})
    except (ValueError, keystore.KeystoreError) as exc:
        emit({"event": "error", "id": request_id, "text": str(exc)})
