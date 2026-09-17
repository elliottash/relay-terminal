# SPDX-License-Identifier: GPL-3.0-or-later
"""The keys modal's Test button: one minimal call per provider (protocol 13.8).

The call sends a two-word prompt with no tools and a tiny output budget, so it costs a handful of
tokens. It answers one question only: does this key reach this endpoint? The key itself is read from
the keystore inside the worker, is never echoed, and never appears in the reply or the error text —
provider error bodies can quote the submitted request, so only the HTTP status survives
(provider.ProviderError already strips the body).
"""
from __future__ import annotations

import threading
import time

from .presets import PRESETS, apply_effort
from .provider import ChatProvider, ProviderConfig

SYSTEM = "Reply with the single word: ok"
USER = "ping"
# Reasoning models spend output tokens on thinking before the word, and several of them cannot turn
# thinking off at all, so a tight budget makes the answer come back truncated. 1024 is still a few
# tenths of a cent, and a truncated answer counts as a pass anyway (see _TRUNCATED below).
MAX_TOKENS = 1024
TIMEOUT_S = 30
# provider.py raises this when the model hit the output limit. For an ordinary turn that is a real
# failure; for the key test it is a pass, because the request was authenticated, routed and answered
# — the only thing it did not do is finish a sentence nobody reads. There is no error code to match
# on, so this matches the message, which is a literal in provider.py.
_TRUNCATED = "truncated or filtered"


def _provider(preset_id: str, key: str) -> ChatProvider:
    preset = PRESETS[preset_id]
    # Ask for the least thinking this provider allows: the test is about reachability, not quality.
    extra, _ = apply_effort(dict(preset.extra), preset.effort_style, "low")
    return ChatProvider(ProviderConfig(preset.base_url, preset.model, key, extra, MAX_TOKENS))


def check(preset_id: str, key: str, factory=_provider) -> dict:
    """Run the call. Returns the body of a ``key_tested`` event; never raises for provider trouble."""
    from . import sidecall
    preset = PRESETS[preset_id]
    started = time.monotonic()
    result = {"preset": preset_id, "model": preset.model}
    try:
        reply, _ = sidecall.call(factory(preset_id, key), SYSTEM, USER)
        result["ok"] = True
        # A model that answers anything at all proves the key and endpoint; the text is not checked.
        result["reply_chars"] = len(reply)
    except Exception as exc:                       # ProviderError, timeouts, DNS, bad JSON
        text = str(exc)
        if type(exc).__name__ == "ProviderError" and _TRUNCATED in text:
            result["ok"] = True
            result["truncated"] = True
            result["reply_chars"] = 0
        else:
            result["ok"] = False
            result["error"] = text[:300] if type(exc).__name__ in ("ProviderError", "ValueError", "OSError") \
                else f"Test failed ({type(exc).__name__})."
    result["elapsed_ms"] = int((time.monotonic() - started) * 1000)
    return result


def run(preset_id: str, emit, request_id=None, lookup=None, factory=_provider) -> threading.Thread:
    """Test a stored key on a background thread and emit exactly one ``key_tested`` event."""
    from . import keystore
    if preset_id not in PRESETS:
        raise ValueError("Unknown provider preset.")
    key = (lookup or keystore.lookup)(preset_id)
    if not key:
        emit({"event": "key_tested", "id": request_id, "preset": preset_id, "ok": False,
              "model": PRESETS[preset_id].model, "elapsed_ms": 0,
              "error": "No key is stored for this provider."})
        return None

    def work():
        emit({"event": "key_tested", "id": request_id, **check(preset_id, key, factory)})

    thread = threading.Thread(target=work, name="relay-key-test", daemon=True)
    thread.start()
    return thread
