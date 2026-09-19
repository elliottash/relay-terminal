# SPDX-License-Identifier: GPL-3.0-or-later
"""Web Push, end to end (docs/REMOTE-PROTOCOL.md section 9).

Four things are worth testing here and each needs a different kind of check:

* **The cryptography is what a browser will open.** A round trip against ourselves proves nothing
  — an implementation can be consistently wrong, and this one was: the key derivation had
  ``as_public || ua_public`` where RFC 8291 says the other order, and the record carried no
  aes128gcm padding delimiter. Both are invisible to a self-test and fatal on a real phone. So the
  receiver here is written from the RFC rather than imported, and it is anchored by decrypting the
  RFC's own Appendix A vector before it is trusted to check ours.
* **The seal fails closed.** ``app/sw.js`` is run under Node against a body Python sealed, the way
  ``tests/noise_peer.mjs`` runs the browser's Noise against the desktop's, and a wrong key must
  show nothing rather than something.
* **The rendezvous never sees plaintext.** A real local push service takes a real delivery and the
  bytes it receives are inspected.
* **The hub's decisions.** Triggers, the presence rule, the per-pane cooldown, and — the one that
  matters most — what a body is allowed to say. The pane in ``BodyTests`` is titled ``ssh
  prod-db`` on purpose.
"""
import asyncio
import base64
import contextlib
import json
import os
import shutil
import ssl
import subprocess
import tempfile
import time
import unittest
import urllib.request
from pathlib import Path
from types import SimpleNamespace

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.kdf.hkdf import HKDF
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

from remote import client as client_mod
from remote import devtls, gui_host, host as host_mod, httpd, identity as identity_mod, noise, \
    notify, panes as panes_mod, push, wire
from rendezvous.server import Store, build
from tests.browser import Browser, find_chrome, shown

HERE = Path(__file__).resolve().parent
APP_DIR = HERE.parent / "app"


def run(coroutine, timeout=40):
    return asyncio.run(asyncio.wait_for(coroutine, timeout))


def un64(text: str) -> bytes:
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


# ---- an independent receiver ---------------------------------------------------------------------
# Written from RFC 8291 section 3.4 and RFC 8188 section 2, deliberately not sharing a line with
# remote/push.py. If both were wrong in the same way, the round trip below would still pass — which
# is why the Appendix A vector is decrypted with this and nothing else first.

def receive(ua_private: bytes, auth: bytes, record: bytes) -> bytes:
    salt, rs, idlen = record[:16], int.from_bytes(record[16:20], "big"), record[20]
    as_public = record[21:21 + idlen]
    ciphertext = record[21 + idlen:]
    assert rs >= 18, "rs must leave room for a record"
    ua = ec.derive_private_key(int.from_bytes(ua_private, "big"), ec.SECP256R1())
    ua_public = ua.public_key().public_bytes(Encoding.X962, PublicFormat.UncompressedPoint)
    ecdh = ua.exchange(ec.ECDH(),
                       ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), as_public))
    # PRK_key = HKDF-Extract(auth, ecdh); IKM = HKDF-Expand(PRK_key, key_info, 32). `cryptography`
    # does extract-then-expand in one call, so this pair of lines is exactly that pair of steps.
    key_info = b"WebPush: info\x00" + ua_public + as_public
    ikm = HKDF(algorithm=hashes.SHA256(), length=32, salt=auth, info=key_info).derive(ecdh)
    cek = HKDF(algorithm=hashes.SHA256(), length=16, salt=salt,
               info=b"Content-Encoding: aes128gcm\x00").derive(ikm)
    nonce = HKDF(algorithm=hashes.SHA256(), length=12, salt=salt,
                 info=b"Content-Encoding: nonce\x00").derive(ikm)
    padded = AESGCM(cek).decrypt(nonce, ciphertext, b"")
    end = len(padded)
    while end and padded[end - 1] == 0:
        end -= 1
    assert end and padded[end - 1] in (1, 2), "a record must end with a padding delimiter"
    return padded[:end - 1]


def subscription_keypair() -> tuple[bytes, bytes, bytes]:
    """What ``pushManager.subscribe`` hands a page: (private, p256dh, auth)."""
    key = ec.generate_private_key(ec.SECP256R1())
    private = key.private_numbers().private_value.to_bytes(32, "big")
    public = key.public_key().public_bytes(Encoding.X962, PublicFormat.UncompressedPoint)
    return private, public, bytes(range(16))


class VapidPersistenceTests(unittest.TestCase):
    """The local registry keeps one VAPID key across restarts (a phone subscribes with it)."""

    def test_the_same_store_file_gives_the_same_key_after_a_restart(self):
        import os, stat
        from rendezvous.server import Store
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "rendezvous.db"
            first = Store(path)
            _, public = first.vapid_pair()
            first.close()
            again = Store(path)
            self.assertEqual(again.vapid_pair()[1], public)
            self.assertEqual(stat.S_IMODE(os.stat(path).st_mode), 0o600)
            again.close()


class Rfc8291Tests(unittest.TestCase):
    # RFC 8291 Appendix A, "Push Message Encryption Example".
    PLAINTEXT = b"When I grow up, I want to be a watermelon"
    UA_PRIVATE = "q1dXpw3UpT5VOmu_cf_v6ih07Aems3njxI-JWgLcM94"
    AUTH = "BTBZMqHH6r4Tts7J_aSIgg"
    BODY = ("DGv6ra1nlYgDCS1FRnbzlwAAEABBBP4z9KsN6nGRTbVYI_c7VJSPQTBtkgcy27mlmlMoZIIgDll6e3vCYLoc"
            "InmYWAmS6TlzAC8wEqKK6PBru3jl7A_yl95bQpu6cVPTpK4Mqgkf1CXztLVBSt2Ks3oZwbuwXPXLWyouBWLV"
            "WGNWQexSgSxsj_Qulcy4a-fN")

    def test_the_rfcs_own_vector_decrypts(self):
        """Anchors the receiver below: it agrees with the RFC before it judges our sender."""
        self.assertEqual(receive(un64(self.UA_PRIVATE), un64(self.AUTH), un64(self.BODY)),
                         self.PLAINTEXT)

    def test_our_sender_agrees_with_the_rfcs_own_receiver(self):
        private, p256dh, auth = subscription_keypair()
        record = push.rfc8291_encrypt(p256dh, auth, b'{"kind":"agent_finished"}')
        self.assertEqual(receive(private, auth, record), b'{"kind":"agent_finished"}')

    def test_the_record_is_padded_the_way_rfc_8188_asks(self):
        """Without the 0x02 delimiter a browser reads the body a byte short, or not at all."""
        private, p256dh, auth = subscription_keypair()
        record = push.rfc8291_encrypt(p256dh, auth, b"hello")
        salt, idlen = record[:16], record[20]
        ua = ec.derive_private_key(int.from_bytes(private, "big"), ec.SECP256R1())
        ua_public = ua.public_key().public_bytes(Encoding.X962, PublicFormat.UncompressedPoint)
        as_public = record[21:21 + idlen]
        ecdh = ua.exchange(ec.ECDH(), ec.EllipticCurvePublicKey.from_encoded_point(
            ec.SECP256R1(), as_public))
        ikm = HKDF(algorithm=hashes.SHA256(), length=32, salt=auth,
                   info=b"WebPush: info\x00" + ua_public + as_public).derive(ecdh)
        cek = HKDF(algorithm=hashes.SHA256(), length=16, salt=salt,
                   info=b"Content-Encoding: aes128gcm\x00").derive(ikm)
        nonce = HKDF(algorithm=hashes.SHA256(), length=12, salt=salt,
                     info=b"Content-Encoding: nonce\x00").derive(ikm)
        self.assertEqual(AESGCM(cek).decrypt(nonce, record[21 + idlen:], b""), b"hello\x02")

    def test_our_own_receiver_round_trips(self):
        private, p256dh, auth = subscription_keypair()
        record = push.rfc8291_encrypt(p256dh, auth, b"payload")
        self.assertEqual(push.rfc8291_decrypt(private, auth, record), b"payload")

    def test_a_key_that_is_not_a_p256_point_is_refused(self):
        with self.assertRaises(ValueError):
            push.rfc8291_encrypt(b"\x02" + bytes(64), bytes(16), b"x")
        with self.assertRaises(ValueError):
            push.rfc8291_encrypt(bytes(10), bytes(16), b"x")

    def test_a_truncated_record_is_refused(self):
        with self.assertRaises(ValueError):
            push.rfc8291_decrypt(bytes(32), bytes(16), b"short")


class SealTests(unittest.TestCase):
    """The inner seal: the thing that makes a forged push from a compromised rendezvous useless."""

    def test_round_trip(self):
        key = bytes(range(32))
        body = {"v": 1, "kind": "password", "pane": "p1", "title": "Password prompt",
                "body": "Pane 1 · sudo"}
        self.assertEqual(push.open_sealed(key, push.seal(key, body)), body)

    def test_a_wrong_key_fails_closed(self):
        blob = push.seal(bytes(range(32)), {"kind": "password"})
        self.assertIsNone(push.open_sealed(bytes(32), blob))

    def test_a_tampered_blob_fails_closed(self):
        key = bytes(range(32))
        blob = bytearray(push.seal(key, {"kind": "agent_finished"}))
        blob[-1] ^= 0x01
        self.assertIsNone(push.open_sealed(key, bytes(blob)))
        self.assertIsNone(push.open_sealed(key, b""))
        self.assertIsNone(push.open_sealed(key, b"not a seal at all"))

    def test_the_nonce_is_fresh_every_time(self):
        key = bytes(range(32))
        first, second = push.seal(key, {"kind": "x"}), push.seal(key, {"kind": "x"})
        self.assertNotEqual(first[:12], second[:12])


@unittest.skipUnless(shutil.which("node"), "node is not installed")
class ServiceWorkerTests(unittest.TestCase):
    """app/sw.js opening what remote/push.py sealed, under Node."""

    def opened(self, key: bytes, blob: bytes) -> list[dict]:
        done = subprocess.run(
            [shutil.which("node"), str(HERE / "push_peer.mjs"),
             base64.b64encode(key).decode(), base64.b64encode(blob).decode()],
            capture_output=True, text=True, cwd=str(HERE.parent))
        self.assertEqual(done.returncode, 0, done.stderr)
        return json.loads(done.stdout)

    def test_the_worker_shows_what_the_desktop_sealed(self):
        key = bytes(range(32))
        body = {"v": 1, "kind": "waiting_input", "pane": "p1", "title": "Waiting for you",
                "body": "Pane 2 is waiting for input"}
        shown = self.opened(key, push.seal(key, body))
        self.assertEqual(len(shown), 1)
        self.assertEqual(shown[0]["title"], "Waiting for you")
        self.assertEqual(shown[0]["options"]["body"], "Pane 2 is waiting for input")
        self.assertEqual(shown[0]["options"]["tag"], "waiting_input")

    def test_a_push_the_worker_cannot_open_shows_nothing(self):
        blob = push.seal(bytes(range(32)), {"kind": "password", "title": "Password prompt",
                                            "body": "type your password"})
        self.assertEqual(self.opened(bytes(32), blob), [],
                         "a forged push must be discarded, not shown")


class VapidTests(unittest.TestCase):
    ENDPOINT = "https://push.example.com/v1/subscription/abc123"

    def test_the_jwt_verifies_for_that_audience(self):
        private, public = push.vapid_generate()
        header = push.vapid_authorization(private, self.ENDPOINT)
        self.assertTrue(header.startswith("vapid t="))
        self.assertTrue(push.vapid_verify(public, header, self.ENDPOINT))

    def test_the_shape_is_a_jose_es256_token(self):
        private, public = push.vapid_generate()
        header = push.vapid_authorization(private, self.ENDPOINT)
        fields = dict(pair.split("=", 1) for pair in header.split(" ", 1)[1].split(", "))
        self.assertEqual(un64(fields["k"]), public)
        head, claims, signature = fields["t"].split(".")
        self.assertEqual(json.loads(un64(head)), {"typ": "JWT", "alg": "ES256"})
        body = json.loads(un64(claims))
        self.assertEqual(body["aud"], "https://push.example.com")
        self.assertGreater(body["exp"], time.time())
        self.assertLessEqual(body["exp"], time.time() + 24 * 3600)
        self.assertTrue(body["sub"].startswith("mailto:"))
        self.assertEqual(len(un64(signature)), 64, "JOSE wants raw r||s, not DER")

    def test_another_key_does_not_verify(self):
        private, _ = push.vapid_generate()
        _, other_public = push.vapid_generate()
        header = push.vapid_authorization(private, self.ENDPOINT)
        self.assertFalse(push.vapid_verify(other_public, header, self.ENDPOINT))

    def test_a_token_for_one_service_is_not_good_for_another(self):
        private, public = push.vapid_generate()
        header = push.vapid_authorization(private, self.ENDPOINT)
        self.assertFalse(push.vapid_verify(public, header, "https://elsewhere.example/x"))

    def test_rubbish_does_not_verify(self):
        _, public = push.vapid_generate()
        for bad in ("", "vapid", "bearer t=x, k=y", "vapid t=a.b.c, k=zz"):
            self.assertFalse(push.vapid_verify(public, bad, self.ENDPOINT), bad)


# ---- the hub's decisions -------------------------------------------------------------------------

class Devices:
    """A DeviceStore in a temporary directory, with one subscribed phone."""

    def __init__(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.store = identity_mod.DeviceStore(Path(self.temporary.name))
        private, public = noise.generate_keypair()
        self.device = self.store.pair(public, "Pixel 9", "Chrome", wire.AGENT)
        self.ua_private, self.p256dh, self.auth = subscription_keypair()
        self.seal_key = bytes(range(32, 64))
        self.wants(*notify.KINDS)

    def wants(self, *kinds: str) -> None:
        """What this phone asked to be told about (`push_subscribe`'s `kinds`)."""
        self.store.set_push(self.device.device_id, {
            "endpoint": "https://push.example.com/v1/subscription/abc",
            "p256dh": push.b64url(self.p256dh), "auth": push.b64url(self.auth),
            "key": push.b64url(self.seal_key), "kinds": list(kinds)})

    def opened(self, payload: bytes) -> dict:
        """What the phone would show: through RFC 8291, then through the seal."""
        return push.open_sealed(self.seal_key, receive(self.ua_private, self.auth, payload))

    def close(self):
        self.temporary.cleanup()


class NotifierHarness:
    def __init__(self, reply=None):
        self.devices = Devices()
        self.sent: list[tuple[str, bytes]] = []
        self.reply = reply or {"delivered": True, "status": 201, "drop": False}
        self.tasks: list = []
        self.notifier = notify.Notifier(self.devices.store, self.send,
                                        spawn=lambda coroutine: self.tasks.append(
                                            asyncio.ensure_future(coroutine)))

    async def send(self, endpoint: str, payload: bytes, origin: str = "") -> dict:
        self.sent.append((endpoint, payload))
        return self.reply

    async def settle(self):
        while self.tasks:
            await asyncio.gather(*self.tasks)
            self.tasks = [task for task in self.tasks if not task.done()]

    def bodies(self) -> list[dict]:
        return [self.devices.opened(payload) for _, payload in self.sent]


def pane(status="idle", **extra):
    item = {"id": "p1", "window": 1, "tab": "relay-terminal", "title": "ssh prod-db",
            "cwd": "/home/elliott/repos/relay-terminal", "program": "ssh", "control": "human",
            "status": status, "unread": 0, "queue": 0, "updated": time.time()}
    item.update(extra)
    return item


class TriggerTests(unittest.TestCase):
    """Which events are worth a buzz (section 9), against the events remote/wire.py forwards."""

    def fire(self, work):
        async def main():
            harness = NotifierHarness()
            try:
                await work(harness)
                await harness.settle()
                return harness
            finally:
                harness.devices.close()
        return run(main())

    def test_a_long_turn_notifies_and_a_short_one_does_not(self):
        async def work(harness):
            harness.notifier.on_agent("p1", {"event": "agent_started"})
            harness.notifier._turn_started["p1"] -= notify.LONG_TURN + 1
            harness.notifier.on_agent("p1", {"event": "agent_finished", "turn_id": "t1"})
        harness = self.fire(work)
        self.assertEqual([body["kind"] for body in harness.bodies()], ["agent_finished"])

        async def quick(harness):
            harness.notifier.on_agent("p1", {"event": "agent_started"})
            harness.notifier.on_agent("p1", {"event": "agent_finished", "turn_id": "t1"})
        self.assertEqual(self.fire(quick).sent, [], "a turn you waited out is not news")

    def test_a_finish_with_no_start_is_not_a_notification(self):
        async def work(harness):
            harness.notifier.on_agent("p1", {"event": "agent_finished"})
        self.assertEqual(self.fire(work).sent, [])

    def test_a_failed_turn_notifies(self):
        async def work(harness):
            harness.notifier.on_agent("p1", {"event": "error", "message": "provider refused"})
        harness = self.fire(work)
        self.assertEqual([body["kind"] for body in harness.bodies()], ["failed"])

    def test_a_plan_notifies(self):
        async def work(harness):
            harness.notifier.on_agent("p1", {"event": "plan_written", "plan_id": "abc",
                                             "title": "Fix the router"})
        harness = self.fire(work)
        self.assertEqual([body["kind"] for body in harness.bodies()], ["plan"])

    def test_waiting_for_input_notifies_on_the_transition(self):
        async def work(harness):
            harness.notifier.on_panes([pane("running")])
            harness.notifier.on_panes([pane("waiting_input")])
            harness.notifier.on_panes([pane("waiting_input")])   # unchanged: not a second buzz
        harness = self.fire(work)
        self.assertEqual([body["kind"] for body in harness.bodies()], ["waiting_input"])

    def test_the_first_sighting_of_a_pane_is_not_a_trigger(self):
        async def work(harness):
            harness.notifier.on_panes([pane("waiting_input")])
        self.assertEqual(self.fire(work).sent, [],
                         "a pane that was already waiting when sharing started is not news")

    def test_a_password_prompt_notifies_and_names_the_program(self):
        async def work(harness):
            harness.notifier.on_panes([pane("running", program="sudo")])
            harness.notifier.on_panes([pane("password", program="sudo")])
        body = self.fire(work).bodies()[0]
        self.assertEqual(body["kind"], "password")
        self.assertIn("sudo", body["body"])

    def test_a_prompt_from_a_program_the_agent_spawned_is_suppressed(self):
        """Section 9: any program can print `[sudo] password for …`, and one the agent started
        must not be able to put a credential prompt on the owner's phone out of context."""
        async def work(harness):
            harness.notifier.on_panes([pane("running", control="agent")])
            harness.notifier.on_panes([pane("password", control="agent", program="curl")])
        self.assertEqual(self.fire(work).sent, [])

    def test_an_event_nobody_made_a_trigger_is_quiet(self):
        async def work(harness):
            for name in ("delta", "tool_started", "thinking_delta", "status", "queued",
                         "turn_summary", "board_changed"):
                harness.notifier.on_agent("p1", {"event": name})
        self.assertEqual(self.fire(work).sent, [])


class PresenceAndCooldownTests(unittest.TestCase):
    def test_nothing_is_pushed_while_the_desktop_window_is_focused(self):
        async def main():
            harness = NotifierHarness()
            try:
                harness.notifier.window_active(True)
                harness.notifier.on_panes([pane("running")])
                harness.notifier.on_panes([pane("waiting_input")])
                await harness.settle()
                self.assertEqual(harness.sent, [], "you are already looking at it")
                # Walk away, and the next thing that happens does reach the phone.
                harness.notifier.window_active(False)
                harness.notifier.on_panes([pane("failed")])
                await harness.settle()
                self.assertEqual(len(harness.sent), 1)
            finally:
                harness.devices.close()
        run(main())

    def test_one_pane_may_ring_once_per_cooldown(self):
        async def main():
            harness = NotifierHarness()
            try:
                for _ in range(5):
                    harness.notifier.on_panes([pane("running", program="sudo")])
                    harness.notifier.on_panes([pane("password", program="sudo")])
                await harness.settle()
                self.assertEqual(len(harness.sent), 1, "a prompt loop must not spam the phone")
                # Once the cooldown is behind us, the pane may speak again.
                harness.notifier._last_push["p1"] -= notify.PANE_COOLDOWN + 1
                harness.notifier.on_panes([pane("running", program="sudo")])
                harness.notifier.on_panes([pane("password", program="sudo")])
                await harness.settle()
                self.assertEqual(len(harness.sent), 2)
            finally:
                harness.devices.close()
        run(main())

    def test_the_cooldown_is_per_pane(self):
        async def main():
            harness = NotifierHarness()
            try:
                harness.notifier.on_panes([pane("running"), pane("running", id="p2")])
                harness.notifier.on_panes([pane("waiting_input"),
                                           pane("waiting_input", id="p2")])
                await harness.settle()
                self.assertEqual(len(harness.sent), 2)
            finally:
                harness.devices.close()
        run(main())

    def test_a_device_with_no_subscription_is_not_pushed_to(self):
        async def main():
            harness = NotifierHarness()
            try:
                harness.devices.store.set_push(harness.devices.device.device_id, None)
                harness.notifier.on_panes([pane("running")])
                harness.notifier.on_panes([pane("failed")])
                await harness.settle()
                self.assertEqual(harness.sent, [])
            finally:
                harness.devices.close()
        run(main())


class ChosenKindsTests(unittest.TestCase):
    """Which notifications a phone gets is the phone's choice, kept on its device record."""

    def test_a_kind_this_device_switched_off_is_never_pushed(self):
        async def main():
            harness = NotifierHarness()
            try:
                harness.devices.wants("password")          # only the prompt is wanted
                harness.notifier.on_panes([pane("running")])
                harness.notifier.on_panes([pane("waiting_input")])
                harness.notifier.on_agent("p1", {"event": "plan_written"})
                await harness.settle()
                self.assertEqual(harness.sent, [], "this phone asked for prompts only")

                harness.notifier.on_panes([pane("running", program="sudo")])
                harness.notifier.on_panes([pane("password", program="sudo")])
                await harness.settle()
                self.assertEqual([body["kind"] for body in harness.bodies()], ["password"])
            finally:
                harness.devices.close()
        run(main())

    def test_a_kind_nobody_wants_does_not_spend_the_cooldown(self):
        """Otherwise one device's switched-off kind would silence the next real one for a minute."""
        async def main():
            harness = NotifierHarness()
            try:
                harness.devices.wants("failed")
                harness.notifier.on_panes([pane("running")])
                harness.notifier.on_panes([pane("waiting_input")])   # wanted by nobody
                harness.notifier.on_panes([pane("failed")])          # must still arrive
                await harness.settle()
                self.assertEqual([body["kind"] for body in harness.bodies()], ["failed"])
            finally:
                harness.devices.close()
        run(main())

    def test_a_record_written_before_kinds_existed_gets_all_of_them(self):
        devices = Devices()
        try:
            stored = dict(devices.store.devices[devices.device.device_id].push)
            stored.pop("kinds")
            devices.store.set_push(devices.device.device_id, stored)
            self.assertEqual(notify.kinds_of(stored), list(notify.KINDS))
        finally:
            devices.close()


class BodyTests(unittest.TestCase):
    """What a notification may say. The pane is called `ssh prod-db` and sits in a private path."""

    SECRETS = ("ssh prod-db", "prod-db", "/home/elliott", "repos/relay-terminal", "psql",
               "relay-terminal")

    def bodies_for(self, work) -> list[dict]:
        async def main():
            harness = NotifierHarness()
            try:
                work(harness.notifier)
                await harness.settle()
                return harness.bodies()
            finally:
                harness.devices.close()
        return run(main())

    def test_no_title_no_cwd_no_command_text_reaches_the_phone(self):
        def work(notifier):
            notifier.on_panes([pane("running", program="ssh")])
            notifier.on_panes([pane("waiting_input", program="ssh")])
        bodies = self.bodies_for(work)
        self.assertEqual(len(bodies), 1)
        text = json.dumps(bodies[0])
        for secret in self.SECRETS:
            self.assertNotIn(secret, text, f"{secret!r} must never reach a lock screen")
        self.assertEqual(bodies[0]["body"], "Pane 1 is waiting for input")

    def test_the_password_body_names_the_program_and_nothing_else(self):
        def work(notifier):
            notifier.on_panes([pane("running", program="sudo")])
            notifier.on_panes([pane("password", program="sudo")])
        body = self.bodies_for(work)[0]
        self.assertEqual(body["title"], "Password prompt")
        self.assertEqual(body["body"], "Pane 1 · sudo")
        for secret in ("prod-db", "/home/elliott"):
            self.assertNotIn(secret, json.dumps(body))

    def test_a_program_name_that_tries_to_be_a_sentence_is_stripped(self):
        def work(notifier):
            notifier.on_panes([pane("running")])
            notifier.on_panes([pane("password", program="sudo\nEnter your bank password")])
        body = self.bodies_for(work)[0]
        self.assertNotIn("\n", body["body"])
        self.assertLessEqual(len(body["body"]), 40)

    def test_the_label_is_an_ordinal_this_desktop_assigned(self):
        notifier = notify.Notifier(None, None, spawn=lambda coroutine: coroutine.close())
        self.assertEqual(notifier.label("pane-9ab3"), "Pane 1")
        self.assertEqual(notifier.label("pane-7cd1"), "Pane 2")
        self.assertEqual(notifier.label("pane-9ab3"), "Pane 1", "a label must not move")

    def test_every_body_carries_a_kind_a_pane_and_a_version(self):
        notifier = notify.Notifier(None, None, spawn=lambda coroutine: coroutine.close())
        for kind in ("agent_finished", "waiting_input", "password", "failed", "plan"):
            body = notifier.body(kind, "p1", spent=91.0, program="sudo")
            self.assertEqual(body["v"], 1)
            self.assertEqual(body["kind"], kind)
            self.assertEqual(body["pane"], "p1")
            self.assertTrue(body["title"] and body["body"])
        self.assertEqual(notifier.body("agent_finished", "p1", spent=91.0)["body"], "Pane 1 · 1m 31s")


class DroppedSubscriptionTests(unittest.TestCase):
    def test_a_410_from_the_push_service_drops_the_subscription(self):
        async def main():
            harness = NotifierHarness(reply={"delivered": False, "status": 410, "drop": True})
            try:
                harness.notifier.on_panes([pane("running")])
                harness.notifier.on_panes([pane("failed")])
                await harness.settle()
                self.assertEqual(len(harness.sent), 1)
                self.assertIsNone(harness.devices.store.devices[
                    harness.devices.device.device_id].push)
            finally:
                harness.devices.close()
        run(main())

    def test_a_delivery_that_merely_failed_keeps_the_subscription(self):
        async def main():
            harness = NotifierHarness(reply={"delivered": False, "status": None, "drop": False})
            try:
                harness.notifier.on_panes([pane("running")])
                harness.notifier.on_panes([pane("failed")])
                await harness.settle()
                self.assertIsNotNone(harness.devices.store.devices[
                    harness.devices.device.device_id].push)
            finally:
                harness.devices.close()
        run(main())

    def test_revoking_a_device_drops_its_subscription(self):
        devices = Devices()
        try:
            self.assertIsNotNone(devices.device.push)
            devices.store.revoke(devices.device.device_id)
            self.assertIsNone(devices.store.devices[devices.device.device_id].push)
        finally:
            devices.close()


class SubscriptionValidationTests(unittest.TestCase):
    def good(self, **extra) -> dict:
        _, p256dh, auth = subscription_keypair()
        message = {"endpoint": "https://push.example.com/v1/subscription/abc",
                   "p256dh": push.b64url(p256dh), "auth": push.b64url(auth),
                   "key": push.b64url(bytes(32))}
        message.update(extra)
        return message

    def test_a_good_subscription_is_stored_as_base64url(self):
        stored = notify.clean_subscription(self.good())
        self.assertEqual(set(stored), {"endpoint", "p256dh", "auth", "key", "kinds"})
        self.assertEqual(len(un64(stored["p256dh"])), 65)
        self.assertEqual(len(un64(stored["auth"])), 16)
        self.assertEqual(len(un64(stored["key"])), 32)

    def test_the_endpoint_must_be_https_and_sane(self):
        for endpoint in ("http://push.example.com/x", "ftp://x", "", "push.example.com",
                         "https://x/" + "a" * 4000, "https://x/ y"):
            with self.assertRaises(wire.WireError, msg=endpoint):
                notify.clean_subscription(self.good(endpoint=endpoint))

    def test_the_keys_must_be_the_right_size(self):
        for field, value in (("p256dh", push.b64url(bytes(64))),
                             ("p256dh", push.b64url(b"\x03" + bytes(64))),
                             ("auth", push.b64url(bytes(15))),
                             ("auth", push.b64url(bytes(32))),
                             ("key", push.b64url(bytes(16))),
                             ("key", push.b64url(bytes(33)))):
            with self.assertRaises(wire.WireError, msg=f"{field}={value}"):
                notify.clean_subscription(self.good(**{field: value}))

    def test_the_kinds_default_to_all_five(self):
        for value in (None, []):
            stored = notify.clean_subscription(self.good(kinds=value))
            self.assertEqual(stored["kinds"], list(notify.KINDS), value)
        # A client that predates `kinds` sends none at all, and must still be notified.
        message = self.good()
        message.pop("kinds", None)
        self.assertEqual(notify.clean_subscription(message)["kinds"], list(notify.KINDS))

    def test_the_kinds_are_stored_in_one_order_and_without_repeats(self):
        stored = notify.clean_subscription(self.good(kinds=["plan", "password", "plan"]))
        self.assertEqual(stored["kinds"], ["password", "plan"])

    def test_a_kind_nobody_implemented_is_refused(self):
        for kinds in (["agent_finished", "command_finished"], ["subagent_finished"], ["PASSWORD"],
                      [""], [7], "password", {"password": True}, list(notify.KINDS) + ["plan2"]):
            with self.assertRaises(wire.WireError, msg=kinds):
                notify.clean_subscription(self.good(kinds=kinds))

    def test_rubbish_is_refused(self):
        for field in ("p256dh", "auth", "key"):
            for value in (None, 7, "not base64!", "*" * 8):
                with self.assertRaises(wire.WireError, msg=f"{field}={value}"):
                    notify.clean_subscription(self.good(**{field: value}))


# ---- the wire ------------------------------------------------------------------------------------

class Harness:
    """A rendezvous, a desktop hub and a paired client, all over real sockets."""

    def __init__(self, capability=wire.VIEW):
        self.capability = capability

    async def __aenter__(self):
        self.temporary = tempfile.TemporaryDirectory()
        directory = Path(self.temporary.name)
        self.store = Store(":memory:")
        self.server = build(self.store, static_root=APP_DIR)
        await self.server.start("127.0.0.1", 0)
        self.base = f"http://127.0.0.1:{self.server.port}"
        self.identity = identity_mod.Identity.create(directory)
        self.devices = identity_mod.DeviceStore(directory)
        self.source = panes_mod.DemoPaneSource()

        async def approver(request):
            return True, self.capability

        # app_base is where the pairing link points: this server, so a browser can follow it.
        self.host = host_mod.Host(self.identity, self.devices, self.source,
                                  app_base=self.base, approver=approver, name="test desktop")
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

    async def pair(self):
        url, _ = await self.host.open_pairing()
        client = client_mod.Client(self.base)
        paired = await client.pair(url, name="Pixel 9", platform="Chrome")
        return client, paired


class SubscribeOverTheWireTests(unittest.TestCase):
    def subscription(self) -> dict:
        _, p256dh, auth = subscription_keypair()
        return {"t": "push_subscribe", "endpoint": "https://push.example.com/v1/subscription/abc",
                "p256dh": push.b64url(p256dh), "auth": push.b64url(auth),
                "key": push.b64url(bytes(range(32)))}

    def test_a_view_device_may_subscribe_and_unsubscribe(self):
        async def main():
            async with Harness(wire.VIEW) as harness:
                client, paired = await harness.pair()
                await client.send(self.subscription())
                state = await client.expect("push_state")
                self.assertTrue(state["subscribed"])
                stored = harness.devices.get(paired.device_id).push
                self.assertEqual(stored["endpoint"], "https://push.example.com/v1/subscription/abc")

                await client.send({"t": "push_unsubscribe"})
                state = await client.expect("push_state")
                self.assertFalse(state["subscribed"])
                self.assertIsNone(harness.devices.get(paired.device_id).push)
                await client.close()
        run(main())

    def test_push_state_says_which_kinds_are_stored(self):
        async def main():
            async with Harness() as harness:
                client, paired = await harness.pair()
                await client.send({**self.subscription(), "kinds": ["password", "failed"]})
                state = await client.expect("push_state")
                self.assertEqual(state["kinds"], ["password", "failed"])
                self.assertEqual(harness.devices.get(paired.device_id).push["kinds"],
                                 ["password", "failed"])

                # Sending it again replaces the list; the browser is never re-prompted.
                await client.send({**self.subscription(), "kinds": ["plan"]})
                state = await client.expect("push_state")
                self.assertEqual(state["kinds"], ["plan"])
                self.assertEqual(harness.devices.get(paired.device_id).push["kinds"], ["plan"])

                # And with no kinds at all it is all five again.
                await client.send(self.subscription())
                state = await client.expect("push_state")
                self.assertEqual(state["kinds"], list(notify.KINDS))
                await client.close()
        run(main())

    def test_a_kind_nobody_implemented_is_refused_and_the_old_list_stands(self):
        async def main():
            async with Harness() as harness:
                client, paired = await harness.pair()
                await client.send({**self.subscription(), "kinds": ["plan"]})
                await client.expect("push_state")
                await client.send({**self.subscription(), "kinds": ["plan", "command_finished"]})
                error = await client.expect("error")
                self.assertEqual(error["code"], "unknown_type")
                self.assertIn("command_finished", error["message"])
                self.assertEqual(harness.devices.get(paired.device_id).push["kinds"], ["plan"])
                await client.close()
        run(main())

    def test_a_bad_subscription_is_refused_and_nothing_is_stored(self):
        async def main():
            async with Harness() as harness:
                client, paired = await harness.pair()
                bad = self.subscription()
                bad["endpoint"] = "http://push.example.com/x"
                await client.send(bad)
                error = await client.expect("error")
                self.assertEqual(error["code"], "unknown_type")
                self.assertIsNone(harness.devices.get(paired.device_id).push)
                await client.close()
        run(main())

    def test_revoking_a_subscribed_device_forgets_the_subscription(self):
        async def main():
            async with Harness() as harness:
                client, paired = await harness.pair()
                await client.send(self.subscription())
                await client.expect("push_state")
                harness.devices.revoke(paired.device_id)
                self.assertIsNone(harness.devices.devices[paired.device_id].push)
                await client.close()
        run(main())

    def test_the_subscription_never_reaches_the_rendezvous(self):
        async def main():
            async with Harness() as harness:
                client, _ = await harness.pair()
                message = self.subscription()
                await client.send(message)
                await client.expect("push_state")
                rows = harness.store.db.execute("SELECT detail FROM events").fetchall()
                blob = json.dumps([row["detail"] for row in rows])
                self.assertNotIn(message["endpoint"], blob)
                self.assertNotIn(message["p256dh"], blob)
                self.assertNotIn(message["key"], blob)
                await client.close()
        run(main())


# ---- a real delivery ------------------------------------------------------------------------------

class FakePushService:
    """A local stand-in for FCM: it takes the POST and keeps what it was given."""

    def __init__(self, directory: Path, status: int = 201):
        self.directory = directory
        self.status = status
        self.taken: list[tuple[dict, bytes]] = []

    async def __aenter__(self):
        self.server = httpd.Server()

        @self.server.route("POST", "/subscription/one")
        async def take(request, body):
            self.taken.append((dict(request.headers), body))
            if self.status >= 400:
                return httpd.Response.error(self.status, "gone")
            return httpd.Response(status=self.status, body=b"", content_type="text/plain")

        await self.server.start("127.0.0.1", 0, ssl_context=devtls.context(self.directory))
        # The rendezvous posts only to a Web Push service (rendezvous/server.py), which a
        # loopback address is not: `RELAY_PUSH_HOSTS` is the hook a self-hoster with their own
        # push service uses, and it is what makes this stand-in reachable.
        self._hosts = os.environ.get("RELAY_PUSH_HOSTS")
        os.environ["RELAY_PUSH_HOSTS"] = "127.0.0.1"
        self.endpoint = f"https://127.0.0.1:{self.server.port}/subscription/one"
        # The certificate is the dev one, made for a LAN address, so the delivery side of this
        # process is told not to check it: what is under test is the bytes, not the PKI. urllib
        # caches its default opener, so replacing the opener is the only thing that takes effect
        # in a process that has already made a request.
        urllib.request.install_opener(urllib.request.build_opener(
            urllib.request.HTTPSHandler(context=ssl._create_unverified_context())))
        return self

    async def __aexit__(self, *exc):
        urllib.request.install_opener(None)
        if self._hosts is None:
            os.environ.pop("RELAY_PUSH_HOSTS", None)
        else:
            os.environ["RELAY_PUSH_HOSTS"] = self._hosts
        await self.server.close()


class DeliveryTests(unittest.TestCase):
    """The whole path: hub → rendezvous → push service, with nothing readable in the middle."""

    def test_a_push_is_delivered_and_the_rendezvous_can_read_none_of_it(self):
        async def main():
            async with Harness() as harness:
                with tempfile.TemporaryDirectory() as certs:
                    async with FakePushService(Path(certs)) as service:
                        ua_private, p256dh, auth = subscription_keypair()
                        seal_key = bytes(range(32))
                        body = {"v": 1, "kind": "password", "pane": "p1",
                                "title": "Password prompt", "body": "Pane 1 · sudo"}
                        payload = push.rfc8291_encrypt(p256dh, auth, push.seal(seal_key, body))
                        reply = await harness.host.push_send(service.endpoint, payload)
                        self.assertTrue(reply["delivered"], reply)
                        self.assertFalse(reply["drop"])

                        headers, received = service.taken[0]
                        self.assertEqual(received, payload, "the payload must arrive unchanged")
                        # What the push service holds is opaque to it and to the rendezvous.
                        self.assertNotIn(b"Pane 1", received)
                        self.assertNotIn(b"sudo", received)
                        self.assertNotIn(b"password", received.lower())
                        # ... and it really is the notification, to a phone holding the keys.
                        opened = push.open_sealed(seal_key, receive(ua_private, auth, received))
                        self.assertEqual(opened, body)

                        # VAPID: signed by the rendezvous, for this endpoint's origin.
                        lower = {name.lower(): value for name, value in headers.items()}
                        self.assertTrue(push.vapid_verify(harness.store.vapid_pair()[1],
                                                          lower["authorization"], service.endpoint))
                        self.assertEqual(lower["ttl"], str(push.PUSH_TTL))
                        self.assertEqual(lower["content-type"], "application/octet-stream")

                        # The rendezvous kept a count, not a copy.
                        rows = harness.store.db.execute(
                            "SELECT kind, detail FROM events WHERE kind = 'push'").fetchall()
                        self.assertTrue(rows)
                        self.assertTrue(all(row["detail"] is None for row in rows))
        run(main())

    def test_a_410_comes_back_as_drop(self):
        async def main():
            async with Harness() as harness:
                with tempfile.TemporaryDirectory() as certs:
                    async with FakePushService(Path(certs), status=410) as service:
                        _, p256dh, auth = subscription_keypair()
                        payload = push.rfc8291_encrypt(p256dh, auth, push.seal(bytes(32), {"v": 1}))
                        reply = await harness.host.push_send(service.endpoint, payload)
                        self.assertFalse(reply["delivered"])
                        self.assertEqual(reply["status"], 410)
                        self.assertTrue(reply["drop"])
        run(main())

    def test_the_rendezvous_refuses_an_endpoint_that_is_not_https(self):
        async def main():
            async with Harness() as harness:
                with self.assertRaises(wire.WireError) as caught:
                    await harness.host.push_send("http://push.example.com/x", b"payload")
                self.assertIn("400", str(caught.exception))
        run(main())


class SidecarPresenceTests(unittest.TestCase):
    """The GUI's `window_active` line reaching the hub (remote/gui_host.py).

    `src/RemoteShare.cpp` sends it on `QGuiApplication::applicationStateChanged` and once when the
    sidecar starts; this is the other end of that line.
    """

    def test_the_window_state_is_remembered_and_then_applied(self):
        async def main():
            sidecar = gui_host.Sidecar()
            self.assertFalse(sidecar.window_active, "a hub nobody told pushes; that is the default")
            await sidecar.handle({"t": "window_active", "active": True})
            self.assertTrue(sidecar.window_active, "remembered until there is a hub to tell")

            devices = Devices()
            try:
                notifier = notify.Notifier(devices.store, None,
                                           spawn=lambda coroutine: coroutine.close())
                # The line goes to the hub, which passes it on to the notifier and reads it
                # itself for "guests can act only while I am present" (section 10.5): one signal,
                # two readers, so the double routes it exactly as `Host.window_active` does.
                sidecar.host = SimpleNamespace(notifier=notifier,
                                               window_active=notifier.window_active)
                await sidecar.handle({"t": "window_active", "active": False})
                self.assertFalse(notifier.active)
                await sidecar.handle({"t": "window_active", "active": True})
                self.assertTrue(notifier.active)
            finally:
                devices.close()
        run(main())


@unittest.skipUnless(find_chrome(), "no Chrome or Chromium installed")
class NotifyRowTests(unittest.TestCase):
    """The row itself, in a real browser: it renders, and asking happens from a tap."""

    def test_the_inbox_offers_to_notify_this_phone(self):
        async def main():
            async with Harness() as harness:
                url, _ = await harness.host.open_pairing()
                browser = Browser()
                await browser.start()
                try:
                    await browser.navigate(url)
                    await browser.wait_for(shown('screen-inbox'), timeout=40)
                    state = await browser.wait_for("""
                        (() => {
                          const button = document.getElementById('notify');
                          if (!button) return null;
                          return {
                            text: button.textContent,
                            hidden: button.hidden,
                            note: document.getElementById('notify-note').textContent,
                            usable: 'serviceWorker' in navigator && 'PushManager' in window
                              && 'Notification' in window,
                            permission: window.Notification ? Notification.permission : 'none',
                          };
                        })()
                    """, timeout=20)
                    self.assertIsNotNone(state, "the inbox has no Notify row")
                    if state["usable"]:
                        # Nothing was asked on load: permission is still the browser's default.
                        self.assertEqual(state["permission"], "default")
                        self.assertFalse(state["hidden"])
                        self.assertEqual(state["text"], "Notify me on this phone")
                        # Nothing to choose between until there is something to be notified about.
                        self.assertTrue(await browser.evaluate(
                            "document.getElementById('notify-kinds').hidden"))
                    else:
                        self.assertTrue(state["hidden"])
                        self.assertIn("notification", state["note"].lower())
                finally:
                    await browser.stop()
        run(main(), timeout=120)

    def test_the_checkboxes_are_this_phones_choice_and_survive_a_reload(self):
        """The whole loop in a browser: subscribe, uncheck one, reload, and it is still unchecked.

        Skipped where the machine cannot reach a push service, because `pushManager.subscribe`
        really does talk to one — and takes its time about it, which is why the waits here are
        measured in tens of seconds rather than the usual two.
        """
        async def main():
            async with Harness() as harness:
                url, _ = await harness.host.open_pairing()
                browser = Browser()
                await browser.start()
                try:
                    # Permission is a browser gesture we cannot make; granting it up front is the
                    # only part of this the test fakes.
                    await browser.call("Browser.grantPermissions",
                                       {"origin": harness.base, "permissions": ["notifications"]})
                    await browser.navigate(url)
                    await browser.wait_for(shown('screen-inbox'), timeout=40)
                    device = harness.devices.live()[0]

                    await browser.evaluate("document.getElementById('notify').click()")
                    for _ in range(400):                 # FCM registration takes ~35 s here
                        await asyncio.sleep(0.2)
                        if device.push:
                            break
                    if not device.push:
                        note = await browser.evaluate(
                            "document.getElementById('notify-note').textContent")
                        raise unittest.SkipTest(f"no push service reachable here ({note!r})")

                    # Everything on to begin with, and the boxes say so.
                    self.assertEqual(device.push["kinds"], list(notify.KINDS))
                    checked = await browser.wait_for("""
                        (() => {
                          const boxes = [...document.querySelectorAll('#notify-kinds input')];
                          return boxes.length ? boxes.map(b => [b.id, b.checked]) : null;
                        })()
                    """, timeout=20)
                    self.assertEqual(len(checked), len(notify.KINDS))
                    self.assertTrue(all(state for _, state in checked))

                    # Uncheck one: the desktop's record loses exactly that kind.
                    await browser.evaluate(
                        "document.getElementById('notify-kind-agent_finished').click()")
                    for _ in range(150):
                        await asyncio.sleep(0.2)
                        if "agent_finished" not in device.push["kinds"]:
                            break
                    self.assertEqual(device.push["kinds"],
                                     [kind for kind in notify.KINDS if kind != "agent_finished"])

                    # Reload: the boxes are drawn from what was stored, not from the defaults.
                    await browser.navigate(harness.base)
                    await browser.wait_for(shown('screen-inbox'), timeout=40)
                    again = await browser.wait_for("""
                        (() => {
                          const box = document.getElementById('notify-kind-agent_finished');
                          return box ? { off: !box.checked,
                                         on: document.getElementById('notify-kind-password').checked }
                                     : null;
                        })()
                    """, timeout=20)
                    self.assertTrue(again["off"], "the unchecked box came back checked")
                    self.assertTrue(again["on"])
                finally:
                    await browser.stop()
        run(main(), timeout=240)


class VapidKeyRouteTests(unittest.TestCase):
    def test_the_public_key_is_served_and_is_stable(self):
        async def main():
            async with Harness() as harness:
                import urllib.request

                def get():
                    with urllib.request.urlopen(f"{harness.base}/v1/push/key", timeout=10) as reply:
                        return json.loads(reply.read()), reply.headers
                first, headers = await asyncio.to_thread(get)
                second, _ = await asyncio.to_thread(get)
                self.assertEqual(first["vapid"], second["vapid"])
                self.assertEqual(len(un64(first["vapid"])), 65)
                self.assertEqual(un64(first["vapid"]), harness.store.vapid_pair()[1])
                self.assertEqual(headers.get("Access-Control-Allow-Origin"), "*")
        run(main())


if __name__ == "__main__":
    unittest.main()
