# SPDX-License-Identifier: GPL-3.0-or-later
"""The remote-sharing sidecar the GUI runs: ``python3 -m remote.gui_host``.

Relay's panes live in the GUI process; the Noise sessions, the rendezvous and the web app live
here. The two halves talk line JSON over stdio, the same shape the agent worker already uses, so
the GUI never links a crypto library and this process never touches a widget.

  GUI → here
    {"t":"start","tls":true,"address":"...","port":0}       bring the service up
    {"t":"pane","id":"p1","title":"...","cwd":"...","rows":R,"cols":C,"status":"idle"}
    {"t":"frame","pane":"p1","full":bool,"cursor":{...},"lines":[...],"rows":R,"cols":C,"alt":b}
    {"t":"agent","pane":"p1","event":{...}}                  one worker event, verbatim
    {"t":"unpane","id":"p1"}                                 pane closed or stopped sharing
    {"t":"pair"}                                             open a pairing room, get a QR
    {"t":"answer","id":N,"allow":true,"capability":"full"}   the user answered the dialog
    {"t":"address","value":"192.168.1.9"}                    serve the QR on another address
    {"t":"revoke","device":"..."}   {"t":"devices"}   {"t":"stop"}

  here → GUI
    {"t":"started","base":"...","fingerprint":"...","note":"...",
     "addresses":[{"value":"192.168.1.9","where":"this network","current":true}, ...]}
    {"t":"pairing","url":"...","qr":[[0,1,...],...],"expires":N}
    {"t":"ask","id":N,"name":"...","platform":"...","fingerprint":"...","code":"12345","peer":"..."}
    {"t":"paired","device":"...","name":"...","capability":"..."}
    {"t":"devices","items":[...]}     {"t":"connected","count":N}
    {"t":"input","pane":"p1","bytes":"<base64>"}             keys from a phone
    {"t":"compose","pane":"p1","text":"...","route":bool,"origin":"remote:<id>"}   a prompt
    {"t":"error","message":"..."}

Input arrives here as RRP messages and leaves as `input`: the GUI writes the bytes into the pane's
own session, so a phone drives the pane exactly as the keyboard does, and every capability and
password-prompt rule has already been applied before the GUI sees anything.
"""
from __future__ import annotations

import asyncio
import base64
import contextlib
import json
import logging
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from remote import devtls, host as host_mod, identity as identity_mod, panes as panes_mod, wire
from rendezvous.server import Store, build

log = logging.getLogger("relay.gui_host")
APP_DIR = Path(__file__).resolve().parent.parent / "app"


class GuiPaneSource(panes_mod.PaneSource):
    """Panes owned by the GUI. Screen state comes in over stdio; input goes back the same way."""

    def __init__(self, send):
        self.send = send
        self.panes: dict[str, dict] = {}
        self.screens: dict[str, dict] = {}       # last full frame per pane, for a late joiner
        self._panes_callbacks: list = []
        self._agent_callbacks: list = []
        self._screen_callbacks: list = []

    # ---- observation -------------------------------------------------------------------------

    def snapshot(self) -> list[dict]:
        return list(self.panes.values())

    def on_panes(self, callback) -> None:
        self._panes_callbacks.append(callback)

    def on_agent(self, callback) -> None:
        self._agent_callbacks.append(callback)

    def on_screen(self, callback) -> None:
        self._screen_callbacks.append(callback)

    def has_pane(self, pane: str) -> bool:
        return pane in self.panes

    def screen_snapshot(self, pane_id: str) -> dict:
        if pane_id not in self.panes:
            raise wire.WireError("no_such_pane", "no such pane.")
        state = self.screens.get(pane_id)
        if state is None:
            # Nothing has arrived yet; an empty grid is better than an error, and the next frame
            # from the GUI fills it in.
            pane = self.panes[pane_id]
            return {"t": "screen_snapshot", "pane": pane_id, "rows": pane.get("rows", 24),
                    "cols": pane.get("cols", 80), "alt": False,
                    "cursor": {"row": 0, "col": 0, "visible": False, "shape": 0}, "lines": []}
        return {"t": "screen_snapshot", "pane": pane_id, **state}

    # ---- from the GUI ------------------------------------------------------------------------

    def set_pane(self, message: dict) -> None:
        pane_id = message["id"]
        self.panes[pane_id] = {
            "id": pane_id, "window": message.get("window", 1), "tab": message.get("title", ""),
            "title": message.get("title", pane_id), "cwd": message.get("cwd", ""),
            "program": message.get("program", ""), "control": message.get("control", "human"),
            "status": message.get("status", "idle"), "unread": 0, "queue": 0,
            "updated": message.get("updated", 0), "rows": message.get("rows", 24),
            "cols": message.get("cols", 80),
        }
        for callback in list(self._panes_callbacks):
            callback()

    def drop_pane(self, pane_id: str) -> None:
        self.panes.pop(pane_id, None)
        self.screens.pop(pane_id, None)
        for callback in list(self._panes_callbacks):
            callback()

    def set_frame(self, message: dict) -> None:
        pane_id = message.get("pane")
        if pane_id not in self.panes:
            return
        full = bool(message.get("full"))
        lines = message.get("lines", [])
        state = self.screens.get(pane_id)
        if full or state is None:
            state = {"rows": message.get("rows", self.panes[pane_id]["rows"]),
                     "cols": message.get("cols", self.panes[pane_id]["cols"]),
                     "alt": bool(message.get("alt")), "cursor": message.get("cursor", {}),
                     "lines": []}
            rows = {line["row"]: line.get("segs", []) for line in lines}
            state["lines"] = [{"row": row, "segs": rows.get(row, [])}
                              for row in range(state["rows"])]
        else:
            rows = {line["row"]: line.get("segs", []) for line in state["lines"]}
            for line in lines:
                rows[line["row"]] = line.get("segs", [])
            state["lines"] = [{"row": row, "segs": rows.get(row, [])}
                              for row in range(state["rows"])]
            state["cursor"] = message.get("cursor", state["cursor"])
        self.screens[pane_id] = state

        out = {"t": "screen_snapshot" if full else "screen_diff", "pane": pane_id,
               "cursor": message.get("cursor", {}), "lines": lines}
        if full:
            out.update({"rows": state["rows"], "cols": state["cols"], "alt": state["alt"]})
        for callback in list(self._screen_callbacks):
            callback(pane_id, out)

    # ---- input, straight back to the GUI ------------------------------------------------------

    def _pane(self, pane_id: str) -> dict:
        pane = self.panes.get(pane_id)
        if pane is None:
            raise wire.WireError("no_such_pane", "no such pane.")
        if pane.get("status") == "password":
            raise wire.WireError("not_permitted",
                                 "that pane is at a password prompt; ordinary input is refused.")
        return pane

    async def send_keys(self, pane_id: str, data: bytes, *, device: str) -> None:
        self._pane(pane_id)
        self.send({"t": "input", "pane": pane_id, "bytes": base64.b64encode(data).decode(),
                   "device": device})

    async def send_line(self, pane_id: str, text: str, *, device: str) -> None:
        if "\n" in text or "\r" in text:
            raise wire.WireError("unknown_type", "a line may not contain a newline.")
        await self.send_keys(pane_id, text.encode() + b"\n", device=device)

    async def paste(self, pane_id: str, text: str, *, device: str) -> None:
        self._pane(pane_id)
        payload = b"\x1b[200~" + text.encode() + b"\x1b[201~"
        self.send({"t": "input", "pane": pane_id, "bytes": base64.b64encode(payload).decode(),
                   "device": device})

    async def interrupt(self, pane_id: str) -> None:
        await self.send_keys(pane_id, b"\x03", device="owner")

    async def request_snapshot(self, pane_id: str) -> None:
        self.send({"t": "resend", "pane": pane_id})

    def release(self, pane_id: str, device: str) -> None:
        self.send({"t": "release", "pane": pane_id, "device": device})

    def release_device(self, device: str) -> None:
        self.send({"t": "release", "pane": "", "device": device})

    # ---- the agent half belongs to the GUI's own composer --------------------------------------

    async def compose(self, pane: str, text: str, *, to_agent: bool, when: str, origin: str) -> None:
        """A prompt from a client. `route` asks the desktop to decide shell or agent, the way its
        own composer does; without it the text can only reach the agent.

        The host has already applied the rule that routing needs a `full` device: an `agent`
        device's compose always arrives with to_agent set, so it cannot reach the shell.
        """
        self._pane(pane)
        self.send({"t": "compose", "pane": pane, "text": text, "when": when, "origin": origin,
                   "route": not to_agent})

    def agent_event(self, pane: str, event: dict) -> None:
        """A worker event the GUI forwarded. The allow-list in the hub decides what leaves."""
        if pane not in self.panes:
            return
        for callback in list(self._agent_callbacks):
            callback(pane, event)

    async def agent_stop(self, pane: str) -> None:
        self.send({"t": "agent_stop", "pane": pane})

    async def queue_remove(self, pane: str, item_id: str) -> None:
        self.send({"t": "queue_remove", "pane": pane, "item": item_id})

    async def recap_request(self, pane: str) -> None:
        self.send({"t": "recap_request", "pane": pane})

    async def plan_execute(self, pane: str, plan_id: str, origin: str) -> None:
        raise wire.WireError("not_permitted", "plans are not shared yet.")

    async def transcribe(self, pane: str, audio: bytes, audio_format: str) -> str:
        raise wire.WireError("not_permitted", "voice from a phone is not wired up yet.")


class Sidecar:
    def __init__(self):
        self.out_lock = asyncio.Lock()
        self.loop = asyncio.get_event_loop()
        self.source = GuiPaneSource(self.emit)
        self.identity: identity_mod.Identity | None = None
        self.devices: identity_mod.DeviceStore | None = None
        self.host: host_mod.Host | None = None
        self.address = ""
        self.tls_port = 0
        self.base = ""
        self.note = """"""
        self.server = None
        self.store = None
        self.serving: asyncio.Task | None = None
        self.asks: dict[int, asyncio.Future] = {}
        self.next_ask = 0

    # ---- stdio ---------------------------------------------------------------------------------

    def emit(self, message: dict) -> None:
        sys.stdout.write(json.dumps(message, separators=(",", ":")) + "\n")
        sys.stdout.flush()

    async def read_forever(self) -> None:
        reader = asyncio.StreamReader()
        await self.loop.connect_read_pipe(lambda: asyncio.StreamReaderProtocol(reader), sys.stdin)
        while True:
            line = await reader.readline()
            if not line:
                break
            try:
                message = json.loads(line)
            except ValueError:
                continue
            try:
                await self.handle(message)
            except Exception as error:                     # never let one bad line kill the link
                log.exception("handling %s", message.get("t"))
                self.emit({"t": "error", "message": str(error)[:300]})

    # ---- commands ------------------------------------------------------------------------------

    async def handle(self, message: dict) -> None:
        kind = message.get("t")
        if kind == "start":
            await self.start(message)
        elif kind == "pane":
            self.source.set_pane(message)
        elif kind == "unpane":
            self.source.drop_pane(message.get("id", ""))
        elif kind == "frame":
            self.source.set_frame(message)
        elif kind == "agent":
            self.source.agent_event(message.get("pane", ""), message.get("event") or {})
        elif kind == "pair":
            await self.pair()
        elif kind == "address":
            self.set_address(str(message.get("value", "")))
            await self.pair()
        elif kind == "answer":
            future = self.asks.pop(int(message.get("id", -1)), None)
            if future and not future.done():
                future.set_result((bool(message.get("allow")),
                                   message.get("capability", wire.VIEW)))
        elif kind == "devices":
            self.report_devices()
        elif kind == "revoke":
            if self.devices and self.devices.revoke(message.get("device", "")):
                self.report_devices()
        elif kind == "stop":
            await self.stop()

    async def start(self, message: dict) -> None:
        if self.host is not None:
            self.emit({"t": "started", "base": self.base, "fingerprint": self.identity.fingerprint,
                       "note": self.note})
            return
        self.identity = identity_mod.Identity.load_or_create()
        self.devices = identity_mod.DeviceStore()
        self.store = Store(":memory:")
        self.server = build(self.store, static_root=APP_DIR)
        await self.server.start("127.0.0.1", int(message.get("port") or 0))
        local = f"http://127.0.0.1:{self.server.port}"

        self.note = ""
        self.base = local
        self.address = ""
        self.tls_port = 0
        if message.get("tls", True):
            # One listener on every interface; which address goes in the QR is a separate choice,
            # because only the person knows whether the phone is on the Wi-Fi or on the tailnet.
            listener = await self.server.start("0.0.0.0", int(message.get("tls_port") or 0),
                                               ssl_context=devtls.context(identity_mod.state_dir()))
            self.tls_port = self.server.port_of(listener)
            self.address = message.get("address") or devtls.preferred_address()
            self.base = f"https://{self.address}:{self.tls_port}"
            self.note = ("The certificate is self-signed, so your phone warns once. Its SHA-256 "
                         f"begins {devtls.fingerprint(identity_mod.state_dir())}. After you "
                         "accept the warning, scan the code again: some browsers drop the "
                         "pairing code when they reload past it.")

        self.host = host_mod.Host(self.identity, self.devices, self.source, app_base=self.base,
                                  approver=self.ask, name=message.get("name", "this desktop"))
        await self.host.register(local)
        self.serving = asyncio.create_task(self.host.serve())
        for _ in range(100):
            await asyncio.sleep(0.02)
            if self.host.socket is not None:
                break
        self.emit({"t": "started", "base": self.base, "fingerprint": self.identity.fingerprint,
                   "note": self.note, "addresses": self.address_list()})
        self.report_devices()

    def address_list(self) -> list[dict]:
        if not self.tls_port:
            return []
        return [{"value": address, "where": devtls.describe(address),
                 "current": address == self.address}
                for address in devtls.local_addresses()]

    def set_address(self, address: str) -> None:
        """Point the pairing link at another of this machine's addresses."""
        if not self.tls_port or address not in devtls.local_addresses():
            return
        self.address = address
        self.base = f"https://{address}:{self.tls_port}"
        if self.host is not None:
            self.host.app_base = self.base
        self.emit({"t": "started", "base": self.base, "fingerprint": self.identity.fingerprint,
                   "note": self.note, "addresses": self.address_list()})

    async def pair(self) -> None:
        if self.host is None:
            self.emit({"t": "error", "message": "sharing is not running."})
            return
        url, room = await self.host.open_pairing()
        self.emit({"t": "pairing", "url": url, "qr": qr_matrix(url),
                   "expires": room.seconds_left()})

    async def ask(self, request: host_mod.PairRequest) -> tuple[bool, str]:
        """Put the question to the GUI and wait for the dialog's answer."""
        self.next_ask += 1
        ask_id = self.next_ask
        future = self.loop.create_future()
        self.asks[ask_id] = future
        self.emit({"t": "ask", "id": ask_id, "name": request.name, "platform": request.platform,
                   "fingerprint": request.fingerprint, "code": request.code, "peer": request.peer})
        try:
            allowed, capability = await asyncio.wait_for(future, 180)
        except asyncio.TimeoutError:
            self.asks.pop(ask_id, None)
            return False, wire.VIEW
        if allowed:
            self.loop.call_later(0.2, self.report_devices)
        return allowed, capability

    def report_devices(self) -> None:
        if self.devices is None:
            return
        self.emit({"t": "devices", "items": [
            {"id": device.device_id, "name": device.name, "platform": device.platform,
             "capability": device.capability, "fingerprint": device.fingerprint}
            for device in self.devices.live()]})

    async def stop(self) -> None:
        if self.host is not None:
            await self.host.stop()
        if self.serving:
            self.serving.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self.serving
        if self.server is not None:
            await self.server.close()
        if self.store is not None:
            self.store.close()
        self.host = self.server = self.store = self.serving = None
        self.emit({"t": "stopped"})


def qr_matrix(url: str) -> list[list[int]]:
    """The QR as a matrix of 0/1, so the GUI can draw it with the widget toolkit it already has."""
    try:
        import qrcode
    except ImportError:
        return []
    code = qrcode.QRCode(border=2, error_correction=qrcode.constants.ERROR_CORRECT_M)
    code.add_data(url)
    code.make(fit=True)
    return [[1 if cell else 0 for cell in row] for row in code.get_matrix()]


async def main_async() -> int:
    sidecar = Sidecar()
    try:
        await sidecar.read_forever()
    finally:
        await sidecar.stop()
    return 0


def main() -> int:
    logging.basicConfig(level=logging.WARNING, stream=sys.stderr,
                        format="%(levelname)s %(name)s %(message)s")
    try:
        return asyncio.run(main_async())
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
