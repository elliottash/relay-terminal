import sys, tempfile
sys.path[:0] = ["backend", ".", "tests"]
from unittest import mock
from relay_core.agent import Agent
from relay_core import guest_harness_provider as P
from relay_core.provider import ProviderConfig
from tests.guest_harness_fake import FakeHarness, ev

class Endpoint:
    def cancel(self): pass
    def complete(self, messages, tools, emit, cancel):
        return {"role": "assistant", "content": "Noted: the codeword is PELICAN-42."}

tmp = tempfile.mkdtemp()
events = []
agent = Agent(ProviderConfig("https://api.moonshot.ai/v1", "kimi-k3", "k", {}, 8192), tmp, events.append,
              provider=Endpoint(), completion_check=False, track_requests=False, todo_tool=False)
agent.ask("Remember the codeword PELICAN-42 for later.")
harness = FakeHarness([{"events": [ev("delta", text="ok")], "result": ("ok", "end", {})}], guest="claude",
                      session_id="cl-1", model="fable")
with mock.patch.object(P, "make_harness", return_value=harness):
    provider = P.start_provider("guest:claude", {}, tmp)       # what switch_model's apply_target does
agent.set_model(provider.config, "guest:claude", None, provider=provider)
P.attach(agent, provider)
agent.ask("What was the codeword?")
sent = harness.sent[-1]["prompt"]
print("relay transcript keeps it:", any("PELICAN-42" in str(m.get("content")) for m in agent.messages))
print("guest was sent the earlier conversation:", "PELICAN-42" in sent)
print("--- guest prompt tail ---"); print(sent[-300:])
