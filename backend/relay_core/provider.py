# SPDX-License-Identifier: GPL-3.0-or-later
"""OpenAI-compatible chat-completions transport with streamed tool-call assembly."""
from __future__ import annotations

import base64
import json
import os
import socket
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from typing import Callable

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
WATCHDOG_TICK = 0.5
# Environment override for the deadline, kept from the 2026-09-17 stopgap that widened the raw
# socket timeout. It wins over the agent option, so a pane that needs more room needs no settings
# change; unset, the option (default 60 s) decides. One code path, one deadline.
ENV_TIMEOUT = "RELAY_PROVIDER_TIMEOUT"
ENV_MIN, ENV_MAX = 5.0, 900.0


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


def wire_messages(messages: list[dict]) -> list[dict]:
    """Drop Relay's own bookkeeping keys (relay_kind, relay_request) before a message leaves the machine."""
    if not any(isinstance(m, dict) and any(k.startswith("relay_") for k in m) for m in messages):
        return messages
    return [{k: v for k, v in m.items() if not k.startswith("relay_")} if isinstance(m, dict) else m for m in messages]


class ProviderError(RuntimeError):
    pass

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
        raise ProviderError("Provider redirected the request. Check the configured base URL.")

@dataclass
class ProviderConfig:
    base_url: str
    model: str
    api_key: str = field(repr=False)
    extra: dict = field(default_factory=dict)
    max_tokens: int = 32768

    def validate(self) -> None:
        url = urllib.parse.urlsplit(self.base_url)
        if url.scheme not in {"https", "http"} or not url.hostname or url.username or url.password or url.query or url.fragment:
            raise ValueError("Base URL must be an HTTPS URL without credentials, query, or fragment.")
        if url.scheme == "http" and url.hostname not in {"localhost", "127.0.0.1", "::1"}:
            raise ValueError("Unencrypted HTTP is only allowed for a loopback/local model server.")
        if not self.model.strip():
            raise ValueError("A model ID is required.")
        if not self.api_key.strip() and url.scheme == "https":
            raise ValueError("An API key is required for this remote provider.")
        if not isinstance(self.extra, dict):
            raise ValueError("Extra parameters must be a JSON object.")
        allowed = {"thinking", "reasoning", "reasoning_effort", "temperature", "top_p"}
        if set(self.extra) - allowed:
            raise ValueError("Extra parameters may only contain thinking, reasoning, reasoning_effort, temperature, and top_p.")
        if not 256 <= self.max_tokens <= 32768:
            raise ValueError("Output token limit must be between 256 and 32768.")

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
        self._lock = threading.Lock()

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
        return max(CONNECT_TIMEOUT, self.stall_timeout)

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
                    "same provider - open Settings › Models › API keys… to check it.")
        return (f"Provider HTTP {code} for {where}. "
                "Check endpoint, model access, key, quota, and parameters.")

    # ----- idle deadline -------------------------------------------------------------
    def _note_progress(self) -> None:
        self._progress = time.monotonic()

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
                if time.monotonic() - self._progress >= self.stall_timeout:
                    self._stalled = True
                    hard_close(response)
                    return

        threading.Thread(target=watch, name="relay-provider-stall", daemon=True).start()
        return finished

    def _maybe_stalled(self, cancel: threading.Event) -> None:
        if self._stalled and not cancel.is_set():
            raise ProviderStalled(self.stall_timeout, self._produced) from None

    def complete(self, messages: list[dict], tools: list[dict],
                 emit: Callable[[dict], None], cancel: threading.Event) -> dict:
        if cancel.is_set():
            raise Cancelled("Stopped.")
        started = time.monotonic()
        self._stalled = False
        self._produced = False
        self._note_progress()
        payload = {"model": self.config.model, "messages": wire_messages(messages),
                   "stream": True, "max_tokens": self.config.max_tokens, **self.config.extra}
        if tools:
            # Side calls (summaries, recaps, suggestions) send no tools; some APIs reject "tools": [].
            payload["tools"] = tools
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
            response = opener.open(request, timeout=self.open_timeout)
            with self._lock:
                self._response = response
                self._last_response = response
            self._note_progress()
            # Socket timeout as a backstop under the watchdog, so a byte-silent stream and a
            # keepalive-only stream end the same way, with the watchdog deciding first.
            sock = _socket_of(response)
            if sock is not None:
                try:
                    sock.settimeout(self.stall_timeout * 2 + WATCHDOG_TICK)
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
                    thinking = _reasoning_text(message)
                    if thinking:
                        emit({"event": "thinking_delta", "text": thinking})
                        emit({"event": "thinking_done", "elapsed_ms": 0, "chars": len(thinking)})
                    if message.get("content"):
                        emit({"event": "delta", "text": message["content"]})
                    return self._normalize(message)
                return self._stream(response, emit, cancel, started)
        except urllib.error.HTTPError as exc:
            # The error carries the response, so it also carries the socket: close it here rather
            # than leaving it to the garbage collector.
            if getattr(exc, "fp", None) is not None:
                hard_close(exc.fp)
            # Providers can echo submitted secrets/prompts in error bodies. Do not log them.
            raise ProviderError(self.http_message(exc.code)) from None
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            # The watchdog's hard close surfaces here; report the stall, not a generic failure.
            self._maybe_stalled(cancel)
            if cancel.is_set():
                raise Cancelled("Stopped.") from None
            if isinstance(exc, TimeoutError) or isinstance(getattr(exc, "reason", None), TimeoutError):
                # Either the socket-timeout backstop fired before the watchdog tick, or no response
                # arrived at all. Both are a stall the agent may retry, not a broken base URL.
                streaming = watchdog is not None
                raise ProviderStalled(self.stall_timeout if streaming else self.open_timeout,
                                      self._produced, "stream" if streaming else "connect") from None
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
            if watchdog is not None:
                watchdog.set()
            with self._lock:
                response, self._response = self._response, None
            # A turn must never leave a connection behind, whatever ended it (issue SQAM).
            if response is not None and not response_closed(response):
                hard_close(response)

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

    def _stream(self, response, emit, cancel, started: float | None = None) -> dict:
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

        def finish_thinking():
            nonlocal thinking_closed
            thinking_closed = True
            emit({"event": "thinking_done", "elapsed_ms": int((time.monotonic() - thinking_started) * 1000),
                  "chars": thinking_chars})
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
                self._note_progress()
                got_done = True
                break
            obj = json.loads(event)
            if "error" in obj:
                raise ProviderError("Provider reported a streaming error. No partial tool call was executed.")
            if isinstance(obj.get("usage"), dict):
                usage = obj["usage"]
                self._note_progress()
            choices = obj.get("choices", [])
            if not choices:
                # An empty-choices event is a keepalive: it does not reset the idle deadline.
                continue
            choice = choices[0]
            # Kimi reports usage inside the final choice unless stream_options is sent.
            if isinstance(choice.get("usage"), dict) and usage is None:
                usage = choice["usage"]
                self._note_progress()
            if choice.get("finish_reason"):
                self._note_progress()
            finish_reason = choice.get("finish_reason") or finish_reason
            delta = choice.get("delta", {})
            if isinstance(delta.get("reasoning"), str) and delta["reasoning"]:
                message["reasoning"] = message.get("reasoning", "") + delta["reasoning"]
            if isinstance(delta.get("reasoning_content"), str):
                message["reasoning_content"] += delta["reasoning_content"]
            thinking = _reasoning_text(delta)
            if thinking:
                self._note_progress()   # reasoning is progress, but it is not an answer yet
                if not reasoning_announced:
                    emit({"event": "status", "text": "Model is reasoning…"})
                    reasoning_announced = True
                if thinking_started is None:
                    thinking_started = started if started is not None else time.monotonic()
                thinking_chars += len(thinking)
                emit({"event": "thinking_delta", "text": thinking})
            if thinking_started is not None and not thinking_closed and (
                    (isinstance(delta.get("content"), str) and delta["content"]) or delta.get("tool_calls")):
                finish_thinking()
            if isinstance(delta.get("content"), str):
                if delta["content"]:
                    # An answer has begun: a retry would repeat text the user can already see.
                    self._produced = True
                    self._note_progress()
                message["content"] += delta["content"]
                emit({"event": "delta", "text": delta["content"]})
            for chunk in delta.get("tool_calls", []):
                self._produced = True
                self._note_progress()
                index = chunk.get("index")
                if not isinstance(index, int) or not 0 <= index < 16:
                    raise ProviderError("Invalid tool-call index.")
                call = calls.setdefault(index, {"id": "", "type": "function", "function": {"name": "", "arguments": ""}})
                if chunk.get("id"):
                    call["id"] = chunk["id"]
                func = chunk.get("function", {})
                call["function"]["name"] += func.get("name") or ""
                call["function"]["arguments"] += func.get("arguments") or ""
        if thinking_started is not None and not thinking_closed:
            finish_thinking()
        if cancel.is_set():
            raise Cancelled("Stopped.")
        if not got_done and finish_reason not in {"stop", "tool_calls"}:
            raise ProviderError("Provider stream ended unexpectedly; partial tools were not executed.")
        if finish_reason in {"length", "content_filter"}:
            raise ProviderError("Response was truncated or filtered; partial tools were not executed. Increase output limit or narrow the task.")
        if usage is not None:
            emit({"event": "usage", "usage": usage})
        if calls:
            message["tool_calls"] = [calls[i] for i in sorted(calls)]
        return self._normalize(message)
