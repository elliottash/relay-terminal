# SPDX-License-Identifier: AGPL-3.0-or-later
"""Poll subscription quota windows without delaying the worker protocol thread (#XH4K).

The two vendor endpoints are separate from their model API bases. They are best-effort:
malformed or unavailable answers leave the last good snapshot in place, and keys never enter
logs, events, or the cache. A successful poll re-emits even unchanged figures so the 30-minute
freshness rule used by tied-rank selection continues to have evidence.
"""
from __future__ import annotations

import datetime as dt
import json
import logging
import math
import threading
import time
import urllib.request

_log = logging.getLogger("relay.provider_limits")
ZAI_URL = "https://api.z.ai/api/monitor/usage/quota/limit"
KIMI_URL = "https://api.kimi.com/coding/v1/usages"
POLL_SECONDS = 15 * 60
TIMEOUT_SECONDS = 15
PRESETS = ("glm-coding", "kimi-code")

_lock = threading.Lock()
_last: dict[str, dict] = {}
_started = False
_listener = None


def set_listener(callback) -> None:
    global _listener
    _listener = callback


def last(preset: str) -> dict:
    with _lock:
        held = _last.get(preset)
        return {"windows": [dict(w) for w in held["windows"]], "updated_at": held["updated_at"]} if held else {}


def _number(value) -> float | None:
    if isinstance(value, bool):
        return None
    try:
        n = float(value)
    except (TypeError, ValueError):
        return None
    return n if math.isfinite(n) else None


def _timestamp(value) -> int | None:
    n = _number(value)
    if n is not None:
        if n > 10_000_000_000:  # vendor epoch milliseconds
            n /= 1000
        return int(n) if n > 0 else None
    if isinstance(value, str):
        try:
            return int(dt.datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp())
        except ValueError:
            pass
    return None


def _window(kind: str, used: float | None, reset) -> dict | None:
    if used is None or not 0 <= used <= 100:
        return None
    return {"kind": kind, "used_percent": round(used, 1), "resets_at": _timestamp(reset)}


def parse_zai(payload) -> list[dict]:
    """Only coding credit/token rows; unit 3 is 5h and unit 6 is weekly."""
    if not isinstance(payload, dict) or payload.get("code", 200) != 200:
        return []
    data = payload.get("data") or payload
    rows = data.get("limits") if isinstance(data, dict) else None
    if not isinstance(rows, list):
        return []
    found = {}
    for row in rows:
        if not isinstance(row, dict) or row.get("type") not in ("CREDIT_LIMIT", "TOKENS_LIMIT"):
            continue
        kind = {3: "5h", 6: "weekly"}.get(row.get("unit"))
        if not kind:
            continue
        window = _window(kind, _number(row.get("percentage")), row.get("nextResetTime"))
        if window:
            found[kind] = window
    return [found[k] for k in ("5h", "weekly") if k in found]


def _count_window(kind: str, row) -> dict | None:
    if not isinstance(row, dict):
        return None
    limit = _number(row.get("limit"))
    remaining = _number(row.get("remaining"))
    if limit is None or remaining is None or limit <= 0:
        return None
    return _window(kind, (1 - remaining / limit) * 100, row.get("resetTime"))


def parse_kimi(payload) -> list[dict]:
    """Accept the current ratio pools and the older count-based /usages response."""
    if not isinstance(payload, dict) or payload.get("error"):
        return []
    found = {}
    pools = payload.get("usages")
    if isinstance(pools, dict):
        for field, kind in (("limit_5h", "5h"), ("limit_7d", "weekly"),
                            ("limit_month_total", "monthly")):
            pool = pools.get(field)
            if isinstance(pool, dict):
                ratio = _number(pool.get("used_ratio"))
                window = _window(kind, ratio * 100 if ratio is not None else None,
                                 pool.get("reset_time"))
                if window:
                    found[kind] = window
    for row in payload.get("limits") or ():
        if not isinstance(row, dict):
            continue
        duration = row.get("window")
        if not isinstance(duration, dict) or duration.get("duration") != 300 \
                or duration.get("timeUnit") != "TIME_UNIT_MINUTE":
            continue
        found.setdefault("5h", _count_window("5h", row.get("detail")))
    if "weekly" not in found:
        weekly = _count_window("weekly", payload.get("usage"))
        if weekly:
            found["weekly"] = weekly
    return [found[k] for k in ("5h", "weekly", "monthly") if found.get(k)]


def fetch(preset: str, key: str, *, opener=None) -> list[dict]:
    url = ZAI_URL if preset == "glm-coding" else KIMI_URL
    request = urllib.request.Request(url, headers={"Authorization": "Bearer " + key,
                                                    "Accept": "application/json"})
    opener = opener or urllib.request.urlopen
    with opener(request, timeout=TIMEOUT_SECONDS) as response:
        payload = json.loads(response.read(1024 * 1024))
    return parse_zai(payload) if preset == "glm-coding" else parse_kimi(payload)


def poll_once(*, key_lookup, emit, fetcher=fetch, clock=time.time) -> None:
    for preset in PRESETS:
        try:
            key = key_lookup(preset)
            if not key:
                continue
            windows = fetcher(preset, key)
            if not windows:
                continue
            with _lock:
                _last[preset] = {"windows": windows, "updated_at": int(clock())}
            emit({"event": "usage_limits", "preset": preset, "windows": windows,
                  "source": "provider_poll"})
            if _listener:
                _listener()
        except Exception as exc:
            # Do not include an exception string: HTTP clients may echo a credential or body.
            _log.debug("%s quota poll failed (%s)", preset, type(exc).__name__)


def start(*, key_lookup, emit, fetcher=fetch) -> None:
    global _started
    with _lock:
        if _started:
            return
        _started = True

    def run():
        while True:
            poll_once(key_lookup=key_lookup, emit=emit, fetcher=fetcher)
            time.sleep(POLL_SECONDS)

    threading.Thread(target=run, name="relay-provider-limits", daemon=True).start()
