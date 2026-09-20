#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""What handing the helper the keybinding catalogue costs its request (#GMCF).

    PYTHONPATH=$PWD/backend python3 docs/qa_evidence/2026-09-20-perf-fixes/helperkeys/measure.py

The helper agent is built twice — once as `configure` built it yesterday, with no `keybindings`
block, once as it is built now — and the two tool lists are measured. Nothing is sent anywhere:
the provider config points at a port nothing is listening on and no turn is taken.

An optional argument is a JSON file of `{id, description, keys}` rows (the block the GUI sends);
without one the catalogue is 92 synthetic actions, which is what Relay's registry holds today.
"""
import json
import sys
import tempfile
from pathlib import Path

from relay_core import app_tools as A
from relay_core import board as B
from relay_core import board_protocol as P
from relay_core.agent import Agent
from relay_core.keybindings import KeybindingCatalog
from relay_core.provider import ProviderConfig

ACTIONS = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8")) if len(sys.argv) > 1 else [
    {"id": f"group{i}.action", "description": "An action of Relay's, described in a line " * 1,
     "keys": ["Ctrl+%d" % (i % 10)]} for i in range(92)]


class StubTurns:
    """`board_protocol`'s supervisor stub: no provider, nothing submitted."""

    agent = None
    busy = False

    def reset(self):
        pass


def helper_tools(catalog, temp):
    """The tool list one helper turn sends, built the way `_build_page_agent` builds it."""
    repo = Path(temp)
    root = repo / "issues"
    root.mkdir(exist_ok=True)
    (root / B.BOARD_CONFIG).write_text(
        "tabs: [{id: features, folder: features}]\ncolumns: [inbox, done]\n", encoding="utf-8")
    commands = P.BoardCommands(StubTurns(), lambda event: None)
    commands.configure(str(repo), {})
    main = Agent(ProviderConfig("http://127.0.0.1:12345/v1", "mock", ""), str(repo),
                 lambda event: None, keybindings=catalog)
    main.app = A.AppTools(A.AppCatalog.from_request(
        {"tab": "t1", "writes_enabled": True, "options": [],
         "actions": [{"key": "settings.open", "section": "Relay", "label": "Open settings",
                      "agent_safe": True}]}), A.AppBridge(lambda event: None))
    commands.turns.agent = main
    agent, tools = commands._build_page_agent(lambda event: None)
    tools.begin_chat_turn()
    specs = agent.tools()
    tools.end_chat_turn()
    return specs


def size(specs):
    blob = json.dumps(specs, separators=(",", ":"))
    return len(blob), len(blob) // 4          # bytes, and the four-bytes-a-token rule of thumb


def main():
    with tempfile.TemporaryDirectory() as temp:
        catalog = KeybindingCatalog(str(Path(temp) / "relay" / "keybindings.json"), ACTIONS)
        before = helper_tools(None, temp)
        after = helper_tools(catalog, temp)
    (bn, bt), (an, at) = size(before), size(after)
    names = sorted({t["function"]["name"] for t in after} - {t["function"]["name"] for t in before})
    print(f"actions in the catalogue: {len(ACTIONS)}")
    print(f"helper tool list, no keybindings block: {len(before):>2} tools  {bn:>6} B  ~{bt} tok")
    print(f"helper tool list, with it:              {len(after):>2} tools  {an:>6} B  ~{at} tok")
    print(f"difference: +{an - bn} B / ~+{at - bt} tok  ({', '.join(names)})")


if __name__ == "__main__":
    main()
