# SPDX-License-Identifier: AGPL-3.0-or-later
"""Relay Pro secrets stay in the keystore; only current, validated access enters the catalog."""
from __future__ import annotations

import hashlib
import logging
import threading
import time

from . import hosted, keystore

PRESET_ID = "relay-pro"
MODELS = ("relay-pro-high", "relay-pro-main", "relay-pro-flash")
REFRESH_SECONDS = 60
_lock = threading.RLock()
_operation_lock = threading.Lock()
_generation = 0
_state = {}
_pending = set()
_listener = None


def set_listener(callback):
    global _listener
    _listener = callback


def _notify():
    if _listener is not None:
        try:
            _listener()
        except Exception:
            # Access state is already committed; a failed UI push must not retry validation.
            logging.getLogger(__name__).warning("Relay Pro availability notification failed.")


def _fingerprint(code):
    return (hosted.base_url(), hashlib.sha256(code.encode()).hexdigest())


def code() -> str:
    return keystore.lookup(PRESET_ID)


def validate(value: str) -> list[str]:
    if not isinstance(value, str) or not value or len(value) > 128 or any(c.isspace() for c in value):
        raise hosted.HostedUnavailable("Enter a valid Relay Pro access code.", "pro_access_denied")
    if hosted.disabled():   # RELAY_HOSTED=off (#RCPF): say so, not "try again"
        raise hosted.HostedUnavailable(hosted.DISABLED.replace("Relay Free", "Relay Pro", 1))
    try:
        result = hosted.session().fetch_pro(value)
    except hosted.HostedUnavailable as exc:
        text = ("Relay Pro access was denied. Check or replace your code."
                if exc.code == "pro_access_denied" else "Relay Pro access could not be checked. Try again.")
        raise hosted.HostedUnavailable(text, exc.code) from None
    models = result.get("models")
    if (result.get("active") is not True or not isinstance(models, list) or not models
            or any(not isinstance(model, str) or model not in MODELS for model in models)
            or set(models) != set(MODELS)):
        raise hosted.HostedUnavailable("Relay Pro is not fully enabled on this gateway.", "pro_access_denied")
    return models


def invalidate(value: str | None = None):
    """Forget availability on removal or a live refusal; stale checks cannot restore it."""
    global _generation, _state
    with _lock:
        _generation += 1
        _state = {"fingerprint": _fingerprint(value) if value else None, "available": False,
                  "at": time.monotonic(), "access_note": "Relay Pro access is not active."}
    _notify()


def _record(value, models, generation, note):
    global _state
    with _lock:
        if generation != _generation or code() != value:
            return
        _state = {"fingerprint": _fingerprint(value), "available": bool(models),
                  "models": list(models), "at": time.monotonic(), "access_note": note}


def status() -> dict:
    """No network on the caller thread. Storage and authorization are distinct facts."""
    value = code()
    source = keystore.key_source(PRESET_ID) if value else ""
    fingerprint = _fingerprint(value)
    with _lock:
        current = _state.get("fingerprint") == fingerprint
        fresh = current and time.monotonic() - _state.get("at", 0) < REFRESH_SECONDS
        return {"has_stored_key": bool(value), "key_source": source,
                "available": bool(value and fresh and _state.get("available")),
                "access_note": (_state.get("access_note", "") if value and fresh else
                                "Checking Relay Pro access…" if value else "Enter your Relay Pro access code.")}


def start_refresh():
    value = code()
    if not value:
        return
    fingerprint = _fingerprint(value)
    with _lock:
        if (fingerprint in _pending or (_state.get("fingerprint") == fingerprint
                and time.monotonic() - _state.get("at", 0) < REFRESH_SECONDS)):
            return
        generation = _generation
        _pending.add(fingerprint)

    def work():
        try:
            models = validate(value)
            note = "Relay Pro access is active."
        except Exception:
            models, note = [], "Relay Pro access could not be validated. Check your code or try again."
        try:
            _record(value, models, generation, note)
        finally:
            with _lock:
                _pending.discard(fingerprint)
            _notify()
    threading.Thread(target=work, name="relay-pro-access", daemon=True).start()


def run(action: str, emit, request_id=None, value=""):
    """Worker key commands are serialized off its input thread and never generate model output."""
    def work():
        global _generation
        try:
            with _operation_lock:
                with _lock:
                    _generation += 1
                if action == "remove":
                    removed = keystore.remove(PRESET_ID)
                    invalidate()
                    event = {"event": "key_removed", "removed": removed}
                else:
                    candidate = value.strip() if action == "store" else code()
                    with _lock:
                        generation = _generation
                    models = validate(candidate)
                    if action == "store":
                        keystore.store(PRESET_ID, candidate)
                    _record(candidate, models, generation, "Relay Pro access is active.")
                    event = {"event": "key_stored"} if action == "store" else {
                        "event": "key_tested", "ok": True, "model": "relay-pro-main", "elapsed_ms": 0}
            emit({**event, "id": request_id, "preset": PRESET_ID})
        except Exception as exc:
            if isinstance(exc, hosted.HostedUnavailable):
                message = str(exc)  # validate() supplies only our own safe sentences
            elif isinstance(exc, keystore.KeystoreError):
                message = "Relay Pro code could not be saved or removed. Unlock your keyring and try again."
            else:
                message = "Relay Pro access could not be updated. Try again."
            if action != "store":
                invalidate(code())
            event = {"event": "key_tested", "ok": False,
                     "error": message} \
                if action == "test" else {"event": "error", "text": message}
            emit({**event, "id": request_id, "preset": PRESET_ID})
        finally:
            _notify()
    thread = threading.Thread(target=work, name="relay-pro-key", daemon=True)
    thread.start()
    return thread
