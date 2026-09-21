# SPDX-License-Identifier: AGPL-3.0-or-later
"""Plan mode (Warp-style): investigate, ask what is genuinely ambiguous (#MQ9C), then write one Markdown plan."""
from __future__ import annotations

import os
import re
import tempfile
import time
from pathlib import Path

MODES = ("build", "plan")
# The only file write in plan mode is the plan itself, through write_plan. The subagent tools are
# here since #GMCF: a subagent may write files, so a plan turn never started one, which used to be
# done by leaving all three out of the tool list. The list is the same in both modes now (a tool
# that comes and goes costs the whole cached prefix), so the refusal is what keeps them out.
PLAN_BLOCKED_TOOLS = {"write_file", "edit_file", "set_keybinding",
                      "agent", "agent_message", "agent_wait"}
MAX_PLAN_BYTES = 131072
MAX_TITLE = 200

PLAN_MODE_NOTE = """

PLAN MODE is active. Investigate before proposing changes: you may read files, list directories, load skills and run commands, but commands must be read-only (no edits, installs, git commits, deletions, or writes of any kind). Do not modify the workspace.
Ask the user clarifying questions with ask_user rather than making large assumptions about what they want. Once you have read enough to know what is actually ambiguous — which of two directions, how far the change goes, a trade-off worth their opinion, wording only they can choose — ask it, in one call, before you write the plan. Give options when the decision has a few known branches and leave them out when it does not; an open question is better than three invented choices. Do not ask what the code can tell you, and do not ask whether the plan is any good: write it and let them edit it.
When you understand the task, call write_plan exactly once with a short title and a complete Markdown plan: goal, findings with exact file paths, numbered steps, risks, and how to verify. When the work is big enough to split across subagents, the plan also carries an Orchestration block: each subagent (its type and a one-line task), which steps run in parallel, and which wait for which. Only steps that touch no shared files may run in parallel, and writes stay with the main agent; a small plan gets no block. The user reviews and edits the plan file before anything is executed. When ready to implement, call exit_plan_mode with a concise reason to offer Execute or Keep planning. Continue implementation only if its result says approved and build mode is active. A declined, dismissed or unanswered request leaves plan mode active; do not repeat the request. Otherwise reply with a two-sentence summary."""

EXIT_PLAN_MODE_SPEC = {"type": "function", "function": {
    "name": "exit_plan_mode",
    "description": "Request the user's permission to leave plan mode and execute. Explain what is ready to execute; only an explicit Execute answer enables build mode.",
    "parameters": {"type": "object", "properties": {
        "reason": {"type": "string", "maxLength": 250, "description": "What is ready to execute and why planning is complete."}},
        "required": ["reason"], "additionalProperties": False}}}


def exit_plan_question(args) -> dict:
    if not isinstance(args, dict) or set(args) != {"reason"}:
        raise ValueError("exit_plan_mode takes one argument, reason.")
    reason = args["reason"]
    if not isinstance(reason, str) or not reason.strip() or "\x00" in reason or len(reason) > 250:
        raise ValueError("reason must be non-empty text of at most 250 characters.")
    return {"questions": [{
        "header": "Exit plan mode",
        "question": reason.strip() + "\nLeave planning mode and execute?",
        "options": [
            {"label": "Execute", "description": "Switch to build mode and continue with implementation."},
            {"label": "Keep planning", "description": "Stay in plan mode without enabling edits."}],
    }]}


WRITE_PLAN_SPEC = {"type": "function", "function": {
    "name": "write_plan",
    "description": "Plan mode only: save the finished implementation plan as a Markdown file the user can edit and execute.",
    "parameters": {"type": "object", "properties": {
        "title": {"type": "string", "description": "Short plan title (a few words)."},
        "content": {"type": "string", "description": "The complete plan in Markdown."}},
        "required": ["title", "content"], "additionalProperties": False}}}


# ----- a plan turn on a guest harness (protocol 13.7, owner 2026-09-20) --------------------------
# A `guest:` entry of the High list (Claude Code, Codex) serves a plan turn by starting the guest's
# harness for that one turn. The guest has none of Relay's tools — no `ask_user`, no `write_plan` —
# and none of the conversation, so its first prompt carries the rules in its own terms, the
# transcript so far, and the request; its reply *is* the plan, and the agent saves it exactly as
# `write_plan` would have (`Agent._save_guest_plan`).
GUEST_PLAN_NOTE = """PLAN MODE. You are joining this conversation for one planning turn. Investigate before proposing changes: read files, list directories and run read-only commands, but do not modify the workspace (no edits, installs, git commits, deletions, or writes of any kind), and do not ask questions — where something is genuinely ambiguous, state the assumption you are making in the plan.
When you understand the task, reply with the complete implementation plan as Markdown and nothing else: a first line `# <short title>`, then the goal, findings with exact file paths, numbered steps, risks, and how to verify. The user reviews and edits the plan before anything is executed."""
# How much of the transcript the guest is shown, most recent first to be kept: enough for a plan
# to know what was already tried, and far below any guest's first-prompt limit.
MAX_GUEST_TRANSCRIPT_CHARS = 48_000
MAX_GUEST_TOOL_RESULT_CHARS = 1_500
_OMITTED = "[… earlier conversation omitted …]"


def _message_text(content) -> str:
    """The text of a message whose content is a string or OpenAI content parts (images dropped)."""
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        return "\n".join(part["text"] for part in content
                         if isinstance(part, dict) and part.get("type") == "text"
                         and isinstance(part.get("text"), str))
    return ""


def strip_context_blocks(text: str, open_marker: str, close_marker: str) -> str:
    """A user message without the notes Relay prepends to it (the plan-mode note, the terminal
    context): they are about Relay's own tools and this turn, not something the user typed."""
    out = text
    while open_marker and open_marker in out:
        start = out.index(open_marker)
        end = out.find(close_marker, start)
        if end < 0:
            break
        out = out[:start] + out[end + len(close_marker):]
    return out.strip()


def guest_plan_prompt(messages: list[dict], prompt: str, guest_name: str = "",
                      context_markers: tuple[str, str] = ("", "")) -> str:
    """The first prompt a guest's fresh session is sent for a plan turn.

    ``messages`` is the pane's conversation; the transcript is everything before the user message
    that carries this turn's ``prompt`` (matched by `relay_kind: "prompt"`, last one), rendered as
    labelled turns — the user's words with Relay's notes stripped, the assistant's text and the
    tools it called, tool results cut to a bounded excerpt — and capped from the front so the most
    recent part is what survives. ``context_markers`` are the lines Relay wraps its notes in
    (`agent.CONTEXT_OPEN` / `CONTEXT_CLOSE`), passed in because this module does not import the agent.
    """
    open_marker, close_marker = context_markers
    history = list(messages or [])
    for index in range(len(history) - 1, -1, -1):
        message = history[index]
        if isinstance(message, dict) and message.get("role") == "user" \
                and message.get("relay_kind") == "prompt":
            history = history[:index]
            break
    lines: list[str] = []
    for message in history:
        if not isinstance(message, dict):
            continue
        role = message.get("role")
        if role == "user":
            text = strip_context_blocks(_message_text(message.get("content")), open_marker, close_marker)
            if text:
                lines.append(f"User:\n{text}")
        elif role == "assistant":
            text = _message_text(message.get("content")).strip()
            calls = []
            for call in message.get("tool_calls") or []:
                function = call.get("function") if isinstance(call, dict) else None
                if isinstance(function, dict):
                    args = function.get("arguments")
                    args = args if isinstance(args, str) else ""
                    calls.append(f"(called {function.get('name') or 'a tool'} {args[:200]})".rstrip())
            if text or calls:
                lines.append("Assistant:\n" + "\n".join(([text] if text else []) + calls))
        elif role == "tool":
            text = _message_text(message.get("content")).strip()
            if text:
                cut = text[:MAX_GUEST_TOOL_RESULT_CHARS]
                lines.append("Tool result:\n" + cut + (" […]" if len(text) > len(cut) else ""))
    transcript = "\n\n".join(lines)
    if len(transcript) > MAX_GUEST_TRANSCRIPT_CHARS:
        transcript = _OMITTED + "\n\n" + transcript[-MAX_GUEST_TRANSCRIPT_CHARS:]
    parts = [GUEST_PLAN_NOTE]
    if transcript:
        parts.append("The conversation so far in this session"
                     + (f" (the assistant was Relay's own agent, not {guest_name})" if guest_name else "")
                     + ":\n\n" + transcript)
    parts.append("The request to plan:\n\n" + (prompt or "").strip())
    return "\n\n".join(parts)


def plan_from_reply(text) -> tuple[str, str] | None:
    """(title, content) read off a guest's plan-turn reply, or None when the reply holds no plan.

    The title is the first `# ` heading; a reply with none is still a plan (the guest wrote prose)
    and gets a title from its first non-empty line. An empty reply is not one.
    """
    if not isinstance(text, str) or not text.strip():
        return None
    title = ""
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("# "):
            title = stripped[2:].strip()
            break
    if not title:
        title = next((line.strip().lstrip("#").strip() for line in text.splitlines() if line.strip()), "Plan")
    title = " ".join(title.split())[:MAX_TITLE] or "Plan"
    content = text.strip()
    if len(content.encode("utf-8")) > MAX_PLAN_BYTES:
        content = content.encode("utf-8")[:MAX_PLAN_BYTES].decode("utf-8", "ignore")
    return title, content


def validate_mode(mode) -> str:
    if mode not in MODES:
        raise ValueError('mode must be "build" or "plan".')
    return mode


def slugify(title: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")[:60].strip("-")
    return slug or "plan"


def validate_plan_args(args) -> tuple[str, str]:
    if not isinstance(args, dict) or set(args) - {"title", "content"}:
        raise ValueError("write_plan takes title and content.")
    title, content = args.get("title"), args.get("content")
    if not isinstance(title, str) or not title.strip() or len(title) > MAX_TITLE or "\x00" in title:
        raise ValueError(f"title must be text of 1–{MAX_TITLE} characters.")
    if not isinstance(content, str) or not content.strip() or "\x00" in content \
            or len(content.encode("utf-8")) > MAX_PLAN_BYTES:
        raise ValueError("content must be non-empty Markdown of at most 128 KiB.")
    return " ".join(title.split()), content


def plan_path(plans_dir: Path, title: str, now: float | None = None) -> Path:
    stamp = time.strftime("%Y-%m-%d-%H%M", time.localtime(now if now is not None else time.time()))
    base = f"{stamp}-{slugify(title)}"
    candidate = plans_dir / f"{base}.md"
    n = 2
    while candidate.exists():
        candidate = plans_dir / f"{base}-{n}.md"
        n += 1
    return candidate


def write_plan(plans_dir: str | Path, title: str, content: str, now: float | None = None) -> Path:
    plans_dir = Path(plans_dir)
    plans_dir.mkdir(parents=True, exist_ok=True)
    path = plan_path(plans_dir, title, now)
    body = content if content.lstrip().startswith("# ") else f"# {title}\n\n{content}"
    if not body.endswith("\n"):
        body += "\n"
    fd, temp = tempfile.mkstemp(dir=plans_dir, prefix=".plan-")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as out:
            os.fchmod(out.fileno(), 0o644)
            out.write(body)
        # Never replace an existing plan (plan_path picked a free name; link fails if it raced).
        os.link(temp, path)
    finally:
        os.unlink(temp)
    return path


def read_plan(path) -> str:
    if not isinstance(path, str) or not os.path.isabs(path):
        raise ValueError("Plan path must be absolute.")
    data = Path(path).read_bytes()
    if len(data) > MAX_PLAN_BYTES or b"\x00" in data:
        raise ValueError("Plan file is too large or binary.")
    return data.decode("utf-8")


# Appended to every Execute prompt (#K3TY): an Orchestration block in the plan is the plan's own
# decision about how the work is executed, so the executing agent follows it rather than re-planning.
ORCHESTRATION_NOTE = (
    "Where the plan carries an Orchestration block, follow it: start the subagents it lists "
    "(independent ones in one response, so they run concurrently), wait before dependent waves, do "
    "yourself only what it assigns to the main agent, and name any deviation from it in your final reply.")


def execution_prompt(path: str, content: str) -> str:
    return f"Execute the plan in {path}:\n\n{content}\n\n{ORCHESTRATION_NOTE}"
