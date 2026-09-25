"""Card #12JX — write the sandbox a killed worker leaves behind.

One conversation the pane will resume, and one subagent thread the worker never wrote a stop
to: its file still says `running`, the state a hard kill leaves. The session and thread dicts
come from the test fixtures (tests/test_conv_index.session, tests/test_session_threads.thread)
and go through the real SessionStore, so the app reads exactly the shapes it wrote itself.
"""
import hashlib
import json
import os
import sys
import time

from relay_core import sessions
from test_conv_index import session
from test_session_threads import thread

SB = sys.argv[1]
WORKSPACE = os.path.join(SB, "project")
SESSION_ID = "a12b3c4d5e6f7890a12b3c4d5e6f7890"          # 32 hex chars, as SESSION_ID requires

# Where the worker puts this workspace's sessions: computed the same way agent_options does.
digest = hashlib.sha256(
    (WORKSPACE.rstrip("/") or "/").encode("utf-8")).hexdigest()[:16]
session_dir = os.path.join(SB, "home", "relay", "sessions", digest)
os.makedirs(session_dir, exist_ok=True)
store = sessions.SessionStore(session_dir)

data = session(SESSION_ID, workspace=WORKSPACE, title="Pelican survey — killed mid-run", turns=2)
store.save(data)
store.save_thread(thread(status="running", owner=SESSION_ID, description="Count the pelicans"))
row = store.threads(SESSION_ID)[0]
print(f"session {SESSION_ID} in {session_dir}")
print(f"thread  {row['id']} status={row['status']}")

# The saved window layout: one window, one tab, one pane that resumes that session.
state_dir = os.path.join(SB, "home", "relay", "state")
os.makedirs(state_dir, exist_ok=True)
layout = {"version": 1, "saved": int(time.time()),
          "windows": [{"geometry": {"x": 80, "y": 80, "w": 1100, "h": 760},
                       "screen": "", "current": 0,
                       "tabs": [{"pane": {"cwd": WORKSPACE, "workspace": WORKSPACE,
                                          "engine": "claude", "engine_core": "",
                                          "agent_role": "build", "preset": "", "model": "",
                                          "effort": "", "agent_mode": "build",
                                          "input_mode": "agent", "session_id": SESSION_ID,
                                          "scrollback": ""}}]}]}
with open(os.path.join(state_dir, "windows.json"), "w", encoding="utf-8") as fh:
    json.dump(layout, fh, indent=1)
print(f"layout  {os.path.join(state_dir, 'windows.json')}")
