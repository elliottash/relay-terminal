# SPDX-License-Identifier: GPL-3.0-or-later
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
from relay_core.agent import Agent                                             # noqa: E402
from relay_core.presets import PRESETS                                         # noqa: E402
from relay_core.provider import ProviderConfig, ProviderError                                 # noqa: E402
from relay_core.roles import RoleResolver                                      # noqa: E402
from test_images import RecordingProvider                                      # noqa: E402

KIMI = ProviderConfig("https://api.moonshot.ai/v1", "kimi-k3", "key", {"reasoning_effort": "high"}, 8192)
KIMI_MAX = ProviderConfig("https://api.moonshot.ai/v1", "kimi-k3", "key", {"reasoning_effort": "max"}, 8192)
ANTHROPIC = ProviderConfig("https://api.anthropic.com/v1", "claude-opus-5", "key", {}, 8192)


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


def resolver(main: ProviderConfig, preset_id: str, roles=None):
    """A RoleResolver with every provider's key present, so nothing falls back for lack of a key."""
    return RoleResolver(main, preset_id, roles or {}, key_lookup=lambda preset: "key")


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

    def build(self, config, preset_id, roles=None):
        self.events: list = []
        return Agent(config, self.temp.name, self.events.append, preset_id=preset_id,
                     roles=resolver(config, preset_id, roles), track_requests=False,
                     todo_tool=False, completion_check=False)

    def plan_agent(self, config=KIMI, preset_id="kimi", roles=None):
        agent = self.build(config, preset_id, roles)
        agent.set_mode("plan")
        return agent

    def kinds(self):
        return [event["event"] for event in self.events]

    def event(self, name):
        return next((e for e in self.events if e["event"] == name), None)

    def served_configs(self):
        return TrackedProvider.served_configs

    # ----- a plan turn swaps, then goes back ------------------------------------------
    def test_a_plan_turn_runs_on_the_planning_config_and_comes_back(self):
        agent = self.plan_agent()
        original = agent.provider
        agent.ask("plan this change")
        route = self.event("plan_route")
        self.assertIsNotNone(route)
        self.assertEqual((route["model"], route["from_model"], route["preset"]),
                         ("kimi-k3", "kimi-k3", "kimi"))
        self.assertEqual((route["source"], route["effort"], route["scope"]), ("default", "max", "turn"))
        self.assertEqual(route["base_url"], "https://api.moonshot.ai/v1")
        self.assertIn("plan", route["text"].lower())
        # The model is unchanged; the reasoning the turn was built with is what moved.
        self.assertEqual(self.served_configs()[-1][1], {"reasoning_effort": "max"})
        self.assertEqual(RecordingProvider.served[-1][0], "kimi-k3")
        ended = self.event("plan_route_ended")
        self.assertIsNotNone(ended)
        self.assertEqual((ended["model"], ended["was"]), ("kimi-k3", "kimi-k3"))
        self.assertIs(agent.provider, original)
        self.assertEqual(agent.config.extra, {"reasoning_effort": "high"})
        self.assertEqual(self.events[-1]["event"], "done")

    def test_the_turn_after_a_plan_turn_is_back_on_the_panes_own_effort(self):
        agent = self.plan_agent()
        agent.ask("plan this change")
        agent.set_mode("build")
        agent.ask("now do it")
        self.assertEqual(len(RecordingProvider.served), 2)
        self.assertEqual(self.served_configs()[-1][1], {"reasoning_effort": "high"})
        self.assertEqual(self.kinds().count("plan_route"), 1)
        self.assertEqual(self.kinds().count("plan_route_ended"), 1)

    def test_the_provider_comes_back_even_when_the_plan_turn_fails(self):
        agent = self.plan_agent()
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
        self.assertEqual(agent.config.model, "claude-opus-5")
        self.assertEqual(agent.provider.config.model, "claude-opus-5")

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
        self.assertEqual(seen, [("claude-opus-5", "anthropic")])
        self.assertEqual(agent.session_data()["model"], "claude-opus-5")

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
        self.assertEqual(agent.config.model, "claude-opus-5")   # the restore ran, and kept nothing
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
                                        f"then back to claude-opus-5 ({anthropic}).")
        self.assertEqual((route["preset"], route["from_preset"]), ("kimi", "anthropic"))
        ended = self.event("plan_route_ended")
        self.assertEqual(ended["text"], f"Back to claude-opus-5 ({anthropic}).")
        self.assertEqual((ended["preset"], ended["was_preset"]), ("anthropic", "kimi"))
        statuses = [e["text"] for e in self.events if e["event"] == "status"]
        self.assertIn(f"Plan turn · kimi-k3 ({kimi})", statuses)
        self.assertIn(f"Back to claude-opus-5 ({anthropic})", statuses)

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

    def test_the_default_planning_role_drops_its_raised_effort_and_carries_on(self):
        # The default planning role is the pane's own model at max reasoning — the same provider,
        # one knob further. When that refuses, the turn continues at the pane's own effort.
        self.refusing(lambda config: config.extra.get("reasoning_effort") == "max")
        agent = self.plan_agent()
        agent.ask("plan this change")
        self.assertEqual([config[1] for config in self.served_configs()],
                         [{"reasoning_effort": "max"}, {"reasoning_effort": "high"}])
        self.assertEqual(self.events[-1]["event"], "done")
        self.assertEqual(agent.config.extra, {"reasoning_effort": "high"})
        self.assertEqual(self.kinds().count("plan_route_ended"), 1)

    def test_a_build_turn_emits_no_plan_route(self):
        agent = self.build(KIMI, "kimi")
        agent.ask("do this change")
        self.assertNotIn("plan_route", self.kinds())
        self.assertEqual(self.served_configs()[-1][1], {"reasoning_effort": "high"})
        self.assertEqual(self.events[-1]["event"], "done")

    def test_a_plan_turn_on_a_pane_already_at_max_emits_no_plan_route(self):
        # The default is the main model at max reasoning; there is nothing to raise, so the turn is
        # simply the pane's own turn (roles.RoleResolver.planning_target returns None).
        agent = self.plan_agent(KIMI_MAX)
        original = agent.provider
        agent.ask("plan this change")
        self.assertNotIn("plan_route", self.kinds())
        self.assertEqual(self.served_configs()[-1][1], {"reasoning_effort": "max"})
        self.assertIs(agent.provider, original)
        self.assertEqual(self.events[-1]["event"], "done")

    def test_a_provider_with_no_effort_knob_emits_no_plan_route(self):
        # Anthropic's compat layer ignores reasoning_effort: the swap would be a request it cannot
        # make, so plan turns stay on the pane's own model with nothing said.
        agent = self.plan_agent(ANTHROPIC, "anthropic")
        original = agent.provider
        agent.ask("plan this change")
        self.assertNotIn("plan_route", self.kinds())
        self.assertEqual(self.served_configs()[-1][1], {})
        self.assertIs(agent.provider, original)


if __name__ == "__main__":
    unittest.main()
