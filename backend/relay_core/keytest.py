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

from . import localmodels
from .presets import PRESETS, apply_effort
from .provider import ChatProvider, ProviderConfig, ProviderError, ProviderTruncated

SYSTEM = "Reply with the single word: ok"
USER = "ping"
# Reasoning models spend output tokens on thinking before the word, and several of them cannot turn
# thinking off at all, so a tight budget makes the answer come back truncated. 1024 is still a few
# tenths of a cent, and a truncated answer counts as a pass anyway (see ProviderTruncated below).
MAX_TOKENS = 1024
TIMEOUT_S = 30
# provider.py raises ProviderTruncated when the model hit the output limit. For an ordinary turn that
# is a real failure; for the key test it is a pass, because the request was authenticated, routed and
# answered — the only thing it did not do is finish a sentence nobody reads. It is matched by type:
# matching the message read the wording of a sentence written for the user, and broke the moment that
# sentence was rewritten.


def _preset(preset_id: str):
    """A built-in preset, or a saved model server on this machine in the same shape (protocol 23)."""
    if preset_id in PRESETS:
        return PRESETS[preset_id]
    endpoint = localmodels.find(preset_id)
    return endpoint.as_preset() if endpoint is not None else None


def _provider(preset_id: str, key: str) -> ChatProvider:
    preset = _preset(preset_id)
    if preset.local:
        # No key and no effort knob. The test still means what it says on the button: this server
        # is reachable and answers. It waits out a cold model load, which a probe does not.
        fields = localmodels.provider_fields(preset_id, preset.base_url, preset.model)
        return ChatProvider(ProviderConfig(preset.base_url, preset.model, "", dict(preset.extra),
                                           localmodels.clamp_max_tokens(MAX_TOKENS, fields), **fields))
    # Ask for the least thinking this provider allows: the test is about reachability, not quality.
    extra, _ = apply_effort(dict(preset.extra), preset.effort_style, "low")
    return ChatProvider(ProviderConfig(preset.base_url, preset.model, key, extra, MAX_TOKENS))


def check(preset_id: str, key: str, factory=_provider) -> dict:
    """Run the call. Returns the body of a ``key_tested`` event; never raises for provider trouble."""
    from . import sidecall
    preset = _preset(preset_id)
    started = time.monotonic()
    result = {"preset": preset_id, "model": preset.model}
    try:
        reply, _ = sidecall.call(factory(preset_id, key), SYSTEM, USER)
        result["ok"] = True
        # A model that answers anything at all proves the key and endpoint; the text is not checked.
        result["reply_chars"] = len(reply)
    except Exception as exc:                       # ProviderError, timeouts, DNS, bad JSON
        text = str(exc)
        if isinstance(exc, ProviderTruncated):
            result["ok"] = True
            result["truncated"] = True
            result["reply_chars"] = 0
        else:
            result["ok"] = False
            # A ProviderError subclass (a stall, say) says the useful thing in its message, and none
            # of them ever carry the provider's body or the key.
            result["error"] = text[:300] if isinstance(exc, (ProviderError, ValueError, OSError)) \
                else f"Test failed ({type(exc).__name__})."
    result["elapsed_ms"] = int((time.monotonic() - started) * 1000)
    return result


def run(preset_id: str, emit, request_id=None, lookup=None, factory=_provider) -> threading.Thread:
    """Test a stored key on a background thread and emit exactly one ``key_tested`` event."""
    from . import keystore
    preset = _preset(preset_id) if isinstance(preset_id, str) else None
    if preset is None:
        raise ValueError("Unknown provider preset.")
    key = "" if preset.local else (lookup or keystore.lookup)(preset_id)
    if not key and not preset.local:
        emit({"event": "key_tested", "id": request_id, "preset": preset_id, "ok": False,
              "model": PRESETS[preset_id].model, "elapsed_ms": 0,
              "error": "No key is stored for this provider."})
        return None

    def work():
        emit({"event": "key_tested", "id": request_id, **check(preset_id, key, factory)})

    thread = threading.Thread(target=work, name="relay-key-test", daemon=True)
    thread.start()
    return thread
