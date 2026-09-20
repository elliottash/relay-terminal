#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""What the system prompt and the tool schemas cost, per section and per tool (#GMCF, #PF4K).

Both are input on *every* provider request of every step, so a byte here is paid hundreds of times
in a session. This prints them broken down, for a pane with and without a Switchboard and in each
mode the worker has, so a before/after is one diff of two runs.

    python3 docs/qa_evidence/2026-09-20-perf-fixes/prompt/promptsize.py            # this machine's skills
    python3 .../promptsize.py --skills-dir <dir>   # a fixed skill library, for a reproducible number
    python3 .../promptsize.py --json              # machine-readable

Tokens are `relay_core.context.estimate_tokens`' estimate (4 chars per token), the same one the
context chip shows; a real tokenizer differs by a few per cent, not by a factor.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "backend"))

from relay_core import activity_tools, app_tools, board as board_mod, board_tools  # noqa: E402
from relay_core import instructions as instructions_mod, skills as skills_mod  # noqa: E402
from relay_core.agent import Agent  # noqa: E402
from relay_core.context import estimate_tokens  # noqa: E402
from relay_core.keybindings import KeybindingCatalog  # noqa: E402
from relay_core.provider import ProviderConfig  # noqa: E402

CONFIG = ProviderConfig("http://127.0.0.1:12345/v1", "mock", "")
#: The `app` block a pane sends (§30.2): a few rows of each kind, which is all the tool list
#: depends on — the schemas are fixed and only the counts in the app rules follow the catalog.
APP = {"tab": "t1", "writes_enabled": True,
       "options": [{"id": "appearance.theme", "section": "appearance", "section_label": "Appearance",
                    "label": "Theme", "kind": "choice", "value": "dark", "settable": True,
                    "choices": [{"value": "dark", "label": "Dark"}, {"value": "light", "label": "Light"}]},
                   {"id": "agent.turn_limit", "section": "agent", "section_label": "Agent",
                    "label": "Turn limit", "kind": "number", "value": 40, "min": 1, "max": 200,
                    "settable": True}],
       "actions": [{"key": "settings.open", "section": "Relay", "label": "Open settings",
                    "agent_safe": True}]}
BOARD_CONFIG = """tabs:
  - id: features
    title: Features
    statuses: [inbox, triaged, executing, needs-verification, done]
  - id: bugs
    title: Bugs
    statuses: [inbox, triaged, executing, needs-verification, done]
"""


def nbytes(text: str) -> int:
    return len(text.encode("utf-8"))


def keybinding_catalog(path: Path) -> KeybindingCatalog:
    """The catalog the GUI sends with `configure`: `src/Keymap.h`'s registry, keys and all.

    A pane's `configure` carries it, so an Agent built without one is missing a tool — before
    #GMCF the largest of them all (9,837 B), which is exactly why this fixture now builds it.
    """
    source = (ROOT / "src" / "Keymap.h").read_text(encoding="utf-8")
    actions = [{"id": match.group(1), "description": match.group(2),
                "keys": re.findall(r'QStringLiteral\("([^"]+)"\)', match.group(3))}
               for match in re.finditer(
                   r'^\s*add\("([^"]+)",\s*"[^"]*",\s*"((?:[^"\\]|\\.)*)",\s*\{(.*?)\}\);',
                   source, re.M)]
    return KeybindingCatalog(str(path / "relay" / "keybindings.json"), actions)


def make_agent(workspace: Path, *, skills_dirs, board_root: Path | None, with_instructions: bool,
               keybindings: KeybindingCatalog | None = None):
    """A pane agent with the sections a real pane has: skills, todos, app, activity, maybe a board."""
    agent = Agent(CONFIG, str(workspace), lambda event: None, provider=_NullProvider(),
                  keybindings=keybindings)
    agent.executor.skills = skills_mod.SkillIndex.load(skills_dirs, skills_mod.DEFAULT_EXCLUDE,
                                                       defaults=True)
    if with_instructions:
        agent.instructions = instructions_mod.load({"project_auto": True}, str(workspace))
    agent.app = app_tools.AppTools(
        app_tools.AppCatalog.from_request(APP), app_tools.AppBridge(lambda event: None),
        keybindings=lambda: agent.executor.keybindings)
    agent.activity = activity_tools.ActivityTools(agent)
    if board_root is not None:
        board = board_mod.Board(board_root / "issues", board_root)
        agent.board = board_tools.BoardTools(
            board, emit=lambda event: None, autonomy="auto",
            context=board_tools.ToolContext(actor="agent", model="anthropic/claude-opus-5", pane="2"),
            state_path=board_root / ".relay" / "board-rate.json",
            pane_token="3f2504e0-4f89-11d3-9a0c-0305e82c3301")
    agent.refresh_system_prompt()
    return agent


class _NullProvider:
    def complete(self, messages, tools, emit, cancel):    # pragma: no cover - never called here
        raise RuntimeError("promptsize does not run turns")

    def cancel(self) -> None:
        pass


def sections(agent: Agent) -> list[tuple[str, str]]:
    """The pieces `Agent.system_prompt` concatenates, in its order."""
    from relay_core import agent as agent_module
    from relay_core import todos as todo_tool
    ex = agent.executor
    return [
        ("SYSTEM", agent_module.SYSTEM),
        ("workspace line", "\nChosen workspace: " + str(ex.workspace.root)),
        ("skills catalogue", ex.skills.prompt_section() if ex.skills is not None else ""),
        ("instructions (AGENTS.md/CLAUDE.md)", agent.instructions.section if agent.instructions else ""),
        ("todo rules", todo_tool.RULES if agent.track_requests and agent.todo_tool else ""),
        ("board rules", board_tools.prompt_section(agent.board)),
        ("app rules", app_tools.prompt_section(agent.app)),
        ("own-session rules", activity_tools.prompt_section(agent.activity)),
        ("plan-mode note", agent_module.PLAN_MODE_NOTE if agent.mode == "plan" else ""),
        ("board session note (volatile tail)", board_tools.session_note(agent.board)),
    ]


def scenario(agent: Agent, name: str) -> dict:
    rows = [{"section": label, "bytes": nbytes(text), "tokens": estimate_tokens(text)}
            for label, text in sections(agent) if text]
    prompt = agent.system_prompt()
    specs = sorted(agent.tools(), key=lambda s: s["function"]["name"])
    tools = [{"tool": s["function"]["name"], "bytes": nbytes(json.dumps(s, ensure_ascii=False)),
              "tokens": estimate_tokens(s)} for s in specs]
    return {"scenario": name, "sections": rows, "tools": tools,
            "prompt_bytes": nbytes(prompt), "prompt_tokens": estimate_tokens(prompt),
            "tools_bytes": nbytes(json.dumps(specs, ensure_ascii=False)),
            "tools_tokens": estimate_tokens(specs),
            "skills": len(agent.executor.skills.skills) if agent.executor.skills else 0}


def table(result: dict) -> str:
    out = [f"## {result['scenario']}", "",
           f"| section | bytes | ~tokens |", "| --- | ---: | ---: |"]
    for row in result["sections"]:
        out.append(f"| {row['section']} | {row['bytes']:,} | {row['tokens']:,} |")
    out.append(f"| **system prompt, total** | **{result['prompt_bytes']:,}** | **{result['prompt_tokens']:,}** |")
    out += ["", "| tool schema | bytes | ~tokens |", "| --- | ---: | ---: |"]
    for row in result["tools"]:
        out.append(f"| {row['tool']} | {row['bytes']:,} | {row['tokens']:,} |")
    out.append(f"| **tools, total ({len(result['tools'])})** | **{result['tools_bytes']:,}** | **{result['tools_tokens']:,}** |")
    out.append("")
    out.append(f"Per-request input: **{result['prompt_bytes'] + result['tools_bytes']:,} bytes**, "
               f"~{result['prompt_tokens'] + result['tools_tokens']:,} tokens.")
    out.append("")
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skills-dir", action="append", default=None,
                        help="skill directory to index instead of this machine's defaults (repeatable)")
    parser.add_argument("--workspace", default=None, help="workspace root (default: a temp dir)")
    parser.add_argument("--instructions", action="store_true",
                        help="also load the workspace's AGENTS.md/CLAUDE.md (machine-dependent)")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as temp:
        workspace = Path(args.workspace).resolve() if args.workspace else Path(temp) / "ws"
        workspace.mkdir(parents=True, exist_ok=True)
        board_root = Path(temp) / "repo"
        (board_root / "issues").mkdir(parents=True)
        (board_root / "issues" / board_mod.BOARD_CONFIG).write_text(BOARD_CONFIG, encoding="utf-8")
        dirs = ([Path(d) for d in args.skills_dir] if args.skills_dir
                else skills_mod.default_directories(str(workspace)))

        keys = keybinding_catalog(Path(temp))
        results = []
        for name, root, mode in (("no board, build mode", None, "build"),
                                 ("no board, plan mode", None, "plan"),
                                 ("board attached, build mode", board_root, "build"),
                                 ("board attached, plan mode", board_root, "plan")):
            agent = make_agent(workspace, skills_dirs=dirs, board_root=root,
                               with_instructions=args.instructions, keybindings=keys)
            agent.set_mode(mode)
            results.append(scenario(agent, name))

    if args.json:
        print(json.dumps(results, indent=2))
    else:
        print(f"Skills indexed: {results[0]['skills']}\n")
        for result in results:
            print(table(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
