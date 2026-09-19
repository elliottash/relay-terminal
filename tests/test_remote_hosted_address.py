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
import shutil
import socket
import subprocess
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
from rendezvous import server as rendezvous_mod
from rendezvous.server import Store, build

HERE = Path(__file__).resolve().parent
APP_DIR = HERE.parent / "app"


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
        self.side.source.send = self.side.out.append    # the pane source's own line to the GUI
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
                self.assertIn("drops the guests and phones connected", entry["where"])
                self.assertIn("Invites made earlier work again", entry["where"])
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


# ---- per-device push origins (section 9) ----------------------------------------------------------

def subscription_message() -> dict:
    """A `push_subscribe` as a phone sends it: real P-256 keys, so the hub can encrypt to them."""
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
    from remote import push
    key = ec.generate_private_key(ec.SECP256R1())
    public = key.public_key().public_bytes(Encoding.X962, PublicFormat.UncompressedPoint)
    return {"t": "push_subscribe", "endpoint": "https://push.example.com/v1/subscription/abc",
            "p256dh": push.b64url(public), "auth": push.b64url(bytes(range(16))),
            "key": push.b64url(bytes(range(32)))}


BODY = {"v": 1, "kind": "waiting_input", "pane": "p1", "title": "Waiting for you",
        "body": "Pane 1 is waiting for input"}


class PushOriginTests(unittest.TestCase):
    """Each device is pushed through the rendezvous it subscribed through, whichever one the hub
    is registered with now: that server's VAPID key is the one the browser subscribed under."""

    async def spy(self, h):
        """Replace `/v1/push/send` on both servers with a recorder, keyed by which server."""
        h.pushed = []
        for name, server in (("local", h.side.server), ("hosted", h.hosted_server)):
            async def record(request, body, name=name):
                h.pushed.append((name, json.loads(body)))
                return rendezvous_mod.httpd.Response.json(
                    {"delivered": True, "status": 201, "drop": False})
            server.routes[("POST", "/v1/push/send")] = record

    async def phone(self, h, through):
        """Pair a phone through ``through`` (the owner allows it), then connect it there."""
        await h.side.pair()
        url = h.sent("pairing")[-1]["url"]
        asks = len(h.sent("ask"))
        pairing = client_mod.Client(through)
        task = asyncio.create_task(pairing.pair(url, name="phone", platform="Safari"))
        await h.until(lambda: len(h.sent("ask")) > asks, timeout=10)
        await h.side.handle({"t": "answer", "id": h.sent("ask")[-1]["id"], "allow": True,
                             "capability": wire.FULL})
        record = await asyncio.wait_for(task, 20)
        await pairing.close()
        return record

    async def connect(self, record, through):
        client = client_mod.Client(through)
        await client.connect(record)
        return client

    async def subscribe(self, client):
        await client.send(subscription_message())
        state = await client.expect("push_state")
        self.assertTrue(state["subscribed"])

    def device(self, h, record):
        return h.side.devices.devices[record.device_id]

    def test_a_phone_paired_locally_is_pushed_locally_after_the_hub_moves(self):
        async def main():
            async with Hosted() as h:
                await self.spy(h)
                record = await self.phone(h, h.local)
                self.assertEqual(self.device(h, record).origin, "", "local is recorded as \"\"")
                client = await self.connect(record, h.local)
                await self.subscribe(client)
                await client.close()

                await h.choose("relay-terminal.ai")
                self.assertEqual(h.side.host.rendezvous, h.hosted)
                await h.side.host.notifier.deliver(BODY)
                self.assertEqual([name for name, _ in h.pushed], ["local"])
                self.assertEqual(h.pushed[0][1]["token"], h.side.host.tokens[h.local],
                                 "with the local token, which still works there")
                self.assertEqual(h.pushed[0][1]["endpoint"],
                                 "https://push.example.com/v1/subscription/abc")
        run(main())

    def test_a_phone_that_subscribes_again_moves_to_the_new_origin(self):
        async def main():
            async with Hosted() as h:
                await self.spy(h)
                record = await self.phone(h, h.local)
                client = await self.connect(record, h.local)
                await self.subscribe(client)
                await client.close()

                # The hub moves; the phone reaches it through the hosted server, sees a different
                # /v1/push/key and subscribes again (app/pushkey.js).
                await h.choose("relay-terminal.ai")
                client = await self.connect(record, h.hosted)
                await self.subscribe(client)
                await client.close()
                self.assertEqual(self.device(h, record).origin, h.hosted)
                await h.side.host.notifier.deliver(BODY)
                self.assertEqual([name for name, _ in h.pushed], ["hosted"])

                # Back home: its subscription is still under the hosted key, so it stays there.
                await h.side.drop_hosted()
                self.assertEqual(h.side.host.rendezvous, h.local)
                await h.side.host.notifier.deliver(BODY)
                self.assertEqual([name for name, _ in h.pushed], ["hosted", "hosted"])
        run(main())

    def test_a_phone_paired_through_hosted_records_it_and_is_never_pushed_locally(self):
        async def main():
            async with Hosted() as h:
                await self.spy(h)
                await h.choose("relay-terminal.ai")
                record = await self.phone(h, h.hosted)
                self.assertEqual(self.device(h, record).origin, h.hosted)
                client = await self.connect(record, h.hosted)
                await self.subscribe(client)
                await client.close()
                await h.side.drop_hosted()
                # The hub re-registered locally, which rotated nothing at the hosted server.
                await h.side.host.notifier.deliver(BODY)
                self.assertEqual([name for name, _ in h.pushed], ["hosted"])
        run(main())

    def test_an_unreachable_origin_drops_the_push_and_says_so_once_without_the_endpoint(self):
        async def main():
            async with Hosted() as h:
                await self.spy(h)
                await h.choose("relay-terminal.ai")
                record = await self.phone(h, h.hosted)
                client = await self.connect(record, h.hosted)
                await self.subscribe(client)
                await client.close()
                await h.side.drop_hosted()
                await h.hosted_server.close()          # relay-terminal.ai goes away
                with self.assertLogs("relay.host", level="INFO") as logs:
                    await h.side.host.notifier.deliver(BODY)
                    await h.side.host.notifier.deliver(BODY)
                    gui_host.host_mod.log.info("end of test")   # so assertLogs has a line
                unreachable = [line for line in logs.output if "unreachable" in line]
                self.assertEqual(len(unreachable), 1, logs.output)
                self.assertNotIn("push.example.com", "\n".join(logs.output))
                self.assertEqual(h.pushed, [], "never sent through another origin instead")
                self.assertIsNotNone(self.device(h, record).push, "dropped, not unsubscribed")
                # Restart it on the same port so the harness can close it again.
                await h.hosted_server.start("127.0.0.1", int(h.hosted.rsplit(":", 1)[1]))
        run(main())

    def test_a_record_written_before_origins_is_the_local_registry(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "devices.json"
            path.write_text(json.dumps({"devices": [{
                "device_id": "d1", "name": "old phone", "platform": "Safari",
                "public_key": "AAAA", "capability": "full"}]}))
            store = identity_mod.DeviceStore(Path(directory))
            self.assertEqual(store.devices["d1"].origin, "")


@unittest.skipUnless(shutil.which("node"), "node is not installed")
class PushKeyRenewalTests(unittest.TestCase):
    """app/pushkey.js, the phone's half: renew under a moved key, silently, and only then."""

    def test_the_phone_renews_only_when_the_key_moved(self):
        from remote import push
        old = push.b64url(bytes(range(65)))
        new = push.b64url(bytes(range(1, 66)))
        done = subprocess.run([shutil.which("node"), str(HERE / "push_key_peer.mjs"), old, new],
                              capture_output=True, text=True, cwd=str(HERE.parent))
        self.assertEqual(done.returncode, 0, done.stderr)
        results = json.loads(done.stdout)
        self.assertTrue(results["moved"]["renewed"])
        self.assertEqual(results["moved"]["subscribed"], [new])
        self.assertEqual(results["moved"]["reported"],
                         [{"endpoint": "https://push.example/fresh", "vapid": new}])
        self.assertFalse(results["same"]["renewed"])
        self.assertEqual(results["same"]["subscribed"], [])
        self.assertTrue(results["browser_only"]["renewed"])
        self.assertFalse(results["unknown"]["renewed"])
        self.assertEqual(results["unknown"]["fetched"], 0)
        self.assertFalse(results["none"]["renewed"])
        self.assertEqual(results["made_under"], old)


if __name__ == "__main__":
    unittest.main()
