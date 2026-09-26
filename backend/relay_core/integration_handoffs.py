# SPDX-License-Identifier: AGPL-3.0-or-later
"""Poll a project's durable publication outbox from an author worker.

Delivery is at least once. A handoff row stays pending in the service until the author's turn
that read it has *finished* (a child's: finished and its thread saved) — never on enqueue, since
the turn queue and a child's inbox live in memory. A worker that dies before then leaves the row
pending and the next worker wakes it again; the stable key `land-handoff:<id>` keeps a live
worker from queuing it twice.
"""
from __future__ import annotations

import threading


class AuthorHandoffs:
    def __init__(self, turns, subagents, emit, *, service_factory=None, interval=3.0):
        self.turns, self.subagents, self.emit = turns, subagents, emit
        self.service_factory = service_factory
        self.interval = interval
        self._identity = {}
        self._stop = threading.Event()
        self._thread = None
        #: handoff id -> ("main"|"child", request key): woken, not yet acknowledged. In memory on
        #: purpose: after a worker restart it is empty and the still-pending row wakes again.
        self._inflight = {}
        self._pending_reasons = {}
        self._event_id = 0

    def configure(self, identity: dict, session: str) -> None:
        self.close()
        self._identity = dict(identity) if identity.get("state") == "active" else {}
        if self._identity:
            self._identity["session"] = session
        self._inflight.clear()
        self._pending_reasons.clear()
        self._event_id = 0
        if not self._identity:
            return
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, name="relay-author-handoffs", daemon=True)
        self._thread.start()

    def close(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2)
            self._thread = None

    def _service(self):
        if self.service_factory is not None:
            return self.service_factory(self._identity["project_root"])
        from .integration_service import IntegrationService
        return IntegrationService(self._identity["project_root"], register=False)

    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                self.poll_once()
            except Exception as exc:  # an unavailable publisher must not kill the worker
                self.emit({"event": "integration_poll_error", "reason": str(exc)[:300]})
            self._stop.wait(self.interval)

    def poll_once(self) -> None:
        if not self._identity:
            return
        service = self._service()
        # The publisher normally drains this itself; this also recovers a finished job if it
        # exited after writing its outbox row and before delivering it.
        service.deliver_handoffs()
        status = service.queue_status(main=True)
        self.emit({"event": "queue_status", **status})
        for event in service.events(since_id=self._event_id):
            self._event_id = max(self._event_id, int(event["id"]))
            if event.get("kind") == "main_moved":
                self.emit({"event": "main_moved", **{k: v for k, v in event.items() if k != "kind"}})
        session = self._identity.get("session")
        workspace_id = self._identity["workspace_id"]
        rows = service.handoffs(pending_only=True)
        for handoff_id in set(self._inflight) - {row["id"] for row in rows}:
            self._forget(handoff_id)          # acknowledged elsewhere
        # A child submission carries its own session, so fetch all outstanding rows and address
        # only this pane's workspace or a child actually held by this manager.
        for row in rows:
            handoff_id = row["id"]
            key = f"land-handoff:{handoff_id}"
            target = row.get("workspace_id")
            if handoff_id in self._inflight:
                state = self._state(handoff_id)
                if state in ("queued", "running", "sent"):
                    continue                  # delivered once already; its turn has not ended
                if state in ("done", "removed"):
                    # The turn that read it finished (or the person dismissed it from the
                    # strip). Only now is it acknowledged; a failed ACK leaves the entry here
                    # and the next poll retries the ACK alone, never the wake.
                    service.ack_handoff(handoff_id)
                    self._forget(handoff_id)
                    self.emit({"event": "handoff_delivered", "handoff_id": handoff_id,
                               "job_id": row.get("job_id"), "workspace_id": target})
                    continue
                self._forget(handoff_id)      # error, cancelled, gone, failed: wake it again
            text = row.get("text") or ""
            try:
                if target == workspace_id and row.get("session") == session:
                    if self.turns.queue_handoff(text, key):
                        self._inflight[handoff_id] = ("main", key)
                        delivered, reason = True, ""
                    else:
                        delivered, reason = False, "author turn is stopped, paused, or not configured"
                elif target and target != workspace_id:
                    delivered = self.subagents.deliver_workspace_handoff(target, text, key)
                    if delivered is None:
                        continue
                    if delivered:
                        self._inflight[handoff_id] = ("child", key)
                    reason = "child author is unavailable or stopped" if not delivered else ""
                else:
                    continue
            except (ValueError, AttributeError) as exc:
                delivered, reason = False, str(exc)
            if not delivered:
                if self._pending_reasons.get(handoff_id) != reason:
                    # The event log is durable and tied to the same service database as the
                    # unacknowledged handoff. No schema change is needed for older projects.
                    with service._tx() as conn:
                        service._event(conn, "handoff_pending", {"repo_id": service.repo_id,
                                                               "handoff_id": handoff_id,
                                                               "job_id": row.get("job_id"),
                                                               "reason": reason})
                    self._pending_reasons[handoff_id] = reason
                self.emit({"event": "handoff_pending", "handoff_id": handoff_id,
                           "job_id": row.get("job_id"), "reason": reason})
                continue
            self._pending_reasons.pop(handoff_id, None)
            self.emit({"event": "handoff_queued", "handoff_id": handoff_id,
                       "job_id": row.get("job_id"), "workspace_id": target})

    def _state(self, handoff_id) -> str | None:
        where, key = self._inflight[handoff_id]
        if where == "main":
            return self.turns.handoff_state(key)
        return self.subagents.workspace_handoff_state(key)

    def _forget(self, handoff_id) -> None:
        where, key = self._inflight.pop(handoff_id)
        (self.turns.forget_handoff if where == "main" else self.subagents.forget_workspace_handoff)(key)
