#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""What a build → plan switch costs in prefill, before and after #GMCF decision 4.

    python3 modeswitch.py --repo <checkout or clean export> [--bench http://127.0.0.1:8080]

Builds one pane agent (skills, project instructions, app block, keybinding catalogue, Switchboard)
the way `backend/worker.py` wires a pane, takes its system prompt, tool list and turn note in build
mode, switches it to plan mode, and takes them again. With `--bench` it primes a llama.cpp slot
with the build-mode request and then sends the plan-mode one with `cache_prompt: true`, so the
server's own `timings.prompt_n` is exactly what the switch re-prefilled. It is the owner's local
model on 127.0.0.1: no key, no cost, nothing hosted.

Read-only towards the repository; XDG directories and the board are temporary copies.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--repo", required=True)
parser.add_argument("--bench", default=None)
parser.add_argument("--runs", type=int, default=3)
parser.add_argument("--json", default=None)
ARGS = parser.parse_args()

SCRATCH = tempfile.mkdtemp(prefix="relay-modeswitch-")
os.environ["RELAY_KEYRING"] = "off"
for var, sub in (("XDG_DATA_HOME", "d"), ("XDG_CONFIG_HOME", "c"), ("XDG_STATE_HOME", "s"), ("XDG_CACHE_HOME", "k")):
    os.environ[var] = os.path.join(SCRATCH, sub)
    os.makedirs(os.environ[var], exist_ok=True)
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(ARGS.repo, "backend"))

from relay_core import activity_tools, app_tools, board as B, board_tools as T  # noqa: E402
from relay_core import agent as agent_module, skills  # noqa: E402
from relay_core.agent import Agent  # noqa: E402
from relay_core.keybindings import KeybindingCatalog  # noqa: E402
from relay_core.provider import ProviderConfig  # noqa: E402

CONFIG = ProviderConfig("http://127.0.0.1:9/v1", "stub", "stub", {}, 131072)
APP = {"tab": "t1", "writes_enabled": True,
       "options": [{"id": "appearance.theme", "section": "appearance", "section_label": "Appearance",
                    "label": "Theme", "kind": "choice", "value": "dark", "settable": True,
                    "choices": [{"value": "dark", "label": "Dark"}, {"value": "light", "label": "Light"}]}],
       "actions": [{"key": "settings.open", "section": "Relay", "label": "Open settings", "agent_safe": True}]}


def keybindings(repo: Path) -> KeybindingCatalog:
    """`src/Keymap.h`'s registry, the way the GUI's `configure` sends it."""
    source = (repo / "src" / "Keymap.h").read_text(encoding="utf-8")
    actions = [{"id": m.group(1), "description": m.group(2),
                "keys": re.findall(r'QStringLiteral\("([^"]+)"\)', m.group(3))}
               for m in re.finditer(
                   r'^\s*add\("([^"]+)",\s*"[^"]*",\s*"((?:[^"\\]|\\.)*)",\s*\{(.*?)\}\);', source, re.M)]
    return KeybindingCatalog(str(Path(SCRATCH) / "keybindings.json"), actions)


def pane_agent(repo: Path) -> Agent:
    workspace = Path(SCRATCH) / "ws"
    (workspace / "issues").mkdir(parents=True, exist_ok=True)
    (workspace / "AGENTS.md").write_text("Project rules: run the tests before you say it works.\n")
    (workspace / "issues" / B.BOARD_CONFIG).write_text((repo / "issues" / B.BOARD_CONFIG).read_text())
    agent = Agent(CONFIG, str(workspace), lambda event: None, provider=object(),
                  keybindings=keybindings(repo))
    agent.executor.skills = skills.from_request(None, str(workspace))
    agent.app = app_tools.AppTools(app_tools.AppCatalog.from_request(APP),
                                   app_tools.AppBridge(lambda event: None))
    agent.activity = activity_tools.ActivityTools(agent)
    agent.board = T.BoardTools(B.Board(workspace / "issues", workspace), emit=lambda e: None,
                               autonomy="auto", state_path=Path(SCRATCH) / "rate.json",
                               pane_token="3f2504e0-4f89-11d3-9a0c-0305e82c3301",
                               context=T.ToolContext(actor="agent", model="m", pane="2"))
    agent.board.begin_turn("t-1")
    agent.refresh_system_prompt()
    return agent


def turn_note(agent: Agent) -> str:
    """The Relay context block this turn would carry — where the plan-mode note lives after the fix."""
    note = getattr(agent_module, "plan_mode_note", None)
    return note(agent.mode) if note is not None else ""


def snapshot(agent: Agent) -> dict:
    return {"prompt": agent.system_prompt(), "tools": agent.tools(), "note": turn_note(agent)}


def request(state: dict, nonce: str) -> dict:
    return {"model": "local", "stream": False, "max_tokens": 1, "cache_prompt": True,
            "messages": [{"role": "system", "content": state["prompt"]},
                         {"role": "user", "content": "Where is the pane's tool list built?"},
                         {"role": "assistant", "content": "In Agent.tools()."},
                         {"role": "user", "content": f"{state['note']}[{nonce}] Reply with the single word OK."}],
            "tools": state["tools"]}


def send(body: dict) -> dict:
    call = urllib.request.Request(ARGS.bench.rstrip("/") + "/v1/chat/completions",
                                  json.dumps(body).encode(), {"Content-Type": "application/json"})
    started = time.perf_counter()
    with urllib.request.urlopen(call, timeout=600) as response:
        answer = json.load(response)
    timings = answer.get("timings") or {}
    return {"wall_s": round(time.perf_counter() - started, 3), "prompt_n": timings.get("prompt_n"),
            "cache_n": timings.get("cache_n"), "prompt_ms": round(timings.get("prompt_ms", 0), 1)}


def main() -> int:
    repo = Path(ARGS.repo).resolve()
    agent = pane_agent(repo)
    build = snapshot(agent)
    agent.set_mode("plan")
    plan = snapshot(agent)
    report = {"repo": str(repo)}
    for name, state in (("build", build), ("plan", plan)):
        report[name] = {"prompt_bytes": len(state["prompt"].encode()),
                        "tools_bytes": len(json.dumps(state["tools"], ensure_ascii=False).encode()),
                        "tools": len(state["tools"]), "note_bytes": len(state["note"].encode())}
    report["switch_changes_prompt"] = build["prompt"] != plan["prompt"]
    report["switch_changes_tools"] = build["tools"] != plan["tools"]
    print(json.dumps(report, indent=1))
    if ARGS.bench:
        send({"model": "local", "stream": False, "max_tokens": 1,
              "messages": [{"role": "user", "content": "hi"}]})            # wake the model
        runs = []
        for i in range(ARGS.runs):
            send(request(build, f"prime-{i}"))                             # the pane is in build mode
            runs.append(send(request(plan, f"switch-{i}")))                # the user switches to plan
            print("switch", runs[-1])
        report["switch"] = runs
    if ARGS.json:
        Path(ARGS.json).write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
