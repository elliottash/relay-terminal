#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The approval path against the real guests, once (GT7X, protocol 29.1/29.3).

Everything else in Tier A runs under `permissions: "bypass"` — the owner's rule, and what the model
picker sends — so the approval path ships untried in a live pane: nothing in the GUI asks for
`"ask"` today. This driver is the exception. For each guest it starts the real harness with
`permissions="ask"` in a scratch directory, gives it one small job that needs a tool
(`write a file`), and then does what the pane would do: takes the `approval` event the adapter
raised, builds the section 27 card the provider would draw from it, answers with the **Allow**
choice, and checks that the guest went on and the file is there.

What it proves, in one turn per guest: the real CLI raises an approval under `ask`; the adapter
turns it into the contract's `approval` event; the card the pane draws has the four choices
(Allow / Allow for session / Deny / Deny and stop); the answer reaches the guest; and the turn
finishes rather than hanging. It deliberately does **not** stand up a worker or a pane — the
`question` / `question_answer` round trip is unit-tested, and this is about the live wire.

    PYTHONPATH=backend python3 approval-drive.py [claude|codex|both]

It uses the real HOME (both CLIs need their login), writes only under a temp directory, and costs
one turn per guest on the owner's own plans.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import time

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "backend"))

from relay_core import guest_harness, guest_harness_provider  # noqa: E402

PROMPT = ("Create a file called approval-probe.txt in the current directory whose only contents are "
          "the word ok, then stop. Do not ask me anything first.")
PROBE = "approval-probe.txt"


def log(message: str) -> None:
    print(f"[approval] {message}", flush=True)


def drive(guest: str) -> bool:
    harness = guest_harness_provider.make_harness(guest)
    room = Path(tempfile.mkdtemp(prefix=f"relay-approval-{guest}-",
                                 dir="/tmp/claude-1000" if Path("/tmp/claude-1000").is_dir() else None))
    events: list[guest_harness.HarnessEvent] = []
    answered: list[dict] = []
    ok = True

    def emit(event: guest_harness.HarnessEvent) -> None:
        events.append(event)
        if event.kind != "approval":
            return
        data = event.data
        log(f"approval raised: kind={data.get('kind')!r} detail={str(data.get('detail'))[:120]!r}")
        # Exactly what the provider hands the pane (protocol 27), from the guest's own words.
        card = guest_harness_provider.approval_card(data.get("kind", "other"), data.get("detail"))
        labels = [option["label"] for option in card[0]["options"]]
        log(f"card: header={card[0]['header']!r} options={labels}")
        decision = guest_harness_provider.approval_decision("Allow")
        log(f"answering {decision}")
        harness.answer(data["id"], decision)
        answered.append({"card": card, "decision": decision})

    try:
        started = harness.start(cwd=str(room), permissions="ask")
        log(f"{guest} started: session={started.session_id[:12] or '(not yet named)'} model={started.model!r}")
        result = harness.send(PROMPT, emit=emit, cancel=threading.Event())
        log(f"turn ended: stop_reason={result.stop_reason} text={result.text.strip()[:80]!r}")
        wrote = (room / PROBE).exists()
        kinds = [event.kind for event in events]
        log(f"events: {', '.join(kinds)}")
        log(f"{PROBE} written: {wrote}")
        if not answered:
            log(f"FAIL: {guest} raised no approval under permissions=ask")
            ok = False
        else:
            labels = [option["label"] for option in answered[0]["card"][0]["options"]]
            if labels != [name for name, _d, _x in guest_harness_provider.APPROVAL_CHOICES]:
                log(f"FAIL: the card's options are {labels}")
                ok = False
        if result.stop_reason != "end":
            log(f"FAIL: the turn ended {result.stop_reason}, not end")
            ok = False
        if not wrote:
            log(f"FAIL: the guest was allowed but {PROBE} is not there")
            ok = False
        return ok
    except guest_harness.HarnessError as error:
        log(f"FAIL: {error}")
        return False
    finally:
        harness.close()
        log(f"scratch directory kept for the record: {room}")


def main() -> int:
    which = sys.argv[1] if len(sys.argv) > 1 else "both"
    guests = ("claude", "codex") if which == "both" else (which,)
    results = {}
    for guest in guests:
        log(f"===== {guest}")
        started = time.monotonic()
        results[guest] = drive(guest)
        log(f"{guest}: {'passed' if results[guest] else 'FAILED'} in {time.monotonic() - started:.0f} s")
    log("RESULT: " + json.dumps(results))
    return 0 if all(results.values()) else 2


if __name__ == "__main__":
    sys.exit(main())
