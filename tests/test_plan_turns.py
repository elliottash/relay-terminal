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
from relay_core.provider import ProviderConfig                                 # noqa: E402
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


def resolver(main: ProviderConfig, preset_id: str, roles=None):
    """A RoleResolver with every provider's key present, so nothing falls back for lack of a key."""
    return RoleResolver(main, preset_id, roles or {}, key_lookup=lambda preset: "key")


class PlanTurnTests(unittest.TestCase):
    """One plan-mode ask: which config served it, what it said, and what came back."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        RecordingProvider.served = []
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

    # ----- nothing to swap: no event, no provider change ------------------------------
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
