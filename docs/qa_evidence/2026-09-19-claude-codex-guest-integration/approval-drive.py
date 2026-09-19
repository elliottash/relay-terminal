#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
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

    PYTHONPATH=backend python3 approval-drive.py [claude|codex|both] [choice…]

`choice` is one of the card's own labels, and each is checked for what it should mean:

    Allow              the tool runs, the file is there, the turn ends normally
    Allow for session  the word is accepted and the turn runs on (see the note below)
    Deny               the guest is told no, carries on and says so; the file is not written
    Deny and stop      the turn ends there; the file is not written

"Allow for session" cannot be proved by a driver: it asks the guest to change one file twice, but
whether the second call is skipped is the guest's judgement of sameness (codex caches "the same
files"; claude writes a narrow rule), and the guest chooses its own tools. The run checks that the
word is accepted and the work completes, and reports how many times it was asked. With no choice
named, all four run in that order.

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
# "Allow for session" is only observable across two calls: the first is asked about, the second
# must not be. It has to be the *same file* twice. Codex's `acceptForSession` is defined as "future
# changes to the same files should run without prompting" (its own schema), and claude's session
# rule is written as narrowly as the thing that was asked about, so two different files are two
# different questions in both — as a first run of this driver found, by asking twice and failing.
PAIR_PROMPT = ("Do two separate steps in the current directory. First create probe-one.txt "
               "containing the single word one. Then, as a second step, edit probe-one.txt so it "
               "contains the single word two instead. Then stop. Do not ask me anything first.")
PAIR = ("probe-one.txt",)
CHOICES = tuple(name for name, _description, _decision in guest_harness_provider.APPROVAL_CHOICES)


def log(message: str) -> None:
    print(f"[approval] {message}", flush=True)


def drive(guest: str, choice: str = "Allow") -> bool:
    harness = guest_harness_provider.make_harness(guest)
    room = Path(tempfile.mkdtemp(prefix=f"relay-approval-{guest}-",
                                 dir="/tmp/claude-1000" if Path("/tmp/claude-1000").is_dir() else None))
    events: list[guest_harness.HarnessEvent] = []
    answered: list[dict] = []
    session_scope = choice == "Allow for session"
    prompt, probes = (PAIR_PROMPT, PAIR) if session_scope else (PROMPT, (PROBE,))
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
        # The first ask is answered with the choice under test; a second ask (which "Allow for
        # session" is supposed to prevent) is allowed, so the turn can finish and be reported on.
        decision = guest_harness_provider.approval_decision(choice if not answered else "Allow")
        log(f"answering {decision}")
        harness.answer(data["id"], decision)
        answered.append({"card": card, "decision": decision})

    try:
        started = harness.start(cwd=str(room), permissions="ask")
        log(f"{guest} started: session={started.session_id[:12] or '(not yet named)'} model={started.model!r}")
        result = harness.send(prompt, emit=emit, cancel=threading.Event())
        log(f"turn ended: stop_reason={result.stop_reason} text={result.text.strip()[:100]!r}")
        written = [name for name in probes if (room / name).exists()]
        log(f"events: {', '.join(event.kind for event in events)}")
        log(f"asked {len(answered)} time(s); files written: {written or 'none'}")
        if not answered:
            log(f"FAIL: {guest} raised no approval under permissions=ask")
            return False
        labels = [option["label"] for option in answered[0]["card"][0]["options"]]
        if labels != list(CHOICES):
            log(f"FAIL: the card's options are {labels}")
            ok = False
        # What each choice has to mean, checked rather than assumed.
        if choice == "Allow":
            ok &= _expect(written == [PROBE], f"the file is there ({written})")
            ok &= _expect(result.stop_reason == "end", f"the turn ended normally ({result.stop_reason})")
        elif session_scope:
            # What this can honestly check is that the word was accepted and the turn ran on.
            # Whether a *later* call is skipped is the guest's own judgement of sameness — codex
            # caches "the same files" for a file change and "the same session-scoped approval" for
            # a command (its own schema), and claude writes a rule as narrow as what was asked
            # about — and the guest picks its own tools, so a prompt cannot force a second call to
            # be the same thing. Two runs of this bore that out: it edited a different file the
            # first time, and reached for a shell command the second. The count is reported.
            ok &= _expect(list(written) == list(PAIR), f"the file is there ({written})")
            ok &= _expect(result.stop_reason == "end", f"the turn ended normally ({result.stop_reason})")
            # Observations, not verdicts: both depend on what the guest chose to do. Across four
            # runs of this one choice the guests edited a different file, reached for a shell
            # command, honoured the session scope, and once skipped their own second step.
            body = (room / PAIR[0]).read_text(encoding="utf-8", errors="replace").strip() if written else ""
            log(f"note: asked {len(answered)} time(s); the file ends as {body[:40]!r}. A second ask "
                f"is legitimate when the guest judged the call to be a different thing, and it is "
                f"the guest that decides whether to make the second change at all")
        elif choice == "Deny":
            ok &= _expect(not written, f"nothing was written ({written})")
            ok &= _expect(result.stop_reason == "end",
                          f"the guest carried on and answered ({result.stop_reason})")
        elif choice == "Deny and stop":
            ok &= _expect(not written, f"nothing was written ({written})")
            ok &= _expect(result.stop_reason in ("interrupted", "end"),
                          f"the turn ended there ({result.stop_reason})")
            ok &= _expect(len(answered) == 1, f"it did not ask again ({len(answered)})")
        return ok
    except guest_harness.HarnessError as error:
        log(f"FAIL: {error}")
        return False
    finally:
        harness.close()
        log(f"scratch directory kept for the record: {room}")


def _expect(held: bool, what: str) -> bool:
    log(("ok: " if held else "FAIL: ") + what)
    return held


def main() -> int:
    which = sys.argv[1] if len(sys.argv) > 1 else "both"
    guests = ("claude", "codex") if which == "both" else (which,)
    choices = sys.argv[2:] or list(CHOICES)
    unknown = [c for c in choices if c not in CHOICES]
    if unknown:
        log(f"unknown choice(s) {unknown}: one of {list(CHOICES)}")
        return 1
    results = {}
    for guest in guests:
        for choice in choices:
            log(f"===== {guest} · {choice}")
            started = time.monotonic()
            results[f"{guest}: {choice}"] = drive(guest, choice)
            log(f"{guest} · {choice}: {'passed' if results[f'{guest}: {choice}'] else 'FAILED'}"
                f" in {time.monotonic() - started:.0f} s")
    log("RESULT: " + json.dumps(results, indent=2))
    return 0 if all(results.values()) else 2


if __name__ == "__main__":
    sys.exit(main())
