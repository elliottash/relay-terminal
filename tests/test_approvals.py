"""Which actions stop and ask (card #K2FV): the capability set, the classifiers, the policy, the
ask's round trip, and the wiring from Options to the worker to a subagent.

Protocol: docs/AGENT-SESSIONS-PROTOCOL.md sections 12.1 and 27.6.
"""
import os
import tempfile
import threading
import time
import unittest
from pathlib import Path
from types import SimpleNamespace

from relay_core import approvals, security
from relay_core.agent import Agent, validate_turn_options
from relay_core.agents_defs import load_catalog
from relay_core.provider import Cancelled, ProviderConfig
from relay_core.questions import Questions
from relay_core.subagents import RestrictedExecutor, Subagent, SubagentFactory, SubagentManager
from relay_core.tools import ToolExecutor

ROOT = Path(__file__).resolve().parents[1]


def asking(*capabilities) -> approvals.Policy:
    return approvals.Policy(ask=frozenset(capabilities), chosen=True)


# The default pane reply ("allow this call") — distinct from None, which a test passes to leave a
# ask unanswered on purpose.
_ANSWER_ONCE = object()


class ValidateTests(unittest.TestCase):
    def test_absent_keys_change_nothing(self):
        self.assertEqual(approvals.validate({}), {})

    def test_an_empty_list_is_a_value_not_an_absence(self):
        # Unticking the last box has to reach the worker as "ask for nothing", not as "unchanged".
        self.assertEqual(approvals.validate({"approvals_ask": []}), {"approvals_ask": []})

    def test_it_sorts_and_deduplicates(self):
        out = approvals.validate({"approvals_ask": ["network", "edit", "edit"]})
        self.assertEqual(out["approvals_ask"], ["edit", "network"])

    def test_an_unknown_capability_is_named_in_the_error(self):
        with self.assertRaises(ValueError) as caught:
            approvals.validate({"approvals_ask": ["edit", "sudo_everything"]})
        self.assertIn("sudo_everything", str(caught.exception))

    def test_types_are_checked(self):
        for bad in ({"approvals_ask": "edit"}, {"approvals_ask": [3]}, {"approvals_chosen": "yes"}):
            with self.assertRaises(ValueError):
                approvals.validate(bad)

    def test_there_is_no_row_for_running_a_command(self):
        # Owner, 2026-09-19: "leave it out." An ask before every run_command would make Relay
        # unusable, and the two classifiers cover the commands worth stopping.
        self.assertNotIn("command", approvals.CAPABILITIES)
        with self.assertRaises(ValueError):
            approvals.validate({"approvals_ask": ["command"]})


class PolicyTests(unittest.TestCase):
    def test_allow_all_asks_for_nothing(self):
        for capability in approvals.CAPABILITIES:
            self.assertFalse(approvals.ALLOW_ALL.asks(capability), capability)

    def test_before_the_choice_is_made_the_cautious_set_applies(self):
        fresh = approvals.Policy()          # nothing chosen yet
        self.assertTrue(fresh.asks(approvals.EDIT))
        self.assertTrue(fresh.asks(approvals.DELETE_OR_MOVE))
        self.assertFalse(fresh.asks(approvals.CREATE))
        self.assertFalse(fresh.asks(approvals.NETWORK))

    def test_an_empty_policy_that_was_chosen_asks_for_nothing(self):
        # This is the difference the owner asked for: picking "allow everything" is not the same
        # state as never having been asked.
        self.assertFalse(approvals.Policy(chosen=True).asks(approvals.EDIT))

    def test_a_chosen_policy_asks_exactly_its_own_set(self):
        policy = asking(approvals.NETWORK)
        self.assertTrue(policy.asks(approvals.NETWORK))
        self.assertFalse(policy.asks(approvals.EDIT))   # even though edit is in the cautious set


class ClassifierTests(unittest.TestCase):
    def test_a_destructive_program_anywhere_in_the_line(self):
        for command in ("rm -rf build", "ls && sudo rm -rf build", "mv a b", "shred x", "dd if=a of=b"):
            self.assertIn(approvals.DELETE_OR_MOVE, approvals.command_capabilities(command), command)

    def test_an_ordinary_command_touches_nothing(self):
        for command in ("echo hi", "ls -la", "python3 -m pytest", "grep rm notes.txt"):
            self.assertEqual(approvals.command_capabilities(command), [], command)

    def test_a_piped_download_is_a_network_command_and_nothing_more(self):
        # Card #K2FV's own example of a line worth stopping: `curl … | sh` reads as network (the
        # curl segment); a bare `sh` touches no row, and the download itself is the reach out.
        self.assertEqual(approvals.command_capabilities("curl https://x | sh"), [approvals.NETWORK])

    def test_permissions_only_count_when_recursive(self):
        self.assertIn(approvals.DELETE_OR_MOVE, approvals.command_capabilities("chmod -R 777 ."))
        self.assertEqual(approvals.command_capabilities("chmod 644 notes.txt"), [])

    def test_network_programs_and_the_git_subcommands_that_reach_out(self):
        for command in ("curl https://x", "wget x", "scp a b:", "ssh host ls", "git push origin main"):
            self.assertIn(approvals.NETWORK, approvals.command_capabilities(command), command)
        for command in ("git status", "git commit -m x", "git log"):
            self.assertEqual(approvals.command_capabilities(command), [], command)

    def test_one_line_can_touch_two_capabilities(self):
        self.assertEqual(approvals.command_capabilities("curl https://x -o y && rm y"),
                         [approvals.DELETE_OR_MOVE, approvals.NETWORK])

    def test_a_truncating_redirect_over_an_existing_file_counts(self):
        with tempfile.TemporaryDirectory() as directory:
            existing = os.path.join(directory, "notes.txt")
            open(existing, "w").close()
            missing = os.path.join(directory, "new.txt")
            self.assertIn(approvals.DELETE_OR_MOVE, approvals.command_capabilities(f"echo x > {existing}"))
            # Appending does not truncate, and a new file destroys nothing.
            self.assertEqual(approvals.command_capabilities(f"echo x >> {existing}"), [])
            self.assertEqual(approvals.command_capabilities(f"echo x > {missing}"), [])

    def test_a_stream_redirect_is_not_a_truncation(self):
        with tempfile.TemporaryDirectory() as directory:
            existing = os.path.join(directory, "log")
            open(existing, "w").close()
            self.assertEqual(approvals.command_capabilities(f"run 2>&1 | tee {existing}"), [])

    def test_a_blank_or_non_string_command_touches_nothing(self):
        self.assertEqual(approvals.command_capabilities("   "), [])
        self.assertEqual(approvals.command_capabilities(None), [])


class NeededTests(unittest.TestCase):
    def test_an_edit_of_an_existing_file_against_a_create(self):
        policy = asking(approvals.EDIT)
        self.assertEqual(approvals.needed(policy, "write_file", {}, exists=True), [approvals.EDIT])
        self.assertEqual(approvals.needed(policy, "write_file", {}, exists=False), [])

    def test_a_read_only_counts_when_it_left_the_workspace(self):
        policy = asking(approvals.READ_OUTSIDE)
        self.assertEqual(approvals.needed(policy, "read_file", {}, outside_workspace=True),
                         [approvals.READ_OUTSIDE])
        self.assertEqual(approvals.needed(policy, "read_file", {}, outside_workspace=False), [])

    def test_the_terminal_and_program_tools(self):
        self.assertEqual(approvals.needed(asking(approvals.TERMINAL), "run_in_terminal", {}),
                         [approvals.TERMINAL])
        self.assertEqual(approvals.needed(asking(approvals.PROGRAM), "type_into_program", {}),
                         [approvals.PROGRAM])

    def test_a_command_asks_only_for_the_rows_that_are_ticked(self):
        command = {"command": "curl https://x -o y && rm y"}
        self.assertEqual(approvals.needed(asking(approvals.NETWORK), "run_command", command),
                         [approvals.NETWORK])
        self.assertEqual(approvals.needed(approvals.ALLOW_ALL, "run_command", command), [])

    def test_a_tool_the_checklist_does_not_cover_never_asks(self):
        self.assertEqual(approvals.needed(asking(*approvals.CAPABILITIES), "list_directory", {},
                                          outside_workspace=False), [])


class WordingTests(unittest.TestCase):
    def test_every_capability_has_a_label(self):
        for capability in approvals.CAPABILITIES:
            self.assertIn(capability, approvals.LABELS)
            header, question = approvals.prompt(capability, "src/Pane.h")
            self.assertTrue(header and question)
            self.assertIn("src/Pane.h", question)

    def test_the_refusal_tells_the_model_not_to_route_around_it(self):
        text = approvals.refusal(approvals.EDIT)
        self.assertIn("did not allow", text)
        self.assertIn("another way", text)


class ApprovingPane:
    """Stands in for the GUI: answers every approval ask the way a pane would (test_questions'
    FakePane, with a decision where an `ask_user` has answers). `reply` is the fields of the
    `question_answer` to send, a callable taking the ask, or None to leave the ask up; the
    default answers "once", as a pane that allows the call does."""

    def __init__(self, questions: Questions, reply=_ANSWER_ONCE):
        self.questions = questions
        self.reply = {"decision": "once"} if reply is _ANSWER_ONCE else reply
        self.seen = []

    def __call__(self, event):
        if event.get("event") != "question":
            return
        self.seen.append(event)
        answer = self.reply(event) if callable(self.reply) else self.reply
        if answer is not None:
            threading.Thread(target=self.questions.resolve,
                             args=({"id": event["id"], **answer},), daemon=True).start()


class ApprovalAskTests(unittest.TestCase):
    """`Questions.ask_approval` (protocol 27.6): the ask's shape, the four decisions, and Stop."""

    def setUp(self):
        self.events = []
        self.cancel = threading.Event()
        self.questions = Questions(self.events.append, self.cancel)
        self.questions.begin_turn()

    def wire(self, reply=_ANSWER_ONCE):
        pane = ApprovingPane(self.questions, reply)
        self.questions.emit = lambda event: (self.events.append(event), pane(event))[0]
        return pane

    def ask(self, capability=approvals.EDIT, subject="src/Pane.h"):
        return self.questions.ask_approval(capability, subject)

    def test_the_ask_carries_its_own_fields_and_no_questions_array(self):
        pane = self.wire()
        self.assertEqual(self.ask(), "once")
        ask = pane.seen[0]
        self.assertEqual(ask["event"], "question")
        self.assertEqual(ask["kind"], "approval")
        self.assertEqual(ask["capability"], approvals.EDIT)
        self.assertEqual(ask["header"], "Change a file that already exists")
        self.assertIn("Allow the agent to change a file that already exists?", ask["question"])
        self.assertIn("src/Pane.h", ask["question"])
        self.assertEqual(ask["subject"], "src/Pane.h")
        self.assertTrue(ask["id"].startswith("q-"))
        # It belongs to no turn: an approval is drawn wherever the pane is, not where a turn is.
        self.assertNotIn("turn_id", ask)
        self.assertNotIn("questions", ask)
        self.assertEqual(self.questions._pending, {})

    def test_each_of_the_four_decisions_comes_back_unchanged(self):
        for decision in approvals.DECISIONS:
            with self.subTest(decision=decision):
                self.wire({"decision": decision})
                self.assertEqual(self.ask(), decision)

    def test_a_turn_decision_last_exactly_one_turn(self):
        self.wire({"decision": "turn"})
        self.ask()
        self.assertTrue(self.questions.turn_allows(approvals.EDIT))
        self.questions.begin_turn()
        self.assertFalse(self.questions.turn_allows(approvals.EDIT))

    def test_a_reply_that_is_not_one_of_the_four_is_a_deny(self):
        # The safe side of an ask the user never actually answered.
        self.wire({"decision": "yes please"})
        self.assertEqual(self.ask(), "deny")
        self.wire({})
        self.assertEqual(self.ask(), "deny")

    def test_stop_under_an_ask_ends_the_wait_and_closes_it(self):
        # The ask's Stop requirement: Stop during an approval ends the turn like it does a
        # question, and the pane is told so no ask stays up for a turn that no longer exists.
        self.wire(None)
        threading.Timer(0.05, self.cancel.set).start()
        with self.assertRaises(Cancelled):
            self.ask()
        self.assertEqual([e["event"] for e in self.events if e["event"].startswith("question")],
                         ["question", "question_closed"])

    def test_a_turn_ending_under_an_ask_takes_it_with_it(self):
        self.wire(None)
        threading.Timer(0.05, self.questions.end_turn).start()
        with self.assertRaises(Cancelled):
            self.ask()
        self.assertEqual([e["event"] for e in self.events if e["event"].startswith("question")],
                         ["question", "question_closed"])

    def test_an_answer_routes_by_id_to_whichever_questions_holds_it(self):
        # The worker routes a `question_answer` by the pending id (worker.py): a subagent's ask
        # is answered through the same pane, so `handles` is what says whose ask it is.
        other = Questions(lambda event: None, threading.Event())
        self.wire(None)
        outcome = {}
        thread = threading.Thread(target=lambda: outcome.setdefault("decision", self.ask()))
        thread.start()
        while not any(e.get("event") == "question" for e in self.events):
            time.sleep(0.005)
        call_id = self.questions._pending and next(iter(self.questions._pending))
        self.assertTrue(self.questions.handles(call_id))
        self.assertFalse(other.handles(call_id))
        self.questions.resolve({"id": call_id, "decision": "always"})
        thread.join(2.0)
        self.assertFalse(thread.is_alive())
        self.assertEqual(outcome["decision"], "always")
        self.assertFalse(self.questions.handles(call_id))


class ApprovalGateTests(unittest.TestCase):
    """The executor's gate (tools.py `_approval`): the ask goes up at prepare, once per call."""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.events = []
        self.cancel = threading.Event()
        self.executor = ToolExecutor(self.directory.name, self.events.append, self.cancel)
        self.executor.questions.begin_turn()

    def wire(self, reply=_ANSWER_ONCE):
        pane = ApprovingPane(self.executor.questions, reply)
        self.executor.questions.emit = lambda event: (self.events.append(event), pane(event))[0]
        return pane

    def asks(self):
        return [event for event in self.events
                if event.get("event") == "question" and event.get("kind") == "approval"]

    def touch(self, name="notes.txt", text="one\n"):
        path = os.path.join(self.directory.name, name)
        with open(path, "w") as handle:
            handle.write(text)
        return path

    def edit(self, reply=_ANSWER_ONCE, name="notes.txt"):
        self.wire(reply)
        return self.executor.prepare("write_file", {"path": self.touch(name), "content": "two\n"})

    def test_the_ask_goes_up_while_preparing_so_nothing_has_run_yet(self):
        # `Prepared.approved` is a run_command concern (its execution-time second look); for the
        # file tools the ask itself is the whole gate — nothing runs before it is answered.
        self.executor.approvals = asking(approvals.EDIT)
        self.edit()
        ask = self.asks()[0]
        self.assertEqual(ask["capability"], approvals.EDIT)
        self.assertTrue(ask["subject"].endswith("notes.txt"))

    def test_once_allows_this_call_and_not_the_next(self):
        self.executor.approvals = asking(approvals.EDIT)
        self.edit()
        self.edit(name="other.txt")
        self.assertEqual([ask["capability"] for ask in self.asks()],
                         [approvals.EDIT, approvals.EDIT])

    def test_the_rest_of_the_turn_is_not_asked_again(self):
        self.executor.approvals = asking(approvals.EDIT)
        self.edit(reply={"decision": "turn"})
        self.edit(name="other.txt")                    # same turn: no second ask
        self.assertEqual(len(self.asks()), 1)
        self.executor.questions.begin_turn()
        self.edit(name="third.txt")                    # a new turn asks again
        self.assertEqual(len(self.asks()), 2)

    def test_deny_refuses_the_call_with_the_model_readable_refusal(self):
        self.executor.approvals = asking(approvals.EDIT)
        with self.assertRaises(ValueError) as caught:
            self.edit(reply={"decision": "deny"})
        self.assertIn("did not allow", str(caught.exception))
        self.assertIn("another way", str(caught.exception))
        self.assertEqual(len(self.asks()), 1)

    def test_a_read_from_a_folder_options_lists_as_readable_still_asks(self):
        # read_outside is about the folders Options › Security widens reads to (#3KB7): a file in
        # one of those is readable, and the row is what stops to say so.
        with tempfile.TemporaryDirectory() as outside:
            notes = os.path.join(outside, "notes.txt")
            open(notes, "w").close()
            executor = ToolExecutor(self.directory.name, self.events.append, threading.Event(),
                                    policy=security.policy_from({"readable_roots": [outside]}))
            executor.approvals = asking(approvals.READ_OUTSIDE)
            pane = ApprovingPane(executor.questions)
            executor.questions.emit = lambda event: (self.events.append(event), pane(event))[0]
            executor.prepare("read_file", {"path": notes})
            self.assertEqual([ask["capability"] for ask in self.asks()], [approvals.READ_OUTSIDE])

    def test_run_commands_second_look_asks_only_about_what_the_policy_added(self):
        # The checklist may change while an ask sits unanswered, so run_command is checked again
        # at execution; what prepare already drew its ask for is not asked for twice.
        self.wire()
        self.executor.approvals = asking(approvals.DELETE_OR_MOVE)
        command = {"command": "curl https://x -o y && rm y"}
        prepared = self.executor.prepare("run_command", command)
        self.assertEqual(prepared.approved, (approvals.DELETE_OR_MOVE,))
        self.executor.approvals = asking(approvals.DELETE_OR_MOVE, approvals.NETWORK)
        approved = self.executor._approval("run_command", prepared.arguments, subject=command["command"],
                                           already=prepared.approved)
        self.assertEqual([ask["capability"] for ask in self.asks()],
                         [approvals.DELETE_OR_MOVE, approvals.NETWORK])
        self.assertEqual(approved, (approvals.DELETE_OR_MOVE, approvals.NETWORK))

    def test_run_command_is_not_asked_about_twice_for_one_call(self):
        self.wire()
        self.executor.approvals = asking(approvals.DELETE_OR_MOVE)
        existing = self.touch("log.txt")
        prepared = self.executor.prepare("run_command", {"command": f"echo hi > {existing}"})
        self.executor.execute(prepared)                # runs echo; the second look stays quiet
        self.assertEqual(len(self.asks()), 1)

    def test_may_approve_off_draws_no_ask_and_blocks_nothing(self):
        self.executor.approvals = asking(approvals.EDIT)
        self.executor.may_approve = False
        prepared = self.edit()
        self.assertEqual(self.asks(), [])
        self.assertEqual(prepared.approved, ())


class OptionsWiringTests(unittest.TestCase):
    """configure / set_agent_options (protocol 12.1) to the executor's policy."""

    def agent(self, **kwargs):
        root = tempfile.TemporaryDirectory()
        self.addCleanup(root.cleanup)
        return Agent(ProviderConfig("http://127.0.0.1:12345/v1", "mock", ""), root.name,
                     lambda event: None, provider=object(), **kwargs)

    def test_the_two_keys_travel_under_one_key(self):
        out = validate_turn_options({"approvals_ask": ["edit"], "approvals_chosen": False})
        self.assertEqual(out, {"approval_options": {"approvals_ask": ["edit"], "approvals_chosen": False}})

    def test_a_configure_that_says_nothing_about_approvals_allows_everything(self):
        # Allow-all is the bare Agent's state (the tests', the subagents'); the cautious set
        # belongs to the GUI's first-launch default, which always carries approvals_chosen.
        self.assertEqual(self.agent().executor.approvals, approvals.ALLOW_ALL)

    def test_the_first_launch_state_is_the_cautious_set(self):
        agent = self.agent(approval_options={"approvals_ask": [], "approvals_chosen": False})
        policy = agent.executor.approvals
        self.assertFalse(policy.chosen)
        self.assertTrue(policy.asks(approvals.EDIT))       # cautious while unanswered
        self.assertFalse(policy.asks(approvals.CREATE))
        self.assertFalse(policy.asks(approvals.NETWORK))

    def test_set_agent_options_changes_only_the_key_it_carries(self):
        agent = self.agent(approval_options={"approvals_ask": ["edit", "network"],
                                             "approvals_chosen": True})
        agent.set_options({"approvals_ask": []})           # allow everything: the choice stays made
        policy = agent.executor.approvals
        self.assertTrue(policy.chosen)
        self.assertEqual(policy.ask, frozenset())
        agent.set_options({"approvals_ask": ["network"]})  # and back, without re-sending chosen
        self.assertEqual(agent.executor.approvals.ask, frozenset({approvals.NETWORK}))
        self.assertTrue(agent.executor.approvals.chosen)

    def test_bad_values_are_refused_before_anything_changes(self):
        agent = self.agent(approval_options={"approvals_ask": ["edit"], "approvals_chosen": True})
        with self.assertRaises(ValueError):
            agent.set_options({"approvals_ask": ["sudo_everything"]})
        self.assertEqual(agent.executor.approvals.ask, frozenset({approvals.EDIT}))


class SubagentApprovalTests(unittest.TestCase):
    """A subagent cannot ask the user anything, but its actions draw asks in the pane it
    belongs to, named as the subagent's (protocol 27.6)."""

    def test_a_subagent_cannot_ask_but_its_actions_still_draw_asks(self):
        with tempfile.TemporaryDirectory() as root:
            events = []
            sub = RestrictedExecutor(root, events.append, threading.Event(), None, {"write_file"})
            self.assertFalse(sub.can_ask)
            self.assertTrue(sub.may_approve)
            sub.approvals = asking(approvals.EDIT)
            pane = ApprovingPane(sub.questions)
            sub.questions.emit = lambda event: (events.append(event), pane(event))[0]
            path = os.path.join(root, "notes.txt")
            open(path, "w").close()
            sub.prepare("write_file", {"path": path, "content": "two\n"})
            self.assertEqual([ask["capability"] for ask in events if ask.get("kind") == "approval"],
                             [approvals.EDIT])

    def test_an_ask_a_subagent_drew_is_forwarded_named_as_its_own(self):
        events = []
        manager = SubagentManager(events.append)
        sub = Subagent(id="s1", type="general", description="Fix the docs", background=True,
                       model="mock", effort=None)
        manager._on_event(sub, {"event": "question", "kind": "approval", "id": "q-1",
                                "capability": approvals.EDIT})
        forwarded = next(event for event in events if event["event"] == "question")
        self.assertEqual(forwarded["subagent"], "Fix the docs")
        self.assertEqual(forwarded["agent_id"], "s1")
        self.assertEqual(forwarded["capability"], approvals.EDIT)
        manager._on_event(sub, {"event": "question_closed", "id": "q-1", "reason": "cancelled"})
        self.assertTrue(any(event["event"] == "question_closed" and event.get("agent_id") == "s1"
                            for event in events))

    def test_the_answer_routes_back_to_the_subagent_whose_ask_it_is(self):
        events = []
        manager = SubagentManager(events.append)
        questions = Questions(events.append, threading.Event())
        questions._pending["q-1"] = [threading.Event(), None]
        sub = Subagent(id="s1", type="general", description="Fix the docs", background=True,
                       model="mock", effort=None,
                       agent=SimpleNamespace(executor=SimpleNamespace(questions=questions)))
        manager._agents["s1"] = sub
        self.assertTrue(manager.resolve_question({"id": "q-1", "decision": "deny"}))
        self.assertEqual(questions._pending["q-1"][1], {"decision": "deny"})
        self.assertTrue(questions._pending["q-1"][0].is_set())
        # An id nobody holds is the user's click landing late, not an error.
        self.assertFalse(manager.resolve_question({"id": "q-elsewhere", "decision": "deny"}))

    def test_live_subagents_follow_a_checklist_change(self):
        events = []
        manager = SubagentManager(events.append)
        sub = Subagent(id="s1", type="general", description="Fix the docs", background=True,
                       model="mock", effort=None,
                       agent=SimpleNamespace(executor=SimpleNamespace(approvals=approvals.ALLOW_ALL)))
        manager._agents["s1"] = sub
        manager.set_approvals(asking(approvals.NETWORK))
        self.assertEqual(sub.agent.executor.approvals.ask, frozenset({approvals.NETWORK}))

    def spawn(self, root, events, main_agent):
        factory = SubagentFactory(ProviderConfig("http://127.0.0.1:12345/v1", "mock", ""), root,
                                  provider_factory=lambda config: object(), main_agent=main_agent)
        return factory(load_catalog(root, []).get("explore"), None, None, events.append, "a1")[0]

    def test_a_subagent_inherits_the_panes_checklist_when_it_spawns(self):
        with tempfile.TemporaryDirectory() as root:
            agent = self.spawn(root, [],
                               SimpleNamespace(executor=SimpleNamespace(approvals=asking(approvals.EDIT))))
            self.assertEqual(agent.executor.approvals.ask, frozenset({approvals.EDIT}))

    def test_a_pane_agent_that_models_only_its_switches_still_spawns(self):
        # tests/test_failover.py hands the factory a SimpleNamespace of the two failover switches,
        # which the factory reads with getattr; the checklist is read just as forgivingly, and a
        # subagent handed no checklist keeps the bare Agent's allow-all (it draws no asks).
        with tempfile.TemporaryDirectory() as root:
            agent = self.spawn(root, [], SimpleNamespace(failover=True, failover_hosted=False))
            self.assertEqual(agent.executor.approvals, approvals.ALLOW_ALL)


class SourceTestCase(unittest.TestCase):
    """The ask and the first-launch screen are C++ (`src/Pane.h`, `src/ApprovalsPane.h`,
    `src/RelayWindow.h`), so the decisions their review turned on are checked where they are
    written, the way tests/test_questions.py's PaneAskTests reads `src/Pane.h`."""

    @staticmethod
    def block(source: str, start: str, end: str) -> str:
        assert start in source, start
        rest = source.split(start, 1)[1]
        assert end in rest, end
        return rest.split(end, 1)[0]


class PaneApprovalAskTests(SourceTestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "src" / "Pane.h").read_text(encoding="utf-8")

    def test_a_superseded_approval_is_denied_rather_than_answered_empty(self):
        # Protocol 27 has one ask at a time, so a second `question` means the first is stale;
        # an approval nobody answers is a refusal on the worker's side, and an empty answer to
        # an approval would read there as a deny the user never chose — so say that instead.
        show = self.block(self.source, "void showQuestion(const QJsonObject &event) {",
                          "// The worker took the ask away")
        superseded = show.split("if (!superseded.isEmpty() && superseded != incoming) {", 1)[1]
        superseded = superseded.split("        }", 1)[0]
        self.assertIn("if (wasApproval)", superseded)
        self.assertIn('{"decision", QStringLiteral("deny")}', superseded)
        self.assertIn('{"answers", QJsonArray()}', superseded)
        self.assertLess(superseded.index('{"decision"'), superseded.index('{"answers"'))

    def test_an_approval_the_pane_cannot_read_is_a_deny_not_an_empty_answer(self):
        # The approval branch is parsed before the unreadable check, which would otherwise answer
        # the ask away as empty — an empty answer to an approval is a silent deny.
        show = self.block(self.source, "void showQuestion(const QJsonObject &event) {",
                          "// The worker took the ask away")
        self.assertLess(show.index('QStringLiteral("kind")'), show.index("if (unreadableAsk())"))
        unreadable = show.split("if (unreadableAsk()) {", 1)[1]
        self.assertIn("not allowed", unreadable)              # the pane says denied, not "unanswered"
        self.assertLess(unreadable.index("if (approval)"), unreadable.index('{"decision"'))
        self.assertIn('{"decision", QStringLiteral("deny")}', unreadable)

    def test_the_pane_prints_the_four_decisions_in_the_workers_order(self):
        decisions = self.block(self.source, "static QStringList approvalDecisions() {", "void printApproval()")
        indexes = [decisions.index(f'QStringLiteral("{decision}")') for decision in approvals.DECISIONS]
        self.assertEqual(indexes, sorted(indexes))
        ask = self.block(self.source, "void printApproval() {", "int readDecision(")
        for label in ("Allow once", "Allow this turn", "Always allow", "Deny"):
            self.assertIn(label, ask)
        self.assertIn("no skip, no free text", ask)
        self.assertIn("stopTurnHint()", ask)                 # Esc is not this ask's key either

    def test_words_that_are_not_one_of_the_four_do_not_reach_the_worker(self):
        # Anything else would arrive as a deny the user never chose, so the ask is restated.
        answer = self.block(self.source, "bool answerApproval(const QString &text, const QString &author) {",
                            "// Esc while an ask is up")
        refused = answer.split("if (index < 0) {", 1)[1].split("return true;", 1)[0]
        self.assertIn("Answer 1–4", refused)
        self.assertIn("printApproval();", refused)            # the ask goes up again
        self.assertNotIn("recordDecision", refused)           # nothing is decided
        self.assertNotIn("send(", refused)                    # and nothing is sent: the wait goes on
        reader = self.block(self.source, "int readDecision(", "QStringList questionPlaceholders()")
        self.assertIn("number <= decisions.size()", reader)   # 1–4 only
        self.assertIn("return -1;", reader)

    def test_esc_does_not_deny(self):
        # Esc is the one key a person reaches for to make an ask go away, and on this ask it
        # must not decide anything: the ask stays up and the line under it says why.
        skip = self.block(self.source, "bool skipQuestion() {", "// One question answered (or skipped)")
        branch = skip.split("if (m_ask.approval) {", 1)[1].split("return true;", 1)[0]
        self.assertIn("Esc does not deny this one", branch)
        self.assertIn("focusInput()", branch)                 # the ask stays up
        self.assertNotIn("recordAnswer", branch)
        self.assertNotIn("question_answer", branch)

    def test_always_unticks_the_row_and_pushes_the_policy_before_the_decision(self):
        # `always` is the one decision that outlives the turn, so the saved checklist is rewritten
        # and the new policy pushed before the answer lands, on an agent that already allows it.
        record = self.block(self.source, "void recordDecision(int index",
                            '// "Always allow": the capability comes off')
        self.assertLess(record.index("rememberAlwaysAllowed()"), record.index('{"decision", decision}'))
        remember = self.block(self.source, "void rememberAlwaysAllowed() {", "public:")
        self.assertIn("ask.removeAll(m_ask.capability)", remember)
        self.assertIn("security/approvals_chosen", remember)
        self.assertIn("set_agent_options", remember)
        self.assertIn("requestOptions()", remember)

    def test_always_before_the_first_launch_choice_starts_from_the_cautious_set(self):
        # The saved key is empty until the choice is made, and the cautious set is what is in
        # force: reading the key raw would store "ask about nothing" and flip the whole policy
        # to allow-all with one ask (found by the Xvfb drive, 2026-09-19).
        remember = self.block(self.source, "void rememberAlwaysAllowed() {", "public:")
        branch = remember.split("QStringList ask =", 1)[1].split("ask.removeAll", 1)[0]
        self.assertIn('security/approvals_chosen"), false)', branch)   # gated on the choice
        self.assertIn("relay::approvals::cautious()", branch)          # the same list the rows show

    def test_every_configure_carries_both_approval_keys(self):
        # Always both, including empty, so an unticked list reaches the worker as "ask about
        # nothing" rather than "unchanged"; false is the first-launch state.
        options = self.block(self.source, "static QJsonObject requestOptions() {",
                             "// Session-related configure fields")
        self.assertIn('{"approvals_ask"', options)
        self.assertIn('{"approvals_chosen"', options)
        self.assertIn('security/approvals_chosen"), false)', options)

    def test_the_first_launch_screen_comes_back_until_it_is_answered(self):
        # Gated on the setting, not a once-per-process flag: the pane's buttons are what write
        # security/approvals_chosen, and until they do the cautious set stays in force and the
        # screen returns at the next configure.
        hook = self.block(self.source,
                          "// First launch: the approvals choice, one screen before anything else",
                          "void resumeRestoredSession()")
        self.assertIn("security/approvals_chosen", hook)
        self.assertIn("onApprovalsChoice", hook)
        self.assertNotIn("static bool", hook)


class FirstLaunchPaneTests(SourceTestCase):
    @classmethod
    def setUpClass(cls):
        cls.view = (ROOT / "src" / "ApprovalsPane.h").read_text(encoding="utf-8")
        cls.window = (ROOT / "src" / "RelayWindow.h").read_text(encoding="utf-8")

    def test_the_screen_says_what_an_unanswered_relay_does(self):
        for line in ("Relay's agent runs commands and edits files without stopping to ask.",
                     "That is what makes it fast, and it is what we recommend.",
                     "It runs with your user's permissions, inside the pane's workspace.",
                     "Until you choose, the cautious set asks first"):
            self.assertIn(line, self.view)

    def test_it_offers_two_answers_and_no_way_out_of_itself(self):
        self.assertIn("Allow everything", self.view)
        self.assertIn("Choose what needs approval", self.view)
        # Nothing in the view closes it: the choice is the way out (the chrome's × still can).
        self.assertNotIn("onClose", self.view)

    def test_the_buttons_write_the_two_keys(self):
        pane = self.block(self.window, "void openApprovalsPane(Pane *owner) {",
                          "// Either button: the two keys are already written")
        allow = pane.split("view->onAllowEverything", 1)[1].split("};", 1)[0]
        self.assertIn("QStringList{}", allow)                # allow everything, not "unchanged"
        self.assertIn("security/approvals_chosen", allow)
        choose = pane.split("view->onChooseChecklist", 1)[1].split("};", 1)[0]
        self.assertIn("approvalsCautious()", choose)
        self.assertIn("security/approvals_chosen", choose)

    def test_either_button_makes_the_answer_real_everywhere(self):
        answered = self.block(self.window, "void approvalsAnswered(ToolPane *tool, Pane *owner, bool checklist) {",
                              "// ----- the Sharing pane")
        self.assertIn("agentOptionsChanged", answered)        # every pane's worker hears the list
        self.assertIn("refreshSettingsPanes()", answered)     # the Security page redraws its ticks
        self.assertIn("closePane", answered)
        self.assertIn('QStringLiteral("security")', answered)  # the checklist lands in Options

    def test_the_checklist_covers_exactly_the_capabilities(self):
        for capability in approvals.CAPABILITIES:
            self.assertIn(f'approvalRow(QStringLiteral("{capability}")', self.window, capability)

    def test_the_rows_display_the_cautious_set_until_chosen(self):
        # The list itself lives beside the first-launch pane (relay::approvals::cautious) so the
        # ask's "Always allow" shares it; the window's rows are its only other reader.
        shared = self.block(self.view, "inline QStringList cautious() {", "}  // namespace approvals")
        for capability in approvals.CAUTIOUS:
            self.assertIn(f'QStringLiteral("{capability}")', shared, capability)
        delegate = self.block(self.window, "static QStringList approvalsCautious() {",
                              "// One row of the approvals checklist")
        self.assertIn("return relay::approvals::cautious();", delegate)
        row = self.block(self.window, "relay::SettingRow approvalRow(", "static relay::SettingRow headingRow(")
        self.assertIn("approvalsCautious().contains(capability)", row)
        self.assertIn("security/approvals_chosen", row)
        self.assertIn("ask = approvalsCautious()", row)       # the first toggle starts from what showed

    def test_show_it_again_unanswers_the_choice_and_raises_the_screen(self):
        again = self.block(self.window, 'QStringLiteral("Show it again")', "sections << security;")
        self.assertIn('QSettings().remove(QStringLiteral("security/approvals_chosen"))', again)
        self.assertIn("agentOptionsChanged", again)
        self.assertIn("openApprovalsPane", again)


if __name__ == "__main__":
    unittest.main()
