# SPDX-License-Identifier: GPL-3.0-or-later
"""The allow-lists, framing and sequencing rules (docs/REMOTE-PROTOCOL.md sections 3, 6 and 7).

The first test is the one that matters: it reads the event names the worker actually emits out of
``backend/``, and fails if any of them has not been classified as forwarded or withheld. That is
what makes "denied by default" a property of the codebase rather than a sentence in a document —
adding an event to the worker breaks this test until somebody decides whether a phone may see it.
"""
import re
import unittest
from pathlib import Path

from remote import envelope, pairing, wire

BACKEND = Path(__file__).resolve().parent.parent / "backend"
EVENT_PATTERN = re.compile(r'"event"\s*:\s*"([a-z_]+)"')


def emitted_events() -> set[str]:
    names: set[str] = set()
    for path in sorted(BACKEND.rglob("*.py")):
        if "__pycache__" in path.parts:
            continue
        names.update(EVENT_PATTERN.findall(path.read_text()))
    return names


class AllowListTests(unittest.TestCase):
    def test_every_worker_event_is_classified(self):
        emitted = emitted_events()
        self.assertTrue(emitted, "found no worker events; the scan is broken, not the code")
        unclassified = sorted(emitted - wire.KNOWN_WORKER_EVENTS)
        self.assertEqual(unclassified, [], "classify these in remote/wire.py as forwarded or "
                                           "withheld before a phone can see them")

    def test_no_event_is_both_forwarded_and_withheld(self):
        self.assertEqual(sorted(wire.FORWARDED_EVENTS & set(wire.WITHHELD_EVENTS)), [])

    def test_key_material_events_are_withheld(self):
        for name in ("key_stored", "key_removed", "key_tested", "warp_imported", "presets",
                     "model_roles", "configured", "state_loaded", "fork_state"):
            self.assertFalse(wire.may_forward(name), name)
            self.assertIn(name, wire.WITHHELD_EVENTS)

    def test_an_unknown_event_is_not_forwarded(self):
        self.assertFalse(wire.may_forward("some_new_event_nobody_classified"))

    def test_client_types_never_overlap_the_forbidden_list(self):
        self.assertEqual(sorted(set(wire.CLIENT_TYPES) & wire.NEVER_FROM_CLIENT), [])

    def test_dangerous_types_are_absent_from_the_allow_list(self):
        for name in ("store_key", "configure", "set_model", "import_warp", "load_state", "fork",
                     "rewind", "keybindings", "reset"):
            self.assertNotIn(name, wire.CLIENT_TYPES, name)

    def test_capability_ladder(self):
        self.assertTrue(wire.allows(wire.FULL, wire.AGENT))
        self.assertTrue(wire.allows(wire.AGENT, wire.VIEW))
        self.assertFalse(wire.allows(wire.VIEW, wire.AGENT))
        self.assertFalse(wire.allows(wire.AGENT, wire.FULL))
        self.assertFalse(wire.allows("made up", wire.VIEW))

    def test_input_needs_the_right_capability(self):
        self.assertEqual(wire.CLIENT_TYPES["compose"], wire.AGENT)
        self.assertEqual(wire.CLIENT_TYPES["keys"], wire.FULL)
        self.assertEqual(wire.CLIENT_TYPES["secret_input"], wire.FULL)
        self.assertEqual(wire.CLIENT_TYPES["pane_focus"], wire.VIEW)


class DecodeTests(unittest.TestCase):
    def test_a_message_must_be_an_object_with_a_type(self):
        for bad in (b"[]", b'"hello"', b"{}", b'{"t": 3}', b"not json"):
            with self.assertRaises(wire.WireError, msg=bad):
                wire.decode(bad)

    def test_oversized_messages_are_refused(self):
        with self.assertRaises(wire.WireError):
            wire.decode(b"x" * (wire.MAX_MESSAGE + 1))


class StreamTests(unittest.TestCase):
    def test_sequence_numbers_start_at_one_and_advance(self):
        stream = wire.Stream("panes")
        self.assertEqual(stream.add({"t": "panes"})["seq"], 1)
        self.assertEqual(stream.add({"t": "panes"})["seq"], 2)

    def test_since_returns_only_what_was_missed(self):
        stream = wire.Stream("agent:pane-1")
        for index in range(5):
            stream.add({"t": "agent", "n": index})
        self.assertEqual([m["n"] for m in stream.since(3)], [3, 4])
        self.assertEqual(stream.since(5), [])

    def test_since_is_none_when_the_ring_has_moved_on(self):
        stream = wire.Stream("agent:pane-1", limit=3)
        for index in range(10):
            stream.add({"t": "agent", "n": index})
        self.assertIsNone(stream.since(1), "the client must take a fresh snapshot instead")
        self.assertEqual([m["n"] for m in stream.since(9)], [9])

    def test_a_sequence_from_the_future_is_refused(self):
        stream = wire.Stream("panes")
        stream.add({"t": "panes"})
        self.assertIsNone(stream.since(99))


class DeduplicatorTests(unittest.TestCase):
    def test_an_id_is_applied_once(self):
        dedupe = wire.Deduplicator()
        self.assertTrue(dedupe.fresh("a1"))
        self.assertFalse(dedupe.fresh("a1"))

    def test_messages_without_an_id_are_always_fresh(self):
        dedupe = wire.Deduplicator()
        self.assertTrue(dedupe.fresh(None))
        self.assertTrue(dedupe.fresh(None))

    def test_the_cache_is_bounded(self):
        dedupe = wire.Deduplicator(limit=4)
        for index in range(10):
            self.assertTrue(dedupe.fresh(f"m{index}"))
        self.assertLessEqual(len(dedupe.seen), 4)
        self.assertFalse(dedupe.fresh("m9"))


class EnvelopeTests(unittest.TestCase):
    def test_round_trip(self):
        channel = envelope.new_channel()
        frame = envelope.unpack(envelope.pack(envelope.KIND_DATA, channel, b"ciphertext"))
        self.assertEqual(frame.kind, envelope.KIND_DATA)
        self.assertEqual(frame.channel, channel)
        self.assertEqual(frame.payload, b"ciphertext")

    def test_malformed_envelopes_are_refused(self):
        for bad in (b"", b"\x01short", bytes([0x7F]) + envelope.new_channel()):
            with self.assertRaises(envelope.EnvelopeError, msg=bad):
                envelope.unpack(bad)

    def test_a_channel_id_must_be_sixteen_bytes(self):
        with self.assertRaises(envelope.EnvelopeError):
            envelope.pack(envelope.KIND_DATA, b"short", b"")

    def test_metadata_must_be_a_json_object(self):
        channel = envelope.new_channel()
        frame = envelope.unpack(envelope.pack_json(envelope.KIND_OPEN, channel, {"room": "r1"}))
        self.assertEqual(frame.meta(), {"room": "r1"})
        with self.assertRaises(envelope.EnvelopeError):
            envelope.unpack(envelope.pack(envelope.KIND_OPEN, channel, b"[1,2]")).meta()


class PairingTests(unittest.TestCase):
    def test_the_secret_is_in_the_fragment(self):
        secret = b"\x11" * 16
        url = pairing.pair_url("https://app.example", b"\x22" * 32, secret, "room-1")
        head, _, fragment = url.partition("#")
        self.assertNotIn(pairing.b64(secret), head, "the secret must never leave the fragment")
        self.assertIn("s=", fragment)
        parsed = pairing.parse_pair_url(url)
        self.assertEqual(parsed["secret"], secret)
        self.assertEqual(parsed["room"], "room-1")

    def test_malformed_links_are_refused(self):
        for bad in ("https://app.example/pair",
                    "https://app.example/pair#v=2&d=x&s=y&r=z",
                    "https://app.example/pair#v=1&s=y&r=z",
                    "https://app.example/pair#v=1&d=" + pairing.b64(b"short") + "&s=y&r=z"):
            with self.assertRaises(ValueError, msg=bad):
                pairing.parse_pair_url(bad)

    def test_a_room_is_single_use(self):
        room = pairing.Room(room="r1")
        self.assertTrue(room.check(room.secret))
        self.assertFalse(room.check(room.secret), "a second proof must fail")

    def test_five_wrong_attempts_burn_the_room(self):
        room = pairing.Room(room="r1")
        for _ in range(5):
            self.assertFalse(room.check(b"\x00" * 16))
        self.assertTrue(room.expired)
        self.assertFalse(room.check(room.secret))

    def test_expired_rooms_refuse_everything(self):
        room = pairing.Room(room="r1", ttl=-1)
        self.assertTrue(room.expired)
        self.assertFalse(room.check(room.secret))


class LabelTests(unittest.TestCase):
    def test_device_names_are_stripped(self):
        from remote.identity import clean_label
        self.assertEqual(clean_label("Pixel 9"), "Pixel 9")
        self.assertEqual(clean_label("evil\r\nAllow: yes"), "evilAllow: yes")
        self.assertEqual(clean_label(""), "unnamed device")
        self.assertLessEqual(len(clean_label("x" * 500)), 40)


if __name__ == "__main__":
    unittest.main()
