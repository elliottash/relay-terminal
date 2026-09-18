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
    {"t":"address","value":"192.168.1.9"}                    serve the QR on another address;
                                          the value may also be the tailnet name from the
                                          `addresses` list, which publishes with `tailscale serve`
    {"t":"revoke","device":"..."}   {"t":"devices"}   {"t":"stop"}
    {"t":"password_entry","device":"...","allow":true}   per-device switch (section 6.7)
    {"t":"window_active","active":true}   Relay's own window is (or is not) the focused one:
                                          the presence rule, so no push arrives while you are
                                          looking at the desktop (section 9)
    {"t":"transcribed","pane":"p1","id":"v1","ok":true,"text":"..."}   the answer to a `voice`
                                                            (`ok:false` carries `error` instead)
    {"t":"history_page","pane":"p1","id":"h1","ok":true,"from_row":N,"total":N,"more":b,
     "lines":[<row>]}                                       the answer to a `history`

  GUI → here, multiplayer (section 10.5). Desktop only: every one of these names is in
  wire.OWNER_ONLY, so the same message over the wire from any device is refused.
    {"t":"invite_create","pane":"p1","role":"viewer","expires":86400,"uses":1}  → `invite`
    {"t":"invite_revoke","id":"<invite id>"}
    {"t":"knock_answer","participant":"<id>","admit":true,"role":"viewer"}   the dialog's answer
    {"t":"role_set","participant":"<id>","role":"editor"}
    {"t":"participant_remove","participant":"<id>"}
    {"t":"share_end","pane":"p1"}     {"t":"participants"}
    {"t":"prompt_answer","id":"<prompt id>","approve":true}      the answer to a `prompt_ask`
    {"t":"control_answer","pane":"p1","participant":"<id>","grant":true}   ...to a `control_ask`
    {"t":"control_take","pane":"p1"}    the owner typed in the pane: control comes straight back
    {"t":"control_revoke","pane":"p1"}  the same, from the sharing panel rather than the keyboard
    {"t":"share_pause","pane":"p1","on":true}   pane omitted or "" pauses the whole share
    {"t":"share_options","pane":"p1","prompts_immediate":false,"present_only":true}

  here → GUI
    {"t":"started","base":"...","fingerprint":"...","note":"...",
     "addresses":[{"value":"spark.tail0.ts.net","kind":"tailscale","available":true,
                   "where":"no certificate warning, works from anywhere on your tailnet",
                   "label":"https://spark.tail0.ts.net — no certificate warning, ...",
                   "reason":"","current":true},
                  {"value":"192.168.1.9","kind":"ip","available":true,"where":"this network",
                   "label":"192.168.1.9 — reachable from this network","reason":"",
                   "current":false}, ...]}
                                          Best first. The `tailscale` entry is always there: with
                                          `available:false` and a one-sentence `reason` when this
                                          machine cannot serve a real certificate
    {"t":"pairing","url":"...","qr":[[0,1,...],...],"expires":N}
    {"t":"ask","id":N,"name":"...","platform":"...","fingerprint":"...","code":"12345","peer":"..."}
    {"t":"paired","device":"...","name":"...","capability":"..."}
    {"t":"devices","items":[...]}     {"t":"connected","count":N}
    {"t":"input","pane":"p1","bytes":"<base64>"}             keys from a phone
    {"t":"secret_input","pane":"p1","bytes":"<base64>"}      a password line, nonce already checked
    {"t":"compose","pane":"p1","text":"...","route":bool,"origin":"remote:<id>"}   a prompt
    {"t":"voice","pane":"p1","id":"v1","format":"webm","data":"<base64>"}   a clip to transcribe
    {"t":"history","pane":"p1","id":"h1","before_row":R,"count":N}   a page of the pane's
                                              scrollback, ending just below absolute row R
                                              (R < 0 asks for the newest page)
    {"t":"error","message":"..."}

  here → GUI, multiplayer (section 10.5)
    {"t":"invite","id":"<id>","url":"...","qr":[[0,1,...],...],"role":"viewer",
     "panes":["p1"],"uses":1,"expires":86400}               the link and its QR
    {"t":"knock","participant":"<id>","name":"alice","platform":"Chrome","code":"12345",
     "fingerprint":"AB12 CD34 EF56","peer":"192.0.2.7","role":"viewer","pane":"p1",
     "panes":["p1"],"invite":"<id>"}                        someone at the door; answer with
                                                            `knock_answer` within two minutes
    {"t":"participants","items":[{"id","name","platform","role","panes","invite",
     "fingerprint","expires","online","driving"}],"invites":[{"id","panes","role","uses",
     "expires"}]}
    {"t":"prompt_ask","id":"<prompt id>","participant":"<id>","name":"alice","pane":"p1",
     "text":"the whole prompt","when":"now","plan":""}      a guest's prompt, waiting for you
    {"t":"control_ask","pane":"p1","participant":"<id>","name":"alice"}   ...for the keyboard
    {"t":"control","pane":"p1","holder":"participant:<id>","name":"alice"}   who is driving now;
                                              `holder` is "owner", "agent" or "participant:<id>"
    {"t":"share_state","pane":"p1","paused":true,"reason":"away"}   why guests cannot act:
                                              "owner" (you paused) or "away" (present-only)

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

from remote import devtls, guests as guests_mod, host as host_mod, identity as identity_mod, \
    panes as panes_mod, tailnet as tailnet_mod, terminal as terminal_mod, wire
from rendezvous.server import Store, build

log = logging.getLogger("relay.gui_host")
APP_DIR = Path(__file__).resolve().parent.parent / "app"

# How long a clip may wait for the GUI's answer. Transcription is a network round trip on the
# desktop, so this is generous; what it must never be is unbounded, because the phone is holding
# a spinner open until it hears something.
VOICE_TIMEOUT = 90.0

# How long a knock may sit in the GUI's dialog. The hub applies its own two-minute refusal
# (section 10.2) and this is the same number, so nothing waits on a dialog the hub gave up on.
KNOCK_TIMEOUT = guests_mod.KNOCK_TIMEOUT

# A scrollback page is a read of memory the GUI already holds, on its own thread, so this is a
# wedged-GUI timeout and not a work budget. The phone is holding a scroll gesture open on it.
HISTORY_TIMEOUT = 15.0


class GuiPaneSource(panes_mod.PaneSource):
    """Panes owned by the GUI. Screen state comes in over stdio; input goes back the same way."""

    scrollback = True

    def __init__(self, send):
        self.send = send
        self.panes: dict[str, dict] = {}
        self.screens: dict[str, dict] = {}       # last full frame per pane, for a late joiner
        self._panes_callbacks: list = []
        self._agent_callbacks: list = []
        self._screen_callbacks: list = []
        # Voice clips waiting for the GUI: request id -> (pane, future). The id is minted here,
        # so two clips in flight cannot be answered with each other's text.
        self._voice: dict[str, tuple[str, asyncio.Future]] = {}
        self._voice_seq = 0
        # Scrollback pages waiting for the GUI: request id -> future. Same rule as a clip — the
        # id is minted here, so two devices paging at once cannot be handed each other's page.
        self._history: dict[str, asyncio.Future] = {}
        self._history_seq = 0

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
                    "cols": pane.get("cols", 80), "alt": False, "base": 0, "history": 0,
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
            "shell_pid": message.get("pid", 0), "foreground_pid": message.get("foreground_pid", 0),
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
                     "base": 0, "history": 0, "lines": []}
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
        # Where this screen sits in the scrollback, so a client holding history knows whether its
        # rows still join the live block (section 6.5). Carried on every frame, diff included.
        state["base"] = int(message.get("base", state.get("base", 0)))
        state["history"] = int(message.get("history", state.get("history", 0)))
        self.screens[pane_id] = state

        out = {"t": "screen_snapshot" if full else "screen_diff", "pane": pane_id,
               "cursor": message.get("cursor", {}), "base": state["base"],
               "history": state["history"], "lines": lines}
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

    async def compose(self, pane: str, text: str, *, to_agent: bool, when: str, origin: str,
                      origin_name: str = "") -> None:
        """A prompt from a client. `route` asks the desktop to decide shell or agent, the way its
        own composer does; without it the text can only reach the agent.

        The host has already applied the rule that routing needs a `full` device: an `agent`
        device's compose always arrives with to_agent set, so it cannot reach the shell.

        ``origin_name`` rides along so the queue row can name its author (section 10.4): the
        origin itself is `guest:<id>`, which is what the transcript keeps, and the name is what
        the desktop shows. It crosses this stdio line only — no client is ever sent another
        guest's prompt, with or without a name on it.
        """
        self._pane(pane)
        self.send({"t": "compose", "pane": pane, "text": text, "when": when, "origin": origin,
                   "origin_name": origin_name, "route": not to_agent})

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
        """A clip from a phone, transcribed by the pane's own worker.

        The audio crosses the stdio line to the GUI, which hands it to the same `transcribe`
        request the microphone beside the prompt box uses; the text comes back here and goes to
        the phone that spoke. No transcription key is held in this process and none leaves the
        desktop, which is the whole point of doing it this way round.
        """
        self._pane(pane)
        self._voice_seq += 1
        request = f"v{self._voice_seq}"
        future = asyncio.get_event_loop().create_future()
        self._voice[request] = (pane, future)
        self.send({"t": "voice", "pane": pane, "id": request, "format": audio_format,
                   "data": base64.b64encode(audio).decode()})
        try:
            reply = await asyncio.wait_for(future, VOICE_TIMEOUT)
        except asyncio.TimeoutError:
            raise wire.WireError("unavailable",
                                 "the desktop did not answer that clip in time.") from None
        finally:
            # Every path: a timeout, a cancelled client, or the answer itself.
            self._voice.pop(request, None)
        if not reply.get("ok"):
            # The worker's own words, so the phone reads what the desktop would have shown.
            detail = str(reply.get("error") or reply.get("code") or "")
            raise wire.WireError("unavailable", detail or "the clip could not be transcribed.")
        return str(reply.get("text") or "")

    async def history(self, pane: str, before_row: int, count: int) -> dict:
        """A page of the pane's scrollback, read from the core the GUI already owns.

        The GUI answers from `VtCore::historyLines`, which is const and moves nothing: a phone
        paging back must never scroll the desktop user's own screen. The page comes back through
        the same serializer the live frames use, so history and the live screen cannot drift into
        two shapes.
        """
        self._pane(pane)
        self._history_seq += 1
        request = f"h{self._history_seq}"
        future = asyncio.get_event_loop().create_future()
        self._history[request] = future
        self.send({"t": "history", "pane": pane, "id": request,
                   "before_row": int(before_row), "count": int(count)})
        try:
            reply = await asyncio.wait_for(future, HISTORY_TIMEOUT)
        except asyncio.TimeoutError:
            raise wire.WireError("unavailable",
                                 "the desktop did not answer that page in time.") from None
        finally:
            # Every path: a timeout, a cancelled client, or the answer itself.
            self._history.pop(request, None)
        if not reply.get("ok"):
            raise wire.WireError("unavailable",
                                 str(reply.get("error") or "") or "that pane is no longer shared.")
        return {"from_row": int(reply.get("from_row", 0)), "total": int(reply.get("total", 0)),
                "more": bool(reply.get("more")), "lines": reply.get("lines") or []}

    def history_reply(self, message: dict) -> None:
        """The GUI's answer to a `history` line, matched on the id this process minted.

        Strictly by id: unlike a voice clip there is no sensible "the oldest one waiting on that
        pane", because a phone and an iPad page the same pane at the same time. An id nobody is
        waiting on is dropped.
        """
        future = self._history.get(message.get("id") or "")
        if future is not None and not future.done():
            future.set_result(message)

    def voice_reply(self, message: dict) -> None:
        """The GUI's answer to a `voice` line: `ok` with the text, or the worker's error.

        Matched on the id this process minted, so two clips in flight cannot be answered with
        each other's text. An answer with no id is an older GUI's spelling, and it is taken only
        when exactly one clip is waiting on that pane: guessing between two would hand one
        person's phone a recording of somebody else talking, which is the one mistake a
        transcript must not make. With two waiting, the unaddressed answer is dropped and both
        clips time out, which the phone shows as an error.
        """
        request = message.get("id")
        if isinstance(request, str) and request:
            entry = self._voice.get(request)
        else:
            pane = message.get("pane")
            waiting = [item for item in self._voice.values() if item[0] == pane]
            entry = waiting[0] if len(waiting) == 1 else None
        if entry is None:
            return
        _, future = entry
        if not future.done():
            future.set_result(message)

    # ---- password prompts (section 6.7) ---------------------------------------------------------
    # The GUI's pane message carries the shell's pid, so this sidecar can make the same fresh
    # termios read the desktop makes; the write itself goes back to the GUI, which re-checks
    # once more at the moment it writes — the check the spec insists on.

    def _pids(self, pane_id: str) -> tuple[int, int]:
        pane = self.panes.get(pane_id, {})
        return int(pane.get("shell_pid", 0)), int(pane.get("foreground_pid", 0))

    def secret_state(self, pane: str) -> dict | None:
        shell_pid, foreground_pid = self._pids(pane)
        if not shell_pid or not terminal_mod.secret_prompt(shell_pid):
            return None
        return {"shell_pid": shell_pid, "foreground_pid": foreground_pid}

    def secret_prompt(self, pane: str) -> bool:
        shell_pid, _ = self._pids(pane)
        return bool(shell_pid) and terminal_mod.secret_prompt(shell_pid)

    async def send_secret(self, pane: str, data: bytes, *, device: str) -> None:
        if not self.secret_prompt(pane):
            raise wire.WireError("not_permitted", "that pane is no longer at a password prompt.")
        self.send({"t": "secret_input", "pane": pane,
                   "bytes": base64.b64encode(data).decode(), "device": device})


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
        # The warning-free address (remote/tailnet.py). `found` is what detection said — the name
        # when there is one, the one-sentence reason when there is not — and `served_by_tailscale`
        # says whether `tailscale serve` is up right now, because it has to come down again.
        self.found = tailnet_mod.Tailnet()
        self.served_by_tailscale = False
        self.server = None
        self.store = None
        self.serving: asyncio.Task | None = None
        self.asks: dict[int, asyncio.Future] = {}
        self.next_ask = 0
        # Knocks waiting for the owner's dialog, by the participant id the hub minted for each
        # (section 10.5). Keyed by that id rather than a counter of our own, so the `knock` line,
        # the `knock_answer` that follows and every audit line name the same person.
        self.knocks: dict[str, asyncio.Future] = {}
        # The same pattern for the two questions of 10.3 and 10.4: keyed by the id the hub minted
        # for the prompt, and by (pane, participant) for the keyboard, so an answer cannot be
        # applied to somebody else's question.
        self.prompts: dict[str, asyncio.Future] = {}
        self.controls: dict[tuple[str, str], asyncio.Future] = {}
        # The presence rule (section 9). Until the GUI says otherwise this process assumes the
        # window is not the focused one, which is the safe default: a missed push is worse than
        # one you did not need.
        self.window_active = False

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
        elif kind == "transcribed":
            self.source.voice_reply(message)
        elif kind == "history_page":
            self.source.history_reply(message)
        elif kind == "pair":
            await self.pair()
        elif kind == "address":
            await self.set_address(str(message.get("value", "")))
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
        elif kind == "window_active":
            self.window_active = bool(message.get("active"))
            if self.host is not None:
                self.host.window_active(self.window_active)
        elif kind == "password_entry":
            if self.devices:
                self.devices.set_password_entry(message.get("device", ""),
                                                bool(message.get("allow")))
                self.report_devices()
        # ---- multiplayer, section 10.5. Every one of these is desktop-only: the same names are
        # in wire.OWNER_ONLY, so the identical message arriving over the wire from any device —
        # the owner's own phone included — is refused `not_permitted` before it is dispatched.
        elif kind == "invite_create":
            await self.invite_create(message)
        elif kind == "invite_revoke":
            if self.host is not None and self.host.invite_revoke(str(message.get("id", ""))):
                self.report_participants()
        elif kind == "knock_answer":
            future = self.knocks.pop(str(message.get("participant", "")), None)
            if future is not None and not future.done():
                future.set_result((bool(message.get("admit")),
                                   str(message.get("role") or wire.VIEWER)))
        elif kind == "role_set":
            if self.host is not None:
                await self.host.role_set(str(message.get("participant", "")),
                                         str(message.get("role") or wire.VIEWER))
                self.report_participants()
        elif kind == "participant_remove":
            if self.host is not None:
                await self.host.participant_remove(str(message.get("participant", "")))
                self.report_participants()
        elif kind == "share_end":
            if self.host is not None:
                await self.host.share_end(str(message.get("pane", "")))
                self.report_participants()
        elif kind == "participants":
            self.report_participants()
        elif kind == "prompt_answer":
            future = self.prompts.pop(str(message.get("id", "")), None)
            if future is not None and not future.done():
                future.set_result(bool(message.get("approve")))
            elif self.host is not None:
                # The dialog answered something this process is no longer waiting on — it was
                # restarted, or the answer came from the panel rather than the dialog. The hub
                # still knows the prompt, so apply it there.
                self.host.decide_prompt(str(message.get("id", "")), bool(message.get("approve")))
        elif kind == "control_answer":
            key = (str(message.get("pane", "")), str(message.get("participant", "")))
            future = self.controls.pop(key, None)
            if future is not None and not future.done():
                future.set_result(bool(message.get("grant")))
        elif kind == "control_take":
            if self.host is not None:
                self.host.take_control(str(message.get("pane", "")))
        elif kind == "control_revoke":
            if self.host is not None:
                self.host.revoke_control(str(message.get("pane", "")))
        elif kind == "share_pause":
            if self.host is not None:
                self.host.share_pause(str(message.get("pane") or ""), bool(message.get("on")))
        elif kind == "share_options":
            if self.host is not None:
                changes = {name: bool(message[name]) for name in
                           ("prompts_immediate", "present_only") if name in message}
                self.host.set_share_options(str(message.get("pane") or ""), **changes)
        # pane_state (relay-terminal-71): a pane's state for the phone, and the text of a row a
        # phone took back to edit (docs/REMOTE-PROTOCOL.md section 16). The hub cleans both.
        elif kind in ("pane_state", "queue_edit_text"):
            if self.host is not None:
                self.host.pane_state_from_gui(message)
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
        self.served_by_tailscale = False
        # Detection is two `tailscale` calls, so it happens off the loop: a wedged CLI must not
        # hold the share button down.
        self.found = await asyncio.to_thread(tailnet_mod.probe)
        if message.get("tls", True):
            # One listener on every interface; which address goes in the QR is a separate choice,
            # because only the person knows whether the phone is on the Wi-Fi or on the tailnet.
            listener = await self.server.start("0.0.0.0", int(message.get("tls_port") or 0),
                                               ssl_context=devtls.context(identity_mod.state_dir()))
            self.tls_port = self.server.port_of(listener)
            self.address = message.get("address") or devtls.preferred_address()
            self.base = f"https://{self.address}:{self.tls_port}"
            self.note = self.self_signed_note()
        # A real certificate beats a warning, so it is what the QR gets whenever there is one. An
        # address the GUI remembered is honoured instead: the person chose it last time.
        if self.found.ready and not message.get("address"):
            await self.use_tailnet()

        self.host = host_mod.Host(self.identity, self.devices, self.source, app_base=self.base,
                                  approver=self.ask, name=message.get("name", "this desktop"),
                                  knock_approver=self.knock, prompt_approver=self.prompt,
                                  control_approver=self.control)
        self.host.on_control(lambda pane, holder, name: self.emit(
            {"t": "control", "pane": pane, "holder": holder, "name": name}))
        self.host.on_share_state(lambda pane, paused, reason: self.emit(
            {"t": "share_state", "pane": pane, "paused": paused, "reason": reason}))
        # One `window_active` signal, two readers: the notification presence rule of section 9 and
        # "guests can act only while I am present" (10.5). The hub passes it on to the notifier.
        self.host.window_active(self.window_active)
        await self.host.register(local)
        self.serving = asyncio.create_task(self.host.serve())
        for _ in range(100):
            await asyncio.sleep(0.02)
            if self.host.socket is not None:
                break
        self.emit({"t": "started", "base": self.base, "fingerprint": self.identity.fingerprint,
                   "note": self.note, "addresses": self.address_list()})
        self.report_devices()

    def self_signed_note(self) -> str:
        return ("The certificate is self-signed, so your phone warns once. Its SHA-256 "
                f"begins {devtls.fingerprint(identity_mod.state_dir())}. After you "
                "accept the warning, scan the code again: some browsers drop the "
                "pairing code when they reload past it.")

    TAILNET_WHERE = "no certificate warning, works from anywhere on your tailnet"

    def address_list(self) -> list[dict]:
        """What the share dialog offers, best first.

        The tailnet **name** is always the first entry — as something to choose when tailscale can
        serve it, and as one sentence saying why not when it cannot, because "there is no such
        option" and "you have not run one command yet" look identical otherwise. Then this
        machine's own addresses behind the self-signed certificate: the LAN one, which is what a
        phone on the same Wi-Fi wants, and the tailnet IP last.
        """
        if self.found.ready:
            entries = [{"value": self.found.name, "kind": "tailscale", "available": True,
                        "where": self.TAILNET_WHERE, "reason": "",
                        "label": f"{self.found.url} — {self.TAILNET_WHERE}",
                        "current": self.served_by_tailscale}]
        else:
            entries = [{"value": "", "kind": "tailscale", "available": False,
                        "where": self.TAILNET_WHERE, "reason": self.found.reason,
                        "label": "", "current": False}]
        if not self.tls_port:
            return entries
        entries.extend(
            {"value": address, "kind": "ip", "available": True,
             "where": devtls.describe(address), "reason": "",
             "label": f"{address} — reachable from {devtls.describe(address)}",
             "current": not self.served_by_tailscale and address == self.address}
            for address in devtls.local_addresses())
        return entries

    def announce(self) -> None:
        if self.host is not None:
            self.host.app_base = self.base
        self.emit({"t": "started", "base": self.base, "fingerprint": self.identity.fingerprint,
                   "note": self.note, "addresses": self.address_list()})

    async def use_tailnet(self) -> bool:
        """Put the app behind `tailscale serve`, and point the pairing link at that origin.

        Serve terminates TLS itself and proxies to a plain http port, so what goes behind it is the
        loopback listener, never the self-signed one. Same routes, same CSP, same `/pair` and
        `/join`; the Noise session is end-to-end above all of it and does not notice.
        """
        if self.server is None:
            return False
        url, said = await asyncio.to_thread(tailnet_mod.publish, self.server.port)
        if url is None:
            # Detection said yes and serve said no — the admin console is the usual reason. Keep
            # the name, carry tailscale's own words as the reason the picker shows.
            self.found = tailnet_mod.Tailnet(name=self.found.name, reason=said)
            self.served_by_tailscale = False
            return False
        self.served_by_tailscale = True
        self.address = self.found.name
        self.base = url
        self.note = ("Tailscale serves this with a real certificate, so your phone shows no "
                     "warning — and notifications can be turned on, which a browser refuses "
                     "behind a certificate it did not like.")
        return True

    async def drop_tailnet(self) -> None:
        if not self.served_by_tailscale:
            return
        await asyncio.to_thread(tailnet_mod.unpublish)
        self.served_by_tailscale = False

    async def set_address(self, address: str) -> None:
        """Point the pairing link at another of this machine's addresses."""
        if self.server is None:
            return
        if address and address == self.found.name:
            if not self.served_by_tailscale:
                await self.use_tailnet()
            self.announce()
            return
        if not self.tls_port or address not in devtls.local_addresses():
            return
        await self.drop_tailnet()
        self.address = address
        self.base = f"https://{address}:{self.tls_port}"
        self.note = self.self_signed_note()
        self.announce()

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

    # ---- multiplayer (section 10.5) --------------------------------------------------------------

    async def invite_create(self, message: dict) -> None:
        """``invite_create {pane, role, expires, uses}`` → ``invite {id, url, qr}``."""
        if self.host is None:
            self.emit({"t": "error", "message": "sharing is not running."})
            return
        panes = message.get("panes")
        if not isinstance(panes, list):
            panes = [message.get("pane", "")]
        try:
            expires = float(message.get("expires") or 0) or guests_mod.DEFAULT_EXPIRY
            invite, url = await self.host.invite_create(
                [str(pane) for pane in panes if pane],
                str(message.get("role") or wire.VIEWER),
                expires_in=expires, uses=int(message.get("uses") or 1))
        except wire.WireError as error:
            self.emit({"t": "error", "message": error.message})
            return
        self.emit({"t": "invite", "id": invite.invite_id, "url": url, "qr": qr_matrix(url),
                   "role": invite.role, "panes": invite.panes, "uses": invite.uses_left,
                   "expires": invite.seconds_left()})

    async def knock(self, request: host_mod.KnockRequest) -> tuple[bool, str]:
        """``knock {participant, name, platform, code, role, pane}`` → ``knock_answer``.

        The same shape as :meth:`ask` for pairing: the question goes to the GUI and this waits.
        No answer is a refusal — the hub's own two-minute timeout closes the door either way.
        """
        future = self.loop.create_future()
        self.knocks[request.participant] = future
        self.emit({"t": "knock", "participant": request.participant, "name": request.name,
                   "platform": request.platform, "fingerprint": request.fingerprint,
                   "code": request.code, "peer": request.peer, "role": request.role,
                   "pane": request.panes[0] if request.panes else "", "panes": request.panes,
                   "invite": request.invite})
        try:
            admit, role = await asyncio.wait_for(future, KNOCK_TIMEOUT)
        except asyncio.TimeoutError:
            self.knocks.pop(request.participant, None)
            return False, wire.VIEWER
        if admit:
            self.loop.call_later(0.2, self.report_participants)
        return admit, role

    async def prompt(self, request: host_mod.PromptRequest) -> bool:
        """``prompt_ask {id, participant, name, pane, text}`` → ``prompt_answer {id, approve}``.

        The owner sees the guest's name and the whole text (section 10.4). No answer is not an
        approval: the hub's own ten-minute lapse refuses it, and so does this.
        """
        future = self.loop.create_future()
        self.prompts[request.prompt_id] = future
        self.emit({"t": "prompt_ask", "id": request.prompt_id, "participant": request.participant,
                   "name": request.name, "pane": request.pane, "text": request.text,
                   "when": request.when, "plan": request.plan_id})
        try:
            return bool(await future)
        finally:
            self.prompts.pop(request.prompt_id, None)

    async def control(self, request: host_mod.ControlRequest) -> bool:
        """``control_ask {pane, participant, name}`` → ``control_answer {pane, participant,
        grant}``. The hub gives up after a minute (section 10.3) and this waits on it."""
        key = (request.pane, request.participant)
        future = self.loop.create_future()
        self.controls[key] = future
        self.emit({"t": "control_ask", "pane": request.pane, "participant": request.participant,
                   "name": request.name})
        try:
            return bool(await future)
        finally:
            self.controls.pop(key, None)

    def report_participants(self) -> None:
        """``participants {items}``: who is on which pane, for the sharing dialog."""
        if self.host is None:
            return
        online = {channel.participant_id for channel in self.host.channels.values()
                  if channel.participant_id and not channel.closed}
        self.emit({"t": "participants", "items": [
            {"id": p.participant_id, "name": p.name, "platform": p.platform, "role": p.role,
             "panes": p.panes, "invite": p.invite, "fingerprint": p.fingerprint,
             "expires": round(p.expires, 3), "online": p.participant_id in online,
             "driving": [pane for pane in p.panes
                         if self.host.control_holder(pane) == p.participant_id]}
            for p in self.host.guests.live()],
            "invites": [
            {"id": invite.invite_id, "panes": invite.panes, "role": invite.role,
             "uses": invite.uses_left, "expires": invite.seconds_left()}
            for invite in self.host.guests.live_invites()]})

    def report_devices(self) -> None:
        if self.devices is None:
            return
        self.emit({"t": "devices", "items": [
            {"id": device.device_id, "name": device.name, "platform": device.platform,
             "capability": device.capability, "fingerprint": device.fingerprint,
             "password_entry": device.password_entry}
            for device in self.devices.live()]})

    async def stop(self) -> None:
        # Before anything else: a `tailscale serve` route left behind would go on answering for a
        # port nothing is listening on.
        await self.drop_tailnet()
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
