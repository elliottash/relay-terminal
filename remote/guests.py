# SPDX-License-Identifier: GPL-3.0-or-later
"""Guests: invites, knocking, and the people this desktop is not owned by.

docs/REMOTE-PROTOCOL.md section 10. An invite is a bare unguessable link; a guest who knocks with
it and is admitted becomes a **participant**, known by the device key pinned at that moment and
scoped to the panes the invite named.

**A participant record is never a device record**, and this file is where that is structural
rather than promised:

* the two live in different files (``guests.json`` beside ``devices.json``), in different classes,
  and neither store has a method that produces the other's record;
* the field names differ, so a row of one kind cannot be loaded as the other: both loaders build
  their dataclass with ``**entry``, so a device row handed to :class:`GuestStore` — or a
  participant row handed to ``DeviceStore`` — raises ``TypeError`` and is dropped, rather than
  being half-read into something that then answers ``by_key``;
* :meth:`GuestStore.admit` refuses to pin a key the device store already knows, and the hub looks
  a handshake's static key up in the device store **first**, so one key is a device *or* a
  participant and never both;
* nothing here can raise a participant to a capability. A participant has a ``role`` out of
  ``wire.GUEST_ROLES``; there is no code path from a role to ``view``/``agent``/``full``, and the
  hub's capability checks read ``Channel.device``, which is ``None`` on a participant's channel.

The invite secret is never stored: only ``sha256`` of it is, and the comparison is constant time.
The plaintext exists once, in the link the owner hands out. That means a restarted desktop can
still admit a guest holding the link but can no longer re-display the link itself, which is the
right way round.
"""
from __future__ import annotations

import hmac
import json
import logging
import os
import secrets
import time
from dataclasses import asdict, dataclass, field
from hashlib import sha256
from pathlib import Path

from . import identity as identity_mod, wire

log = logging.getLogger("relay.guests")

SECRET_BYTES = 16                    # 128 bits, as pairing's
DEFAULT_EXPIRY = 24 * 3600           # section 10.2: a day
MAX_EXPIRY = 7 * 86400               # capped at a week
MAX_ATTEMPTS = 5                     # five wrong secrets burn the invite
MAX_PANES_PER_INVITE = 8
MAX_USES = 32

KNOCKS_PER_MINUTE = 5                # per invite (section 10.2)
MAX_WAITING_KNOCKS = 3               # at once, per invite
KNOCK_TIMEOUT = 120.0                # no answer in two minutes is a refusal

PROMPT_LIFETIME = 600.0              # section 10.4: a pending prompt lapses after ten minutes
PROMPTS_PER_GUEST = 3
CONTROL_LIFETIME = 60.0              # section 10.3: a control request lapses after a minute


def hash_secret(secret: bytes) -> str:
    """What is stored for an invite. Domain-separated so it cannot be confused with any other
    digest of the same bytes elsewhere in the protocol."""
    return sha256(b"RRP/1 invite secret" + secret).hexdigest()


# ---- invites ----------------------------------------------------------------------------------

@dataclass
class Invite:
    """One invite link: which panes, which role, until when, and how many joins are left."""
    invite_id: str
    panes: list[str]
    role: str
    expires: float                      # absolute, seconds since the epoch
    uses_left: int
    secret_hash: str                    # never the secret itself
    room: str = ""                      # the rendezvous room the link points at
    created: float = field(default_factory=time.time)
    attempts: int = 0                   # wrong secrets so far
    burned: bool = False                # revoked, or five wrong secrets

    @property
    def expired(self) -> bool:
        return time.time() >= self.expires

    @property
    def dead(self) -> bool:
        return self.burned or self.expired or self.uses_left <= 0

    def seconds_left(self) -> int:
        return max(0, int(self.expires - time.time()))

    def check(self, secret: bytes) -> bool:
        """Constant-time. A knock consumes nothing; five wrong secrets burn the invite."""
        if self.dead:
            return False
        if not hmac.compare_digest(self.secret_hash, hash_secret(secret)):
            self.attempts += 1
            if self.attempts >= MAX_ATTEMPTS:
                self.burned = True
            return False
        return True

    def consume(self) -> None:
        """Admission takes one use. A knock alone never reaches here."""
        self.uses_left = max(0, self.uses_left - 1)


# ---- participants -----------------------------------------------------------------------------

@dataclass
class Participant:
    """Someone admitted to a share. Deliberately *not* shaped like ``identity.Device``: the field
    names differ so neither store can read the other's rows (see the module docstring)."""
    participant_id: str
    name: str
    platform: str
    guest_key: str                      # base64url of the pinned static key
    role: str
    panes: list[str]
    invite: str                         # the invite id they came in on
    expires: float
    created: float = field(default_factory=time.time)
    last_seen: float = 0.0
    removed: bool = False

    @property
    def key_bytes(self) -> bytes:
        from . import pairing
        return pairing.un64(self.guest_key)

    @property
    def fingerprint(self) -> str:
        from . import pairing
        return pairing.fingerprint(self.key_bytes)

    @property
    def expired(self) -> bool:
        return time.time() >= self.expires

    @property
    def live(self) -> bool:
        return not self.removed and not self.expired

    def may_see(self, pane: str) -> bool:
        return pane in self.panes


# ---- the store --------------------------------------------------------------------------------

class GuestStore:
    """Invites and participants, in ``guests.json`` beside ``devices.json``.

    Same file discipline as the device list: 0600 inside a 0700 directory, written to a temporary
    file and renamed, so a half-written list never replaces a good one.
    """

    def __init__(self, directory: Path | None = None,
                 devices: identity_mod.DeviceStore | None = None):
        self.directory = Path(directory) if directory else identity_mod.state_dir()
        self.directory.mkdir(parents=True, exist_ok=True)
        self.directory.chmod(0o700)
        self.path = self.directory / "guests.json"
        self.devices = devices
        self.invites: dict[str, Invite] = {}
        self.participants: dict[str, Participant] = {}
        self._on_change: list = []
        # Rate-limiting state, deliberately memory-only: a restart is not a way to get more knocks
        # in, because the process that was counting is the process that was being knocked at.
        self._knocks: dict[str, list[float]] = {}
        self._waiting: dict[str, int] = {}
        self.load()

    def on_change(self, callback) -> None:
        """Called with a participant id whose record changed in a way a live session must feel:
        a role change, a removal, or the end of a share."""
        self._on_change.append(callback)

    def _notify(self, participant_id: str) -> None:
        for callback in list(self._on_change):
            try:
                callback(participant_id)
            except Exception:
                log.exception("guest change callback failed for %s", participant_id)

    # ---- storage -----------------------------------------------------------------------------

    def load(self) -> None:
        if not self.path.is_file():
            return
        try:
            raw = json.loads(self.path.read_text())
        except ValueError:
            return
        for entry in raw.get("invites", []):
            try:
                invite = Invite(**entry)
            except TypeError:
                continue                 # not an invite row; never guessed at
            self.invites[invite.invite_id] = invite
        for entry in raw.get("participants", []):
            try:
                participant = Participant(**entry)
            except TypeError:
                continue                 # a device row cannot become a participant here
            self.participants[participant.participant_id] = participant
        self.prune()

    def save(self) -> None:
        payload = {"invites": [asdict(invite) for invite in self.invites.values()],
                   "participants": [asdict(p) for p in self.participants.values()]}
        temporary = self.directory / "guests.json.tmp"
        # 0600 from the moment it exists, rather than chmod-ed afterwards: the window between the
        # two is when the other accounts on the machine would get to read it.
        handle = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(handle, "w", encoding="utf-8") as out:
            out.write(json.dumps(payload, indent=2))
        os.replace(temporary, self.path)

    # ---- invites -----------------------------------------------------------------------------

    def create_invite(self, panes: list[str], role: str, room: str, *,
                      expires_in: float = DEFAULT_EXPIRY, uses: int = 1) -> tuple[Invite, bytes]:
        """Mint an invite and its one-and-only plaintext secret, which the caller puts in the URL."""
        if role not in wire.GUEST_ROLES:
            raise wire.WireError("not_permitted", f"{role!r} is not a role an invite may grant.")
        wanted = [pane for pane in panes if isinstance(pane, str) and pane][:MAX_PANES_PER_INVITE]
        if not wanted:
            raise wire.WireError("no_such_pane", "an invite has to name a pane.")
        lifetime = max(60.0, min(float(expires_in or DEFAULT_EXPIRY), MAX_EXPIRY))
        secret = secrets.token_bytes(SECRET_BYTES)
        invite = Invite(invite_id=secrets.token_hex(8), panes=wanted, role=role,
                        expires=time.time() + lifetime,
                        uses_left=max(1, min(int(uses or 1), MAX_USES)),
                        secret_hash=hash_secret(secret), room=room)
        self.invites[invite.invite_id] = invite
        self.save()
        return invite, secret

    def invite(self, invite_id: str) -> Invite | None:
        return self.invites.get(invite_id)

    def invite_by_room(self, room: str) -> Invite | None:
        """The live invite a rendezvous room belongs to. A room with no live invite is not an
        invite channel, which is what keeps a burned link from reaching the knock handler."""
        if not room:
            return None
        for invite in self.invites.values():
            if invite.room == room and not invite.dead:
                return invite
        return None

    def live_invites(self) -> list[Invite]:
        return [invite for invite in self.invites.values() if not invite.dead]

    def invites_for_pane(self, pane: str) -> list[Invite]:
        return [invite for invite in self.invites.values() if pane in invite.panes]

    def burn_invite(self, invite_id: str) -> bool:
        invite = self.invites.get(invite_id)
        if invite is None or invite.burned:
            return False
        invite.burned = True
        self.save()
        return True

    # ---- knock budget ------------------------------------------------------------------------

    def may_knock(self, invite_id: str) -> bool:
        """Five knocks a minute per invite, and at most three waiting for an answer at once."""
        now = time.monotonic()
        recent = [at for at in self._knocks.get(invite_id, []) if at > now - 60]
        if len(recent) >= KNOCKS_PER_MINUTE or self._waiting.get(invite_id, 0) >= MAX_WAITING_KNOCKS:
            self._knocks[invite_id] = recent
            return False
        recent.append(now)
        self._knocks[invite_id] = recent
        return True

    def waiting(self, invite_id: str, delta: int) -> None:
        self._waiting[invite_id] = max(0, self._waiting.get(invite_id, 0) + delta)

    # ---- participants ------------------------------------------------------------------------

    def unique_name(self, name: str) -> str:
        """``clean_label`` first, then a second "alice" becomes "alice (2)" (section 10.2)."""
        base = identity_mod.clean_label(name)
        taken = {p.name for p in self.participants.values() if p.live}
        if base not in taken:
            return base
        for number in range(2, 100):
            candidate = f"{base} ({number})"
            if candidate not in taken:
                return candidate
        return f"{base} ({secrets.token_hex(2)})"

    def admit(self, invite: Invite, public_key: bytes, name: str, platform: str, role: str,
              participant_id: str | None = None) -> Participant:
        """Pin a guest. Admission consumes one use of the invite; a knock alone consumed nothing.

        The key is refused if the device store already knows it: a paired device stays a device,
        and a participant never becomes one. That is the promotion this store cannot perform.
        """
        from . import pairing
        if role not in wire.GUEST_ROLES:
            raise wire.WireError("not_permitted", f"{role!r} is not a role a guest may hold.")
        if not wire.role_allows(invite.role, role):
            # The owner may admit below the invite's role, never above it.
            raise wire.WireError("not_permitted", "that is more than the invite offers.")
        if self.devices is not None and self.devices.by_key(public_key) is not None:
            raise wire.WireError("not_permitted",
                                 "that key is already a paired device; it cannot also be a guest.")
        stored = pairing.b64(public_key)
        for existing in self.participants.values():
            if existing.guest_key == stored and existing.live:
                raise wire.WireError("not_permitted", "that key is already a participant here.")
        invite.consume()
        participant = Participant(participant_id=participant_id or secrets.token_hex(8),
                                  name=self.unique_name(name),
                                  platform=identity_mod.clean_label(platform, 24),
                                  guest_key=stored, role=role, panes=list(invite.panes),
                                  invite=invite.invite_id,
                                  expires=min(invite.expires, time.time() + MAX_EXPIRY))
        self.participants[participant.participant_id] = participant
        self.save()
        return participant

    def participant(self, participant_id: str) -> Participant | None:
        """The live record, or None. Expired and removed participants are not returned, so every
        caller that reads through this gets the current answer rather than a cached one."""
        participant = self.participants.get(participant_id)
        return participant if participant is not None and participant.live else None

    def by_key(self, public_key: bytes) -> Participant | None:
        from . import pairing
        wanted = pairing.b64(public_key)
        for participant in self.participants.values():
            if participant.guest_key == wanted and participant.live:
                return participant
        return None

    def any_by_key(self, public_key: bytes) -> Participant | None:
        """Including expired records, so a guest whose access ended can be told why rather than
        having the socket shut in their face. Never used to authorise anything."""
        from . import pairing
        wanted = pairing.b64(public_key)
        return next((p for p in self.participants.values() if p.guest_key == wanted), None)

    def live(self) -> list[Participant]:
        return [p for p in self.participants.values() if p.live]

    def on_pane(self, pane: str) -> list[Participant]:
        """Everyone admitted to one pane. This is how the hub enumerates who is on a share."""
        return [p for p in self.participants.values() if p.live and p.may_see(pane)]

    def set_role(self, participant_id: str, role: str) -> bool:
        participant = self.participants.get(participant_id)
        if participant is None or role not in wire.GUEST_ROLES:
            return False
        participant.role = role
        self.save()
        self._notify(participant_id)
        return True

    def remove(self, participant_id: str) -> Participant | None:
        """Remove a participant and burn the invite they came in on (section 10.5).

        The row is kept, marked ``removed``, until its own expiry passes and :meth:`prune` drops
        it. It grants nothing — every read goes through :meth:`participant` or :meth:`by_key`,
        which return only live records — and it is what lets a guest who reconnects from a phone
        that was asleep be *told* their access ended instead of having the socket shut on them.
        The row holds a name, a platform and a public key, all of which the guest gave us.
        """
        participant = self.participants.get(participant_id)
        if participant is None:
            return None
        participant.removed = True
        participant.panes = []
        if participant.invite in self.invites:
            self.invites[participant.invite].burned = True
        self.prune()
        self.save()
        self._notify(participant_id)
        return participant

    def end_share(self, pane: str) -> list[Participant]:
        """End a pane's share: burn its invites, cut its participants loose, say who was on it.

        A participant scoped to more than one pane keeps the others; only this pane leaves their
        record, and one with nothing left is removed as above.
        """
        for invite in self.invites_for_pane(pane):
            invite.burned = True
        gone: list[Participant] = []
        for participant in list(self.participants.values()):
            if not participant.live or not participant.may_see(pane):
                continue
            participant.panes = [p for p in participant.panes if p != pane]
            if not participant.panes:
                participant.removed = True
            gone.append(participant)
        self.prune()
        self.save()
        for participant in gone:
            self._notify(participant.participant_id)
        return gone

    def prune(self) -> None:
        """Forget removed rows once their own expiry has passed, and invites nothing points at."""
        for participant in list(self.participants.values()):
            if participant.removed and participant.expired:
                self.participants.pop(participant.participant_id, None)
        wanted = {p.invite for p in self.participants.values()}
        for invite in list(self.invites.values()):
            if invite.dead and invite.expired and invite.invite_id not in wanted:
                self.invites.pop(invite.invite_id, None)

    def touch(self, participant_id: str) -> None:
        participant = self.participants.get(participant_id)
        if participant is not None:
            participant.last_seen = time.time()


# ---- what the next agent fills in ---------------------------------------------------------------
# Two queues with nothing draining them but expiry. The hub parks a guest's prompt or control
# request here and answers `prompt_pending` / `control_pending`; approving one, and handing over
# the keyboard, is the next piece of section 10 (10.3 and 10.4) and replaces the bodies of
# ``Host.ask_owner_about_prompt`` and ``Host.ask_owner_about_control``, not this plumbing.

@dataclass
class PendingPrompt:
    prompt_id: str
    participant: str
    pane: str
    text: str
    when: str                            # "now" or "queue", as the guest asked
    created: float
    expires: float
    plan_id: str = ""                    # a guest's plan_execute is treated as a prompt (10.4)


@dataclass
class PendingControl:
    pane: str
    participant: str
    created: float
    expires: float


class PromptQueue:
    """Guest prompts waiting for the owner (section 10.4).

    ``now`` is injected so the lapse can be tested without sleeping for ten minutes.
    """

    def __init__(self, now=time.time, lifetime: float = PROMPT_LIFETIME,
                 per_guest: int = PROMPTS_PER_GUEST):
        self.now = now
        self.lifetime = lifetime
        self.per_guest = per_guest
        self.pending: dict[str, PendingPrompt] = {}

    def expire(self) -> list[PendingPrompt]:
        """Drop everything past its ten minutes and say what went. The only thing that drains
        this queue until the approval flow exists."""
        now = self.now()
        lapsed = [item for item in self.pending.values() if item.expires <= now]
        for item in lapsed:
            self.pending.pop(item.prompt_id, None)
        return lapsed

    def park(self, participant: str, pane: str, text: str, *, when: str = "now",
             plan_id: str = "") -> PendingPrompt:
        self.expire()
        mine = [item for item in self.pending.values() if item.participant == participant]
        if len(mine) >= self.per_guest:
            raise wire.WireError("busy", "you already have prompts waiting to be approved.")
        now = self.now()
        item = PendingPrompt(prompt_id=secrets.token_hex(8), participant=participant, pane=pane,
                             text=text, when=when, created=now, expires=now + self.lifetime,
                             plan_id=plan_id)
        self.pending[item.prompt_id] = item
        return item

    def get(self, prompt_id: str) -> PendingPrompt | None:
        self.expire()
        return self.pending.get(prompt_id)

    def drop(self, prompt_id: str) -> PendingPrompt | None:
        return self.pending.pop(prompt_id, None)

    def for_participant(self, participant: str) -> list[PendingPrompt]:
        self.expire()
        return [item for item in self.pending.values() if item.participant == participant]

    def for_pane(self, pane: str) -> list[PendingPrompt]:
        self.expire()
        return [item for item in self.pending.values() if item.pane == pane]


class ControlQueue:
    """Control requests waiting for the owner (section 10.3). Lapses after a minute."""

    def __init__(self, now=time.time, lifetime: float = CONTROL_LIFETIME):
        self.now = now
        self.lifetime = lifetime
        self.pending: dict[tuple[str, str], PendingControl] = {}

    def expire(self) -> list[PendingControl]:
        now = self.now()
        lapsed = [item for item in self.pending.values() if item.expires <= now]
        for item in lapsed:
            self.pending.pop((item.pane, item.participant), None)
        return lapsed

    def park(self, participant: str, pane: str) -> PendingControl:
        self.expire()
        now = self.now()
        item = PendingControl(pane=pane, participant=participant, created=now,
                              expires=now + self.lifetime)
        self.pending[(pane, participant)] = item
        return item

    def get(self, pane: str, participant: str) -> PendingControl | None:
        self.expire()
        return self.pending.get((pane, participant))

    def drop(self, pane: str, participant: str) -> PendingControl | None:
        return self.pending.pop((pane, participant), None)

    def for_pane(self, pane: str) -> list[PendingControl]:
        self.expire()
        return [item for item in self.pending.values() if item.pane == pane]
