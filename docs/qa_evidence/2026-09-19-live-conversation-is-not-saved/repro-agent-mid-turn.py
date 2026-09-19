#!/usr/bin/env python3
"""Is a conversation that is still in its first turn written to disk (and so indexed)?

Runs a real Agent with a real SessionStore, a stub provider whose first reply is a slow tool call
(`sleep 6`), and looks for the session file while that tool is still running.
"""
import json, pathlib, sys, tempfile, threading, time
sys.path.insert(0, "/home/elliott/repos/relay-terminal/backend")
from relay_core.agent import Agent
from relay_core.provider import ProviderConfig

CONFIG = ProviderConfig("http://127.0.0.1:1/v1", "mock", "")


class SlowToolProvider:
    def __init__(self):
        self.calls = 0

    def complete(self, messages, tools, emit, cancel):
        self.calls += 1
        if self.calls == 1:
            return {"role": "assistant", "content": "", "tool_calls": [{
                "id": "c1", "type": "function",
                "function": {"name": "run_command", "arguments": json.dumps({"command": "sleep 6"})}}]}
        return {"role": "assistant", "content": "All done: the widget report is ready."}

    def cancel(self):
        pass


tmp = tempfile.TemporaryDirectory()
sessions = pathlib.Path(tmp.name) / "sessions"
sessions.mkdir()
work = pathlib.Path(tmp.name) / "work"
work.mkdir()
events = []
agent = Agent(CONFIG, str(work), events.append, provider=SlowToolProvider(), session_dir=str(sessions))
path = sessions / f"{agent.session_id}.json"

done = threading.Event()
def turn():
    agent.ask("please investigate the widget report and tell me about the flux capacitor")
    done.set()

thread = threading.Thread(target=turn, daemon=True)
thread.start()
for label, wait in (("1s", 1.0), ("3s", 2.0), ("5s", 2.0)):
    time.sleep(wait)
    print(f"t≈{label}: session file exists = {path.exists()}, turns = {agent.turns}, "
          f"messages = {len(agent.messages)}, turn still running = {not done.is_set()}")
done.wait(30)
print("after the turn: file exists =", path.exists(), "| turns =", agent.turns)
if path.exists():
    data = json.loads(path.read_text())
    print("   saved turns:", data.get("turns"), "messages:", len(data.get("messages") or []))
print("tool calls seen by the executor:", [e.get("tool") for e in events if e.get("event") == "tool_started"])
tmp.cleanup()
