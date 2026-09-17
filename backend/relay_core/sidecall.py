# SPDX-License-Identifier: GPL-3.0-or-later
"""No-tools model calls used for summaries, recaps, suggestions and instruction synthesis.

These never stream text to the GUI and never offer tools. Transcripts are rendered as plain,
size-capped text so the call cannot exceed the model window by itself.
"""
from __future__ import annotations

import json
import re
import threading

TRANSCRIPT_MESSAGE_CAP = 4000


def call(provider, system: str, user: str, cancel: threading.Event | None = None) -> tuple[str, dict | None]:
    """Run one no-tools completion. Returns (text, usage)."""
    usage: dict = {}

    def quiet(event: dict) -> None:
        if event.get("event") == "usage" and isinstance(event.get("usage"), dict):
            usage.update(event["usage"])

    message = provider.complete([{"role": "system", "content": system}, {"role": "user", "content": user}],
                                [], quiet, cancel or threading.Event())
    return (message.get("content") or "").strip(), (usage or None)


def render_transcript(messages: list[dict], max_chars: int = 200_000,
                      per_message: int = TRANSCRIPT_MESSAGE_CAP, keep: str = "tail") -> str:
    """Plain-text rendering of chat messages (system prompt excluded). Keeps the tail when capped."""
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
        if len(content) > per_message:
            content = content[: per_message // 2] + "\n[…trimmed…]\n" + content[-per_message // 2:]
        parts.append(f"### {label}\n{content.strip()}")
    text = "\n\n".join(parts)
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
