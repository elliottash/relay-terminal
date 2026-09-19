# SPDX-License-Identifier: AGPL-3.0-or-later
"""One upstream call: open it with failover, then pump its stream to the client.

The transport is ``urllib.request`` in a plain thread, the pattern of ``backend/relay_core/
provider.py``: redirects are refused (an ``Authorization`` header must never follow a redirect to
a host the operator did not configure), the response socket gets a stall timeout once the head is
in, and bytes are handed to the event loop through an ``asyncio.Queue`` as they arrive.

Failover is decided **before the first byte** only. A transient refusal (``RETRYABLE_STATUSES``)
or a connect timeout from one upstream means the next in the role's list is tried; once a byte has
reached the client the reply is that upstream's, whatever happens next, because a client cannot be
handed a second beginning.

**The gateway owns the upstream retries** (owner, 2026-09-19). A refusal it returns after trying
more than one upstream carries ``error.retried`` (the number of upstreams it tried beyond the
first), and the desktop's ``HostedChatProvider`` treats such a refusal as final instead of running
its own six retries over the same chain: 429s and 5xx were being retried twice over, once here and
once there, so one refused turn could cost a couple of dozen upstream requests.

Usage accounting: every upstream is asked for ``stream_options.include_usage`` and the final
``usage`` chunk is what the quota settles on. A provider that sends none is charged from what
was seen, at four characters a token, so no reply goes uncounted.
"""
from __future__ import annotations

import asyncio
import json
import logging
import threading
import time
import urllib.error
import urllib.request
from typing import AsyncIterator

from .config import Config, Upstream
from .validate import Validated

log = logging.getLogger("relay.gateway.proxy")
bodies_log = logging.getLogger("relay.gateway.bodies")

MAX_RESPONSE = 8 * 1024 * 1024       # the client's own limit (provider.MAX_RESPONSE)
MAX_EVENT = 1024 * 1024
USER_AGENT = "relay-gateway/0.1"
_DONE = object()

# The upstream statuses worth trying the next upstream for. Deliberately the same set as the
# desktop transport's ``ChatProvider.HTTP_RETRY_STATUSES`` (protocol 15.2): a status one layer
# calls transient and the other calls final would mean a refusal is either retried nowhere or
# retried twice over. The two cannot share a module — the box runs ``gateway/`` and ``remote/``
# only, never ``backend/`` (gateway/README.md) — so ``tests/test_gateway.py`` asserts they are
# equal instead. 501 and 505 are as final as a 404; 529 is Anthropic's "overloaded".
RETRYABLE_STATUSES = frozenset({408, 409, 429, 500, 502, 503, 504, 529})


class NoRedirect(urllib.request.HTTPRedirectHandler):
    """Never forward an Authorization header to a redirected endpoint."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # urllib closes ``fp`` only after this returns; raising here leaves that to us.
        hard_close(fp)
        raise UpstreamFailure("redirected", retryable=False)


class UpstreamFailure(Exception):
    """One upstream did not give a usable response head. ``retryable`` says whether the next
    upstream should be tried (429, 5xx, connect failure) or the request is simply refused."""

    def __init__(self, reason: str, *, retryable: bool, status: int | None = None):
        super().__init__(reason)
        self.reason = reason
        self.retryable = retryable
        self.status = status


def hard_close(response) -> None:
    """Close a response so its socket cannot outlive the request, and so a thread blocked in a
    read on it is unblocked (``shutdown`` does what ``close`` alone does not)."""
    try:
        sock = getattr(getattr(response, "fp", None), "raw", None)
        sock = getattr(sock, "_sock", None)
        if sock is not None:
            import socket as socket_module
            sock.shutdown(socket_module.SHUT_RDWR)
    except OSError:
        pass
    try:
        response.close()
    except Exception:
        pass


def _socket_of(response):
    raw = getattr(getattr(response, "fp", None), "raw", None)
    return getattr(raw, "_sock", None)


class Outcome:
    """What the log line and the quota settlement need, filled in as the call proceeds."""

    def __init__(self):
        self.provider = ""
        self.model = ""
        self.fallback = 0            # index of the upstream that served, 0 = the first choice
        self.attempts = 0            # upstreams opened for this request; attempts - 1 were retries
        self.status = 0              # the status the *client* was given
        self.ttft_ms: int | None = None
        self.total_ms = 0
        self.input_tokens = 0
        self.output_tokens = 0
        self.usage_reported = False
        self.error = ""              # a stable error code, or ""
        self.served = False          # true once a byte reached the client
        self.truncated = False
        self.output_chars = 0
        self.reached_upstream = False


class Completion:
    """One client request against a role: ``open()`` finds an upstream that answers, then
    ``chunks()`` is the client's stream."""

    def __init__(self, config: Config, validated: Validated, upstreams: list[Upstream],
                 *, diagnostic: bool = False):
        self.config = config
        self.validated = validated
        self.upstreams = upstreams
        self.diagnostic = diagnostic
        self.outcome = Outcome()
        self.started = time.monotonic()
        self.cancel = threading.Event()
        self.response = None
        self.upstream: Upstream | None = None
        self.content_type = "text/event-stream"

    # ---- opening -------------------------------------------------------------------------------

    def _connect(self, upstream: Upstream):
        """Blocking: post to one upstream and return its response once the head is in."""
        provider = self.config.provider_for(upstream)
        key = provider.key()
        if not key:
            raise UpstreamFailure("no key for " + provider.name, retryable=True)
        body = self.validated.upstream_body(upstream.model, upstream.extra, provider.effort_style)
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        if self.diagnostic:
            bodies_log.info("upstream request to %s: %s", provider.name, data.decode("utf-8", "replace"))
        request = urllib.request.Request(
            provider.base_url + "/chat/completions", data=data, method="POST",
            headers={"Content-Type": "application/json", "Accept": "text/event-stream",
                     "User-Agent": USER_AGENT, "Authorization": "Bearer " + key})
        opener = urllib.request.build_opener(NoRedirect())
        try:
            response = opener.open(request, timeout=self.config.upstream_connect_timeout)
        except urllib.error.HTTPError as exc:
            # The body may echo the prompt; it is neither read nor logged.
            if getattr(exc, "fp", None) is not None:
                hard_close(exc.fp)
            raise UpstreamFailure(f"HTTP {exc.code}", status=exc.code,
                                  retryable=exc.code in RETRYABLE_STATUSES) from None
        except UpstreamFailure:
            raise
        except (urllib.error.URLError, TimeoutError, OSError, ValueError) as exc:
            raise UpstreamFailure(type(exc).__name__, retryable=True) from None
        sock = _socket_of(response)
        if sock is not None:
            try:
                sock.settimeout(self.config.upstream_stall_timeout)
            except (OSError, ValueError):
                pass
        return response

    async def open(self) -> bool:
        """Try the role's upstreams in order until one returns a response head. False when none
        did; ``outcome.error`` then says why (``free_unavailable``)."""
        loop = asyncio.get_running_loop()
        for index, upstream in enumerate(self.upstreams):
            provider = self.config.provider_for(upstream)
            self.outcome.provider, self.outcome.model = provider.name, upstream.model
            self.outcome.fallback = index
            self.outcome.attempts = index + 1
            try:
                self.response = await _in_thread(loop, self._connect, upstream)
            except UpstreamFailure as failure:
                log.info("upstream %s/%s refused: %s", provider.name, upstream.model, failure.reason)
                if failure.retryable and index + 1 < len(self.upstreams):
                    continue
                self.outcome.error = "free_unavailable"
                self.outcome.status = 503 if failure.retryable else 502
                return False
            self.upstream = upstream
            self.outcome.reached_upstream = True
            if "application/json" in self.response.headers.get("Content-Type", ""):
                self.content_type = "application/json"
            return True
        self.outcome.error = "free_unavailable"
        self.outcome.status = 503
        return False

    # ---- streaming -----------------------------------------------------------------------------

    def _pump(self, loop: asyncio.AbstractEventLoop, queue: asyncio.Queue) -> None:
        """Blocking, in its own thread: read upstream events and hand each to the loop."""
        response = self.response

        def push(item) -> None:
            loop.call_soon_threadsafe(queue.put_nowait, item)

        total = 0
        try:
            if self.content_type == "application/json":
                raw = response.read(MAX_RESPONSE + 1)
                if len(raw) > MAX_RESPONSE:
                    self.outcome.truncated = True
                    return
                self._account(raw)
                push(raw)
                return
            event = bytearray()
            while not self.cancel.is_set():
                line = response.readline(MAX_EVENT + 1)
                if not line:
                    break
                total += len(line)
                if len(line) > MAX_EVENT or len(event) + len(line) > MAX_EVENT or total > MAX_RESPONSE:
                    self.outcome.truncated = True
                    break
                event += line
                if line in (b"\n", b"\r\n"):
                    self._account(bytes(event))
                    push(bytes(event))
                    event = bytearray()
            if event and not self.outcome.truncated:
                self._account(bytes(event))
                push(bytes(event))
        except Exception as exc:                # a stall, a reset, a hard close from cancel()
            if not self.cancel.is_set():
                log.info("upstream stream ended early: %s", type(exc).__name__)
                self.outcome.truncated = True
        finally:
            hard_close(response)
            push(_DONE)

    def _account(self, event: bytes) -> None:
        """Pull usage and content length out of one SSE event (or a JSON body)."""
        if self.diagnostic:
            bodies_log.info("upstream event: %s", event.decode("utf-8", "replace").rstrip())
        for line in event.split(b"\n"):
            line = line.strip()
            if self.content_type != "application/json":
                if not line.startswith(b"data:"):
                    continue
                line = line[5:].strip()
            if not line or line == b"[DONE]":
                continue
            try:
                payload = json.loads(line)
            except ValueError:
                continue
            if not isinstance(payload, dict):
                continue
            usage = payload.get("usage")
            if isinstance(usage, dict):
                prompt = usage.get("prompt_tokens")
                completion = usage.get("completion_tokens")
                if isinstance(prompt, int) and isinstance(completion, int):
                    self.outcome.input_tokens = max(0, prompt)
                    self.outcome.output_tokens = max(0, completion)
                    self.outcome.usage_reported = True
            for choice in payload.get("choices") or []:
                if not isinstance(choice, dict):
                    continue
                piece = choice.get("delta") or choice.get("message") or {}
                if isinstance(piece, dict):
                    for field in ("content", "reasoning", "reasoning_content"):
                        text = piece.get(field)
                        if isinstance(text, str):
                            self.outcome.output_chars += len(text)
                    for call in piece.get("tool_calls") or []:
                        if isinstance(call, dict):
                            arguments = (call.get("function") or {}).get("arguments")
                            if isinstance(arguments, str):
                                self.outcome.output_chars += len(arguments)

    async def chunks(self) -> AsyncIterator[bytes]:
        """The client's body. Ends when upstream ends, the size cap is hit, or the client goes
        away (``aclose`` from the HTTP layer lands in the ``finally``)."""
        loop = asyncio.get_running_loop()
        queue: asyncio.Queue = asyncio.Queue()
        thread = threading.Thread(target=self._pump, args=(loop, queue), daemon=True,
                                  name="gateway-upstream")
        thread.start()
        try:
            while True:
                item = await queue.get()
                if item is _DONE:
                    break
                if self.outcome.ttft_ms is None:
                    self.outcome.ttft_ms = int((time.monotonic() - self.started) * 1000)
                self.outcome.served = True
                yield item
        finally:
            self.cancel.set()
            if self.response is not None:
                hard_close(self.response)
            self.finish()

    def finish(self) -> None:
        """Freeze the outcome: the estimate stands in for any usage upstream did not report."""
        self.outcome.total_ms = int((time.monotonic() - self.started) * 1000)
        if not self.outcome.usage_reported and self.outcome.reached_upstream:
            self.outcome.input_tokens = self.validated.input_estimate
            self.outcome.output_tokens = max(1, self.outcome.output_chars // 4) \
                if self.outcome.output_chars else 0

    def cost_micros(self) -> int:
        if self.upstream is None:
            return 0
        provider = self.config.provider_for(self.upstream)
        return provider.cost_micros(self.upstream.model, self.outcome.input_tokens,
                                    self.outcome.output_tokens)


async def _in_thread(loop: asyncio.AbstractEventLoop, function, *args):
    """Run a blocking call in a fresh daemon thread and await its result. Not the default
    executor: that pool is small and shared, and a slow upstream must not queue behind another."""
    future = loop.create_future()

    def run():
        try:
            result = function(*args)
        except BaseException as exc:                      # noqa: BLE001 - re-raised in the loop
            loop.call_soon_threadsafe(_settle, future, None, exc)
        else:
            loop.call_soon_threadsafe(_settle, future, result, None)

    threading.Thread(target=run, daemon=True, name="gateway-connect").start()
    return await future


def _settle(future: asyncio.Future, result, exc) -> None:
    if future.cancelled():
        if result is not None:
            hard_close(result)        # nobody is waiting for the response any more
        return
    if exc is not None:
        future.set_exception(exc)
    else:
        future.set_result(result)
