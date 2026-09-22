---
id: EFT9
type: work
status: executing
labels: [bug, remote, models]
assignee: claude-code
rank: zeft9
created: '2026-09-22'
source: Measured by Claude Code driving the phone app at 390x844, 2026-09-22
links: {plans: [], commits: [b8222865, 44a46ca1, 7d313776], evidence: [docs/qa_evidence/2026-09-22-phone-ux-drive/, docs/qa_evidence/2026-09-22-streamA-pane/], related: [MDL1, PH0N], github: null}
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

## Execution Summary
**Faults 1 and 2 are fixed in the client (`44a46ca1`). Fault 3 is the wire's and is still open** —
see below, and the `## Tests` line that is deliberately not claimed.

`renderEffort()` was the last line of `renderModel()`, below that function's early return on an
unchanged model signature — and the level is in neither half of that signature, so every
`pane_state` in which only the level moved was dropped. It is now called from the top of
`renderModel`, before the guard and outside it; the guard on the *level's own* signature stays, so
an open native picker still survives the ten-a-second `pane_state`.

The closed chip read `✓▾high` for two reasons, both fixed. The `✓ ` marking the current option was
baked into that option's text; the level now uses the model menu's shape — a disabled placeholder
option carrying the plain current level, with the `✓` only on the options in the list. And
`.rp-model-chevron` was absolutely positioned against `.rp-model-wrap`, which held
`model, chevron, effort`, so it was anchored to the level's right edge: each control now has its
own relative box (`.rp-model-box`, `.rp-effort-box`) and its own chevron. A pick returns the select
to the placeholder, as the model menu does, so the closed chip says the level the *pane* is on
rather than the one last tapped, until the pane's own state moves it.

`m.effort_fixed === true` disables the chip and drops its chevron. **That branch is driven from a
fixture only**: nothing publishes the field yet. Stream E owns making it real — gating
`remoteState()`'s `in.efforts` on `effortFixed`, publishing `effort_fixed` through `PaneState` and
the hub's cleaner, and republishing `pane_state` after a refused `effort_pick` so the chip cannot
keep a rejected word. The `EFFORT = ^[a-z][a-z0-9-]{0,15}$` fault (a provider's level word dropped
individually rather than the block rejected, `remote/pane_state.py:61`) is on the same stream.

## Tests
`RELAY_KEYRING=off python3 -m unittest tests.test_pane_view` — 36 tests, OK, 16 s. Re-run by the
orchestrating session after the landing, not only by the implementer.

- `tests/test_pane_view.py::PaneViewTests::test_the_effort_chip_follows_a_state_that_only_moved_the_level`
- `tests/test_pane_view.py::PaneViewTests::test_a_fixed_level_draws_a_chip_that_cannot_be_changed`
- `manual: docs/qa_evidence/2026-09-22-streamA-pane/` — `EFT9-chip.png`,
  `EFT9-chip-after-level-only-state.png`, `EFT9-chip-fixed.png`.
- `manual: docs/qa_evidence/2026-09-22-phone-ux-drive/` — the independent probe this card was filed
  from, re-run unchanged against the landed tree by the orchestrating session. Before:
  `after effort=low {"shown":"✓ high"}` (the state ignored). After:
  `start {"shown":"high"} → after effort=low {"shown":"low"}`, and the chip hides whole when the
  model takes no levels. Chevron geometry, same probe: chevron x 140.2–147.5, model 17–154.5,
  level 158.5–230.6 — `chevron over effort: False`, where it was 216.4–223.6 across a level at
  198–289.

**Not proven, and not claimed:** fault 3. Nothing here exercises a fixed-effort model or a refused
`effort_pick` against a real desktop, because nothing publishes `effort_fixed` yet. A verifier
should read this card as "the chip is right about what it is told" and hold the rest until stream E
lands.
