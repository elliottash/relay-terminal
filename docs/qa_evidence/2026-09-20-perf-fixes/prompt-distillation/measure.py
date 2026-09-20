#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Inventory of what Relay sends before the conversation starts: system prompt and tool schemas.

    python3 measure.py [--repo /path/to/relay-terminal] [--tokenize http://127.0.0.1:8080] [--json out.json]

Read-only. It imports `relay_core` from the checkout, builds Agents in-process the way
`backend/worker.py` wires them (skills index, Switchboard tools, app tools, activity tools, subagent
manager) against a provider that is never called, and measures `Agent.system_prompt()` and
`Agent.tools()` for every configuration that exists. Nothing is sent anywhere except, with
`--tokenize`, the text itself to the local llama.cpp server's `/tokenize` endpoint (no generation,
no key), so the token column is the Local tier's own tokenizer rather than bytes/4.

The board is a throwaway one in a temporary directory (the repository's `issues/board.yaml` is
copied, no cards), and XDG directories are temporary, so nothing of the owner's is written. HOME is
left alone on purpose: the skill catalogue and the subagent type list are built from the owner's
own `~/.claude` and `~/.warp`, and that is the number he pays. `--fresh-home` measures a new
install instead (bundled skills only).
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
import threading
import urllib.request
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--repo", default=str(Path(__file__).resolve().parents[4]))
parser.add_argument("--tokenize", default=None, help="llama.cpp server base URL, e.g. http://127.0.0.1:8080")
parser.add_argument("--json", default=None)
parser.add_argument("--fresh-home", action="store_true")
parser.add_argument("--dump", default=None, help="directory to write each configuration's prompt and tools into")
ARGS = parser.parse_args()

SCRATCH = tempfile.mkdtemp(prefix="relay-distill-")
os.environ["RELAY_KEYRING"] = "off"
for var, sub in (("XDG_DATA_HOME", "d"), ("XDG_CONFIG_HOME", "c"), ("XDG_STATE_HOME", "s"), ("XDG_CACHE_HOME", "k")):
    os.environ[var] = os.path.join(SCRATCH, sub)
    os.makedirs(os.environ[var], exist_ok=True)
if ARGS.fresh_home:
    os.environ["HOME"] = os.path.join(SCRATCH, "home")
    os.makedirs(os.environ["HOME"], exist_ok=True)
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(ARGS.repo, "backend"))

from relay_core import activity_tools, agents_defs, app_tools, board as B, board_tools as T  # noqa: E402
from relay_core import planning, skills, todos  # noqa: E402
from relay_core.keybindings import KeybindingCatalog  # noqa: E402
from relay_core import agent as agent_module  # noqa: E402
from relay_core.agent import Agent  # noqa: E402
from relay_core.provider import ProviderConfig  # noqa: E402
from relay_core.subagents import SubagentFactory, SubagentManager, subagent_prompt  # noqa: E402

CONFIG = ProviderConfig("http://127.0.0.1:9/v1", "stub", "stub", {}, 8192)


class NeverCalled:
    config = None

    def complete(self, *a, **k):  # pragma: no cover
        raise AssertionError("measure.py never calls a model")


_token_cache: dict[str, int] = {}


def tokens(text: str) -> int | None:
    if not ARGS.tokenize:
        return None
    if text in _token_cache:
        return _token_cache[text]
    body = json.dumps({"content": text}).encode()
    request = urllib.request.Request(ARGS.tokenize.rstrip("/") + "/tokenize", body,
                                     {"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=60) as response:
        count = len(json.load(response)["tokens"])
    _token_cache[text] = count
    return count


def size(text: str) -> dict:
    raw = len(text.encode("utf-8"))
    return {"bytes": raw, "tokens_div4": round(raw / 4), "tokens_local": tokens(text)}


def tools_text(specs: list[dict]) -> str:
    # What goes on the wire: the JSON of the `tools` array. The Local tier's chat template renders
    # each entry with `tojson` inside the system block, so this is close to what it prefills.
    return json.dumps(specs, ensure_ascii=False)


# ---- fixtures -------------------------------------------------------------------------------
def workspace(with_board: bool) -> Path:
    ws = Path(tempfile.mkdtemp(prefix="ws-", dir=SCRATCH))
    if with_board:
        (ws / "issues").mkdir()
        (ws / "issues" / B.BOARD_CONFIG).write_text(
            (Path(ARGS.repo) / "issues" / B.BOARD_CONFIG).read_text(encoding="utf-8"), encoding="utf-8")
    return ws


APP = {"writes_enabled": True,
       "options": [{"id": "appearance.theme", "section": "appearance", "section_label": "Appearance",
                    "label": "Theme", "kind": "choice", "value": "dark", "settable": True,
                    "choices": [{"value": "dark", "label": "Dark"}, {"value": "light", "label": "Light"}]}],
       "actions": [{"key": "settings.open", "section": "Relay", "label": "Open settings", "agent_safe": True}]}


def keybinding_catalog() -> KeybindingCatalog:
    """The catalog the GUI sends with `configure`: every `add("id", "category", "description", {keys})`
    in src/Keymap.h with its default keys. It is what `set_keybinding`'s description lists."""
    import re
    source = (Path(ARGS.repo) / "src" / "Keymap.h").read_text(encoding="utf-8")
    actions = []
    for match in re.finditer(r'^\s*add\("([^"]+)",\s*"[^"]*",\s*"((?:[^"\\]|\\.)*)",\s*\{(.*?)\}\);', source, re.M):
        keys = re.findall(r'QStringLiteral\("([^"]+)"\)', match.group(3))
        actions.append({"id": match.group(1), "description": match.group(2), "keys": keys})
    return KeybindingCatalog(os.path.join(SCRATCH, "c", "relay", "keybindings.json"), actions)


def pane_agent(*, board: bool, app: bool = True, keys: bool = True, mode: str = "build", todo_tool: bool = True,
               program: bool = False, terminal: bool = False, scope=None) -> Agent:
    ws = workspace(board)
    index = skills.from_request(None, str(ws))
    board_tools = None
    if board:
        b = B.Board(ws / "issues", ws)
        board_tools = T.BoardTools(b, emit=lambda e: None, autonomy="auto",
                                   context=T.ToolContext(actor="agent", model="stub", pane="1"),
                                   state_path=ws / ".relay" / "board-rate.json",
                                   pane_token="3f2504e0-4f89-11d3-9a0c-0305e82c3301")
        if scope is not None:
            board_tools.card_scope = scope
    app_side = None
    if app:
        catalog = app_tools.AppCatalog.from_request(APP)
        app_side = app_tools.AppTools(catalog, app_tools.AppBridge(lambda e: None), sessions=None,
                                      workspace=str(ws))
    agent = Agent(CONFIG, str(ws), lambda e: None, provider=NeverCalled(), skills=index,
                  keybindings=keybinding_catalog() if keys else None, board=board_tools, app=app_side, todo_tool=todo_tool)
    if scope is None:
        activity_tools.ActivityTools.attach(agent, live_info=lambda: {})
        manager = SubagentManager(lambda e: None)
        manager.configure(agents_defs.load_catalog(str(ws), None),
                          SubagentFactory(CONFIG, str(ws), skills=index, main_agent=agent))
        manager.attach(agent)
    if program:
        agent.executor.program.available = lambda: True
    if terminal:
        agent.executor.terminal.available = lambda: True
    if mode != "build":
        agent.set_mode(mode)
    else:
        agent.refresh_system_prompt()
    return agent


def describe(agent: Agent) -> dict:
    prompt = agent.system_prompt()
    specs = agent.tools()
    per_tool = sorted(({"name": s["function"]["name"], **size(json.dumps(s, ensure_ascii=False)),
                        "description_bytes": len(s["function"].get("description", "").encode())}
                       for s in specs), key=lambda row: -row["bytes"])
    return {"prompt": size(prompt), "tools": size(tools_text(specs)), "tool_count": len(specs),
            "total": size(prompt + tools_text(specs)), "per_tool": per_tool,
            "_prompt_text": prompt, "_tools": specs}


def components(agent: Agent) -> list[dict]:
    """The pieces `Agent.system_prompt` concatenates, in the order it concatenates them."""
    executor = agent.executor
    rows = [("SYSTEM", agent_module.SYSTEM),
            ("workspace line", "\nChosen workspace: " + str(executor.workspace.root)),
            ("skill catalogue", executor.skills.prompt_section() if executor.skills is not None else ""),
            ("project instructions (AGENTS.md etc.)", agent.instructions.section if agent.instructions is not None else ""),
            ("todo rules", todos.RULES if agent._todos_enabled() else ""),
            ("Switchboard header + policy", T.prompt_section(agent.board)),
            ("app rules", app_tools.prompt_section(agent.app)),
            ("own-session rules", activity_tools.prompt_section(agent.activity)),
            ("plan-mode note", planning.PLAN_MODE_NOTE if agent.mode == "plan" else ""),
            # d46c4f56 (#GMCF): the volatile tail — the pane's token and the cards it holds.
            ("Switchboard session note (volatile tail)",
             getattr(T, "session_note", lambda tools: "")(agent.board))]
    assert "".join(text for _, text in rows) == agent.system_prompt(), "system_prompt() has a piece this script does not know"
    return [{"component": name, **size(text)} for name, text in rows]


def tool_groups(agent: Agent) -> list[dict]:
    groups: dict[str, list[dict]] = {}
    for spec in agent.tools():
        name = spec["function"]["name"]
        if name.startswith("board_"):
            key = "Switchboard"
        elif name.startswith("app_"):
            key = "app"
        elif name in ("session_info", "activity"):
            key = "own session"
        elif name.startswith("agent"):
            key = "subagents"
        elif name in ("load_skill", "read_skill_file"):
            key = "skills"
        elif name in ("update_todos", "write_plan", "ask_user"):
            key = "turn bookkeeping"
        elif name in ("command_output", "stop_command", "list_jobs"):
            key = "jobs"
        elif name.startswith("tests_"):
            key = "tests"
        elif name == "set_keybinding":
            key = "keybindings"
        elif name in ("run_in_terminal", "type_into_program"):
            key = "user's terminal"
        else:
            key = "core files and commands"
        groups.setdefault(key, []).append(spec)
    return [{"group": key, "count": len(specs), "names": [s["function"]["name"] for s in specs],
             **size(tools_text(specs))} for key, specs in groups.items()]


def main() -> int:
    out: dict = {"repo": ARGS.repo, "fresh_home": ARGS.fresh_home, "tokenizer": ARGS.tokenize, "configurations": {}}
    configurations = {
        "pane, no board, build": dict(board=False),
        "pane, no board, build, terminal handoff + program handed over": dict(board=False, program=True, terminal=True),
        "pane, no board, plan": dict(board=False, mode="plan"),
        "pane, no board, build, todo tool off": dict(board=False, todo_tool=False),
        "bare Agent as the profile measured it (no app block, no keybindings)": dict(board=False, app=False, keys=False),
        "pane, board, build": dict(board=True),
        "pane, board, build, terminal handoff + program handed over": dict(board=True, program=True, terminal=True),
        "pane, board, plan": dict(board=True, mode="plan"),
        "helper: card Discuss turn": dict(board=True, scope=T.CardScope("discuss", "K7Q2")),
        "helper: card Plan turn": dict(board=True, scope=T.CardScope("plan", "K7Q2")),
        "helper: Switchboard page chat": dict(board=True, scope=T.ChatScope()),
    }
    for label, kw in configurations.items():
        agent = pane_agent(**kw)
        row = describe(agent)
        row["components"] = components(agent)
        row["tool_groups"] = tool_groups(agent)
        out["configurations"][label] = row

    # A subagent: a bare Agent with the restricted executor, SYSTEM + skills + the subagent section.
    parent = pane_agent(board=False)
    for name in ("general", "explore"):
        definition = parent.subagents.catalog.definitions[name]
        sub, _, _ = parent.subagents.factory(definition, None, None, lambda e: None, "a1")
        prompt = sub.messages[0]["content"]
        specs = sub.tools()
        out["configurations"][f"subagent: {name}"] = {
            "prompt": size(prompt), "tools": size(tools_text(specs)), "tool_count": len(specs),
            "total": size(prompt + tools_text(specs)),
            "components": [{"component": "SYSTEM", **size(agent_module.SYSTEM)},
                           {"component": "skill catalogue",
                            **size(sub.executor.skills.prompt_section() if sub.executor.skills is not None else "")},
                           {"component": "subagent section", **size(subagent_prompt(definition, "a1"))}],
            "per_tool": sorted(({"name": s["function"]["name"], **size(json.dumps(s, ensure_ascii=False))}
                                for s in specs), key=lambda r: -r["bytes"]),
            "_prompt_text": prompt, "_tools": specs}

    # The briefs the helper's first user message carries (not the system prompt, but paid once per conversation).
    core = Path(ARGS.repo) / "backend" / "relay_core"
    out["briefs"] = {p.name: size(p.read_text(encoding="utf-8")) for p in sorted(core.glob("board_*_brief.md"))}

    # SYSTEM line by line, for the verdict table.
    out["system_lines"] = [{"line": n, **size(text), "text": text}
                           for n, text in enumerate(agent_module.SYSTEM.split("\n"), 1)]

    if ARGS.dump:
        os.makedirs(ARGS.dump, exist_ok=True)
        for label, row in out["configurations"].items():
            stem = "".join(c if c.isalnum() else "-" for c in label).strip("-")
            Path(ARGS.dump, stem + ".prompt.txt").write_text(row["_prompt_text"], encoding="utf-8")
            Path(ARGS.dump, stem + ".tools.json").write_text(json.dumps(row["_tools"], indent=1, ensure_ascii=False), encoding="utf-8")
    for row in out["configurations"].values():
        row.pop("_prompt_text", None)
        row.pop("_tools", None)

    def cell(entry: dict) -> str:
        local = entry["tokens_local"]
        return f"{entry['bytes']:>7,} B  ~{entry['tokens_div4']:>6,} tok/4" + (f"  {local:>6,} tok local" if local is not None else "")

    for label, row in out["configurations"].items():
        print(f"\n== {label}")
        print(f"   prompt {cell(row['prompt'])}")
        print(f"   tools  {cell(row['tools'])}   ({row['tool_count']} tools)")
        print(f"   total  {cell(row['total'])}")
        for part in row["components"]:
            if part["bytes"]:
                print(f"     - {part['component']:<40} {cell(part)}")
        for group in row.get("tool_groups", []):
            print(f"     * tools/{group['group']:<34} {cell(group)}   {group['count']}")
    print("\n== largest tool schemas (pane, board, build)")
    for tool in out["configurations"]["pane, board, build"]["per_tool"][:25]:
        print(f"   {tool['name']:<28} {cell(tool)}   description {tool['description_bytes']:,} B")
    print("\n== briefs")
    for name, entry in out["briefs"].items():
        print(f"   {name:<28} {cell(entry)}")
    if ARGS.json:
        Path(ARGS.json).write_text(json.dumps(out, indent=1, ensure_ascii=False), encoding="utf-8")
    return 0


if __name__ == "__main__":
    threading.current_thread().name = "measure"
    sys.exit(main())
