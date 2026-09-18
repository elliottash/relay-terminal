# SPDX-License-Identifier: GPL-3.0-or-later
"""What a local model writes into ``content`` that is not the answer: reasoning tags and tool calls.

A hosted provider separates these before Relay sees them. A local server does it only when it was
started the right way (llama.cpp ``--jinja --reasoning-format deepseek``) and the model kept to its
template; otherwise ``<think>`` blocks and ``<tool_call>`` blocks arrive as plain text. Aider strips
the first client-side, LM Studio documents the second ("they'll instead appear in content"), Goose
and Cline each carry a parser for it. This module is both, with no I/O, used by provider.py only
for an endpoint marked local.

Tool-call recovery is off unless the endpoint turns it on (card #24XJ): it runs a command out of
text, so it takes the whole message or nothing.
"""
from __future__ import annotations

import json
import re

REASONING_TAGS = ("think", "thinking", "reasoning")
_OPEN = tuple(f"<{t}>" for t in REASONING_TAGS)
_CLOSE = tuple(f"</{t}>" for t in REASONING_TAGS)
_TAG = re.compile(r"</?(?:" + "|".join(REASONING_TAGS) + r")>", re.IGNORECASE)
_LONGEST = max(len(t) for t in _CLOSE)


def _held_suffix(text: str) -> int:
    """How many trailing characters could still turn into a tag once the next chunk arrives."""
    start = text.rfind("<", max(0, len(text) - _LONGEST))
    if start < 0:
        return 0
    tail = text[start:].lower()
    return len(tail) if any(tag.startswith(tail) for tag in _OPEN + _CLOSE) else 0


class ThinkSplitter:
    """Splits streamed text into answer and reasoning, one chunk at a time.

    ``feed`` returns ``[("content" | "thinking", text), ...]`` in order. A tag cut in two by a chunk
    boundary is held until it is whole. An opener that is never closed makes everything after it
    reasoning: that is a reply cut off mid-thought, and showing it as the answer would be wrong.

    A closer with no opener cannot be fixed while streaming, because the text before it has already
    been shown; ``split_reasoning`` on the finished text handles that case for what is kept.
    """

    def __init__(self):
        self.thinking = False
        self._held = ""

    def feed(self, text: str) -> list[tuple[str, str]]:
        text, self._held = self._held + text, ""
        hold = _held_suffix(text)
        if hold:
            text, self._held = text[:-hold], text[-hold:]
        return self._split(text)

    def flush(self) -> list[tuple[str, str]]:
        text, self._held = self._held, ""
        return [("thinking" if self.thinking else "content", text)] if text else []

    def _split(self, text: str) -> list[tuple[str, str]]:
        out, position = [], 0
        for found in _TAG.finditer(text):
            closing = found.group().startswith("</")
            if closing != self.thinking:
                continue                  # an opener inside reasoning, or a stray closer: plain text
            if found.start() > position:
                out.append(("thinking" if self.thinking else "content", text[position:found.start()]))
            self.thinking = not closing
            position = found.end()
        if position < len(text):
            out.append(("thinking" if self.thinking else "content", text[position:]))
        return out


def split_reasoning(text: str) -> tuple[str, str]:
    """``(answer, reasoning)`` for a finished reply.

    Beyond what the streaming splitter does, this handles the closer that has no opener: a chat
    template that opens ``<think>`` in the prompt makes the model emit only ``</think>``, and
    everything before it was reasoning.
    """
    if not isinstance(text, str) or "<" not in text:
        return text if isinstance(text, str) else "", ""
    splitter = ThinkSplitter()
    parts = splitter.feed(text) + splitter.flush()
    answer = "".join(t for kind, t in parts if kind == "content")
    reasoning = "".join(t for kind, t in parts if kind == "thinking")
    stray = next((m for m in _TAG.finditer(answer) if m.group().startswith("</")), None)
    if stray is not None:
        reasoning = answer[:stray.start()] + reasoning
        answer = answer[stray.end():]
    return answer.strip("\n") if reasoning else answer, reasoning.strip()


# ----- tool calls written as text ------------------------------------------------------------
MAX_RECOVERED = 16
_BLOCKS = (
    re.compile(r"<tool_call>\s*(.*?)\s*</tool_call>", re.DOTALL),               # Hermes, Qwen
    re.compile(r"\[TOOL_REQUEST\]\s*(.*?)\s*\[END_TOOL_REQUEST\]", re.DOTALL),   # LM Studio default mode
    re.compile(r"```(?:json|tool_call)?\s*\n(.*?)\n\s*```", re.DOTALL),          # a fenced block
)
# Qwen3-Coder's template: <function=name><parameter=key>value</parameter></function>
_XML_FUNCTION = re.compile(r"^<function=([^>\s]+)>\s*(.*?)\s*</function>$", re.DOTALL)
_XML_PARAMETER = re.compile(r"<parameter=([^>\s]+)>\n?(.*?)\n?</parameter>", re.DOTALL)


def _schemas(tools: list[dict]) -> dict[str, dict]:
    out = {}
    for tool in tools or []:
        func = tool.get("function") if isinstance(tool, dict) else None
        if isinstance(func, dict) and isinstance(func.get("name"), str):
            params = func.get("parameters") if isinstance(func.get("parameters"), dict) else {}
            out[func["name"]] = params.get("properties") if isinstance(params.get("properties"), dict) else {}
    return out


def _from_json(body: str):
    try:
        obj = json.loads(body)
    except ValueError:
        return None
    if not isinstance(obj, dict) or not isinstance(obj.get("name"), str):
        return None
    arguments = obj.get("arguments", obj.get("parameters", {}))
    if isinstance(arguments, str):
        try:
            arguments = json.loads(arguments)
        except ValueError:
            return None
    if not isinstance(arguments, dict) or set(obj) - {"name", "arguments", "parameters", "id", "type"}:
        return None
    return obj["name"], arguments


def _from_xml(body: str, schemas: dict[str, dict]):
    found = _XML_FUNCTION.match(body)
    if not found:
        return None
    name, inner = found.group(1), found.group(2)
    if _XML_PARAMETER.sub("", inner).strip():
        return None                       # something between the parameters that is not a parameter
    arguments = {}
    for key, value in _XML_PARAMETER.findall(inner):
        declared = (schemas.get(name) or {}).get(key)
        if isinstance(declared, dict) and declared.get("type") not in (None, "string"):
            try:
                value = json.loads(value)   # the template writes every value as text
            except ValueError:
                pass
        arguments[key] = value
    return name, arguments


def recover_tool_calls(content: str, tools: list[dict]) -> list[dict] | None:
    """Tool calls a model wrote as text, or None when the text is anything else.

    All or nothing, on purpose. The message must consist of call blocks and whitespace only, every
    block must parse, and every name must be a tool that was offered. An answer that *mentions* a
    call, or shows JSON to the user, is an answer.
    """
    if not isinstance(content, str) or not content.strip() or not tools:
        return None
    schemas = _schemas(tools)
    text = content.strip()
    bodies = None
    for pattern in _BLOCKS:
        matches = list(pattern.finditer(text))
        if matches and not pattern.sub("", text).strip():
            bodies = [m.group(1).strip() for m in matches]
            break
    if bodies is None:
        bodies = [text] if text.startswith("{") and text.endswith("}") else None
    if not bodies or len(bodies) > MAX_RECOVERED:
        return None
    calls = []
    for index, body in enumerate(bodies):
        parsed = _from_json(body) if body.startswith("{") else _from_xml(body, schemas)
        if parsed is None or parsed[0] not in schemas:
            return None
        calls.append({"id": f"call_text_{index}", "type": "function",
                      "function": {"name": parsed[0], "arguments": json.dumps(parsed[1], ensure_ascii=False)}})
    return calls
