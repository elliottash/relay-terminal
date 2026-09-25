"""Card #12JX — prove the staged sandbox restores, without a GUI.

Runs what the worker runs when the restored pane resumes the session: `agent.resume(sid)` then
`SubagentManager.restore_threads(agent)` (the `_resume` path, card #12JX). Exits non-zero and
prints what failed if anything does not hold.
"""
import os
import sys

from relay_core.subagents import SubagentFactory, SubagentManager
from relay_core.agents_defs import load_catalog
from relay_core.agent import Agent
from relay_core import sessions
from test_subagents import CONFIG, Hub, MainProvider, SubProvider

SB = sys.argv[1]
WORKSPACE = os.path.join(SB, "project")
SESSION_ID = "a12b3c4d5e6f7890a12b3c4d5e6f7890"
store = sessions.SessionStore(str(sessions.default_session_dir(WORKSPACE)))

agent = Agent(CONFIG, WORKSPACE, lambda event: None, provider=MainProvider([]),
              session_dir=str(sessions.default_session_dir(WORKSPACE)))
agent.resume(SESSION_ID)                      # the restored pane's resume: the conversation loads
print(f"resume ok: {len(agent.messages)} messages")

manager = SubagentManager(lambda event: None)
manager.configure(load_catalog(WORKSPACE, []),
                  SubagentFactory(CONFIG, WORKSPACE, provider_factory=lambda config: SubProvider(Hub())))
restored = manager.restore_threads(agent)
rows = manager.list()
saved = store.load_thread(rows[0]["thread_id"]) if rows else {}
manager.shutdown()

checks = [restored == 1,
          [row["id"] for row in rows] == ["a1"],
          bool(rows) and rows[0]["status"] == "interrupted",
          saved.get("status") == "interrupted"]     # the file never says `running` again
for ok, what in zip(checks, ("restore_threads == 1", "roster holds a1", "roster status interrupted",
                             "thread file re-saved interrupted")):
    print(("PASS " if ok else "FAIL ") + what)
sys.exit(0 if all(checks) else 1)
