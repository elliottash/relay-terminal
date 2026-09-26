"""Publication handoffs wake their author once and respect Stop."""
import subprocess
import tempfile
from contextlib import contextmanager
from pathlib import Path
from unittest import TestCase
from unittest.mock import Mock

from relay_core.integration_handoffs import AuthorHandoffs
from relay_core.integration_service import IntegrationService
from relay_core.queue import TurnSupervisor


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


class AuthorHandoffsTests(TestCase):
    def setUp(self):
        self.events = []
        self.turns = Mock()
        self.turns.queue_handoff.return_value = True
        self.children = Mock()
        self.children.deliver_workspace_handoff.return_value = None
        self.service = FakeService([
            {"id": 1, "job_id": "j1", "session": "pane", "workspace_id": "main", "text": "landed"},
            {"id": 2, "job_id": "j2", "session": "main:child:a1", "workspace_id": "child", "text": "fix"},
            {"id": 3, "job_id": "j3", "session": "other", "workspace_id": "other", "text": "other"},
        ])
        self.poller = AuthorHandoffs(self.turns, self.children, self.events.append,
                                     service_factory=lambda root: self.service)
        self.poller._identity = {"state": "active", "project_root": "/repo",
                                 "workspace_id": "main", "session": "pane"}

    def test_main_child_and_project_events(self):
        self.children.deliver_workspace_handoff.side_effect = lambda ws, text: True if ws == "child" else None
        self.poller.poll_once()
        self.poller.poll_once()
        self.assertEqual(self.service.acked, [1, 2])
        self.turns.queue_handoff.assert_called_once_with("landed", "land-handoff:1")
        self.assertEqual(sum(e["event"] == "main_moved" for e in self.events), 1)
        self.assertEqual(sum(e["event"] == "queue_status" for e in self.events), 2)

    def test_stop_or_unavailable_child_stays_durable(self):
        self.turns.queue_handoff.return_value = False
        self.children.deliver_workspace_handoff.side_effect = lambda ws, text: False if ws == "child" else None
        self.poller.poll_once()
        self.assertEqual(self.service.acked, [])
        self.assertEqual([e["handoff_id"] for e in self.events
                          if e["event"] == "handoff_pending"], [1, 2])
        self.assertIn("stopped", next(e for e in self.service.events_rows
                                      if e["kind"] == "handoff_pending")["reason"])

    def test_ack_retry_does_not_enqueue_twice(self):
        original = self.service.ack_handoff
        attempts = [0]

        def flaky(handoff_id):
            attempts[0] += 1
            if attempts[0] == 1:
                raise OSError("temporary database failure")
            original(handoff_id)

        self.service.ack_handoff = flaky
        with self.assertRaises(OSError):
            self.poller.poll_once()
        self.poller.poll_once()
        self.turns.queue_handoff.assert_called_once_with("landed", "land-handoff:1")
        self.assertIn(1, self.service.acked)

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
