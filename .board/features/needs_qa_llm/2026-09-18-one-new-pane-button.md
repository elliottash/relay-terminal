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
acceptance: the pane corner row has one new-pane button; pressing it makes a pane on the right at once and toasts that the pane is placed by dragging its header; Ctrl+E and the pane.split* actions are unchanged
source: '`issues/feature_intake.txt`, 2026-09-18: "only have 1 "new pane" button, not 2. when you press it, give a notification "use arrow keys (arrow icons?) to place new pane""'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-one-new-pane-button/'], related: [78BN], github: null}
---
# One "new pane" button: a pane on the right, placed by dragging

## Issue

only have 1 "new pane" button, not 2. when you press it, give a notification "use arrow keys (arrow icons?) to place new pane"

## Decisions

- **Superseded 2026-09-18 (see below).** The first version (commit 484ee4a) asked first: a card
  under the button row with "Use arrow keys to place the new pane" and ← ↑ → ↓ buttons, and nothing
  was made until an arrow was chosen.
- **Owner, 2026-09-18, on 484ee4a:** "the opane button shouldnt make you choose, it just goes to the
  right. since you are already with the mouse you can drag it where you want."
  So ⊞ now makes the pane on the right at once, like Ctrl+E, and toasts
  "New pane · drag its header to place it". The choose-first card, its `PlacementWindow::Mode::Choose`
  and their unit tests are removed; nothing else used them.
- **No arrow window after the button.** Ctrl+E's two-second "← ↑ ↓ re-docks it" (#78BN) is not
  armed by ⊞: the user is on the mouse, and the toast names the mouse way (dragging). So the text
  says nothing about arrows; the shortcut hint names Ctrl+E, which is where the arrows live.

## Implemented

- `src/PaneChrome.h`: ⬓+ (`pane.splitDown`) and ◫+ (`pane.splitRight`) are replaced by one ⊞ that
  runs `pane.newByMouse` (not a Keymap action). Its tooltip, "New pane (drag its header to place
  it)", shows the live `pane.splitRight` keys through a new `keysFrom` button property read by
  `refreshTooltips()`.
- `src/RelayWindow.h`, `runAction("pane.newByMouse")`: `splitToward(Right)` (no placement window),
  `notice("New pane · drag its header to place it")`, and the WARP.md shortcut hint
  `pane.new.mouse`: "Next time: <pane.splitRight keys> · new pane", with the registry's limit and
  the global "Shortcut hints" setting.
- Docs: `docs/ARCHITECTURE.md`, "Pane button row".

## QA checklist

- [ ] Every pane kind (terminal, Switchboard, explorer, preview, settings) shows one ⊞ and no ⬓+ / ◫+.
- [ ] ⊞ makes a new pane to the right of that pane at once, focused, with no choice asked.
- [ ] A toast says "New pane · drag its header to place it"; dragging the new pane's header re-docks it.
- [ ] Arrow keys right after ⊞ behave normally (they do not move the pane).
- [ ] The "Next time: Ctrl+E · new pane" hint shows at most three times, never with "Shortcut hints" off, and names rebound keys.
- [ ] Ctrl+E then an arrow still works as in #78BN; `pane.splitDown/Left/Up` from the palette still work.
