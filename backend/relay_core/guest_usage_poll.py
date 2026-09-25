# SPDX-License-Identifier: AGPL-3.0-or-later
"""Best-effort, read-only quota polling for signed-in subscription guests.

Guest turn events are authoritative while a turn runs. This poll fills the interval before the
first turn and refreshes idle accounts, so a new pane can route on current usage. No reset is
spent and neither tokens nor raw HTTP replies reach logs or the worker protocol.
"""
from __future__ import annotations

import datetime as dt
import json
import logging
import math
import os
from pathlib import Path
import threading
import time
import urllib.request

from . import guest_accounts, guest_harness_provider

_log = logging.getLogger("relay.guest_usage_poll")
POLL_SECONDS = 15 * 60
CLAUDE_URL = "https://api.anthropic.com/api/oauth/usage?cedar_ember=1&skip_spend=1"
CODEX_URL = "https://chatgpt.com/backend-api/wham/usage"
_started = False
_lock = threading.Lock()


def _epoch(value):
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)) and math.isfinite(value):
        return int(value / 1000 if value > 10_000_000_000 else value) if value > 0 else None
    if isinstance(value, str):
        try:
            return int(dt.datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp())
        except ValueError:
            return None
    return None


def _percent(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        return None
    return round(max(0.0, min(100.0, float(value))), 1)


def _read_json(url, headers, opener=None):
    request = urllib.request.Request(url, headers=headers)
    with (opener or urllib.request.urlopen)(request, timeout=15) as response:
        return json.loads(response.read(1024 * 1024))


def fetch_claude(config_dir: str, *, opener=None) -> dict:
    # Reuse the CLI login but never pass its token or directory to an event or log.
    from .guest_harness_claude import _installed_version, _oauth_token, parse_resets
    token = _oauth_token(config_dir)
    if not token:
        return {}
    binary = os.environ.get("RELAY_CLAUDE_BIN") or "claude"
    version = _installed_version(binary) or "0.0.0"
    data = _read_json(CLAUDE_URL, {"Authorization": "Bearer " + token,
                      "anthropic-beta": "oauth-2025-04-20",
                      "User-Agent": f"claude-cli/{version} (external, cli)",
                      "Accept": "application/json"}, opener)
    windows = []
    for wire, kind in (("five_hour", "5h"), ("seven_day", "weekly")):
        item = data.get(wire) if isinstance(data, dict) else None
        if not isinstance(item, dict):
            continue
        used, reset = _percent(item.get("utilization")), _epoch(item.get("resets_at"))
        if used is not None:
            windows.append({"kind": kind, "used_percent": used, "resets_at": reset})
    if not windows:
        return {}
    out = {"windows": windows}
    credits = parse_resets(data)
    if credits is not None:
        out["resets_available"], out["resets_expire_at"] = credits
    return out


def fetch_codex(config_dir: str, *, opener=None) -> dict:
    try:
        auth = json.loads((Path(config_dir) / "auth.json").read_text(encoding="utf-8"))
        token = auth["tokens"]["access_token"]
        account = auth["tokens"].get("account_id")
    except (OSError, ValueError, KeyError, TypeError):
        return {}
    if not isinstance(token, str) or not token:
        return {}
    headers = {"Authorization": "Bearer " + token, "Accept": "application/json",
               "User-Agent": "codex_cli_rs/0.0.0 (Relay; read-only)"}
    if isinstance(account, str) and account:
        headers["chatgpt-account-id"] = account
    data = _read_json(CODEX_URL, headers, opener)
    limits = data.get("rate_limit") if isinstance(data, dict) else None
    if not isinstance(limits, dict):
        return {}
    windows = []
    for name in ("primary_window", "secondary_window"):
        item = limits.get(name)
        if not isinstance(item, dict):
            continue
        duration = item.get("limit_window_seconds")
        kind = "weekly" if isinstance(duration, (int, float)) and duration >= 86400 else "5h"
        used, reset = _percent(item.get("used_percent")), _epoch(item.get("reset_at"))
        if used is not None:
            windows.append({"kind": kind, "used_percent": used, "resets_at": reset})
    if not windows:
        return {}
    out = {"windows": windows}
    credits = data.get("rate_limit_reset_credits")
    if isinstance(credits, dict):
        count = credits.get("available_count")
        if isinstance(count, (int, float)) and not isinstance(count, bool) and count >= 0:
            out["resets_available"] = int(count)
        expiries = [_epoch(x.get("expires_at") or x.get("expiresAt"))
                    for x in credits.get("credits", []) if isinstance(x, dict)]
        expiries = [x for x in expiries if x]
        if expiries:
            out["resets_expire_at"] = min(expiries)
    return out


def poll_once(*, emit, changed=None, fetchers=None) -> None:
    fetchers = fetchers or {"claude": fetch_claude, "codex": fetch_codex}
    targets = [("claude", "", os.environ.get("CLAUDE_CONFIG_DIR") or str(Path.home() / ".claude")),
               ("codex", "", os.environ.get("CODEX_HOME") or str(Path.home() / ".codex"))]
    targets += [(row.guest, row.id, row.config_dir)
                for family in ("claude", "codex") for row in guest_accounts.accounts(family)]
    any_new = False
    for family, account, config_dir in targets:
        try:
            data = fetchers[family](config_dir)
            if not data:
                continue
            event = guest_harness_provider.usage_limits_event(family, data, account)
            if event:
                event["source"] = "subscription_poll"
                emit(event)
                any_new = True
        except Exception as exc:
            # HTTP errors can include headers/body. Only the exception type is safe to log.
            _log.debug("%s quota poll failed (%s)", family, type(exc).__name__)
    if any_new and changed:
        changed()


def start(*, emit, changed=None) -> None:
    global _started
    with _lock:
        if _started:
            return
        _started = True

    def run():
        while True:
            poll_once(emit=emit, changed=changed)
            time.sleep(POLL_SECONDS)

    threading.Thread(target=run, name="relay-guest-usage", daemon=True).start()
