# SPDX-License-Identifier: AGPL-3.0-or-later
"""Plan-mode turns (owner, 2026-09-19): the planning role serves them, for that turn only.

Everything here runs offline against the recording provider from tests/test_images.py: no API key,
no network. That provider keeps the ProviderConfig it was built with, which is what these tests
assert on — a plan turn's default keeps the *same model* and raises its reasoning, so the model id
alone would not tell the two apart.
"""
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).parent))

from relay_core import agent as agent_module                                    # noqa: E402
from relay_core import guest_harness_provider as ghp                            # noqa: E402
from relay_core.agent import Agent                                             # noqa: E402
from relay_core.guest_harness import HarnessNotAvailable                       # noqa: E402
from relay_core.presets import PRESETS                                         # noqa: E402
from relay_core.provider import ProviderConfig, ProviderError                                 # noqa: E402
from relay_core.roles import RoleResolver, validate_tiers                      # noqa: E402
from guest_harness_fake import FakeHarness, ev                                 # noqa: E402
from test_images import RecordingProvider                                      # noqa: E402

KIMI = ProviderConfig("https://api.moonshot.ai/v1", "kimi-k3", "key", {"reasoning_effort": "high"}, 8192)
KIMI_MAX = ProviderConfig("https://api.moonshot.ai/v1", "kimi-k3", "key", {"reasoning_effort": "max"}, 8192)
ANTHROPIC = ProviderConfig("https://api.anthropic.com/v1", "claude-opus-5-5", "key", {}, 8192)


class TrackedProvider(RecordingProvider):
    """The recording provider, keeping the config each call actually ran under.

    RecordingProvider records the model that served each turn; a plan turn's default keeps the
    same model and raises its reasoning, so the whole config is what has to be recorded. Copied at
    the call, because the pane's own config object is mutated in place by set_effort.
    """

    served_configs: list = []

    def complete(self, messages, tools, emit, cancel):
        TrackedProvider.served_configs.append(
            (self.config.model, dict(self.config.extra), self.config.base_url))
        return super().complete(messages, tools, emit, cancel)


class RefusingPlanner(TrackedProvider):
    """The tracking provider, refusing outright for the configs ``refuse`` matches.

    Nothing is streamed before the refusal, which is the only case a turn may be re-routed from
    (protocol 15.2): a call that has put part of an answer on the screen keeps it.
    """

    refuse: tuple = ()           # predicates on the ProviderConfig; any match refuses the call

    def complete(self, messages, tools, emit, cancel):
        if any(matches(self.config) for matches in RefusingPlanner.refuse):
            TrackedProvider.served_configs.append(
                (self.config.model, dict(self.config.extra), self.config.base_url))
            raise ProviderError(f"Provider HTTP 503 for {self.config.model}.")
        return super().complete(messages, tools, emit, cancel)


def resolver(main: ProviderConfig, preset_id: str, roles=None, tiers=None, guests=()):
    """A RoleResolver with every provider's key present, so nothing falls back for lack of a key.
    ``guests`` are the guest ids whose harness "runs here" — injected, so no test ever finds the
    real claude or codex on PATH and starts it (protocol 29)."""
    return RoleResolver(main, preset_id, roles or {}, key_lookup=lambda preset: "key",
                        tiers=validate_tiers(tiers), guest_check=lambda guest_id: guest_id in guests)


class PlanTurnTests(unittest.TestCase):
    """One plan-mode ask: which config served it, what it said, and what came back."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        RecordingProvider.served = []
        RecordingProvider.raw = []
        RecordingProvider.windows = []
        RecordingProvider.agent = None
        self.addCleanup(setattr, RecordingProvider, "agent", None)
        TrackedProvider.served_configs = []
        patch = mock.patch.object(agent_module, "ChatProvider", TrackedProvider)
        patch.start()
        self.addCleanup(patch.stop)

    def build(self, config, preset_id, roles=None, tiers=None, guests=()):
        self.events: list = []
        return Agent(config, self.temp.name, self.events.append, preset_id=preset_id,
                     roles=resolver(config, preset_id, roles, tiers, guests), track_requests=False,
                     todo_tool=False, completion_check=False)

    def plan_agent(self, config=KIMI, preset_id="kimi", roles=None, tiers=None, guests=()):
        agent = self.build(config, preset_id, roles, tiers, guests)
        agent.set_mode("plan")
        return agent

    def kinds(self):
        return [event["event"] for event in self.events]

    def event(self, name):
        return next((e for e in self.events if e["event"] == name), None)

    def served_configs(self):
        return TrackedProvider.served_configs

    # A hand-pinned planning role: the only thing that routes a plan turn since 2026-09-22 (owner:
    # plan mode "is supposed to go into /high"; the pane goes there itself, #PH9G).
    PINNED = {"planning": {"preset": "glm", "model": "glm-5.3"}}

    # ----- a plan turn swaps, then goes back ------------------------------------------
    def test_plan_keeps_the_selected_high_model_and_preserves_main(self):
        agent = self.plan_agent(KIMI, "kimi", tiers={"high": [
            {"preset": "glm", "model": "glm-5.3", "effort": "max"}]})
        high = agent.roles.resolve("high")
        agent.set_model(high.config, high.preset_id)
        agent.set_effort(high.effort)
        agent.ask("plan this change")
        self.assertEqual(self.served_configs()[-1][0], "glm-5.3")
        self.assertEqual(agent.config.model, "glm-5.3")
        self.assertEqual(agent.roles.main_config.model, "kimi-k3")
        self.assertNotIn("plan_route", self.kinds())

    def test_a_plan_turn_runs_on_the_planning_config_and_comes_back(self):
        agent = self.plan_agent(roles=self.PINNED)
        original = agent.provider
        agent.ask("plan this change")
        route = self.event("plan_route")
        self.assertIsNotNone(route)
        self.assertEqual((route["model"], route["from_model"], route["preset"]),
                         ("glm-5.3", "kimi-k3", "glm"))
        self.assertEqual((route["source"], route["scope"]), ("configured", "turn"))
        self.assertIn("plan", route["text"].lower())
        self.assertEqual(RecordingProvider.served[-1][0], "glm-5.3")
        ended = self.event("plan_route_ended")
        self.assertIsNotNone(ended)
        self.assertEqual((ended["model"], ended["was"]), ("kimi-k3", "glm-5.3"))
        self.assertIs(agent.provider, original)
        self.assertEqual(agent.config.extra, {"reasoning_effort": "high"})
        self.assertEqual(self.events[-1]["event"], "done")

    def test_an_unpinned_plan_turn_is_the_panes_own_turn(self):
        # Plan mode adds no swap and no effort of its own (owner, 2026-09-22): entering it put the
        # pane on /high, so the turn runs on whatever that gave it, at that level — even with a
        # filled High list, which only /high reads.
        agent = self.plan_agent(KIMI, "kimi",
                                tiers={"high": [{"preset": "glm", "model": "glm-5.3", "effort": "high"}]})
        original = agent.provider
        agent.ask("plan this change")
        self.assertNotIn("plan_route", self.kinds())
        self.assertEqual(self.served_configs()[-1], ("kimi-k3", {"reasoning_effort": "high"}, KIMI.base_url))
        self.assertIs(agent.provider, original)
        self.assertEqual(self.events[-1]["event"], "done")

    def test_the_turn_after_a_plan_turn_is_back_on_the_panes_own_model(self):
        agent = self.plan_agent(roles=self.PINNED)
        agent.ask("plan this change")
        agent.set_mode("build")
        agent.ask("now do it")
        self.assertEqual(len(RecordingProvider.served), 2)
        self.assertEqual(self.served_configs()[-1][0], "kimi-k3")
        self.assertEqual(self.kinds().count("plan_route"), 1)
        self.assertEqual(self.kinds().count("plan_route_ended"), 1)

    def test_the_provider_comes_back_even_when_the_plan_turn_fails(self):
        agent = self.plan_agent(roles=self.PINNED)
        original = agent.provider
        with mock.patch.object(TrackedProvider, "complete", side_effect=RuntimeError("boom")):
            agent.ask("plan this change")
        self.assertEqual(self.events[-1]["event"], "error")
        self.assertIsNotNone(self.event("plan_route_ended"))
        self.assertIs(agent.provider, original)
        self.assertEqual(agent.config.extra, {"reasoning_effort": "high"})

    def test_a_configured_planning_model_serves_the_plan_turn(self):
        agent = self.plan_agent(roles={"planning": {"preset": "glm", "model": "glm-5.3"}})
        agent.ask("plan this change")
        route = self.event("plan_route")
        self.assertEqual((route["model"], route["preset"], route["source"]),
                         ("glm-5.3", "glm", "configured"))
        self.assertEqual(self.served_configs()[-1][2], "https://api.z.ai/api/paas/v4")
        self.assertEqual(RecordingProvider.served[-1][0], "glm-5.3")
        self.assertEqual(agent.provider.config.model, "kimi-k3")

    # ----- a planning model on another vendor: its dialect, its window, the pane's name ----
    def kimi_planner(self):
        """An Anthropic pane whose planning role is pinned to Kimi: two vendors, two dialects,
        two context windows, and a reasoning dialect the pane itself never writes."""
        agent = self.plan_agent(ANTHROPIC, "anthropic",
                                {"planning": {"preset": "kimi", "model": "kimi-k3"}})
        RecordingProvider.agent = agent
        return agent

    def test_a_pinned_planning_model_gets_the_history_in_its_dialect_and_its_own_window(self):
        # The swap used to replace the provider and nothing else: the conversation reached the
        # planning model in the pane's reasoning dialect, and a compaction inside the turn measured
        # it against the pane's context window. Kimi refuses an assistant tool-call message with no
        # `reasoning_content`, and an Anthropic pane never writes one.
        agent = self.kimi_planner()
        agent.messages.append({"role": "assistant", "content": "",
                               "tool_calls": [{"id": "c0", "type": "function",
                                               "function": {"name": "read_file", "arguments": "{}"}}]})
        agent.messages.append({"role": "tool", "tool_call_id": "c0", "content": "ok"})
        agent.ask("plan this change")
        self.assertEqual(RecordingProvider.served[-1][0], "kimi-k3")
        adapted = [m for m in RecordingProvider.raw[-1] if m.get("tool_calls")]
        self.assertTrue(adapted and adapted[0].get("reasoning_content"))
        self.assertEqual(RecordingProvider.windows[-1], PRESETS["kimi"].context_window)
        # ... and the pane is measured against its own window again once the turn is over.
        self.assertEqual(agent.context.window, PRESETS["anthropic"].context_window)
        self.assertEqual(agent.config.model, "claude-opus-5-5")
        self.assertEqual(agent.provider.config.model, "claude-opus-5-5")

    def test_a_save_while_a_plan_turn_is_routed_records_the_panes_own_model(self):
        # `_autosave_soon` fires inside the turn, and a title or summary thread saves from its own:
        # neither may write the planning model into the session file, which is what the sessions
        # list, the resume picker and the index read.
        agent = self.kimi_planner()
        seen = []
        original = TrackedProvider.complete

        def complete(provider, messages, tools, emit, cancel):
            seen.append((agent.session_data()["model"], agent.session_data()["preset"]))
            return original(provider, messages, tools, emit, cancel)

        with mock.patch.object(TrackedProvider, "complete", complete):
            agent.ask("plan this change")
        self.assertEqual(RecordingProvider.served[-1][0], "kimi-k3")
        self.assertEqual(seen, [("claude-opus-5-5", "anthropic")])
        self.assertEqual(agent.session_data()["model"], "claude-opus-5-5")

    def test_a_model_switch_during_a_plan_turn_lands_after_the_restore(self):
        # The switch waits for the turn's end, as it does under a failover (#G9VE): landing it at a
        # step boundary would put the pane on the new model and then have the restore undo it.
        target = PRESETS["openai"]
        other = ProviderConfig(target.base_url, target.model, "k")
        agent = self.kimi_planner()
        seen = {}
        original = TrackedProvider.complete

        def complete(provider, messages, tools, emit, cancel):
            # What the worker does for a set_model that arrives mid-turn, then what the turn loop
            # does at the next step boundary.
            seen["deferred"] = agent.defer_model(other, "openai")
            seen["at_step"] = agent.apply_pending_model("t1", 1, "step")
            seen["model_at_step"] = agent.config.model
            return original(provider, messages, tools, emit, cancel)

        with mock.patch.object(TrackedProvider, "complete", complete):
            agent.ask("plan this change")
        self.assertEqual(seen["deferred"]["applies"], "turn_end")
        self.assertEqual(seen["deferred"]["in_flight_model"], "kimi-k3")
        self.assertIsNone(seen["at_step"])                      # not while the swap is in force
        self.assertEqual(seen["model_at_step"], "kimi-k3")
        self.assertEqual(agent.config.model, "claude-opus-5-5")   # the restore ran, and kept nothing
        self.assertEqual(agent.apply_pending_model(at="turn_end")["model"], target.model)
        self.assertEqual((agent.config.model, agent.context.window),
                         (target.model, target.context_window))

    def test_the_route_and_the_return_name_the_preset_on_the_note_and_on_the_status(self):
        # One shape for plan, vision and failover (protocol 15.2.2): both ends of both notes name
        # the preset as well as the model, and the return says so on the status line too, which a
        # plan turn did not emit at all.
        agent = self.kimi_planner()
        agent.ask("plan this change")
        kimi, anthropic = PRESETS["kimi"].label, PRESETS["anthropic"].label
        route = self.event("plan_route")
        self.assertEqual(route["text"], f"Plan mode · this turn runs on kimi-k3 ({kimi}), "
                                        f"then back to claude-opus-5-5 ({anthropic}).")
        self.assertEqual((route["preset"], route["from_preset"]), ("kimi", "anthropic"))
        ended = self.event("plan_route_ended")
        self.assertEqual(ended["text"], f"Back to claude-opus-5-5 ({anthropic}).")
        self.assertEqual((ended["preset"], ended["was_preset"]), ("anthropic", "kimi"))
        statuses = [e["text"] for e in self.events if e["event"] == "status"]
        self.assertIn(f"Plan turn · kimi-k3 ({kimi})", statuses)
        self.assertIn(f"Back to claude-opus-5-5 ({anthropic})", statuses)

    # ----- nothing to swap: no event, no provider change ------------------------------
    # ----- a planning model whose provider is down (owner, 2026-09-19) -----------------
    def refusing(self, predicate):
        RefusingPlanner.refuse = (predicate,)
        self.addCleanup(setattr, RefusingPlanner, "refuse", ())
        patch = mock.patch.object(agent_module, "ChatProvider", RefusingPlanner)
        patch.start()
        self.addCleanup(patch.stop)

    def test_a_planning_model_that_will_not_answer_hands_the_turn_back_to_the_pane(self):
        # Protocol 15.2.3: this used to fail the turn outright, with the pane's own model sitting
        # there able to answer.
        self.refusing(lambda config: config.model == "glm-5.3")
        agent = self.plan_agent(roles={"planning": {"preset": "glm", "model": "glm-5.3"}})
        agent.ask("plan this change")
        self.assertEqual([config[0] for config in self.served_configs()], ["glm-5.3", "kimi-k3"])
        self.assertEqual(self.events[-1]["event"], "done")
        dropped = next(e for e in self.events if e["event"] == "provider_retry")
        self.assertEqual(dropped["reason"], "route_dropped")
        self.assertEqual((dropped["from_model"], dropped["to_model"]), ("glm-5.3", "kimi-k3"))
        self.assertIn("Planning model", dropped["text"])
        self.assertIn("is not answering", dropped["text"])
        # The routing really ended: one plan_route_ended, before the note, and the pane is its own.
        self.assertEqual(self.kinds().count("plan_route_ended"), 1)
        self.assertLess(self.kinds().index("plan_route_ended"), self.kinds().index("provider_retry"))
        self.assertEqual(agent.config.extra, {"reasoning_effort": "high"})
        self.assertEqual(agent.provider.config.model, "kimi-k3")
        self.assertIsNone(agent._planning)

    def test_a_build_turn_emits_no_plan_route(self):
        agent = self.build(KIMI, "kimi")
        agent.ask("do this change")
        self.assertNotIn("plan_route", self.kinds())
        self.assertEqual(self.served_configs()[-1][1], {"reasoning_effort": "high"})
        self.assertEqual(self.events[-1]["event"], "done")

    def test_a_plan_turn_on_a_pane_already_at_max_emits_no_plan_route(self):
        # Unpinned, a plan turn is simply the pane's own turn (planning_target returns None).
        agent = self.plan_agent(KIMI_MAX)
        original = agent.provider
        agent.ask("plan this change")
        self.assertNotIn("plan_route", self.kinds())
        self.assertEqual(self.served_configs()[-1][1], {"reasoning_effort": "max"})
        self.assertIs(agent.provider, original)
        self.assertEqual(self.events[-1]["event"], "done")

    def test_a_provider_with_no_effort_knob_emits_no_plan_route(self):
        # Unpinned, plan turns stay on the pane's own model with nothing said.
        agent = self.plan_agent(ANTHROPIC, "anthropic")
        original = agent.provider
        agent.ask("plan this change")
        self.assertNotIn("plan_route", self.kinds())
        self.assertEqual(self.served_configs()[-1][1], {})
        self.assertIs(agent.provider, original)


GLM = PRESETS["glm-coding"]
GLM_CONFIG = ProviderConfig(GLM.base_url, GLM.model, "key", dict(GLM.extra), 8192)
PLAN_TEXT = "# Split the widget\n\n## Goal\n\nTwo files.\n\n1. Move it.\n2. Test it.\n"


class GuestPlanTurnTests(PlanTurnTests):
    """A `guest:` pin of the planning role serves a plan turn through its harness (protocol 13.7;
    the pin is the only way off the pane's own model since card #HR5E). Everything runs on the
    scripted FakeHarness: no test starts a real claude or codex."""

    PIN = {"planning": {"preset": "guest:codex", "effort": "xhigh"}}

    def harness(self, script=None, **kwargs):
        kwargs.setdefault("guest", "codex")
        kwargs.setdefault("session_id", "cx-1")
        kwargs.setdefault("model", "gpt-5.5-codex")
        made = FakeHarness(script if script is not None else
                           [{"events": [ev("delta", text=PLAN_TEXT)], "result": (PLAN_TEXT, "end", {})}],
                           **kwargs)
        patch = mock.patch.object(ghp, "make_harness", return_value=made)
        patch.start()
        self.addCleanup(patch.stop)
        return made

    def glm_planner(self, roles=None, tiers=None):
        agent = self.plan_agent(GLM_CONFIG, "glm-coding", roles=roles or self.PIN, tiers=tiers,
                                guests=("codex",))
        agent.messages.append({"role": "user", "content": "earlier: look at widget.py", "relay_kind": "prompt"})
        agent.messages.append({"role": "assistant", "content": "",
                               "tool_calls": [{"id": "c0", "type": "function",
                                               "function": {"name": "read_file", "arguments": '{"path": "widget.py"}'}}]})
        agent.messages.append({"role": "tool", "tool_call_id": "c0", "content": "def widget(): pass"})
        agent.messages.append({"role": "assistant", "content": "It is one function."})
        return agent

    def test_a_plan_turn_on_a_glm_pane_runs_through_codex_and_comes_back(self):
        harness = self.harness()
        agent = self.glm_planner()
        own = agent.provider
        agent.ask("plan splitting the widget")
        self.assertEqual(self.events[-1]["event"], "done")
        # Started for the turn: in the pane's workspace, read-only, at the entry's level in codex's
        # own word, on the guest's own model (the entry named none).
        self.assertEqual(harness.starts, [{"cwd": self.temp.name, "model": None, "resume": None, "fork": False,
                                           "permissions": agent_module.PLAN_GUEST_PERMISSIONS, "effort": "xhigh"}])
        self.assertEqual(agent_module.PLAN_GUEST_PERMISSIONS, "deny")
        # The route says where the turn went and where it comes back to, guest and all.
        route = self.event("plan_route")
        self.assertEqual((route["model"], route["preset"], route["base_url"], route["effort"], route["guest"],
                          route["guest_session"], route["from_model"], route["from_preset"]),
                         ("gpt-5.5-codex", "guest:codex", "harness://codex", "xhigh", "codex", "cx-1",
                          "glm-5.3", "glm-coding"))
        self.assertEqual(route["text"], f"Plan mode · this turn runs on gpt-5.5-codex (Codex), "
                                        f"then back to glm-5.3 ({GLM.label}).")
        self.assertIn("Plan turn · gpt-5.5-codex (Codex)", [e["text"] for e in self.events if e["event"] == "status"])
        # The harness took one prompt: the plan rules in its own terms, the conversation so far
        # (the user's words, the tools Relay's agent called, their results) and the request.
        self.assertEqual(len(harness.sent), 1)
        sent = harness.sent[0]["prompt"]
        # Project instructions may precede the planning directive (#GPF7).
        self.assertIn("PLAN MODE.", sent)
        self.assertLess(sent.index("PLAN MODE."), sent.index("The request to plan:"))
        self.assertIn("do not modify the workspace", sent)
        self.assertIn("reply with the complete implementation plan as Markdown", sent)
        self.assertIn("User:\nearlier: look at widget.py", sent)
        self.assertIn("(called read_file {\"path\": \"widget.py\"})", sent)
        self.assertIn("Tool result:\ndef widget(): pass", sent)
        self.assertIn("Assistant:\nIt is one function.", sent)
        self.assertTrue(sent.endswith("The request to plan:\n\nplan splitting the widget"), sent[-120:])
        self.assertNotIn("write_plan", sent)              # Relay's plan note is not the guest's
        self.assertNotIn("[Relay context", sent)
        # Its reply is the plan: saved exactly as write_plan saves one, and said so.
        written = self.event("plan_written")
        self.assertEqual((written["title"], written["guest"]), ("Split the widget", "codex"))
        self.assertTrue(written["path"].startswith(str(agent.plans_dir)))
        self.assertEqual(Path(written["path"]).read_text(), PLAN_TEXT)
        self.assertEqual(agent.plan_path, written["path"])
        self.assertEqual(agent.messages[-1], {"role": "assistant", "content": PLAN_TEXT})
        # The pane's own model was never asked, and the pane is back on it, the guest ended.
        self.assertEqual(self.served_configs(), [])
        ended = self.event("plan_route_ended")
        self.assertEqual((ended["model"], ended["was"], ended["was_preset"]), ("glm-5.3", "gpt-5.5-codex", "guest:codex"))
        self.assertEqual(ended["text"], f"Back to glm-5.3 ({GLM.label}).")
        self.assertIs(agent.provider, own)
        self.assertFalse(agent._injected_provider)
        self.assertEqual((agent.config.model, agent.config.base_url, agent.effort),
                         ("glm-5.3", GLM.base_url, "high"))
        self.assertIsNone(agent.preset is None or None)
        self.assertEqual(agent.preset.id, "glm-coding")
        self.assertTrue(harness.closed)
        self.assertIsNone(agent._planning)
        # ... and the next plan turn starts a fresh session (one harness per plan turn).
        second = self.harness(session_id="cx-2")
        agent.ask("plan it again")
        self.assertEqual(self.events[-1]["event"], "done")
        self.assertEqual(len(second.starts), 1)
        self.assertTrue(second.closed)
        self.assertEqual(self.kinds().count("plan_route"), 2)
        self.assertEqual(self.served_configs(), [])

    def test_a_build_turn_never_starts_the_guest(self):
        harness = self.harness()
        agent = self.build(GLM_CONFIG, "glm-coding", roles=self.PIN, guests=("codex",))
        agent.ask("do it")
        self.assertEqual(self.events[-1]["event"], "done")
        self.assertEqual(harness.starts, [])
        self.assertEqual(self.served_configs()[-1][0], "glm-5.3")

    def test_a_guest_that_will_not_start_is_said_and_the_turn_plans_without_it(self):
        harness = self.harness(start_error=HarnessNotAvailable("codex is not installed."))
        # Planning pinned onto the High tier itself (the pre-#HR5E default, kept as an explicit
        # pin): the guest entry plans the turn, and the entry below it is the fallback.
        tiers = {"high": [{"preset": "guest:codex", "effort": "xhigh"},
                          {"preset": "kimi", "model": "kimi-k3", "effort": "max"}]}
        agent = self.glm_planner(roles={"planning": {"tier": "high"}}, tiers=tiers)
        agent.ask("plan splitting the widget")
        self.assertEqual(self.events[-1]["event"], "done")
        self.assertTrue(harness.closed)
        status = next(e["text"] for e in self.events if e["event"] == "status" and "could not start" in e["text"])
        self.assertIn("Codex could not start for this plan turn", status)
        self.assertIn("not installed", status)
        self.assertIn("planning without it", status)
        # The entry below the guest served the turn, as a plan turn, and the pane came back.
        route = self.event("plan_route")
        self.assertEqual((route["model"], route["preset"], route["effort"]), ("kimi-k3", "kimi", "max"))
        self.assertNotIn("guest", route)
        self.assertEqual([c[0] for c in self.served_configs()], ["kimi-k3"])
        self.assertIsNotNone(self.event("plan_route_ended"))
        self.assertEqual(agent.config.model, "glm-5.3")
        self.assertIsNone(self.event("plan_written"))
        # Pinned to the guest alone: the pane as it is plans, with no route and no boost.
        self.harness(start_error=HarnessNotAvailable("codex is not installed."))
        agent = self.glm_planner()
        agent.ask("plan splitting the widget")
        self.assertEqual(self.events[-1]["event"], "done")
        self.assertIsNone(self.event("plan_route"))
        self.assertEqual(self.served_configs()[-1], ("glm-5.3", dict(GLM.extra), GLM.base_url))

    def test_a_guest_whose_turn_fails_hands_the_plan_back_to_the_pane(self):
        # The harness answered the turn with an error: 15.2.3 as for any planning model that is
        # not answering — the routing ends, the guest with it, and the pane's own model plans.
        harness = self.harness([{"events": [], "raise": HarnessNotAvailable("codex crashed.")}])
        agent = self.glm_planner()
        agent.ask("plan splitting the widget")
        self.assertEqual(self.events[-1]["event"], "done")
        dropped = next(e for e in self.events if e["event"] == "provider_retry")
        self.assertEqual((dropped["reason"], dropped["from_model"], dropped["to_model"]),
                         ("route_dropped", "gpt-5.5-codex", "glm-5.3"))
        self.assertEqual(dropped["text"], f"Planning model gpt-5.5-codex (Codex) is not answering; "
                                          f"continuing on glm-5.3 ({GLM.label}).")
        self.assertTrue(harness.closed)
        self.assertEqual([c[0] for c in self.served_configs()], ["glm-5.3"])
        self.assertIsNone(self.event("plan_written"))
        self.assertEqual(agent.config.model, "glm-5.3")

    def test_an_empty_reply_writes_no_plan(self):
        self.harness([{"events": [], "result": ("", "end", {})}])
        agent = self.glm_planner()
        agent.ask("plan splitting the widget")
        self.assertEqual(self.events[-1]["event"], "done")
        self.assertIsNone(self.event("plan_written"))
        self.assertIsNone(agent.plan_path)

    def test_a_pane_that_is_itself_a_guest_plans_at_its_own_level(self):
        # Unpinned, a guest pane's plan turn is its own turn: no second harness, no swap, and no
        # per-turn effort boost (#HR5E's was withdrawn on 2026-09-22 — the /high entry sets it).
        pane = FakeHarness([{"events": [ev("delta", text="the plan")], "result": ("the plan", "end", {})}],
                           guest="claude", session_id="cl-1", model="claude-fake")
        pane.start(cwd=self.temp.name)
        provider = ghp.HarnessProvider(ProviderConfig("harness://claude", "claude-fake", "", {}, 32_768),
                                       pane, "claude")
        other = self.harness()
        self.events = []
        agent = Agent(provider.config, self.temp.name, self.events.append, preset_id="guest:claude",
                      provider=provider, roles=resolver(provider.config, "guest:claude",
                                                        guests=("codex", "claude")),
                      track_requests=False, todo_tool=False, completion_check=False)
        ghp.attach(agent, provider)
        agent.set_mode("plan")
        agent.ask("plan this")
        self.assertEqual(self.events[-1]["event"], "done")
        self.assertEqual(other.starts, [])
        self.assertEqual(len(pane.sent), 1)
        self.assertIsNone(self.event("plan_route"))
        self.assertFalse([call for call in pane.calls if call[0] == "set_effort"])
        self.assertIs(agent.provider, provider)


if __name__ == "__main__":
    unittest.main()
