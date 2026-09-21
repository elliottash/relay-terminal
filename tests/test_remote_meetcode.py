# SPDX-License-Identifier: AGPL-3.0-or-later
"""Join with a meeting code and a PIN (card #97EG): the routes, the code phase and the handoff.

Everything runs over the real rendezvous, the real hub and real sockets, as the other remote tests
do: the rules here — three failures burn, success burns, a code room never reaches the Noise
path — are enforcement rules, and a stubbed hub would only test the stub. The attacker's side is
driven with raw sockets and remote/meetcode.py's own helpers, so an attempt can fail in each of the
ways the card lists: a wrong tag, an invalid point, closing early, and running out of time.

The security claims (what the rendezvous can and cannot learn, a forwarded fragment, the CPace
vectors) are in tests/test_remote_security.py with the others.
"""
import asyncio
import contextlib
import json
import tempfile
import time
import unittest
import urllib.error
import urllib.request
from pathlib import Path

from remote import client as client_mod
from remote import gui_host
from remote import guests as guests_mod
from remote import host as host_mod
from remote import identity as identity_mod
from remote import meetcode
from remote import panes as panes_mod
from remote import pairing, wire, ws
from remote.cpace import CPace
from rendezvous import server as rendezvous_mod
from rendezvous.server import Store, build


def run(coroutine, timeout=60):
    return asyncio.run(asyncio.wait_for(coroutine, timeout))


class Harness:
    """A rendezvous, a desktop with two panes, and an owner who answers knocks as told.

    Every store is given this harness's own temporary directory: an Identity made without one
    reads and writes the owner's real profile and keyring.
    """

    def __init__(self, admit=True, answer_delay=0.0, allow_device=True,
                 capability=wire.FULL):
        self.admit = admit
        self.answer_delay = answer_delay
        # The owner's answer to a *pairing* dialog (card #FR1C), the seam `approver` fills: a
        # pairing code ends at the same five-digit compare a scanned QR does.
        self.allow_device = allow_device
        self.capability = capability
        self.pair_asks: list[host_mod.PairRequest] = []
        self.knocks: list[host_mod.KnockRequest] = []
        self.states: list[dict] = []
        # What the rendezvous was sent: HTTP request lines, headers and bodies, and every socket
        # message in either direction. The PIN must never be in any of it.
        self.seen: list[bytes] = []

    async def __aenter__(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.store = Store(":memory:")
        self.server = build(self.store)
        self._spy(self.server)
        await self.server.start("127.0.0.1", 0)
        self.base = f"http://127.0.0.1:{self.server.port}"

        self.identity = identity_mod.Identity.create(self.directory)
        self.devices = identity_mod.DeviceStore(self.directory)
        self.guests = guests_mod.GuestStore(self.directory, devices=self.devices)
        self.source = panes_mod.DemoPaneSource()

        async def knock_approver(request):
            self.knocks.append(request)
            if self.answer_delay:
                await asyncio.sleep(self.answer_delay)
            return self.admit, request.role

        async def approver(request):
            self.pair_asks.append(request)
            if self.answer_delay:
                await asyncio.sleep(self.answer_delay)
            return self.allow_device, self.capability

        self.host = host_mod.Host(self.identity, self.devices, self.source,
                                  app_base="https://app.example", approver=approver,
                                  knock_approver=knock_approver,
                                  guests=self.guests, name="test desktop")
        self.host.codes.on_state(lambda record: self.states.append(
            {"code": record.code, "state": record.state, "failures": record.failures}))
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

    def _spy(self, server):
        seen = self.seen
        for key, handler in list(server.routes.items()):
            async def spied(request, body, handler=handler):
                seen.append(f"{request.method} {request.target}".encode())
                seen.append(json.dumps(request.headers).encode())
                seen.append(body)
                return await handler(request, body)
            server.routes[key] = spied
        original = server.sockets["/v1/connect"]

        async def connect(socket):
            seen.append(socket.request.target.encode())
            recv, send = socket.recv, socket.send

            async def spied_recv():
                message = await recv()
                seen.append(message if isinstance(message, bytes) else message.encode())
                return message

            async def spied_send(message):
                seen.append(message if isinstance(message, bytes) else message.encode())
                return await send(message)
            socket.recv, socket.send = spied_recv, spied_send
            await original(socket)
        server.sockets["/v1/connect"] = connect

    async def code(self, pane="pane-1", role=wire.EDITOR):
        return await self.host.code_create([pane], role)

    async def pair_code(self):
        return await self.host.pair_code()

    async def lookup(self, code, headers=None):
        """``GET /v1/codes/<code>`` → (status, body), off the loop the server runs on."""
        return await asyncio.to_thread(self._lookup, code, headers)

    async def status(self, code, headers=None):
        return (await self.lookup(code, headers))[0]

    def _lookup(self, code, headers=None):
        request = urllib.request.Request(f"{self.base}/v1/codes/{code}", headers=headers or {})
        try:
            with urllib.request.urlopen(request, timeout=10) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as error:
            return error.code, error.read()

    async def post(self, path, payload):
        return await asyncio.to_thread(self._post, path, payload)

    def _post(self, path, payload):
        request = urllib.request.Request(f"{self.base}{path}", data=json.dumps(payload).encode(),
                                         headers={"Content-Type": "application/json"},
                                         method="POST")
        try:
            with urllib.request.urlopen(request, timeout=10) as response:
                return response.status, json.loads(response.read())
        except urllib.error.HTTPError as error:
            return error.code, json.loads(error.read())

    def credentials(self):
        return {"desktop_id": self.identity.desktop_id, "token": self.host.token}

    async def raw(self, room):
        """A bare socket on a code room, for an attempt the Python client would never make."""
        return await ws.connect(self.base.replace("http://", "ws://")
                                + f"/v1/connect?room={room}")

    def audit_lines(self):
        return [json.loads(line) for path in sorted(self.directory.glob("audit-*.jsonl"))
                for line in path.read_text().splitlines()]

    async def until(self, predicate, timeout=5.0):
        deadline = time.monotonic() + timeout
        while not predicate():
            if time.monotonic() > deadline:
                raise AssertionError("condition not reached")
            await asyncio.sleep(0.02)


async def meet_error(socket, timeout=5.0) -> str:
    """The `meet_error` a code room answers with, then its close."""
    message = json.loads(await asyncio.wait_for(socket.recv(), timeout))
    assert message["t"] == "meet_error", message
    return message["error"]


def wrong_pin(pin: str) -> str:
    return f"{(int(pin) + 1) % 10000:04d}"


# ---- the rendezvous routes -----------------------------------------------------------------------

class RouteTests(unittest.TestCase):
    def test_a_code_is_four_letters_from_the_alphabet_and_resolves_in_any_case(self):
        async def main():
            async with Harness() as harness:
                record = await harness.code()
                self.assertEqual(len(record.code), 4)
                self.assertTrue(set(record.code) <= set(rendezvous_mod.CODE_ALPHABET))
                self.assertTrue(set("ILO").isdisjoint(rendezvous_mod.CODE_ALPHABET))
                status, body = await harness.lookup(record.code.lower())
                self.assertEqual(status, 200)
                self.assertEqual(json.loads(body), {"room": record.room})
        run(main())

    def test_posting_a_code_is_authenticated_like_rooms(self):
        async def main():
            async with Harness() as harness:
                room, _ = await harness.host._open_room(600)
                status, _ = await harness.post("/v1/codes", {
                    "desktop_id": harness.identity.desktop_id, "token": "wrong", "room": room})
                self.assertEqual(status, 401)
                status, _ = await harness.post("/v1/codes", {**harness.credentials(),
                                                             "room": "not-a-room-of-mine"})
                self.assertEqual(status, 404)
                status, reply = await harness.post("/v1/codes", {
                    **harness.credentials(), "room": room, "ttl": 86400})
                self.assertEqual(status, 200)
                self.assertLessEqual(reply["expires_in"], 600, "ttl is capped at ten minutes")
                status, _ = await harness.post(f"/v1/codes/{reply['code']}/burn",
                                               {"desktop_id": harness.identity.desktop_id,
                                                "token": "wrong"})
                self.assertEqual(status, 401)
                self.assertEqual(await harness.status(reply["code"]), 200,
                                 "a bad burn burns nothing")
                status, _ = await harness.post(f"/v1/codes/{reply['code'].lower()}/burn",
                                               harness.credentials())
                self.assertEqual(status, 200)
                self.assertEqual(await harness.status(reply["code"]), 404, "burned at once")
        run(main())

    def test_live_codes_are_unique(self):
        store = Store(":memory:")
        try:
            store.register("d" * 32, "key")
            room, _ = store.open_room("d" * 32, 600)
            codes = set()
            for _ in range(rendezvous_mod.MAX_LIVE_CODES_PER_DESKTOP):
                code, _ = store.open_code("d" * 32, room, 600)
                codes.add(code)
            self.assertEqual(len(codes), rendezvous_mod.MAX_LIVE_CODES_PER_DESKTOP)
            self.assertIsNone(store.open_code("d" * 32, room, 600),
                              "one desktop cannot hold more than its share of live codes")
            # Uniqueness is enforced, not hoped for: make the alphabet one letter so every draw
            # collides with the first code, and the second allocation must not reuse it.
            original = rendezvous_mod.CODE_ALPHABET
            rendezvous_mod.CODE_ALPHABET = "B"
            try:
                store.codes.clear()
                first, _ = store.open_code("d" * 32, room, 600)
                with self.assertRaises(RuntimeError):
                    store.open_code("d" * 32, room, 600)
                self.assertEqual(list(store.codes), [first])
            finally:
                rendezvous_mod.CODE_ALPHABET = original
        finally:
            store.close()

    def test_unknown_and_expired_codes_answer_identically(self):
        async def main():
            async with Harness() as harness:
                room, _ = await harness.host._open_room(600)
                _, reply = await harness.post("/v1/codes", {**harness.credentials(),
                                                            "room": room, "ttl": 1})
                self.assertEqual(await harness.status(reply["code"]), 200)
                await asyncio.sleep(1.2)
                expired = await harness.lookup(reply["code"])
                unknown = await harness.lookup("ZZZZ" if reply["code"] != "ZZZZ" else "YYYY")
                malformed = await harness.lookup("not-a-code")
                self.assertEqual(expired[0], 404)
                self.assertEqual(expired, unknown)
                self.assertEqual(expired, malformed)
        run(main())

    def test_lookups_are_rate_limited_per_address(self):
        async def main():
            async with Harness() as harness:
                statuses = [await harness.status("ZZZZ")
                            for _ in range(rendezvous_mod.CODE_LOOKUPS_PER_MINUTE + 1)]
                self.assertEqual(statuses[:-1], [404] * rendezvous_mod.CODE_LOOKUPS_PER_MINUTE)
                self.assertEqual(statuses[-1], 429)
                # Behind a proxy on loopback (cloudflared, tailscale serve) the address is the
                # one the proxy names; another visitor has a budget of their own.
                self.assertEqual(
                    await harness.status("ZZZZ", {"CF-Connecting-IP": "198.51.100.7"}), 404)
        run(main())

    def test_a_forwarding_header_is_believed_only_from_loopback(self):
        def request(peer, headers):
            return ws.Request("GET", "/v1/codes/ABCD", headers=headers, peer=peer)
        self.assertEqual(rendezvous_mod.client_address(
            request("203.0.113.9:5000", {"cf-connecting-ip": "198.51.100.7"})), "203.0.113.9")
        self.assertEqual(rendezvous_mod.client_address(
            request("127.0.0.1:5000", {"cf-connecting-ip": "198.51.100.7"})), "198.51.100.7")
        self.assertEqual(rendezvous_mod.client_address(
            request("127.0.0.1:5000", {"x-forwarded-for": "10.0.0.1, 100.64.0.3"})),
            "100.64.0.3")
        self.assertEqual(rendezvous_mod.client_address(request("127.0.0.1:5000", {})),
                         "127.0.0.1")

    def test_the_code_is_never_kept_in_the_database(self):
        async def main():
            async with Harness() as harness:
                record = await harness.code()
                dump = "\n".join(harness.store.db.iterdump())
                self.assertNotIn(record.code, dump)
                self.assertNotIn(record.pin, dump)
        run(main())


# ---- the code phase ------------------------------------------------------------------------------

class CodePhaseTests(unittest.TestCase):
    def test_the_right_code_and_pin_give_an_invite_whose_knock_works(self):
        async def main():
            async with Harness() as harness:
                record = await harness.code()
                guest = client_mod.Client(harness.base)
                url = await guest.join_with_code(record.code.lower(), record.pin,
                                                 app_base="https://app.example")
                self.assertTrue(url.startswith("https://app.example/join#v=1&"))
                link = pairing.parse_invite_url(url)
                invite = harness.guests.invite(record.invite_id)
                self.assertEqual(link["room"], invite.room)
                self.assertTrue(invite.single_knock)
                self.assertEqual(invite.uses_left, 1)
                self.assertLessEqual(invite.seconds_left(), 600)

                # From here the ordinary path: Noise, knock, the owner admits by hand.
                client = client_mod.Client(harness.base)
                joined = await client.knock(url, name="alice", platform="Chrome")
                self.assertEqual(joined.role, wire.EDITOR)
                self.assertEqual(joined.panes, ["pane-1"])
                self.assertEqual(len(harness.knocks), 1, "the owner was asked, by hand")
                self.assertEqual(harness.states[-1]["state"], "used")
                self.assertEqual(await harness.status(record.code), 404,
                                 "success burns the code")
                kinds = [line["kind"] for line in harness.audit_lines()]
                for kind in ("code_create", "code_used", "knock", "admitted"):
                    self.assertIn(kind, kinds)
                self.assertLess(kinds.index("code_used"), kinds.index("knock"))
                await client.close()
        run(main())

    def test_success_burns_the_code(self):
        async def main():
            async with Harness() as harness:
                record = await harness.code()
                await client_mod.Client(harness.base).join_with_code(record.code, record.pin)
                with self.assertRaises(wire.WireError) as caught:
                    await client_mod.Client(harness.base).join_with_code(record.code, record.pin)
                self.assertEqual(caught.exception.code, "no_such_code")
                socket = await harness.raw(record.room)
                self.assertEqual(await meet_error(socket), "burned")
                await socket.close()
                self.assertEqual(record.state, "used")
                self.assertFalse(harness.guests.invite(record.invite_id).dead,
                                 "the invite stays for the knock")
        run(main())

    def test_three_wrong_pins_burn_the_code_and_its_invite(self):
        async def main():
            async with Harness() as harness:
                record = await harness.code()
                for attempt in range(meetcode.MAX_FAILURES):
                    with self.assertRaises(wire.WireError) as caught:
                        await client_mod.Client(harness.base).join_with_code(
                            record.code, wrong_pin(record.pin))
                    self.assertEqual(caught.exception.code, "wrong_pin")
                    await harness.until(lambda: record.failures == attempt + 1)
                await harness.until(lambda: record.state == "burned")
                self.assertEqual(harness.states[-1],
                                 {"code": record.code, "state": "burned", "failures": 3})
                self.assertTrue(harness.guests.invite(record.invite_id).dead)
                for _ in range(100):
                    if await harness.status(record.code) == 404:
                        break
                    await asyncio.sleep(0.05)
                self.assertEqual(await harness.status(record.code), 404)
                with self.assertRaises(wire.WireError) as caught:
                    await client_mod.Client(harness.base).join_with_code(record.code, record.pin)
                self.assertEqual(caught.exception.code, "no_such_code")
                kinds = [line["kind"] for line in harness.audit_lines()]
                self.assertEqual(kinds.count("code_attempt"), 3)
                self.assertIn("code_burned", kinds)
        run(main())

    def test_a_bad_confirmation_tag_is_a_wrong_pin(self):
        """A guest that skips its own check of tag_b and confirms anyway is caught here."""
        async def main():
            async with Harness() as harness:
                record = await harness.code()
                socket = await harness.raw(record.room)
                party = CPace(b"0000" if record.pin != "0000" else b"1111", meetcode.CI,
                              record.room.encode(), initiator=True, ad=meetcode.AD_GUEST)
                await socket.send(meetcode.encode({"t": "meet_a",
                                                   "y": pairing.b64(party.message)}))
                reply = json.loads(await socket.recv())
                self.assertEqual(reply["t"], "meet_b")
                await socket.send(meetcode.encode({"t": "meet_confirm",
                                                   "tag": pairing.b64(bytes(32))}))
                self.assertEqual(await meet_error(socket), "wrong_pin")
                await socket.close()
                self.assertEqual(record.failures, 1)
                self.assertEqual(harness.audit_lines()[-1]["reason"], "wrong_pin")
        run(main())

    def test_an_invalid_point_is_a_failure(self):
        async def main():
            async with Harness() as harness:
                record = await harness.code()
                socket = await harness.raw(record.room)
                await socket.send(meetcode.encode({"t": "meet_a", "y": pairing.b64(bytes(32))}))
                self.assertEqual(await meet_error(socket), "wrong_pin")
                await socket.close()
                self.assertEqual(record.failures, 1)
                self.assertEqual(harness.audit_lines()[-1]["reason"], "invalid_point")
        run(main())

    def test_closing_early_is_a_failure(self):
        async def main():
            async with Harness() as harness:
                record = await harness.code()
                socket = await harness.raw(record.room)
                await harness.until(lambda: record.in_flight == 1)
                await socket.close()
                await harness.until(lambda: record.failures == 1)
                self.assertEqual(harness.audit_lines()[-1]["reason"], "closed")
                self.assertEqual(record.state, "live", "one failure is not three")
        run(main())

    def test_running_out_of_time_is_a_failure(self):
        async def main():
            original = meetcode.ATTEMPT_TIMEOUT
            meetcode.ATTEMPT_TIMEOUT = 0.5
            try:
                async with Harness() as harness:
                    record = await harness.code()
                    socket = await harness.raw(record.room)
                    self.assertEqual(await meet_error(socket), "wrong_pin")
                    await socket.close()
                    self.assertEqual(record.failures, 1)
                    self.assertEqual(harness.audit_lines()[-1]["reason"], "timeout")
            finally:
                meetcode.ATTEMPT_TIMEOUT = original
        run(main())

    def test_attempts_in_the_air_count_against_the_three(self):
        """tag_b tells a guest whether its PIN was right before it confirms, so three sockets
        opened at once must not become a fourth, fifth and hundredth guess."""
        async def main():
            async with Harness() as harness:
                record = await harness.code()
                sockets = [await harness.raw(record.room) for _ in range(meetcode.MAX_FAILURES)]
                await harness.until(lambda: record.in_flight == meetcode.MAX_FAILURES)
                fourth = await harness.raw(record.room)
                self.assertEqual(await meet_error(fourth), "burned")
                for socket in sockets + [fourth]:
                    await socket.close()
                await harness.until(lambda: record.state == "burned")
        run(main())

    def test_an_unused_code_expires_and_takes_its_invite(self):
        async def main():
            original = meetcode.CODE_LIFETIME
            meetcode.CODE_LIFETIME = 1.0
            try:
                async with Harness() as harness:
                    record = await harness.code()
                    self.assertEqual(await harness.status(record.code), 200)
                    await harness.until(lambda: record.state == "expired", timeout=5)
                    self.assertEqual(harness.states[-1]["state"], "expired")
                    self.assertTrue(harness.guests.invite(record.invite_id).dead)
                    self.assertEqual(await harness.status(record.code), 404)
                    self.assertIn("code_expired", [l["kind"] for l in harness.audit_lines()])
            finally:
                meetcode.CODE_LIFETIME = original
        run(main())

    def test_the_owner_refusing_the_one_knock_burns_the_invite(self):
        async def main():
            async with Harness(admit=False) as harness:
                record = await harness.code()
                url = await client_mod.Client(harness.base).join_with_code(record.code,
                                                                           record.pin)
                with self.assertRaises(wire.WireError):
                    await client_mod.Client(harness.base).knock(url, name="alice",
                                                                platform="Chrome")
                self.assertTrue(harness.guests.invite(record.invite_id).dead)
        run(main())

    def test_a_link_invite_still_takes_many_knocks(self):
        """single_knock is for a code's invite only; a link invite is unchanged."""
        async def main():
            async with Harness(admit=False) as harness:
                invite, url = await harness.host.invite_create(["pane-1"], wire.VIEWER, uses=2)
                self.assertFalse(invite.single_knock)
                for _ in range(2):
                    with self.assertRaises(wire.WireError) as caught:
                        await client_mod.Client(harness.base).knock(url, name="bob",
                                                                    platform="Chrome")
                    self.assertEqual(caught.exception.code, "not_admitted")
                self.assertFalse(invite.dead)
        run(main())

    def test_the_pin_never_reaches_the_rendezvous_or_the_audit_log(self):
        async def main():
            async with Harness() as harness:
                record = await harness.code()
                for _ in range(2):
                    with contextlib.suppress(wire.WireError):
                        await client_mod.Client(harness.base).join_with_code(
                            record.code, wrong_pin(record.pin))
                url = await client_mod.Client(harness.base).join_with_code(record.code,
                                                                           record.pin)
                client = client_mod.Client(harness.base)
                await client.knock(url, name="alice", platform="Chrome")
                await client.close()
                pin = record.pin.encode()
                self.assertTrue(harness.seen, "the spy saw the traffic")
                for message in harness.seen:
                    self.assertNotIn(pin, message)
                for line in harness.audit_lines():
                    self.assertNotIn("pin", line)
                    self.assertNotIn(record.pin, json.dumps(line))
        run(main())


# ---- the sidecar ---------------------------------------------------------------------------------

class SidecarTests(unittest.TestCase):
    def test_code_create_answers_with_the_code_and_code_state_follows(self):
        async def main():
            async with Harness() as harness:
                sidecar = gui_host.Sidecar()
                sent = []
                sidecar.emit = sent.append
                sidecar.host = harness.host
                harness.host.codes.on_state(sidecar.code_state)
                await sidecar.handle({"t": "code_create", "pane": "pane-1", "role": "editor"})
                reply = [m for m in sent if m["t"] == "code"][0]
                self.assertEqual(set(reply), {"t", "code", "pin", "expires", "invite"})
                self.assertRegex(reply["pin"], r"^\d{4}$")
                self.assertLessEqual(reply["expires"], 600)
                self.assertIn(reply["invite"], [i["id"] for m in sent if m["t"] == "participants"
                                                for i in m["invites"]])
                await client_mod.Client(harness.base).join_with_code(reply["code"], reply["pin"])
                await harness.until(lambda: any(m["t"] == "code_state" for m in sent))
                self.assertEqual([m for m in sent if m["t"] == "code_state"][-1],
                                 {"t": "code_state", "code": reply["code"], "state": "used",
                                  "failures": 0})
        run(main())

    def test_code_revoke_burns_the_code_and_its_invite(self):
        async def main():
            async with Harness() as harness:
                sidecar = gui_host.Sidecar()
                sent = []
                sidecar.emit = sent.append
                sidecar.host = harness.host
                harness.host.codes.on_state(sidecar.code_state)
                await sidecar.handle({"t": "code_create", "pane": "pane-1", "role": "viewer"})
                reply = [m for m in sent if m["t"] == "code"][0]
                await sidecar.handle({"t": "code_revoke", "code": reply["code"].lower()})
                self.assertEqual([m for m in sent if m["t"] == "code_state"][-1],
                                 {"t": "code_state", "code": reply["code"], "state": "burned",
                                  "failures": 0})
                self.assertTrue(harness.guests.invite(reply["invite"]).dead)
                self.assertEqual(await harness.status(reply["code"]), 404)
                burned = [l for l in harness.audit_lines() if l["kind"] == "code_burned"]
                self.assertEqual(burned[-1]["reason"], "revoked")
                # A revoked code is still a code room: a late connection is refused, not handed
                # to the Noise path.
                record = harness.host.codes.by_code(reply["code"])
                socket = await harness.raw(record.room)
                self.assertEqual(await meet_error(socket), "burned")
                await socket.close()
                # Revoking again, or a code that never existed, changes nothing.
                self.assertIsNone(await harness.host.codes.revoke(reply["code"]))
                self.assertIsNone(await harness.host.codes.revoke("ZZZZ"))
        run(main())

    def test_a_new_code_for_a_pane_burns_the_live_one(self):
        async def main():
            async with Harness() as harness:
                first = await harness.code("pane-1")
                other = await harness.code("pane-2")
                second = await harness.code("pane-1")
                self.assertEqual(first.state, "burned")
                self.assertTrue(harness.guests.invite(first.invite_id).dead)
                self.assertEqual(await harness.status(first.code), 404)
                self.assertEqual(harness.states[-1],
                                 {"code": first.code, "state": "burned", "failures": 0})
                burned = [l for l in harness.audit_lines() if l["kind"] == "code_burned"]
                self.assertEqual(burned[-1]["reason"], "replaced")
                self.assertEqual(second.state, "live")
                self.assertEqual(other.state, "live", "a code on another pane is untouched")
                url = await client_mod.Client(harness.base).join_with_code(second.code,
                                                                           second.pin)
                self.assertTrue(url)
        run(main())

    def test_code_create_and_revoke_are_owner_only(self):
        for kind in ("code_create", "code_revoke"):
            self.assertIn(kind, wire.OWNER_ONLY)
            self.assertIn(kind, wire.NEVER_FROM_CLIENT)
            self.assertNotIn(kind, wire.CLIENT_TYPES)


# ---- a code that pairs a phone (card #FR1C) ------------------------------------------------------

class PairCodeTests(unittest.TestCase):
    """The same four letters and four digits, delivering the *pairing* fragment.

    Everything the code phase does is #97EG's and is tested above; what is tested here is the one
    thing that differs — what a correct PIN earns — and the rule that keeps the two apart: a
    pairing code and an invite code are not interchangeable, and it is the fragment's own shape
    that decides, not the code and not the page it was typed into.
    """

    def test_a_correct_pin_earns_the_pairing_fragment_and_pairs_the_phone(self):
        async def main():
            async with Harness() as harness:
                record = await harness.pair_code()
                self.assertEqual(record.kind, meetcode.KIND_PAIR)
                self.assertEqual(len(record.code), 4)
                self.assertRegex(record.pin, r"^\d{4}$")

                # What the phone is handed: a pairing link, on the pairing page, for the room the
                # desktop opened for this code — not an invite.
                phone = client_mod.Client(harness.base)
                url = await phone.join_with_code(record.code.lower(), record.pin,
                                                 app_base="https://app.example")
                self.assertTrue(url.startswith("https://app.example/pair#v=1&"), url)
                self.assertEqual(pairing.fragment_kind(url.split("#", 1)[1]), "pair")
                link = pairing.parse_pair_url(url)
                self.assertEqual(link["desktop_public"], harness.identity.public)
                self.assertEqual(link["room"], record.pair_room)
                with self.assertRaises(ValueError):
                    pairing.parse_invite_url(url)

                # And from here section 5 unchanged: Noise IK against `d`, `pair_prove`, the
                # owner's dialog with the five digits, the capability, the connect token.
                paired = await phone.pair(url, name="iPhone", platform="Safari")
                self.assertEqual(len(harness.pair_asks), 1)
                self.assertEqual(harness.pair_asks[0].name, "iPhone")
                self.assertRegex(harness.pair_asks[0].code, r"^\d{5}$")
                self.assertEqual(harness.pair_asks[0].code, phone.auth_code,
                                 "the same five digits on both screens")
                self.assertEqual(paired.capability, wire.FULL)
                self.assertTrue(paired.connect_token)
                self.assertEqual([device.name for device in harness.devices.live()], ["iPhone"])
                await phone.close()

                # The code was one use, and its record says which sort it was.
                self.assertEqual(record.state, "used")
                self.assertEqual(harness.states[-1]["state"], "used")
                self.assertEqual(await harness.status(record.code), 404)
                create = [l for l in harness.audit_lines() if l["kind"] == "code_create"][-1]
                self.assertEqual(create["code_kind"], "pair")
                self.assertIsNone(create["invite"])
                kinds = [l["kind"] for l in harness.audit_lines()]
                self.assertLess(kinds.index("code_used"), kinds.index("pair"))
        run(main())

    def test_the_paired_device_reconnects_and_reads_the_panes(self):
        """The point of the exercise: what the phone holds afterwards is an ordinary device
        record, so the next connection is an ordinary one."""
        async def main():
            async with Harness() as harness:
                record = await harness.pair_code()
                phone = client_mod.Client(harness.base)
                paired = await phone.pair_with_code(record.code, record.pin,
                                                    name="iPad", platform="Safari")
                await phone.close()
                again = client_mod.Client(harness.base, static_private=paired.static_private)
                await again.connect(paired)
                panes = await again.expect("panes")
                self.assertTrue(panes["items"])
                await again.close()
        run(main())

    def test_the_pairing_room_is_this_codes_alone_and_never_in_a_qr(self):
        """A room behind both a QR and a typed code would let a scan and the code race for its
        one use, and the loser would be told the secret was wrong."""
        async def main():
            async with Harness() as harness:
                shown, qr_room = await harness.host.open_pairing()
                record = await harness.pair_code()
                self.assertNotEqual(record.pair_room, qr_room.room)
                self.assertNotEqual(record.pair_room, record.room, "and not the code's own room")
                self.assertNotIn(record.pair_room, shown)
                second = await harness.pair_code()
                self.assertNotEqual(second.pair_room, record.pair_room)
                self.assertEqual(record.state, "burned", "one live pairing code at a time")
                burned = [l for l in harness.audit_lines() if l["kind"] == "code_burned"][-1]
                self.assertEqual((burned["reason"], burned["code_kind"]), ("replaced", "pair"))
                # The QR the owner already had is untouched by either.
                self.assertIsNotNone(harness.host.rooms.get(qr_room.room))
                self.assertIsNone(harness.host.rooms.get(record.pair_room),
                                  "a burned code's room is gone")
        run(main())

    def test_three_wrong_pins_burn_the_code_and_the_room_behind_it(self):
        async def main():
            async with Harness() as harness:
                record = await harness.pair_code()
                for attempt in range(meetcode.MAX_FAILURES):
                    with self.assertRaises(wire.WireError) as caught:
                        await client_mod.Client(harness.base).join_with_code(
                            record.code, wrong_pin(record.pin))
                    self.assertEqual(caught.exception.code, "wrong_pin")
                    await harness.until(lambda: record.failures == attempt + 1)
                await harness.until(lambda: record.state == "burned")
                self.assertEqual(harness.states[-1],
                                 {"code": record.code, "state": "burned", "failures": 3})
                self.assertIsNone(harness.host.rooms.get(record.pair_room),
                                  "the pairing room dies with the code")
                for _ in range(100):
                    if await harness.status(record.code) == 404:
                        break
                    await asyncio.sleep(0.05)
                self.assertEqual(await harness.status(record.code), 404)
                attempts = [l for l in harness.audit_lines() if l["kind"] == "code_attempt"]
                self.assertEqual(len(attempts), 3)
                self.assertEqual({l["code_kind"] for l in attempts}, {"pair"})
                self.assertTrue(all(l["invite"] is None for l in attempts))
                self.assertNotIn(record.pin, json.dumps(harness.audit_lines()))
        run(main())

    def test_revoking_a_pairing_code_burns_it_and_its_room(self):
        async def main():
            async with Harness() as harness:
                record = await harness.pair_code()
                self.assertIs(await harness.host.codes.revoke(record.code.lower(),
                                                              meetcode.KIND_PAIR), record)
                self.assertEqual(record.state, "burned")
                self.assertIsNone(harness.host.rooms.get(record.pair_room))
                self.assertEqual(await harness.status(record.code), 404)
                burned = [l for l in harness.audit_lines() if l["kind"] == "code_burned"][-1]
                self.assertEqual((burned["reason"], burned["code_kind"]), ("revoked", "pair"))
                # A withdrawn code is still a code room: a late connection is answered here,
                # never handed to the Noise path.
                socket = await harness.raw(record.room)
                self.assertEqual(await meet_error(socket), "burned")
                await socket.close()
                self.assertIsNone(await harness.host.codes.revoke(record.code,
                                                                  meetcode.KIND_PAIR))
        run(main())

    def test_neither_surface_can_withdraw_the_others_code(self):
        """`code_revoke` is the sharing panel's, `pair_code_revoke` the pairing dialog's."""
        async def main():
            async with Harness() as harness:
                share = await harness.code()
                phone = await harness.pair_code()
                self.assertIsNone(await harness.host.codes.revoke(share.code,
                                                                  meetcode.KIND_PAIR))
                self.assertIsNone(await harness.host.codes.revoke(phone.code,
                                                                  meetcode.KIND_INVITE))
                self.assertEqual((share.state, phone.state), ("live", "live"))
                self.assertIsNotNone(await harness.host.codes.revoke(share.code,
                                                                     meetcode.KIND_INVITE))
                self.assertIsNotNone(await harness.host.codes.revoke(phone.code,
                                                                     meetcode.KIND_PAIR))
        run(main())

    def test_a_pairing_code_does_not_replace_a_pane_code_or_the_other_way_round(self):
        async def main():
            async with Harness() as harness:
                share = await harness.code("pane-1")
                phone = await harness.pair_code()
                self.assertEqual(share.state, "live", "a pairing code is about no pane")
                again = await harness.code("pane-1")
                self.assertEqual(share.state, "burned")
                self.assertEqual(phone.state, "live")
                self.assertEqual(again.state, "live")
        run(main())

    def test_an_unused_pairing_code_expires_and_takes_its_room(self):
        async def main():
            original = meetcode.CODE_LIFETIME
            meetcode.CODE_LIFETIME = 1.0
            try:
                async with Harness() as harness:
                    record = await harness.pair_code()
                    await harness.until(lambda: record.state == "expired", timeout=5)
                    self.assertIsNone(harness.host.rooms.get(record.pair_room))
                    self.assertEqual(await harness.status(record.code), 404)
                    expired = [l for l in harness.audit_lines()
                               if l["kind"] == "code_expired"][-1]
                    self.assertEqual(expired["code_kind"], "pair")
            finally:
                meetcode.CODE_LIFETIME = original
        run(main())


class TwoShapesTests(unittest.TestCase):
    """A pairing code and an invite code are not interchangeable (card #FR1C).

    The fragment decides, and it is refused twice: by the page it is handed to, and again by the
    desktop on whichever room it is offered on. The second refusal is the one that matters — a
    client is whatever the person on the other end wrote.
    """

    def test_a_pairing_fragment_delivered_to_join_is_refused(self):
        async def main():
            async with Harness() as harness:
                record = await harness.pair_code()
                phone = client_mod.Client(harness.base)
                url = await phone.join_with_code(record.code, record.pin,
                                                 app_base="https://app.example")
                await phone.close()
                as_invite = url.replace("/pair#", "/join#")
                self.assertEqual(pairing.fragment_kind(as_invite.split("#", 1)[1]), "pair",
                                 "the path is not what says which it is")
                # The join page would not get this far: there is no `i` to offer.
                with self.assertRaises(ValueError):
                    pairing.parse_invite_url(as_invite)
                # And a client that does not care: the pairing secret, knocked on the pairing
                # room. The desktop has no invite there and says so.
                link = pairing.parse_pair_url(url)
                impostor = client_mod.Client(harness.base)
                impostor.socket = await ws.connect(
                    harness.base.replace("http://", "ws://")
                    + f"/v1/connect?room={link['room']}")
                await impostor._handshake(link["desktop_public"])
                await impostor.send({"t": "knock", "invite": pairing.b64(link["secret"]),
                                     "name": "alice", "platform": "Chrome"})
                with self.assertRaises(wire.WireError) as caught:
                    await impostor.expect("knock_pending", timeout=10)
                self.assertEqual(caught.exception.code, "not_permitted")
                await impostor.close()
                self.assertEqual(harness.knocks, [], "the owner was never asked")
                self.assertEqual(harness.guests.live(), [])
                # The room is untouched: the phone this code was for can still pair.
                real = client_mod.Client(harness.base)
                paired = await real.pair(url, name="iPhone", platform="Safari")
                self.assertTrue(paired.device_id)
                await real.close()
        run(main())

    def test_an_invite_fragment_delivered_to_the_pairing_path_is_refused(self):
        async def main():
            async with Harness() as harness:
                record = await harness.code()
                guest = client_mod.Client(harness.base)
                url = await guest.join_with_code(record.code, record.pin,
                                                 app_base="https://app.example")
                await guest.close()
                self.assertTrue(url.startswith("https://app.example/join#"))
                as_pair = url.replace("/join#", "/pair#")
                self.assertEqual(pairing.fragment_kind(as_pair.split("#", 1)[1]), "invite")
                with self.assertRaises(ValueError):
                    pairing.parse_pair_url(as_pair)
                # The wire: the invite secret offered as a pairing secret on the invite's room.
                link = pairing.parse_invite_url(url)
                impostor = client_mod.Client(harness.base)
                impostor.socket = await ws.connect(
                    harness.base.replace("http://", "ws://")
                    + f"/v1/connect?room={link['room']}")
                await impostor._handshake(link["desktop_public"])
                await impostor.send({"t": "pair_prove", "secret": pairing.b64(link["secret"]),
                                     "name": "iPhone", "platform": "Safari"})
                with self.assertRaises(wire.WireError) as caught:
                    await impostor.expect("paired", timeout=10)
                self.assertEqual(caught.exception.code, "not_permitted")
                self.assertIn("not a pairing code", caught.exception.message)
                await impostor.close()
                self.assertEqual(harness.pair_asks, [], "the owner was never asked")
                self.assertEqual(harness.devices.live(), [])
        run(main())

    def test_the_reference_client_refuses_the_wrong_code_before_the_desktop_does(self):
        async def main():
            async with Harness() as harness:
                share = await harness.code()
                phone = client_mod.Client(harness.base)
                with self.assertRaises(wire.WireError) as caught:
                    await phone.pair_with_code(share.code, share.pin, name="iPhone",
                                               platform="Safari")
                self.assertEqual(caught.exception.code, "not_permitted")
                await phone.close()
                self.assertEqual(harness.devices.live(), [])
        run(main())

    def test_a_fragment_that_is_both_or_neither_belongs_to_no_page(self):
        secret = pairing.b64(bytes(16))
        desktop = pairing.b64(bytes(32))
        self.assertEqual(pairing.fragment_kind(
            f"v=1&d={desktop}&s={secret}&r=room1"), "pair")
        self.assertEqual(pairing.fragment_kind(
            f"#v=1&d={desktop}&i={secret}&r=room1"), "invite")
        for bad in (f"v=1&d={desktop}&s={secret}&i={secret}&r=room1",   # both doors
                    f"v=1&d={desktop}&r=room1",                          # neither
                    f"v=2&d={desktop}&s={secret}&r=room1",               # another version
                    f"v=1&s={secret}&r=room1",                           # no desktop key
                    f"v=1&d={desktop}&s={secret}",                       # no room
                    "", "not a fragment"):
            self.assertEqual(pairing.fragment_kind(bad), "", bad)
            with self.assertRaises(ValueError, msg=bad):
                pairing.fragment_url("https://app.example", bad)
        # Percent-encoded by a QR reader or a link handler, and still itself.
        self.assertEqual(pairing.fragment_kind(
            f"v=1%26d={desktop}%26s={secret}%26r=room1"), "pair")


if __name__ == "__main__":
    unittest.main()
