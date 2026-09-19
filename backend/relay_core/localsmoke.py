# SPDX-License-Identifier: AGPL-3.0-or-later
"""Five checks that decide whether a model server on this machine can drive the agent.

A probe (``localmodels.probe``) says what is serving and what window it was started with. It cannot
say whether the model *works as an agent*: that is decided by the chat template, the quantisation
and the build, and every way it goes wrong looks like a working server. The three failures seen on
this machine and reported against every other harness are:

- a tool call written into ``content`` as text (no ``--jinja``, or a template with no tool section);
- a template that handles the *first* tool call and corrupts the second or third, so a turn that
  looks fine dies halfway through a task;
- a template whose tool parser fires on the literal string ``<tool_call>`` in an ordinary answer.

So the checks run Relay's own transport (``ChatProvider`` with the ``ProviderConfig`` that
``localmodels.provider_fields`` builds for this endpoint), against the real server, on loopback
only. Whatever passes here is what the agent gets: same envelope repair, same reasoning split, same
deadlines. Nothing is written and no tool is executed - the "tools" exist only in the request.
"""
from __future__ import annotations

import json
import threading
import time
from dataclasses import dataclass, field

from . import localmodels
from .provider import ChatProvider, ProviderConfig, loopback_http

DEFAULT_MAX_TOKENS = 4096       # room for a reasoning model to think before a short answer
DEFAULT_STALL_S = 90.0
MAX_REASON = 300

# One tool, twice. `lookup_number` is deliberately not a real capability: a model that hallucinates
# the answer instead of calling it is telling us something too, and nothing here can be executed.
TOOL = {"type": "function", "function": {
    "name": "lookup_number",
    "description": "Look up the number stored under a key. The only way to learn a stored number.",
    "parameters": {"type": "object", "properties": {"key": {"type": "string", "description": "The key to look up."}},
                   "required": ["key"], "additionalProperties": False}}}
TOOLS = [TOOL]
SYSTEM = "You are a tool-using assistant. When a tool can answer, call it instead of guessing."
ASK_FIRST = 'Look up the number stored under the key "alpha" with the lookup_number tool.'
ASK_SECOND = ('Now look up the key "beta" the same way, then tell me the sum of the two numbers in '
              'one sentence.')
FIRST_RESULT = '{"key": "alpha", "value": 7}'
SECOND_RESULT = '{"key": "beta", "value": 5}'
ASK_TAGS = ('In plain prose, explain what the literal strings <tool_call> and </think> mean when a '
            'model writes them into its answer. Two sentences. Do not call a tool.')
ASK_PLAIN = "Reply with the two words: ready now"

# What a tool call written as text looks like, by the harness that produced it: Hermes/Qwen
# <tool_call>, Qwen3-Coder's XML, LM Studio's brackets, a fenced JSON block naming a tool.
TEXT_MARKERS = ("<tool_call>", "</tool_call>", "<function=", "[TOOL_REQUEST]", "<|tool_call")
TEXT_HINT = "tool call arrived as text; consider tool_text_recovery or fix the server template"

TITLES = {
    "answers": "the server answers at all",
    "tool_call": "a native tool call with valid JSON arguments",
    "two_calls": "two consecutive tool calls, then a plain answer",
    "tags_in_prose": "tool and reasoning tags survive inside an answer",
    "usage": "token usage is reported and plausible",
}
ORDER = ("answers", "tool_call", "two_calls", "tags_in_prose", "usage")


def _one_line(text) -> str:
    return " ".join(str(text).split())[:MAX_REASON]


@dataclass
class Check:
    name: str
    ok: bool = False
    reason: str = ""
    skipped: bool = False
    seconds: float = 0.0

    def to_dict(self) -> dict:
        return {"name": self.name, "title": TITLES.get(self.name, self.name), "ok": self.ok,
                "skipped": self.skipped, "reason": self.reason, "seconds": round(self.seconds, 2)}


@dataclass
class Smoke:
    """One run against one endpoint. ``run()`` never raises for anything the server does."""

    base_url: str
    model: str = ""
    endpoint_id: str | None = None
    max_tokens: int = DEFAULT_MAX_TOKENS
    stall_timeout: float = DEFAULT_STALL_S
    extra: dict = field(default_factory=dict)

    def __post_init__(self):
        self.checks: dict[str, Check] = {name: Check(name) for name in ORDER}
        self.usage: list[dict] = []
        self.sent_chars = 0
        self.notes: list[str] = []
        self._provider = None
        self._conversation: list[dict] = []

    # ----- one request ---------------------------------------------------------------------
    def _call(self, messages: list[dict], tools: list[dict] | None) -> dict:
        events: list[dict] = []
        # The tool schemas are part of the prompt the server templates, so they are part of what a
        # plausible prompt_tokens is measured against.
        self.sent_chars = len(json.dumps(messages, ensure_ascii=False)) \
            + len(json.dumps(tools or [], ensure_ascii=False))
        message = self._provider.complete(messages, tools or [], events.append, threading.Event())
        for event in events:
            if event.get("event") == "usage" and isinstance(event.get("usage"), dict):
                self.usage.append(event["usage"])
        return message

    @staticmethod
    def _call_problem(message: dict, offered: list[dict]) -> str:
        """Why this reply is not one usable tool call, or "" when it is."""
        calls = message.get("tool_calls") or []
        if not calls:
            content = message.get("content") or ""
            if any(marker in content for marker in TEXT_MARKERS):
                return TEXT_HINT
            if content.strip().startswith("{") and '"name"' in content:
                return TEXT_HINT
            return "no tool call was made; the model answered in prose instead"
        if len(calls) > 1:
            return (f"{len(calls)} tool calls came back at once; the agent sends "
                    "parallel_tool_calls: false, so the server ignored it")
        function = calls[0].get("function") or {}
        names = [t["function"]["name"] for t in offered]
        if function.get("name") not in names:
            return f"called {function.get('name')!r}, which was not one of the tools offered ({', '.join(names)})"
        try:
            arguments = json.loads(function.get("arguments") or "")
        except (ValueError, TypeError):
            return f"arguments are not valid JSON: {_one_line(function.get('arguments'))[:120]}"
        if not isinstance(arguments, dict):
            return "arguments parsed to " + type(arguments).__name__ + ", not an object"
        if "key" not in arguments:
            return f"arguments {json.dumps(arguments)[:120]} are missing the required 'key' property"
        return ""

    @staticmethod
    def _result_message(message: dict, content: str) -> list[dict]:
        call = message["tool_calls"][0]
        return [{"role": "assistant", "content": message.get("content") or "", "tool_calls": message["tool_calls"]},
                {"role": "tool", "tool_call_id": call["id"], "name": call["function"]["name"], "content": content}]

    # ----- the checks ----------------------------------------------------------------------
    def _check(self, name: str, work) -> bool:
        check = self.checks[name]
        started = time.monotonic()
        try:
            check.reason = work()
            check.ok = not check.reason
            if check.ok and not check.reason:
                check.reason = "ok"
        except Exception as exc:                      # a provider failure is a failed check, not a crash
            check.ok = False
            check.reason = _one_line(exc) or type(exc).__name__
        check.seconds = time.monotonic() - started
        return check.ok

    def _skip(self, name: str, reason: str) -> None:
        check = self.checks[name]
        check.skipped, check.ok, check.reason = True, False, reason

    def _answers(self) -> str:
        message = self._call([{"role": "user", "content": ASK_PLAIN}], None)
        if not (message.get("content") or "").strip():
            if (message.get("reasoning_content") or "").strip():
                return "the reply was all reasoning and no answer; content was empty"
            return "the server answered with empty content"
        return ""

    def _tool_call(self) -> str:
        self._conversation = [{"role": "system", "content": SYSTEM}, {"role": "user", "content": ASK_FIRST}]
        message = self._call(self._conversation, TOOLS)
        problem = self._call_problem(message, TOOLS)
        if problem:
            return problem
        self._conversation += self._result_message(message, FIRST_RESULT)
        return ""

    def _two_calls(self) -> str:
        self._conversation.append({"role": "user", "content": ASK_SECOND})
        message = self._call(self._conversation, TOOLS)
        problem = self._call_problem(message, TOOLS)
        if problem:
            return "the second call in the same conversation: " + problem
        self._conversation += self._result_message(message, SECOND_RESULT)
        final = self._call(self._conversation, TOOLS)
        if final.get("tool_calls"):
            return "after both tool results the model called a tool again instead of answering"
        if not (final.get("content") or "").strip():
            return "after both tool results the model answered with empty content"
        return ""

    def _tags_in_prose(self) -> str:
        message = self._call([{"role": "system", "content": SYSTEM}, {"role": "user", "content": ASK_TAGS}], TOOLS)
        content = (message.get("content") or "").strip()
        if message.get("tool_calls"):
            function = (message["tool_calls"][0].get("function") or {})
            made = f'{function.get("name")}({_one_line(function.get("arguments"))[:60]})'
            return (f"the tags in the question were parsed as a tool call, {made}: the server's parser "
                    "fires on text the user can type")
        if not content:
            where = "reasoning_content" if (message.get("reasoning_content") or "").strip() else "nowhere"
            return f"the explanation did not arrive in content (it went to {where})"
        if len(content) < 20:
            return f"the answer was cut down to {len(content)} characters: {_one_line(content)}"
        if "<tool_call>" in content and "</think>" in content:
            self.notes.append("both literal tags survived in the answer")
        return ""

    def _usage(self) -> str:
        counted = [u for u in self.usage if isinstance(u.get("prompt_tokens"), int)]
        if not counted:
            return ("no usage was reported; the context tracker would fall back to guessing four "
                    "characters a token")
        prompt = counted[-1]["prompt_tokens"]
        if prompt <= 0:
            return f"prompt_tokens was {prompt}"
        if not isinstance(counted[-1].get("completion_tokens"), int):
            return "usage carried prompt_tokens but no completion_tokens"
        low, high = max(1, self.sent_chars // 20), max(8, self.sent_chars)
        if not low <= prompt <= high:
            return (f"prompt_tokens {prompt:,} is not plausible for {self.sent_chars:,} characters of "
                    f"messages (expected {low:,}-{high:,})")
        self.notes.append(f"last turn: {prompt:,} prompt tokens for {self.sent_chars:,} characters")
        return ""

    # ----- the run -------------------------------------------------------------------------
    def result(self, ok: bool, error: str = "") -> dict:
        out = {"base_url": self.base_url, "model": self.model, "ok": ok,
               "checks": [self.checks[name].to_dict() for name in ORDER], "notes": list(self.notes)}
        if error:
            out["error"] = error
        return out

    def run(self) -> dict:
        base_url = str(self.base_url or "").strip()
        if not loopback_http(base_url):
            # Refused before a socket is opened: a smoke test sends prompts, and these only ever go
            # to a server on this machine.
            for name in ORDER:
                self._skip(name, "not run")
            return self.result(False, "Relay only smoke-tests a model server on this machine: "
                                      "http://localhost, http://127.0.0.1 or http://[::1].")
        _, self.base_url = localmodels.roots(base_url)
        endpoint = localmodels.resolve(self.endpoint_id, self.base_url, self.model)
        if endpoint is not None and not self.model:
            self.model = endpoint.model
        if endpoint is not None and not self.extra:
            self.extra = dict(endpoint.extra)
        if not self.model:
            found = localmodels.probe(self.base_url)
            if not found.ok:
                for name in ORDER:
                    self._skip(name, "not run")
                return self.result(False, found.error or "Nothing is serving there.")
            if len(found.models) != 1:
                for name in ORDER:
                    self._skip(name, "not run")
                served = ", ".join(m["id"] for m in found.models[:10]) or "none"
                return self.result(False, f"Name the model with --model. The server lists: {served}.")
            self.model = found.models[0]["id"]

        fields = localmodels.provider_fields(self.endpoint_id or (endpoint.id if endpoint else None),
                                             self.base_url, self.model)
        try:
            config = ProviderConfig(self.base_url, self.model, "", dict(self.extra),
                                    localmodels.clamp_max_tokens(self.max_tokens, fields), **fields)
            self._provider = ChatProvider(config, self.stall_timeout)
        except ValueError as exc:
            for name in ORDER:
                self._skip(name, "not run")
            return self.result(False, _one_line(exc))

        if not self._check("answers", self._answers):
            for name in ("tool_call", "two_calls", "tags_in_prose"):
                self._skip(name, "not run: the server did not answer a plain question")
            self._check("usage", self._usage)
            return self.result(False)
        if self._check("tool_call", self._tool_call):
            self._check("two_calls", self._two_calls)
        else:
            self._skip("two_calls", "not run: the first tool call did not come back")
        self._check("tags_in_prose", self._tags_in_prose)
        self._check("usage", self._usage)
        return self.result(all(c.ok for c in self.checks.values()))


def smoke(base_url: str, model: str = "", **options) -> dict:
    """Run the five checks. Returns the result dict; never raises for what the server does."""
    return Smoke(base_url, model, **options).run()


def exit_code(result: dict) -> int:
    """0 every check passed, 1 a check failed, 2 nothing was run (refused, or no server)."""
    if result.get("ok"):
        return 0
    return 2 if all(c.get("skipped") for c in result.get("checks", [])) else 1


def report(result: dict) -> str:
    """The lines the CLI prints: one per check, then a verdict."""
    lines = [f'{result["base_url"]}  {result["model"] or "(no model)"}']
    if result.get("error"):
        lines.append("  " + result["error"])
    for check in result.get("checks", []):
        mark = "pass" if check["ok"] else ("skip" if check["skipped"] else "FAIL")
        seconds = f' {check["seconds"]:.1f}s' if check["seconds"] else ""
        lines.append(f'  [{mark}] {check["title"]}{seconds}')
        if not check["ok"] or check["reason"] not in ("", "ok"):
            lines.append(f'         {check["reason"]}')
    for note in result.get("notes", []):
        lines.append("  note: " + note)
    passed = sum(1 for c in result.get("checks", []) if c["ok"])
    lines.append(f'{passed}/{len(result.get("checks", []))} checks passed'
                 + ("." if result.get("ok") else ". Read the failures above before registering it."))
    return "\n".join(lines)
