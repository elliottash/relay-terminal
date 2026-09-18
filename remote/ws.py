# SPDX-License-Identifier: GPL-3.0-or-later
"""A small RFC 6455 WebSocket, server and client, on asyncio and the standard library.

The rendezvous exists to be boring, auditable and self-hostable, so it does not pull in a
WebSocket stack; and the desktop needs the client half of the same thing to dial out. Only what
RRP uses is here: binary and text data frames, ping/pong, close, and the masking rules. There is no
extension negotiation and no permessage-deflate — RRP frames are ciphertext and would not compress.

Everything is length-checked before allocation: a peer cannot make us reserve memory by announcing
a large frame.
"""
from __future__ import annotations

import asyncio
import base64
import os
import secrets
import struct
from dataclasses import dataclass, field
from hashlib import sha1
from urllib.parse import urlsplit

GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
MAX_FRAME = 1 << 21          # 2 MiB: RRP frames are capped at 1 MiB plus overhead
MAX_HEADERS = 64 * 1024

OP_CONT, OP_TEXT, OP_BINARY, OP_CLOSE, OP_PING, OP_PONG = 0x0, 0x1, 0x2, 0x8, 0x9, 0xA


class WebSocketError(Exception):
    pass


class ConnectionClosed(WebSocketError):
    def __init__(self, code: int = 1006, reason: str = ""):
        super().__init__(f"connection closed ({code}) {reason}".strip())
        self.code, self.reason = code, reason


def accept_key(key: str) -> str:
    return base64.b64encode(sha1(key.encode() + GUID).digest()).decode()


@dataclass
class Request:
    """One parsed HTTP request line plus headers."""
    method: str
    target: str
    headers: dict[str, str] = field(default_factory=dict)
    peer: str = ""

    @property
    def path(self) -> str:
        return urlsplit(self.target).path

    @property
    def query(self) -> dict[str, str]:
        out: dict[str, str] = {}
        for part in urlsplit(self.target).query.split("&"):
            if not part:
                continue
            name, _, value = part.partition("=")
            from urllib.parse import unquote_plus
            out[unquote_plus(name)] = unquote_plus(value)
        return out

    def header(self, name: str, default: str = "") -> str:
        return self.headers.get(name.lower(), default)

    @property
    def wants_upgrade(self) -> bool:
        return ("websocket" in self.header("upgrade").lower()
                and "upgrade" in self.header("connection").lower())


async def read_request(reader: asyncio.StreamReader, peer: str = "") -> Request | None:
    """Read one HTTP request head. Returns None at a clean EOF."""
    try:
        head = await reader.readuntil(b"\r\n\r\n")
    except (asyncio.IncompleteReadError, asyncio.LimitOverrunError, ConnectionResetError):
        return None
    if len(head) > MAX_HEADERS:
        raise WebSocketError("request head too large.")
    lines = head.decode("latin-1").split("\r\n")
    method, _, rest = lines[0].partition(" ")
    target, _, _ = rest.partition(" ")
    headers: dict[str, str] = {}
    for line in lines[1:]:
        if not line:
            continue
        name, _, value = line.partition(":")
        headers[name.strip().lower()] = value.strip()
    return Request(method=method.upper(), target=target, headers=headers, peer=peer)


class WebSocket:
    """One connection. Not safe for concurrent senders; use ``send`` from a single task."""

    def __init__(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter, *,
                 mask: bool, request: Request | None = None):
        self._reader = reader
        self._writer = writer
        self._mask = mask                  # clients mask, servers must not
        self._closed = False
        self._lock = asyncio.Lock()
        self.request = request

    @property
    def closed(self) -> bool:
        return self._closed

    @property
    def peer(self) -> str:
        if self.request and self.request.peer:
            return self.request.peer
        info = self._writer.get_extra_info("peername")
        return f"{info[0]}:{info[1]}" if info else "?"

    # ---- receiving ---------------------------------------------------------------------------

    async def recv(self) -> bytes | str:
        """The next data message. Raises ConnectionClosed when the peer goes away."""
        chunks: list[bytes] = []
        kind = None
        while True:
            fin, opcode, payload = await self._read_frame()
            if opcode == OP_CLOSE:
                code, reason = 1005, ""
                if len(payload) >= 2:
                    code = struct.unpack("!H", payload[:2])[0]
                    reason = payload[2:].decode("utf-8", "replace")
                await self._close_now(code if code != 1005 else 1000)
                raise ConnectionClosed(code, reason)
            if opcode == OP_PING:
                await self._send_frame(OP_PONG, payload)
                continue
            if opcode == OP_PONG:
                continue
            if opcode in (OP_TEXT, OP_BINARY):
                if kind is not None:
                    raise WebSocketError("a new data frame arrived inside a fragmented message.")
                kind = opcode
            elif opcode == OP_CONT:
                if kind is None:
                    raise WebSocketError("a continuation frame arrived with nothing to continue.")
            else:
                raise WebSocketError(f"unknown opcode {opcode:#x}.")
            chunks.append(payload)
            if sum(len(c) for c in chunks) > MAX_FRAME:
                raise WebSocketError("message too large.")
            if fin:
                data = b"".join(chunks)
                return data.decode("utf-8") if kind == OP_TEXT else data

    async def _read_exactly(self, count: int) -> bytes:
        try:
            return await self._reader.readexactly(count)
        except (asyncio.IncompleteReadError, ConnectionResetError) as exc:
            self._closed = True
            raise ConnectionClosed() from exc

    async def _read_frame(self) -> tuple[bool, int, bytes]:
        first, second = await self._read_exactly(2)
        fin = bool(first & 0x80)
        if first & 0x70:
            raise WebSocketError("reserved bits are set but no extension was negotiated.")
        opcode = first & 0x0F
        masked = bool(second & 0x80)
        length = second & 0x7F
        if length == 126:
            length = struct.unpack("!H", await self._read_exactly(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", await self._read_exactly(8))[0]
        if length > MAX_FRAME:
            raise WebSocketError(f"frame of {length} bytes exceeds the limit.")
        if opcode >= 0x8:
            if not fin or length > 125:
                raise WebSocketError("a control frame must be short and unfragmented.")
        # RFC 6455 section 5.1: client frames are masked, server frames are not.
        if self._mask and masked:
            raise WebSocketError("the server sent a masked frame.")
        if not self._mask and not masked:
            raise WebSocketError("the client sent an unmasked frame.")
        key = await self._read_exactly(4) if masked else b""
        payload = await self._read_exactly(length)
        if masked:
            payload = bytes(byte ^ key[index % 4] for index, byte in enumerate(payload))
        return fin, opcode, payload

    # ---- sending -----------------------------------------------------------------------------

    async def send(self, message: bytes | str) -> None:
        opcode = OP_TEXT if isinstance(message, str) else OP_BINARY
        payload = message.encode() if isinstance(message, str) else message
        await self._send_frame(opcode, payload)

    async def ping(self, payload: bytes = b"") -> None:
        await self._send_frame(OP_PING, payload[:125])

    async def _send_frame(self, opcode: int, payload: bytes) -> None:
        if self._closed:
            raise ConnectionClosed()
        header = bytearray([0x80 | opcode])
        length = len(payload)
        mask_bit = 0x80 if self._mask else 0
        if length < 126:
            header.append(mask_bit | length)
        elif length < (1 << 16):
            header.append(mask_bit | 126)
            header += struct.pack("!H", length)
        else:
            header.append(mask_bit | 127)
            header += struct.pack("!Q", length)
        if self._mask:
            key = os.urandom(4)
            header += key
            payload = bytes(byte ^ key[index % 4] for index, byte in enumerate(payload))
        async with self._lock:
            self._writer.write(bytes(header) + payload)
            try:
                await self._writer.drain()
            except (ConnectionResetError, BrokenPipeError) as exc:
                self._closed = True
                raise ConnectionClosed() from exc

    async def close(self, code: int = 1000, reason: str = "") -> None:
        if self._closed:
            return
        try:
            await self._send_frame(OP_CLOSE, struct.pack("!H", code) + reason.encode()[:123])
        except (ConnectionClosed, WebSocketError):
            pass
        await self._close_now(code)

    async def _close_now(self, code: int = 1000) -> None:
        self._closed = True
        try:
            self._writer.close()
            await self._writer.wait_closed()
        except (ConnectionResetError, BrokenPipeError, OSError):
            pass


# ---- server side ---------------------------------------------------------------------------

async def accept(request: Request, reader: asyncio.StreamReader, writer: asyncio.StreamWriter,
                 subprotocol: str | None = None) -> WebSocket:
    """Complete the upgrade for a request that ``wants_upgrade``."""
    key = request.header("sec-websocket-key")
    if not key or request.header("sec-websocket-version") != "13":
        raise WebSocketError("not a WebSocket 13 handshake.")
    lines = ["HTTP/1.1 101 Switching Protocols", "Upgrade: websocket", "Connection: Upgrade",
             f"Sec-WebSocket-Accept: {accept_key(key)}"]
    if subprotocol:
        lines.append(f"Sec-WebSocket-Protocol: {subprotocol}")
    writer.write(("\r\n".join(lines) + "\r\n\r\n").encode())
    await writer.drain()
    return WebSocket(reader, writer, mask=False, request=request)


# ---- client side ---------------------------------------------------------------------------

async def connect(url: str, *, ssl_context=None, headers: dict[str, str] | None = None,
                  timeout: float = 20.0) -> WebSocket:
    """Dial a ws:// or wss:// URL."""
    parts = urlsplit(url)
    secure = parts.scheme == "wss"
    if parts.scheme not in ("ws", "wss"):
        raise WebSocketError(f"not a WebSocket URL: {url}")
    port = parts.port or (443 if secure else 80)
    target = parts.path or "/"
    if parts.query:
        target += "?" + parts.query
    if secure and ssl_context is None:
        import ssl
        ssl_context = ssl.create_default_context()
    reader, writer = await asyncio.wait_for(
        asyncio.open_connection(parts.hostname, port, ssl=ssl_context if secure else None),
        timeout=timeout)
    key = base64.b64encode(secrets.token_bytes(16)).decode()
    host = parts.hostname if port in (80, 443) else f"{parts.hostname}:{port}"
    lines = [f"GET {target} HTTP/1.1", f"Host: {host}", "Upgrade: websocket", "Connection: Upgrade",
             f"Sec-WebSocket-Key: {key}", "Sec-WebSocket-Version: 13"]
    for name, value in (headers or {}).items():
        lines.append(f"{name}: {value}")
    writer.write(("\r\n".join(lines) + "\r\n\r\n").encode())
    await writer.drain()

    head = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), timeout=timeout)
    status_line, _, rest = head.decode("latin-1").partition("\r\n")
    if " 101 " not in status_line:
        writer.close()
        raise WebSocketError(f"the server refused the upgrade: {status_line.strip()}")
    got = ""
    for line in rest.split("\r\n"):
        name, _, value = line.partition(":")
        if name.strip().lower() == "sec-websocket-accept":
            got = value.strip()
    if got != accept_key(key):
        writer.close()
        raise WebSocketError("the server's Sec-WebSocket-Accept did not match.")
    return WebSocket(reader, writer, mask=True)
