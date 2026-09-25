# SPDX-License-Identifier: AGPL-3.0-or-later
"""The keys modal's Test button: one minimal call per provider (protocol 13.8).

The call sends a two-word prompt with no tools and a tiny output budget, so it costs a handful of
tokens. It answers one question only: does this key reach this endpoint? The key itself is read from
the keystore inside the worker, is never echoed, and never appears in the reply or the error text —
provider error bodies can quote the submitted request, so only the HTTP status survives
(provider.ProviderError already strips the body).
"""
from __future__ import annotations

import shutil
import tempfile
import threading
import time

from . import customproviders, guest_accounts, guest_harness_provider, localmodels
from .guest_harness import HarnessError, HarnessNotAvailable
from .presets import PRESETS, apply_effort
from .provider import ChatProvider, ProviderConfig, ProviderError, ProviderTruncated, make_provider

SYSTEM = "Reply with the single word: ok"
USER = "ping"
# Reasoning models spend output tokens on thinking before the word, and several of them cannot turn
# thinking off at all, so a tight budget makes the answer come back truncated. 1024 is still a few
# tenths of a cent, and a truncated answer counts as a pass anyway (see ProviderTruncated below).
MAX_TOKENS = 1024
# The wall clock the transport's HTTP retries may spend on this call. The Test button shows a
# spinner and nothing else: it has no status line for "asking again in 60 s", and its thread is
# not cancellable, so a provider answering 429 with ``Retry-After: 60`` six times over left the
# button spinning for six minutes (review of #VMZP). Past this, the refusal is the answer.
TIMEOUT_S = 30
# A guest's test is a whole CLI start plus one model turn (protocol 29.3): Claude Code takes a
# few seconds to come up and codex's app-server about as long, so the budget is twice the cloud
# one. Past it the guest is closed and "timed out" is the answer.
GUEST_TIMEOUT_S = 60
GUEST_PROMPT = "Reply with the single word ok."
# provider.py raises ProviderTruncated when the model hit the output limit. For an ordinary turn that
# is a real failure; for the key test it is a pass, because the request was authenticated, routed and
# answered — the only thing it did not do is finish a sentence nobody reads. It is matched by type:
# matching the message read the wording of a sentence written for the user, and broke the moment that
# sentence was rewritten.


def _preset(preset_id: str):
    """A built-in preset, a saved model server on this machine (protocol 28) or a saved custom
    provider (28.6), in the same shape."""
    if preset_id in PRESETS:
        return PRESETS[preset_id]
    endpoint = localmodels.find(preset_id)
    if endpoint is not None:
        return endpoint.as_preset()
    entry = customproviders.find(preset_id)
    if entry is not None:
        return entry.as_preset()
    from . import key_accounts
    return key_accounts.as_preset(preset_id)            # a second plan subscription (#YC0T)


def _keyless(preset) -> bool:
    """No key to look up: a local server, Relay Free, or a custom provider on a loopback URL."""
    return preset.local or preset.hosted or localmodels.loopback_http(preset.base_url)


def _provider(preset_id: str, key: str) -> ChatProvider:
    preset = _preset(preset_id)
    if preset.local or localmodels.loopback_http(preset.base_url):
        # No key and no effort knob. The test still means what it says on the button: this server
        # is reachable and answers. It waits out a cold model load, which a probe does not.
        fields = localmodels.provider_fields(preset_id, preset.base_url, preset.model)
        return ChatProvider(ProviderConfig(preset.base_url, preset.model, "", dict(preset.extra),
                                           localmodels.clamp_max_tokens(MAX_TOKENS, fields), **fields))
    # Ask for the least thinking this provider allows: the test is about reachability, not quality.
    extra, _ = apply_effort(dict(preset.extra), preset.effort_style, "low")
    # Relay Free: one real call through the gateway, with a token rather than a key (protocol 13.9).
    return make_provider(ProviderConfig(preset.base_url, preset.model, key, extra, MAX_TOKENS,
                                        hosted=preset.hosted))


def check(preset_id: str, key: str, factory=_provider) -> dict:
    """Run the call. Returns the body of a ``key_tested`` event; never raises for provider trouble."""
    from . import sidecall
    preset = _preset(preset_id)
    started = time.monotonic()
    result = {"preset": preset_id, "model": preset.model}
    try:
        reply, _ = sidecall.call(factory(preset_id, key), SYSTEM, USER, retry_budget_s=TIMEOUT_S)
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


# ----- a guest (Claude Code / Codex as a provider, protocol 29.3) ------------------------------


def _one_line(text, limit: int = 120) -> str:
    """The first non-empty line of a guest's words, trimmed: a `key_tested` never carries more
    of a guest's output than that, whichever way the test went."""
    for line in str(text or "").splitlines():
        line = line.strip()
        if line:
            return line[:limit]
    return ""


def check_guest(guest_id: str, make_harness=None, login=None, timeout_s: float = GUEST_TIMEOUT_S,
                cwd: str | None = None) -> dict:
    """One minimal headless turn through the guest's harness adapter: the same `claude -p` /
    `codex app-server` a pane on that guest uses, started with no tools in an empty scratch
    directory, asked `GUEST_PROMPT`, and closed. Returns the body of a ``key_tested`` event and
    never raises for guest trouble.

    `make_harness(guest_id, probe=True)` and `login(guest_id, binary)` are the seams; the real ones
    are guest_harness_provider's. The login check comes first because it is free and precise —
    a signed-out `claude -p` otherwise dies at start with its stderr as the only clue — and its
    answer, like the turn's, is remembered for the guest's `presets` row (`note_login`).
    """
    ghp = guest_harness_provider
    make_harness = make_harness or ghp.make_harness
    login = login or ghp._read_login_status
    # `guest_id` may be a registered account's key, `claude:work` (#M8S2): the same test, run
    # under that account's directory, and its answer filed under that key.
    key = guest_id
    guest_id, account = guest_accounts.split_key(key)
    guest_id = guest_id or key
    name = guest_id
    preset_id = ghp.PRESET_PREFIX + key
    started = time.monotonic()
    result = {"preset": preset_id, "guest": guest_id, "model": ""}
    if account:
        result["account"] = account

    def finish(ok: bool, **fields) -> dict:
        result["ok"] = ok
        result.update(fields)
        result["elapsed_ms"] = int((time.monotonic() - started) * 1000)
        return result

    state = ghp.installations().get(guest_id) or {}
    if not state.get("installed"):
        return finish(False, error=f"{name} is not installed: no `{name}` on PATH")
    try:
        signed_in = (login(guest_id, state.get("binary") or name,
                           env=guest_accounts.environment(guest_id, account)) if account
                     else login(guest_id, state.get("binary") or name))
    except ValueError as exc:                           # the account is not registered any more
        return finish(False, error=str(exc))
    except Exception as exc:                            # a status command that would not run
        signed_in = None
        result["login_check"] = f"{type(exc).__name__}"
    if signed_in is False:
        ghp.note_login(key, False)
        hint = (f" (sign in with `{guest_accounts.login_command(guest_id, account)}`)"
                if account else "")
        return finish(False, error=f"{name} is not logged in: change login first{hint}")

    scratch = cwd or tempfile.mkdtemp(prefix="relay-keytest-")
    harness = None
    try:
        harness = (make_harness(guest_id, probe=True, account=account) if account
                   else make_harness(guest_id, probe=True))
        began = harness.start(cwd=scratch, permissions="deny")
        result["model"] = began.model or ""
        outcome: dict = {}
        cancel = threading.Event()

        def turn():
            try:
                outcome["result"] = harness.send(GUEST_PROMPT, emit=lambda event: None,
                                                 cancel=cancel)
            except BaseException as exc:                    # reported below, on the caller's thread
                outcome["error"] = exc

        worker = threading.Thread(target=turn, name="relay-key-test-turn", daemon=True)
        worker.start()
        worker.join(timeout_s)
        if worker.is_alive():
            cancel.set()
            result["model"] = harness.model or result["model"]   # what it said before stalling
            _close_quietly(harness)                        # EOF ends the blocked send()
            worker.join(2.0)
            return finish(False, error=f"{name} timed out after {int(timeout_s)} s")
        if "error" in outcome:
            raise outcome["error"]
        turn_result = outcome["result"]
        result["model"] = harness.model or result["model"]
        if turn_result.stop_reason != "end":
            return finish(False, error=f"{name} ended the turn: {turn_result.stop_reason}")
        ghp.note_login(key, True)
        text = _one_line(turn_result.text)
        return finish(True, text=text, reply_chars=len(turn_result.text or ""))
    except HarnessNotAvailable as exc:
        return finish(False, error=_one_line(exc) or f"{name} could not be started")
    except HarnessError as exc:
        words = _one_line(exc) or f"{name} ended the turn with an error"
        if "not logged in" in words.lower() or "log in" in words.lower():
            ghp.note_login(key, False)
        return finish(False, error=words)
    except Exception as exc:                               # the adapter itself broke
        return finish(False, error=f"{name} test failed ({type(exc).__name__}).")
    finally:
        if harness is not None:
            _close_quietly(harness)
        if cwd is None:
            shutil.rmtree(scratch, ignore_errors=True)


def _close_quietly(harness) -> None:
    try:
        harness.close()
    except Exception:
        pass


def _run_guest(guest_id: str, emit, request_id, **seams) -> threading.Thread | None:
    """The guest half of `run()`: not installed is answered at once; anything else is a turn on a
    thread, so the worker keeps answering while the CLI comes up."""
    ghp = guest_harness_provider
    preset_id = ghp.PRESET_PREFIX + guest_id
    family = guest_accounts.split_key(guest_id)[0] or guest_id
    if not (ghp.installations().get(family) or {}).get("installed"):
        emit({"event": "key_tested", "id": request_id, "preset": preset_id, "guest": family,
              "ok": False, "model": "", "elapsed_ms": 0,
              "error": f"{family} is not installed: no `{family}` on PATH"})
        return None

    def work():
        emit({"event": "key_tested", "id": request_id, **check_guest(guest_id, **seams)})

    thread = threading.Thread(target=work, name="relay-key-test", daemon=True)
    thread.start()
    return thread


def run(preset_id: str, emit, request_id=None, lookup=None, factory=_provider, **guest_seams) \
        -> threading.Thread:
    """Test a stored key on a background thread and emit exactly one ``key_tested`` event.

    A `guest:<id>` preset (Claude Code, Codex) has no key: its test is one headless turn through
    the guest's own harness (`check_guest`), and `guest_seams` are that function's test hooks.
    """
    from . import keystore
    guest_id = guest_harness_provider.preset_key(preset_id)
    if guest_id is not None:
        return _run_guest(guest_id, emit, request_id, **guest_seams)
    preset = _preset(preset_id) if isinstance(preset_id, str) else None
    if preset is None:
        raise ValueError("Unknown provider preset.")
    keyless = _keyless(preset)
    key = "" if keyless else (lookup or keystore.lookup)(preset_id)
    if not key and not keyless:
        emit({"event": "key_tested", "id": request_id, "preset": preset_id, "ok": False,
              "model": preset.model, "elapsed_ms": 0,
              "error": "No key is stored for this provider."})
        return None

    def work():
        emit({"event": "key_tested", "id": request_id, **check(preset_id, key, factory)})

    thread = threading.Thread(target=work, name="relay-key-test", daemon=True)
    thread.start()
    return thread
