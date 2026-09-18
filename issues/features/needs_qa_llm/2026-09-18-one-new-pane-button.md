---
id: 803C
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code, subagent), 2026-09-18
rank: 0a
created: '2026-09-18'
acceptance: the pane corner row has one new-pane button; pressing it shows "Use arrow keys to place the new pane" with clickable ← ↑ → ↓; an arrow key or arrow click makes the pane on that side, Esc or a click elsewhere makes none; Ctrl+E and the pane.split* actions are unchanged
source: '`issues/feature_intake.txt`, 2026-09-18: "only have 1 "new pane" button, not 2. when you press it, give a notification "use arrow keys (arrow icons?) to place new pane""'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-one-new-pane-button/'], related: [78BN], github: null}
---
# One "new pane" button, and it asks which side

## Issue

only have 1 "new pane" button, not 2. when you press it, give a notification "use arrow keys (arrow icons?) to place new pane"

## Decisions

- **Nothing is made until the answer.** The key path (#78BN: Ctrl+E makes a pane on the right at
  once, an arrow within two seconds re-docks it) stays as it is. The button asks first instead,
  because two seconds is not enough time to move the mouse to an arrow, and a pane that appears and
  then jumps is harder to follow with the pointer than one that appears where you pointed.
- **All four sides.** Left and above insert the new pane before the current one (`splitToward()`
  already supports all four through `insertBeside(…, towardStart(direction))`).
- **The notification is the pane's own toast style**, as a small card right under the pane's button
  row, not a new popup kind; its arrow buttons are the pane chrome's own `paneChromeButton`s.
- Esc cancels and is consumed; any other key, or a click anywhere but the prompt, cancels and is
  passed on; the prompt closes itself after ten seconds.

## Implemented

- `src/PaneChrome.h`: ⬓+ (`pane.splitDown`) and ◫+ (`pane.splitRight`) are replaced by one ⊞ that
  runs `pane.choose`. Its tooltip shows the live `pane.splitRight` keys (new `keysFrom` button
  property read by `refreshTooltips()`).
- `src/PaneLayout.h/.cpp`: `PlacementWindow` gains `Mode::Choose` (all four arrows place, Esc →
  `Action::Cancel`, `kChooseTimeoutMs` = 10 s). `Mode::Move`, the default, behaves exactly as before.
- `src/RelayWindow.h`: `choosePlacement()`, `placeChosen()`, `showPlacementPrompt()`,
  `placePlacementPrompt()`; the #78BN event-filter branch handles the Choose mode and leaves clicks
  on the prompt to its buttons (asked by `QApplication::widgetAt()`, since the press reaches the
  filter through the `QWindow` first).
- `src/Theme.cpp`: `QFrame#placementPrompt` looks like `QLabel#toast` in every theme, including
  the metal and plastic materials.
- Shortcut hint (WARP.md): the first three times (per the registry's limit and the global
  "Shortcut hints" setting), the prompt's second line reads "Next time: <pane.splitRight keys>,
  then an arrow" — hint id `pane.choose.mouse`.
- Docs: `docs/ARCHITECTURE.md`, "Pane button row".

## QA checklist

- [ ] Every pane kind (terminal, Switchboard, explorer, preview, settings) shows one ⊞ and no ⬓+ / ◫+.
- [ ] ⊞ shows "Use arrow keys to place the new pane" with ← ↑ → ↓ under the button row, clamped inside the window.
- [ ] Each arrow key places the new pane on that side of the pane whose ⊞ was pressed; left/up insert before it.
- [ ] Each arrow button does the same with the mouse.
- [ ] Esc cancels and does not reach the prompt box or a full-screen program; a letter cancels and is typed.
- [ ] A click elsewhere cancels; the prompt goes by itself after ten seconds.
- [ ] Ctrl+E then an arrow still works as in #78BN; `pane.splitDown/Left/Up` from the palette still work.
- [ ] The "Next time" line appears at most three times and never with "Shortcut hints" off; it names rebound keys.
- [ ] Light, dark, metal and plastic themes: the prompt is legible.
