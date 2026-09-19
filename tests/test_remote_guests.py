# SPDX-License-Identifier: GPL-3.0-or-later
"""Multiplayer: invites, knocking, and what a guest can and cannot reach (section 10).

Everything runs over the real rendezvous, a real Noise session and the real hub, exactly as
``tests/test_remote_host.py`` does, because the rules being tested are enforcement rules and a stub
of the hub would be testing the stub. Two panes exist on every hub here, and the invite names one:
"a guest cannot see the other pane" is the whole point of pane scoping and it needs a second pane
to be a real question.
"""
import asyncio
import contextlib
import json
import stat
import tempfile
import time
import unittest
from pathlib import Path

from remote import client as client_mod
from remote import guests as guests_mod
from remote import host as host_mod
from remote import identity as identity_mod
from remote import panes as panes_mod
from remote import pairing, wire
from rendezvous.server import Store, build


def run(coroutine, timeout=30):
    return asyncio.run(asyncio.wait_for(coroutine, timeout))


class Harness:
    """A rendezvous, a desktop with two panes, and an owner who answers knocks as told."""

    def __init__(self, admit=True, role=None, capability=wire.AGENT):
        self.admit = admit
        self.role = role                     # None: admit with whatever the invite offers
        self.capability = capability
        self.knocks: list[host_mod.KnockRequest] = []
        self.answer_delay = 0.0

    async def __aenter__(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.store = Store(":memory:")
        self.server = build(self.store)
        await self.server.start("127.0.0.1", 0)
        self.base = f"http://127.0.0.1:{self.server.port}"

        self.identity = identity_mod.Identity.create(self.directory)
        self.devices = identity_mod.DeviceStore(self.directory)
        self.guests = guests_mod.GuestStore(self.directory, devices=self.devices)
        self.source = panes_mod.DemoPaneSource()

        async def approver(request):
            return True, self.capability

        async def knock_approver(request):
            self.knocks.append(request)
            if self.answer_delay:
                await asyncio.sleep(self.answer_delay)
            return self.admit, self.role or request.role

        self.host = host_mod.Host(self.identity, self.devices, self.source,
                                  app_base="https://app.example", approver=approver,
                                  knock_approver=knock_approver, guests=self.guests,
                                  name="test desktop")
        await self.host.register(self.base)
        self.serving = asyncio.create_task(self.host.serve())
        for _ in range(100):
            await asyncio.sleep(0.02)
            if self.host.socket is not None:
                break
        return self

    async def __aexit__(self, *exc):
        await self.host.stop()
        self.serving.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await self.serving
        await self.server.close()
        self.store.close()
        self.temporary.cleanup()

    async def invite(self, panes=("pane-1",), role=wire.VIEWER, **kwargs):
        return await self.host.invite_create(list(panes), role, **kwargs)

    async def guest(self, url, name="alice", platform="Chrome"):
        client = client_mod.Client(self.base)
        joined = await client.knock(url, name=name, platform=platform)
        return client, joined

    async def paired_owner_device(self, capability=wire.FULL):
        self.capability = capability
        url, _ = await self.host.open_pairing()
        pairing_client = client_mod.Client(self.base)
        record = await pairing_client.pair(url, name="Pixel 9", platform="Chrome")
        await pairing_client.close()
        client = client_mod.Client(self.base)
        await client.connect(record)
        await client.expect("panes")
        return client, record

    def audit_lines(self):
        return [json.loads(line) for path in sorted(self.directory.glob("audit-*.jsonl"))
                for line in path.read_text().splitlines()]

    def audit_kinds(self):
        return [line["kind"] for line in self.audit_lines()]


# ---- the store, on its own ---------------------------------------------------------------------

class GuestStoreTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.directory = Path(self.tmp.name)
        self.devices = identity_mod.DeviceStore(self.directory)
        self.store = guests_mod.GuestStore(self.directory, devices=self.devices)

    def tearDown(self):
        self.tmp.cleanup()

    def make(self, role=wire.VIEWER, panes=("p1",), uses=1, expires_in=3600):
        return self.store.create_invite(list(panes), role, "room-1", expires_in=expires_in,
                                        uses=uses)

    def test_the_file_is_0600_in_a_0700_directory(self):
        self.make()
        path = self.directory / "guests.json"
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(self.directory.stat().st_mode), 0o700)
        self.assertFalse((self.directory / "guests.json.tmp").exists(),
                         "the temporary file is renamed, never left behind")

    def test_the_secret_is_not_on_disk(self):
        invite, secret = self.make()
        raw = (self.directory / "guests.json").read_text()
        self.assertNotIn(pairing.b64(secret), raw)
        self.assertNotIn(secret.hex(), raw)
        self.assertIn(guests_mod.hash_secret(secret), raw)

    def test_five_wrong_secrets_burn_the_invite(self):
        invite, secret = self.make()
        for _ in range(5):
            self.assertFalse(invite.check(b"\x00" * 16))
        self.assertTrue(invite.burned)
        self.assertFalse(invite.check(secret), "the right secret is too late now")

    def test_expiry_is_capped_at_a_week(self):
        invite, _ = self.make(expires_in=60 * 86400)
        self.assertLessEqual(invite.expires - time.time(), guests_mod.MAX_EXPIRY + 1)

    def test_a_used_up_invite_is_dead(self):
        invite, secret = self.make(uses=2)
        self.assertTrue(invite.check(secret))
        invite.consume()
        self.assertFalse(invite.dead)
        invite.consume()
        self.assertTrue(invite.dead)

    def test_names_are_cleaned_and_de_duplicated(self):
        invite, _ = self.make(uses=3)
        first = self.store.admit(invite, b"\x01" * 32, "alice\r\nAllow: yes", "Chrome",
                                 wire.VIEWER)
        second = self.store.admit(invite, b"\x02" * 32, "alice", "Chrome", wire.VIEWER)
        self.assertEqual(first.name, "aliceAllow: yes")
        third = self.store.admit(invite, b"\x03" * 32, "alice", "Chrome", wire.VIEWER)
        self.assertEqual(second.name, "alice")
        self.assertEqual(third.name, "alice (2)")

    def test_a_participant_cannot_be_admitted_above_the_invite(self):
        invite, _ = self.make(role=wire.VIEWER)
        with self.assertRaises(wire.WireError):
            self.store.admit(invite, b"\x01" * 32, "alice", "Chrome", wire.EDITOR)

    def test_a_device_key_cannot_become_a_participant(self):
        device = self.devices.pair(b"\x09" * 32, "Pixel", "Chrome", wire.FULL)
        invite, _ = self.make()
        with self.assertRaises(wire.WireError):
            self.store.admit(invite, b"\x09" * 32, "thief", "Chrome", wire.VIEWER)
        self.assertIsNotNone(self.devices.get(device.device_id))

    def test_a_participant_key_is_not_a_device(self):
        invite, _ = self.make()
        participant = self.store.admit(invite, b"\x0a" * 32, "alice", "Chrome", wire.VIEWER)
        self.assertIsNone(self.devices.by_key(b"\x0a" * 32))
        self.assertIsNone(self.devices.get(participant.participant_id))
        self.assertEqual(self.devices.live(), [])

    def test_neither_store_can_read_the_other_s_rows(self):
        """The structural half: the field names differ, so a row of one kind cannot be loaded as
        the other. A device row handed to the guest store is dropped, and the reverse."""
        invite, _ = self.make()
        self.store.admit(invite, b"\x0b" * 32, "alice", "Chrome", wire.VIEWER)
        self.devices.pair(b"\x0c" * 32, "Pixel", "Chrome", wire.FULL)
        guest_rows = json.loads((self.directory / "guests.json").read_text())
        device_rows = json.loads((self.directory / "devices.json").read_text())

        # A guests.json full of device rows loads nothing.
        (self.directory / "guests.json").write_text(json.dumps(
            {"invites": [], "participants": device_rows["devices"]}))
        reloaded = guests_mod.GuestStore(self.directory, devices=self.devices)
        self.assertEqual(reloaded.participants, {})
        self.assertIsNone(reloaded.by_key(b"\x0c" * 32))

        # And a devices.json full of participant rows does the same.
        (self.directory / "devices.json").write_text(json.dumps(
            {"devices": guest_rows["participants"]}))
        devices = identity_mod.DeviceStore(self.directory)
        self.assertEqual(devices.devices, {})
        self.assertIsNone(devices.by_key(b"\x0b" * 32))

    def test_removing_burns_the_invite_they_came_in_on(self):
        invite, _ = self.make(uses=5)
        participant = self.store.admit(invite, b"\x0d" * 32, "alice", "Chrome", wire.VIEWER)
        self.store.remove(participant.participant_id)
        self.assertTrue(invite.burned)
        self.assertIsNone(self.store.participant(participant.participant_id))
        self.assertIsNone(self.store.by_key(b"\x0d" * 32))

    def test_ending_a_share_clears_the_pane_but_keeps_the_others(self):
        both, _ = self.store.create_invite(["p1", "p2"], wire.VIEWER, "room-2", uses=2)
        participant = self.store.admit(both, b"\x0e" * 32, "alice", "Chrome", wire.VIEWER)
        self.store.end_share("p1")
        again = self.store.participant(participant.participant_id)
        self.assertIsNotNone(again)
        self.assertEqual(again.panes, ["p2"])
        self.store.end_share("p2")
        self.assertIsNone(self.store.participant(participant.participant_id))


class UrlTests(unittest.TestCase):
    def test_the_secret_is_in_the_fragment_under_i(self):
        secret = b"\x33" * 16
        url = pairing.invite_url("https://app.example", b"\x44" * 32, secret, "room-9")
        head, _, fragment = url.partition("#")
        self.assertTrue(head.endswith("/join"))
        self.assertNotIn(pairing.b64(secret), head)
        self.assertIn("i=", fragment)
        parsed = pairing.parse_invite_url(url)
        self.assertEqual(parsed["secret"], secret)
        self.assertEqual(parsed["room"], "room-9")
        self.assertEqual(parsed["desktop_public"], b"\x44" * 32)

    def test_percent_encoded_fragments_still_parse(self):
        url = pairing.invite_url("https://app.example", b"\x44" * 32, b"\x33" * 16, "room-9")
        head, _, fragment = url.partition("#")
        mangled = head + "#" + fragment.replace("&", "%26")
        self.assertEqual(pairing.parse_invite_url(mangled)["room"], "room-9")

    def test_a_pairing_link_is_not_an_invite_link(self):
        pair = pairing.pair_url("https://app.example", b"\x44" * 32, b"\x33" * 16, "r")
        with self.assertRaises(ValueError):
            pairing.parse_invite_url(pair)
        invite = pairing.invite_url("https://app.example", b"\x44" * 32, b"\x33" * 16, "r")
        with self.assertRaises(ValueError):
            pairing.parse_pair_url(invite)


class AllowListTests(unittest.TestCase):
    def test_guest_events_are_a_strict_subset_of_forwarded_events(self):
        self.assertTrue(wire.GUEST_EVENTS < wire.FORWARDED_EVENTS,
                        "a guest may never see an event a device may not")
        self.assertTrue(wire.GUEST_EVENTS)

    def test_the_events_that_carry_tool_output_are_withheld_from_guests(self):
        for name in ("turn_summary", "tool_started", "tool_result", "tool_output",
                     "turn_transcript", "delta", "thinking_delta"):
            self.assertFalse(wire.may_forward_to_guest(name), name)

    def test_the_never_list_is_absent_from_the_guest_allow_list(self):
        self.assertEqual(sorted(set(wire.GUEST_NEVER) & set(wire.GUEST_TYPES)), [])

    def test_guest_types_are_a_subset_of_client_types(self):
        self.assertEqual(sorted(set(wire.GUEST_TYPES) - set(wire.CLIENT_TYPES)), [])

    def test_the_owner_s_controls_are_never_accepted_from_the_wire(self):
        for name in ("invite_create", "invite_revoke", "role_set", "participant_remove",
                     "share_end"):
            self.assertIn(name, wire.NEVER_FROM_CLIENT, name)
            self.assertNotIn(name, wire.CLIENT_TYPES, name)
            self.assertNotIn(name, wire.GUEST_TYPES, name)

    def test_the_role_ladder(self):
        self.assertTrue(wire.role_allows(wire.EDITOR, wire.VIEWER))
        self.assertTrue(wire.role_allows(wire.VIEWER, wire.VIEWER))
        self.assertFalse(wire.role_allows(wire.VIEWER, wire.EDITOR))
        self.assertFalse(wire.role_allows(wire.OWNER, wire.VIEWER),
                         "`owner` is not a role an invite grants, so it is not on this ladder")
        self.assertFalse(wire.role_allows("made up", wire.VIEWER))


# ---- knocking, over the wire --------------------------------------------------------------------

class KnockTests(unittest.TestCase):
    def test_the_happy_path(self):
        async def main():
            async with Harness() as harness:
                invite, url = await harness.invite(role=wire.EDITOR)
                client, joined = await harness.guest(url)
                self.assertEqual(joined.role, wire.EDITOR)
                self.assertEqual(joined.panes, ["pane-1"])
                # The same five digits on both screens, from the handshake hash.
                self.assertEqual(client.auth_code, harness.knocks[0].code)
                self.assertEqual(harness.knocks[0].name, "alice")
                panes = await client.expect("panes")
                self.assertEqual([item["id"] for item in panes["items"]], ["pane-1"])
                presence = await client.expect("participants")
                self.assertEqual(presence["pane"], "pane-1")
                self.assertEqual([item["name"] for item in presence["items"]], ["alice"])
                self.assertTrue(presence["items"][0]["you"])
                self.assertFalse(presence["items"][0]["driving"])
                # Admission spent the one use, and nothing became a device.
                self.assertEqual(harness.guests.invite(invite.invite_id).uses_left, 0)
                self.assertEqual(harness.devices.live(), [])
                self.assertEqual(len(harness.guests.live()), 1)
                await client.close()
        run(main())

    def test_a_knock_alone_consumes_nothing(self):
        async def main():
            async with Harness(admit=False) as harness:
                invite, url = await harness.invite()
                client = client_mod.Client(harness.base)
                with self.assertRaises(wire.WireError) as caught:
                    await client.knock(url, name="alice", platform="Chrome")
                self.assertEqual(caught.exception.code, "not_admitted")
                await client.close()
                self.assertEqual(harness.guests.invite(invite.invite_id).uses_left, 1)
                self.assertEqual(harness.guests.live(), [])
                self.assertIn("refused", harness.audit_kinds())
        run(main())

    def test_five_wrong_secrets_burn_the_invite(self):
        async def main():
            async with Harness() as harness:
                invite, url = await harness.invite()
                head, _, fragment = url.partition("#")
                wrong = head + "#" + fragment.replace(
                    "i=" + fragment.split("i=")[1].split("&")[0],
                    "i=" + pairing.b64(b"\x00" * 16))
                for _ in range(5):
                    attacker = client_mod.Client(harness.base)
                    with self.assertRaises(wire.WireError):
                        await attacker.knock(wrong, name="thief", platform="curl")
                    await attacker.close()
                self.assertTrue(harness.guests.invite(invite.invite_id).burned)
                honest = client_mod.Client(harness.base)
                with self.assertRaises(wire.WireError):
                    await honest.knock(url, name="alice", platform="Chrome")
                await honest.close()
                self.assertEqual(harness.guests.live(), [])
        run(main())

    def test_an_expired_invite_admits_nobody(self):
        async def main():
            async with Harness() as harness:
                invite, url = await harness.invite()
                invite.expires = time.time() - 1
                client = client_mod.Client(harness.base)
                with self.assertRaises(wire.WireError):
                    await client.knock(url, name="alice", platform="Chrome")
                await client.close()
                self.assertEqual(harness.knocks, [], "the owner was never even asked")
        run(main())

    def test_a_used_up_invite_admits_nobody(self):
        async def main():
            async with Harness() as harness:
                invite, url = await harness.invite()
                first, _ = await harness.guest(url)
                await first.close()
                second = client_mod.Client(harness.base)
                with self.assertRaises(wire.WireError):
                    await second.knock(url, name="mallory", platform="Chrome")
                await second.close()
                self.assertEqual(len(harness.guests.live()), 1)
        run(main())

    def test_two_uses_admit_two_guests(self):
        async def main():
            async with Harness() as harness:
                invite, url = await harness.invite(uses=2)
                first, one = await harness.guest(url, name="alice")
                second, two = await harness.guest(url, name="alice")
                self.assertNotEqual(one.participant, two.participant)
                self.assertEqual(sorted(p.name for p in harness.guests.live()),
                                 ["alice", "alice (2)"])
                self.assertEqual(harness.guests.invite(invite.invite_id).uses_left, 0)
                await first.close()
                await second.close()
        run(main())

    def test_no_answer_in_two_minutes_is_a_refusal(self):
        """The clock is injected, not waited on: KNOCK_TIMEOUT is patched to a blink and the
        owner's answer is made to take longer than it."""
        async def main():
            async with Harness() as harness:
                harness.answer_delay = 1.0
                _, url = await harness.invite()
                original = guests_mod.KNOCK_TIMEOUT
                host_mod.guests_mod.KNOCK_TIMEOUT = 0.05
                try:
                    client = client_mod.Client(harness.base)
                    with self.assertRaises(wire.WireError) as caught:
                        await client.knock(url, name="slowpoke", platform="Chrome")
                    self.assertEqual(caught.exception.code, "not_admitted")
                    await client.close()
                finally:
                    host_mod.guests_mod.KNOCK_TIMEOUT = original
                self.assertEqual(harness.guests.live(), [])
        run(main())

    def test_the_owner_may_admit_below_the_invite_and_never_above_it(self):
        async def main():
            async with Harness(role=wire.VIEWER) as harness:
                _, url = await harness.invite(role=wire.EDITOR)
                client, joined = await harness.guest(url)
                self.assertEqual(joined.role, wire.VIEWER)
                await client.close()
            async with Harness(role=wire.EDITOR) as harness:
                _, url = await harness.invite(role=wire.VIEWER)
                client, joined = await harness.guest(url)
                self.assertEqual(joined.role, wire.VIEWER,
                                 "an answer above the invite is taken as the invite's own role")
                await client.close()
        run(main())

    def test_an_invite_link_cannot_pair_a_device(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite()
                link = pairing.parse_invite_url(url)
                client = client_mod.Client(harness.base)
                with self.assertRaises(wire.WireError):
                    # The same room, the same secret, the pairing message: refused, and no
                    # device record appears.
                    await client.pair(
                        pairing.pair_url("https://app.example", link["desktop_public"],
                                         link["secret"], link["room"]),
                        name="thief", platform="curl")
                await client.close()
                self.assertEqual(harness.devices.live(), [])
        run(main())

    def test_only_three_knocks_may_wait_at_once(self):
        async def main():
            async with Harness(admit=False) as harness:
                harness.answer_delay = 1.5      # the owner is slow, so they pile up
                _, url = await harness.invite(uses=guests_mod.MAX_USES)
                clients = [client_mod.Client(harness.base)
                           for _ in range(guests_mod.MAX_WAITING_KNOCKS + 1)]
                results = await asyncio.gather(
                    *(client.knock(url, name="alice", platform="Chrome") for client in clients),
                    return_exceptions=True)
                for client in clients:
                    await client.close()
                codes = sorted(error.code for error in results
                               if isinstance(error, wire.WireError))
                self.assertEqual(len(codes), guests_mod.MAX_WAITING_KNOCKS + 1)
                self.assertIn("rate_limited", codes,
                              "the fourth waiting knock is turned away, not queued")
                self.assertEqual(len(harness.knocks), guests_mod.MAX_WAITING_KNOCKS,
                                 "the owner was asked three times, not four")
        run(main())

    def test_a_knock_is_rate_limited_per_invite(self):
        async def main():
            async with Harness(admit=False) as harness:
                _, url = await harness.invite(uses=guests_mod.MAX_USES)
                refusals = 0
                for _ in range(guests_mod.KNOCKS_PER_MINUTE + 2):
                    client = client_mod.Client(harness.base)
                    try:
                        await client.knock(url, name="alice", platform="Chrome")
                    except wire.WireError as error:
                        refusals += 1
                        last = error.code
                    await client.close()
                self.assertEqual(refusals, guests_mod.KNOCKS_PER_MINUTE + 2)
                self.assertEqual(last, "rate_limited")
        run(main())


# ---- reconnect ----------------------------------------------------------------------------------

class ReconnectTests(unittest.TestCase):
    def test_an_admitted_guest_reconnects_with_hello(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(role=wire.EDITOR)
                client, joined = await harness.guest(url)
                await client.close()

                again = client_mod.Client(harness.base)
                welcome = await again.rejoin(joined)
                self.assertEqual(welcome["participant"], joined.participant)
                self.assertEqual(welcome["role"], wire.EDITOR)
                self.assertEqual(welcome["panes"], ["pane-1"])
                self.assertNotIn("capability", welcome, "a guest holds no capability")
                self.assertNotIn("password_entry", welcome)
                panes = await again.expect("panes")
                self.assertEqual([item["id"] for item in panes["items"]], ["pane-1"])
                await again.close()
        run(main())

    def test_an_expired_participant_is_told_to_discard_the_record(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite()
                client, joined = await harness.guest(url)
                await client.close()
                record = harness.guests.participants[joined.participant]
                record.expires = time.time() - 1

                again = client_mod.Client(harness.base)
                with self.assertRaises(wire.WireError) as caught:
                    await again.rejoin(joined)
                self.assertEqual(caught.exception.code, "closed")
                await again.close()
        run(main())

    def test_removal_cuts_the_live_session_and_the_next_reconnect_fails(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite()
                client, joined = await harness.guest(url)
                await client.expect("panes")
                await harness.host.participant_remove(joined.participant)
                with self.assertRaises(wire.WireError):
                    await client.expect("panes", timeout=5)
                await client.close()

                again = client_mod.Client(harness.base)
                with self.assertRaises((wire.WireError, client_mod.PinMismatch)):
                    await again.rejoin(joined)
                await again.close()
                self.assertIn("participant_remove", harness.audit_kinds())
        run(main())

    def test_ending_the_share_closes_every_session_on_the_pane(self):
        async def main():
            async with Harness() as harness:
                invite, url = await harness.invite(uses=2)
                first, one = await harness.guest(url, name="alice")
                second, two = await harness.guest(url, name="bob")
                for client in (first, second):
                    await client.expect("panes")
                await harness.host.share_end("pane-1")
                for client in (first, second):
                    with self.assertRaises(wire.WireError):
                        await client.expect("panes", timeout=5)
                    await client.close()
                self.assertEqual(harness.guests.live(), [])
                self.assertTrue(harness.guests.invite(invite.invite_id).burned)
                self.assertIn("share_end", harness.audit_kinds())
        run(main())


# ---- scoping and roles ---------------------------------------------------------------------------

class ScopeTests(unittest.TestCase):
    def test_a_guest_sees_only_the_pane_of_their_invite(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(panes=["pane-1"])
                client, _ = await harness.guest(url)
                panes = await client.expect("panes")
                self.assertEqual([item["id"] for item in panes["items"]], ["pane-1"])

                await client.send({"t": "panes_get"})
                again = await client.expect("panes")
                self.assertEqual([item["id"] for item in again["items"]], ["pane-1"])
                await client.close()
        run(main())

    def test_a_guest_cannot_focus_another_pane(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(panes=["pane-1"])
                client, _ = await harness.guest(url)
                await client.send({"t": "pane_focus", "pane": "pane-2"})
                with self.assertRaises(wire.WireError) as caught:
                    await client.expect("agent", timeout=5)
                self.assertEqual(caught.exception.code, "not_permitted")
                await client.close()
        run(main())

    def test_no_event_for_another_pane_reaches_a_guest(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(panes=["pane-1"])
                client, _ = await harness.guest(url)
                await client.send({"t": "pane_focus", "pane": "pane-1"})
                await asyncio.sleep(0.3)
                # The hub does not know the guest is not on pane-2; the filter does.
                harness.source._emit("pane-2", {"event": "status", "text": "secret"})
                harness.source._emit("pane-1", {"event": "status", "text": "mine"})
                event = await client.expect("agent", timeout=5)
                self.assertEqual(event["pane"], "pane-1")
                self.assertEqual(event["event"]["text"], "mine")
                await client.close()
        run(main())

    def test_a_withheld_event_never_reaches_a_guest(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(panes=["pane-1"])
                client, _ = await harness.guest(url)
                await client.send({"t": "pane_focus", "pane": "pane-1"})
                await asyncio.sleep(0.3)
                # `delta` is forwarded to a device and withheld from a guest; `status` is both.
                harness.source._emit("pane-1", {"event": "delta", "text": "the agent's words"})
                harness.source._emit("pane-1", {"event": "turn_summary", "tools": ["secret"]})
                harness.source._emit("pane-1", {"event": "status", "text": "after"})
                event = await client.expect("agent", timeout=5)
                self.assertEqual(event["event"]["event"], "status")
                await client.close()
        run(main())

    def test_a_replay_is_filtered_the_same_way_the_first_send_was(self):
        """Section 7: replayed messages are re-filtered on the way out. A guest who resumes an
        agent stream for a pane that is not theirs gets the replay dropped, not the ring."""
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(panes=["pane-1"])
                client, _ = await harness.guest(url)
                await client.expect("panes")
                harness.source._emit("pane-2", {"event": "status", "text": "not yours"})
                harness.source._emit("pane-1", {"event": "status", "text": "yours"})
                await asyncio.sleep(0.2)
                await client.send({"t": "resume",
                                   "streams": {"agent:pane-1": 0, "agent:pane-2": 0}})
                seen = []
                while True:
                    message = await asyncio.wait_for(client.inbox.get(), 5)
                    if message["t"] == "resumed":
                        break
                    if message["t"] == "agent":
                        seen.append(message)
                self.assertEqual([(m["pane"], m["event"]["text"]) for m in seen],
                                 [("pane-1", "yours")])
                await client.close()
        run(main())

    def test_a_guest_never_sees_a_password_nonce(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(panes=["pane-1"])
                client, _ = await harness.guest(url)
                await client.expect("panes")
                harness.source.set_password_prompt("pane-1", True, shell_pid=4321,
                                                   foreground_pid=4322)
                panes = await client.expect("panes", timeout=5)
                item = panes["items"][0]
                self.assertEqual(item["status"], "password")
                self.assertNotIn("secret_nonce", item)
                await client.close()
        run(main())


class RoleTests(unittest.TestCase):
    def test_a_viewer_cannot_type_or_prompt(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(role=wire.VIEWER)
                client, _ = await harness.guest(url)
                await client.expect("panes")
                for message in ({"t": "compose", "pane": "pane-1", "text": "hello"},
                                {"t": "control_request", "pane": "pane-1"},
                                {"t": "keys", "pane": "pane-1", "bytes": "eA"},
                                {"t": "line", "pane": "pane-1", "text": "rm -rf /"},
                                {"t": "paste", "pane": "pane-1", "text": "x"},
                                {"t": "plan_execute", "pane": "pane-1", "plan_id": "abc"}):
                    await client.send(message)
                    with self.assertRaises(wire.WireError, msg=message["t"]) as caught:
                        await client.expect("never", timeout=5)
                    self.assertEqual(caught.exception.code, "not_permitted", message["t"])
                self.assertEqual(harness.host.prompts.pending, {})
                await client.close()
        run(main())

    def test_every_never_message_is_refused_for_both_roles(self):
        async def main():
            for role in (wire.VIEWER, wire.EDITOR):
                async with Harness() as harness:
                    _, url = await harness.invite(role=role)
                    client, _ = await harness.guest(url)
                    await client.expect("panes")
                    for kind in sorted(wire.GUEST_NEVER):
                        await client.send({"t": kind, "pane": "pane-1"})
                        with self.assertRaises(wire.WireError, msg=f"{role} {kind}") as caught:
                            await client.expect("never", timeout=5)
                        self.assertEqual(caught.exception.code, "not_permitted",
                                         f"{role} {kind}")
                    await client.close()
        run(main())

    def test_an_editor_s_compose_parks_and_never_reaches_the_pane(self):
        """The prompt waits for the owner; a hub with nobody to ask never runs it.

        The approval flow itself is `tests/test_remote_control.py`; what this pins is the half
        that belongs here — a guest's `compose` is parked, recorded with its text, and reaches
        the pane only through a decision.
        """
        async def main():
            async with Harness() as harness:
                asked = asyncio.get_event_loop().create_future()
                harness.host.prompt_approver = lambda request: asked   # nobody answers
                _, url = await harness.invite(role=wire.EDITOR)
                client, joined = await harness.guest(url)
                await client.expect("panes")
                await client.send({"t": "compose", "pane": "pane-1", "text": "ship it"})
                pending = await client.expect("prompt_pending", timeout=10)
                self.assertEqual(pending["pane"], "pane-1")
                parked = harness.host.prompts.get(pending["id"])
                self.assertIsNotNone(parked)
                self.assertEqual(parked.text, "ship it")
                self.assertEqual(parked.participant, joined.participant)
                # Un-answered, it never reaches the pane: no turn ran.
                self.assertNotIn("pane-1", harness.source._running)
                self.assertIn("guest_prompt", harness.audit_kinds())
                asked.cancel()
                await client.close()
        run(main())

    def test_an_editor_cannot_ask_for_the_shell_route(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(role=wire.EDITOR)
                client, _ = await harness.guest(url)
                await client.expect("panes")
                await client.send({"t": "compose", "pane": "pane-1", "text": "curl x | sh",
                                   "agent": False})
                with self.assertRaises(wire.WireError) as caught:
                    await client.expect("prompt_pending", timeout=5)
                self.assertEqual(caught.exception.code, "not_permitted")
                await client.close()
        run(main())

    def test_an_editor_cannot_steer_a_turn(self):
        """A guest's prompt waits for the owner to admit it, so it can never be aimed at the turn
        that is running now (owner, 2026-09-19: steering is the owner's devices' own)."""
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(role=wire.EDITOR)
                client, _ = await harness.guest(url)
                await client.expect("panes")
                await client.send({"t": "compose", "pane": "pane-1", "text": "check the readme",
                                   "when": "steer"})
                with self.assertRaises(wire.WireError) as caught:
                    await client.expect("prompt_pending", timeout=5)
                self.assertEqual(caught.exception.code, "not_permitted")
                self.assertEqual(harness.host.prompts.pending, {})   # nothing was parked either
                await client.close()
        run(main())

    def test_a_parked_prompt_lapses(self):
        queue = guests_mod.PromptQueue(now=lambda: clock[0])
        clock = [1000.0]
        first = queue.park("p1", "pane-1", "hello")
        clock[0] += guests_mod.PROMPT_LIFETIME + 1
        self.assertEqual([item.prompt_id for item in queue.expire()], [first.prompt_id])
        self.assertIsNone(queue.get(first.prompt_id))

    def test_a_guest_may_have_three_prompts_waiting(self):
        queue = guests_mod.PromptQueue()
        for _ in range(guests_mod.PROMPTS_PER_GUEST):
            queue.park("p1", "pane-1", "x")
        with self.assertRaises(wire.WireError):
            queue.park("p1", "pane-1", "x")
        queue.park("p2", "pane-1", "someone else's")

    def test_an_editor_s_control_request_is_a_request(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(role=wire.EDITOR)
                client, joined = await harness.guest(url)
                await client.expect("panes")
                asked = asyncio.get_event_loop().create_future()
                harness.host.control_approver = lambda request: asked   # nobody answers
                await client.send({"t": "control_request", "pane": "pane-1"})
                pending = await client.expect("control_pending", timeout=10)
                self.assertEqual(pending["pane"], "pane-1")
                self.assertIsNotNone(harness.host.controls.get("pane-1", joined.participant))
                self.assertIsNone(harness.host.control_holder("pane-1"))
                # Removing them takes the request with them, so the owner is never asked about
                # somebody who has already gone.
                await harness.host.participant_remove(joined.participant)
                self.assertIsNone(harness.host.controls.get("pane-1", joined.participant))
                client, joined = await harness.guest(
                    (await harness.invite(role=wire.EDITOR))[1])
                await client.expect("panes")
                await client.send({"t": "control_request", "pane": "pane-1"})
                await client.expect("control_pending", timeout=10)
                # And until the owner says yes nobody drives, so typing is refused.
                await client.send({"t": "keys", "pane": "pane-1", "bytes": "eA"})
                with self.assertRaises(wire.WireError) as caught:
                    await client.expect("never", timeout=5)
                self.assertEqual(caught.exception.code, "not_driving")
                asked.cancel()
                await client.close()
        run(main())

    def test_a_role_change_lands_on_the_next_message(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(role=wire.VIEWER)
                client, joined = await harness.guest(url)
                await client.expect("panes")
                await client.send({"t": "compose", "pane": "pane-1", "text": "one"})
                with self.assertRaises(wire.WireError):
                    await client.expect("prompt_pending", timeout=5)
                await harness.host.role_set(joined.participant, wire.EDITOR)
                await client.send({"t": "compose", "pane": "pane-1", "text": "two"})
                await client.expect("prompt_pending", timeout=10)
                self.assertIn("role_set", harness.audit_kinds())
                await client.close()
        run(main())


class OwnerControlTests(unittest.TestCase):
    def test_the_owner_s_own_phone_cannot_run_them_over_the_wire(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.paired_owner_device(wire.FULL)
                for kind in sorted(wire.OWNER_ONLY):
                    await client.send({"t": kind, "pane": "pane-1", "role": "editor",
                                       "participant": "x", "id": "x"})
                    with self.assertRaises(wire.WireError, msg=kind) as caught:
                        await client.expect("never", timeout=5)
                    self.assertEqual(caught.exception.code, "not_permitted", kind)
                await client.close()
        run(main())

    def test_a_guest_cannot_run_them_either(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(role=wire.EDITOR)
                client, _ = await harness.guest(url)
                await client.expect("panes")
                for kind in sorted(wire.OWNER_ONLY):
                    await client.send({"t": kind, "pane": "pane-1"})
                    with self.assertRaises(wire.WireError, msg=kind) as caught:
                        await client.expect("never", timeout=5)
                    self.assertEqual(caught.exception.code, "not_permitted", kind)
                await client.close()
        run(main())

    def test_an_invite_names_a_pane_that_exists(self):
        async def main():
            async with Harness() as harness:
                with self.assertRaises(wire.WireError):
                    await harness.host.invite_create(["no-such-pane"], wire.VIEWER)
        run(main())

    def test_a_revoked_invite_admits_nobody(self):
        async def main():
            async with Harness() as harness:
                invite, url = await harness.invite()
                self.assertTrue(harness.host.invite_revoke(invite.invite_id))
                client = client_mod.Client(harness.base)
                with self.assertRaises(wire.WireError):
                    await client.knock(url, name="alice", platform="Chrome")
                await client.close()
                self.assertIn("invite_revoke", harness.audit_kinds())
        run(main())

    def test_the_room_lives_as_long_as_the_invite(self):
        async def main():
            async with Harness() as harness:
                invite, _ = await harness.invite(expires_in=7 * 86400)
                row = harness.store.db.execute(
                    "SELECT expires FROM rooms WHERE room = ?", (invite.room,)).fetchone()
                self.assertGreater(row["expires"] - time.time(), 6 * 86400,
                                   "a week-long invite must not point at a five-minute room")
        run(main())


class AuditTests(unittest.TestCase):
    def test_every_step_is_recorded_and_names_the_participant(self):
        async def main():
            async with Harness() as harness:
                invite, url = await harness.invite(role=wire.EDITOR, uses=2)
                client, joined = await harness.guest(url)
                await client.expect("panes")
                await harness.host.role_set(joined.participant, wire.VIEWER)
                await harness.host.participant_remove(joined.participant)
                await client.close()
                await asyncio.sleep(0.3)
                await harness.host.share_end("pane-1")

                kinds = harness.audit_kinds()
                for wanted in ("invite_create", "knock", "admitted", "join", "role_set",
                               "participant_remove", "leave", "share_end"):
                    self.assertIn(wanted, kinds, wanted)
                named = {line["kind"] for line in harness.audit_lines()
                         if line.get("participant") == joined.participant}
                self.assertEqual(named, {"knock", "admitted", "join", "role_set",
                                         "participant_remove", "leave"})
                # The invite lines name the invite, not a participant who did not exist yet.
                created = next(line for line in harness.audit_lines()
                               if line["kind"] == "invite_create")
                self.assertEqual(created["invite"], invite.invite_id)
                self.assertEqual(created["panes"], ["pane-1"])
        run(main())

    def test_a_refusal_is_recorded(self):
        async def main():
            async with Harness(admit=False) as harness:
                _, url = await harness.invite()
                client = client_mod.Client(harness.base)
                with self.assertRaises(wire.WireError):
                    await client.knock(url, name="mallory", platform="curl")
                await client.close()
                kinds = harness.audit_kinds()
                self.assertIn("knock", kinds)
                self.assertIn("refused", kinds)
                self.assertNotIn("admitted", kinds)
        run(main())


# ---- the sidecar lines (section 10.5) -----------------------------------------------------------

class SidecarTests(unittest.TestCase):
    """The owner's controls as the GUI sees them: line JSON in, line JSON out, with a double for
    the GUI's dialog. The hub itself is the real one."""

    def test_the_invite_and_knock_lines_round_trip(self):
        async def main():
            from remote import gui_host
            sidecar = gui_host.Sidecar()
            emitted: list[dict] = []
            sidecar.emit = emitted.append
            async with Harness() as harness:
                sidecar.host = harness.host
                sidecar.devices = harness.devices
                harness.host.knock_approver = sidecar.knock

                await sidecar.handle({"t": "invite_create", "pane": "pane-1", "role": "editor",
                                      "uses": 1})
                invite_line = next(m for m in emitted if m["t"] == "invite")
                self.assertEqual(invite_line["role"], "editor")
                self.assertEqual(invite_line["panes"], ["pane-1"])
                self.assertTrue(invite_line["url"].startswith("https://app.example/join#"))

                # The GUI's side of the dialog: answer the `knock` line as it arrives.
                async def answer_when_asked():
                    for _ in range(200):
                        knock = next((m for m in emitted if m["t"] == "knock"), None)
                        if knock is not None:
                            await sidecar.handle({"t": "knock_answer",
                                                  "participant": knock["participant"],
                                                  "admit": True, "role": "viewer"})
                            return knock
                        await asyncio.sleep(0.02)
                    raise AssertionError("no knock line reached the GUI")

                asked = asyncio.create_task(answer_when_asked())
                client, joined = await harness.guest(invite_line["url"])
                knock = await asked
                self.assertEqual(knock["pane"], "pane-1")
                self.assertEqual(knock["role"], "editor")
                self.assertEqual(knock["code"], client.auth_code)
                self.assertEqual(joined.role, "viewer", "admitted below the invite")
                self.assertEqual(joined.participant, knock["participant"])

                emitted.clear()
                sidecar.report_participants()
                people = next(m for m in emitted if m["t"] == "participants")
                self.assertEqual([p["id"] for p in people["items"]], [joined.participant])
                self.assertEqual([i["id"] for i in people["invites"]], [])

                await sidecar.handle({"t": "participant_remove",
                                      "participant": joined.participant})
                self.assertEqual(harness.guests.live(), [])
                await client.close()
        run(main())


if __name__ == "__main__":
    unittest.main()
