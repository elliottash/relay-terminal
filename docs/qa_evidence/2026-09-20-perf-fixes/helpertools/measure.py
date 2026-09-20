#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""What a helper turn's first action costs, with and without the deferral gap (#GMCF).

    PYTHONPATH=$PWD/backend python3 docs/qa_evidence/2026-09-20-perf-fixes/helpertools/measure.py

Three agents are built and their tool lists measured, nothing is sent anywhere: the tab helper of a
tab with **no project attached** (no board), the tab helper of a tab with one, and an ordinary pane
agent. `_build_page_agent` builds the two helpers, exactly as `helperkeys/measure.py` does — this
script is that one with the board-less helper and the pane beside it.

A helper that defers the `app` group sends fewer bytes and then pays a whole extra round trip on its
first action, which is what every one of its turns is: the printed "first action" figure is the
bytes the model has to read before it can call `app_action_run` — one request when the schemas are
there, two when they are not.
"""
import json
import sys
import tempfile
from pathlib import Path

from relay_core import activity_tools as V
from relay_core import app_tools as A
from relay_core import board as B
from relay_core import board_protocol as P
from relay_core.agent import Agent
from relay_core.keybindings import KeybindingCatalog
from relay_core.provider import ProviderConfig

ACTIONS = [{"id": f"group{i}.action", "description": "An action of Relay's, described in a line",
            "keys": ["Ctrl+%d" % (i % 10)]} for i in range(92)]


class StubTurns:
    agent = None
    busy = False

    def reset(self):
        pass


def app_tools_for(agent):
    return A.AppTools(A.AppCatalog.from_request(
        {"tab": "t1", "writes_enabled": True, "options": [],
         "actions": [{"key": "settings.open", "section": "Relay", "label": "Open settings",
                      "agent_safe": True}]}), A.AppBridge(lambda event: None))


def main_agent(repo, catalog):
    main = Agent(ProviderConfig("http://127.0.0.1:12345/v1", "mock", ""), str(repo),
                 lambda event: None, keybindings=catalog)
    main.app = app_tools_for(main)
    main.activity = V.ActivityTools(main, live_info=lambda: {"event": "session_info"})
    return main


def helper(temp, *, board: bool):
    """The tool list and prompt of one helper turn, as `_build_page_agent` builds it."""
    repo = Path(temp) / ("with-board" if board else "no-board")
    repo.mkdir(exist_ok=True)
    if board:
        (repo / "issues").mkdir(exist_ok=True)
        (repo / "issues" / B.BOARD_CONFIG).write_text(
            "tabs: [{id: features, folder: features}]\ncolumns: [inbox, done]\n", encoding="utf-8")
    catalog = KeybindingCatalog(str(repo / "keybindings.json"), ACTIONS)
    commands = P.BoardCommands(StubTurns(), lambda event: None)
    commands.configure(str(repo), {})
    commands.turns.agent = main_agent(repo, catalog)
    agent, tools = commands._build_page_agent(lambda event: None)
    if tools is not None:
        tools.begin_chat_turn()
    # `built` is the prompt in `messages[0]` — the one the turn actually sends. It is written once,
    # at construction, before any `ChatScope` exists, which is why the board-attached helper used to
    # carry a `load_tools` rule for a tool its turn is never offered.
    built = agent.messages[0]["content"]
    specs = agent.tools()
    if tools is not None:
        tools.end_chat_turn()
    return specs, built


def pane(temp):
    repo = Path(temp) / "pane"
    repo.mkdir(exist_ok=True)
    agent = main_agent(repo, KeybindingCatalog(str(repo / "keybindings.json"), ACTIONS))
    agent.refresh_system_prompt()
    return agent.tools(), agent.messages[0]["content"]


def report(label, specs, prompt):
    blob = json.dumps(specs, separators=(",", ":"))
    names = {t["function"]["name"] for t in specs}
    eager = "app_action_run" in names
    first = len(blob) + len(prompt) if eager else 2 * len(blob) + len(prompt)
    print(f"{label:<28} {len(specs):>2} tools  {len(blob):>6} B  prompt {len(prompt):>6} B  "
          f"app tools {'eager' if eager else 'DEFERRED'}  "
          f"prompt says load_tools: {'yes' if 'load_tools' in prompt else 'no '}  "
          f"tool offered: {'yes' if 'load_tools' in names else 'no '}  "
          f"first action ~{first} B")


def main():
    with tempfile.TemporaryDirectory() as temp:
        report("helper, no project", *helper(temp, board=False))
        report("helper, project attached", *helper(temp, board=True))
        report("ordinary pane", *pane(temp))


if __name__ == "__main__":
    main()
