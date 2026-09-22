---
id: EFT9
type: work
status: executing
labels: [bug, remote, models]
assignee: claude-code
rank: zeft9
created: '2026-09-22'
source: 'Measured by Claude Code driving the phone app at 390x844, 2026-09-22'
links: {plans: [], commits: [b8222865], evidence: ['docs/qa_evidence/2026-09-22-phone-ux-drive/'], related: [MDL1, PH0N], github: null}
---
# The phone's reasoning-level chip is stale, reads "✓▾high", and never learns a pick was refused

## Issue
Three faults in the chip `b8222865` added, all reproduced at 390×844 in headless Chrome
(`docs/qa_evidence/2026-09-22-phone-ux-drive/`, findings 1 and 2; `D-effort-chip-closeup.png`).

**1. A level-only `pane_state` is dropped.** `renderEffort()` (`app/pane.js:1061`) is called from
the last line of `renderModel()`, below that function's early return on an unchanged model
signature (`app/pane.js:1042`). The signature is `[label, choices]`; the level is in neither, and
`collapsedText()` keeps the level out of the label by design (#MDL1). Measured through
`window.paneDemo.update`:

```
start              {"value":"high","shown":"✓ high","opts":["low","medium","✓ high"]}
after effort=low   {"value":"high","shown":"✓ high","opts":["low","medium","✓ high"]}   <- ignored
after model change {"value":"low", "shown":"✓ low", "opts":["✓ low","medium","high"]}   <- only now
```

So the level moving at the desktop, another partner's `effort_pick`, and the echo of the phone's
own pick are all invisible until something about the model happens to change.

**2. The closed chip reads `✓▾high`.** The `✓ ` that marks the current option is baked into that
option's text, so it shows in the closed control — the model select beside it avoids this with a
disabled placeholder option carrying the plain label, and reads `fake · local`. And
`.rp-model-chevron` is `position: absolute; right: 7px` inside `.rp-model-wrap`, which now holds
`model, chevron, effort` (`app/pane.js:297`), so the chevron is anchored to the **effort** select's
right edge and paints over its text while the model keeps a 22 px empty gutter. Measured: chevron
at x 216.4–223.6, effort at x 198–289.

**3. A refused pick is never corrected.** `src/RemoteShare.cpp:328-333` throws away the `bool` from
`remoteEffortPick()`, whose own comment (`src/Pane.h:13858`) says "False when nothing changed, so
the hub can say so". Nothing listens; there is no `effort_pick` answer in `SERVER_TYPES`. A refusal
changes nothing on the pane, so no `pane_state` is republished, so — by fault 1 — the phone's chip
keeps the rejected value for the rest of the session. The reachable cases:

* a **fixed-effort** model. `remoteState()` sets `in.efforts = offeredEfforts()` unconditionally
  (`src/Pane.h:13740`) while every other surface gates on `effortFixed` first (the desktop's own
  box is greyed, `src/Pane.h:5539`; `src/ModelPicker.cpp:1230`; `src/JobsTab.cpp:370`).
  Relay Free is exactly this case (`src/ModelCatalog.cpp:57-62`). The phone gets a live picker for
  a level the pane will not change.
* the model changed between the state the phone drew and the tap:
  `if (levels.isEmpty() || !levels.contains(level)) return false;` (`src/Pane.h:13860`).

Related, same area: `docs/REMOTE-PROTOCOL.md:1675` says "The pane **snaps** a level the model does
not take" — the pane refuses outright and never reaches `snapEffort`. And
`remote/pane_state.py:61`'s `EFFORT = ^[a-z][a-z0-9-]{0,15}$` drops a provider's level word
*individually* rather than rejecting the block, so `efforts: ["low","very_high"]` with the pane on
`very_high` publishes `{"effort": null, "efforts": ["low"]}` — the phone then shows `low` with
nothing ticked, and the pane's real level is unreachable from the phone.

## Discussion points
The cheapest shape consistent with the rest of section 16 is probably: call `renderEffort` from
`update()` rather than from inside `renderModel`'s guard; give the effort select the model's
placeholder-option pattern and its own chevron (or move `.rp-model-chevron` inside a wrapper of the
model alone); publish the fixed-ness (`effort_fixed` plus the reason the desktop already writes) so
the chip can be disabled; and republish `pane_state` on a refused pick so the authoritative state
overwrites the phone's guess.

## Done means
The chip on the phone always says the level the pane is actually on. A `pane_state` whose
model is unchanged and whose `effort` moved repaints it; a pick the pane refuses is followed by the
pane's own state, so the chip goes back rather than keeping the rejected word; and a model whose
level is fixed draws a chip that cannot be changed rather than a live picker. Closed, the chip reads
`high` — no checkmark, no chevron across it — and the model chip beside it keeps its own chevron.
It fails if any of those still needs a model change to correct itself, or if the closed control
shows a `✓`.
