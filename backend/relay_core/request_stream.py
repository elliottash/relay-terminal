# SPDX-License-Identifier: AGPL-3.0-or-later
"""The `requests` event as the entries that changed since the last one (protocol 12.11, card #PPR4).

Section 12.3's `requests` event carries the newest 200 ledger entries, and it is emitted after
*every* ledger change — three times in an ordinary turn. Each entry is about 400 bytes of JSON, so
by the two-hundredth turn of a conversation the worker sends the GUI a quarter of a megabyte per
turn of a list that changed by one entry. Measured on a 250-turn stub conversation
(`docs/qa_evidence/2026-09-20-perf-fixes/growth/`): **96 % of all worker→GUI bytes**, growing from
2 KB at turn 1 to 143 KB at turn 120, and it is the reason a turn cost half as much again at turn
225 as at turn 25 (`docs/qa_evidence/2026-09-20-perf-profile/transcript/FINDINGS.md`, finding 3).

So the GUI may ask, with `requests_delta: true`, for the entries that changed. Nothing is lost: the
GUI keeps the list it has already been sent, an entry it is not sent has not changed, and `removed`
names the entries that left — either because the ledger was replaced or because they fell off the
end of the newest 200. `total`, `open` and `counts` are sent whole every time; they are small and
they are what the chip reads.

The default is off, so a GUI that never sends the option — and a phone watching through one — sees
exactly the event it saw before.

One rule keeps this safe: **a ledger that is not the one already sent goes whole.** A new chat, a
load, a resume or a rewind gives ids back to different prompts, and the GUI decides that it is
looking at another ledger by exactly the test below — an id whose preview changed, or a highest id
that went backwards. Doing it here rather than there means the GUI never has to apply that test to
a partial list, where it would be wrong.
"""


def _number(request_id) -> int:
    """`R7` → 7, anything else → 0."""
    if isinstance(request_id, str) and request_id.startswith("R"):
        try:
            return int(request_id[1:])
        except ValueError:
            return 0
    return 0


class RequestStream:
    """Per connection: what the GUI has already been told about each ledger entry."""

    def __init__(self) -> None:
        self._sent: dict[str, dict] = {}
        self._max_seen = 0

    def reset(self) -> None:
        """Forget it all: the next event goes whole."""
        self._sent.clear()
        self._max_seen = 0

    def _other_ledger(self, items: list) -> bool:
        if not self._sent:
            return True          # nothing has been said yet, so everything is news
        highest = max((_number(item.get("id")) for item in items), default=0)
        if highest < self._max_seen:
            return True
        for item in items:
            seen = self._sent.get(item.get("id"))
            if seen is not None and seen.get("text_preview") != item.get("text_preview"):
                return True
        return False

    def trim(self, event: dict) -> dict:
        """`event` with only the entries that changed, or `event` unchanged when it is not one."""
        if event.get("event") != "requests":
            return event
        items = event.get("items")
        if not isinstance(items, list):
            return event
        # A subagent's own event, forwarded with its id (15.4): it is another ledger entirely, and
        # subagents have none today, so it travels whole rather than through this one's memory.
        if event.get("agent_id") or event.get("subagent"):
            return event
        # The reply to the `requests` command carries the id of the request it answers: the GUI
        # asked for the list, so it gets the list.
        if event.get("id") is not None or self._other_ledger(items):
            self.reset()
            for item in items:
                if isinstance(item.get("id"), str):
                    self._sent[item["id"]] = item
                    self._max_seen = max(self._max_seen, _number(item["id"]))
            return event
        changed = []
        listed = set()
        for item in items:
            request_id = item.get("id")
            if not isinstance(request_id, str):
                changed.append(item)            # nothing to remember it by: it always travels
                continue
            listed.add(request_id)
            self._max_seen = max(self._max_seen, _number(request_id))
            if self._sent.get(request_id) != item:
                changed.append(item)
                self._sent[request_id] = item
        removed = [request_id for request_id in self._sent if request_id not in listed]
        for request_id in removed:
            del self._sent[request_id]
        return {**event, "items": changed, "removed": removed, "delta": True}


def validate(value) -> bool:
    """`requests_delta` from configure / set_agent_options."""
    if type(value) is not bool:
        raise ValueError("requests_delta must be a boolean.")
    return value
