# SPDX-License-Identifier: AGPL-3.0-or-later
"""OpenRouter's live model list, as the `openrouter` preset's catalog (owner, 2026-09-20).

The built-in `MODEL_CATALOG` names three OpenRouter rows; OpenRouter serves several hundred, and
the id box's completion should offer them all without anyone maintaining a list by hand. So the
worker fetches ``https://openrouter.ai/api/v1/models`` (no key needed) **once per worker process,
on a background thread**, and `presets.catalog_rows("openrouter")` appends what it found after the
built-in tier rows. What it found is cached at ``$XDG_CACHE_HOME/relay/openrouter-models.json``
for 24 hours, so the next worker serves the cache first and refreshes only when it is stale.

Rules, in the order they matter:

- **Never on the protocol thread.** `rows()` reads memory, else the cache file (a small local
  read); the fetch runs on its own thread from `start_refresh()`, which never blocks.
- **Serve the cache first, refresh after.** A stale cache is still served until the fetch lands.
- **A failure leaves what was there**, logged at debug: no rows is not an error, and neither is
  a network that is not there.
- **The worker is told when it lands** (`set_listener`), so it can push a fresh `presets` the way
  it does when the codex catalogue lands (guest_harness_provider.start_catalog_scan).

Row shape, the same keys `catalog_rows` gives a built-in row plus `context_window`:
``{"id", "label", "tier": None, "efforts", "intelligence": None, "openrouter": None,
"context_window"}``. `label` is the API's `name` lower-cased (Warp style, like every label the
GUI shows); `efforts` is the openrouter effort style's levels, narrowed to `[]` for a model whose
`supported_parameters` says it takes no `reasoning`.
"""
from __future__ import annotations

import json
import logging
import os
import threading
import time
import urllib.request
from pathlib import Path

_log = logging.getLogger("relay.openrouter_catalog")

URL = "https://openrouter.ai/api/v1/models"
CACHE_TTL_S = 24 * 60 * 60
FETCH_TIMEOUT_S = 20.0
# Set to "off" to never fetch (tests, an air-gapped machine); the cache is still served.
ENV_SWITCH = "RELAY_OPENROUTER_CATALOG"

_lock = threading.Lock()
_rows: list[dict] | None = None      # None until the cache has been looked at
_fetched_at: float = 0.0             # when the rows in memory were fetched (0: unknown)
_started = False
_listener = None
# Set once the one refresh of this process has finished, whatever it found. Tests wait on it.
ready = threading.Event()


def cache_path() -> Path:
    """``$XDG_CACHE_HOME/relay/openrouter-models.json`` (default ``~/.cache/relay/``)."""
    base = os.environ.get("XDG_CACHE_HOME") or str(Path.home() / ".cache")
    return Path(base) / "relay" / "openrouter-models.json"


def set_listener(callback) -> None:
    """Register (or clear, with None) the callable told when a fetch has landed new rows."""
    global _listener
    _listener = callback


def parse_rows(payload) -> list[dict]:
    """The API's ``{"data": [...]}`` (or the list itself) as catalog rows. Entries without a
    string id are dropped; the rest never raise, whatever shape a field turns out to be."""
    from .presets import DEFAULT_CONTEXT_WINDOW, effort_levels
    entries = payload.get("data") if isinstance(payload, dict) else payload
    if not isinstance(entries, list):
        return []
    levels = effort_levels("openrouter")
    out: list[dict] = []
    seen: set[str] = set()
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        slug = entry.get("id")
        if not isinstance(slug, str) or not slug.strip() or slug in seen:
            continue
        seen.add(slug)
        name = entry.get("name")
        label = name.strip().lower() if isinstance(name, str) and name.strip() else slug.lower()
        supported = entry.get("supported_parameters")
        efforts = list(levels) if (not isinstance(supported, list) or "reasoning" in supported) else []
        window = entry.get("context_length")
        if not isinstance(window, int) or window <= 0:
            top = entry.get("top_provider")
            window = top.get("context_length") if isinstance(top, dict) else None
        if not isinstance(window, int) or window <= 0:
            window = DEFAULT_CONTEXT_WINDOW
        out.append({"id": slug, "label": label, "tier": None, "efforts": efforts,
                    "intelligence": None, "openrouter": None, "context_window": int(window)})
    return out


def _read_cache() -> tuple[list[dict], float]:
    """The cached rows and when they were fetched; ([], 0.0) when there is no usable cache."""
    try:
        data = json.loads(cache_path().read_text(encoding="utf-8"))
        rows = data.get("rows")
        fetched_at = data.get("fetched_at")
        if not isinstance(rows, list) or not isinstance(fetched_at, (int, float)):
            return [], 0.0
        return [r for r in rows if isinstance(r, dict) and isinstance(r.get("id"), str)], float(fetched_at)
    except FileNotFoundError:
        return [], 0.0
    except Exception as exc:                        # a truncated or foreign file: as if absent
        _log.debug("openrouter catalog cache unreadable: %s", exc)
        return [], 0.0


def _write_cache(rows: list[dict], fetched_at: float) -> None:
    path = cache_path()
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_name(path.name + ".tmp")
        tmp.write_text(json.dumps({"fetched_at": fetched_at, "rows": rows}), encoding="utf-8")
        os.replace(tmp, path)
    except Exception as exc:                        # a read-only cache dir costs nothing but the cache
        _log.debug("openrouter catalog cache not written: %s", exc)


def _load_locked() -> None:
    """Look at the cache once. Caller holds the lock."""
    global _rows, _fetched_at
    if _rows is None:
        _rows, _fetched_at = _read_cache()


def rows() -> list[dict]:
    """What is known right now: the rows in memory, else the cache file, else []. Never fetches."""
    with _lock:
        _load_locked()
        return [dict(r) for r in _rows or []]


def stale(now: float | None = None) -> bool:
    """Whether the rows in hand are older than the TTL (or there are none)."""
    with _lock:
        _load_locked()
        return not _rows or (now if now is not None else time.time()) - _fetched_at >= CACHE_TTL_S


def fetch(url: str = URL, timeout: float = FETCH_TIMEOUT_S):
    """The default fetcher: GET the listing and return the parsed JSON body."""
    request = urllib.request.Request(url, headers={"Accept": "application/json",
                                                   "User-Agent": "relay-terminal"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def start_refresh(fetcher=None, *, force: bool = False) -> bool:
    """Start the one background refresh of this process, if the cache is stale and nothing has
    started it. Never blocks. Returns whether a thread was started.

    ``fetcher`` returns the parsed listing (tests hand in a canned one); ``force`` refreshes a
    fresh cache too. With ``RELAY_OPENROUTER_CATALOG=off`` nothing is ever fetched.
    """
    global _started
    with _lock:
        if _started:
            return False
        _started = True
    if os.environ.get(ENV_SWITCH, "").lower() in ("off", "0", "false", "no"):
        ready.set()
        return False
    if not force and not stale():
        ready.set()
        return False
    get = fetcher or fetch

    def refresh():
        global _rows, _fetched_at
        try:
            parsed = parse_rows(get())
        except Exception as exc:            # offline, a 5xx, a body that is not JSON: keep what we had
            _log.debug("openrouter catalog not fetched: %s", exc)
            ready.set()
            return
        if not parsed:
            _log.debug("openrouter catalog fetched but empty: keeping %d cached rows", len(_rows or []))
            ready.set()
            return
        now = time.time()
        with _lock:
            _rows, _fetched_at = parsed, now
        _write_cache(parsed, now)
        listener = _listener
        ready.set()
        if listener is not None:
            try:
                listener()          # the worker pushes a fresh `presets` now that there is a list
            except Exception:                                          # pragma: no cover
                _log.debug("openrouter catalog listener failed", exc_info=True)

    threading.Thread(target=refresh, name="relay-openrouter-catalog", daemon=True).start()
    return True


def reset() -> None:
    """Forget the rows and the fact that a refresh ran (tests)."""
    global _rows, _fetched_at, _started, _listener
    with _lock:
        _rows, _fetched_at, _started, _listener = None, 0.0, False, None
    ready.clear()
