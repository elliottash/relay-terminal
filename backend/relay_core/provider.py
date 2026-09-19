# SPDX-License-Identifier: GPL-3.0-or-later
"""OpenAI-compatible chat-completions transport with streamed tool-call assembly."""
from __future__ import annotations

import base64
import contextlib
import email.utils
import json
import math
import os
import random
import socket
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from typing import Callable

from . import logs

_log = logs.get("provider")


def _host(base_url: str) -> str:
    """Provider host for the log. The path, query and key never go near the log file."""
    try:
        return urllib.parse.urlsplit(base_url).hostname or ""
    except ValueError:
        return ""

MAX_EVENT = 2 * 1024 * 1024
MAX_RESPONSE = 8 * 1024 * 1024

# The idle deadline: how long the model may send nothing usable before the turn is ended. It covers
# the response headers *and* every streamed chunk after them.
#
# Why a raw socket timeout is not enough (measured 2026-09-17): `urlopen(timeout=N)` does apply to
# each read of the response, but *any* byte resets it, and an SSE stream of keepalive comments or
# empty deltas is all bytes and no answer. A stream that pinged every 0.2 s read for 8 s against a
# 2 s timeout without ever raising. That is how a worker held an ESTABLISHED connection to the
# provider for 12 minutes while the pane showed only "thinking". The deadline therefore runs from
# the last *usable* chunk and is enforced by a watchdog that closes the response.
CONNECT_TIMEOUT = 30.0           # floor for DNS/TLS/headers; the deadline below raises it when larger
DEFAULT_STALL_TIMEOUT = 60.0     # reasoning models go quiet for a long time between chunks
MIN_STALL_TIMEOUT = 1.0
MAX_STALL_TIMEOUT = 1800.0
# The output budget one model call may ask for. Reasoning counts towards it on every provider that
# streams reasoning, so the ceiling is also the ceiling on how long a model may think in one step.
# The ceiling is what the most generous model Relay ships documents (131,072 = 128K); what each one
# actually gets is its own documented cap, from presets.max_output. 0 means "automatic": that cap,
# which is what a new install asks for.
MIN_OUTPUT_TOKENS = 256
MAX_OUTPUT_TOKENS = 131072
AUTOMATIC_OUTPUT_TOKENS = 0
WATCHDOG_TICK = 0.5
# Environment override for the deadline, kept from the 2026-09-17 stopgap that widened the raw
# socket timeout. It wins over the agent option, so a pane that needs more room needs no settings
# change; unset, the option (default 60 s) decides. One code path, one deadline.
ENV_TIMEOUT = "RELAY_PROVIDER_TIMEOUT"
ENV_MIN, ENV_MAX = 5.0, 900.0

# The only hosts plain HTTP may go to, and so the only hosts a model server needs no key on
# (localmodels.py). One definition: the transport's guard and the keyless rule cannot drift apart.
LOCAL_HOSTS = frozenset({"localhost", "127.0.0.1", "::1"})


def loopback_http(base_url) -> bool:
    """Whether a base URL is plain HTTP to this machine: the one shape that is a local model server.

    Deliberately not "any URL without a key": a mistyped https endpoint must keep failing with
    "No stored key", never go out unauthenticated.
    """
    if not isinstance(base_url, str):
        return False
    try:
        url = urllib.parse.urlsplit(base_url.strip())
        return url.scheme == "http" and url.hostname in LOCAL_HOSTS
    except ValueError:
        return False


def _finite(raw) -> float | None:
    """A header's number, or None when it is not a finite one: ``soon``, ``nan``, ``inf``.

    ``float()`` accepts "nan" and "inf" happily, and both survived the clamp below as themselves:
    ``min(max(nan, 0.0), 60.0)`` is ``nan``. A nan wait is a hot loop of zero-delay retries and a
    status line reading "asking again in nan s" (review of #VMZP), so a non-finite header is no
    hint at all and the backoff decides.
    """
    try:
        value = float(str(raw).strip())
    except (TypeError, ValueError):
        return None
    return value if math.isfinite(value) else None


def env_stall_timeout() -> float | None:
    """The RELAY_PROVIDER_TIMEOUT override in seconds, clamped to 5-900, or None when unset/invalid."""
    raw = os.environ.get(ENV_TIMEOUT, "").strip()
    if not raw:
        return None
    try:
        return min(max(float(raw), ENV_MIN), ENV_MAX)
    except ValueError:
        return None


def validate_stall_timeout(value) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"stall_timeout_s must be a number from {MIN_STALL_TIMEOUT:g} to {MAX_STALL_TIMEOUT:g}.")
    value = float(value)
    if not MIN_STALL_TIMEOUT <= value <= MAX_STALL_TIMEOUT:
        raise ValueError(f"stall_timeout_s must be a number from {MIN_STALL_TIMEOUT:g} to {MAX_STALL_TIMEOUT:g}.")
    return value


def _socket_of(response):
    """The raw socket behind an HTTPResponse, or None (a BytesIO in tests has none)."""
    raw = getattr(getattr(response, "fp", None), "raw", None)
    return getattr(raw, "_sock", None)


def hard_close(response) -> None:
    """Close a response so its socket cannot outlive the turn.

    `close()` alone does not unblock a read another thread is already waiting in, and it leaves the
    peer with an ESTABLISHED connection until it notices; `shutdown()` ends both directions at once.
    On 2026-09-17 a worker still held an ESTABLISHED connection 12 minutes after its turn, which is
    what this exists to prevent.
    """
    def close_quietly():
        try:
            response.close()
        except (OSError, ValueError, AttributeError):
            pass

    sock = _socket_of(response)
    if sock is None:
        # Nothing to shut down, and close() waits for the buffer lock a blocked reader holds, so it
        # must not run on the caller's thread: cancel() is called from the protocol loop.
        threading.Thread(target=close_quietly, name="relay-provider-close", daemon=True).start()
        return
    try:
        sock.shutdown(socket.SHUT_RDWR)
    except (OSError, ValueError, AttributeError):
        pass
    # The shutdown just made any blocked read return, so this cannot wait on the network.
    close_quietly()


def response_closed(response) -> bool:
    """Whether a response holds no socket any more. Used by the per-turn socket-hygiene check."""
    if response is None or getattr(response, "fp", None) is None:
        return True
    sock = _socket_of(response)
    if sock is not None:
        try:
            if sock.fileno() != -1:
                return False
        except (OSError, ValueError):
            return True
    try:
        return bool(response.isclosed())
    except (OSError, ValueError, AttributeError):
        return True

def _reasoning_text(part: dict) -> str:
    """Displayable reasoning in a streamed delta or a complete message.

    Kimi and GLM use reasoning_content, OpenRouter reasoning, and OpenRouter also sends
    reasoning_details (text or summary items), usually duplicating reasoning; details are only used
    when no plain field is present.
    """
    if not isinstance(part, dict):
        return ""
    for key in ("reasoning_content", "reasoning"):
        if isinstance(part.get(key), str) and part[key]:
            return part[key]
    details = part.get("reasoning_details")
    if isinstance(details, list):
        texts = []
        for item in details:
            if isinstance(item, dict):
                value = item.get("text") if isinstance(item.get("text"), str) else item.get("summary")
                if isinstance(value, str) and value:
                    texts.append(value)
        return "".join(texts)
    return ""


# --- multimodal content parts (issue EM1E) ------------------------------------------------------
# OpenAI-compatible chat completions carry an image as a content part on a user message:
#   {"role": "user", "content": [{"type": "text", "text": ...},
#                                {"type": "image_url", "image_url": {"url": "data:image/png;base64,..."}}]}
# Relay always inlines the bytes as a data URL: no image of the user's ever leaves the machine for
# anywhere but the configured provider, and no third party has to be able to fetch a URL.
#
# The caps are local, not the provider's. A base64 data URL is 4/3 of the file, the whole request
# has to stay under MAX_RESPONSE (8 MiB), and the conversation has to fit beside it, so one image
# may be 3 MiB and a turn 6 MiB of raw image bytes.
MAX_IMAGE_BYTES = 3 * 1024 * 1024
MAX_IMAGE_TOTAL = 6 * 1024 * 1024
MAX_IMAGES_PER_TURN = 4
IMAGE_TYPES = ("image/png", "image/jpeg", "image/webp", "image/gif")
IMAGE_DETAIL = "auto"


def data_url(media_type: str, raw: bytes) -> str:
    """`data:<type>;base64,<data>` for one image. Rejects an unsupported type or an oversized file."""
    if media_type not in IMAGE_TYPES:
        raise ValueError(f"Unsupported image type {media_type!r}; use PNG, JPEG, WebP or GIF.")
    if len(raw) > MAX_IMAGE_BYTES:
        raise ValueError(f"Image is larger than the {MAX_IMAGE_BYTES // (1024 * 1024)} MiB limit for one image.")
    return f"data:{media_type};base64," + base64.b64encode(raw).decode("ascii")


def image_part(media_type: str, raw: bytes, detail: str = IMAGE_DETAIL) -> dict:
    return {"type": "image_url", "image_url": {"url": data_url(media_type, raw), "detail": detail}}


def text_part(text: str) -> dict:
    return {"type": "text", "text": text}


def content_parts(text: str, images: list[dict]) -> list[dict]:
    """The `content` list for a user message that carries images.

    ``images`` are ``{media_type, raw}`` in the order the user attached them; the text always comes
    first, because a model reads the instruction before the picture.
    """
    if len(images) > MAX_IMAGES_PER_TURN:
        raise ValueError(f"At most {MAX_IMAGES_PER_TURN} images per turn.")
    total = sum(len(image["raw"]) for image in images)
    if total > MAX_IMAGE_TOTAL:
        raise ValueError(f"Images in one turn may total at most {MAX_IMAGE_TOTAL // (1024 * 1024)} MiB.")
    return [text_part(text)] + [image_part(image["media_type"], image["raw"]) for image in images]


def message_images(message) -> list[dict]:
    """The image parts of one message, or an empty list. Never raises on odd content."""
    content = message.get("content") if isinstance(message, dict) else None
    if not isinstance(content, list):
        return []
    return [part for part in content
            if isinstance(part, dict) and part.get("type") == "image_url" and isinstance(part.get("image_url"), dict)]


def has_images(messages: list[dict]) -> bool:
    return any(message_images(message) for message in messages)


def _arguments_as_object(message: dict) -> dict | None:
    """A copy of one message with each tool call's ``arguments`` as the object it spells, or None
    when nothing needs changing.

    OpenAI's shape carries ``arguments`` as a JSON string, and that is what Relay stores and replays.
    Some chat templates cannot read it: Meta's ATEM template (Muse Glimmer) served by llama.cpp
    ``--jinja`` refuses a string, because the HF jinja sandbox has nothing to parse one with, so the
    second turn of every tool conversation fails. Only a mapping is substituted — that is what such a
    template iterates — and a string that is not JSON is left exactly as it is.
    """
    calls = message.get("tool_calls")
    if not isinstance(calls, list):
        return None
    out, changed = [], False
    for call in calls:
        func = call.get("function") if isinstance(call, dict) else None
        arguments = func.get("arguments") if isinstance(func, dict) else None
        if isinstance(arguments, str):
            try:
                parsed = json.loads(arguments)
            except ValueError:
                parsed = None
            if isinstance(parsed, dict):
                call = {**call, "function": {**func, "arguments": parsed}}
                changed = True
        out.append(call)
    return {**message, "tool_calls": out} if changed else None


def wire_messages(messages: list[dict], *, tool_arguments_as_object: bool = False) -> list[dict]:
    """The outgoing copy of the conversation: the one place a message is shaped for the wire.

    Relay's own bookkeeping keys (relay_kind, relay_request) are dropped, and for a local endpoint
    that asked for it, ``tool_calls[].function.arguments`` travels as an object instead of a JSON
    string. The stored messages are never touched: what changes is copied first.
    """
    out, changed = [], False
    for message in messages:
        if isinstance(message, dict):
            if any(k.startswith("relay_") for k in message):
                message = {k: v for k, v in message.items() if not k.startswith("relay_")}
                changed = True
            if tool_arguments_as_object:
                objects = _arguments_as_object(message)
                if objects is not None:
                    message, changed = objects, True
        out.append(message)
    return out if changed else messages


class ProviderError(RuntimeError):
    """A model call failed. The message is for the user and never carries a key or a body.

    ``code`` and ``resets_at`` are set only by Relay's own hosted service (``HostedChatProvider``):
    one of ``hosted.ERROR_CODES`` and the unix time a quota refusal lifts, so the pane can word the
    failure and offer a key of the user's own. Every other provider leaves them empty.
    """
    def __init__(self, text: str = "", code: str = "", resets_at: int | None = None):
        super().__init__(text)
        self.code = code
        self.resets_at = resets_at


def repair_tool_calls(calls) -> list:
    """The tool-call envelope quirks local servers are known for, made conformant.

    llama.cpp has returned ``arguments`` as a JSON object instead of a string (its issue 20198),
    Ollama's /v1 has omitted ``id`` and ``type``, and ids have repeated across calls. A hosted
    provider doing any of this is still an error (``_normalize``); only a local endpoint is repaired.
    Anything that is not one of these stays as it is, and ``_normalize`` refuses it as before.
    """
    if not isinstance(calls, list):
        return calls
    repaired, seen = [], set()
    for index, call in enumerate(calls):
        if not isinstance(call, dict):
            repaired.append(call)
            continue
        call = dict(call)
        func = dict(call["function"]) if isinstance(call.get("function"), dict) else call.get("function")
        if isinstance(func, dict):
            arguments = func.get("arguments")
            if isinstance(arguments, (dict, list)):
                func["arguments"] = json.dumps(arguments, ensure_ascii=False)
            elif arguments is None or arguments == "":
                func["arguments"] = "{}"
            call["function"] = func
        if call.get("type") is None:
            call["type"] = "function"
        call_id = call.get("id")
        if not isinstance(call_id, str) or not call_id or call_id in seen:
            call_id = f"call_{index}"
            while call_id in seen:
                call_id += "_"
        call["id"] = call_id
        seen.add(call_id)
        repaired.append(call)
    return repaired

class ProviderTruncated(ProviderError):
    """The response stopped at the output limit (``length``) or was filtered (``content_filter``).

    ``produced`` says whether any of it reached the user (answer text or a tool-call fragment); as
    with a stall, a response that produced nothing can be asked for again without showing the user
    the same text twice. Reasoning alone does not count as produced, and a reasoning model that
    spends its whole budget thinking is exactly the case this carries: the step cost four minutes
    and delivered nothing.

    ``partial`` is the answer text that did arrive, when it can be kept — a message with no tool
    calls. A truncated tool call is never kept: its arguments are cut-off JSON, and an assistant
    message carrying tool calls without their results is not a conversation a provider will accept.
    """
    def __init__(self, reason: str, max_tokens: int, produced: bool = False,
                 partial: dict | None = None, model_cap: int | None = None):
        if reason == "content_filter":
            text = ("The provider filtered this response; partial tools were not executed. "
                    "Rephrase the request, or send it to another model.")
        else:
            # Telling someone to raise a limit they cannot raise is the thing this message got
            # wrong before: it is spent either when Relay will take no larger number, or when the
            # model itself documents no larger number (Gemini 3.1 Pro stops at 65,536).
            spent = max_tokens >= MAX_OUTPUT_TOKENS or (model_cap is not None and max_tokens >= model_cap)
            text = (f"The model used its whole {max_tokens}-token output budget on one step without "
                    "finishing, so nothing of it was used; reasoning counts towards that budget. "
                    + ("That is as much as this model gives for one call: lower the effort in "
                       "Options › Models, or ask for a smaller step."
                       if spent else
                       "Raise the output token limit in Options › Models, or ask for a smaller step."))
        super().__init__(text)
        self.reason = reason
        self.max_tokens = max_tokens
        self.model_cap = model_cap
        self.produced = produced
        self.partial = partial


class ProviderStalled(ProviderError):
    """Nothing usable arrived from the provider for ``seconds``; the socket was closed.

    ``produced`` says whether the stalled response had already started an answer (content or a
    tool-call fragment). The agent retries a turn once only when it has not: that text is already on
    the user's screen, and no tool call can be in flight (a stall can only happen while waiting for
    the model, when every earlier tool call already has its result).

    ``stage`` is "stream" (headers arrived, then nothing usable) or "connect" (no response at all).
    """
    def __init__(self, seconds: float, produced: bool = False, stage: str = "stream"):
        what = ("the model sent nothing for" if stage == "stream"
                else "the provider did not answer within")
        super().__init__(f"Provider stalled: {what} {seconds:g} s.")
        self.seconds = seconds
        self.produced = produced
        self.stage = stage

class Cancelled(RuntimeError):
    pass

class NoRedirect(urllib.request.HTTPRedirectHandler):
    """Never forward an Authorization header to a redirected endpoint."""
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # urllib closes ``fp`` only after this returns; raising here would leak the redirect
        # response and its socket, so close it first.
        hard_close(fp)
        raise ProviderError("Provider redirected the request. Check the configured base URL.")

@dataclass
class ProviderConfig:
    base_url: str
    model: str
    api_key: str = field(repr=False)
    extra: dict = field(default_factory=dict)
    max_tokens: int = AUTOMATIC_OUTPUT_TOKENS   # 0: ask for what this model documents
    # A model server on this machine (localmodels.py). Everything the transport does differently
    # for one is behind this flag, so a hosted provider's request and its failures are unchanged.
    local: bool = False
    first_token_timeout: float | None = None   # a cold load plus a long prefill is silent for minutes
    parallel_tool_calls: bool = False
    tool_text_recovery: bool = False           # off unless the endpoint asks (localtext.py)
    # Send tool-call arguments as an object, not the JSON string OpenAI's shape carries: a chat
    # template that cannot parse a string (Meta's ATEM on llama.cpp --jinja) fails on the second
    # turn of every tool conversation otherwise. The outgoing copy only (``wire_messages``).
    tool_arguments_as_object: bool = False
    context_window: int | None = None          # the served window, for the overflow message only
    # Relay's own hosted service (hosted.py, the relay-free preset): no key is stored, because the
    # transport takes a short-lived bearer token before each call. Only make_provider reads it.
    hosted: bool = False

    def __post_init__(self) -> None:
        """Settle ``max_tokens`` at construction, so everything downstream — the request, the
        compaction reserve, the model-switch ceiling — reads one number.

        0 means automatic: the model's own documented output cap (``presets.max_output``). A number
        the user pinned is kept, but never above that cap, because a request over it is refused
        rather than trimmed. An endpoint Relay cannot name keeps whatever it was given: the user
        typed that base URL and knows what it takes.
        """
        from .presets import match_preset, resolve_max_tokens
        preset = None if self.local else match_preset(self.base_url, self.model)
        self.max_tokens = resolve_max_tokens(self.max_tokens, preset)
        if self.local and self.context_window:
            # A server on this machine promises the whole window to the reply otherwise; the quarter
            # is localmodels.clamp_max_tokens's rule, applied here too so no path can miss it.
            self.max_tokens = max(MIN_OUTPUT_TOKENS, min(self.max_tokens, self.context_window // 4))

    def validate(self) -> None:
        url = urllib.parse.urlsplit(self.base_url)
        if url.scheme not in {"https", "http"} or not url.hostname or url.username or url.password or url.query or url.fragment:
            raise ValueError("Base URL must be an HTTPS URL without credentials, query, or fragment.")
        if url.scheme == "http" and url.hostname not in LOCAL_HOSTS:
            raise ValueError("Unencrypted HTTP is only allowed for a loopback/local model server.")
        if not self.model.strip():
            raise ValueError("A model ID is required.")
        if not self.api_key.strip() and url.scheme == "https" and not self.hosted:
            raise ValueError("An API key is required for this remote provider.")
        if not isinstance(self.extra, dict):
            raise ValueError("Extra parameters must be a JSON object.")
        allowed = {"thinking", "reasoning", "reasoning_effort", "temperature", "top_p"}
        if set(self.extra) - allowed:
            raise ValueError("Extra parameters may only contain thinking, reasoning, reasoning_effort, temperature, and top_p.")
        if not MIN_OUTPUT_TOKENS <= self.max_tokens <= MAX_OUTPUT_TOKENS:
            raise ValueError(f"Output token limit must be between {MIN_OUTPUT_TOKENS} and {MAX_OUTPUT_TOKENS}.")
        if self.local and not loopback_http(self.base_url):
            raise ValueError("Only plain HTTP to a loopback host is a local model server.")
        if self.hosted and self.local:
            raise ValueError("A provider is Relay's hosted service or a local model server, not both.")
        if self.tool_arguments_as_object and not self.local:
            raise ValueError("tool_arguments_as_object is only for a local model server: a hosted "
                             "provider takes tool-call arguments as a JSON string.")
        if self.first_token_timeout is not None:
            validate_stall_timeout(self.first_token_timeout)

class ChatProvider:
    def __init__(self, config: ProviderConfig, stall_timeout: float = DEFAULT_STALL_TIMEOUT):
        config.validate()
        self.config = config
        self._stall_timeout = validate_stall_timeout(stall_timeout)
        self._response = None
        # The last response this provider opened, kept (closed) so the socket-hygiene check after a
        # turn can prove it was closed. It holds no file descriptor once closed.
        self._last_response = None
        self._stalled = False
        self._produced = False
        self._progress = 0.0
        self._streaming = False     # a usable chunk has arrived: the idle deadline applies from here
        self._retry_budget = None   # an explicit wall-clock cap on the retry loop, else derived
        self._retry_origin = None   # monotonic start of the logical call the budget is measured from
        self._retries_used = 0      # retries already spent by this logical call
        self._lock = threading.Lock()

    @property
    def first_token_timeout(self) -> float:
        """How long the first usable chunk may take. The idle deadline for a hosted provider; for a
        local server the longer budget of its endpoint, because loading the weights and reading a
        long prompt produce no bytes at all. opencode, Codex and Qwen Code all allow 300 s here."""
        if self.config.local and self.config.first_token_timeout:
            return max(self.stall_timeout, float(self.config.first_token_timeout))
        return self.stall_timeout

    @property
    def deadline(self) -> float:
        return self.stall_timeout if self._streaming else self.first_token_timeout

    @property
    def stall_timeout(self) -> float:
        """The idle deadline in force: the environment override when set, else the agent option."""
        override = env_stall_timeout()
        return override if override is not None else self._stall_timeout

    @property
    def open_timeout(self) -> float:
        """Budget for DNS, TLS and the response headers.

        Some providers withhold the 200 until the first token is ready, so this must not be shorter
        than the idle deadline: "Provider connection failed (TimeoutError)" before any output was
        exactly that case (owner reports, 2026-09-17).
        """
        return max(CONNECT_TIMEOUT, self.first_token_timeout)

    @property
    def retry_budget(self) -> float:
        """Wall clock the whole retry loop of one call may take, waits and refusals together.

        The count alone did not bound anything: six retries each honouring a ``Retry-After: 60``
        is six minutes, and every retry calls ``_note_progress``, so the stall watchdog never fires
        either. A call that has been refused for twice as long as the first token was ever allowed
        to take is not going to be answered by asking a seventh time, so it fails with the
        provider's own status instead of holding the pane, the Test button or a side call.
        """
        if self._retry_budget is not None:
            return self._retry_budget
        return self.first_token_timeout * self.HTTP_RETRY_BUDGET_FACTOR

    @contextlib.contextmanager
    def limit_retry_budget(self, seconds: float):
        """Narrow the retry budget for the calls made inside the block; never widen it.

        A side call (a title, a recap, compaction, route_assist) and the keys modal's Test button
        have no streamed answer to protect and, in the Test button's case, nowhere to show a wait:
        they take a small budget so a provider answering "not now" cannot park them for minutes.
        """
        previous = self._retry_budget
        self._retry_budget = float(seconds) if previous is None else min(previous, float(seconds))
        try:
            yield
        finally:
            self._retry_budget = previous

    def _begin_retries(self) -> bool:
        """Start the retry count and clock for one logical call; True when this frame owns them.

        A transport that makes more than one HTTP call for the same request — the hosted one, which
        refreshes its token after a 401 — calls this around the lot, so the second call continues
        the first's count and clock instead of starting six fresh retries with a fresh budget.
        """
        if self._retry_origin is not None:
            return False
        self._retry_origin = time.monotonic()
        self._retries_used = 0
        return True

    def _end_retries(self, owned: bool) -> None:
        if owned:
            self._retry_origin = None

    def set_stall_timeout(self, seconds) -> float:
        self._stall_timeout = validate_stall_timeout(seconds)
        return self.stall_timeout

    def cancel(self) -> None:
        """Close the in-flight response now. Deterministic, not best effort.

        ``shutdown()`` cannot block, so this runs on the caller's thread: the protocol loop is never
        held up and, unlike the old detached closer, the socket is gone by the time cancel returns.
        """
        with self._lock:
            # Capture this response now: a late cancel must never close a newer turn.
            response = self._response
        if response is not None:
            hard_close(response)

    def response_open(self) -> bool:
        """Whether this provider still holds an open HTTP response.

        False after every end state of a turn (done, cancelled, failed, stalled). ``Agent`` checks
        it when a turn ends and logs a warning if it is ever True (issue SQAM, socket hygiene).
        """
        with self._lock:
            response, last = self._response, self._last_response
        if response is not None and not response_closed(response):
            return True
        return last is not None and not response_closed(last)

    # ----- error text ----------------------------------------------------------------
    def http_message(self, code: int) -> str:
        """One line for an HTTP failure, naming the endpoint that produced it.

        The response body is never included: providers quote the request they were sent, key
        material and prompts with it. Only the status, the model and the host survive.

        401/403 gets its own sentence. "Provider HTTP 401." on its own reads as "the agent broke",
        and the user cannot tell which of their providers refused, nor that the answer is a key
        rather than a retry - which is exactly how a Switchboard configured against the wrong
        endpoint looked (owner report, 2026-09-18).
        """
        from .presets import match_preset
        base_url = self.config.base_url
        host = urllib.parse.urlsplit(base_url).hostname or base_url
        preset = match_preset(base_url, self.config.model)
        where = f"{self.config.model} at {host}" + (f" ({preset.label})" if preset else "")
        if code in (401, 403):
            return (f"Provider HTTP {code}: the API key was rejected for {where}. "
                    "The stored key is missing, wrong, or belongs to a different endpoint of the "
                    "same provider - open Options › Models › API keys… to check it.")
        return (f"Provider HTTP {code} for {where}. "
                "Check endpoint, model access, key, quota, and parameters.")

    # ----- idle deadline -------------------------------------------------------------
    def _note_progress(self, usable: bool = False) -> None:
        self._progress = time.monotonic()
        if usable:
            self._streaming = True

    def _watch_for_stall(self, response, cancel: threading.Event) -> threading.Event:
        """Close ``response`` when nothing usable has arrived for ``stall_timeout`` seconds.

        The socket timeout is not enough on its own: a provider that drips SSE keepalive comments or
        empty deltas resets it forever, so a dead turn stays "thinking". The deadline therefore runs
        from the last *usable* chunk (content, reasoning, a tool-call fragment, usage or [DONE]),
        not from the last byte.
        """
        finished = threading.Event()
        tick = max(0.05, min(WATCHDOG_TICK, self.stall_timeout / 4))

        def watch():
            while not finished.wait(tick):
                if cancel.is_set():
                    return
                if time.monotonic() - self._progress >= self.deadline:
                    self._stalled = True
                    hard_close(response)
                    return

        threading.Thread(target=watch, name="relay-provider-stall", daemon=True).start()
        return finished

    def _maybe_stalled(self, cancel: threading.Event) -> None:
        if self._stalled and not cancel.is_set():
            raise ProviderStalled(self.deadline, self._produced) from None

    def complete(self, messages: list[dict], tools: list[dict],
                 emit: Callable[[dict], None], cancel: threading.Event) -> dict:
        if cancel.is_set():
            raise Cancelled("Stopped.")
        started = time.monotonic()
        owns_retries = self._begin_retries()
        self._stalled = False
        self._produced = False
        self._streaming = False
        self._note_progress()
        payload = {"model": self.config.model,
                   "messages": wire_messages(messages, tool_arguments_as_object=(
                       self.config.local and self.config.tool_arguments_as_object)),
                   "stream": True, "max_tokens": self.config.max_tokens, **self.config.extra}
        if tools:
            # Side calls (summaries, recaps, suggestions) send no tools; some APIs reject "tools": [].
            payload["tools"] = tools
        if self.config.local or self.config.hosted:
            # llama.cpp and Ollama stream no usage unless asked, and without it the context tracker
            # estimates at four characters a token: tolerable at 1M, not at 32K. Relay's gateway
            # settles its quota from the same usage chunk, so it is asked for there too.
            payload["stream_options"] = {"include_usage": True}
        if self.config.local:
            if tools and not self.config.parallel_tool_calls:
                payload["parallel_tool_calls"] = False
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        if len(data) > MAX_RESPONSE:
            if has_images(messages):
                raise ProviderError("This turn is too large to send with its images. Attach a smaller image, "
                                    "or fewer of them.")
            raise ProviderError("Conversation exceeds the local request size limit. Start a new conversation.")
        headers = {"Content-Type": "application/json", "Accept": "text/event-stream", "User-Agent": "Relay/0.1"}
        if self.config.api_key:
            headers["Authorization"] = "Bearer " + self.config.api_key
        request = urllib.request.Request(self.config.base_url.rstrip("/") + "/chat/completions",
                                         data=data, headers=headers, method="POST")
        opener = urllib.request.build_opener(NoRedirect())
        watchdog = None
        try:
            # DNS, TLS and the headers; every streamed chunk after them is covered by the watchdog.
            response = self._open(opener, request, emit, cancel, started)
            with self._lock:
                self._response = response
                self._last_response = response
            self._note_progress()
            # Socket timeout as a backstop under the watchdog, so a byte-silent stream and a
            # keepalive-only stream end the same way, with the watchdog deciding first.
            sock = _socket_of(response)
            if sock is not None:
                try:
                    sock.settimeout(max(self.stall_timeout, self.first_token_timeout) * 2 + WATCHDOG_TICK)
                except (OSError, ValueError):
                    pass
            watchdog = self._watch_for_stall(response, cancel)
            with response:
                content_type = response.headers.get("Content-Type", "")
                if "application/json" in content_type:
                    raw = response.read(MAX_RESPONSE + 1)
                    if len(raw) > MAX_RESPONSE:
                        raise ProviderError("Provider response is too large.")
                    obj = json.loads(raw)
                    choices = obj.get("choices", [])
                    if not choices:
                        raise ProviderError("Provider returned no choices.")
                    if choices[0].get("finish_reason") in {"length", "content_filter"}:
                        raise ProviderError("Provider stopped before completing its response; no partial tools were executed.")
                    if isinstance(obj.get("usage"), dict):
                        emit({"event": "usage", "usage": obj["usage"]})
                    message = choices[0].get("message", {})
                    if self.config.local:
                        message = self._tidy_local(message, tools, choices[0].get("finish_reason"))
                    thinking = _reasoning_text(message)
                    if thinking:
                        emit({"event": "thinking_delta", "text": thinking})
                        emit({"event": "thinking_done", "elapsed_ms": 0, "chars": len(thinking)})
                    if message.get("content"):
                        emit({"event": "delta", "text": message["content"]})
                    return self._normalize(message)
                return self._stream(response, emit, cancel, started, tools)
        except urllib.error.HTTPError as exc:
            # What the failure reads like is decided while the body is still readable (a local
            # server's 400 says the prompt no longer fits; Relay's gateway says which quota).
            error = self._http_error(exc)
            # The error carries the response, so it also carries the socket: close it here rather
            # than leaving it to the garbage collector.
            if getattr(exc, "fp", None) is not None:
                hard_close(exc.fp)
            # Providers can echo submitted secrets/prompts in error bodies. Do not log them.
            raise error from None
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            # The watchdog's hard close surfaces here; report the stall, not a generic failure.
            self._maybe_stalled(cancel)
            if cancel.is_set():
                raise Cancelled("Stopped.") from None
            if isinstance(exc, TimeoutError) or isinstance(getattr(exc, "reason", None), TimeoutError):
                # Either the socket-timeout backstop fired before the watchdog tick, or no response
                # arrived at all. Both are a stall the agent may retry, not a broken base URL.
                streaming = watchdog is not None
                raise ProviderStalled(self.deadline if streaming else self.open_timeout,
                                      self._produced, "stream" if streaming else "connect") from None
            if self.config.local:
                # Nothing is listening: say what would be, the way Codex does for Ollama.
                from .localmodels import start_hint
                raise ProviderError(start_hint(self.config.base_url)) from None
            raise ProviderError(f"Provider connection failed ({type(exc).__name__}). Check connectivity and the base URL.") from None
        except AttributeError:
            # cancel() closes the response from another thread; http.client then reads from fp=None.
            self._maybe_stalled(cancel)
            if cancel.is_set():
                raise Cancelled("Stopped.") from None
            raise
        except (json.JSONDecodeError, UnicodeError, KeyError, TypeError, ValueError) as exc:
            self._maybe_stalled(cancel)
            if cancel.is_set():
                raise Cancelled("Stopped.") from None
            raise ProviderError(f"Malformed provider response ({type(exc).__name__}).") from None
        finally:
            self._end_retries(owns_retries)
            if watchdog is not None:
                watchdog.set()
            with self._lock:
                response, self._response = self._response, None
            # A turn must never leave a connection behind, whatever ended it (issue SQAM).
            if response is not None and not response_closed(response):
                hard_close(response)

    # ----- a model server on this machine ------------------------------------------------------
    LOADING_RETRY_S = 2.0
    _OVERFLOW = ("exceeds the available context size", "exceed_context_size", "context size",
                 "context length", "context window", "maximum context", "too many tokens")

    # ----- transient provider refusals (429, 5xx) ------------------------------------------------
    # What Claude Code's transport does (the Anthropic SDK compiled into it): a request refused
    # with 408, 409, 429 or any 5xx is sent again, after the delay a Retry-After header names
    # when there is one and otherwise after an exponential backoff that starts at 0.5 s, doubles
    # and stops at 8 s, cut by up to 25 % jitter so panes that share a key do not march in step.
    # The same policy sits here, in the one place every model call passes through, so a pane
    # turn, a side call and the key test all inherit it.
    #
    # A retry is safe because the status arrives before any content is streamed: nothing the user
    # has already seen can be repeated. Statuses that describe the request itself (401, 403, 404,
    # every other 4xx) are raised at once, because sending the same bytes again cannot change
    # that answer. A local model server is left out of the generic policy: its 5xx are
    # deterministic (the prompt no longer fits), so a retry would only delay the sentence that
    # explains it, and its 503-while-loading has the fixed wait of its own below.
    #
    # The retried 5xx are named one by one rather than taken as "anything from 500 up": 501 (the
    # endpoint does not implement this route) and 505 (it refuses this HTTP version) are as final
    # as a 404, and asking six more times only delays the sentence that says so (review of #VMZP).
    # 529 is Anthropic's "overloaded", which is exactly a transient refusal.
    HTTP_RETRY_STATUSES = frozenset({408, 409, 429, 500, 502, 503, 504, 529})
    HTTP_RETRY_ATTEMPTS = 6             # retries after the first refusal
    HTTP_RETRY_BASE_S = 0.5
    HTTP_RETRY_CEILING_S = 8.0
    HTTP_RETRY_JITTER = 0.25            # each backoff waits 75-100 % of the computed delay
    RETRY_AFTER_MAX_S = 60.0            # a Retry-After header is honoured up to a minute
    HTTP_RETRY_BUDGET_FACTOR = 2.0      # default wall-clock budget: twice the first-token deadline

    def _open(self, opener, request, emit, cancel: threading.Event, started: float):
        """Open the response, asking again when the provider's answer is "not now".

        Two refusals are worth another attempt, because nothing has streamed yet and the request
        is unchanged: a local server still loading its weights (llama-server answers 503 on every
        route until they are in), and a provider's transient refusal — 429, 408, 409, a 5xx —
        waited out with the Retry-After the provider sent or the backoff above. Everything else
        is raised at once.
        """
        loading_announced = False
        origin = self._retry_origin if self._retry_origin is not None else started
        retries = self._retries_used
        while True:
            try:
                return opener.open(request, timeout=self.open_timeout)
            except urllib.error.HTTPError as exc:
                waited = time.monotonic() - started
                if self.config.local and exc.code == 503:
                    # The weights are still loading: wait inside the first-token budget instead
                    # of failing a turn that would have worked ten seconds later.
                    if cancel.is_set() or waited + self.LOADING_RETRY_S >= self.first_token_timeout:
                        raise
                    delay = self.LOADING_RETRY_S
                    if not loading_announced:
                        emit({"event": "status", "text": "The local server is loading its model…"})
                        loading_announced = True
                else:
                    delay = None if cancel.is_set() else self._http_retry_wait(
                        exc, retries + 1, time.monotonic() - origin)
                    if delay is not None:
                        note = (f"Provider HTTP {exc.code} · asking again in {self._wait_text(delay)} s "
                                f"(retry {retries + 1} of {self.HTTP_RETRY_ATTEMPTS})")
                        emit({"event": "provider_retry", "reason": "http",
                              "attempt": retries + 1, "max_attempts": self.HTTP_RETRY_ATTEMPTS,
                              "text": note})
                        emit({"event": "status", "text": note})
                if delay is None:
                    raise
                retries += 1
                self._retries_used = retries
                logs.event(_log, "provider_http_retry", level_name="error",
                           model=self.config.model, host=_host(self.config.base_url),
                           status=exc.code, attempt=retries, wait_s=round(delay, 2))
                if getattr(exc, "fp", None) is not None:
                    hard_close(exc.fp)
                self._note_progress()
                if cancel.wait(delay):
                    raise Cancelled("Stopped.") from None

    def _http_retry_wait(self, exc: urllib.error.HTTPError, attempt: int,
                         elapsed: float = 0.0) -> float | None:
        """Seconds to wait before sending this request again, or None when the refusal is final.

        ``attempt`` is the retry being considered, 1-based, and ``elapsed`` is how long this
        logical call has been going. Both caps apply: the count, and the wall clock, because six
        waits a provider named itself can add up to six minutes of a pane, a Test button or a side
        call showing nothing.
        """
        delay = self._http_retry_delay(exc, attempt)
        if delay is None:
            return None
        if elapsed + delay > self.retry_budget:
            logs.event(_log, "provider_retry_budget_spent", level_name="error",
                       model=self.config.model, host=_host(self.config.base_url),
                       status=exc.code, attempt=attempt, elapsed_s=round(elapsed, 2),
                       wait_s=round(delay, 2), budget_s=round(self.retry_budget, 2))
            return None
        return delay

    def _http_retry_delay(self, exc: urllib.error.HTTPError, attempt: int) -> float | None:
        """The wait this refusal asks for, before the budget is applied, or None when it is final.

        A Retry-After header wins over the backoff, because it is the provider naming its own
        window. ``HostedChatProvider`` reads the refusal body before deciding (a spent allowance
        lifts at midnight, not in seconds).
        """
        if self.config.local:
            # A local server's failures are deterministic (the loading 503 has its own wait in
            # ``_open``); a generic retry would only delay the sentence that explains them.
            return None
        if attempt > self.HTTP_RETRY_ATTEMPTS:
            return None
        if exc.code not in self.HTTP_RETRY_STATUSES:
            return None
        hinted = self._retry_after_s(exc)
        if hinted is not None:
            return hinted
        delay = min(self.HTTP_RETRY_BASE_S * 2 ** (attempt - 1), self.HTTP_RETRY_CEILING_S)
        return delay * (1.0 - random.random() * self.HTTP_RETRY_JITTER)

    def _retry_after_s(self, exc: urllib.error.HTTPError) -> float | None:
        """The delay a ``retry-after-ms`` or ``Retry-After`` header asks for, clamped, or None.

        Retry-After may carry seconds or an HTTP date; a date already in the past means "now".
        The clamp keeps a misconfigured or hostile endpoint from parking a turn for an hour —
        an endpoint that means longer than a minute says so in a body the failure text covers.
        """
        headers = getattr(exc, "headers", None)
        if headers is None:
            return None
        raw = headers.get("retry-after-ms")
        if raw is not None:
            value = _finite(raw)
            if value is not None:
                return self._clamp_wait(value / 1000.0)
        raw = headers.get("Retry-After")
        if raw is None:
            return None
        raw = raw.strip()
        value = _finite(raw)
        if value is not None:
            return self._clamp_wait(value)
        parsed = email.utils.parsedate_tz(raw)
        if parsed is None:
            return None
        return self._clamp_wait(email.utils.mktime_tz(parsed) - time.time())

    def _clamp_wait(self, seconds: float) -> float | None:
        """A named wait, clamped to 0…``RETRY_AFTER_MAX_S``, or None when it is not a number.

        The clamp keeps a misconfigured or hostile endpoint from parking a turn for an hour, and
        the finite check keeps a nan through it: ``min(max(nan, 0.0), 60.0)`` is nan.
        """
        if not math.isfinite(seconds):
            return None
        return min(max(float(seconds), 0.0), self.RETRY_AFTER_MAX_S)

    @staticmethod
    def _wait_text(seconds: float) -> str:
        """A wait as the status line shows it: ``8``, ``0.5``, ``0``."""
        return f"{seconds:.1f}".rstrip("0").rstrip(".") or "0"

    def _http_error(self, exc) -> ProviderError:
        """The ProviderError for an HTTP failure. Only the status survives, plus the one phrase a
        local server's overflow body is matched for; HostedChatProvider reads Relay's own body."""
        return ProviderError(self._local_http_reason(exc) or self.http_message(exc.code))

    def _local_http_reason(self, exc) -> str | None:
        """A sentence for a local server's 4xx, or None. Only a *recognised* phrase survives: the
        body quotes the request it was sent, so it is matched, never shown."""
        if not self.config.local or exc.code not in (400, 413, 422, 500):
            return None
        try:
            body = exc.read(4096).decode("utf-8", "replace").lower()
        except (OSError, ValueError, AttributeError):
            return None
        if not any(phrase in body for phrase in self._OVERFLOW):
            return None
        window = f" ({self.config.context_window:,} tokens)" if self.config.context_window else ""
        return (f"The conversation no longer fits the context the local server was started with{window}. "
                "Use /compact or start a new conversation; to make room for good, restart the server "
                "with a larger context (llama-server -c, OLLAMA_CONTEXT_LENGTH) and probe it again.")

    def _tidy_local(self, message: dict, tools: list[dict] | None, finish_reason) -> dict:
        """What a hosted provider does before Relay sees a reply, done here for a local one:
        envelope quirks repaired, reasoning tags moved out of the answer, and (only when the
        endpoint asked for it) tool calls that were written as text recovered."""
        from . import localtext
        message = dict(message)
        if message.get("tool_calls"):
            message["tool_calls"] = repair_tool_calls(message["tool_calls"])
        content = message.get("content")
        if isinstance(content, str) and content:
            answer, reasoning = localtext.split_reasoning(content)
            if reasoning:
                message["content"] = answer
                message["reasoning_content"] = ((message.get("reasoning_content") or "") + reasoning)
        if self.config.tool_text_recovery and not message.get("tool_calls") and finish_reason in (None, "stop"):
            recovered = localtext.recover_tool_calls(message.get("content") or "", tools or [])
            if recovered:
                message["tool_calls"] = recovered
                message["content"] = ""
        return message

    def _model_cap(self) -> int | None:
        """What this endpoint documents for one call, or None when Relay cannot know."""
        from .presets import match_preset
        if self.config.local:
            return max(MIN_OUTPUT_TOKENS, self.config.context_window // 4) if self.config.context_window else None
        preset = match_preset(self.config.base_url, self.config.model)
        return preset.max_output if preset is not None else None

    @staticmethod
    def _partial(message: dict, calls: dict) -> dict | None:
        """The keepable part of a cut-off response: its answer text, and only when no tool call
        was being written. Kept so a partial answer the user watched arrive is still in the
        conversation afterwards, and "carry on" has something to carry on from."""
        if calls or not message.get("content"):
            return None
        partial = {"role": "assistant", "content": message["content"]}
        if message.get("reasoning_content"):
            partial["reasoning_content"] = message["reasoning_content"]
        if message.get("reasoning"):
            partial["reasoning"] = message["reasoning"]
        return partial

    @staticmethod
    def _normalize(message: dict) -> dict:
        normalized = {"role": "assistant", "content": message.get("content") or ""}
        if message.get("reasoning_content") is not None:
            normalized["reasoning_content"] = message["reasoning_content"]
        # OpenRouter reports reasoning as "reasoning"; keep it for multi-step tool turns.
        if message.get("reasoning"):
            normalized["reasoning"] = message["reasoning"]
        calls = message.get("tool_calls") or []
        if len(calls) > 16:
            raise ProviderError("Too many tool calls in one response.")
        ids = set()
        for call in calls:
            if not isinstance(call, dict) or not call.get("id") or call.get("type") != "function":
                raise ProviderError("Invalid tool-call envelope.")
            if call["id"] in ids:
                raise ProviderError("Duplicate tool-call ID.")
            ids.add(call["id"])
            func = call.get("function", {})
            if not isinstance(func.get("name"), str) or not isinstance(func.get("arguments"), str):
                raise ProviderError("Invalid function call.")
        if calls:
            normalized["tool_calls"] = calls
        return normalized

    def _stream(self, response, emit, cancel, started: float | None = None,
                tools: list[dict] | None = None) -> dict:
        """started: when the request was sent. thinking_done.elapsed_ms counts from then, because some
        providers (GLM) buffer reasoning and deliver it in one burst just before the answer."""
        message = {"content": "", "reasoning_content": ""}
        calls: dict[int, dict] = {}
        total = 0
        got_done = False
        finish_reason = None
        reasoning_announced = False
        usage = None
        event_lines: list[str] = []
        event_size = 0
        thinking_started = None
        thinking_closed = False
        thinking_chars = 0
        # A local model may write its reasoning into the answer as <think> tags (localtext.py).
        splitter = None
        if self.config.local:
            from .localtext import ThinkSplitter
            splitter = ThinkSplitter()

        def finish_thinking():
            nonlocal thinking_closed, thinking_chars
            thinking_closed = True
            emit({"event": "thinking_done", "elapsed_ms": int((time.monotonic() - thinking_started) * 1000),
                  "chars": thinking_chars})
            thinking_chars = 0
        while True:
            if cancel.is_set():
                raise Cancelled("Stopped.")
            raw = response.readline(MAX_EVENT + 1)
            if not raw:
                # The watchdog closes the socket from another thread; that reads as a clean EOF here.
                self._maybe_stalled(cancel)
                break
            total += len(raw)
            event_size += len(raw)
            if len(raw) > MAX_EVENT or event_size > MAX_EVENT or total > MAX_RESPONSE:
                raise ProviderError("Provider stream exceeded the size limit.")
            line = raw.decode("utf-8").rstrip("\r\n")
            if line:
                if line.startswith("data:"):
                    event_lines.append(line[5:].lstrip(" "))
                continue
            event_size = 0
            if not event_lines:
                continue
            event = "\n".join(event_lines)
            event_lines.clear()
            if event == "[DONE]":
                self._note_progress(True)
                got_done = True
                break
            obj = json.loads(event)
            if "error" in obj:
                raise ProviderError("Provider reported a streaming error. No partial tool call was executed.")
            if isinstance(obj.get("usage"), dict):
                usage = obj["usage"]
                self._note_progress(True)
            choices = obj.get("choices", [])
            if not choices:
                # An empty-choices event is a keepalive: it does not reset the idle deadline.
                continue
            choice = choices[0]
            # Kimi reports usage inside the final choice unless stream_options is sent.
            if isinstance(choice.get("usage"), dict) and usage is None:
                usage = choice["usage"]
                self._note_progress(True)
            if choice.get("finish_reason"):
                self._note_progress(True)
            finish_reason = choice.get("finish_reason") or finish_reason
            delta = choice.get("delta", {})
            if splitter is not None and isinstance(delta.get("content"), str) and delta["content"]:
                # Reasoning tags become reasoning_content here, so everything below treats a local
                # model's thinking exactly as it treats a hosted one's.
                parts = splitter.feed(delta["content"])
                delta = {**delta, "content": "".join(t for k, t in parts if k == "content")}
                tagged = "".join(t for k, t in parts if k == "thinking")
                if tagged:
                    delta["reasoning_content"] = (delta.get("reasoning_content") or "") + tagged
            if isinstance(delta.get("reasoning"), str) and delta["reasoning"]:
                message["reasoning"] = message.get("reasoning", "") + delta["reasoning"]
            if isinstance(delta.get("reasoning_content"), str):
                message["reasoning_content"] += delta["reasoning_content"]
            thinking = _reasoning_text(delta)
            if thinking:
                self._note_progress(True)   # reasoning is progress, but it is not an answer yet
                if not reasoning_announced:
                    emit({"event": "status", "text": "Model is reasoning…"})
                    reasoning_announced = True
                if thinking_started is None:
                    thinking_started = started if started is not None else time.monotonic()
                elif thinking_closed:
                    # Reasoning resumed after the answer had begun (GLM interleave): a second block,
                    # clocked from its own start, with its own thinking_done when it ends.
                    thinking_closed = False
                    thinking_started = time.monotonic()
                thinking_chars += len(thinking)
                emit({"event": "thinking_delta", "text": thinking})
            if thinking_started is not None and not thinking_closed and (
                    (isinstance(delta.get("content"), str) and delta["content"]) or delta.get("tool_calls")):
                finish_thinking()
            if isinstance(delta.get("content"), str):
                if delta["content"]:
                    # An answer has begun: a retry would repeat text the user can already see.
                    self._produced = True
                    self._note_progress(True)
                message["content"] += delta["content"]
                emit({"event": "delta", "text": delta["content"]})
            for chunk in delta.get("tool_calls", []):
                self._produced = True
                self._note_progress(True)
                index = chunk.get("index")
                if not isinstance(index, int) or not 0 <= index < 16:
                    raise ProviderError("Invalid tool-call index.")
                call = calls.setdefault(index, {"id": "", "type": "function", "function": {"name": "", "arguments": ""}})
                if chunk.get("id"):
                    call["id"] = chunk["id"]
                func = chunk.get("function", {})
                call["function"]["name"] += func.get("name") or ""
                call["function"]["arguments"] += func.get("arguments") or ""
        if splitter is not None:
            for kind, text in splitter.flush():
                if kind == "content":
                    message["content"] += text
                    emit({"event": "delta", "text": text})
                else:
                    message["reasoning_content"] += text
        if thinking_started is not None and not thinking_closed:
            finish_thinking()
        if cancel.is_set():
            raise Cancelled("Stopped.")
        # Usage is reported before any refusal below: a cut-off response still spent its tokens, and
        # dropping them leaves the session total and the context tracker short by the largest request
        # of the turn (owner report, 2026-09-18).
        if usage is not None:
            emit({"event": "usage", "usage": usage})
        if not got_done and finish_reason not in {"stop", "tool_calls"}:
            raise ProviderError("Provider stream ended unexpectedly; partial tools were not executed.")
        if finish_reason in {"length", "content_filter"}:
            raise ProviderTruncated(finish_reason, self.config.max_tokens, self._produced,
                                    self._partial(message, calls), self._model_cap())
        if calls:
            message["tool_calls"] = [calls[i] for i in sorted(calls)]
        if self.config.local:
            message = self._tidy_local(message, tools, finish_reason)
        return self._normalize(message)


# ----- Relay's own hosted service (the relay-free preset) --------------------------------------

class HostedChatProvider(ChatProvider):
    """ChatProvider against Relay's gateway: a bearer token from ``hosted.Session`` instead of a key.

    Everything about the request and the stream is the parent's. What differs: the token is taken
    (and made, on first use) just before each call; a 401 is retried once after a forced refresh,
    because the gateway may have rotated it; the ``X-Relay-Quota-*`` reply headers become one
    ``hosted_quota`` event; and a refusal's JSON body, which is Relay's own, picks the wording and
    the ``code`` the pane branches on — and decides the parent's transient-refusal retry: a rate
    limit is waited out until its window reopens, a spent allowance is not waited out at all.
    A provider's body is never shown; this one's ``message`` is,
    only for a code with no sentence of its own, and truncated (``hosted.describe_error``).
    """

    def __init__(self, config: ProviderConfig, stall_timeout: float = DEFAULT_STALL_TIMEOUT,
                 session=None):
        super().__init__(config, stall_timeout)
        from . import hosted
        # RELAY_HOSTED_URL (tests, a local gateway) applies here, the one place every hosted call
        # passes through, so the pane's model, each tier and the key test all follow it.
        endpoint = hosted.endpoint_for(config.base_url)
        if endpoint != config.base_url:
            config.base_url = endpoint
            config.validate()
        self._session = session
        self._quota_headers = None      # the headers of the response just opened, read after it closes
        self._refusal = None            # (exception, body): an HTTPError's body is readable once

    @property
    def session(self):
        if self._session is None:
            from . import hosted
            self._session = hosted.session()
        return self._session

    def complete(self, messages: list[dict], tools: list[dict],
                 emit: Callable[[dict], None], cancel: threading.Event) -> dict:
        from . import hosted
        # The refresh below makes a second HTTP call for the same request. Owning the retry count
        # and clock here means it continues this one's instead of starting six fresh retries with
        # a fresh budget: a gateway answering 401 and then 429 was worth 14 requests (review of
        # #VMZP), and each of those retries could wait a minute.
        owns_retries = self._begin_retries()
        try:
            try:
                self.config.api_key = self.session.token()
            except hosted.HostedUnavailable as exc:
                raise ProviderError(str(exc), exc.code, exc.resets_at) from None
            try:
                return self._call(messages, tools, emit, cancel)
            except ProviderError as exc:
                if exc.code != "token_expired" or cancel.is_set():
                    raise
            # Once: the token the clock thought was good was refused, so take a fresh one and try
            # again. A second refusal is reported as it is; retrying further would loop on a gateway
            # that has stopped accepting this installation.
            try:
                self.config.api_key = self.session.token(force=True)
            except hosted.HostedUnavailable as exc:
                raise ProviderError(str(exc), exc.code, exc.resets_at) from None
            return self._call(messages, tools, emit, cancel)
        finally:
            self._end_retries(owns_retries)

    def _call(self, messages, tools, emit, cancel) -> dict:
        self._quota_headers = None      # a call that never opens must not report the last one's quota
        try:
            result = super().complete(messages, tools, emit, cancel)
        except ProviderError:
            self._emit_quota(emit)
            raise
        self._emit_quota(emit)
        return result

    def _emit_quota(self, emit) -> None:
        """One ``hosted_quota`` event from the headers of the response just closed, when it had them."""
        headers = self._quota_headers
        self._quota_headers = None
        quota = self.session.note_quota(headers)
        if quota is not None:
            emit({"event": "hosted_quota", **quota})

    def _open(self, opener, request, emit, cancel: threading.Event, started: float):
        response = super()._open(opener, request, emit, cancel, started)
        self._quota_headers = response.headers
        return response

    def _refusal_body(self, exc) -> bytes:
        """The refusal body, read once and kept: ``fp`` gives its bytes a single time, and both
        the retry decision below and the failure text need them."""
        from . import hosted
        if self._refusal is not None and self._refusal[0] is exc:
            return self._refusal[1]
        body = b""
        if getattr(exc, "fp", None) is not None:
            try:
                body = exc.read(hosted.MAX_BODY)
            except (OSError, ValueError, AttributeError):
                body = b""
        self._refusal = (exc, body)
        return body

    def _http_retry_delay(self, exc, attempt: int) -> float | None:
        """The gateway names its refusal in a body only it sends, and that decides the wait:
        ``quota_exhausted`` lifts at midnight, not in seconds, so it is final here and the pane
        gets its sentence at once; ``rate_limited`` carries the moment its window reopens
        (``resets_at``), which is exactly how long to wait. The budget in ``_http_retry_wait``
        applies to the answer either way.

        A refusal the gateway marks ``retried`` is final too (owner, 2026-09-19): it already sent
        this request to every upstream the role has, so the six retries here would re-run that
        chain a second time — the same 429 or 5xx paid for twice, once on each side of the
        gateway. The rate-limit window still wins where the gateway reports one, because that is
        the gateway's own door and not an upstream's."""
        from . import hosted
        body = self._refusal_body(exc)
        _, code, resets_at = hosted.describe_error(exc.code, body)
        if code == "quota_exhausted":
            return None
        wait = super()._http_retry_delay(exc, attempt)
        if wait is not None and code == "rate_limited" and isinstance(resets_at, (int, float)) \
                and not isinstance(resets_at, bool):
            window = self._clamp_wait(resets_at - time.time())
            if window is not None:
                return window
        if wait is not None and hosted.upstream_retried(body):
            logs.event(_log, "hosted_retry_owned_by_gateway", model=self.config.model,
                       host=_host(self.config.base_url), status=exc.code, attempt=attempt)
            return None
        return wait

    def _http_error(self, exc) -> ProviderError:
        from . import hosted
        # A refusal carries the quota headers too, and a 429 is exactly when the chip must update.
        self._quota_headers = getattr(exc, "headers", None)
        text, code, resets_at = hosted.describe_error(exc.code, self._refusal_body(exc))
        return ProviderError(text, code, resets_at)


def make_provider(config: ProviderConfig, stall_timeout: float = DEFAULT_STALL_TIMEOUT) -> ChatProvider:
    """The transport for a config: Relay's hosted one for a ``hosted`` config, the plain one otherwise.

    Every place that builds a provider for a turn goes through here (agent.py, keytest.py), so a
    hosted config can never be sent with an empty Authorization header by a caller that forgot.
    """
    if config.hosted:
        return HostedChatProvider(config, stall_timeout)
    return ChatProvider(config, stall_timeout)
