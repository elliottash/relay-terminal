# Ctrl+Enter on an empty prompt box always sends Continue (#SXF1) — implementer evidence

Live check of the landed build at tip `e7b1e4be` (land.py's verify tree, which is the exact tree
that went onto `main`), 2026-09-20 ~19:43 local, under `Xvfb :161` with an isolated
`XDG_CONFIG_HOME` whose only provider is a mock OpenAI-compatible server on loopback (`drive.py`;
no network, no real key, `agent/max_steps=2` so a turn can be driven into its step limit in two
model calls).

```
PASS window came up
PASS D: an ordinary turn ran and finished (no limit, no cut-off)
PASS D: Ctrl+Enter on the empty box sent the prompt "Continue" after an ordinary turn
PASS D: no "Type a prompt first." status on screen
PASS F: Ctrl+Enter with text in the box sent that text, not Continue
PASS F: the request was the typed line and never Continue
PASS A: a turn ran to its step limit (2 model calls)
PASS A: Ctrl+Enter on the empty box sent "Continue" after the step limit
PASS H: /continue shows the "Next time: Ctrl+Enter" hint
      — "Next time: Ctrl+Return - send Continue from an empty prompt box"
PASS G: Ctrl+Enter on the empty box sent nothing to a busy agent — model calls: 9 → 9
PASS G: the pane kept its busy answer ("interrupts the agent")
ALL PASS
```

The mock's own record of the turn-starting user messages, in order — the four prompts the drive
typed, plus the `Continue` that no typing produced:

```
0  text  finish quickly      (D: an ordinary turn)
1  text  Continue            (D: the empty box's Ctrl+Enter — the case the owner hit)
2  text  hello there         (F: typed text is still sent as itself)
3  tools count carefully     (A: the limit turn)
4  text  Continue            (A: the empty box after the step limit)
5  tools two more steps      (H: a second limit turn, for the hint)
```

## What each check is

- **D — an ordinary finished turn** (`implementer-01..03`): a turn that ran out normally, with no
  limit stop and nothing cut off, leaves the box empty; Ctrl+Enter then sends the ordinary prompt
  `Continue` (the mock sees a user message ending in `Continue`, and no turn was needed to earn
  it). This is the case the old rule answered with "Type a prompt first." — the screen OCR of
  `implementer-03` holds no such status. The owner's words, on the card: "ctrl+enter in an empty
  prompt should always send agent prompt 'continue'".
- **F — text in the box is untouched**: Ctrl+Enter with "hello there" typed reaches the model as
  "hello there", and nothing in that stretch of the mock's log ends in `Continue`.
- **A — the limit case still works**: a turn that hits its step limit, empty box, Ctrl+Enter →
  `Continue` (what the card first landed, still true under the plain rule).
- **H — the slow path teaches the fast one** (`implementer-06`): `/continue` shows the
  Superhuman-style hint naming the empty-box key, read live from `agent.interrupt`'s first binding
  (which the Keymap spells `Ctrl+Return`; Return and Enter are the same key), with the new wording
  — "send Continue from an empty prompt box".
- **G — a busy agent keeps its answer** (`implementer-07`): with the `/continue` turn still
  hanging, Ctrl+Enter on the empty box sends no model call at all (9 → 9) and the pane keeps
  "Type a prompt first; Ctrl+Enter interrupts the agent with it." — the empty-box Continue is for
  an idle agent only.

Also captured: the limit line with its ▸ Continue link (`implementer-04`), `relay.log`,
`worker.log` and the mock's request log (`mockserver.log`).

## Harness notes (why the drive is shaped as it is)

- **Order matters after a hung turn.** A model call that never answers keeps the turn running, and
  anything typed behind it queues instead of starting a turn — an earlier arrangement put the busy
  case in the middle and the limit case then never reached the model. The busy check (G) runs last
  and reuses the hanging `/continue` turn from H.
- **A `mock` marker is a count, not an index.** `MockState.tail_after(requests, marker)` takes the
  value `MockState.count()` read *before* the action under test, so the request the action produces
  is `requests[marker]` — `i >= marker`. The first draft compared `i > marker`, which skipped
  exactly the request each check was about; the mock's log showed the behaviour was right while
  every assertion failed.
- In H, `/continue` is typed early (a non-empty editor stops the 4 s idle-tip timer) and submitted
  >20 s after the limit line printed: the limit line's own hints (`turn.link`, `call.fold`) open
  ShortcutHints' global 20 s gap, which would otherwise silently drop the `continue.slow` hint at
  `Pane::hint`'s `mayShow` gate.
- The hint shot is taken while the `/continue` turn hangs, so its turn-end status cannot retire
  the 5 s toast before the screenshot; G waits out that toast before OCR-ing the status line.

## Reproducing

```
Xvfb :161 -screen 0 1400x900x24 &
DISPLAY=:161 python3 drive.py /path/to/relay      # default: land.py's verify tree for this card
```

Needs Xvfb, xdotool, ImageMagick (`import`, `convert`) and tesseract.
