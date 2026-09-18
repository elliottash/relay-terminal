# SPDX-License-Identifier: GPL-3.0-or-later
"""The relay envelope: the only cleartext the rendezvous ever sees.

A desktop holds **one** outbound WebSocket but may be talking to several phones, so the rendezvous
needs to know which client a frame belongs to without being able to read it. Every message on the
desktop's socket is therefore:

    [1 byte kind][16 byte channel id][payload]

and every message on a client's socket is the bare payload, because a client has exactly one
channel. The payload is a Noise message (or, before the handshake, the client's first handshake
message). The rendezvous copies it; it holds no key that can open it.

Over a WebSocket the message boundary is the frame boundary, so there is no length prefix here.
The 4-byte prefix in docs/REMOTE-PROTOCOL.md section 3 applies to stream transports (a WebRTC data
channel in P2), where boundaries have to be restored.
"""
from __future__ import annotations

import json
import secrets
from dataclasses import dataclass

CHANNEL_BYTES = 16

KIND_DATA = 0x01     # payload is a Noise message for this channel
KIND_OPEN = 0x02     # rendezvous → desktop: a client attached; payload is JSON metadata
KIND_CLOSE = 0x03    # either way: this channel is finished; payload is JSON {reason}

KINDS = {KIND_DATA, KIND_OPEN, KIND_CLOSE}


class EnvelopeError(Exception):
    pass


def new_channel() -> bytes:
    return secrets.token_bytes(CHANNEL_BYTES)


@dataclass(frozen=True)
class Envelope:
    kind: int
    channel: bytes
    payload: bytes = b""

    def meta(self) -> dict:
        """The JSON metadata of an open or close envelope."""
        if not self.payload:
            return {}
        try:
            value = json.loads(self.payload)
        except ValueError as exc:
            raise EnvelopeError("envelope metadata is not JSON.") from exc
        if not isinstance(value, dict):
            raise EnvelopeError("envelope metadata must be a JSON object.")
        return value


def pack(kind: int, channel: bytes, payload: bytes = b"") -> bytes:
    if kind not in KINDS:
        raise EnvelopeError(f"unknown envelope kind {kind:#x}.")
    if len(channel) != CHANNEL_BYTES:
        raise EnvelopeError("a channel id must be 16 bytes.")
    return bytes([kind]) + channel + payload


def pack_json(kind: int, channel: bytes, meta: dict) -> bytes:
    return pack(kind, channel, json.dumps(meta, separators=(",", ":")).encode())


def unpack(message: bytes) -> Envelope:
    if len(message) < 1 + CHANNEL_BYTES:
        raise EnvelopeError("envelope too short.")
    kind = message[0]
    if kind not in KINDS:
        raise EnvelopeError(f"unknown envelope kind {kind:#x}.")
    return Envelope(kind=kind, channel=message[1:1 + CHANNEL_BYTES],
                    payload=message[1 + CHANNEL_BYTES:])
