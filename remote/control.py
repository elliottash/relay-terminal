# SPDX-License-Identifier: GPL-3.0-or-later
"""Who is driving a shared pane: **one** state (docs/REMOTE-PROTOCOL.md section 10.3).

Before this file there were two: the source's own driver (``TerminalPaneSource`` set
``pane.driver`` on every write, and the GUI's pane line carries ``control: human|agent``) and the
idea of a participant holding the keyboard. Section 10.3 says they are the same token as the
human/agent handoff of ``ARCHITECTURE.md`` section 9 — "the agent is driving" and "alice is
driving" are one state — so this is the book the hub keeps, and every other spelling is derived
from it:

* the ``control`` field of a `panes` item (section 6.3: ``human | agent | remote:<device> |
  participant:<id>``);
* the ``holder`` of the `control` message (section 10.3: ``owner | agent | participant:<id>``,
  because an owner's own remote device *is* the owner);
* the ``driving`` flag on each `participants` row.

The book never talks to the wire or to a source. It answers "did that change anything?", and the
hub does the telling — which is what keeps the fan-out in one place and lets this be tested with
no sockets at all.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

# The three kinds of holder. A paired device of the owner's is `OWNER` with its device id in
# `who`: there is no fourth kind, because the owner's phone is the owner.
OWNER, AGENT, PARTICIPANT = "owner", "agent", "participant"


@dataclass(frozen=True)
class Holder:
    kind: str = OWNER
    who: str = ""            # a device id for the owner's remote device, or a participant id
    name: str = ""           # what to show; a participant's cleaned name

    @property
    def label(self) -> str:
        """The `control` message's ``holder`` (section 10.3)."""
        return f"participant:{self.who}" if self.kind == PARTICIPANT else self.kind

    @property
    def field(self) -> str:
        """The `panes` item's ``control`` (section 6.3)."""
        if self.kind == PARTICIPANT:
            return f"participant:{self.who}"
        if self.kind == AGENT:
            return AGENT
        return f"remote:{self.who}" if self.who else "human"


THE_OWNER = Holder(OWNER, "", "")


class ControlBook:
    """One holder per pane, and the reasons it changes.

    ``on_change(pane, old, new)`` is called after the book has changed, never during: the hub
    fans out `control` and `participants` from it, and may read the book back.
    """

    def __init__(self, on_change: Callable[[str, Holder, Holder], None] | None = None):
        self.holders: dict[str, Holder] = {}
        # Panes the owner took back **by hand** (a keystroke in the pane, a revoke, a pause).
        # Not a second state: the holder in those panes is the desktop either way, and this only
        # says *why*. It is here because section 6.6 lets one of the owner's own `full` devices
        # claim the keyboard by typing, with no separate claim message — so without it the
        # keystroke that took the pane back would be undone by the phone's very next line, which
        # is exactly what #W5N2's live drive found (shots 13a, 13b). A device may still take the
        # pane back whenever it likes; it has to ask for it (`control_request`) rather than take
        # it by typing.
        self.taken_back: set[str] = set()
        self.on_change = on_change or (lambda pane, old, new: None)

    # ---- reading ---------------------------------------------------------------------------

    def holder(self, pane: str) -> Holder:
        return self.holders.get(pane, THE_OWNER)

    def label(self, pane: str) -> str:
        return self.holder(pane).label

    def field(self, pane: str) -> str:
        return self.holder(pane).field

    def participant(self, pane: str) -> str | None:
        """The participant id driving this pane, or None for the owner or the agent."""
        holder = self.holder(pane)
        return holder.who if holder.kind == PARTICIPANT else None

    def device(self, pane: str) -> str | None:
        holder = self.holder(pane)
        return holder.who if holder.kind == OWNER and holder.who else None

    def panes_held_by(self, kind: str, who: str) -> list[str]:
        return [pane for pane, holder in self.holders.items()
                if holder.kind == kind and holder.who == who]

    # ---- writing ----------------------------------------------------------------------------

    def _set(self, pane: str, holder: Holder) -> bool:
        old = self.holder(pane)
        if (old.kind, old.who) == (holder.kind, holder.who):
            # The same holder, told to us again — typically by a source echoing its own driver
            # back with no name on it. A handoff is a change of *who*, never of how they are
            # labelled, so this is not one: keep whichever label we know and say nothing.
            if holder.name and holder.name != old.name and old.who:
                self.holders[pane] = holder
            return False
        if holder == THE_OWNER:
            self.holders.pop(pane, None)
        else:
            self.holders[pane] = holder
        self.on_change(pane, old, holder)
        return True

    def grant(self, pane: str, participant_id: str, name: str = "") -> bool:
        """The owner said yes to an editor (10.3). Whoever held it loses it."""
        self.taken_back.discard(pane)
        return self._set(pane, Holder(PARTICIPANT, participant_id, name))

    def may_type(self, pane: str, device_id: str) -> bool:
        """Whether this device's ordinary input claims the keyboard on its own (section 6.6).

        It does when it already holds the pane, and when the pane is free — nobody has asked for
        it and the owner has not taken it back by hand. It does **not** when a guest, the agent
        or another device of the owner's holds it, or after a `control_take`: section 10.3 refuses
        input from anyone who is not the holder, and that sentence has to mean the owner's own
        phone too or the keystroke that takes the pane back means nothing.
        """
        holder = self.holder(pane)
        if holder.kind == OWNER and holder.who == device_id:
            return True
        if holder != THE_OWNER:
            return False
        return pane not in self.taken_back

    def claim_device(self, pane: str, device_id: str, name: str = "", *,
                     by_typing: bool = False) -> bool:
        """One of the owner's own `full` devices took over. Still the owner, in section 10.3's
        vocabulary, which is why a guest sees `holder: "owner"` and not a device id.

        ``by_typing`` is the implicit claim of section 6.6 — input arriving from a device that
        has not asked — and it is refused (False, nothing changed) unless :meth:`may_type` says
        the keyboard is this device's to take.
        """
        if by_typing and not self.may_type(pane, device_id):
            return False
        self.taken_back.discard(pane)
        return self._set(pane, Holder(OWNER, device_id, name))

    def take(self, pane: str) -> bool:
        """The owner's physical keystroke, a revoke, or a pause: back to the desktop, no asking.

        The pane is remembered as taken back, so the device that was driving cannot simply type
        its way back in; it has to ask.
        """
        changed = self._set(pane, THE_OWNER)
        self.taken_back.add(pane)
        return changed

    def release(self, pane: str, kind: str, who: str = "") -> bool:
        """Give it up, but only if you are the one holding it."""
        holder = self.holder(pane)
        if holder.kind != kind or holder.who != who:
            return False
        return self._set(pane, THE_OWNER)

    def drop_participant(self, participant_id: str) -> list[str]:
        """A guest disconnected, was removed, demoted or expired: they drive nothing now."""
        gone = self.panes_held_by(PARTICIPANT, participant_id)
        for pane in gone:
            self._set(pane, THE_OWNER)
        return gone

    def drop_device(self, device_id: str) -> list[str]:
        gone = self.panes_held_by(OWNER, device_id)
        for pane in gone:
            self._set(pane, THE_OWNER)
        return gone

    def forget(self, pane: str) -> None:
        """The pane is gone or is no longer shared."""
        self.taken_back.discard(pane)
        self._set(pane, THE_OWNER)

    # ---- the desktop's own token (ARCHITECTURE.md section 9) ---------------------------------

    def observe(self, pane: str, field: str) -> bool:
        """Fold the source's own ``control`` field into the book.

        The GUI's pane line and the terminal source both say who they think is driving. Only two
        of their answers are news here:

        * ``agent`` — the desktop handed the program to the agent, which outranks a guest holding
          the keyboard: the guest is told with `control`, like any other loss;
        * ``remote:<device>`` — the terminal source noticed one of the owner's devices typing.

        ``human`` is **not** taken as "the owner grabbed it back": for the GUI source it only
        means "not the agent", and a guest's keystrokes never move it. The owner taking control
        back is the explicit `control_take` line, so that it is an act and not an inference.
        """
        if field == AGENT:
            return self._set(pane, Holder(AGENT, "", ""))
        if field.startswith("participant:") or field.startswith("remote:guest:"):
            # Our own answer, echoed back: the hub names a guest `guest:<id>` when it writes to a
            # source, and a source that keys its driver by that string says so in its pane list.
            return False
        if field.startswith("remote:"):
            device = field.split(":", 1)[1]
            if device:
                return self._set(pane, Holder(OWNER, device, ""))
            return False
        # "human", or anything a source made up: the agent is no longer driving, and nobody else
        # is claimed by it.
        if self.holder(pane).kind == AGENT:
            return self._set(pane, THE_OWNER)
        return False
