"""Codex plan notifications feed the same request-linked tasks as relay_board."""
import unittest
from types import SimpleNamespace

from relay_core.guest_harness_codex import CodexHarness, _TurnState
from relay_core.guest_harness_provider import _Turn
from relay_core.requests import RequestLedger
from relay_core.todos import TodoList


class BackgroundPlanTests(unittest.TestCase):
    def test_codex_plan_updates_relay_tasks_without_replacing_other_requests(self):
        emitted = []
        harness = CodexHarness()
        codex_turn = _TurnState(emitted.append)
        harness._handle(codex_turn, {"method": "turn/plan/updated", "params": {
            "plan": [{"step": "Build it", "status": "inProgress"}]}})
        self.assertEqual(emitted[0].kind, "plan_updated")

        requests = RequestLedger()
        earlier = requests.add("Earlier work", "ask")["id"]
        current = requests.add("Build feature", "ask")["id"]
        todos = TodoList()
        todos.replace({"items": [{"text": "Earlier task", "status": "pending",
                                  "request_ids": [earlier]}]}, requests.ids(), "old", [earlier])
        events = []
        agent = SimpleNamespace(requests=requests, todos=todos, emit=events.append,
                                _turn_ctx={"turn_id": "turn-1", "opening": [current],
                                           "requests": [current], "todos_touched": False,
                                           "since_todos": 2})
        relay_turn = object.__new__(_Turn)
        relay_turn.agent = agent
        relay_turn._on_plan_updated(emitted[0].data)
        self.assertEqual([(row["text"], row["status"]) for row in todos.items],
                         [("Earlier task", "pending"), ("Build it", "in_progress")])
        self.assertEqual(todos.items[1]["request_ids"], [current])
        todo_id = todos.items[1]["id"]
        relay_turn._on_plan_updated({"steps": [{"step": "Build it", "status": "completed"}]})
        self.assertEqual(todos.items[1]["id"], todo_id)
        self.assertEqual(todos.items[1]["status"], "completed")
        self.assertEqual(requests.find(current)["status"], "done")
        self.assertEqual(events[-1]["event"], "todos")

        agent._turn_ctx["todos_touched"] = True
        relay_turn._on_plan_updated({"steps": [{"step": "New native step", "status": "pending"}]})
        self.assertEqual(todos.items[1]["text"], "Build it")


if __name__ == "__main__":
    unittest.main()
