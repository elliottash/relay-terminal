# SPDX-License-Identifier: GPL-3.0-or-later
"""The public link: a cloudflared quick tunnel (remote/cloudflare.py).

Nothing here opens a real tunnel. A shell script called `cloudflared` goes on PATH and prints what
a test wants it to print, so the cases that matter are reachable: it is not installed, it prints
the URL, it prints nothing before the deadline, it dies on start, and it has to be killed. The
things worth pinning are the ones a public address makes expensive to get wrong — only a real
`https://<name>.trycloudflare.com` is ever taken for a tunnel, the URL never reaches a log, one
tunnel exists at a time, and `unpublish()` kills the child it started and nobody else's.
"""
import asyncio
import os
import subprocess
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

from remote import cloudflare


class Fake:
    """A `cloudflared` on PATH that prints a script and then waits to be killed."""

    def __init__(self, directory: Path, *, lines: str, linger: bool = True, exit_code: int = 0):
        self.directory = directory
        self.log = directory / "calls.log"
        (directory / "say.txt").write_text(lines)
        binary = directory / "cloudflared"
        wait = 'while true; do sleep 0.2; done' if linger else f"exit {exit_code}"
        binary.write_text(f"""#!/bin/sh
printf '%s\\n' "$*" >> {self.log}
cat {directory}/say.txt
{wait}
""")
        binary.chmod(0o755)

    def calls(self) -> list[str]:
        return self.log.read_text().splitlines() if self.log.exists() else []


class TunnelTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.path = mock.patch.dict(os.environ, {"PATH": f"{self.directory}{os.pathsep}{os.environ['PATH']}"})
        self.path.start()
        self.addCleanup(self.path.stop)
        self.addCleanup(self.temp.cleanup)
        self.addCleanup(cloudflare.unpublish)

    # ---- what is offered -----------------------------------------------------------------------
    def test_without_cloudflared_there_is_a_sentence_not_a_silence(self):
        with mock.patch.dict(os.environ, {"PATH": "/nonexistent"}):
            found = cloudflare.probe()
            self.assertFalse(found.ready)
            self.assertIn("cloudflared is not installed", found.reason)
            url, said = cloudflare.publish(9999)
            self.assertIsNone(url)
            self.assertIn("cloudflared is not installed", said)

    def test_installed_means_offered_with_no_reason_to_show(self):
        Fake(self.directory, lines="")
        found = cloudflare.probe()
        self.assertEqual(found.reason, "")
        self.assertFalse(found.ready, "probing starts nothing; only publish() opens a tunnel")

    # ---- opening one ---------------------------------------------------------------------------
    def test_the_url_is_read_from_what_cloudflared_prints(self):
        fake = Fake(self.directory, lines=(
            "2026-09-18T20:00:00Z INF Requesting new quick Tunnel on trycloudflare.com...\n"
            "+-----------------------------------------+\n"
            "|  https://tidy-otter-rides-again.trycloudflare.com  |\n"
            "+-----------------------------------------+\n"))
        url, said = cloudflare.publish(18499, timeout=10)
        self.assertEqual(url, "https://tidy-otter-rides-again.trycloudflare.com")
        self.assertIn("--url http://127.0.0.1:18499", " ".join(fake.calls()))
        # The message tells the person the two things a quick tunnel makes true.
        self.assertIn("anyone with it", said)
        self.assertIn("stops working when you stop sharing", said)
        self.assertNotIn(url, said, "the link itself is not repeated into a message that gets logged")

    def test_only_a_real_tunnel_name_is_taken_for_one(self):
        Fake(self.directory, lines=(
            "INF see https://developers.cloudflare.com/trycloudflare.com/ for the docs\n"
            "INF connect to https://evil.example.com/trycloudflare.com now\n"
            "INF https://not-a-tunnel.trycloudflare.com.attacker.test/\n"), linger=False)
        url, said = cloudflare.publish(18499, timeout=3)
        self.assertIsNone(url, f"took something that is not a quick tunnel: {url}")
        self.assertIn("did not open a tunnel", said)

    def test_a_tunnel_that_never_comes_up_gives_cloudflareds_own_words(self):
        Fake(self.directory, lines="ERR failed to dial to edge: connection refused\n")
        url, said = cloudflare.publish(18499, timeout=3)
        self.assertIsNone(url)
        self.assertIn("failed to dial to edge", said)

    def test_a_binary_that_dies_on_start_is_reported_not_awaited(self):
        Fake(self.directory, lines="", linger=False, exit_code=1)
        started = time.monotonic()
        url, said = cloudflare.publish(18499, timeout=20)
        self.assertIsNone(url)
        self.assertLess(time.monotonic() - started, 10, "waited for a process that had already gone")

    # ---- one at a time, and taken down -----------------------------------------------------------
    def test_one_tunnel_at_a_time(self):
        Fake(self.directory, lines="https://first-one.trycloudflare.com\n")
        first, _ = cloudflare.publish(18499, timeout=10)
        again, said = cloudflare.publish(18499, timeout=10)
        self.assertEqual((first, again), ("https://first-one.trycloudflare.com",) * 2)
        self.assertIn("already open", said)
        running = cloudflare.current()
        self.assertIsNotNone(running)
        self.assertEqual(running.port, 18499)

    def test_unpublish_kills_the_child_it_started(self):
        Fake(self.directory, lines="https://going-away.trycloudflare.com\n")
        url, _ = cloudflare.publish(18499, timeout=10)
        self.assertTrue(url)
        child = cloudflare.current().process
        self.assertIsNone(child.poll(), "the fake should still be running")
        self.assertTrue(cloudflare.unpublish())
        self.assertIsNotNone(child.poll(), "the child outlived unpublish()")
        self.assertIsNone(cloudflare.current())
        self.assertFalse(cloudflare.unpublish(), "nothing to take down twice")

    def test_a_tunnel_this_process_did_not_start_is_not_ours_to_close(self):
        """`cloudflared` may be running for something else on this machine — another Relay, or the
        owner's own tunnel. unpublish() closes what this process opened and nothing else."""
        other = subprocess.Popen(["sleep", "30"])
        self.addCleanup(other.kill)
        self.assertFalse(cloudflare.unpublish())
        self.assertIsNone(other.poll(), "killed a process this module never started")


class PublicLinkInviteTests(unittest.IsolatedAsyncioTestCase):
    """A public link admits as many people as the owner asked for (section 10.2).

    A one-use clamp lived here for an hour on 2026-09-18 and the owner chose against it: he wants
    to send one link to a group. What is pinned instead is that the number he picks is the number
    he gets, wherever the app is served from, and that the warning beside it is true.
    """

    class FakeInvite:
        def __init__(self, uses):
            self.invite_id, self.role, self.panes, self.uses_left = "inv1", "viewer", ["p1"], uses

        def seconds_left(self):
            return 3600

    class FakeHost:
        """Just enough host to answer invite_create, recording what it was asked for."""

        def __init__(self):
            self.asked = []

        async def invite_create(self, panes, role, *, expires_in, uses):
            self.asked.append(uses)
            return PublicLinkInviteTests.FakeInvite(uses), "https://example.test/join#i=secret"

    async def sidecar(self, *, tunnel: bool):
        from remote import gui_host
        side = gui_host.Sidecar()
        side.out = []
        side.emit = side.out.append
        side.host = self.FakeHost()
        side.served_by_cloudflare = tunnel
        return side

    async def test_a_public_link_admits_as_many_as_were_asked_for(self):
        side = await self.sidecar(tunnel=True)
        await side.invite_create({"pane": "p1", "role": "viewer", "uses": 20})
        self.assertEqual(side.host.asked, [20], "the owner's number is the number")
        reply = [m for m in side.out if m["t"] == "invite"][-1]
        self.assertEqual(reply["uses"], 20)
        # The warning says what is true of a public link, and nothing that is not.
        self.assertEqual(reply["note"], "Over a public link, anyone this link is forwarded to can "
                                        "knock. You admit each person by hand.")

    async def test_on_the_lan_the_number_asked_for_is_the_number_given(self):
        side = await self.sidecar(tunnel=False)
        await side.invite_create({"pane": "p1", "role": "viewer", "uses": 5})
        self.assertEqual(side.host.asked, [5])
        reply = [m for m in side.out if m["t"] == "invite"][-1]
        self.assertEqual(reply["uses"], 5)
        self.assertNotIn("note", reply, "nothing to explain when nothing was clamped")

    async def test_the_address_does_not_change_how_many_a_link_admits(self):
        side = await self.sidecar(tunnel=True)
        await side.invite_create({"pane": "p1", "role": "viewer", "uses": 4})
        side.served_by_cloudflare = False          # the owner picked the LAN address again
        await side.invite_create({"pane": "p1", "role": "viewer", "uses": 4})
        self.assertEqual(side.host.asked, [4, 4], "the tunnel is not a different set of rules")


class JoinNotificationTests(unittest.IsolatedAsyncioTestCase):
    """The owner is emailed when somebody joins (owner, 2026-09-18).

    A link that admits several people is only comfortable if the owner hears about each one, so
    this is the other half of lifting the clamp: once per person, never on a reconnection.
    """

    class Guest:
        def __init__(self, pid, name, role="viewer", panes=("p1",)):
            self.participant_id, self.name, self.role, self.panes = pid, name, role, list(panes)
            # The rest of what report_participants puts on the wire; the notification only reads
            # the name, the pane and the role.
            self.platform, self.invite, self.fingerprint, self.expires = "Chrome", "inv1", "ab:cd", 0.0

    class FakeGuests:
        def __init__(self, guests):
            self._guests = guests

        def live(self):
            return self._guests

        def live_invites(self):
            return []

    class FakeHost:
        def __init__(self, guests):
            self.guests = JoinNotificationTests.FakeGuests(guests)
            self.channels = {}

        def control_holder(self, pane):
            return ""

    async def sidecar(self, guests):
        from remote import gui_host
        side = gui_host.Sidecar()
        side.out = []
        side.emit = side.out.append
        side.host = self.FakeHost(guests)
        side.desktop_name = "spark"
        return side

    async def test_each_guest_is_announced_once(self):
        from remote import gui_host
        sent = []

        async def drain():
            await asyncio.sleep(0.05)   # the mail goes out on a task, not inline

        alice = self.Guest("g1", "alice")
        side = await self.sidecar([alice])
        with mock.patch.object(gui_host.email_mod, "notify_joined",
                               side_effect=lambda name, **kw: sent.append((name, kw)) or (True, "")):
            side.report_participants()
            await drain()
            side.report_participants()          # she is still there; not a second mail
            await drain()
            side.host.guests._guests.append(self.Guest("g2", "bo", role="editor"))
            side.report_participants()
            await drain()
        self.assertEqual([name for name, _ in sent], ["alice", "bo"])
        self.assertEqual(sent[1][1]["role"], "editor")
        self.assertEqual(sent[0][1]["pane"], "p1")
        self.assertEqual(sent[0][1]["desktop"], "spark")

    async def test_a_send_that_fails_is_not_an_error_on_screen(self):
        from remote import gui_host
        side = await self.sidecar([self.Guest("g1", "alice")])
        with mock.patch.object(gui_host.email_mod, "notify_joined",
                               side_effect=RuntimeError("SES is down")):
            side.report_participants()
            await asyncio.sleep(0.05)
        self.assertEqual([m for m in side.out if m.get("t") == "error"], [],
                         "a mail that did not send is not a dialog")


if __name__ == "__main__":
    unittest.main()
