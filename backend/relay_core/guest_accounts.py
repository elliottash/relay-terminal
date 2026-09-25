# SPDX-License-Identifier: AGPL-3.0-or-later
"""Guest accounts: more than one Claude Code or Codex login, side by side (card #M8S2).

A guest CLI keeps its login, settings and transcripts in one directory — Claude Code in
``~/.claude`` (``CLAUDE_CONFIG_DIR`` moves it), Codex in ``~/.codex`` (``CODEX_HOME``). Anthropic
documents ``CLAUDE_CONFIG_DIR`` for exactly this, running accounts side by side; users on Reddit
run two or three that way. So an *account* here is nothing but a name and a directory: Relay
starts that guest's process with the directory in its environment and the CLI does the rest.
Credentials never pass through Relay — the CLI signs in and stores them where it always does,
inside the directory (a file on Linux, a per-directory Keychain entry on macOS).

The registry is ``$XDG_CONFIG_HOME/relay/guest-accounts.json`` (``RELAY_GUEST_ACCOUNTS``
overrides the path), beside ``custom-providers.json``::

    {"accounts": [{"id": "work", "guest": "claude", "label": "work", "config_dir": "/abs/dir"}]}

An account is a preset of its own, ``guest:<guest>:<id>`` — ``guest:claude:work`` — and everything
that already carries a preset id (the model box, the five lists, the saved layout, a resume)
carries the account with it. ``guest:claude`` and ``guest:codex`` stay what they were: the CLI's
default login, whatever its environment says. The config's base URL is ``harness://<guest>/<id>``.

The worker answers ``guest_accounts`` (the list), ``guest_account_save`` and
``guest_account_delete``; a save or a delete pushes a fresh ``presets``.
"""
from __future__ import annotations

import json
import os
import re
import shlex
import threading
from dataclasses import dataclass
from pathlib import Path

from . import guest

ENV_PATH = "RELAY_GUEST_ACCOUNTS"
TYPES = {"guest_accounts", "guest_account_save", "guest_account_delete"}
GUESTS = ("claude", "codex")
MAX_ACCOUNTS = 32
MAX_LABEL = 60

# The variable each CLI reads its whole state directory from.
CONFIG_ENV = {"claude": "CLAUDE_CONFIG_DIR", "codex": "CODEX_HOME"}
# Credentials in the environment that outrank a saved subscription login. Claude Code's own
# precedence puts ANTHROPIC_API_KEY and ANTHROPIC_AUTH_TOKEN ahead of the login in the directory,
# and codex takes an API key from CODEX_API_KEY / OPENAI_API_KEY. An account launch drops them:
# the whole point of naming an account is that its login pays.
OVERRIDING_ENV = {"claude": ("ANTHROPIC_API_KEY", "ANTHROPIC_AUTH_TOKEN", "CLAUDE_CODE_OAUTH_TOKEN"),
                  "codex": ("CODEX_API_KEY", "OPENAI_API_KEY")}
# How a person signs a directory in, typed into a terminal pane (the CLIs' logins are interactive:
# a browser round trip and, for claude, a code pasted back).
LOGIN_ARGS = {"claude": ("auth", "login"), "codex": ("login",)}

_ID = re.compile(r"^[a-z0-9][a-z0-9-]{0,31}$")
_listener = None


@dataclass(frozen=True)
class Account:
    id: str
    guest: str
    label: str
    config_dir: str

    @property
    def key(self) -> str:
        """``claude:work``: the part of the preset id after ``guest:``."""
        return f"{self.guest}:{self.id}"

    @property
    def preset_id(self) -> str:
        return "guest:" + self.key

    def to_dict(self) -> dict:
        return {"id": self.id, "guest": self.guest, "label": self.label,
                "config_dir": self.config_dir, "preset": self.preset_id,
                "exists": os.path.isdir(self.config_dir),
                "login_command": login_command(self.guest, self.id, self.config_dir)}


# ----- ids ----------------------------------------------------------------------------------------


def split_key(key) -> tuple[str, str]:
    """``("claude", "work")`` for ``claude:work``, ``("claude", "")`` for ``claude``; ("", "") for
    anything that is not a guest followed by an optional account id. Syntax only: whether the
    account is registered is `find`'s question."""
    if not isinstance(key, str):
        return "", ""
    family, _, account = key.partition(":")
    if family not in GUESTS:
        return "", ""
    if account and not _ID.match(account):
        return "", ""
    return family, account


def valid_id(value) -> bool:
    return isinstance(value, str) and bool(_ID.match(value)) and value != "default"


def make_id(text: str) -> str:
    """A registry id from whatever the user typed as the account's name."""
    slug = re.sub(r"[^a-z0-9]+", "-", str(text or "").lower()).strip("-")[:32].strip("-")
    return slug or "account"


def default_config_dir(guest_id: str, account_id: str) -> str:
    """Where a new account's directory goes when the user names none: under Relay's own config,
    so nothing lands in the home directory's top level."""
    base = os.environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")
    return str(Path(base) / "relay" / "guest-accounts" / f"{guest_id}-{account_id}")


def login_command(guest_id: str, account_id: str = "", config_dir: str | None = None) -> str:
    """The shell line that signs this account in: ``CLAUDE_CONFIG_DIR=… claude auth login``."""
    binary = guest.spec(guest_id).binaries[0]
    words = [binary, *LOGIN_ARGS[guest_id]]
    line = " ".join(shlex.quote(w) for w in words)
    if not account_id:
        return line
    directory = config_dir or ((find(guest_id, account_id) or Account("", "", "", "")).config_dir)
    return f"{CONFIG_ENV[guest_id]}={shlex.quote(directory)} {line}"


# ----- the registry file --------------------------------------------------------------------------


def config_path() -> Path:
    override = os.environ.get(ENV_PATH, "").strip()
    if override:
        return Path(override).expanduser()
    base = os.environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")
    return Path(base) / "relay" / "guest-accounts.json"


_lock = threading.Lock()
_cache: tuple[str, float, int, list[Account]] | None = None


def _from_dict(raw) -> Account:
    if not isinstance(raw, dict):
        raise ValueError("An account must be an object.")
    family = raw.get("guest")
    if family not in GUESTS:
        raise ValueError("An account's guest must be claude or codex.")
    account_id = raw.get("id")
    if not valid_id(account_id):
        raise ValueError("An account id is 1-32 lower-case letters, digits and hyphens, "
                         "and not 'default'.")
    directory = raw.get("config_dir")
    if not isinstance(directory, str) or not directory.strip():
        raise ValueError("An account needs its config directory.")
    directory = os.path.abspath(os.path.expanduser(directory.strip()))
    label = " ".join(str(raw.get("label") or account_id).split())[:MAX_LABEL] or account_id
    return Account(account_id, family, label, directory)


def accounts(guest_id: str | None = None) -> list[Account]:
    """Every registered account, in the file's order; only `guest_id`'s when one is named. Re-read
    when the file changes: each pane has its own worker, and an account added in one must be
    there for the next configure in another."""
    global _cache
    path = config_path()
    try:
        stat = path.stat()
    except OSError:
        return []
    with _lock:
        if _cache is None or _cache[:3] != (str(path), stat.st_mtime, stat.st_size):
            items: list[Account] = []
            seen = set()
            try:
                raw = json.loads(path.read_text(encoding="utf-8"))
                for spec in (raw.get("accounts") if isinstance(raw, dict) else None) or []:
                    try:
                        entry = _from_dict(spec)
                    except ValueError:
                        continue            # one bad entry must not hide the others
                    if entry.key not in seen:
                        seen.add(entry.key)
                        items.append(entry)
            except (OSError, ValueError):
                items = []
            _cache = (str(path), stat.st_mtime, stat.st_size, items[:MAX_ACCOUNTS])
        found = list(_cache[3])
    return [a for a in found if guest_id is None or a.guest == guest_id]


def find(guest_id: str, account_id: str) -> Account | None:
    for entry in accounts(guest_id):
        if entry.id == account_id:
            return entry
    return None


def _write(items: list[Account]) -> None:
    path = config_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    body = {"accounts": [{"id": a.id, "guest": a.guest, "label": a.label,
                          "config_dir": a.config_dir} for a in items]}
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(body, indent=2) + "\n", encoding="utf-8")
    os.replace(tmp, path)


def save(spec: dict) -> Account:
    """Add an account, or replace the one with the same guest and id. A missing `id` is made from
    the label; a missing `config_dir` is `default_config_dir`, created empty (the CLI fills it in
    at its first login). A directory already in use by another account is refused: two accounts
    on one directory are one login under two names."""
    if not isinstance(spec, dict):
        raise ValueError("An account must be an object.")
    spec = dict(spec)
    if not spec.get("id"):
        spec["id"] = make_id(spec.get("label") or "")
    if spec.get("guest") in GUESTS and valid_id(spec.get("id")) and not spec.get("config_dir"):
        spec["config_dir"] = default_config_dir(spec["guest"], spec["id"])
    entry = _from_dict(spec)
    # Neither the default login's home nor, beside a Relay-owned one (#5A37), the user's own
    # directory: both are already read as the preset guest:<guest>.
    for default_dir in {os.path.abspath(guest.config_dir(entry.guest)),
                        os.path.abspath(guest.user_config_dir(entry.guest))}:
        if entry.config_dir == default_dir:
            raise ValueError(f"{entry.config_dir} is {guest.spec(entry.guest).name}'s default login; "
                             f"it is already the preset guest:{entry.guest}.")
    items = accounts()
    for other in items:
        if other.config_dir == entry.config_dir and other.key != entry.key:
            raise ValueError(f"{entry.config_dir} is already the account {other.label} ({other.key}).")
    replaced = [entry if a.key == entry.key else a for a in items]
    if entry.key not in {a.key for a in items}:
        if len(items) >= MAX_ACCOUNTS:
            raise ValueError(f"At most {MAX_ACCOUNTS} accounts.")
        replaced.append(entry)
    os.makedirs(entry.config_dir, mode=0o700, exist_ok=True)
    _write(replaced)
    return entry


def delete(guest_id: str, account_id: str) -> bool:
    """Take an account out of the registry. Its directory — the login and the transcripts — is
    left where it is: removing a name from Relay is not signing anybody out."""
    items = accounts()
    kept = [a for a in items if not (a.guest == guest_id and a.id == account_id)]
    if len(kept) == len(items):
        return False
    _write(kept)
    return True


# ----- launching under one ------------------------------------------------------------------------


def environment(guest_id: str, account_id: str, base: dict | None = None) -> dict:
    """The environment a process of this guest runs with for this account: `base` (os.environ by
    default) with the account's directory set and the credentials that would outrank its login
    removed. The default account ("") is `base` unchanged. An account that is not registered is a
    ValueError — a removed account must never quietly fall back to another login."""
    env = dict(os.environ if base is None else base)
    if not account_id:
        return env
    entry = find(guest_id, account_id)
    if entry is None:
        raise ValueError(f"{guest.spec(guest_id).name} has no account {account_id!r} in Relay "
                         "any more; pick another one or add it back under Options › Models.")
    env[CONFIG_ENV[guest_id]] = entry.config_dir
    for key in OVERRIDING_ENV[guest_id]:
        env.pop(key, None)
    return env


def overrides(guest_id: str, account_id: str) -> tuple[dict, tuple]:
    """(variables to set, variables to remove) for this account, for a spawner that builds its own
    environment. ({}, ()) for the default account."""
    if not account_id:
        return {}, ()
    entry = find(guest_id, account_id)
    if entry is None:
        environment(guest_id, account_id)          # raises the one sentence
    return {CONFIG_ENV[guest_id]: entry.config_dir}, OVERRIDING_ENV[guest_id]


def config_dir(guest_id: str, account_id: str = "", home: str | None = None) -> str:
    """The directory this account's CLI keeps its state in: the registered one, or for the default
    account whatever the CLI itself would use (its variable when set, else ``~/.<guest>``)."""
    if account_id:
        entry = find(guest_id, account_id)
        return entry.config_dir if entry is not None else ""
    if home is None:
        own = os.environ.get(CONFIG_ENV[guest_id], "").strip()
        if own:
            return os.path.abspath(os.path.expanduser(own))
    return guest.config_dir(guest_id, home)


# ----- the protocol -------------------------------------------------------------------------------


def set_listener(callback) -> None:
    """Told after a save or a delete (the worker pushes a fresh `presets`)."""
    global _listener
    _listener = callback


def _notify() -> None:
    if _listener is not None:
        try:
            _listener()
        except Exception:                                     # pragma: no cover
            pass


def handle(request: dict, emit) -> None:
    """`guest_accounts`, `guest_account_save` and `guest_account_delete`."""
    kind = request.get("type")
    request_id = request.get("id")
    try:
        if kind == "guest_account_save":
            entry = save(request.get("account"))
            _notify()
            # `sign_in` is the GUI's own flag, echoed: it asked to have the new account's login
            # typed into a pane once the directory exists (Options › Models, "add account…").
            emit({"event": "guest_account_saved", "id": request_id, "account": entry.to_dict(),
                  "sign_in": bool(request.get("sign_in"))})
        elif kind == "guest_account_delete":
            family, account_id = split_key(request.get("key") or "")
            if not family or not account_id:
                raise ValueError("guest_account_delete needs key: \"<guest>:<id>\".")
            removed = delete(family, account_id)
            if removed:
                _notify()
            emit({"event": "guest_account_deleted", "id": request_id,
                  "key": f"{family}:{account_id}", "removed": removed})
        emit({"event": "guest_accounts", "id": request_id,
              "accounts": [a.to_dict() for a in accounts()]})
    except ValueError as exc:
        emit({"event": "error", "id": request_id, "text": str(exc)})
