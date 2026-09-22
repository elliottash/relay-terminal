# SPDX-License-Identifier: AGPL-3.0-or-later
"""Custom providers: a named OpenAI-compatible endpoint with a key and model ids (protocol 28.6).

Warp's shape (owner, 2026-09-20): a name, a base URL, an API key and one or more model ids, and
then it is a provider like any other. Relay keeps the entries beside the local-model registry, in
``$XDG_CONFIG_HOME/relay/custom-providers.json`` (``RELAY_CUSTOM_PROVIDERS`` overrides the path),
and the rest of the backend sees one through ``as_preset()``: ``configure``, ``set_model``, a
``tiers`` or ``roles`` entry and ``test_key`` take ``preset: "custom:<slug>"`` exactly as they take
a built-in id. The key never touches this file. It is stored in the keyring under the entry id
(``keystore.store``, which accepts a ``custom:`` id and nothing else with a colon), or read from
``RELAY_CUSTOM_<SLUG>_API_KEY``; ``has_stored_key`` and ``key_source`` on the preset row say
which. A loopback ``http://`` URL is allowed and then no key is sent at all, whatever is stored:
that is the local-server rule in ``session_protocol.provider_config`` and it holds here too.

Effort: a custom endpoint has no effort knob Relay can vouch for, so ``effort_style`` defaults to
``none`` (nothing is sent, no picker) — except an openrouter.ai URL, which gets ``openrouter``
(``reasoning.effort``). ``kimi`` (top-level ``reasoning_effort``, the OpenAI shape) is there for
a provider or proxy known to take it. The choice is the entry's, not the request's, so a pane that
picks an effort on this provider sends the same shape every time.

Models: the given ids, in order, first (``label`` is the id lower-cased, Warp style), then what
the endpoint's ``/models`` listed the last time a save probed it. The probe runs on its own thread
after the save has answered, and only a listing that arrives updates the entry; nothing waits on
it.
"""
from __future__ import annotations

import json
import os
import re
import threading
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field, replace
from pathlib import Path

from . import keystore
from .presets import DEFAULT_CONTEXT_WINDOW, Preset, effort_levels, model_name, openrouter_twin
from .provider import loopback_http, shared_opener

PREFIX = "custom:"
ENV_PATH = "RELAY_CUSTOM_PROVIDERS"
GROUP = "custom"
PLAN = "custom endpoint"
EFFORT_STYLES = ("none", "openrouter", "kimi")
TYPES = {"custom_providers", "custom_provider_save", "custom_provider_delete"}
MAX_MODELS = 200
MAX_NAME = 80
# One GET to a remote host; it runs after the save has been answered, so it may take its time,
# but a dead endpoint must not hold the thread (and its key) for long.
PROBE_TIMEOUT_S = 5.0
MAX_PROBE_BODY = 1024 * 1024

_SLUG = re.compile(r"^[a-z0-9][a-z0-9-]{0,47}$")
_listener = None


def is_custom_id(value) -> bool:
    return isinstance(value, str) and value.startswith(PREFIX)


def set_listener(callback) -> None:
    """Register (or clear, with None) the callable told when a probe has changed an entry's list."""
    global _listener
    _listener = callback


def make_id(value: str) -> str:
    """``custom:<slug>`` from an id, a bare slug, or a name."""
    if not isinstance(value, str) or not value.strip():
        raise ValueError("A custom provider needs a name.")
    raw = value.strip().lower()
    raw = raw[len(PREFIX):] if raw.startswith(PREFIX) else raw
    slug = re.sub(r"[^a-z0-9]+", "-", raw).strip("-")[:48].strip("-")
    if not _SLUG.match(slug):
        raise ValueError("A custom provider id is lowercase letters, digits and hyphens.")
    return PREFIX + slug


def default_effort_style(base_url: str) -> str:
    host = (urllib.parse.urlsplit(base_url or "").hostname or "").lower()
    return "openrouter" if host == "openrouter.ai" or host.endswith(".openrouter.ai") else "none"


@dataclass(frozen=True)
class CustomProvider:
    id: str                         # "custom:<slug>"
    name: str                       # as typed; the row shows it lower-cased
    base_url: str                   # the OpenAI-compatible base, e.g. https://llm.example.com/v1
    models: tuple[str, ...]         # the ids the user gave, first one is the default
    effort_style: str = "none"
    served_models: tuple[str, ...] = ()   # what /models listed at the last probe
    note: str = ""
    extra: dict = field(default_factory=dict)

    @property
    def slug(self) -> str:
        return self.id[len(PREFIX):]

    @property
    def label(self) -> str:
        return self.name.lower()

    @property
    def model(self) -> str:
        return self.models[0]

    def as_preset(self) -> Preset:
        """This provider in the shape the rest of the backend already reads."""
        return Preset(self.id, self.label, self.base_url, self.model, dict(self.extra),
                      context_window=DEFAULT_CONTEXT_WINDOW, effort_style=self.effort_style,
                      group=GROUP, note=self.note, provider=self.label, plan=PLAN, custom=True)

    def catalog_rows(self) -> list[dict]:
        """The row shape of ``presets.catalog_rows``: the given ids, then the served ones."""
        efforts = effort_levels(self.effort_style)
        seen: set[str] = set()
        out = []
        for model_id in self.models + self.served_models:
            if model_id in seen:
                continue
            seen.add(model_id)
            name = model_name(self.id, model_id)
            out.append({"id": model_id, "name": name, "label": name, "tier": None,
                        "efforts": list(efforts), "intelligence": None,
                        "openrouter": openrouter_twin(model_id)})
        return out

    def to_dict(self) -> dict:
        """The keys of ``Preset.to_dict()`` plus what only a custom provider has. ``model_ids`` is
        the list the user typed (what an edit form shows); ``models`` is the catalog."""
        return {**self.as_preset().to_dict(), "name": self.name, "models": self.catalog_rows(),
                "model_ids": list(self.models), "served_models": list(self.served_models),
                "effort_style": self.effort_style}

    def stored(self) -> dict:
        return {"id": self.id, "name": self.name, "base_url": self.base_url, "models": list(self.models),
                "effort_style": self.effort_style, "served_models": list(self.served_models),
                "note": self.note, "extra": self.extra}


def _model_ids(value, name: str) -> tuple[str, ...]:
    if isinstance(value, str):
        value = re.split(r"[\s,]+", value)
    if value is None:
        value = []
    if not isinstance(value, list):
        raise ValueError(f"{name} must be a list of model ids.")
    out: list[str] = []
    for item in value:
        if not isinstance(item, str):
            raise ValueError(f"{name} must be a list of model ids.")
        item = item.strip()
        if not item:
            continue
        if len(item) > 200 or any(c.isspace() for c in item):
            raise ValueError("A model id is one word of at most 200 characters.")
        if item not in out:
            out.append(item)
    return tuple(out[:MAX_MODELS])


def from_dict(spec: dict) -> CustomProvider:
    """Validate one entry. Raises ValueError with a sentence a person can act on. ``api_key`` in
    ``spec`` is ignored here: the key goes to the keyring (``save_with_key``), never into the entry."""
    if not isinstance(spec, dict):
        raise ValueError("A custom provider must be an object.")
    name = str(spec.get("name") or spec.get("label") or "").strip()
    if not name:
        raise ValueError("A custom provider needs a name.")
    base_url = str(spec.get("base_url") or "").strip().rstrip("/")
    url = urllib.parse.urlsplit(base_url)
    if url.scheme not in ("https", "http") or not url.hostname:
        raise ValueError("The base URL is the provider's OpenAI-compatible base, https://host/v1.")
    if url.scheme == "http" and not loopback_http(base_url):
        raise ValueError("Unencrypted http:// is only allowed to localhost, 127.0.0.1 or ::1. "
                         "A provider elsewhere needs https.")
    if url.username or url.password or url.query or url.fragment:
        raise ValueError("The base URL must not carry credentials, a query or a fragment.")
    models = _model_ids(spec.get("models"), "models")
    if not models:
        raise ValueError("A custom provider needs at least one model id.")
    style = spec.get("effort_style")
    if style is None or style == "":
        style = default_effort_style(base_url)
    if style not in EFFORT_STYLES:
        raise ValueError("effort_style must be one of " + ", ".join(EFFORT_STYLES) + ".")
    extra = spec.get("extra", {})
    if not isinstance(extra, dict):
        raise ValueError("Extra parameters must be a JSON object.")
    # Keep the same top-level contract as ProviderConfig.validate; values belong to the endpoint.
    allowed = {"thinking", "reasoning", "reasoning_effort", "temperature", "top_p"}
    if set(extra) - allowed:
        raise ValueError("Extra parameters may only contain thinking, reasoning, reasoning_effort, temperature, and top_p.")
    try:
        extra = json.loads(json.dumps(extra, allow_nan=False))
    except (ValueError, TypeError) as exc:
        raise ValueError("Extra parameters must contain valid JSON values.") from exc
    served = _model_ids(spec.get("served_models"), "served_models")
    return CustomProvider(make_id(spec.get("id") or name), name[:MAX_NAME], base_url, models, style,
                          served, str(spec.get("note") or "")[:200], extra)


# ----- the registry file ---------------------------------------------------------------------
def config_path() -> Path:
    override = os.environ.get(ENV_PATH, "").strip()
    if override:
        return Path(override).expanduser()
    base = os.environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")
    return Path(base) / "relay" / "custom-providers.json"


_lock = threading.Lock()
_cache: tuple[str, float, int, dict[str, CustomProvider]] | None = None


def catalog() -> dict[str, CustomProvider]:
    """Every saved provider by id. Re-read when the file changes: each pane has its own worker, and
    a provider added in one must be there for the next configure in another."""
    global _cache
    path = config_path()
    try:
        stat = path.stat()
    except OSError:
        return {}
    with _lock:
        if _cache is not None and _cache[:3] == (str(path), stat.st_mtime, stat.st_size):
            return dict(_cache[3])
        items: dict[str, CustomProvider] = {}
        try:
            raw = json.loads(path.read_text(encoding="utf-8"))
            for spec in (raw.get("providers") if isinstance(raw, dict) else None) or []:
                try:
                    entry = from_dict(spec)
                except ValueError:
                    continue            # one bad entry must not hide the others
                items[entry.id] = entry
        except (OSError, ValueError):
            items = {}
        _cache = (str(path), stat.st_mtime, stat.st_size, items)
        return dict(items)


def find(provider_id) -> CustomProvider | None:
    return catalog().get(provider_id) if is_custom_id(provider_id) else None


def match(base_url: str) -> CustomProvider | None:
    """The saved provider at a base URL, for a request that named no preset."""
    wanted = (base_url or "").strip().rstrip("/").lower()
    if not wanted:
        return None
    return next((e for e in catalog().values() if e.base_url.lower() == wanted), None)


def _write(items: dict[str, CustomProvider]) -> None:
    global _cache
    path = config_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    body = json.dumps({"version": 1, "providers": [e.stored() for e in items.values()]},
                      indent=2, ensure_ascii=False) + "\n"
    temp = path.with_name(path.name + f".{os.getpid()}.tmp")
    temp.write_text(body, encoding="utf-8")
    os.replace(temp, path)
    with _lock:
        _cache = None


def save(spec: dict) -> CustomProvider:
    """Validate and write one entry; the same id replaces. The served list survives a re-save of
    the same endpoint (the probe refreshes it) and is dropped when the base URL changes."""
    entry = from_dict(spec)
    items = catalog()
    previous = items.get(entry.id)
    if previous is not None:
        # Older callers omit extra: preserve it on edits. An explicit {} clears it.
        if "extra" not in spec:
            entry = replace(entry, extra=previous.extra)
        if not entry.served_models and previous.base_url == entry.base_url:
            entry = replace(entry, served_models=previous.served_models)
    items[entry.id] = entry
    _write(items)
    return entry


def delete(provider_id) -> bool:
    items = catalog()
    if not is_custom_id(provider_id) or provider_id not in items:
        return False
    del items[provider_id]
    _write(items)
    return True


def key_source(provider_id: str) -> str:
    """Where this provider's key comes from: "env", "keyring", "" — or "local" for a loopback
    endpoint, which sends none."""
    entry = find(provider_id)
    if entry is not None and loopback_http(entry.base_url):
        return "local"
    return keystore.key_source(provider_id)


def row(entry: CustomProvider) -> dict:
    """One ``presets`` event row: the entry plus where its key comes from."""
    source = key_source(entry.id)
    return {**entry.to_dict(), "has_stored_key": bool(source) and source != "local", "key_source": source}


def rows() -> list[dict]:
    return [row(entry) for entry in catalog().values()]


# ----- the /models probe -------------------------------------------------------------------
def fetch_models(base_url: str, api_key: str, *, timeout: float = PROBE_TIMEOUT_S) -> list[str] | None:
    """The ids ``GET <base_url>/models`` lists, or None when the endpoint does not answer one.
    Never raises. No redirects (the key must not follow one), a bounded body, no key to loopback."""
    headers = {"Accept": "application/json", "User-Agent": "Relay/0.1"}
    if api_key and not loopback_http(base_url):
        headers["Authorization"] = f"Bearer {api_key}"
    request = urllib.request.Request(base_url.rstrip("/") + "/models", headers=headers, method="GET")
    opener = shared_opener()                       # one per process, not one per probe (#TZWF)
    try:
        with opener.open(request, timeout=timeout) as response:
            if response.status != 200:
                return None
            raw = response.read(MAX_PROBE_BODY + 1)
    except (urllib.error.URLError, TimeoutError, OSError, RuntimeError, ValueError):
        return None
    if len(raw) > MAX_PROBE_BODY:
        return None
    try:
        listing = json.loads(raw)
    except (ValueError, UnicodeError):
        return None
    data = listing.get("data") if isinstance(listing, dict) else listing
    if not isinstance(data, list):
        return None
    ids = [item["id"] for item in data if isinstance(item, dict) and isinstance(item.get("id"), str)]
    return [i for i in ids if i.strip() and not any(c.isspace() for c in i)][:MAX_MODELS]


def _record_served(provider_id: str, served: list[str]) -> bool:
    """Write a probe's listing into the entry, if it is still there and the list changed."""
    items = catalog()
    entry = items.get(provider_id)
    if entry is None or tuple(served) == entry.served_models:
        return False
    items[provider_id] = replace(entry, served_models=tuple(served))
    _write(items)
    return True


def probe_later(entry: CustomProvider, api_key: str) -> threading.Thread:
    """Ask the endpoint what it serves, on a thread, and tell the listener when that changed the
    entry. The save has already been answered; nothing waits on this."""
    def work():
        try:
            served = fetch_models(entry.base_url, api_key)
            if served is not None and _record_served(entry.id, served) and _listener is not None:
                _listener()
        except Exception:                   # never lets the thread die noisily; the list is optional
            pass

    thread = threading.Thread(target=work, name="relay-custom-probe", daemon=True)
    thread.start()
    return thread


# ----- worker protocol (section 28.6) --------------------------------------------------------
def save_with_key(spec: dict, api_key) -> CustomProvider:
    """Store the key first, then the entry: an entry whose key could not be kept is not saved."""
    if api_key is not None and not isinstance(api_key, str):
        raise ValueError("API key must be text.")
    entry = from_dict(spec)
    if api_key and api_key.strip():
        keystore.store(entry.id, api_key)
    return save(spec)


def handle(request: dict, emit) -> threading.Thread | None:
    """Answer one of ``TYPES``. Exactly one event follows every request; a save's ``/models``
    probe runs afterwards on the thread this returns (None when there was nothing to probe)."""
    kind, request_id = request.get("type"), request.get("id")
    if kind == "custom_providers":
        emit({"event": "custom_providers", "id": request_id, "items": rows()})
    elif kind == "custom_provider_delete":
        provider_id = request.get("provider_id")
        removed = delete(provider_id)
        key_removed, error = False, ""
        if removed:
            try:
                key_removed = keystore.remove(provider_id)
            except keystore.KeystoreError as exc:
                error = str(exc)
        event = {"event": "custom_provider_deleted", "id": request_id, "provider_id": provider_id,
                 "removed": removed, "key_removed": key_removed}
        if error:
            event["error"] = error
        emit(event)
    elif kind == "custom_provider_save":
        spec = request.get("provider")
        if not isinstance(spec, dict):
            raise ValueError("provider must be an object.")
        spec = dict(spec)
        api_key = spec.pop("api_key", None)
        if api_key is None:
            api_key = request.get("api_key")
        entry = save_with_key(spec, api_key)
        emit({"event": "custom_provider_saved", "id": request_id, "provider": row(entry)})
        if loopback_http(entry.base_url):
            return probe_later(entry, "")
        key = api_key or ""
        if not key:
            try:
                key = keystore.lookup(entry.id)
            except keystore.KeystoreError:
                key = ""
        if not key:
            return None                     # a remote /models needs the key; nothing to ask with
        return probe_later(entry, key)
    else:
        raise ValueError("Unknown custom-provider request.")
    return None
