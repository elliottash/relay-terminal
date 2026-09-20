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
When you understand the task, call write_plan exactly once with a short title and a complete Markdown plan: goal, findings with exact file paths, numbered steps, risks, and how to verify. When the work is big enough to split across subagents, the plan also carries an Orchestration block: each subagent (its type and a one-line task), which steps run in parallel, and which wait for which. Only steps that touch no shared files may run in parallel, and writes stay with the main agent; a small plan gets no block. Then reply with a two-sentence summary. The user reviews and edits the plan file before anything is executed."""

WRITE_PLAN_SPEC = {"type": "function", "function": {
    "name": "write_plan",
    "description": "Plan mode only: save the finished implementation plan as a Markdown file the user can edit and execute.",
    "parameters": {"type": "object", "properties": {
        "title": {"type": "string", "description": "Short plan title (a few words)."},
        "content": {"type": "string", "description": "The complete plan in Markdown."}},
        "required": ["title", "content"], "additionalProperties": False}}}


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
