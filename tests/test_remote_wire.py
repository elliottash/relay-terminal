# SPDX-License-Identifier: AGPL-3.0-or-later
"""The allow-lists, framing and sequencing rules (docs/REMOTE-PROTOCOL.md sections 3, 6 and 7).

The first test is the one that matters: it reads the event names the worker actually emits out of
``backend/``, and fails if any of them has not been classified as forwarded or withheld. That is
what makes "denied by default" a property of the codebase rather than a sentence in a document —
adding an event to the worker breaks this test until somebody decides whether a phone may see it.
"""
import json
import re
import shutil
import subprocess
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

    def test_the_guest_channel_names_are_not_worker_events(self):
        """The five names on the guest event channel (protocol 26.3) are file-channel names read
        inside the GUI process, not events the worker emits. They used to sit in WITHHELD_EVENTS,
        which put them in KNOWN_WORKER_EVENTS — so a worker event that one day happened to be
        called `state`, `hook` or `slash` would have arrived pre-classified and the test above
        would never have caught it (review of 51587e3)."""
        self.assertEqual(sorted(wire.GUEST_CHANNEL_EVENTS), ["bridge", "hook", "slash", "state", "statusline"])
        self.assertEqual(sorted(set(wire.GUEST_CHANNEL_EVENTS) & wire.KNOWN_WORKER_EVENTS), [])
        for name in wire.GUEST_CHANNEL_EVENTS:
            self.assertFalse(wire.may_forward(name), name)   # desktop-local, all five of them

    def test_no_event_is_both_forwarded_and_withheld(self):
        self.assertEqual(sorted(wire.FORWARDED_EVENTS & set(wire.WITHHELD_EVENTS)), [])

    def test_key_material_events_are_withheld(self):
        for name in ("key_stored", "key_removed", "key_tested", "warp_imported", "presets",
                     "model_roles", "configured", "state_loaded", "fork_state"):
            self.assertFalse(wire.may_forward(name), name)
            self.assertIn(name, wire.WITHHELD_EVENTS)

    def test_globals_editor_stays_on_the_desktop(self):
        for name in ("globals_list", "globals_get", "globals_save", "globals_retire"):
            self.assertIn(name, wire.NEVER_FROM_CLIENT)
            self.assertNotIn(name, wire.CLIENT_TYPES)
            self.assertNotIn(name, wire.GUEST_TYPES)
        for name in ("globals_state", "globals_record", "globals_saved", "globals_error"):
            self.assertIn(name, wire.WITHHELD_EVENTS)
            self.assertFalse(wire.may_forward(name))

    def test_an_unknown_event_is_not_forwarded(self):
        self.assertFalse(wire.may_forward("some_new_event_nobody_classified"))

    def test_tool_text_is_not_forwarded_to_a_share_that_carries_the_screen(self):
        """Card #3H5T. The client's transcript renderer is off whenever there is a screen, so the
        text would be parsed and dropped — 1.33 MB of a 1.46 MB tool-heavy turn on the owner's
        Pixel 8. A share with no screen is the agent-companion shape and still gets both."""
        for name in ("tool_output", "tool_result"):
            self.assertTrue(wire.may_forward(name), name)          # a transcript-only share
            self.assertFalse(wire.may_forward_with_screen(name), name)

    def test_the_screen_redundant_list_is_a_subset_of_what_may_be_forwarded(self):
        # Otherwise the screen rule would be the only classification an event ever got, and the
        # allow-list above would stop being the single gate.
        self.assertEqual(sorted(wire.SCREEN_REDUNDANT_EVENTS - wire.FORWARDED_EVENTS), [])

    def test_everything_else_is_forwarded_to_a_screen_share_unchanged(self):
        for name in sorted(wire.FORWARDED_EVENTS - wire.SCREEN_REDUNDANT_EVENTS):
            self.assertTrue(wire.may_forward_with_screen(name), name)
        self.assertFalse(wire.may_forward_with_screen("some_new_event_nobody_classified"))

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
    def test_the_owners_three_answers_are_full_on_the_wire_and_never_a_guests(self):
        """Card #PH0N, the owner's decision 6 (2026-09-20): a `full` device may admit a knock,
        decide a guest's prompt and grant the keyboard, with the same three messages the Sharing
        pane sends the sidecar. They left OWNER_ONLY; the rest of the owner's controls did not."""
        for kind in ("knock_answer", "prompt_answer", "control_answer"):
            self.assertEqual(wire.CLIENT_TYPES.get(kind), wire.FULL, kind)
            self.assertNotIn(kind, wire.OWNER_ONLY, kind)
            self.assertNotIn(kind, wire.NEVER_FROM_CLIENT, kind)
            # A guest deciding a knock, a prompt or the keyboard would be admitting themselves.
            self.assertNotIn(kind, wire.GUEST_TYPES, kind)
            self.assertIn(kind, wire.GUEST_NEVER, kind)
            self.assertFalse(wire.allows(wire.AGENT, wire.CLIENT_TYPES[kind]), kind)
        for kind in ("invite_create", "invite_revoke", "role_set", "participant_remove",
                     "share_pause", "share_end", "control_take", "control_revoke",
                     "share_options", "code_create", "code_revoke"):
            self.assertIn(kind, wire.OWNER_ONLY, kind)
            self.assertNotIn(kind, wire.CLIENT_TYPES, kind)

    def test_owner_asks_goes_to_devices_and_never_to_a_guest(self):
        self.assertIn("owner_asks", wire.SERVER_TYPES)
        self.assertNotIn("owner_asks", wire.GUEST_SERVER_TYPES)
        self.assertFalse(wire.may_send_to_guest("owner_asks"))

    def test_the_switchboard_is_one_full_request_and_one_event_never_a_guests(self):
        """Card #SWPH, section 17. The ten board requests travel inside `board_request`, which is
        the owner's level and nothing less; the answer is `board_event`, which no guest is sent
        and no client may forge. The GUI-side lines have the same two names."""
        self.assertEqual(wire.CLIENT_TYPES.get("board_request"), wire.FULL)
        self.assertFalse(wire.allows(wire.AGENT, wire.CLIENT_TYPES["board_request"]))
        self.assertIn("board_request", wire.GUEST_NEVER)
        self.assertNotIn("board_request", wire.GUEST_TYPES)
        self.assertNotIn("board_request", wire.NEVER_FROM_CLIENT)
        self.assertIn("board_event", wire.SERVER_TYPES)
        self.assertNotIn("board_event", wire.GUEST_SERVER_TYPES)
        self.assertFalse(wire.may_send_to_guest("board_event"))
        self.assertNotIn("board_event", wire.CLIENT_TYPES)
        self.assertIn("board_event", wire.NEVER_FROM_CLIENT)
        self.assertEqual(wire.SIDECAR_TO_GUI_BOARD, {"board_request"})
        self.assertEqual(wire.GUI_TO_SIDECAR_BOARD, {"board_event"})
        # Nothing else about a board is a client type: not the requests themselves, and above all
        # not the ones a device never gets.
        for kind in ("board_open", "board_move", "board_comment", "board_ask", "board_delete",
                     "board_claim", "board_cleanup", "set_board", "board_init", "board_folder"):
            self.assertNotIn(kind, wire.CLIENT_TYPES, kind)

    def test_a_message_must_be_an_object_with_a_type(self):
        for bad in (b"[]", b'"hello"', b"{}", b'{"t": 3}', b"not json"):
            with self.assertRaises(wire.WireError, msg=bad):
                wire.decode(bad)

    def test_oversized_messages_are_refused(self):
        with self.assertRaises(wire.WireError):
            wire.decode(b"x" * (wire.MAX_MESSAGE + 1))


class BinaryFieldTests(unittest.TestCase):
    """The web client sends base64url with no padding; both spellings must decode."""

    def test_both_spellings_decode(self):
        import base64
        payload = bytes(range(256))
        standard = base64.b64encode(payload).decode()
        urlsafe = base64.urlsafe_b64encode(payload).decode().rstrip("=")
        self.assertEqual(wire.decode_bytes(standard, 10000, "x"), payload)
        self.assertEqual(wire.decode_bytes(urlsafe, 10000, "x"), payload)

    def test_unpadded_short_values_decode(self):
        # One keystroke is two base64 characters and no padding — the case that was broken.
        self.assertEqual(wire.decode_bytes("eA", 100, "keys"), b"x")

    def test_rubbish_is_refused(self):
        for bad in (None, 12, "not base64!", "*" * 8):
            with self.assertRaises(wire.WireError, msg=bad):
                wire.decode_bytes(bad, 100, "x")

    def test_oversized_is_refused(self):
        with self.assertRaises(wire.WireError):
            wire.decode_bytes("A" * 200, 100, "x")


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


@unittest.skipUnless(shutil.which("node"), "node is not installed")
class BrowserStreamNameTests(unittest.TestCase):
    """app/rrp.js files each message under the stream name the hub keeps it in (section 7)."""

    def test_the_client_resumes_under_the_hubs_own_stream_names(self):
        messages = [{"t": "screen_snapshot", "pane": "p1", "seq": 3},
                    {"t": "screen_diff", "pane": "p1", "seq": 4},
                    {"t": "agent", "pane": "p2", "seq": 9},
                    {"t": "panes", "seq": 1}]
        done = subprocess.run(
            [shutil.which("node"), str(Path(__file__).resolve().parent / "stream_name_peer.mjs"),
             json.dumps(messages)], capture_output=True, text=True,
            cwd=str(Path(__file__).resolve().parent.parent))
        self.assertEqual(done.returncode, 0, done.stderr)
        # A snapshot and a diff are one stream, the one the hub calls screen:<pane>; before this
        # they were recorded under their message types, which the hub never replays.
        self.assertEqual(json.loads(done.stdout), ["screen:p1", "screen:p1", "agent:p2", "panes"])


@unittest.skipUnless(shutil.which("node"), "node is not installed")
class BrowserPairLinkTests(unittest.TestCase):
    """app/rrp.js parsing the links a phone actually receives."""

    def parse(self, fragments: list[str]) -> list[dict]:
        done = subprocess.run(
            [shutil.which("node"), str(Path(__file__).resolve().parent / "pair_link_peer.mjs"),
             json.dumps(fragments)], capture_output=True, text=True,
            cwd=str(Path(__file__).resolve().parent.parent))
        self.assertEqual(done.returncode, 0, done.stderr)
        return json.loads(done.stdout)

    def test_a_plain_link_parses(self):
        url = pairing.pair_url("https://host", b"\x11" * 32, b"\x22" * 16, "room-1")
        result = self.parse(["#" + url.split("#", 1)[1]])[0]
        self.assertTrue(result["ok"], result.get("error"))
        self.assertEqual(result["room"], "room-1")
        self.assertEqual(result["desktop"], pairing.b64(b"\x11" * 32))
        self.assertEqual(result["secret"], pairing.b64(b"\x22" * 16))

    def test_percent_encoded_separators_still_parse(self):
        """Some QR readers escape the fragment, which used to read as "missing 'd'"."""
        url = pairing.pair_url("https://host", b"\x11" * 32, b"\x22" * 16, "room-1")
        fragment = url.split("#", 1)[1].replace("&", "%26")
        result = self.parse(["#" + fragment])[0]
        self.assertTrue(result["ok"], result.get("error"))
        self.assertEqual(result["room"], "room-1")
        self.assertEqual(result["desktop"], pairing.b64(b"\x11" * 32))

    def test_a_truncated_link_says_what_to_do(self):
        for fragment in ("#v=1", "#v=1&d=" + pairing.b64(b"\x11" * 32), "#", "#v=1&d=short&s=a&r=b"):
            result = self.parse([fragment])[0]
            self.assertFalse(result["ok"], fragment)
            self.assertIn("again", result["error"].lower(), fragment)


@unittest.skipUnless(shutil.which("node"), "node is not installed")
class BrowserInviteLinkTests(unittest.TestCase):
    """app/rrp.js parsing the invite links a guest actually receives (section 10.2).

    The desktop's own parser is ``pairing.parse_invite_url``; these are the browser's, on the same
    links, because the guest client is the only thing that ever reads the fragment of a real one.
    """

    def parse(self, fragments: list[str]) -> list[dict]:
        done = subprocess.run(
            [shutil.which("node"), str(Path(__file__).resolve().parent / "join_link_peer.mjs"),
             json.dumps(fragments)], capture_output=True, text=True,
            cwd=str(Path(__file__).resolve().parent.parent))
        self.assertEqual(done.returncode, 0, done.stderr)
        return json.loads(done.stdout)

    def invite(self, room="room-9"):
        return pairing.invite_url("https://app.example", b"\x33" * 32, b"\x44" * 16, room)

    def test_a_plain_invite_link_parses(self):
        result = self.parse(["#" + self.invite().split("#", 1)[1]])[0]
        self.assertTrue(result["ok"], result.get("error"))
        self.assertEqual(result["room"], "room-9")
        self.assertEqual(result["desktop"], pairing.b64(b"\x33" * 32))
        self.assertEqual(result["secret"], pairing.b64(b"\x44" * 16))
        # The browser agrees with the desktop's own parser, field for field.
        desktop_side = pairing.parse_invite_url(self.invite())
        self.assertEqual(result["desktop"], pairing.b64(desktop_side["desktop_public"]))
        self.assertEqual(result["secret"], pairing.b64(desktop_side["secret"]))

    def test_percent_encoded_separators_still_parse(self):
        """A link pasted through something that escaped the fragment still opens."""
        fragment = self.invite().split("#", 1)[1].replace("&", "%26")
        result = self.parse(["#" + fragment])[0]
        self.assertTrue(result["ok"], result.get("error"))
        self.assertEqual(result["room"], "room-9")
        self.assertEqual(result["secret"], pairing.b64(b"\x44" * 16))

    def test_an_invite_link_is_not_a_pairing_link(self):
        """`i` and `s` are different fields on purpose: neither link parses as the other kind."""
        invite = "#" + self.invite().split("#", 1)[1]
        pair = "#" + pairing.pair_url("https://app.example", b"\x33" * 32, b"\x44" * 16,
                                      "room-9").split("#", 1)[1]
        self.assertFalse(self.parse([invite])[0]["asPairing"])
        result = self.parse([pair])[0]
        self.assertFalse(result["ok"])
        self.assertTrue(result["asPairing"])

    def test_a_damaged_invite_says_what_to_do(self):
        secret = pairing.b64(b"\x44" * 16)
        for fragment in ("#v=1", "#v=1&d=" + pairing.b64(b"\x33" * 32), "#",
                         f"#v=1&d=short&i={secret}&r=room-9",
                         f"#v=1&d={pairing.b64(b'3' * 31)}&i={secret}&r=room-9",
                         f"#v=1&d={pairing.b64(b'\x33' * 32)}&i={pairing.b64(b'x' * 8)}&r=room-9"):
            result = self.parse([fragment])[0]
            self.assertFalse(result["ok"], fragment)
            # Never a stack trace, and always something the reader can act on.
            self.assertIn("invit", result["error"].lower(), fragment)
            self.assertIn("again", result["error"].lower(), fragment)

    def test_a_newer_version_says_so_rather_than_looking_broken(self):
        result = self.parse(["#v=2&d=" + pairing.b64(b"\x33" * 32)
                             + "&i=" + pairing.b64(b"\x44" * 16) + "&r=room-9"])[0]
        self.assertFalse(result["ok"])
        self.assertIn("newer version", result["error"])


class LabelTests(unittest.TestCase):
    def test_device_names_are_stripped(self):
        from remote.identity import clean_label
        self.assertEqual(clean_label("Pixel 9"), "Pixel 9")
        self.assertEqual(clean_label("evil\r\nAllow: yes"), "evilAllow: yes")
        self.assertEqual(clean_label(""), "unnamed device")
        self.assertLessEqual(len(clean_label("x" * 500)), 40)


if __name__ == "__main__":
    unittest.main()
