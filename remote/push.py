# SPDX-License-Identifier: GPL-3.0-or-later
"""Web Push: RFC 8291 payload encryption, the inner seal, and VAPID signing.

Who may read what decides the shape (docs/REMOTE-PROTOCOL.md section 9):

* the **push service** sees only the RFC 8291 ciphertext — the standard end-to-end encryption
  to the subscription's own keys, so the browser (and nobody in between) can open the payload;
* the **rendezvous** holds only the VAPID signing key. It delivers ciphertext it cannot read,
  and because it never saw the subscription keys it cannot forge a payload either: inside the
  RFC 8291 layer every body is sealed again to a per-device push key that travelled to the
  desktop inside the Noise session (``seal``/``open`` below). A service worker that cannot open
  that inner seal discards the push, which is what makes a forged "password prompt" from a
  compromised rendezvous impossible rather than merely unlikely.

The inner seal is AES-GCM over a symmetric key rather than a Noise pattern on purpose: the
service worker must be able to open it with only what IndexedDB holds, and the property that
matters — only the desktop can construct a body the phone will show — is the same. The
spec records this as the v1 decision; sealing to the pinned Noise static key remains an
alternative with the same guarantee and more moving parts.
"""
from __future__ import annotations

import base64
import json
import os
import time

from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.kdf.hkdf import HKDF
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

PUSH_TTL = 60                      # a stale "agent finished" is worse than a missed one
PAD_LAST = b"\x02"                 # the aes128gcm padding delimiter for the last record
VAPID_SUBJECT = "mailto:relay@relay-terminal.ai"


def b64url(raw: bytes) -> str:
    return base64.urlsafe_b64encode(raw).decode().rstrip("=")


def unb64url(value: str) -> bytes:
    return base64.urlsafe_b64decode(value + "=" * (-len(value) % 4))


# ---- the inner seal ---------------------------------------------------------------------------

def seal(push_key: bytes, body: dict) -> bytes:
    """A body only the desktop could have constructed: AES-GCM with a fresh nonce.

    The whole body — the kind included — is inside the seal, so a sealed `agent_finished` cannot
    be re-labelled `password` on the way; the associated data pins the scheme, not the kind."""
    nonce = os.urandom(12)
    associated = b"relay-push-v1"
    ciphertext = AESGCM(push_key).encrypt(nonce, json.dumps(body, separators=(",", ":")).encode(),
                                          associated)
    return nonce + ciphertext


def open_sealed(push_key: bytes, blob: bytes) -> dict | None:
    """The service worker's rule: a push it cannot open is discarded, never shown."""
    try:
        plaintext = AESGCM(push_key).decrypt(blob[:12], blob[12:], b"relay-push-v1")
        body = json.loads(plaintext)
        return body if isinstance(body, dict) else None
    except (InvalidTag, ValueError, json.JSONDecodeError):
        return None


# ---- RFC 8291: the payload the push service carries -------------------------------------------

def rfc8291_encrypt(p256dh: bytes, auth: bytes, plaintext: bytes) -> bytes:
    """The aes128gcm content coding (RFC 8291 section 3.1), as the browser will decrypt it."""
    if len(p256dh) != 65 or p256dh[0] != 0x04:
        raise ValueError("p256dh must be an uncompressed P-256 point.")
    server = ec.generate_private_key(ec.SECP256R1())
    ua_public = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), p256dh)
    shared = server.exchange(ec.ECDH(), ua_public)
    server_public = server.public_key().public_bytes(Encoding.X962, PublicFormat.UncompressedPoint)

    # key_info is `ua_public || as_public`, in that order (RFC 8291 section 3.4). The other order
    # is self-consistent and every browser refuses it, which no test that only talks to itself can
    # see; the RFC's own Appendix A vector is what catches it.
    ikm = HKDF(algorithm=hashes.SHA256(), length=32, salt=auth,
               info=b"WebPush: info\x00" + p256dh + server_public).derive(shared)

    salt = os.urandom(16)

    def expand(info: bytes, length: int) -> bytes:
        return HKDF(algorithm=hashes.SHA256(), length=length, salt=salt,
                    info=info).derive(ikm)

    cek = expand(b"Content-Encoding: aes128gcm\x00", 16)
    nonce = expand(b"Content-Encoding: nonce\x00", 12)
    # aes128gcm records are padded and the pad is delimited (RFC 8188 section 2): the last record
    # ends its plaintext with 0x02. A receiver strips from that byte, so a record without one is
    # either refused or read one byte short — again invisible to a round trip against ourselves.
    ciphertext = AESGCM(cek).encrypt(nonce, plaintext + PAD_LAST, b"")
    record = bytearray(salt)
    record += (4096).to_bytes(4, "big")            # rs: the whole message is one record
    record.append(len(server_public))
    record += server_public
    record += ciphertext
    return bytes(record)


def rfc8291_decrypt(ua_private: bytes, auth: bytes, blob: bytes) -> bytes:
    """The browser's half of the same scheme. Tests use it to prove the ciphertext really is
    what a phone will open; nothing in the product path calls it."""
    if len(blob) < 21 + 65 + 16:
        raise ValueError("truncated aes128gcm record.")
    salt = blob[:16]
    idlen = blob[20]
    server_public = blob[21:21 + idlen]
    ua = ec.derive_private_key(int.from_bytes(ua_private, "big"), ec.SECP256R1())
    ua_public = ua.public_key().public_bytes(Encoding.X962, PublicFormat.UncompressedPoint)
    shared = ua.exchange(ec.ECDH(),
                         ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), server_public))
    ikm = HKDF(algorithm=hashes.SHA256(), length=32, salt=auth,
               info=b"WebPush: info\x00" + ua_public + server_public).derive(shared)

    def expand(info: bytes, length: int) -> bytes:
        return HKDF(algorithm=hashes.SHA256(), length=length, salt=salt,
                    info=info).derive(ikm)

    cek = expand(b"Content-Encoding: aes128gcm\x00", 16)
    nonce = expand(b"Content-Encoding: nonce\x00", 12)
    padded = AESGCM(cek).decrypt(nonce, blob[21 + idlen:], b"")
    return unpad(padded)


def unpad(padded: bytes) -> bytes:
    """Strip an aes128gcm record's padding: trailing zeros, then the delimiter (RFC 8188)."""
    end = len(padded)
    while end and padded[end - 1] == 0:
        end -= 1
    if not end or padded[end - 1] not in (1, 2):
        raise ValueError("aes128gcm record has no padding delimiter.")
    return padded[:end - 1]


# ---- VAPID (the rendezvous proves it is the sender the subscription was made under) ------------

def vapid_generate() -> tuple[bytes, bytes]:
    """A VAPID P-256 keypair: (private raw 32, public uncompressed 65)."""
    key = ec.generate_private_key(ec.SECP256R1())
    private = key.private_numbers().private_value.to_bytes(32, "big")
    public = key.public_key().public_bytes(Encoding.X962, PublicFormat.UncompressedPoint)
    return private, public


def vapid_authorization(private: bytes, endpoint: str, *, subject: str = VAPID_SUBJECT) -> str:
    """`vapid t=<JWT>,k=<key>` for one endpoint (RFC 8293)."""
    header = b64url(json.dumps({"typ": "JWT", "alg": "ES256"},
                               separators=(",", ":")).encode())
    claims = b64url(json.dumps({"aud": endpoint.split("/")[0] + "//" + endpoint.split("/")[2],
                                "exp": int(time.time()) + 12 * 3600, "sub": subject},
                               separators=(",", ":")).encode())
    signing_input = f"{header}.{claims}".encode()
    key = ec.derive_private_key(int.from_bytes(private, "big"), ec.SECP256R1())
    # JOSE wants raw r||s; a DER signature is not that, so sign the digest by hand.
    from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature
    der = key.sign(signing_input, ec.ECDSA(hashes.SHA256()))
    from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
    r, s = decode_dss_signature(der)
    raw = r.to_bytes(32, "big") + s.to_bytes(32, "big")
    token = f"{header}.{claims}.{b64url(raw)}"
    public = key.public_key().public_bytes(Encoding.X962, PublicFormat.UncompressedPoint)
    return f"vapid t={token}, k={b64url(public)}"


def vapid_verify(public: bytes, authorization: str, endpoint: str) -> bool:
    """Test-side check: the JWT really signs this audience with this key."""
    try:
        scheme, pairs = authorization.split(" ", 1)
        assert scheme == "vapid"
        fields = dict(pair.split("=", 1) for pair in pairs.split(", "))
        token, key = fields["t"], fields["k"]
        header_b64, claims_b64, signature_b64 = token.split(".")
        assert header_b64 == b64url(json.dumps({"typ": "JWT", "alg": "ES256"},
                                               separators=(",", ":")).encode())
        claims = json.loads(unb64url(claims_b64))
        if claims["aud"] != endpoint.split("/")[0] + "//" + endpoint.split("/")[2]:
            return False
        signing_input = f"{header_b64}.{claims_b64}".encode()
        public_key = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), unb64url(key))
        assert unb64url(key) == public
        signature = unb64url(signature_b64)
        # verify() expects a DER signature; build one from raw r||s.
        from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature
        public_key.verify(encode_dss_signature(int.from_bytes(signature[:32], "big"),
                                               int.from_bytes(signature[32:], "big")),
                          signing_input, ec.ECDSA(hashes.SHA256()))
        return True
    except Exception:
        return False
