"""What a restored guest pane asks its CLI to do, after a Relay restart (#PCJY).

Plays the pane's own rule headless, against whichever backend is on PYTHONPATH:

1. the pane resumes its saved conversation (`resume`) — a claude conversation;
2. it applies its guest preset (`configure guest:claude`), staging `guest.resume`
   from whatever the `state_loaded` event named (Pane::takeGuestRequest's rule).

The FakeHarness records the `start()` the worker performed, so the printout shows
whether the claude CLI would be launched fresh or with `--resume <previous id>`.
"""
import json
import sys
import tempfile
from pathlib import Path
from unittest import mock

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "tests"))

from relay_core.agent import Agent                       # noqa: E402
from relay_core.provider import ProviderConfig           # noqa: E402
from relay_core import guest_harness_provider as P       # noqa: E402
from guest_harness_fake import FakeHarness               # noqa: E402
from test_sessions import CONFIG, ScriptedProvider, text  # noqa: E402

GUEST_SESSION = "6f3c8a24-2c8d-4d61-9856-50ddc861b242"   # the pane's claude session from before the restart

temp = tempfile.TemporaryDirectory()
root = Path(temp.name) / "ws"
root.mkdir()
sessions = Path(temp.name) / "sessions"
sessions.mkdir()
events = []

# --- the pane before the restart: a conversation with two turns that ran on claude
agent = Agent(CONFIG(), str(root), events.append, provider=ScriptedProvider(
    [text("The codeword is PLUM."), text("PLUM.")]), preset_id="glm-coding",
    session_dir=str(sessions))
agent.ask("Remember the codeword PLUM.")
agent.ask("Say it back.")
conv = agent.session_id
path = sessions / f"{conv}.json"
saved = json.loads(path.read_text())
saved.update({  # what the guest provider's state hook writes while the pane runs claude
    "guest": "claude", "guest_session": GUEST_SESSION,
    "guest_cursors": {"claude:": {"session": GUEST_SESSION, "messages": 4}}})
path.write_text(json.dumps(saved))

# --- Relay restarts: the pane and the worker are new; the store is on disk
fresh = Agent(CONFIG(), str(root), events.append, provider=ScriptedProvider(name="B"),
              preset_id="glm-coding", session_dir=str(sessions))
event = fresh.resume(conv)
print("conversation resumed:", conv)
print("state_loaded names the guest:",
      {k: v for k, v in event.items() if k.startswith("guest")} or "(nothing)")

# --- the pane applies its guest preset, staging guest.resume as takeGuestRequest does
guest_block = {}
if event.get("guest_session"):
    guest_block["resume"] = event["guest_session"]
harness = FakeHarness(guest="claude", session_id="a-fresh-uuid", model="opus")
with mock.patch.object(P, "make_harness", return_value=harness):
    P.start_provider("guest:claude", {"guest": guest_block}, str(root))
start = harness.starts[0]
if start["resume"]:
    print("harness start: resume=%s fork=%s  ->  claude --resume %s" % (
        start["resume"], start["fork"], start["resume"]))
else:
    print("harness start: resume=None fork=%s  ->  claude --session-id <a fresh uuid> (empty memory)" % start["fork"])
