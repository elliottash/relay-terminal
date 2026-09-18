# SPDX-License-Identifier: GPL-3.0-or-later
"""Noise_IK_25519_AESGCM_SHA256 — the handshake under every RRP/1 session.

Why this suite (docs/REMOTE-PROTOCOL.md section 4): the client is a browser, and WebCrypto offers
X25519, AES-GCM, SHA-256 and HMAC but **not** ChaCha20-Poly1305. AESGCM is therefore the one
standard Noise suite both ends implement with no third-party crypto at all: WebCrypto in the
browser, ``cryptography`` here.

IK because pairing already gave the client the desktop's static public key, so the session is up in
one round trip and the client's own static key travels encrypted:

    <- s                        (pre-message: the desktop's static, pinned at pairing)
    ...
    -> e, es, s, ss             message 1, client to desktop
    <- e, ee, se                message 2, desktop to client

The initiator is always the client; the responder is always the desktop. Nothing here knows about
transports: feed it bytes, get bytes.
"""
from __future__ import annotations

import hmac
from dataclasses import dataclass
from hashlib import sha256

from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.serialization import (Encoding, NoEncryption, PrivateFormat,
                                                          PublicFormat)

PROTOCOL = b"Noise_IK_25519_AESGCM_SHA256"
PROLOGUE = b"RRP/1"          # binds the session to this protocol version
HASHLEN = 32
DHLEN = 32
TAGLEN = 16
MAX_NONCE = 2 ** 64 - 1
REKEY_AFTER = 2 ** 20        # messages; section 4.1


class NoiseError(Exception):
    """A handshake or transport failure. Always fatal to the session."""


# ---- primitives ----------------------------------------------------------------------------

def generate_keypair() -> tuple[bytes, bytes]:
    """A fresh X25519 keypair as (private, public), both 32 raw bytes."""
    private = X25519PrivateKey.generate()
    return private_bytes(private), public_bytes(private.public_key())


def private_bytes(key: X25519PrivateKey) -> bytes:
    return key.private_bytes(Encoding.Raw, PrivateFormat.Raw, NoEncryption())


def public_bytes(key: X25519PublicKey) -> bytes:
    return key.public_bytes(Encoding.Raw, PublicFormat.Raw)


def public_of(private: bytes) -> bytes:
    return public_bytes(X25519PrivateKey.from_private_bytes(private).public_key())


def dh(private: bytes, public: bytes) -> bytes:
    if len(public) != DHLEN:
        raise NoiseError("a public key must be 32 bytes.")
    try:
        shared = X25519PrivateKey.from_private_bytes(private).exchange(X25519PublicKey.from_public_bytes(public))
    except Exception as exc:  # a low-order or malformed point
        raise NoiseError("X25519 exchange failed.") from exc
    # X25519 produces all zeros for low-order points; RFC 7748 says to reject that.
    if shared == bytes(DHLEN):
        raise NoiseError("X25519 exchange produced an all-zero shared secret.")
    return shared


def _hmac(key: bytes, data: bytes) -> bytes:
    return hmac.new(key, data, sha256).digest()


def hkdf(chaining_key: bytes, ikm: bytes, outputs: int) -> tuple[bytes, ...]:
    """Noise's HKDF (section 4.3 of the Noise spec), 2 or 3 outputs."""
    temp = _hmac(chaining_key, ikm)
    out1 = _hmac(temp, b"\x01")
    out2 = _hmac(temp, out1 + b"\x02")
    if outputs == 2:
        return out1, out2
    return out1, out2, _hmac(temp, out2 + b"\x03")


def _nonce(counter: int) -> bytes:
    """Noise's AESGCM nonce: 32 zero bits then a 64-bit big-endian counter."""
    return b"\x00\x00\x00\x00" + counter.to_bytes(8, "big")


# ---- CipherState ---------------------------------------------------------------------------

class CipherState:
    def __init__(self, key: bytes | None = None):
        self.key = key
        self.nonce = 0

    def has_key(self) -> bool:
        return self.key is not None

    def encrypt(self, associated: bytes, plaintext: bytes) -> bytes:
        if self.key is None:
            return plaintext
        if self.nonce >= MAX_NONCE:
            raise NoiseError("nonce exhausted; the session must be rekeyed or closed.")
        out = AESGCM(self.key).encrypt(_nonce(self.nonce), plaintext, associated)
        self.nonce += 1
        return out

    def decrypt(self, associated: bytes, ciphertext: bytes) -> bytes:
        if self.key is None:
            return ciphertext
        if self.nonce >= MAX_NONCE:
            raise NoiseError("nonce exhausted; the session must be rekeyed or closed.")
        try:
            out = AESGCM(self.key).decrypt(_nonce(self.nonce), ciphertext, associated)
        except Exception as exc:
            raise NoiseError("authentication failed.") from exc
        self.nonce += 1
        return out

    def rekey(self) -> None:
        """Noise REKEY: the new key is the encryption of 32 zero bytes under the highest nonce."""
        if self.key is None:
            raise NoiseError("cannot rekey a cipher with no key.")
        self.key = AESGCM(self.key).encrypt(_nonce(MAX_NONCE), bytes(32), b"")[:32]
        self.nonce = 0


# ---- SymmetricState ------------------------------------------------------------------------

class SymmetricState:
    def __init__(self, protocol: bytes = PROTOCOL):
        self.h = protocol.ljust(HASHLEN, b"\x00") if len(protocol) <= HASHLEN else sha256(protocol).digest()
        self.ck = self.h
        self.cipher = CipherState()

    def mix_key(self, ikm: bytes) -> None:
        self.ck, temp = hkdf(self.ck, ikm, 2)
        self.cipher = CipherState(temp)

    def mix_hash(self, data: bytes) -> None:
        self.h = sha256(self.h + data).digest()

    def encrypt_and_hash(self, plaintext: bytes) -> bytes:
        out = self.cipher.encrypt(self.h, plaintext)
        self.mix_hash(out)
        return out

    def decrypt_and_hash(self, ciphertext: bytes) -> bytes:
        out = self.cipher.decrypt(self.h, ciphertext)
        self.mix_hash(ciphertext)
        return out

    def split(self) -> tuple[CipherState, CipherState]:
        k1, k2 = hkdf(self.ck, b"", 2)
        return CipherState(k1), CipherState(k2)


# ---- HandshakeState ------------------------------------------------------------------------

@dataclass
class Session:
    """A live Noise session. ``send``/``recv`` are the two transport CipherStates."""
    send: CipherState
    recv: CipherState
    handshake_hash: bytes
    remote_static: bytes
    sent: int = 0
    received: int = 0

    def encrypt(self, plaintext: bytes) -> bytes:
        if self.sent and self.sent % REKEY_AFTER == 0:
            self.send.rekey()
        self.sent += 1
        return self.send.encrypt(b"", plaintext)

    def decrypt(self, ciphertext: bytes) -> bytes:
        if self.received and self.received % REKEY_AFTER == 0:
            self.recv.rekey()
        self.received += 1
        return self.recv.decrypt(b"", ciphertext)


class Initiator:
    """The client half: knows the desktop's static public key from pairing."""

    def __init__(self, static_private: bytes, remote_static: bytes, prologue: bytes = PROLOGUE):
        if len(remote_static) != DHLEN:
            raise NoiseError("the desktop's static public key must be 32 bytes.")
        self.s = static_private
        self.rs = remote_static
        self.e: bytes | None = None
        self.state = SymmetricState()
        self.state.mix_hash(prologue)
        self.state.mix_hash(self.rs)          # pre-message: "<- s"

    def write_message_1(self, payload: bytes = b"") -> bytes:
        self.e = X25519PrivateKey.generate()
        epub = public_bytes(self.e.public_key())
        self.e = private_bytes(self.e)
        self.state.mix_hash(epub)                                   # e
        self.state.mix_key(dh(self.e, self.rs))                     # es
        spub = self.state.encrypt_and_hash(public_of(self.s))       # s
        self.state.mix_key(dh(self.s, self.rs))                     # ss
        return epub + spub + self.state.encrypt_and_hash(payload)

    def read_message_2(self, message: bytes) -> tuple[bytes, Session]:
        if len(message) < DHLEN + TAGLEN:
            raise NoiseError("handshake message 2 is too short.")
        re, rest = message[:DHLEN], message[DHLEN:]
        self.state.mix_hash(re)                                     # e
        self.state.mix_key(dh(self.e, re))                          # ee
        self.state.mix_key(dh(self.s, re))                          # se
        payload = self.state.decrypt_and_hash(rest)
        c1, c2 = self.state.split()
        return payload, Session(send=c1, recv=c2, handshake_hash=self.state.h, remote_static=self.rs)


class Responder:
    """The desktop half: learns the client's static key from message 1 and pins it."""

    def __init__(self, static_private: bytes, prologue: bytes = PROLOGUE):
        self.s = static_private
        self.e: bytes | None = None
        self.rs: bytes | None = None
        self.re: bytes | None = None
        self.state = SymmetricState()
        self.state.mix_hash(prologue)
        self.state.mix_hash(public_of(self.s))    # pre-message: our own static

    def read_message_1(self, message: bytes) -> bytes:
        if len(message) < DHLEN + DHLEN + TAGLEN + TAGLEN:
            raise NoiseError("handshake message 1 is too short.")
        self.re = message[:DHLEN]
        self.state.mix_hash(self.re)                                        # e
        self.state.mix_key(dh(self.s, self.re))                             # es
        self.rs = self.state.decrypt_and_hash(message[DHLEN:DHLEN + DHLEN + TAGLEN])   # s
        self.state.mix_key(dh(self.s, self.rs))                             # ss
        return self.state.decrypt_and_hash(message[DHLEN + DHLEN + TAGLEN:])

    @property
    def client_static(self) -> bytes:
        """The client's static public key, known after message 1. Pin this."""
        if self.rs is None:
            raise NoiseError("no client static key yet.")
        return self.rs

    def write_message_2(self, payload: bytes = b"") -> tuple[bytes, Session]:
        self.e = X25519PrivateKey.generate()
        epub = public_bytes(self.e.public_key())
        self.e = private_bytes(self.e)
        self.state.mix_hash(epub)                   # e
        self.state.mix_key(dh(self.e, self.re))     # ee
        self.state.mix_key(dh(self.e, self.rs))     # se
        out = epub + self.state.encrypt_and_hash(payload)
        c1, c2 = self.state.split()
        # The initiator's first CipherState is its sender, so ours are swapped.
        return out, Session(send=c2, recv=c1, handshake_hash=self.state.h, remote_static=self.rs)
