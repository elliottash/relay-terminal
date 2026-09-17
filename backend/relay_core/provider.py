# SPDX-License-Identifier: GPL-3.0-or-later
"""OpenAI-compatible chat-completions transport with streamed tool-call assembly."""
from __future__ import annotations

import json
import threading
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from typing import Callable

MAX_EVENT = 2 * 1024 * 1024
MAX_RESPONSE = 8 * 1024 * 1024

class ProviderError(RuntimeError):
    pass

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
    max_tokens: int = 8192

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
    def __init__(self, config: ProviderConfig):
        config.validate()
        self.config = config
        self._response = None
        self._lock = threading.Lock()

    def cancel(self) -> None:
        # Best effort. An in-flight DNS/TLS/read may last until the 30s I/O timeout.
        with self._lock:
            response = self._response
        if response is not None:
            # Capture this response now: a delayed closer must never close a newer turn.
            def close_captured_response():
                try:
                    response.close()
                except (OSError, ValueError):
                    pass
            threading.Thread(target=close_captured_response, daemon=True).start()

    def complete(self, messages: list[dict], tools: list[dict],
                 emit: Callable[[dict], None], cancel: threading.Event) -> dict:
        if cancel.is_set():
            raise Cancelled("Stopped.")
        payload = {"model": self.config.model, "messages": messages,
                   "stream": True, "max_tokens": self.config.max_tokens,
                   "tools": tools, **self.config.extra}
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        if len(data) > MAX_RESPONSE:
            raise ProviderError("Conversation exceeds the local request size limit. Start a new conversation.")
        headers = {"Content-Type": "application/json", "Accept": "text/event-stream", "User-Agent": "Relay/0.1"}
        if self.config.api_key:
            headers["Authorization"] = "Bearer " + self.config.api_key
        request = urllib.request.Request(self.config.base_url.rstrip("/") + "/chat/completions",
                                         data=data, headers=headers, method="POST")
        opener = urllib.request.build_opener(NoRedirect())
        try:
            response = opener.open(request, timeout=30)
            with self._lock:
                self._response = response
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
                    message = choices[0].get("message", {})
                    if message.get("content"):
                        emit({"event": "delta", "text": message["content"]})
                    return self._normalize(message)
                return self._stream(response, emit, cancel)
        except urllib.error.HTTPError as exc:
            # Providers can echo submitted secrets/prompts in error bodies. Do not log them.
            raise ProviderError(f"Provider HTTP {exc.code}. Check endpoint, model access, key, quota, and parameters.") from None
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            if cancel.is_set():
                raise Cancelled("Stopped.") from None
            raise ProviderError(f"Provider connection failed ({type(exc).__name__}). Check connectivity and the base URL.") from None
        except (json.JSONDecodeError, UnicodeError, KeyError, TypeError, ValueError) as exc:
            if cancel.is_set():
                raise Cancelled("Stopped.") from None
            raise ProviderError(f"Malformed provider response ({type(exc).__name__}).") from None
        finally:
            with self._lock:
                self._response = None

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

    def _stream(self, response, emit, cancel) -> dict:
        message = {"content": "", "reasoning_content": ""}
        calls: dict[int, dict] = {}
        total = 0
        got_done = False
        finish_reason = None
        reasoning_announced = False
        event_lines: list[str] = []
        event_size = 0
        while True:
            if cancel.is_set():
                raise Cancelled("Stopped.")
            raw = response.readline(MAX_EVENT + 1)
            if not raw:
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
                got_done = True
                break
            obj = json.loads(event)
            if "error" in obj:
                raise ProviderError("Provider reported a streaming error. No partial tool call was executed.")
            if obj.get("usage"):
                emit({"event": "usage", "usage": obj["usage"]})
            choices = obj.get("choices", [])
            if not choices:
                continue
            choice = choices[0]
            finish_reason = choice.get("finish_reason") or finish_reason
            delta = choice.get("delta", {})
            if isinstance(delta.get("content"), str):
                message["content"] += delta["content"]
                emit({"event": "delta", "text": delta["content"]})
            if isinstance(delta.get("reasoning"), str) and delta["reasoning"]:
                message["reasoning"] = message.get("reasoning", "") + delta["reasoning"]
                if not reasoning_announced:
                    emit({"event": "status", "text": "Model is reasoning…"})
                    reasoning_announced = True
            if isinstance(delta.get("reasoning_content"), str):
                message["reasoning_content"] += delta["reasoning_content"]
                if not reasoning_announced:
                    emit({"event": "status", "text": "Model is reasoning…"})
                    reasoning_announced = True
            for chunk in delta.get("tool_calls", []):
                index = chunk.get("index")
                if not isinstance(index, int) or not 0 <= index < 16:
                    raise ProviderError("Invalid tool-call index.")
                call = calls.setdefault(index, {"id": "", "type": "function", "function": {"name": "", "arguments": ""}})
                if chunk.get("id"):
                    call["id"] = chunk["id"]
                func = chunk.get("function", {})
                call["function"]["name"] += func.get("name") or ""
                call["function"]["arguments"] += func.get("arguments") or ""
        if cancel.is_set():
            raise Cancelled("Stopped.")
        if not got_done and finish_reason not in {"stop", "tool_calls"}:
            raise ProviderError("Provider stream ended unexpectedly; partial tools were not executed.")
        if finish_reason in {"length", "content_filter"}:
            raise ProviderError("Response was truncated or filtered; partial tools were not executed. Increase output limit or narrow the task.")
        if calls:
            message["tool_calls"] = [calls[i] for i in sorted(calls)]
        return self._normalize(message)
