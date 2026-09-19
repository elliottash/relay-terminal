# SPDX-License-Identifier: GPL-3.0-or-later
"""The hosted address (remote/gui_host.py): links through relay-terminal.ai instead of this machine.

The sidecar runs its own rendezvous, and the LAN, tailnet and cloudflare addresses are all routes to
it. The hosted address is a different rendezvous, so choosing it moves the one hub there: it
re-registers (challenge and proof of possession), its socket reconnects there, and every new
pairing, invite and code link carries the hosted origin. Here a second local rendezvous stands in
for https://join.relay-terminal.ai through RELAY_HOSTED_RENDEZVOUS.

Every Identity, DeviceStore and GuestStore lives in this test's own temporary directory: the
sidecar's `start` loads its identity with no directory, which is the owner's real profile and
keyring, so both entry points are patched before it runs.
"""
import asyncio
import contextlib
import functools
import json
import os
import socket
import tempfile
import unittest
import urllib.error
import urllib.request
from pathlib import Path
from unittest import mock

from remote import client as client_mod
from remote import gui_host
from remote import identity as identity_mod
from remote import tailnet as tailnet_mod
from remote import wire
from rendezvous.server import Store, build

APP_DIR = Path(__file__).resolve().parent.parent / "app"


def run(coroutine, timeout=60):
    return asyncio.run(asyncio.wait_for(coroutine, timeout))


def get(url):
    """(status, parsed body) for a GET, errors included."""
    try:
        with urllib.request.urlopen(url, timeout=10) as response:
            return response.status, json.loads(response.read() or b"{}")
    except urllib.error.HTTPError as error:
        body = error.read()
        try:
            return error.code, json.loads(body or b"{}")
        except ValueError:
            return error.code, {}


def closed_port() -> int:
    """A loopback port nothing is listening on."""
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


class Hosted:
    """A sidecar started with no TLS listener, and a second rendezvous as the hosted one."""

    def __init__(self, hosted_up=True):
        self.hosted_up = hosted_up

    async def __aenter__(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        (self.directory / "xdg").mkdir()
        self.patches = contextlib.ExitStack()

        # The hosted stand-in: the same server a deployment runs, on another port.
        self.hosted_store = Store(":memory:")
        self.hosted_server = build(self.hosted_store, static_root=APP_DIR)
        await self.hosted_server.start("127.0.0.1", 0)
        if self.hosted_up:
            self.hosted = f"http://127.0.0.1:{self.hosted_server.port}"
        else:
            self.hosted = f"http://127.0.0.1:{closed_port()}"

        state = self.directory
        self.patches.enter_context(mock.patch.dict(os.environ, {
            "RELAY_HOSTED_RENDEZVOUS": self.hosted,
            "XDG_DATA_HOME": str(self.directory / "xdg"),
            "RELAY_KEYRING": "off"}))
        # The safety rule: never the real identity, never the real device list.
        self.patches.enter_context(mock.patch.object(
            gui_host.identity_mod.Identity, "load_or_create",
            classmethod(lambda cls, directory=None: identity_mod.Identity.create(state))))
        self.patches.enter_context(mock.patch.object(
            gui_host.identity_mod, "DeviceStore",
            functools.partial(identity_mod.DeviceStore, state)))
        self.patches.enter_context(mock.patch.object(
            gui_host.tailnet_mod, "probe",
            lambda: tailnet_mod.Tailnet(reason="tailscale is not installed.")))
        self.patches.enter_context(mock.patch.object(
            gui_host.email_mod, "notify_joined", lambda *a, **k: (True, "")))

        self.side = gui_host.Sidecar()
        self.side.out = []
        self.side.emit = self.side.out.append
        await self.side.start({"port": 0, "tls": False, "name": "test desktop"})
        self.local = self.side.local
        await self.side.handle({"t": "pane", "id": "p1", "title": "relay-terminal",
                                "cwd": "/tmp", "rows": 24, "cols": 80, "status": "idle"})
        return self

    async def __aexit__(self, *exc):
        try:
            await self.side.stop()
            await self.hosted_server.close()
            self.hosted_store.close()
        finally:
            self.patches.close()
            self.temporary.cleanup()

    def sent(self, kind):
        return [message for message in self.side.out if message.get("t") == kind]

    def entry(self, kind="hosted"):
        return [item for item in self.sent("started")[-1]["addresses"]
                if item["kind"] == kind][0]

    async def until(self, predicate, timeout=5.0):
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while not predicate():
            if loop.time() > deadline:
                raise AssertionError("condition not reached")
            await asyncio.sleep(0.02)

    async def desktops(self, base):
        status, body = await asyncio.to_thread(get, f"{base}/v1/health")
        assert status == 200, status
        return body["desktops"]

    async def lookup(self, base, code):
        return (await asyncio.to_thread(get, f"{base}/v1/codes/{code}"))[0]

    async def choose(self, value):
        await self.side.handle({"t": "address", "value": value})

    async def code(self):
        await self.side.handle({"t": "code_create", "pane": "p1", "role": "editor"})
        return self.sent("code")[-1]


class HostedAddressTests(unittest.TestCase):
    def test_the_hosted_entry_is_offered_and_not_the_default(self):
        async def main():
            async with Hosted() as h:
                entry = h.entry()
                self.assertTrue(entry["available"])
                self.assertEqual(entry["value"], "relay-terminal.ai")
                self.assertEqual(entry["reason"], "")
                self.assertIn("works from anywhere, no certificate warning", entry["label"])
                self.assertFalse(entry["current"])
                self.assertEqual(h.sent("started")[-1]["base"], h.local)
                self.assertEqual(await h.desktops(h.local), 1)
                self.assertEqual(await h.desktops(h.hosted), 0)
        run(main())

    def test_an_unreachable_hosted_origin_is_listed_as_unavailable(self):
        async def main():
            async with Hosted(hosted_up=False) as h:
                entry = h.entry()
                self.assertFalse(entry["available"])
                self.assertEqual(entry["label"], "")
                self.assertIn("did not answer", entry["reason"])
                self.assertTrue(entry["reason"].endswith("."), "one sentence")
                # Choosing it anyway changes nothing: the hub stays home.
                await h.choose("relay-terminal.ai")
                self.assertFalse(h.side.served_by_hosted)
                self.assertEqual(h.side.host.rendezvous, h.local)
                self.assertTrue(h.sent("pairing")[-1]["url"].startswith(h.local + "/pair#"))
                self.assertFalse(h.entry()["available"])
        run(main())

    def test_switching_to_hosted_moves_every_new_link_there(self):
        async def main():
            async with Hosted() as h:
                await h.choose("relay-terminal.ai")
                self.assertTrue(h.side.served_by_hosted)
                started = h.sent("started")[-1]
                self.assertEqual(started["base"], h.hosted)
                self.assertTrue(h.entry()["current"])
                self.assertEqual(h.side.host.rendezvous, h.hosted)
                self.assertEqual(h.side.host.app_base, h.hosted)
                # The hub's socket moved: the hosted server has the desktop, the local one not.
                await h.until(lambda: h.side.host.socket is not None)
                self.assertEqual(await h.desktops(h.hosted), 1)
                self.assertEqual(await h.desktops(h.local), 0)

                # The `address` line is followed by a fresh pairing link, on the hosted origin.
                self.assertTrue(h.sent("pairing")[-1]["url"].startswith(h.hosted + "/pair#"))
                await h.side.handle({"t": "invite_create", "pane": "p1", "role": "viewer"})
                invite = h.sent("invite")[-1]
                self.assertTrue(invite["url"].startswith(h.hosted + "/join#"))
                self.assertIn("anyone this link is forwarded to can knock", invite["note"],
                              "a hosted link reaches anyone, as a public link does")

                code = await h.code()
                self.assertEqual(await h.lookup(h.hosted, code["code"]), 200)
                self.assertEqual(await h.lookup(h.local, code["code"]), 404)
        run(main())

    def test_a_guest_completes_the_code_and_knocks_through_the_hosted_server(self):
        async def main():
            async with Hosted() as h:
                await h.choose("relay-terminal.ai")
                code = await h.code()
                guest = client_mod.Client(h.hosted)
                link = await guest.join_with_code(code["code"], code["pin"], app_base=h.hosted)
                self.assertTrue(link.startswith(h.hosted + "/join#"))
                await h.until(lambda: any(state["state"] == "used"
                                          for state in h.sent("code_state")))

                knocking = asyncio.create_task(
                    client_mod.Client(h.hosted).knock(link, name="alice", platform="Firefox"))
                await h.until(lambda: h.sent("knock"), timeout=10)
                knock = h.sent("knock")[-1]
                self.assertEqual(knock["name"], "alice")
                self.assertEqual(knock["pane"], "p1")
                await h.side.handle({"t": "knock_answer", "participant": knock["participant"],
                                     "admit": True, "role": wire.EDITOR})
                joined = await asyncio.wait_for(knocking, 20)
                self.assertEqual(joined.role, wire.EDITOR)
                self.assertEqual(joined.panes, ["p1"])
                await guest.close()
        run(main())

    def test_switching_back_goes_home_and_the_hosted_code_is_dead(self):
        async def main():
            async with Hosted() as h:
                await h.choose("relay-terminal.ai")
                code = await h.code()
                self.assertEqual(await h.lookup(h.hosted, code["code"]), 200)

                # Any other address sends the hub home. With no TLS listener the only other
                # choice is the tailnet name, which is unavailable here; drop_hosted is the
                # branch every other choice takes first.
                await h.side.drop_hosted()
                h.side.announce()
                self.assertFalse(h.side.served_by_hosted)
                self.assertEqual(h.side.host.rendezvous, h.local)
                self.assertEqual(h.side.host.app_base, h.local)
                self.assertFalse(h.entry()["current"])
                await h.until(lambda: h.side.host.socket is not None)
                self.assertEqual(await h.desktops(h.local), 1)
                self.assertEqual(await h.desktops(h.hosted), 0)

                # The code ended as `expired` and the share window was told so.
                states = [s for s in h.sent("code_state") if s["code"] == code["code"]]
                self.assertEqual([s["state"] for s in states], ["expired"])
                # It no longer resolves where it was minted, and a guest who tries it is refused.
                self.assertEqual(await h.lookup(h.hosted, code["code"]), 404)
                with self.assertRaises(wire.WireError) as raised:
                    await client_mod.Client(h.hosted).join_with_code(code["code"], code["pin"])
                self.assertEqual(raised.exception.code, "no_such_code")

                # And new links are local again.
                await h.side.pair()
                self.assertTrue(h.sent("pairing")[-1]["url"].startswith(h.local + "/pair#"))
                fresh = await h.code()
                self.assertEqual(await h.lookup(h.local, fresh["code"]), 200)
                self.assertEqual(await h.lookup(h.hosted, fresh["code"]), 404)
        run(main())

    def test_choosing_a_lan_address_leaves_hosted(self):
        """The `address` line for an ordinary address takes the same way home."""
        async def main():
            async with Hosted() as h:
                await h.choose("relay-terminal.ai")
                code = await h.code()
                h.side.tls_port = 8443                     # as if the TLS listener were up
                with mock.patch("remote.devtls.local_addresses", return_value=["192.168.1.9"]), \
                        mock.patch.object(gui_host.cloudflare_mod, "probe",
                                          lambda: mock.Mock(reason="not installed.")):
                    await h.choose("192.168.1.9")
                    self.assertFalse(h.side.served_by_hosted)
                    self.assertEqual(h.side.host.rendezvous, h.local)
                    self.assertEqual(h.side.base, "https://192.168.1.9:8443")
                    self.assertEqual(h.side.host.app_base, "https://192.168.1.9:8443")
                    self.assertTrue(h.sent("pairing")[-1]["url"].startswith(
                        "https://192.168.1.9:8443/pair#"))
                    self.assertEqual(h.sent("code_state")[-1],
                                     {"t": "code_state", "code": code["code"], "state": "expired",
                                      "failures": 0})
                    # Back to hosted once more: registration works a second time.
                    await h.choose("relay-terminal.ai")
                    self.assertTrue(h.side.served_by_hosted)
                    self.assertEqual(h.side.host.app_base, h.hosted)
                h.side.tls_port = 0
        run(main())


if __name__ == "__main__":
    unittest.main()
