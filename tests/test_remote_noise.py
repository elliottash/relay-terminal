# SPDX-License-Identifier: AGPL-3.0-or-later
"""Noise_IK_25519_AESGCM_SHA256 (docs/REMOTE-PROTOCOL.md section 4).

The cross-implementation half of these tests drives app/noise.js under Node against
remote/noise.py, which is the only way to know the browser and the desktop agree. It is skipped
when Node is missing so the suite still runs everywhere.
"""
import base64
import json
import shutil
import subprocess
import unittest
from pathlib import Path

from remote import noise

HERE = Path(__file__).resolve().parent
NODE = shutil.which("node")


def b64(data: bytes) -> str:
    return base64.b64encode(data).decode()


def un64(text: str) -> bytes:
    return base64.b64decode(text)


class NoiseTests(unittest.TestCase):
    def test_handshake_round_trip(self):
        client_s, client_p = noise.generate_keypair()
        desktop_s, desktop_p = noise.generate_keypair()
        client = noise.Initiator(client_s, desktop_p)
        desktop = noise.Responder(desktop_s)

        self.assertEqual(desktop.read_message_1(client.write_message_1(b"hello")), b"hello")
        self.assertEqual(desktop.client_static, client_p)
        message2, desktop_session = desktop.write_message_2(b"welcome")
        payload, client_session = client.read_message_2(message2)

        self.assertEqual(payload, b"welcome")
        self.assertEqual(client_session.handshake_hash, desktop_session.handshake_hash)
        self.assertEqual(desktop_session.decrypt(client_session.encrypt(b"up")), b"up")
        self.assertEqual(client_session.decrypt(desktop_session.encrypt(b"down")), b"down")

    def test_message_1_hides_the_client_static_key(self):
        client_s, client_p = noise.generate_keypair()
        _, desktop_p = noise.generate_keypair()
        message = noise.Initiator(client_s, desktop_p).write_message_1(b"")
        self.assertNotIn(client_p, message)

    def test_wrong_desktop_key_fails(self):
        """A rendezvous that substitutes its own key cannot complete the handshake."""
        client_s, _ = noise.generate_keypair()
        desktop_s, _ = noise.generate_keypair()
        _, impostor_p = noise.generate_keypair()
        client = noise.Initiator(client_s, impostor_p)
        with self.assertRaises(noise.NoiseError):
            noise.Responder(desktop_s).read_message_1(client.write_message_1(b""))

    def test_tampered_ciphertext_is_rejected(self):
        client_s, _ = noise.generate_keypair()
        desktop_s, desktop_p = noise.generate_keypair()
        client = noise.Initiator(client_s, desktop_p)
        desktop = noise.Responder(desktop_s)
        desktop.read_message_1(client.write_message_1(b""))
        message2, desktop_session = desktop.write_message_2(b"")
        _, client_session = client.read_message_2(message2)

        payload = bytearray(client_session.encrypt(b"run rm -rf"))
        payload[-1] ^= 0x01
        with self.assertRaises(noise.NoiseError):
            desktop_session.decrypt(bytes(payload))

    def test_replayed_frame_is_rejected(self):
        """The nonce advances, so a frame captured by the relay cannot be played back."""
        client_s, _ = noise.generate_keypair()
        desktop_s, desktop_p = noise.generate_keypair()
        client = noise.Initiator(client_s, desktop_p)
        desktop = noise.Responder(desktop_s)
        desktop.read_message_1(client.write_message_1(b""))
        message2, desktop_session = desktop.write_message_2(b"")
        _, client_session = client.read_message_2(message2)

        frame = client_session.encrypt(b"compose")
        self.assertEqual(desktop_session.decrypt(frame), b"compose")
        with self.assertRaises(noise.NoiseError):
            desktop_session.decrypt(frame)

    def test_low_order_public_key_is_rejected(self):
        private, _ = noise.generate_keypair()
        with self.assertRaises(noise.NoiseError):
            noise.dh(private, bytes(32))

    def test_hkdf_three_outputs_differ(self):
        one, two, three = noise.hkdf(b"c" * 32, b"ikm", 3)
        self.assertEqual(len({one, two, three}), 3)


@unittest.skipUnless(NODE, "node is not installed")
class BrowserInteropTests(unittest.TestCase):
    """app/noise.js (the browser client) against remote/noise.py (the desktop)."""

    def setUp(self):
        self.peer = subprocess.Popen(
            [NODE, str(HERE / "noise_peer.mjs")], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, cwd=str(HERE.parent))
        self.addCleanup(self._stop)

    def _stop(self):
        self.peer.stdin.close()
        try:
            self.peer.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.peer.kill()
            self.peer.wait(timeout=5)
        for pipe in (self.peer.stdout, self.peer.stderr):
            if pipe and not pipe.closed:
                pipe.close()

    def call(self, cmd: str, **fields) -> dict:
        self.peer.stdin.write(json.dumps({"cmd": cmd, **fields}) + "\n")
        self.peer.stdin.flush()
        line = self.peer.stdout.readline()
        if not line:
            self.fail("the Node peer exited: " + self.peer.stderr.read())
        reply = json.loads(line)
        self.assertTrue(reply.get("ok"), reply.get("error"))
        return reply

    def test_browser_client_talks_to_the_desktop(self):
        desktop_s, desktop_p = noise.generate_keypair()
        desktop = noise.Responder(desktop_s)

        client_public = un64(self.call("keypair")["public"])
        self.call("start", desktop=b64(desktop_p))

        message1 = un64(self.call("message1", payload=b64(b'{"t":"hello"}'))["message"])
        self.assertEqual(desktop.read_message_1(message1), b'{"t":"hello"}')
        self.assertEqual(desktop.client_static, client_public)

        message2, session = desktop.write_message_2(b'{"t":"welcome"}')
        reply = self.call("message2", message=b64(message2))
        self.assertEqual(un64(reply["payload"]), b'{"t":"welcome"}')
        self.assertEqual(un64(reply["handshake_hash"]), session.handshake_hash)

        # Both directions, several frames, so the nonce counters stay in step.
        for index in range(3):
            sent = f"from the phone {index}".encode()
            got = un64(self.call("encrypt", plaintext=b64(sent))["ciphertext"])
            self.assertEqual(session.decrypt(got), sent)
        for index in range(3):
            sent = f"from the desktop {index}".encode()
            got = self.call("decrypt", ciphertext=b64(session.encrypt(sent)))
            self.assertEqual(un64(got["plaintext"]), sent)


if __name__ == "__main__":
    unittest.main()
