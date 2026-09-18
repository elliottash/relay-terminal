# SPDX-License-Identifier: GPL-3.0-or-later
"""Turn a client's chat-completion request into the one the gateway will send upstream.

The rule is an allow-list, not a deny-list: exactly the fields Relay's own transport sends are
accepted (``model, messages, tools, tool_choice, temperature, top_p, max_tokens, stream,
response_format, stream_options``) and the upstream body is *rebuilt* from them plus the role's
own ``extra``. Nothing the client sent is forwarded as-is: no unknown field, no header, no URL,
and no credential, so the gateway cannot be used to reach an upstream with someone else's key or
to smuggle a parameter the operator did not price for. ``model`` must be one of the configured
roles: a client cannot name an upstream model.

Reasoning effort is the one field the gateway rewrites rather than copies. The client may ask
with ``reasoning_effort`` (Relay's own transport: low|medium|high|max) or ``reasoning: {effort}``;
the ask is clamped to the role's ``max_effort`` silently (Relay Free runs at medium and below),
a role default fills in when nothing was asked, and the result is emitted in the upstream
provider's own dialect. ``thinking`` (the z.ai / Anthropic-style switch) is refused: it has no
effort scale to clamp.
"""
from __future__ import annotations

import json

from .config import EFFORTS, Config, Role, clamp_effort

ALLOWED_FIELDS = frozenset({"model", "messages", "tools", "tool_choice", "temperature", "top_p",
                            "max_tokens", "stream", "response_format", "stream_options",
                            "reasoning", "reasoning_effort"})
MESSAGE_FIELDS = frozenset({"role", "content", "tool_calls", "tool_call_id", "name"})
MESSAGE_ROLES = frozenset({"system", "developer", "user", "assistant", "tool"})
PART_TYPES = frozenset({"text", "image_url", "input_audio", "file"})
MAX_MESSAGES = 4096
MAX_TOOLS = 256
MAX_TOOL_CALLS = 64
MAX_NAME = 256


class BadRequest(ValueError):
    """The client's request is refused (HTTP 400, code ``bad_request``). The message names the
    field, never quotes its value, so it is safe to log and to show."""


class Validated:
    """The outcome: which role, the rebuilt upstream body (without ``model``, which each upstream
    sets), and the size estimate the quota is charged up front."""

    def __init__(self, role: Role, body: dict, input_estimate: int, effort: str):
        self.role = role
        self.body = body
        self.input_estimate = input_estimate
        self.effort = effort

    def upstream_body(self, model: str, extra: dict, effort_style: str = "reasoning_effort") -> dict:
        """The body for one upstream: the validated fields, the effort in the provider's dialect,
        then the role's ``extra`` on top, so the operator's temperature wins over the client's."""
        body = dict(self.body)
        if effort_style == "reasoning":
            body["reasoning"] = {"effort": self.effort}
        elif effort_style == "reasoning_effort":
            body["reasoning_effort"] = self.effort
        body.update(extra)
        body["model"] = model
        return body


def _effort(data: dict, role: Role) -> str:
    """The effort to run at: the client's ask clamped to the role's ceiling, else the default."""
    asked = None
    if "reasoning_effort" in data:
        asked = data["reasoning_effort"]
        if not isinstance(asked, str):
            raise BadRequest("reasoning_effort must be a string.")
    elif "reasoning" in data:
        reasoning = data["reasoning"]
        if not isinstance(reasoning, dict) or set(reasoning) - {"effort"}:
            raise BadRequest("reasoning may only carry effort.")
        if "effort" in reasoning:
            asked = reasoning["effort"]
            if not isinstance(asked, str):
                raise BadRequest("reasoning.effort must be a string.")
    if asked is None:
        return role.effort
    if asked not in EFFORTS:
        raise BadRequest("reasoning effort must be one of " + ", ".join(EFFORTS) + ".")
    return clamp_effort(asked, role.max_effort)


def _string(value, where: str, limit: int) -> str:
    if not isinstance(value, str):
        raise BadRequest(f"{where} must be a string.")
    if len(value) > limit:
        raise BadRequest(f"{where} is too long.")
    return value


def _content(value, where: str):
    if value is None or isinstance(value, str):
        return value
    if not isinstance(value, list):
        raise BadRequest(f"{where}.content must be a string or a list of parts.")
    for index, part in enumerate(value):
        here = f"{where}.content[{index}]"
        if not isinstance(part, dict):
            raise BadRequest(f"{here} must be an object.")
        kind = part.get("type")
        if not isinstance(kind, str) or kind not in PART_TYPES:
            raise BadRequest(f"{here}.type is not a known part type.")
        if kind == "text" and not isinstance(part.get("text"), str):
            raise BadRequest(f"{here}.text must be a string.")
        if kind == "image_url":
            image = part.get("image_url")
            if isinstance(image, dict):
                image = image.get("url")
            if not isinstance(image, str):
                raise BadRequest(f"{here}.image_url must carry a URL.")
    return value


def _tool_calls(value, where: str) -> list:
    if not isinstance(value, list) or len(value) > MAX_TOOL_CALLS:
        raise BadRequest(f"{where}.tool_calls must be a short list.")
    for index, call in enumerate(value):
        here = f"{where}.tool_calls[{index}]"
        if not isinstance(call, dict):
            raise BadRequest(f"{here} must be an object.")
        _string(call.get("id"), f"{here}.id", MAX_NAME)
        if call.get("type", "function") != "function":
            raise BadRequest(f"{here}.type must be 'function'.")
        function = call.get("function")
        if not isinstance(function, dict):
            raise BadRequest(f"{here}.function must be an object.")
        _string(function.get("name"), f"{here}.function.name", MAX_NAME)
        arguments = function.get("arguments", "")
        if not isinstance(arguments, str):
            raise BadRequest(f"{here}.function.arguments must be a JSON string.")
    return value


def _messages(value) -> list:
    if not isinstance(value, list) or not value:
        raise BadRequest("messages must be a non-empty list.")
    if len(value) > MAX_MESSAGES:
        raise BadRequest("messages has too many entries.")
    for index, message in enumerate(value):
        where = f"messages[{index}]"
        if not isinstance(message, dict):
            raise BadRequest(f"{where} must be an object.")
        unknown = set(message) - MESSAGE_FIELDS
        if unknown:
            raise BadRequest(f"{where} has a field that is not allowed: {sorted(unknown)[0]}.")
        role = message.get("role")
        if role not in MESSAGE_ROLES:
            raise BadRequest(f"{where}.role is not a known role.")
        content = _content(message.get("content"), where)
        if "tool_calls" in message:
            if role != "assistant":
                raise BadRequest(f"{where}.tool_calls is only valid on an assistant message.")
            _tool_calls(message["tool_calls"], where)
        elif content is None:
            raise BadRequest(f"{where}.content is required.")
        if "tool_call_id" in message:
            if role != "tool":
                raise BadRequest(f"{where}.tool_call_id is only valid on a tool message.")
            _string(message["tool_call_id"], f"{where}.tool_call_id", MAX_NAME)
        elif role == "tool":
            raise BadRequest(f"{where}.tool_call_id is required on a tool message.")
        if "name" in message:
            _string(message["name"], f"{where}.name", MAX_NAME)
    return value


def _tools(value) -> list:
    if not isinstance(value, list) or len(value) > MAX_TOOLS:
        raise BadRequest("tools must be a list.")
    for index, tool in enumerate(value):
        where = f"tools[{index}]"
        if not isinstance(tool, dict) or tool.get("type") != "function":
            raise BadRequest(f"{where} must be a function tool.")
        function = tool.get("function")
        if not isinstance(function, dict):
            raise BadRequest(f"{where}.function must be an object.")
        _string(function.get("name"), f"{where}.function.name", MAX_NAME)
        if "description" in function and not isinstance(function["description"], str):
            raise BadRequest(f"{where}.function.description must be a string.")
        if "parameters" in function and not isinstance(function["parameters"], dict):
            raise BadRequest(f"{where}.function.parameters must be an object.")
        if "strict" in function and not isinstance(function["strict"], bool):
            raise BadRequest(f"{where}.function.strict must be a boolean.")
    return value


def _tool_choice(value):
    if isinstance(value, str):
        if value not in ("auto", "none", "required"):
            raise BadRequest("tool_choice must be auto, none, required or a function.")
        return value
    if isinstance(value, dict) and value.get("type") == "function" \
            and isinstance(value.get("function"), dict):
        _string(value["function"].get("name"), "tool_choice.function.name", MAX_NAME)
        return {"type": "function", "function": {"name": value["function"]["name"]}}
    raise BadRequest("tool_choice must be auto, none, required or a function.")


def _fraction(value, name: str, upper: float) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise BadRequest(f"{name} must be a number.")
    if not 0 <= value <= upper:
        raise BadRequest(f"{name} must be between 0 and {upper:g}.")
    return float(value)


def _response_format(value) -> dict:
    if not isinstance(value, dict) or not isinstance(value.get("type"), str):
        raise BadRequest("response_format must be an object with a type.")
    if value["type"] not in ("text", "json_object", "json_schema"):
        raise BadRequest("response_format.type is not a known type.")
    if value["type"] == "json_schema" and not isinstance(value.get("json_schema"), dict):
        raise BadRequest("response_format.json_schema must be an object.")
    return value


def validate(raw: bytes, config: Config) -> Validated:
    """Parse and check a request body. Raises ``BadRequest`` with a field name, never a value."""
    try:
        data = json.loads(raw)
    except ValueError:
        raise BadRequest("the body is not JSON.") from None
    if not isinstance(data, dict):
        raise BadRequest("the body must be a JSON object.")
    model = data.get("model")
    if not isinstance(model, str) or model not in config.roles:
        raise BadRequest("model must be one of " + ", ".join(sorted(config.roles)) + ".")
    role = config.roles[model]
    if len(raw) > role.max_input_chars:
        raise BadRequest(f"the request is larger than {model} accepts "
                         f"({role.max_input_chars} bytes). Start a new conversation.")
    unknown = set(data) - ALLOWED_FIELDS
    if unknown:
        raise BadRequest(f"a field that is not allowed: {sorted(unknown)[0]}.")

    body: dict = {"messages": _messages(data.get("messages")), "stream": True,
                  "stream_options": {"include_usage": True}}
    if "tools" in data:
        tools = _tools(data["tools"])
        if tools:                           # some upstreams reject "tools": []
            body["tools"] = tools
    if "tool_choice" in data:
        body["tool_choice"] = _tool_choice(data["tool_choice"])
    if "temperature" in data:
        body["temperature"] = _fraction(data["temperature"], "temperature", 2.0)
    if "top_p" in data:
        body["top_p"] = _fraction(data["top_p"], "top_p", 1.0)
    if "response_format" in data:
        body["response_format"] = _response_format(data["response_format"])
    max_tokens = data.get("max_tokens", role.max_output_tokens)
    if isinstance(max_tokens, bool) or not isinstance(max_tokens, int) or max_tokens < 1:
        raise BadRequest("max_tokens must be a positive integer.")
    body["max_tokens"] = min(max_tokens, role.max_output_tokens)
    # ``stream`` and ``stream_options`` are accepted so a client that sets them is not refused,
    # but the values are the gateway's: it only ever streams, and it needs the usage chunk.
    return Validated(role, body, max(1, len(raw) // 4), _effort(data, role))
