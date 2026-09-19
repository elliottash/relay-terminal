# SPDX-License-Identifier: GPL-3.0-or-later
"""No-tools model calls used for summaries, recaps, suggestions and instruction synthesis.

These never stream text to the GUI and never offer tools. Transcripts are rendered as plain,
size-capped text so the call cannot exceed the model window by itself.
"""
from __future__ import annotations

import contextlib
import json
import re
import threading

TRANSCRIPT_MESSAGE_CAP = 4000
# How long a side call may spend being told "not now" before it gives up. A title, a recap,
# compaction and route_assist stream nothing to the pane and have nowhere to show a wait, so the
# transport's default retry budget — twice the first-token deadline, because a turn is worth
# waiting for — is far too generous here: a provider answering 429 with ``Retry-After: 60`` used
# to hold one of these for six minutes (review of #VMZP).
RETRY_BUDGET_S = 20.0


def call(provider, system: str, user: str, cancel: threading.Event | None = None,
         retry_budget_s: float | None = RETRY_BUDGET_S) -> tuple[str, dict | None]:
    """Run one no-tools completion. Returns (text, usage).

    ``retry_budget_s`` caps the wall clock the transport's HTTP retries may spend; None leaves the
    provider's own budget alone.
    """
    usage: dict = {}

    def quiet(event: dict) -> None:
        if event.get("event") == "usage" and isinstance(event.get("usage"), dict):
            usage.update(event["usage"])

    with _retry_budget(provider, retry_budget_s):
        message = provider.complete([{"role": "system", "content": system}, {"role": "user", "content": user}],
                                    [], quiet, cancel or threading.Event())
    return (message.get("content") or "").strip(), (usage or None)


@contextlib.contextmanager
def _retry_budget(provider, seconds: float | None):
    """Narrow the transport's retry budget for the block.

    Matched by type rather than by ``hasattr``: half the callers here are given a test double, and
    a bare ``Mock`` answers every attribute with another Mock.
    """
    from .provider import ChatProvider
    if seconds is None or not isinstance(provider, ChatProvider):
        yield
        return
    with provider.limit_retry_budget(seconds):
        yield


def render_transcript(messages: list[dict], max_chars: int = 200_000,
                      per_message: int = TRANSCRIPT_MESSAGE_CAP, keep: str = "tail", keep_user: bool = False) -> str:
    """Plain-text rendering of chat messages (system prompt excluded). Keeps the tail when capped.

    keep_user (compaction, research G7): user messages are never middle-trimmed, and when the whole
    transcript is over max_chars the oldest non-user messages are dropped before any user message."""
    parts = []
    for message in messages:
        role = message.get("role")
        if role == "system":
            continue
        content = message.get("content") or ""
        if not isinstance(content, str):
            content = json.dumps(content, ensure_ascii=False)
        if role == "tool":
            label = "TOOL RESULT"
        elif role == "assistant":
            label = "ASSISTANT"
            for call_ in message.get("tool_calls") or []:
                func = call_.get("function", {})
                content += f"\n[tool call {func.get('name')}: {str(func.get('arguments'))[:600]}]"
        else:
            label = "USER"
        if len(content) > per_message and not (keep_user and label == "USER"):
            content = content[: per_message // 2] + "\n[…trimmed…]\n" + content[-per_message // 2:]
        parts.append((label, f"### {label}\n{content.strip()}"))
    total = sum(len(text) + 2 for _, text in parts)
    if keep_user and total > max_chars:
        kept, dropped = [], 0
        for label, text in parts:
            if total > max_chars and label != "USER":
                total -= len(text) + 2
                dropped += 1
                continue
            kept.append((label, text))
        if dropped:
            kept.insert(0, ("NOTE", f"[…{dropped} older assistant and tool messages trimmed; user messages kept…]"))
        parts = kept
    text = "\n\n".join(t for _, t in parts)
    if len(text) > max_chars:
        text = ("[…earlier transcript trimmed…]\n" + text[-max_chars:]) if keep == "tail" else text[:max_chars]
    return text


def parse_json_object(text: str) -> dict | None:
    """Extract the first JSON object from a model reply (tolerates code fences and prose)."""
    text = text.strip()
    fence = re.search(r"```(?:json)?\s*(\{.*?\})\s*```", text, re.S)
    candidates = [fence.group(1)] if fence else []
    start, end = text.find("{"), text.rfind("}")
    if start != -1 and end > start:
        candidates.append(text[start:end + 1])
    for candidate in candidates:
        try:
            value = json.loads(candidate)
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict):
            return value
    return None


def clip(text: str, limit: int) -> str:
    """Cut at a word boundary."""
    text = " ".join((text or "").split()) if "\n" not in (text or "") else (text or "").strip()
    if len(text) <= limit:
        return text
    cut = text[: limit - 1]
    space = cut.rfind(" ")
    if space > limit * 0.6:
        cut = cut[:space]
    return cut.rstrip() + "…"
