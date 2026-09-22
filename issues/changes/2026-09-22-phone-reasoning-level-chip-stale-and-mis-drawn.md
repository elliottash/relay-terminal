---
id: EFT9
type: work
status: needs-verification
labels: [bug, remote, models]
assignee: claude-code
implemented_by: anthropic/claude-opus-5 via claude-code
rank: zeft9
created: '2026-09-22'
source: Measured by Claude Code driving the phone app at 390x844, 2026-09-22
links: {plans: [], commits: [b8222865, 44a46ca1, 2ee01f0c, d329d88a, a0882679, 9c1b1d18, 7d313776], evidence: [docs/qa_evidence/2026-09-22-phone-ux-drive/, docs/qa_evidence/2026-09-22-streamA-pane/, docs/qa_evidence/2026-09-22-streamE-effort/], related: [MDL1, PH0N], github: null}
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
All three faults are fixed, in two halves: the chip (`44a46ca1`) and the wire it is told things
over (`2ee01f0c`, `d329d88a`, `a0882679`).

### The chip

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
own relative box and its own chevron. A pick returns the select to the placeholder, as the model
menu does, so the closed chip says the level the *pane* is on rather than the one last tapped.

### The wire

`remoteState()` set `in.efforts = offeredEfforts()` unconditionally while every other surface gates
on `effortFixed`. `effort_fixed` and `effort_fixed_reason` — the greyed box's own sentence,
`sentenceCase(effortFixedReason())` — now ride in the `model` block, through `PaneState`, the hub's
cleaner and the protocol.

**The levels are still published when the level is fixed, which is a deliberate departure from what
the `## Discussion points` above guessed at.** Gating `in.efforts` on `effortFixed` would publish
no levels, and the client hides the chip whole when there are none — so Relay Free would have drawn
*no* chip where this card's `## Done means` asks for an unchangeable one. `effortFixedReason()`
exists precisely because those levels are still worth showing.

`in.effort` is now the level the desktop's box has selected (`nearestEffort`), not the raw
`m_effort` the pane carries — which is not always one the model takes, so a view could be handed a
list its own `effort` was not in and tick nothing.

`RemoteShare` threw away the `bool` from `remoteEffortPick()`. A refusal now forces
`publishPaneState()`, so the pane's own level overwrites the guess on every watching device. Of
section 16's two shapes — answer the one device (`conversation_id`) or republish (`queue_resume`) —
this is the second: a level is not secret, it is already in every state, and every watching device
drew the same stale chip, so no new message type was needed. `remoteEffortPick` returns `true` when
the level was already the pane's, so there is no spurious publish.

`remote/pane_state.py`'s `EFFORT = ^[a-z][a-z0-9-]{0,15}$` rejected `very_high`, `Medium` and
anything over sixteen characters, and dropped the *word* rather than the block. The pattern is now
the shape a provider's word really has (space, dot and slash still excluded, so it can never be an
address or a path) and the block is all of it or none: a bad word, over-cap, or an `effort` outside
its own list drops both fields, because half a picker is worse than none.

Doc: section 16 said the pane "snaps" a level it refuses — it refuses, because the client
necessarily drew an older state and turning its tap into some other level would set one nobody
asked for. The viewer row now names all four effort fields it strips, and the client→desktop
preamble names `conversation_id` as `full`.

## Tests
`RELAY_KEYRING=off python3 -m unittest tests.test_pane_view` — 36 tests, OK; `…
tests.test_remote_pane_state` — 45 tests, OK (up from 42); `ctest --test-dir build -R panestate`.
All re-run by the orchestrating session after the landing. `land.py`'s build gate built the exact
tree it put on `main` for `d329d88a`, the C++ commit.

**The chip:**

- `tests/test_pane_view.py::PaneViewTests::test_the_effort_chip_follows_a_state_that_only_moved_the_level`
- `tests/test_pane_view.py::PaneViewTests::test_a_fixed_level_draws_a_chip_that_cannot_be_changed`

**The wire:**

- `tests/test_remote_pane_state.py::CleanTests::test_the_reasoning_level_block_is_all_of_it_or_none`
  — the widened words, the whole-block rejection, the `effort ∈ efforts` rule, dedupe,
  `EFFORTS_MAX` exactly and one over, omit-when-empty.
- `tests/test_remote_pane_state.py::CleanTests::test_a_fixed_level_says_so_with_the_desktops_reason`
  — both fields end to end, the reason scrubbed like a model label, anything but a real `true` not
  fixed.
- `tests/test_remote_pane_state.py::…::test_a_refused_pick_is_answered_by_the_panes_own_state`
- `effort_pick` added to both loops of `test_a_guest_can_neither_ask_for_nor_act_on_a_state`, and
  `CapabilityTests` gained four `assertNotIn`s. `EXAMPLE` is the contract's example again —
  measured: before, deleting the viewer strip left the whole suite green; after, it fails.

**Manual, end to end:**

- `manual: docs/qa_evidence/2026-09-22-streamE-effort/` — `seam.py` (the real
  `librelay-panestate.a` → the real `remote/pane_state.py`, now and at `2ee01f0c^`), `drive.py`
  (→ the real `app/pane.js`, headless Chrome 390×844), five screenshots.
- `manual: docs/qa_evidence/2026-09-22-streamA-pane/` — `EFT9-chip.png`,
  `EFT9-chip-after-level-only-state.png`, `EFT9-chip-fixed.png`.
- `manual: docs/qa_evidence/2026-09-22-phone-ux-drive/` — the probes this card was filed from,
  re-run unchanged against the landed tree by the orchestrating session:
  * level-only state — `start {"shown":"high"} → after effort=low {"shown":"low"}`, where before
    the second state was ignored and the chip stayed on `✓ high`.
  * chevron — chevron x 140.2–147.5 inside the model at 17–154.5, level at 158.5–230.6;
    `chevron over effort: False`, where it was 216.4–223.6 across a level at 198–289.
  * **the seam between the two halves**, put through the real hub cleaner into the real view: a
    Relay Free pane publishes
    `{"effort":"high","efforts":["low","high"],"effort_fixed":true,"effort_fixed_reason":"…"}` and
    the chip comes back `{"hidden":false,"disabled":true,"shown":"high"}` — visible and
    unchangeable, which is what `## Done means` asks for. An ordinary pane is `disabled:false`. A
    `view` device's model block is `{"label":"Relay Free"}` and nothing else.

**Not proven:** the refusal path has no C++ test — the only target compiling `src/RemoteShare.cpp`
is `relay-consolemode-tests`, whose source was not this stream's to touch. The hub's half is
tested, the desktop's half is one line whose contract is now written on both the hook and the pane
function, and the build gate compiled it.
