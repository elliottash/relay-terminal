"""Publication handoffs wake their author, are acknowledged only after its turn ends, and respect Stop."""
import subprocess
import tempfile
from contextlib import contextmanager
from pathlib import Path
from unittest import TestCase
from types import SimpleNamespace
from unittest.mock import Mock

from relay_core.integration_handoffs import AuthorHandoffs
from relay_core.integration_service import IntegrationService
from relay_core.agent import Agent
from relay_core.queue import TurnSupervisor
from relay_core.subagents import Subagent, SubagentManager
from test_queue import CONFIG, GatedProvider, Recorder


class FakeService:
    def __init__(self, rows):
        self.repo_id = "r1"
        self.rows = rows
        self.acked = []
        self.events_rows = [{"id": 1, "kind": "main_moved", "repo_id": "r1",
                             "previous_sha": "a", "sha": "b"}]

    def deliver_handoffs(self):
        return []

    def queue_status(self, *, main):
        return {"repo_id": "r1", "jobs": [], "main_release": {"status": "ready"}}

    def events(self, *, since_id):
        return [row for row in self.events_rows if row["id"] > since_id]

    def handoffs(self, *, pending_only):
        return [row for row in self.rows if row["id"] not in self.acked]

    def ack_handoff(self, handoff_id):
        self.acked.append(handoff_id)

    @contextmanager
    def _tx(self):
        yield None

    def _event(self, conn, kind, payload):
        self.events_rows.append({"id": len(self.events_rows) + 1, "kind": kind, **payload})


class FakeTurns:
    """The supervisor's handoff surface, with turn ends set by the test."""
    def __init__(self, accepts=True):
        self.accepts, self.queued, self.states = accepts, [], {}

    def queue_handoff(self, prompt, request_id):
        if not self.accepts:
            return False
        if self.states.get(request_id) not in ("queued", "running", "done"):
            self.queued.append((prompt, request_id))
            self.states[request_id] = "queued"
        return True

    def handoff_state(self, request_id):
        return self.states.get(request_id)

    def forget_handoff(self, request_id):
        self.states.pop(request_id, None)


class FakeChildren:
    def __init__(self, workspaces=("child",)):
        self.workspaces, self.sent, self.states = set(workspaces), [], {}

    def deliver_workspace_handoff(self, workspace_id, text, key=None):
        if workspace_id not in self.workspaces:
            return None
        if self.states.get(key) not in ("sent", "done"):
            self.sent.append((workspace_id, text, key))
            self.states[key] = "sent"
        return True

    def workspace_handoff_state(self, key):
        return self.states.get(key)

    def forget_workspace_handoff(self, key):
        self.states.pop(key, None)


class AuthorHandoffsTests(TestCase):
    def setUp(self):
        self.events = []
        self.turns = FakeTurns()
        self.children = FakeChildren()
        self.service = FakeService([
            {"id": 1, "job_id": "j1", "session": "pane", "workspace_id": "main", "text": "landed"},
            {"id": 2, "job_id": "j2", "session": "main:child:a1", "workspace_id": "child", "text": "fix"},
            {"id": 3, "job_id": "j3", "session": "other", "workspace_id": "other", "text": "other"},
        ])
        self.poller = self.worker(self.turns, self.children)

    def worker(self, turns, children):
        poller = AuthorHandoffs(turns, children, self.events.append,
                                service_factory=lambda root: self.service)
        poller._identity = {"state": "active", "project_root": "/repo",
                            "workspace_id": "main", "session": "pane"}
        return poller

    def test_ack_waits_for_the_turn_that_read_it(self):
        self.poller.poll_once()
        self.poller.poll_once()
        # Queued in memory is not durable: nothing is acknowledged, nothing is queued twice.
        self.assertEqual(self.service.acked, [])
        self.assertEqual(self.turns.queued, [("landed", "land-handoff:1")])
        self.assertEqual(self.children.sent, [("child", "fix", "land-handoff:2")])
        self.turns.states["land-handoff:1"] = "running"
        self.poller.poll_once()
        self.assertEqual(self.service.acked, [])
        self.turns.states["land-handoff:1"] = "done"
        self.children.states["land-handoff:2"] = "done"
        self.poller.poll_once()
        self.assertEqual(self.service.acked, [1, 2])
        self.assertEqual(self.poller._inflight, {})
        self.assertEqual(self.turns.states, {})
        self.assertEqual(sum(e["event"] == "main_moved" for e in self.events), 1)
        self.assertEqual(sum(e["event"] == "queue_status" for e in self.events), 4)

    def test_worker_crash_before_dispatch_redelivers_after_restart(self):
        self.poller.poll_once()
        self.assertEqual(self.service.acked, [])
        # The worker dies with both wakes still in memory; a new worker starts empty.
        turns, children = FakeTurns(), FakeChildren()
        restarted = self.worker(turns, children)
        restarted.poll_once()
        self.assertEqual(turns.queued, [("landed", "land-handoff:1")])
        self.assertEqual(children.sent, [("child", "fix", "land-handoff:2")])
        turns.states["land-handoff:1"] = "done"
        children.states["land-handoff:2"] = "done"
        restarted.poll_once()
        self.assertEqual(self.service.acked, [1, 2])

    def test_interrupted_turn_or_child_is_woken_again(self):
        self.poller.poll_once()
        self.turns.states["land-handoff:1"] = "cancelled"
        self.children.states["land-handoff:2"] = "failed"
        self.poller.poll_once()
        self.assertEqual(self.service.acked, [])
        self.assertEqual(len(self.turns.queued), 2)
        self.assertEqual(len(self.children.sent), 2)

    def test_stop_or_unavailable_child_stays_durable(self):
        self.turns.accepts = False
        self.children.deliver_workspace_handoff = lambda ws, text, key=None: False if ws == "child" else None
        self.poller.poll_once()
        self.assertEqual(self.service.acked, [])
        self.assertEqual([e["handoff_id"] for e in self.events
                          if e["event"] == "handoff_pending"], [1, 2])
        self.assertIn("stopped", next(e for e in self.service.events_rows
                                      if e["kind"] == "handoff_pending")["reason"])

    def test_transient_ack_failure_retries_the_ack_not_the_wake(self):
        self.children.workspaces = set()
        original = self.service.ack_handoff
        attempts = [0]

        def flaky(handoff_id):
            attempts[0] += 1
            if attempts[0] == 1:
                raise OSError("temporary database failure")
            original(handoff_id)

        self.service.ack_handoff = flaky
        self.poller.poll_once()
        self.turns.states["land-handoff:1"] = "done"
        with self.assertRaises(OSError):
            self.poller.poll_once()
        self.poller.poll_once()
        self.assertEqual(self.turns.queued, [("landed", "land-handoff:1")])
        self.assertEqual(self.service.acked, [1])

    def test_real_supervisor_reports_the_handoff_turn_end(self):
        rec = Recorder()
        turns = TurnSupervisor(rec)
        with tempfile.TemporaryDirectory() as workspace:
            provider = GatedProvider()
            turns.set_agent(Agent(CONFIG, workspace, turns.agent_emit, provider=provider))
            self.assertTrue(turns.queue_handoff("publication", "land-handoff:9"))
            self.assertTrue(turns.queue_handoff("publication", "land-handoff:9"))   # idempotent
            rec.wait(lambda e: e["event"] == "agent_started")
            self.assertEqual(turns.handoff_state("land-handoff:9"), "running")
            provider.release.release()
            rec.wait(lambda e: e["event"] == "agent_finished")
            self.assertEqual(turns.handoff_state("land-handoff:9"), "done")
            self.assertEqual(provider.prompts, ["publication"])
            turns.forget_handoff("land-handoff:9")
            self.assertIsNone(turns.handoff_state("land-handoff:9"))
            turns.shutdown(timeout=3)

    def test_child_handoff_settles_when_its_run_ends(self):
        manager = SubagentManager(lambda event: None)
        sub = Subagent("a1", "general", "child", True, "", None, status="running")
        sub.agent = SimpleNamespace(executor=SimpleNamespace(workspace_identity={"workspace_id": "tree"}))
        manager._agents["a1"] = sub
        self.assertTrue(manager.deliver_workspace_handoff("tree", "fix it", "land-handoff:1"))
        self.assertTrue(manager.deliver_workspace_handoff("tree", "fix it", "land-handoff:1"))
        self.assertEqual(len(sub.inbox), 1)
        self.assertEqual(manager.workspace_handoff_state("land-handoff:1"), "sent")
        # Stopped before the run read it: taken back out, so a redelivery is not doubled.
        manager._settle_handoffs_locked(sub, "stopped")
        self.assertEqual((sub.inbox, manager.workspace_handoff_state("land-handoff:1")), ([], "failed"))
        manager.forget_workspace_handoff("land-handoff:1")
        self.assertTrue(manager.deliver_workspace_handoff("tree", "fix it", "land-handoff:1"))
        sub.inbox.clear()                               # the run drained it at a step boundary
        manager._settle_handoffs_locked(sub, "done")
        self.assertEqual(manager.workspace_handoff_state("land-handoff:1"), "done")

    def test_supervisor_stop_blocks_relay_wake_until_user_resumes(self):
        turns = TurnSupervisor(lambda event: None)
        turns.set_agent(Mock())
        self.assertTrue(turns.accepts_handoff)
        turns.cancel()
        self.assertFalse(turns.accepts_handoff)
        self.assertFalse(turns.queue_handoff("publication", "handoff:1"))
        turns.resume()
        self.assertTrue(turns.accepts_handoff)
        turns.shutdown(timeout=1)

    def test_pending_reason_is_durable_in_service_events(self):
        with tempfile.TemporaryDirectory() as root:
            repo = Path(root, "repo")
            repo.mkdir()
            subprocess.run(["git", "init", "-q", "-b", "main", str(repo)], check=True)
            service = IntegrationService(repo, state_root=Path(root, "state"))
            with service._tx() as conn:
                conn.execute("INSERT INTO handoffs(delivery_key,job_id,kind,session,workspace_id,"
                             "card,text,payload_json,created_at) VALUES (?,?,?,?,?,?,?,?,?)",
                             ("delivery", "job", "failed", "pane", "tree", "AB12", "fix", "{}", "now"))
            row = service.handoffs()[0]
            with service._tx() as conn:
                service._event(conn, "handoff_pending", {"handoff_id": row["id"],
                                                         "reason": "author stopped"})
            reopened = IntegrationService(repo, state_root=Path(root, "state"), register=False)
            self.assertEqual(reopened.events()[-1]["reason"], "author stopped")
            reopened.ack_handoff(row["id"])
            self.assertEqual(reopened.handoffs(), [])
