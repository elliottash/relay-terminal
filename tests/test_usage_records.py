# SPDX-License-Identifier: AGPL-3.0-or-later
"""Usage records (#0C0V step 1, step 8): one record per turn in the session file, subagent
threads counted once into their owner session, a list-price estimate where the provider gave no
cost, and the size of a guest's handover brief.

Offline throughout: providers and guest harnesses are scripted, the OpenRouter prices are patched
in, and every session directory is temporary.
"""
import json
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

from relay_core import openrouter_catalog, sessions
from relay_core.agent import Agent
from relay_core.agents_defs import load_catalog
from relay_core.queue import TurnSupervisor
from relay_core.session_protocol import SessionCommands
from relay_core.subagents import SubagentFactory, SubagentManager

sys.path.insert(0, str(Path(__file__).parent))
import test_guest_handover as handover  # noqa: E402
from test_session_threads import OWNER, THREAD, Home, thread  # noqa: E402
from test_subagents import CONFIG, Hub, MainProvider, Recorder, SubProvider, call, calls, final  # noqa: E402
from tests.guest_harness_fake import ev  # noqa: E402

PRICES = {"prompt": 2.0, "completion": 10.0, "cache_read": 0.5}


class Requests:
    """One scripted request per usage dict, the last one answering in text."""

    def __init__(self, *usages):
        self.usages = list(usages)

    def complete(self, messages, tools, emit, cancel):
        if not tools:
            return {"role": "assistant", "content": ""}
        usage = self.usages.pop(0)
        if usage is not None:
            emit({"event": "usage", "usage": usage})
        if self.usages:
            return {"role": "assistant", "content": "", "tool_calls": [
                {"id": f"c{len(self.usages)}", "type": "function",
                 "function": {"name": "list_files", "arguments": "{}"}}]}
        return {"role": "assistant", "content": "Done."}

    def cancel(self):
        pass


class HelperTests(unittest.TestCase):
    def test_estimate_prices_cached_and_written_tokens_apart_from_the_rest(self):
        usage = {"prompt_tokens": 1_000_000, "cached_tokens": 600_000, "cache_write_tokens": 100_000,
                 "completion_tokens": 100_000}
        # 300k fresh at $2, 600k cached at $0.50, 100k written at the prompt price (none listed),
        # 100k out at $10.
        self.assertAlmostEqual(sessions.estimate_cost(usage, PRICES), 0.6 + 0.3 + 0.2 + 1.0)
        self.assertAlmostEqual(sessions.estimate_cost({"prompt_tokens": 500_000}, {"prompt": 2.0, "completion": 1.0}), 1.0)
        self.assertIsNone(sessions.estimate_cost(usage, None))
        self.assertIsNone(sessions.estimate_cost({"total_tokens": 9}, PRICES))

    def test_the_estimate_is_its_own_total_and_old_files_still_load(self):
        totals = sessions.empty_usage()
        sessions.add_usage(totals, {"prompt_tokens": 10, "cost_estimate": 0.25})
        sessions.add_usage(totals, {"prompt_tokens": 10, "cost": 0.5})
        self.assertEqual((totals["cost"], totals["cost_estimate"]), (0.5, 0.25))
        self.assertEqual(sessions.load_usage(totals), totals)
        self.assertNotIn("cost_estimate", sessions.load_usage({"prompt_tokens": 3}))
        self.assertEqual(sessions.load_turns_usage(None), [])            # a file from before #0C0V
        self.assertEqual(sessions.load_children_usage("junk"), {})

    def test_a_turn_record_leaves_out_what_nobody_reported(self):
        usage = sessions.add_usage(sessions.empty_usage(), {"prompt_tokens": 50, "completion_tokens": 5})
        entry = sessions.turn_usage_entry(3, "t-1", "glm-5.3", "native", usage, {"last_prompt_tokens": 50})
        self.assertEqual(entry, {"turn": 3, "turn_id": "t-1", "model": "glm-5.3", "source": "native",
                                 "requests": 1, "prompt_tokens": 50, "completion_tokens": 5,
                                 "last_prompt_tokens": 50})
        loaded = sessions.load_turns_usage([entry, "junk", {"turn": -1, "source": "alien", "cost": "x"}]
                                           + [entry] * sessions.MAX_TURN_USAGE)
        self.assertEqual(len(loaded), sessions.MAX_TURN_USAGE)
        self.assertEqual(sessions.sum_usage([usage, {"requests": 2, "cost": 0.1}, None]),
                         {"prompt_tokens": 50, "completion_tokens": 5, "total_tokens": 55, "requests": 3, "cost": 0.1})


class AgentRecordTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.sessions = str(Path(self.temp.name) / "s")
        self.events = []

    def agent(self, provider, config=CONFIG, **kwargs):
        return Agent(config, self.temp.name, self.events.append, provider=provider, session_dir=self.sessions,
                     completion_check=False, track_requests=False, todo_tool=False, **kwargs)

    def test_each_turn_is_recorded_with_its_last_single_prompt_and_saved(self):
        provider = Requests({"prompt_tokens": 1000, "completion_tokens": 10, "cached_tokens": 800},
                            {"prompt_tokens": 1200, "completion_tokens": 20, "cached_tokens": 1000},
                            {"prompt_tokens": 1500, "completion_tokens": 30})
        agent = self.agent(provider)
        with mock.patch.object(openrouter_catalog, "prices_for", return_value=None):
            agent.ask("look around")
            provider.usages = [{"prompt_tokens": 1600, "completion_tokens": 5}]
            agent.ask("and again")
        first, second = agent.turns_usage
        self.assertEqual(first, {"turn": 1, "turn_id": first["turn_id"], "model": "mock", "source": "native",
                                 "requests": 3, "prompt_tokens": 3700, "completion_tokens": 60,
                                 "cached_tokens": 1800,
                                 # the latest request's prompt, never the sum of the three
                                 "last_prompt_tokens": 1500})
        self.assertEqual((second["turn"], second["requests"], second["last_prompt_tokens"]), (2, 1, 1600))
        self.assertNotIn("cost", first)                       # not reported, and no price to estimate
        self.assertNotIn("cost_estimate", first)
        summary = [e for e in self.events if e["event"] == "turn_summary"][-1]
        self.assertEqual(summary["usage"], second)            # the same record, live, on the stream
        data = json.loads(agent.store.path(agent.session_id).read_text())
        self.assertEqual(data["turns_usage"], [first, second])
        resumed = self.agent(Requests())
        resumed.resume(agent.session_id)
        self.assertEqual(resumed.turns_usage, [first, second])

    def test_an_unpriced_request_gets_a_list_price_estimate_and_a_priced_one_does_not(self):
        agent = self.agent(Requests({"prompt_tokens": 1_000_000, "cached_tokens": 500_000, "completion_tokens": 0}))
        with mock.patch.object(openrouter_catalog, "prices_for", return_value=PRICES) as prices:
            agent.ask("go")
        prices.assert_called_with(None, "mock")
        self.assertAlmostEqual(agent.turns_usage[-1]["cost_estimate"], 1.25)
        self.assertAlmostEqual(agent.usage_totals["cost_estimate"], 1.25)
        usage_event = [e for e in self.events if e["event"] == "usage"][-1]
        self.assertAlmostEqual(usage_event["usage"]["cost_estimate"], 1.25)
        self.assertNotIn("cost", agent.usage_totals)
        agent2 = self.agent(Requests({"prompt_tokens": 100, "completion_tokens": 1, "cost": 0.003}))
        with mock.patch.object(openrouter_catalog, "prices_for", return_value=PRICES) as prices:
            agent2.ask("go")
        prices.assert_not_called()
        self.assertEqual(agent2.turns_usage[-1]["cost"], 0.003)
        self.assertNotIn("cost_estimate", agent2.turns_usage[-1])

    def test_a_model_on_this_machine_is_never_priced(self):
        agent = self.agent(Requests({"prompt_tokens": 100, "completion_tokens": 1}))
        agent.preset = mock.Mock(local=True, id="local:llamacpp")
        with mock.patch.object(openrouter_catalog, "prices_for", return_value=PRICES) as prices:
            agent.ask("go")
        prices.assert_not_called()
        self.assertNotIn("cost_estimate", agent.turns_usage[-1])

    def test_a_turn_with_no_request_has_no_record_and_the_list_is_bounded(self):
        agent = self.agent(Requests(None))
        agent.ask("quiet")
        self.assertEqual(agent.turns_usage, [])
        with mock.patch.object(sessions, "MAX_TURN_USAGE", 2), \
                mock.patch.object(openrouter_catalog, "prices_for", return_value=None):
            for n in range(3):
                agent.provider.usages = [{"prompt_tokens": 10 + n, "completion_tokens": 1}]
                agent.ask(f"turn {n}")
        self.assertEqual([e["last_prompt_tokens"] for e in agent.turns_usage], [11, 12])
        self.assertEqual(agent.usage_totals["requests"], 3)      # older turns still in the totals

    def test_a_session_file_from_before_the_records_resumes(self):
        agent = self.agent(Requests({"prompt_tokens": 5, "completion_tokens": 1}))
        agent.ask("hello")
        path = agent.store.path(agent.session_id)
        data = json.loads(path.read_text())
        for key in ("turns_usage", "children_usage"):
            data.pop(key)
        path.write_text(json.dumps(data))
        resumed = self.agent(Requests())
        resumed.resume(agent.session_id)
        self.assertEqual((resumed.turns_usage, resumed.children_usage), ([], {}))
        self.assertEqual(resumed.usage_totals["requests"], 1)

    def test_a_child_is_counted_once_however_often_it_reports(self):
        agent = self.agent(Requests())
        agent.note_child_usage(THREAD, {"prompt_tokens": 100, "completion_tokens": 10, "total_tokens": 110, "requests": 1})
        agent.note_child_usage(THREAD, {"prompt_tokens": 300, "completion_tokens": 30, "total_tokens": 330, "requests": 3})
        agent.note_child_usage("not-an-id", {"requests": 9})
        self.assertEqual(agent.children_usage, {THREAD: {"prompt_tokens": 300, "completion_tokens": 30,
                                                         "total_tokens": 330, "requests": 3}})


class GuestRecordTests(unittest.TestCase):
    """A guest's usage is its turn's aggregate, so the record's last prompt is the guest's own
    context reading (#CP3M); a fresh harness's handover brief is measured once (step 8)."""

    setUp = handover.HandoverTests.setUp
    to_guest = handover.HandoverTests.to_guest

    def test_the_brief_is_measured_once_per_fresh_harness(self):
        self.agent.ask("Remember the codeword PELICAN-42 for later.")
        usage = ev("usage", input_tokens=9000, output_tokens=20, context_tokens=4000, context_window=258400)
        harness = self.to_guest("claude", [handover.reply("It was PELICAN-42.", usage),
                                           handover.reply("Still here.", usage)])
        with mock.patch.object(openrouter_catalog, "prices_for", return_value=None):
            self.agent.ask("What was the codeword?")
            self.agent.ask("And again?")
        brief = harness.sent[0]["prompt"].rsplit("\n\n", 1)[0]
        briefed, after = self.agent.turns_usage[-2:]
        self.assertEqual(briefed["handover_chars"], len(brief))
        self.assertEqual(briefed["handover_tokens"], (len(brief) + 3) // 4)
        self.assertEqual((briefed["source"], briefed["last_prompt_tokens"], briefed["prompt_tokens"]),
                         ("guest", 4000, 9000))
        self.assertNotIn("handover_chars", after)             # not sent again on the next turn
        self.assertNotIn("PELICAN-42", harness.sent[1]["prompt"])
        status = [e for e in self.events if e["event"] == "status" and "handover_chars" in e]
        self.assertEqual([(e["handover_chars"], e["handover_tokens"]) for e in status],
                         [(briefed["handover_chars"], briefed["handover_tokens"])])

    def test_a_guest_that_starts_the_pane_hands_nothing_over(self):
        self.to_guest("codex", [handover.reply("hi", ev("usage", input_tokens=10, output_tokens=1))])
        with mock.patch.object(openrouter_catalog, "prices_for", return_value=None):
            self.agent.ask("Hello")
        entry = self.agent.turns_usage[-1]
        self.assertNotIn("handover_chars", entry)
        self.assertNotIn("last_prompt_tokens", entry)         # the guest said nothing about its context


class ChildrenInfoTests(Home):
    def test_saved_info_counts_each_thread_once_beside_the_session(self):
        from test_conv_index import session
        data = session(OWNER, title="Owner")
        data["usage"] = {"prompt_tokens": 1000, "completion_tokens": 100, "total_tokens": 1100, "requests": 2}
        # A stale entry for the same thread: its own file is the newer word, and is not added to it.
        data["children_usage"] = {THREAD: {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2, "requests": 1}}
        data["turns_usage"] = [{"turn": 1, "turn_id": "t1", "model": "glm-5", "source": "native", "requests": 2}]
        self.store.save(data)
        self.store.save_thread(thread())
        self.store.save_thread(thread("c" * 32, parent=THREAD))
        sup = TurnSupervisor(Recorder())
        self.addCleanup(sup.shutdown)
        rec = Recorder()
        SessionCommands(sup, rec).handle("session_info", {"session_id": OWNER, "session_dir": str(self.dir)})
        info = rec.of("session_info")[-1]
        self.assertEqual(info["turns_usage"], data["turns_usage"])
        self.assertEqual(info["usage"]["total_tokens"], 1100)
        self.assertEqual(info["children_count"], 2)
        self.assertEqual(info["children_usage"], {"prompt_tokens": 200, "completion_tokens": 40,
                                                  "total_tokens": 240, "requests": 2})
        self.assertEqual(info["task_usage"], {"prompt_tokens": 1200, "completion_tokens": 140,
                                              "total_tokens": 1340, "requests": 4})

    def test_a_finished_subagent_lands_in_its_owner_and_in_the_live_info(self):
        workspace = self.home / "ws"
        workspace.mkdir()
        rec, hub = Recorder(), Hub()
        manager = SubagentManager(rec)
        factory = SubagentFactory(CONFIG, str(workspace), provider_factory=lambda config: SubProvider(hub))
        manager.configure(load_catalog(workspace, []), factory)
        self.addCleanup(manager.shutdown)
        spawn = call("agent", {"description": "scan", "prompt": "tool please", "subagent_type": "general"}, "s-1")
        turns = TurnSupervisor(rec)
        self.addCleanup(turns.shutdown)
        agent = Agent(CONFIG, str(workspace), turns.agent_emit, provider=MainProvider([calls(spawn), final("ok")]),
                      session_dir=str(self.dir))
        manager.attach(agent)
        manager.turns = turns
        turns.set_agent(agent)
        turns.submit("look around", "now", "q1")
        rec.wait(lambda e: e.get("event") == "done")
        thread_id = rec.of("subagent_started")[0]["thread_id"]
        child = agent.children_usage[thread_id]
        self.assertGreaterEqual(child["total_tokens"], 120)
        self.assertEqual(child, self.store.load_thread(thread_id)["usage"])
        commands = SessionCommands(turns, rec, subagents=manager)
        commands.handle("session_info", {"id": "i1"})
        info = rec.of("session_info")[-1]
        self.assertEqual(info["children_count"], 1)
        self.assertEqual(info["children_usage"]["total_tokens"], child["total_tokens"])
        self.assertEqual(info["task_usage"]["total_tokens"],
                         info["usage"]["total_tokens"] + child["total_tokens"])
        # `done` goes out before the turn's last autosave (Agent.ask's `finally`), so wait for it.
        deadline = time.monotonic() + 10
        while True:
            saved = json.loads(self.store.path(agent.session_id).read_text())
            if saved.get("children_usage") or time.monotonic() > deadline:
                break
            time.sleep(0.02)
        self.assertEqual(saved["children_usage"], {thread_id: child})


if __name__ == "__main__":
    unittest.main()
