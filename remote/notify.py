# SPDX-License-Identifier: GPL-3.0-or-later
"""Deciding to send a push, and what it is allowed to say (docs/REMOTE-PROTOCOL.md section 9).

This is the half of notifications that is policy rather than cryptography. ``remote/push.py`` can
seal and encrypt a body; what stops a phone buzzing every four seconds, and what stops the buzz
from putting a command line on a lock screen, is here.

Three rules, each of which exists because the obvious implementation gets it wrong:

* **The hub constructs every body.** It never forwards a desktop notification, because those
  already interpolate the pane's ``cwd``. A body carries a kind, a pane label and — for the
  password case only, exactly as section 9 words it — the foreground program's name. No command
  text, no output, no prompt text, no ``cwd``, no OSC-derived pane title. A terminal title is set
  by program output and is frequently the command itself (``ssh prod-db``), and these land on a
  lock screen, so the label is a desktop-assigned ordinal instead: "Pane 2".
* **Presence.** No push while the desktop's own window is active and focused; the GUI says which
  it is over the sidecar's ``window_active`` line. A hub nobody told (``remote.cli share``, the
  tests) has no window to be looking at, so it pushes.
* **A per-pane cooldown.** A loop that re-prints a password prompt would otherwise ring the phone
  as fast as the loop runs.

Which kinds are worth a buzz is the fourth rule and the only one the desktop does not decide: the
device says so in ``push_subscribe`` and may say so again at any time.

The subscription — the endpoint, the RFC 8291 content keys and the per-device seal key — arrives
inside the Noise session and is kept on the device record. The rendezvous is never told it; it is
handed an opaque endpoint and ciphertext, and a 404 or 410 from the push service comes back as
``drop``, which is what finally forgets a subscription the phone threw away.
"""
from __future__ import annotations

import asyncio
import ipaddress
import logging
import time
from urllib.parse import urlsplit

from . import identity as identity_mod, push, wire

log = logging.getLogger("relay.notify")

LONG_TURN = 30.0                   # section 9: a turn is only worth a buzz if it outlasted you
PANE_COOLDOWN = 60.0               # one pane may ring the phone once a minute, whatever happens
MAX_ENDPOINT = 2048
P256DH_BYTES = 65
AUTH_BYTES = 16
SEAL_KEY_BYTES = 32

# What a device may ask to be told about, and what it gets if it does not say. Which of these is
# worth a buzz is not a product decision the desktop can make for you — it depends on the phone in
# your pocket and what you are doing today — so the choice is per device and is made **on** the
# device: `push_subscribe` carries `kinds`, and sending it again changes them without the browser
# asking for permission a second time.
KINDS = ("agent_finished", "waiting_input", "password", "failed", "plan")
DEFAULT_KINDS = KINDS            # all five on: a notification you did not want is one tap to turn off


def clean_endpoint(value) -> str:
    """The endpoint a phone says its push service gave it, or a WireError saying why not.

    Section 9.1 checks `https://`, a length and no whitespace. That is not enough on its own:
    the endpoint is a URL the **phone** chooses and the **rendezvous** then makes an
    authenticated POST to, with a body the phone also had a hand in. A `view` device — the
    weakest capability there is — could therefore aim the one process on the hosted side that
    has a network at `https://169.254.169.254/` or at anything else behind it.

    The rendezvous refuses a host that is not a push service (``rendezvous/server.py``), which is
    the boundary that matters because anyone may register a desktop there. This is the earlier,
    cheaper half: a literal address in a range that cannot be a public push service is refused
    here, where the phone gets told, rather than silently stored and refused at delivery. A
    hostname is left alone — resolving it here would mean a DNS lookup on the hub's event loop,
    and a name that resolves inside is the rendezvous's business, not this process's.
    """
    if not isinstance(value, str) or not value.startswith("https://"):
        raise wire.WireError("unknown_type", "the push endpoint must be an https URL.")
    if len(value) > MAX_ENDPOINT or any(character.isspace() for character in value):
        raise wire.WireError("unknown_type", "that push endpoint is not a URL.")
    parts = urlsplit(value)
    if parts.username is not None or parts.password is not None or "@" in (parts.netloc or ""):
        # `https://fcm.googleapis.com@127.0.0.1/x` is a request to 127.0.0.1 that reads like one
        # to Google, and the only reason to write one is to be misread.
        raise wire.WireError("unknown_type", "a push endpoint carries no credentials.")
    host = (parts.hostname or "").strip("[]")
    if not host:
        raise wire.WireError("unknown_type", "that push endpoint names no host.")
    try:
        address = ipaddress.ip_address(host)
    except ValueError:
        return value                     # a name; the rendezvous decides whether it is a service
    if not address.is_global or address.is_multicast:
        raise wire.WireError("unknown_type",
                             "that push endpoint is not a push service on the internet.")
    return value


def clean_subscription(message: dict) -> dict:
    """The stored form of a ``push_subscribe``, or a WireError naming what is wrong.

    Everything is checked: a phone is not trusted to send a sane endpoint, and the two content
    keys and the seal key have exactly one right size each.
    """
    endpoint = clean_endpoint(message.get("endpoint"))
    p256dh = wire.decode_bytes(message.get("p256dh"), 200, "p256dh")
    if len(p256dh) != P256DH_BYTES or p256dh[0] != 0x04:
        raise wire.WireError("unknown_type", "p256dh must be an uncompressed P-256 point.")
    auth = wire.decode_bytes(message.get("auth"), 64, "auth")
    if len(auth) != AUTH_BYTES:
        raise wire.WireError("unknown_type", "auth must be 16 bytes.")
    key = wire.decode_bytes(message.get("key"), 64, "the seal key")
    if len(key) != SEAL_KEY_BYTES:
        raise wire.WireError("unknown_type", "the seal key must be 32 bytes.")
    return {"endpoint": endpoint, "p256dh": push.b64url(p256dh), "auth": push.b64url(auth),
            "key": push.b64url(key), "kinds": clean_kinds(message.get("kinds"))}


def clean_kinds(value) -> list[str]:
    """The kinds a device asked for, in this module's order; an unknown one is refused.

    Absent or empty means the default rather than "nothing": a phone that sends an old client's
    ``push_subscribe`` gets notifications, and a phone that wants none unsubscribes.
    """
    if value is None:
        return list(DEFAULT_KINDS)
    if not isinstance(value, list) or len(value) > len(KINDS):
        raise wire.WireError("unknown_type", "kinds must be a list of notification kinds.")
    wanted = set()
    for kind in value:
        if not isinstance(kind, str) or kind not in KINDS:
            raise wire.WireError("unknown_type",
                                 f"{kind!r} is not a notification kind; "
                                 f"they are {', '.join(KINDS)}.")
        wanted.add(kind)
    return [kind for kind in KINDS if kind in wanted] or list(DEFAULT_KINDS)


def kinds_of(subscription: dict) -> list[str]:
    """What a stored subscription asked for. A record written before `kinds` existed gets all."""
    kinds = subscription.get("kinds")
    return [kind for kind in KINDS if kind in kinds] if isinstance(kinds, list) \
        else list(DEFAULT_KINDS)


def elapsed_text(seconds: float) -> str:
    if seconds >= 3600:
        return f"{int(seconds // 3600)}h {int(seconds % 3600 // 60)}m"
    if seconds >= 60:
        return f"{int(seconds // 60)}m {int(seconds % 60)}s"
    return f"{int(seconds)}s"


class Notifier:
    """What the hub pushes, to whom, and how often.

    ``send`` is the one way out: ``await send(endpoint, payload, origin=device.origin)`` posts
    to the device's own rendezvous (``""`` is the local one) and
    answers ``{"delivered": bool, "status": int | None, "drop": bool}``. Keeping it a callable is
    what lets the triggers, the presence rule and the cooldown be tested without a push service.
    ``spawn`` runs a coroutine the hub will not await: the triggers are called from synchronous
    callbacks, so delivery cannot block the thing that produced the event.
    """

    def __init__(self, devices: identity_mod.DeviceStore, send, spawn=None):
        self.devices = devices
        self.send = send
        self.spawn = spawn or (lambda coroutine: asyncio.ensure_future(coroutine))
        self.active = False                        # the desktop window, per `window_active`
        self._labels: dict[str, str] = {}
        self._status: dict[str, str] = {}
        self._turn_started: dict[str, float] = {}
        self._last_push: dict[str, float] = {}

    # ---- presence and labels -------------------------------------------------------------------

    def window_active(self, active: bool) -> None:
        """The desktop window is (or is no longer) the active, focused one."""
        self.active = bool(active)

    def label(self, pane: str) -> str:
        """A pane's name on a lock screen: an ordinal this desktop assigned, never its title."""
        if pane not in self._labels:
            self._labels[pane] = f"Pane {len(self._labels) + 1}"
        return self._labels[pane]

    # ---- triggers ------------------------------------------------------------------------------

    def on_panes(self, items: list[dict]) -> None:
        """Pane status transitions: waiting for input, a password prompt, a failed turn."""
        seen = set()
        for item in items:
            pane = item.get("id", "")
            if not pane:
                continue
            seen.add(pane)
            status = item.get("status", "")
            before = self._status.get(pane)
            self._status[pane] = status
            if before is None or status == before:
                continue
            if status == "waiting_input":
                self.fire("waiting_input", pane)
            elif status == "failed":
                self.fire("failed", pane)
            elif status == "password":
                # Section 9: the push names the foreground program, and a prompt from something
                # the agent spawned is suppressed — a poisoned repo file must not be able to put
                # a credential prompt on the owner's phone out of context.
                if item.get("control") == "agent":
                    log.info("no password push for %s: the agent has the keyboard", pane)
                    continue
                program = identity_mod.clean_label(item.get("program", ""), 24)
                self.fire("password", pane, program=program)
        for pane in [pane for pane in self._status if pane not in seen]:
            self._status.pop(pane, None)
            self._turn_started.pop(pane, None)

    def on_agent(self, pane: str, event: dict) -> None:
        """Worker events: a turn that outlasted you, a turn that failed, a plan to read."""
        name = event.get("event", "")
        if name == "agent_started":
            self._turn_started[pane] = time.monotonic()
        elif name == "agent_finished":
            started = self._turn_started.pop(pane, None)
            if started is None:
                return
            spent = time.monotonic() - started
            if spent >= LONG_TURN:
                self.fire("agent_finished", pane, spent=spent)
        elif name == "error":
            self._turn_started.pop(pane, None)
            self.fire("failed", pane)
        elif name == "plan_written":
            self.fire("plan", pane)

    # ---- the decision ---------------------------------------------------------------------------

    def fire(self, kind: str, pane: str, **detail) -> None:
        """Push, unless presence or the cooldown says not to. Safe to call from sync code."""
        if self.active:
            return                                  # you are looking at the desktop
        now = time.monotonic()
        if now - self._last_push.get(pane, -PANE_COOLDOWN) < PANE_COOLDOWN:
            return
        # The cooldown is only spent if somebody is actually going to hear this one: a device that
        # switched this kind off must not stop another device hearing the next thing.
        if not any(isinstance(device.push, dict) and kind in kinds_of(device.push)
                   for device in self.devices.live()):
            return
        self._last_push[pane] = now
        self.spawn(self.deliver(self.body(kind, pane, **detail)))

    def body(self, kind: str, pane: str, *, spent: float = 0.0, program: str = "") -> dict:
        """The whole body, constructed here. Read the strings: this is the security boundary."""
        label = self.label(pane)
        if kind == "agent_finished":
            title, text = "The agent finished", f"{label} · {elapsed_text(spent)}"
        elif kind == "waiting_input":
            title, text = "Waiting for you", f"{label} is waiting for input"
        elif kind == "password":
            # The one place a program's name is allowed, because without it the prompt has no
            # context at all and the owner cannot tell a `sudo` they started from one they did not.
            title, text = "Password prompt", f"{label} · {program}" if program else label
        elif kind == "failed":
            title, text = "That turn failed", label
        elif kind == "plan":
            title, text = "A plan is ready", label
        else:
            title, text = "Relay", label
        return {"v": 1, "kind": kind, "pane": pane, "title": title, "body": text}

    async def deliver(self, body: dict) -> None:
        """Seal to each subscribed device, encrypt to its subscription, post to the rendezvous."""
        for device in self.devices.live():
            subscription = device.push
            if not isinstance(subscription, dict):
                continue
            if body.get("kind") not in kinds_of(subscription):
                continue                             # this device switched this one off
            try:
                blob = push.seal(push.unb64url(subscription["key"]), body)
                payload = push.rfc8291_encrypt(push.unb64url(subscription["p256dh"]),
                                               push.unb64url(subscription["auth"]), blob)
            except Exception:                        # a stored subscription that will never work
                log.exception("could not encrypt a push for device %s", device.device_id)
                self.devices.set_push(device.device_id, None)
                continue
            try:
                # Through the rendezvous whose VAPID key this subscription was made under.
                reply = await self.send(subscription["endpoint"], payload,
                                        origin=getattr(device, "origin", ""))
            except Exception as error:
                log.info("push for device %s did not go out: %s", device.device_id, error)
                continue
            if isinstance(reply, dict) and reply.get("drop"):
                # 404 or 410 from the push service: this subscription is gone for good.
                log.info("dropping the subscription for device %s", device.device_id)
                self.devices.set_push(device.device_id, None)
