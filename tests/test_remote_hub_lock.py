# SPDX-License-Identifier: AGPL-3.0-or-later
"""Two hubs with one identity must not fight over the rendezvous (#Q5QJ).

On 2026-09-23 a second Relay started from the desktop entry while the owner's own build was
running. Both loaded the profile's identity from the keyring, both registered as the same desktop,
and the rendezvous replaced one socket with the other about 36 times a minute. Every replacement
closed every phone channel, so for twenty minutes no phone could hold a session or finish pairing.

Two things now stop that, and each is tested on its own here:

* the hub lock: a second hub for the same identity on this machine waits, says why, and takes
  over when the first one stops;
* the back-off: a hub whose link is replaced (close code 4409) waits the longest interval instead
  of taking the link straight back, and a link that dies at once does not reset the back-off.
"""
import asyncio
import contextlib
import tempfile
import unittest
from pathlib import Path

from remote import host as host_mod
from remote import identity as identity_mod
from remote import panes as panes_mod
from rendezvous.server import Store, build


def run(coroutine, timeout=30):
    return asyncio.run(asyncio.wait_for(coroutine, timeout))


class Rig:
    """One rendezvous, one identity, and as many hubs for it as a test asks for."""

    async def __aenter__(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.store = Store(":memory:")
        self.server = build(self.store)
        await self.server.start("127.0.0.1", 0)
        self.base = f"http://127.0.0.1:{self.server.port}"
        self.identity = identity_mod.Identity.create(self.directory)
        self.hubs: list[tuple[host_mod.Host, asyncio.Task, list]] = []
        return self

    async def hub(self, *, lock: bool, reconnect=(0.05, 1.0)):
        host = host_mod.Host(self.identity, identity_mod.DeviceStore(self.directory),
                             panes_mod.DemoPaneSource(), app_base="https://app.example",
                             hub_lock=self.directory / "hub.lock" if lock else None)
        host.reconnect_min, host.reconnect_max = reconnect
        host.hub_lock_retry = 0.05
        links: list[tuple[bool, str]] = []
        host.on_link(lambda online, reason: links.append((online, reason)))
        await host.register(self.base)
        task = asyncio.create_task(host.serve())
        self.hubs.append((host, task, links))
        return host, task, links

    def onlines(self) -> int:
        return self.store.db.execute(
            "SELECT COUNT(*) FROM events WHERE kind = 'desktop_online'").fetchone()[0]

    async def stop(self, host, task):
        await host.stop()
        task.cancel()
        with contextlib.suppress(asyncio.CancelledError, Exception):
            await task

    async def __aexit__(self, *exc):
        for host, task, _ in self.hubs:
            await self.stop(host, task)
        await self.server.close()
        await asyncio.sleep(0.1)          # let the last socket handler write its event
        self.store.close()
        self.temporary.cleanup()


async def until(predicate, seconds=5.0):
    for _ in range(int(seconds / 0.02)):
        if predicate():
            return True
        await asyncio.sleep(0.02)
    return predicate()


class HubLockTests(unittest.TestCase):
    def test_a_second_hub_waits_says_why_and_takes_over(self):
        async def scenario():
            async with Rig() as rig:
                first, first_task, _ = await rig.hub(lock=True)
                self.assertTrue(await until(lambda: first.socket is not None))
                second, second_task, links = await rig.hub(lock=True)
                await asyncio.sleep(0.6)
                # The second hub never dialled, so the first one's link was never replaced.
                self.assertIsNone(second.socket)
                self.assertEqual(rig.onlines(), 1)
                self.assertIn((False, host_mod.HUB_LOCKED_REASON), links)
                await rig.stop(first, first_task)
                self.assertTrue(await until(lambda: second.socket is not None))
                self.assertEqual(links[-1], (True, ""))
        run(scenario())

    def test_the_lock_is_the_identitys_and_lives_outside_the_data_directory(self):
        path = host_mod.hub_lock_path("abcdef0123456789ffff")
        self.assertEqual(path.parent, Path("/tmp"))
        self.assertIn("abcdef0123456789", path.name)
        self.assertNotEqual(path, host_mod.hub_lock_path("0000000000000000ffff"))


class ReplacedLinkBackoffTests(unittest.TestCase):
    def test_two_unlocked_hubs_do_not_flap(self):
        """Without the lock, the back-off alone keeps two hubs from trading the link every
        reconnect interval. Before the fix this rig counted a replacement every ~0.05 s."""
        async def scenario():
            async with Rig() as rig:
                _, _, first_links = await rig.hub(lock=False)
                await until(lambda: rig.onlines() >= 1)
                _, _, second_links = await rig.hub(lock=False)
                await asyncio.sleep(1.5)
                # First attach, the second's takeover, and at most a couple of retakes at the
                # 1 s ceiling — not dozens.
                self.assertLessEqual(rig.onlines(), 5)
                said = [reason for _, reason in first_links + second_links]
                self.assertIn(host_mod.REPLACED_REASON, said)
        run(scenario())

    def test_the_reason_for_4409_names_the_other_hub(self):
        from remote import ws
        reason = host_mod.Host._link_reason(ws.ConnectionClosed(4409, "replaced"))
        self.assertEqual(reason, host_mod.REPLACED_REASON)


if __name__ == "__main__":
    unittest.main()
