#!/bin/sh
# Q8TM Try it — what a model switch back to a guest sends, before and after this card.
# No model, no network: the scripted FakeHarness answers for the guest and a scripted
# native provider answers for GLM. Run from anywhere.
cd "$(dirname "$0")/../../.." || exit 1
PYTHONPATH="$PWD/backend:$PWD" python3 - <<'EOF'
import sys, tempfile
sys.path.insert(0, "backend"); sys.path.insert(0, ".")
from unittest import mock
from relay_core import guest_harness_provider as P
from relay_core.agent import Agent
from relay_core.provider import ProviderConfig
from tests.guest_harness_fake import FakeHarness

GLM = ProviderConfig("https://api.z.ai/api/paas/v4", "glm-5.3", "key", {}, 8192)
temp = tempfile.mkdtemp()
events = []


class ScriptedNative:
    """Stands in for the GLM endpoint: no network, one fixed answer."""
    def complete(self, messages, tools, emit, cancel):
        return {"role": "assistant", "content": "Noted: the codeword is PELICAN-42."}


def run(resume):
    """One pane: a turn on the Codex guest, a turn on GLM, then switch back to Codex and ask."""
    events.clear()
    agent = Agent(GLM, temp, events.append, preset_id="glm", completion_check=False,
                  track_requests=False, todo_tool=False)
    with mock.patch.object(P, "make_harness", return_value=FakeHarness(
            [{"events": [], "result": ("ok", "end", {})}], guest="codex",
            session_id="codex-1", model="codex-model")):
        provider = P.start_provider("guest:codex", {}, temp)
    agent.set_model(provider.config, "guest:codex", None, provider=provider)
    P.attach(agent, provider)
    agent.ask("Start on codex.")
    agent.set_model(GLM, "glm", None, provider=ScriptedNative())
    agent.ask("Remember the codeword PELICAN-42 for later.")
    back = FakeHarness([{"events": [], "result": ("PELICAN-42", "end", {})}], guest="codex",
                       session_id="codex-1", model="codex-model")
    with mock.patch.object(P, "make_harness", return_value=back):
        provider = P.start_provider(
            "guest:codex", {}, temp,
            resume_cursor=P.guest_cursor(agent, "codex") if resume else None)
    agent.set_model(provider.config, "guest:codex", None, provider=provider)
    P.attach(agent, provider)
    agent.ask("What was the codeword?")
    statuses = [e["text"] for e in events if e.get("event") == "status"
                and ("Resumed" in e["text"] or "Handed the conversation" in e["text"])]
    return back.sent[0]["prompt"], statuses[-1], back.starts[0]["resume"]


print("### BEFORE — switch back starts a fresh Codex session (main before this card)")
prompt, status, resumed = run(resume=False)
print("harness start: fresh session (resume=%r)" % resumed)
print("pane status  :", status)
print("prompt sent  :")
print(prompt)
print()
print("### AFTER — switch back resumes this pane's own Codex session (this card)")
prompt, status, resumed = run(resume=True)
print("harness start: resumed session %r" % resumed)
print("pane status  :", status)
print("prompt sent  :")
print(prompt)
EOF
