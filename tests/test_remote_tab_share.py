# SPDX-License-Identifier: GPL-3.0-or-later
"""Share whole tab (owner, 2026-09-18): "the partner gets access to all panes in the tab, so you can
add more panes and they immediately get access to the workspace".

An invite made with a `tab` grows with the tab. What is pinned here is that growth and nothing
more: a pane added to the tab reaches the tab's guests at once, and is audited before it does; a
pane that leaves the tab leaves their scope at once, frames and all; an ordinary invite, or a guest
of another tab, gains nothing; and a password prompt on a pane that joined later is refused to a
guest exactly as on the first pane. The hub, sockets and Noise are real (the security suite's
Harness); only the desktop is the demo source.
"""
import asyncio
import os
import tempfile
import unittest
from pathlib import Path

os.environ.setdefault("RELAY_KEYRING", "off")

from remote import guests as guests_mod  # noqa: E402
from remote import wire  # noqa: E402
from tests.test_remote_security import Harness, refusal, run  # noqa: E402

TAB = "tab-a"
OTHER = "tab-b"


def add_pane(harness, pane: str, tab: str) -> None:
    """What the sidecar does with a `pane` line that carries a tab: the scope first, then the
    list (gui_host.Sidecar.handle)."""
    source = harness.source
    if source._pane(pane) is None:
        source._panes.append({"id": pane, "window": 1, "tab": pane, "title": pane, "cwd": "~",
                              "program": "", "control": "human", "status": "idle", "unread": 0,
                              "queue": 0, "updated": 0})
    harness.host.pane_tab(pane, tab)
    source._changed()


def drop_pane(harness, pane: str) -> None:
    harness.host.pane_gone(pane)
    harness.source._panes = [item for item in harness.source._panes if item["id"] != pane]
    harness.source._changed()


async def panes_seen(client, want, timeout=5.0) -> list[str]:
    """The next `panes` list whose ids satisfy `want`, as ids."""
    deadline = asyncio.get_running_loop().time() + timeout
    while True:
        left = deadline - asyncio.get_running_loop().time()
        message = await client.expect("panes", timeout=max(0.1, left))
        ids = sorted(item["id"] for item in message.get("items", []))
        if want(ids):
            return ids


async def quiet(client, kind: str, pane: str, seconds: float = 0.6) -> list[dict]:
    """Every `kind` message for `pane` that arrives in the next moment."""
    seen = []
    while True:
        try:
            message = await client.expect(kind, timeout=seconds)
        except Exception:
            return seen
        if message.get("pane") == pane:
            seen.append(message)


class TabScopeTests(unittest.TestCase):
    def test_a_pane_added_to_the_tab_reaches_its_guests_at_once(self):
        async def main():
            async with Harness() as harness:
                add_pane(harness, "pane-1", TAB)
                _, url = await harness.invite(panes=("pane-1",), role=wire.EDITOR, tab=TAB)
                client, _ = await harness.guest(url)
                add_pane(harness, "pane-3", TAB)
                ids = await panes_seen(client, lambda ids: "pane-3" in ids)
                self.assertEqual(ids, ["pane-1", "pane-3"])
                # And it is theirs to use, not only to see listed.
                await client.send({"t": "screen_get", "pane": "pane-3"})
                snapshot = await client.expect("screen_snapshot")
                self.assertEqual(snapshot["pane"], "pane-3")
                # Audited, one row per participant, before the list went out.
                grown = [row for row in harness.audit_lines() if row["kind"] == "scope_grown"]
                self.assertTrue(any(row.get("pane") == "pane-3" and row.get("participant")
                                    for row in grown), grown)
                await client.close()
        run(main())

    def test_a_pane_that_leaves_the_tab_leaves_the_guest_with_its_frames(self):
        async def main():
            async with Harness() as harness:
                add_pane(harness, "pane-1", TAB)
                add_pane(harness, "pane-3", TAB)
                _, url = await harness.invite(panes=("pane-1",), role=wire.EDITOR, tab=TAB)
                client, joined = await harness.guest(url)
                self.assertIn("pane-3", harness.guests.live()[0].panes,
                              "a tab invite names every shared pane in the tab")
                await client.send({"t": "pane_focus", "pane": "pane-3"})
                await asyncio.sleep(0.2)
                # Moved to another tab that is not shared whole: out of scope at once.
                harness.host.pane_tab("pane-3", "")
                harness.source._changed()
                ids = await panes_seen(client, lambda ids: "pane-3" not in ids)
                self.assertEqual(ids, ["pane-1"])
                harness.host._screen_event("pane-3", {
                    "t": "screen_diff", "pane": "pane-3", "rows": 4, "cols": 20,
                    "lines": [{"row": 0, "segs": [{"text": "after it left"}]}]})
                self.assertEqual(await quiet(client, "screen_diff", "pane-3"), [],
                                 "a frame of a pane that left the tab reached its old guest")
                error = await refusal(client, "screen_get", {"pane": "pane-3"})
                self.assertEqual(error["code"], "not_permitted")
                await client.close()
        run(main())

    def test_closing_the_tabs_last_pane_ends_the_guest(self):
        async def main():
            async with Harness() as harness:
                add_pane(harness, "pane-1", TAB)
                invite, url = await harness.invite(panes=("pane-1",), tab=TAB)
                client, _ = await harness.guest(url)
                drop_pane(harness, "pane-1")
                self.assertEqual(harness.guests.live(), [], "a tab with no panes is a closed tab")
                self.assertTrue(harness.guests.invite(invite.invite_id).burned)
                await client.close()
        run(main())

    def test_an_ordinary_invite_does_not_grow_with_the_tab(self):
        async def main():
            async with Harness() as harness:
                add_pane(harness, "pane-1", TAB)
                _, url = await harness.invite(panes=("pane-1",), role=wire.EDITOR)
                client, _ = await harness.guest(url)
                add_pane(harness, "pane-3", TAB)
                await asyncio.sleep(0.3)
                self.assertEqual(harness.guests.live()[0].panes, ["pane-1"])
                error = await refusal(client, "screen_get", {"pane": "pane-3"})
                self.assertEqual(error["code"], "not_permitted")
                await client.close()
        run(main())

    def test_a_password_prompt_on_a_later_pane_is_refused_as_on_the_first(self):
        async def main():
            async with Harness() as harness:
                add_pane(harness, "pane-1", TAB)
                _, url = await harness.invite(panes=("pane-1",), role=wire.EDITOR, tab=TAB)
                client, _ = await harness.guest(url)
                add_pane(harness, "pane-3", TAB)
                await panes_seen(client, lambda ids: "pane-3" in ids)
                body = {"nonce": "x", "bytes": "eA"}
                first = await refusal(client, "secret_input", {"pane": "pane-1", **body})
                later = await refusal(client, "secret_input", {"pane": "pane-3", **body})
                self.assertEqual(first["code"], later["code"])
                self.assertEqual(harness.source._secrets, [])
                await client.close()
        run(main())


class StoreTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.store = guests_mod.GuestStore(Path(self.temporary.name))

    def admit(self, invite, key=b"k" * 32):
        return self.store.admit(invite, key, "alice", "Chrome", invite.role)

    def test_growth_reaches_only_rows_of_that_tab(self):
        tabbed, _ = self.store.create_invite(["p1"], wire.VIEWER, "room", uses=2, tab=TAB)
        plain, _ = self.store.create_invite(["p1"], wire.VIEWER, "room")
        other, _ = self.store.create_invite(["p9"], wire.VIEWER, "room", tab=OTHER)
        alice = self.admit(tabbed)
        bo = self.admit(plain, b"b" * 32)
        grown = self.store.add_to_tab(TAB, "p2")
        self.assertEqual([p.participant_id for p in grown], [alice.participant_id])
        self.assertEqual(alice.panes, ["p1", "p2"])
        self.assertEqual(tabbed.panes, ["p1", "p2"], "a later join through the link gets it too")
        self.assertEqual(bo.panes, ["p1"])
        self.assertEqual((plain.panes, other.panes), (["p1"], ["p9"]))

    def test_the_tab_survives_a_restart(self):
        invite, _ = self.store.create_invite(["p1"], wire.VIEWER, "room", tab=TAB)
        self.admit(invite)
        again = guests_mod.GuestStore(Path(self.temporary.name))
        self.assertEqual(again.invite(invite.invite_id).tab, TAB)
        self.assertEqual(again.live()[0].tab, TAB)

    def test_growth_stops_at_the_ceiling(self):
        invite, _ = self.store.create_invite(["p0"], wire.VIEWER, "room", tab=TAB)
        alice = self.admit(invite)
        for n in range(1, guests_mod.MAX_PANES_PER_TAB + 5):
            self.store.add_to_tab(TAB, f"p{n}")
        self.assertEqual(len(alice.panes), guests_mod.MAX_PANES_PER_TAB)

    def test_a_removed_guest_gains_nothing(self):
        invite, _ = self.store.create_invite(["p1"], wire.VIEWER, "room", uses=2, tab=TAB)
        alice = self.admit(invite)
        self.store.remove(alice.participant_id)
        self.assertEqual(self.store.add_to_tab(TAB, "p2"), [])
        self.assertEqual(alice.panes, [])


class SidecarOrderTests(unittest.IsolatedAsyncioTestCase):
    """The scope changes before the pane list goes out, or a guest would be sent a list without
    the pane they were just given (gui_host.Sidecar.handle)."""

    async def test_the_scope_grows_before_the_list_is_sent(self):
        from remote import gui_host
        side = gui_host.Sidecar()
        side.emit = lambda message: None
        order = []

        class Host:
            pane_tabs = {}

            def pane_tab(self, pane, tab):
                order.append(("scope", pane, tab))

            def pane_gone(self, pane):
                order.append(("gone", pane))

        side.host = Host()
        side.source.on_panes(lambda: order.append(("list",)))
        await side.handle({"t": "pane", "id": "p3", "title": "t", "tab": TAB})
        await side.handle({"t": "unpane", "id": "p3"})
        self.assertEqual(order, [("scope", "p3", TAB), ("list",), ("gone", "p3"), ("list",)])

    async def test_the_tab_reaches_the_hub(self):
        from remote import gui_host
        side = gui_host.Sidecar()
        side.out = []
        side.emit = side.out.append
        asked = []

        class Invite:
            invite_id, role, uses_left, panes = "i1", "viewer", 1, []

            def seconds_left(self):
                return 60

        class Host:
            async def invite_create(self, panes, role, **kw):
                asked.append((panes, kw.get("tab")))
                return Invite(), "https://example.test/join#i=x"

        side.host = Host()
        side.served_by_cloudflare = False
        await side.invite_create({"pane": "p1", "role": "viewer", "tab": TAB})
        await side.invite_create({"pane": "p9", "role": "viewer"})
        self.assertEqual(asked, [(["p1"], TAB), (["p9"], "")])


if __name__ == "__main__":
    unittest.main()
