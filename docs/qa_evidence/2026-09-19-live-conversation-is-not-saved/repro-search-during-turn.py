#!/usr/bin/env python3
"""End to end: is a conversation still in its first turn findable by full-text search?

Uses the real default session directory (in a throwaway XDG_DATA_HOME) so the real index is written,
runs a turn whose first reply is a slow tool call, and searches the index for a word that is only in
the conversation on screen (the prompt) while that tool is still running.
"""
import json, os, pathlib, sys, tempfile, threading, time
jail = tempfile.mkdtemp(prefix="relay-midturn-")
os.environ["XDG_DATA_HOME"] = jail
os.environ["HOME"] = jail
sys.path.insert(0, "/home/elliott/repos/relay-terminal/backend")
from relay_core import conv_index, sessions
from relay_core.agent import Agent
from relay_core.provider import ProviderConfig

CONFIG = ProviderConfig("http://127.0.0.1:1/v1", "mock", "")
work = pathlib.Path(jail) / "work"
work.mkdir()
session_dir = sessions.default_session_dir(str(work))
print("session dir:", session_dir)


class SlowToolProvider:
    def __init__(self):
        self.calls = 0

    def complete(self, messages, tools, emit, cancel):
        self.calls += 1
        if self.calls == 1:
            return {"role": "assistant", "content": "", "tool_calls": [{
                "id": "c1", "type": "function",
                "function": {"name": "run_command", "arguments": json.dumps({"command": "sleep 8"})}}]}
        return {"role": "assistant", "content": "The flux capacitor holds 1.21 gigawatts."}

    def cancel(self):
        pass


events = []
agent = Agent(CONFIG, str(work), events.append, provider=SlowToolProvider(),
              session_dir=str(session_dir))
done = threading.Event()
threading.Thread(target=lambda: (agent.ask("how much flux does the capacitor hold?"), done.set()),
                 daemon=True).start()

time.sleep(2.0)
index = conv_index.ConversationIndex()
for word in ["flux", "capacitor", "gigawatts"]:
    res = index.search(word, scope="all", limit=10)
    titles = [(it.get("title") or "")[:40] for it in res.get("items", [])]
    print(f"during the turn, searching {word!r}: {len(res.get('items', []))} item(s) {titles}")
print("session file on disk during the turn:", (session_dir / f"{agent.session_id}.json").exists())
done.wait(40)
res = index.search("gigawatts", scope="all", limit=10)
print("after the turn, searching 'gigawatts':", [(it.get("title") or "")[:40] for it in res.get("items", [])])
