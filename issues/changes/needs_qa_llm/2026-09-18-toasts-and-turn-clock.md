---
id: HEFA
type: work
status: needs-qa-llm
labels: [change, bug]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code subagent), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'During an agent turn the "thinking · N s · Esc stops" clock ticks in the strip under the prompt box and is never a toast; a toast raised during the turn stays up for its time; two toasts in a row both appear, one after the other; a shortcut hint counts as shown only when it appears; `relay-hints-tests` passes'
source: 'found while fixing #Y4GE'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-toasts-and-turn-clock/'], related: [Y4GE], github: null}
---
# The turn clock gets a place of its own; toasts queue; a hint counts when it is seen

## Issue

the thinking pill covers every toast during a turn (found while fixing #Y4GE)

## Change

The pane's status line was a toast (`RelayWindow`'s `onStatus` → `Pane::toast(text, 5000)`), and
`Pane::tickTurnClock()` sent "thinking · N s · Esc stops" through it every second. The pane has one
toast label, so every other toast in a turn ("N characters copied", "Withdrawn · …", "Queued · …",
shortcut hints) was replaced within a second. A hint was also counted as shown (count, cooldown
and the 20 s global gap) before it was drawn, so one raised during a turn used up a showing
nobody saw and blocked other hints for the gap.

**The clock** (`Pane::buildSessionControls`, `startTurnClock`/`stopTurnClock`/`tickTurnClock`).
It is state, not an event, so it has a label of its own, `m_turnClockLabel`, in the strip under the
prompt box, left of the "% left" chip. That strip is visible whenever the prompt box is, unlike the
queue strip, which hides when the queue is empty. The label shows while a turn runs and hides when
it ends. The text is the same as before, the "step N/M" note included, with the stop key from the
live Keymap (`agent.stop`). The thinking bubble's header still shows the seconds when the bubble
is open. The clock never goes through `status()` or `toast()` now.

**Toasts queue** (`Pane::toast`, `enqueueToast`, `showNextToast`, `shortenToastForQueue`).
`toast(text, ms)` keeps its signature. While a toast is up the next one waits. The toast up is cut
to at least 1.5 s (or its own time if that is shorter), so the wait stays short. The same text
straight after itself collapses into one toast that stays up for the longer of the two times.
Nothing is dropped, apart from a queued hint whose gates have closed by the time its turn comes
(see below).

**Hint accounting** (`relay::ShortcutHints` in `src/Hints.*`). `shouldShow()` is now two steps:
`mayShow()` asks and records nothing, and `recordShown()` counts a showing and starts the
cooldown and the global gap. `shouldShow()` still does both at once and keeps its limit, cooldown
and gap. Its one remaining caller draws its hint straight away: the "Next time" line in the pane
placement prompt. `Pane::hint()` checks `mayShow()` and queues the toast with its hint id.
`showNextToast()` asks the gates again when the toast's turn comes and records the hint only if
it is then drawn. Two hints queued together still keep the 20 s gap: the second is dropped when
its turn comes. A hint already waiting or up is not queued a second time (`pollProgram` raises
`queue.whileRunning` every 250 ms). `RelayWindow::hint()` used to call `shouldShow()` and then
toast on the active pane. It now hands the hint to that pane's `hint()`. It records at once only
when there is no pane and the text goes to the status bar. Idle tips work the same way:
`nextIdleTip()` picks a tip without recording it, and the pane records it when it shows. An idle
tip is only offered to a pane with no toast up or waiting.

**Other `status()` users.** None runs on a timer. The worker's `status` events are one-shot, apart
from "Requesting model · …", which was already folded into the clock. The shell's "Shell ready ·
exit N" is one per prompt. So they all stay toasts and now queue like the rest.

Docs: `docs/ARCHITECTURE.md` "Shortcut hints".

## Evidence

`docs/qa_evidence/2026-09-18-toasts-and-turn-clock/drive.sh` runs Relay under Xvfb (display :95,
fake model on port 18445). It uses the isolated jail, `launch.sh` and `fake-provider.py` of
`2026-09-18-thinking-copy-and-steer-withdraw`. It was run on a build of HEAD plus this change
only. Each shot has a full-screen PNG and a `-strip.png` crop of the bottom of the pane.

- `implementer-01`: 5 s into the turn. "thinking · 5 s · step 1/256 · Esc stops" is in the strip
  next to "100% left", and there is no toast.
- `implementer-02`: Ctrl+C on a selection in the thinking bubble, followed at once by a second
  prompt sent with Enter. "57 characters copied" is up and the clock reads 7 s. The "Queued · …"
  toast is waiting.
- `implementer-03` / `04`: "Queued · the agent prompt runs after the items ahead of it · …"
  follows at 9 s and is still up at 11 s. The clock keeps counting in the strip.
- `implementer-05`: both toasts have gone and the clock reads 14 s.
- `tests/hints_test.cpp`: `checkingRecordsNothing` shows that `mayShow()` records nothing however
  often it is called, and that `recordShown()` starts the count, the cooldown and the gap. It also
  shows that a picked idle tip that was never drawn leaves the next pick free.
  `nextTimeAndIdleTips` now records the tip it shows.

## QA checklist

- [ ] During an agent turn, "thinking · N s · <stop key> stops" ticks in the strip under the prompt
      box, left of "% left", and no toast repeats it; the chip disappears when the turn ends or is
      stopped
- [ ] Rebinding `agent.stop` changes the key named in the chip
- [ ] With the thinking bubble open, its header still counts the seconds
- [ ] Ctrl+C on a selection in the bubble during a turn: "N characters copied" stays up for its
      full time (about 1.6 s) while the clock keeps ticking
- [ ] Two toasts in quick succession (copy, then Enter on a queued prompt): both appear, one after
      the other; neither is lost
- [ ] The same toast twice in a row (copy the same selection twice) shows once, not twice in a row
- [ ] A shortcut hint raised during a turn is actually seen, and `hints/count/<id>` in
      `relay.conf` goes up only once it appears
- [ ] Two different hints within 20 s: only the first appears (the global gap), and the second
      is not counted
- [ ] Idle tips still appear about 4 s after a finished turn with an empty, focused prompt box,
      and are counted once each
- [ ] A window-level hint (for example the pane-placement arrows hint) shows on the active pane's
      toast corner
