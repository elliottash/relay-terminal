# SPDX-License-Identifier: AGPL-3.0-or-later
"""Poll a project's durable publication outbox from an author worker."""
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
        self._seen = set()
        self._event_id = 0

    def configure(self, identity: dict, session: str) -> None:
        self.close()
        self._identity = dict(identity) if identity.get("state") == "active" else {}
        if self._identity:
            self._identity["session"] = session
        self._seen.clear()
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
        # A child submission carries its own session, so fetch all outstanding rows and address
        # only this pane's workspace or a child actually held by this manager.
        for row in service.handoffs(pending_only=True):
            handoff_id = row["id"]
            if handoff_id in self._seen:
                service.ack_handoff(handoff_id)
                self._seen.discard(handoff_id)
                continue
            target = row.get("workspace_id")
            text = row.get("text") or ""
            try:
                if target == workspace_id and row.get("session") == session:
                    if not self.turns.queue_handoff(text, f"land-handoff:{handoff_id}"):
                        reason = "author turn is stopped, paused, or not configured"
                        delivered = False
                    else:
                        delivered, reason = True, ""
                elif target and target != workspace_id:
                    delivered = self.subagents.deliver_workspace_handoff(target, text)
                    if delivered is None:
                        continue
                    reason = "child author is unavailable or stopped" if not delivered else ""
                else:
                    continue
            except (ValueError, AttributeError) as exc:
                delivered, reason = False, str(exc)
            if not delivered:
                self.emit({"event": "handoff_pending", "handoff_id": handoff_id,
                           "job_id": row.get("job_id"), "reason": reason})
                continue
            # Mark this process before the durable acknowledgement so a transient database
            # failure cannot enqueue the same message twice on the next poll.
            self._seen.add(handoff_id)
            service.ack_handoff(handoff_id)
            self._seen.discard(handoff_id)
            self.emit({"event": "handoff_delivered", "handoff_id": handoff_id,
                       "job_id": row.get("job_id"), "workspace_id": target})
