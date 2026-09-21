# SPDX-License-Identifier: AGPL-3.0-or-later
"""The #SWPH hosted drive's tap on the remote sidecar. It changes nothing the sidecar does.

Relay starts its sidecar as `python3 -m remote.gui_host` in the first directory that holds a
`remote/gui_host.py`, and `RELAY_REMOTE_DIR` is the first it looks in. drive.sh points that at a
folder in the jail whose `remote/` is two files: an `__init__.py` that adds the **real** `remote/`
(the build's own source tree) to the package's path, and this file as `gui_host.py`. So every
module but this one is the real one, and this one loads the real `gui_host.py` by path, wraps three
methods so that they also write a line to `SWPH_TAP_LOG`, and runs the real `main()`.

Why a tap at all: everything between the hub and a phone is inside a Noise session, so "what was
sent to the guest" cannot be read off the network, and the hosted rendezvous only posts a push to
a real push service, so a stubbed subscription's payload cannot be received anywhere either. What
is written down is therefore the hub's own hand-off, a line each:

  {"dir":"in",  "who":…, "kind":"device"|"guest", "cap":…, "t":…, "rid":…, "request":<type>, "card":…}
  {"dir":"out", "who":…, "kind":…, "cap":…, "t":…, "rid":…, "event":<event name>, "code":…, "sent":bool}
  {"dir":"push","endpoint":…, "payload":<base64>, "reply":… | "error":…}
  {"dir":"post","path":"/v1/rooms", "ok":true | "error":…}       every request of the rendezvous

Never a text: not a comment, a title, a reason or a query. `sent` is false when the hub's own
guest scoping dropped the message before the socket (`Host.guest_view` answered None) — for a
guest that is the enforcement point, and the tap asks it the same question the real `send` does.
The push payload is ciphertext (RFC 8291 over the hub's sealed body); run.py opens it with the
stub subscription's private key and the page's own seal key.
"""
import base64
import importlib.util
import json
import os
import sys
import threading
import time

REAL = os.environ["SWPH_REAL_REMOTE"]
LOG = os.environ["SWPH_TAP_LOG"]
_lock = threading.Lock()


def _write(row: dict) -> None:
    row = {"at": round(time.time(), 3), **row}
    with _lock, open(LOG, "a", encoding="utf-8") as log:
        log.write(json.dumps(row) + "\n")


# The real tree's other top-level package (`rendezvous`, the local rendezvous the sidecar embeds).
sys.path.insert(1, os.path.dirname(REAL))
spec = importlib.util.spec_from_file_location("remote._gui_host_real", os.path.join(REAL, "gui_host.py"))
real = importlib.util.module_from_spec(spec)
sys.modules["remote._gui_host_real"] = real
spec.loader.exec_module(real)

from remote import host as host_mod                                        # noqa: E402


def _who(channel) -> dict:
    guest = channel.participant_id is not None
    cap = None
    try:
        cap = channel.role() if guest else channel.capability()
    except Exception:                                                      # noqa: BLE001
        pass
    return {"who": str(channel.who())[:12], "kind": "guest" if guest else "device", "cap": cap}


_send, _dispatch, _push = host_mod.Channel.send, host_mod.Channel._dispatch, host_mod.Host.push_send


async def send(self, message):
    try:
        if isinstance(message, dict) and message.get("t") in ("board_event", "error", "welcome"):
            sent = self.session is not None and not self.closed
            if sent and self.participant_id is not None:
                participant = self.participant
                if participant is None:
                    sent = message.get("t") in ("bye", "error")
                else:
                    sent = self.host.guest_view(participant, message) is not None
            event = message.get("event") if isinstance(message.get("event"), dict) else {}
            row = {"dir": "out", **_who(self), "t": message.get("t"), "sent": bool(sent)}
            if message.get("t") == "board_event":
                row.update(rid=message.get("rid"), event=event.get("event"), code=event.get("code"),
                           card=event.get("card_id"))
            elif message.get("t") == "error":
                row.update(code=message.get("code"), id=message.get("id"), message=message.get("message"))
            else:
                row.update(features=message.get("features"))
            _write(row)
    except Exception as error:                                             # noqa: BLE001
        _write({"dir": "tap-error", "where": "send", "error": repr(error)})
    return await _send(self, message)


async def dispatch(self, message):
    try:
        if isinstance(message, dict) and message.get("t") == "board_request":
            request = message.get("request") if isinstance(message.get("request"), dict) else {}
            _write({"dir": "in", **_who(self), "t": "board_request", "rid": message.get("rid"),
                    "request": request.get("type"), "card": request.get("id"),
                    "msg_id": message.get("msg_id")})
    except Exception as error:                                             # noqa: BLE001
        _write({"dir": "tap-error", "where": "dispatch", "error": repr(error)})
    return await _dispatch(self, message)


async def push_send(self, endpoint, payload, origin=""):
    row = {"dir": "push", "endpoint": endpoint, "origin": origin,
           "payload": base64.b64encode(payload).decode()}
    try:
        reply = await _push(self, endpoint, payload, origin=origin)
    except Exception as error:                                             # noqa: BLE001
        _write({**row, "error": str(error)[:300]})
        raise
    _write({**row, "reply": reply})
    return reply


_post = host_mod.Host._post


async def post(self, path, payload, *, base=None):
    """Every request the hub makes of a rendezvous, by path: the hosted one allows a desktop twenty
    rooms an hour, and a drive that opens the pairing dialog a dozen times needs to know its count."""
    try:
        reply = await _post(self, path, payload, base=base)
    except Exception as error:                                             # noqa: BLE001
        if path != "/v1/push/send":
            _write({"dir": "post", "path": path, "error": str(error)[:200]})
        raise
    if path != "/v1/push/send":
        _write({"dir": "post", "path": path, "ok": True})
    return reply


host_mod.Host._post = post
host_mod.Channel.send = send
host_mod.Channel._dispatch = dispatch
host_mod.Host.push_send = push_send
_write({"dir": "tap", "real": REAL})

if __name__ == "__main__":
    raise SystemExit(real.main())
