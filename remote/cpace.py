# SPDX-License-Identifier: AGPL-3.0-or-later
"""CPACE-X25519-SHA512 — the PAKE behind "join with a meeting code and a PIN" (card #97EG).

Why a PAKE: a four-digit PIN is ten thousand guesses, which is nothing offline. Anything that lets
an eavesdropper (or the rendezvous server) check a guess against a recorded transcript — hashing
the PIN into a key, say — hands it the PIN in milliseconds. CPace turns the PIN into an elliptic
curve generator and runs Diffie-Hellman on it, so each party can test exactly one PIN per live
attempt and a passive observer can test none. The desktop burns the code after three failures.

Why CPace and this suite: it is the CFRG's recommended balanced PAKE, it needs one message each
way, and with X25519 its only group operation is X25519 itself — which WebCrypto has in the
browser and ``cryptography`` has here, the same way remote/noise.py splits its work. The one piece
neither library offers is Elligator2 (hashing to the curve), which is a few lines of field
arithmetic mod 2^255 - 19 on plain ints.

This follows draft-irtf-cfrg-cpace-21, initiator-responder mode, and is checked against the
draft's published X25519/SHA-512 vectors in tests/test_cpace.py. The browser half is
app/cpace.js; tests/cpace_vectors.mjs proves the two agree byte for byte.

    A (initiator: the guest)                  B (responder: the desktop)
    g = calculate_generator(PRS, CI, sid)     g = calculate_generator(PRS, CI, sid)
    Ya = X25519(ya, g)       -- Ya, ADa -->
                             <-- Yb, ADb --   Yb = X25519(yb, g)
    K = X25519(ya, Yb)                        K = X25519(yb, Ya)
    ISK = SHA-512(lv_cat(b"CPace255_ISK", sid, K) || lv_cat(Ya, ADa) || lv_cat(Yb, ADb))

ISK is only the shared secret. It does not prove the peer knew the PIN; the caller does that with
key-confirmation tags derived from it (the card's wire spec), and must not use ISK before they
check out.
"""
from __future__ import annotations

import os
from hashlib import sha512

from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey

DSI = b"CPace255"                  # G_X25519.DSI
DSI_ISK = DSI + b"_ISK"
FIELD_BYTES = 32                   # G_X25519.field_size_bytes
FIELD_BITS = 255                   # G_X25519.field_size_bits
HASH_BLOCK = 128                   # SHA-512's input block size, H.s_in_bytes
IDENTITY = bytes(FIELD_BYTES)      # G_X25519.I

P = 2 ** 255 - 19                  # Curve25519's field
A = 486662                         # Curve25519: v^2 = u^3 + A u^2 + u
Z = 2                              # Elligator2's non-square for this field (RFC 9380, draft A.5)


class CPaceError(Exception):
    """An invalid peer message or a degenerate shared secret. Always fatal to the attempt."""


# ---- string encoding (draft appendix A.1, A.2, A.3.4) --------------------------------------

def _leb128(n: int) -> bytes:
    out = bytearray()
    while True:
        byte, n = n & 0x7F, n >> 7
        out.append(byte | (0x80 if n else 0))
        if not n:
            return bytes(out)


def prepend_len(data: bytes) -> bytes:
    return _leb128(len(data)) + data


def lv_cat(*parts: bytes) -> bytes:
    return b"".join(prepend_len(p) for p in parts)


def generator_string(prs: bytes, ci: bytes, sid: bytes) -> bytes:
    # The zero padding makes DSI and PRS fill the hash's whole first block, so how long the PIN
    # is does not change how many blocks get hashed.
    zpad = max(0, HASH_BLOCK - 1 - len(prepend_len(prs)) - len(prepend_len(DSI)))
    return lv_cat(DSI, prs, bytes(zpad), ci, sid)


def transcript_ir(ya: bytes, ada: bytes, yb: bytes, adb: bytes) -> bytes:
    """The initiator-responder transcript: the initiator's message always goes first."""
    return lv_cat(ya, ada) + lv_cat(yb, adb)


# ---- the group (draft section 8.2) ---------------------------------------------------------

def _elligator2(r: int) -> int:
    """RFC 9380 map_to_curve_elligator2 for Curve25519, u-coordinate only (draft A.5)."""
    denominator = (1 + Z * r * r) % P
    # 1 + 2r^2 = 0 would need -1/2 to be a square; it is not mod p, but RFC 9380 defines the case.
    x1 = (-A * pow(denominator, -1, P)) % P if denominator else (-A) % P
    gx1 = (x1 * x1 * x1 + A * x1 * x1 + x1) % P
    square = pow(gx1, (P - 1) // 2, P) != P - 1          # Euler's criterion; 0 counts as square
    return x1 if square else (-x1 - A) % P


def generator(prs: bytes, ci: bytes, sid: bytes) -> bytes:
    """G_X25519.calculate_generator: the 32-byte u-coordinate g that the PIN picks."""
    digest = sha512(generator_string(prs, ci, sid)).digest()[:FIELD_BYTES]
    # RFC 7748 decodeUCoordinate: little endian with bit 255 cleared (draft A.4).
    r = int.from_bytes(digest, "little") & ((1 << FIELD_BITS) - 1)
    return _elligator2(r % P).to_bytes(FIELD_BYTES, "little")


def scalar_mult_vfy(scalar: bytes, point: bytes) -> bytes:
    """G_X25519.scalar_mult_vfy: X25519(scalar, point), refusing the identity (all zeros)."""
    if len(scalar) != FIELD_BYTES:
        raise CPaceError("a CPace scalar must be 32 bytes.")
    if len(point) != FIELD_BYTES:
        raise CPaceError("a CPace point must be 32 bytes.")
    try:
        out = X25519PrivateKey.from_private_bytes(scalar).exchange(X25519PublicKey.from_public_bytes(point))
    except Exception as exc:  # OpenSSL refuses a low-order point itself
        raise CPaceError("the peer's point is invalid.") from exc
    if out == IDENTITY:
        raise CPaceError("the peer's point is invalid.")
    return out


# ---- the protocol (draft section 7.2) ------------------------------------------------------

class CPace:
    """One side of one attempt. Make a fresh one per attempt: the scalar must never be reused."""

    def __init__(self, prs: bytes, ci: bytes, sid: bytes, *, initiator: bool, ad: bytes,
                 scalar: bytes | None = None):
        self.sid = sid
        self.initiator = initiator
        self.ad = ad
        # sample_scalar() is 32 random bytes; X25519 clamps them. Injectable only for test vectors.
        self._scalar = os.urandom(FIELD_BYTES) if scalar is None else scalar
        if len(self._scalar) != FIELD_BYTES:
            raise CPaceError("a CPace scalar must be 32 bytes.")
        self.message = scalar_mult_vfy(self._scalar, generator(prs, ci, sid))

    def finish(self, peer_y: bytes, peer_ad: bytes) -> bytes:
        """The 64-byte ISK. Raises CPaceError on a malformed or low-order peer point."""
        if len(peer_y) != FIELD_BYTES:
            raise CPaceError("the peer's point must be 32 bytes.")
        k = scalar_mult_vfy(self._scalar, peer_y)
        if self.initiator:
            transcript = transcript_ir(self.message, self.ad, peer_y, peer_ad)
        else:
            transcript = transcript_ir(peer_y, peer_ad, self.message, self.ad)
        return sha512(lv_cat(DSI_ISK, self.sid, k) + transcript).digest()
