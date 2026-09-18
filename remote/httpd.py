# SPDX-License-Identifier: GPL-3.0-or-later
"""A small asyncio HTTP server: JSON routes, static files and WebSocket upgrades on one port.

One port matters. The web app and the rendezvous socket share an origin, so the app connects to
its own host with no CORS, and a single TLS certificate (or a single ``tailscale serve`` route)
covers both. In production the same code runs behind cloudflared; in development it is the thing
the phone talks to directly.

The static half is deliberately strict: it serves a fixed root, refuses anything that escapes it,
and sends the app's CSP. Both are part of the design's answer to "whoever serves the JavaScript can
serve a key-stealing version" (docs/REMOTE-AND-MULTIPLAYER-DESIGN.md section 3).
"""
from __future__ import annotations

import asyncio
import json
import logging
import mimetypes
from dataclasses import dataclass
from pathlib import Path
from typing import Awaitable, Callable

from . import ws

log = logging.getLogger("relay.httpd")

MAX_BODY = 1 << 20

# No third-party scripts, no inline script, no framing. 'wasm-unsafe-eval' is not granted.
CSP = ("default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; "
       "connect-src 'self' wss: ws:; frame-ancestors 'none'; base-uri 'none'; form-action 'none'")

SECURITY_HEADERS = {
    "Content-Security-Policy": CSP,
    "X-Content-Type-Options": "nosniff",
    "Referrer-Policy": "no-referrer",
    "Cross-Origin-Opener-Policy": "same-origin",
    "Permissions-Policy": "camera=(self), microphone=(self), geolocation=()",
}

STATUS_TEXT = {200: "OK", 201: "Created", 204: "No Content", 304: "Not Modified",
               400: "Bad Request", 401: "Unauthorized", 403: "Forbidden", 404: "Not Found",
               405: "Method Not Allowed", 409: "Conflict", 413: "Payload Too Large",
               429: "Too Many Requests", 500: "Internal Server Error"}


@dataclass
class Response:
    status: int = 200
    body: bytes = b""
    content_type: str = "application/json"
    headers: dict[str, str] | None = None

    @classmethod
    def json(cls, payload, status: int = 200) -> "Response":
        return cls(status=status, body=json.dumps(payload).encode(), content_type="application/json")

    @classmethod
    def error(cls, status: int, message: str) -> "Response":
        return cls.json({"error": message}, status=status)


HttpHandler = Callable[[ws.Request, bytes], Awaitable[Response]]
SocketHandler = Callable[[ws.WebSocket], Awaitable[None]]


class Server:
    """Routes are exact paths. ``static_root`` serves everything else."""

    def __init__(self, *, static_root: Path | None = None, index: str = "index.html"):
        self.routes: dict[tuple[str, str], HttpHandler] = {}
        self.sockets: dict[str, SocketHandler] = {}
        self.static_root = Path(static_root).resolve() if static_root else None
        self.index = index
        self._server: asyncio.AbstractServer | None = None
        self._listeners: list[asyncio.AbstractServer] = []

    def route(self, method: str, path: str):
        def register(handler: HttpHandler):
            self.routes[(method.upper(), path)] = handler
            return handler
        return register

    def socket(self, path: str):
        def register(handler: SocketHandler):
            self.sockets[path] = handler
            return handler
        return register

    async def start(self, host: str, port: int, ssl_context=None) -> asyncio.AbstractServer:
        """Add a listener. Several are allowed: development serves plain loopback for the local
        host process and TLS on the tailnet or LAN for a phone, from one set of routes."""
        listener = await asyncio.start_server(self._client, host, port, ssl=ssl_context)
        self._listeners.append(listener)
        if self._server is None:
            self._server = listener
        return listener

    @property
    def port(self) -> int:
        return self._server.sockets[0].getsockname()[1] if self._server else 0

    @staticmethod
    def port_of(listener: asyncio.AbstractServer) -> int:
        return listener.sockets[0].getsockname()[1]

    async def close(self) -> None:
        for listener in self._listeners:
            listener.close()
            await listener.wait_closed()
        self._listeners.clear()
        self._server = None

    # ---- connection handling -----------------------------------------------------------------

    async def _client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        info = writer.get_extra_info("peername")
        peer = f"{info[0]}:{info[1]}" if info else "?"
        try:
            while True:
                request = await ws.read_request(reader, peer=peer)
                if request is None:
                    break
                if request.wants_upgrade:
                    await self._upgrade(request, reader, writer)
                    return
                keep_alive = await self._http(request, reader, writer)
                if not keep_alive:
                    break
        except (ConnectionResetError, BrokenPipeError, asyncio.IncompleteReadError):
            pass
        except ws.WebSocketError as error:
            log.info("bad request from %s: %s", peer, error)
        except Exception:
            log.exception("unhandled error serving %s", peer)
        finally:
            try:
                writer.close()
                await writer.wait_closed()
            except (ConnectionResetError, BrokenPipeError, OSError):
                pass

    async def _upgrade(self, request: ws.Request, reader, writer) -> None:
        handler = self.sockets.get(request.path)
        if handler is None:
            await self._write(writer, ws.Request("GET", "/"), Response.error(404, "no such socket."))
            return
        try:
            socket = await ws.accept(request, reader, writer)
        except ws.WebSocketError as error:
            log.info("upgrade refused for %s: %s", request.peer, error)
            return
        try:
            await handler(socket)
        except ws.ConnectionClosed:
            pass
        except Exception:
            log.exception("socket handler failed for %s", request.peer)
        finally:
            await socket.close()

    async def _http(self, request: ws.Request, reader, writer) -> bool:
        body = b""
        length = request.header("content-length")
        if length:
            try:
                count = int(length)
            except ValueError:
                await self._write(writer, request, Response.error(400, "bad Content-Length."))
                return False
            if count > MAX_BODY:
                await self._write(writer, request, Response.error(413, "body too large."))
                return False
            body = await reader.readexactly(count)

        handler = self.routes.get((request.method, request.path))
        if handler is not None:
            try:
                response = await handler(request, body)
            except Exception:
                log.exception("route %s %s failed", request.method, request.path)
                response = Response.error(500, "internal error.")
        elif request.method in ("GET", "HEAD") and self.static_root is not None:
            response = self._static(request)
        else:
            response = Response.error(404, "not found.")
        return await self._write(writer, request, response)

    def _static(self, request: ws.Request) -> Response:
        relative = request.path.lstrip("/") or self.index
        # Pair and join are client-side routes; both land on the app shell.
        if relative in ("pair", "join"):
            relative = self.index
        candidate = (self.static_root / relative).resolve()
        if not candidate.is_relative_to(self.static_root):
            return Response.error(403, "forbidden.")
        if candidate.is_dir():
            candidate = candidate / self.index
        if not candidate.is_file():
            return Response.error(404, "not found.")
        kind, _ = mimetypes.guess_type(candidate.name)
        if candidate.suffix == ".js":
            kind = "text/javascript"          # some systems still say application/javascript
        elif candidate.suffix == ".webmanifest":
            kind = "application/manifest+json"
        return Response(body=candidate.read_bytes(), content_type=kind or "application/octet-stream",
                        headers={"Cache-Control": "no-store"})

    async def _write(self, writer, request: ws.Request, response: Response) -> bool:
        headers = {"Content-Type": response.content_type,
                   "Content-Length": str(len(response.body)),
                   **SECURITY_HEADERS, **(response.headers or {})}
        keep_alive = "close" not in request.header("connection").lower()
        headers["Connection"] = "keep-alive" if keep_alive else "close"
        text = STATUS_TEXT.get(response.status, "OK")
        head = f"HTTP/1.1 {response.status} {text}\r\n"
        head += "".join(f"{name}: {value}\r\n" for name, value in headers.items())
        writer.write(head.encode() + b"\r\n" + (b"" if request.method == "HEAD" else response.body))
        try:
            await writer.drain()
        except (ConnectionResetError, BrokenPipeError):
            return False
        return keep_alive


def json_body(body: bytes) -> dict:
    """Parse a JSON request body, always returning a dict."""
    if not body:
        return {}
    try:
        value = json.loads(body)
    except ValueError as exc:
        raise ValueError("body is not JSON.") from exc
    if not isinstance(value, dict):
        raise ValueError("body must be a JSON object.")
    return value
