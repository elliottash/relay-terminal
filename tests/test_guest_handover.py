# SPDX-License-Identifier: AGPL-3.0-or-later
"""No context is lost on a model change (#1V4F, owner 2026-09-22: "its critical that no context is
loss on model changes"). Every direction a pane can switch — endpoint to endpoint, endpoint to a
guest, a guest to an endpoint, one guest to another, and back — on the scripted FakeHarness, so no
test starts a real claude or codex (protocol 29)."""
import tempfile
import threading
import unittest
from unittest import mock

from relay_core import agent as agent_module
from relay_core import guest_harness_provider as P
from relay_core.agent import Agent
from relay_core.guest_harness import HarnessError
from relay_core.provider import ProviderConfig
from tests.guest_harness_fake import FakeHarness, ev

KIMI = ProviderConfig("https://api.moonshot.ai/v1", "kimi-k3", "key", {}, 8192)
GLM = ProviderConfig("https://api.z.ai/api/paas/v4", "glm-5.3", "key", {}, 8192)


class Endpoint:
    """A Relay-served model: remembers every conversation it was sent, answers with a line."""

    seen: list = []

    def __init__(self, config=None, *args, **kwargs):
        self.config = config

    def cancel(self):
        pass

    def complete(self, messages, tools, emit, cancel):
        Endpoint.seen.append([dict(m) for m in messages])
        return {"role": "assistant", "content": "Noted: the codeword is PELICAN-42."}


def reply(text, *events):
    return {"events": list(events) + [ev("delta", text=text)], "result": (text, "end", {})}


def said(conversation, needle):
    return any(needle in str(message.get("content")) for message in conversation)


class HandoverTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        Endpoint.seen = []
        patch = mock.patch.object(agent_module, "ChatProvider", Endpoint)
        patch.start()
        self.addCleanup(patch.stop)
        self.events = []
        self.agent = Agent(KIMI, self.temp.name, self.events.append, preset_id="kimi",
                           completion_check=False, track_requests=False, todo_tool=False)

    def to_guest(self, guest_id, script, request=None, harness=None):
        """What `SessionCommands.switch_model` does for a guest preset it has no harness for —
        including the relay-managed resume of this pane's own earlier session on that guest
        (#Q8TM): `apply_target` passes the pane's cursor for (guest, account)."""
        harness = harness or FakeHarness(script, guest=guest_id, session_id=f"{guest_id}-1",
                                         model=f"{guest_id}-model")
        with mock.patch.object(P, "make_harness", return_value=harness):
            provider = P.start_provider(f"guest:{guest_id}", request or {}, self.temp.name,
                                        resume_cursor=P.guest_cursor(self.agent, guest_id))
        previous = P.agent_provider(self.agent)
        self.agent.set_model(provider.config, f"guest:{guest_id}", None, provider=provider)
        P.attach(self.agent, provider)
        if previous is not None and previous is not provider:
            previous.close()
        return harness

    def to_endpoint(self, config, preset):
        """And for an endpoint preset while the pane is on a guest (or on another endpoint)."""
        P.detach(self.agent)
        self.agent.set_model(config, preset)

    def statuses(self):
        return [e["text"] for e in self.events if e["event"] == "status"]

    # ----- endpoint -> guest ---------------------------------------------------------------
    def test_a_guest_switched_in_mid_conversation_is_handed_the_conversation_once(self):
        self.agent.ask("Remember the codeword PELICAN-42 for later.")
        harness = self.to_guest("claude", [reply("It was PELICAN-42."), reply("Still here.")])
        self.agent.ask("What was the codeword?")
        first = harness.sent[0]["prompt"]
        self.assertIn(agent_module.CONTEXT_OPEN, first)
        self.assertIn("Remember the codeword PELICAN-42", first)          # the user's earlier turn
        self.assertIn("Noted: the codeword is PELICAN-42.", first)        # the other model's reply
        self.assertTrue(first.rstrip().endswith("What was the codeword?"))  # the prompt stays last
        self.assertIn("Handed the conversation so far to Claude Code.", self.statuses())
        # Once: the next turn is the guest's own session, which already holds it.
        self.agent.ask("And again?")
        self.assertNotIn("PELICAN-42", harness.sent[1]["prompt"])
        # Relay's own transcript is untouched by the brief.
        self.assertFalse(any(agent_module.CONTEXT_OPEN in str(m.get("content")) and "taking over" in
                             str(m.get("content")) for m in self.agent.messages))

    def test_a_pane_that_starts_on_a_guest_is_never_briefed(self):
        harness = self.to_guest("codex", [reply("hi"), reply("again")])
        self.agent.ask("Hello")
        self.agent.ask("Next")
        self.assertEqual([s["prompt"].split("\n\n")[-1] for s in harness.sent], ["Hello", "Next"])
        self.assertFalse(any(agent_module.CONTEXT_OPEN in s["prompt"] for s in harness.sent))

    def test_a_resumed_guest_session_holds_its_own_history_and_is_not_briefed(self):
        self.agent.ask("Remember the codeword PELICAN-42 for later.")
        harness = self.to_guest("codex", [reply("ok")], {"guest": {"resume": "codex-old"}})
        self.agent.ask("What was the codeword?")
        self.assertNotIn("PELICAN-42", harness.sent[0]["prompt"])

    def test_the_same_guest_on_another_model_keeps_its_session_and_is_not_rebriefed(self):
        self.agent.ask("Remember the codeword PELICAN-42 for later.")
        harness = self.to_guest("codex", [reply("ok"), reply("ok")])
        self.agent.ask("First guest turn")
        reused = P.switch_model(self.agent, "codex", {"guest": {"model": "gpt-other"}})
        self.assertIs(reused, P.agent_provider(self.agent))
        self.agent.ask("Second guest turn")
        self.assertIn("PELICAN-42", harness.sent[0]["prompt"])
        self.assertNotIn("PELICAN-42", harness.sent[1]["prompt"])

    # ----- guest -> endpoint ---------------------------------------------------------------
    def test_a_model_switched_in_after_a_guest_sees_what_the_guest_read_and_ran(self):
        harness = self.to_guest("codex", [reply(
            "The config sets the port.",
            ev("tool_started", call_id="c1", tool="run_command", input={"command": "cat app.conf"}),
            ev("tool_result", call_id="c1", tool="run_command", output="port = 8471\n", ok=True))])
        self.agent.ask("What port does the app use?")
        self.to_endpoint(GLM, "glm")
        self.agent.ask("Say the port again.")
        sent = Endpoint.seen[-1]
        self.assertTrue(said(sent, "What port does the app use?"))
        self.assertTrue(said(sent, "The config sets the port."))
        self.assertTrue(said(sent, "port = 8471"))                        # the guest's tool result
        calls = [c for m in sent for c in m.get("tool_calls") or []]
        self.assertEqual([c["id"] for c in calls], ["c1"])
        results = [m for m in sent if m.get("role") == "tool"]
        self.assertEqual([m["tool_call_id"] for m in results], ["c1"])
        self.assertTrue(harness.closed)

    def test_a_recorded_guest_result_is_capped_and_stays_valid_json(self):
        import json
        big = "x" * (P.RECORDED_TOOL_RESULT_CHARS + 500)
        self.to_guest("codex", [reply("done",
                                      ev("tool_started", call_id="c1", tool="run_command", input={"command": "yes"}),
                                      ev("tool_result", call_id="c1", tool="run_command", output=big, ok=True))])
        self.agent.ask("Run it")
        content = next(m["content"] for m in self.agent.messages if m.get("role") == "tool")
        self.assertTrue(json.loads(content)["output"].endswith(" […]"))

    # ----- guest -> guest, and back ----------------------------------------------------------
    def test_one_guest_hands_over_to_another_including_its_tool_results(self):
        self.to_guest("claude", [reply(
            "Found it.",
            ev("tool_started", call_id="c1", tool="read_file", input={"path": "notes.md"}),
            ev("tool_result", call_id="c1", tool="read_file", output="codeword: PELICAN-42", ok=True))])
        self.agent.ask("Read my notes.")
        codex = self.to_guest("codex", [reply("PELICAN-42")])
        self.agent.ask("What is the codeword in my notes?")
        first = codex.sent[0]["prompt"]
        self.assertIn("Read my notes.", first)
        self.assertIn("codeword: PELICAN-42", first)
        self.assertIn("Found it.", first)

    def test_switching_back_to_a_guest_resumes_its_session_and_gets_only_what_ran_since(self):
        self.to_guest("codex", [reply("ok")])
        self.agent.ask("Start on codex.")
        self.to_endpoint(GLM, "glm")
        self.agent.ask("Remember the codeword PELICAN-42 for later.")
        back = self.to_guest("codex", [reply("PELICAN-42")])
        self.agent.ask("What was the codeword?")
        self.assertEqual(back.starts[0]["resume"], "codex-1")      # its own session, restarted
        first = back.sent[0]["prompt"]
        self.assertIn("Remember the codeword PELICAN-42", first)  # the turn that ran while away
        self.assertNotIn("Start on codex.", first)                # the session already holds it
        self.assertTrue(first.rstrip().endswith("What was the codeword?"))
        self.assertTrue(any(s.startswith("Resumed") and "session codex-1" in s
                            for s in self.statuses()))
        # The cursor moved with the turn: a second round trip delivers only what ran since that.
        self.to_endpoint(GLM, "glm")
        self.agent.ask("Say it once more.")
        again = self.to_guest("codex", [reply("PELICAN-42")])
        self.agent.ask("And the codeword?")
        second = again.sent[0]["prompt"]
        self.assertIn("Say it once more.", second)
        self.assertNotIn("Start on codex.", second)
        self.assertNotIn("What was the codeword?", second)

    def test_a_return_with_nothing_to_deliver_sends_the_prompt_bare(self):
        self.to_guest("codex", [reply("ok"), reply("here")])
        self.agent.ask("Start on codex.")
        self.to_endpoint(GLM, "glm")            # switched away without a turn in between
        back = self.to_guest("codex", [reply("here")])
        self.agent.ask("And now?")
        self.assertEqual(back.starts[0]["resume"], "codex-1")
        self.assertEqual(back.sent[0]["prompt"], "And now?")
        self.assertTrue(any("nothing has happened since it left" in s for s in self.statuses()))

    def test_a_cursor_that_no_longer_fits_falls_back_to_the_full_handover(self):
        self.to_guest("codex", [reply("ok")])
        self.agent.ask("Start on codex.")
        # A fork on the pane: same shape, different history where the cursor points.
        self.agent.messages[-1]["content"] = "Rewound to a different turn."
        back = self.to_guest("codex", [reply("PELICAN-42")])
        self.agent.ask("What was the codeword?")
        self.assertEqual(back.starts[0]["resume"], "codex-1")
        first = back.sent[0]["prompt"]
        self.assertIn(agent_module.CONTEXT_OPEN, first)          # the full brief, not a delta
        self.assertIn("Start on codex.", first)
        self.assertIn("Rewound to a different turn.", first)
        self.assertTrue(any("handed it the whole thing" in s for s in self.statuses()))

    def test_a_guest_that_lost_the_session_starts_fresh_and_is_briefed_in_full(self):
        self.to_guest("codex", [reply("ok")])
        self.agent.ask("Start on codex.")
        self.to_endpoint(GLM, "glm")
        self.agent.ask("Remember the codeword PELICAN-42 for later.")
        pruned = FakeHarness([], guest="codex", session_id="codex-1", model="codex-model")
        pruned.missing_sessions = {"codex-1"}    # the guest rolled that session away
        fresh = FakeHarness([reply("PELICAN-42")], guest="codex", session_id="codex-2",
                            model="codex-model")
        with mock.patch.object(P, "make_harness", side_effect=[pruned, fresh]), \
                mock.patch.object(P.logs, "event") as log_event:
            provider = P.start_provider("guest:codex", {}, self.temp.name,
                                        resume_cursor=P.guest_cursor(self.agent, "codex"))
        self.agent.set_model(provider.config, "guest:codex", None, provider=provider)
        P.attach(self.agent, provider)
        self.agent.ask("What was the codeword?")
        self.assertEqual(pruned.starts[0]["resume"], "codex-1")  # tried, and refused
        self.assertTrue(pruned.closed)
        self.assertFalse(fresh.starts[0]["resume"])             # a fresh session, no resume
        first = fresh.sent[0]["prompt"]
        self.assertIn("Start on codex.", first)                  # the whole conversation again
        self.assertIn("Remember the codeword PELICAN-42", first)
        self.assertTrue(any(s.startswith("Handed the conversation so far") for s in self.statuses()))
        self.assertEqual(provider.switch_metrics,
                         {"guest": "codex", "account": "", "guest_resumed": False,
                          "guest_resume_fallback": True})
        self.assertTrue(any(call.args[1:2] == ("guest_resume_failed",)
                            for call in log_event.call_args_list))

    def test_a_failed_send_is_retried_with_the_catch_up_still_in_the_prompt(self):
        self.to_guest("codex", [reply("ok")])
        self.agent.ask("Start on codex.")
        self.to_endpoint(GLM, "glm")
        self.agent.ask("Remember the codeword PELICAN-42 for later.")
        harness = self.to_guest("codex", [
            {"raise": HarnessError("the guest died mid-turn")},
            reply("PELICAN-42")])
        provider = P.agent_provider(self.agent)
        self.agent.ask("What was the codeword?")
        self.assertTrue(any(e.get("event") == "error" for e in self.events))
        self.agent.ask("Once more: what was the codeword?")
        first, second = harness.sent[0]["prompt"], harness.sent[1]["prompt"]
        self.assertIn("Remember the codeword PELICAN-42", first)
        self.assertIn("Remember the codeword PELICAN-42", second)  # not consumed by the failure

    def test_a_cursor_is_only_resumed_under_the_account_that_ran_it(self):
        self.to_guest("codex", [reply("ok")])
        self.agent.ask("Start on codex.")
        self.assertEqual(P.guest_cursor(self.agent, "codex")["session"], "codex-1")
        self.assertIsNone(P.guest_cursor(self.agent, "codex", "other-login"))
        self.assertIsNone(P.guest_cursor(self.agent, "claude"))

    def test_cursors_survive_a_restart_in_the_session_file(self):
        self.to_guest("codex", [reply("ok")])
        self.agent.ask("Start on codex.")
        self.to_endpoint(GLM, "glm")
        self.agent.ask("Remember the codeword PELICAN-42 for later.")
        data = self.agent.session_data()
        self.assertEqual(data["guest_cursors"]["codex:"]["session"], "codex-1")

        # The pane restarts, loads the session (the transcript with it) and runs a turn elsewhere.
        restarted = Agent(GLM, self.temp.name, self.events.append, preset_id="glm",
                          completion_check=False, track_requests=False, todo_tool=False)
        restarted.messages = [dict(m) for m in self.agent.messages]
        P.resume_session(restarted, data, self.events.append)
        self.assertEqual(P.guest_cursor(restarted, "codex")["session"], "codex-1")
        restarted.ask("After the restart.")
        back = FakeHarness([reply("PELICAN-42")], guest="codex", session_id="codex-1",
                           model="codex-model")
        with mock.patch.object(P, "make_harness", return_value=back):
            provider = P.start_provider("guest:codex", {}, self.temp.name,
                                        resume_cursor=P.guest_cursor(restarted, "codex"))
        restarted.set_model(provider.config, "guest:codex", None, provider=provider)
        P.attach(restarted, provider)
        restarted.ask("What was the codeword?")
        self.assertEqual(back.starts[0]["resume"], "codex-1")
        first = back.sent[0]["prompt"]
        self.assertIn("After the restart.", first)      # only what ran since the codex turn
        self.assertNotIn("Start on codex.", first)      # not the conversation it already holds

    # ----- endpoint -> endpoint --------------------------------------------------------------
    def test_an_endpoint_switch_keeps_the_whole_conversation(self):
        self.agent.ask("Remember the codeword PELICAN-42 for later.")
        self.to_endpoint(GLM, "glm")
        self.agent.ask("What was the codeword?")
        sent = Endpoint.seen[-1]
        self.assertTrue(said(sent, "Remember the codeword PELICAN-42"))
        self.assertTrue(said(sent, "Noted: the codeword is PELICAN-42."))
        self.assertEqual(self.agent.config.model, "glm-5.3")


if __name__ == "__main__":
    unittest.main()
