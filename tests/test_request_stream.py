"""`requests_delta` (protocol 12.11, card #PPR4): the ledger event as the entries that changed.

The rule the filter has to keep is that a receiver which applies "replace what you are sent, drop
what `removed` names, leave the rest" ends up with exactly the list a full event would have given
it — including after a new chat, a resume or a rewind, which give the same ids to other prompts.
"""
import unittest

from relay_core import request_stream


def entry(request_id, preview="ask", status="open", **extra):
    item = {"id": request_id, "text_preview": preview, "source": "ask", "origin": "user",
            "requires_completion": True, "status": status, "reason": None, "turn_id": None,
            "turn": None, "queue_item": None, "delivered": False, "handled": False,
            "todo_ids": [], "attachments": [], "audit": [], "created": 0, "updated": 0}
    item.update(extra)
    return item


def event(items):
    return {"event": "requests", "items": list(items), "total": len(items), "open": len(items),
            "counts": {"open": len(items)}}


def apply_to(listed, sent):
    """What a receiver holds after `sent`, given it already held `listed`."""
    out = list(listed)
    if not sent.get("delta"):
        return list(sent["items"])
    at = {item["id"]: i for i, item in enumerate(out)}
    for item in sent["items"]:
        if item["id"] in at:
            out[at[item["id"]]] = item
        else:
            at[item["id"]] = len(out)
            out.append(item)
    gone = set(sent.get("removed") or [])
    return [item for item in out if item["id"] not in gone]


class RequestStreamTests(unittest.TestCase):
    def test_first_event_is_whole_and_the_next_carries_only_what_changed(self):
        stream = request_stream.RequestStream()
        first = stream.trim(event([entry("R1"), entry("R2")]))
        self.assertNotIn("delta", first)
        self.assertEqual([i["id"] for i in first["items"]], ["R1", "R2"])

        second = stream.trim(event([entry("R1"), entry("R2", status="done"), entry("R3")]))
        self.assertTrue(second["delta"])
        self.assertEqual([i["id"] for i in second["items"]], ["R2", "R3"])
        self.assertEqual(second["removed"], [])
        self.assertEqual(second["total"], 3)
        self.assertEqual(second["counts"], {"open": 3})

    def test_nothing_changed_sends_no_entries(self):
        stream = request_stream.RequestStream()
        items = [entry("R1"), entry("R2")]
        stream.trim(event(items))
        again = stream.trim(event(items))
        self.assertEqual(again["items"], [])
        self.assertEqual(again["removed"], [])

    def test_an_entry_that_falls_off_the_end_is_removed(self):
        stream = request_stream.RequestStream()
        stream.trim(event([entry("R1"), entry("R2"), entry("R3")]))
        trimmed = stream.trim(event([entry("R2"), entry("R3"), entry("R4")]))
        self.assertEqual(trimmed["removed"], ["R1"])
        self.assertEqual([i["id"] for i in trimmed["items"]], ["R4"])

    def test_another_ledger_goes_whole(self):
        stream = request_stream.RequestStream()
        stream.trim(event([entry("R1", "fix the parser"), entry("R2", "write tests")]))
        # A new chat: ids start again and R1 is another prompt.
        fresh = stream.trim(event([entry("R1", "something else")]))
        self.assertNotIn("delta", fresh)
        self.assertEqual([i["id"] for i in fresh["items"]], ["R1"])
        # A rewind: the ids that are left are the ones that were there, and the highest went back.
        stream.trim(event([entry("R1", "something else"), entry("R2", "b"), entry("R3", "c")]))
        back = stream.trim(event([entry("R1", "something else"), entry("R2", "b")]))
        self.assertNotIn("delta", back)
        self.assertEqual([i["id"] for i in back["items"]], ["R1", "R2"])

    def test_the_reply_to_the_requests_command_goes_whole(self):
        stream = request_stream.RequestStream()
        stream.trim(event([entry("R1"), entry("R2")]))
        asked = stream.trim({**event([entry("R1"), entry("R2")]), "id": "req-7"})
        self.assertNotIn("delta", asked)
        self.assertEqual(len(asked["items"]), 2)

    def test_a_receiver_that_merges_ends_up_with_the_whole_list(self):
        stream = request_stream.RequestStream()
        held = []
        # A conversation's worth of ledgers: one entry added a turn, statuses moving, the oldest
        # falling off a ten-entry window, then a new chat.
        lists = []
        for turn in range(1, 30):
            items = [entry(f"R{n}", f"p{n}", status="done") for n in range(1, turn)]
            items.append(entry(f"R{turn}", f"p{turn}", status="in_progress"))
            lists.append(items[-10:])
        lists.append([entry("R1", "a new chat")])
        for items in lists:
            held = apply_to(held, stream.trim(event(items)))
            self.assertEqual(held, items)

    def test_other_events_pass_through(self):
        stream = request_stream.RequestStream()
        todos = {"event": "todos", "items": [{"id": "T1"}]}
        self.assertIs(stream.trim(todos), todos)
        delta = {"event": "delta", "text": "hi"}
        self.assertIs(stream.trim(delta), delta)

    def test_validate(self):
        self.assertIs(request_stream.validate(True), True)
        with self.assertRaises(ValueError):
            request_stream.validate("yes")


if __name__ == "__main__":
    unittest.main()
