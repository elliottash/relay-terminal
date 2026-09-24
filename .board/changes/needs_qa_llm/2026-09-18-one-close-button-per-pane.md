---
id: X2PC
type: work
status: needs-qa-llm
labels: [change, bug]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code subagent), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'The Settings pane shows exactly one close button (the pane chrome''s ×), nothing of the chrome row covers its search box, and that × closes it the way Esc does (focus back where it was; never closes the window). A subagent transcript in a pane of its own shows one ×, and the floating transcript over a narrow pane no longer lands its × on the pane''s.'
source: 'owner in chat, 2026-09-18: "settings pane has redundant overlapping close ''X'' icons. check for that bug elsewhere."'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-one-close-button-per-pane/'], related: [], github: null}
---
# Two close buttons on top of each other in the Settings pane

## What was wrong

Every leaf in the splitter layout gets a `PaneChrome` row (⬓+ ◫+ ⇱ ×) floated over its top-right
corner (`src/main.cpp`, `syncChrome`). When Settings became a real pane on 2026-09-18 it kept the
✕ it had drawn for itself as an overlay (`src/SettingsPane.cpp`, `settingsClose` at the end of its
search row). So the corner had two crosses side by side, and because `PaneChrome::syncHeaderInset`
had no case for the Settings pane, its search row never gave up the room and the chrome buttons were
drawn over the end of the search box too (`implementer-before.png`).

The chrome's × runs `pane.close` → `closeActive()`, which for Settings bypassed
`closeSettingsPane()`: focus did not go back to where it was, and with Settings as the last pane of
the last tab it would have asked to close the window.

## The change

- `src/SettingsPane.cpp/.h`: the pane's own ✕ is gone; the chrome's × is its close button, as for
  every other pane. New `setHeaderRightInset()` keeps the search row clear of the chrome row.
- `src/Theme.cpp`: the two `QToolButton#settingsClose` rules go with the button.
- `src/main.cpp`:
  - `PaneChrome::syncHeaderInset` insets the Settings pane and a subagent transcript pane.
  - `closeActive()` sends the Settings pane to `closeSettingsPane()`, so the ×, Ctrl+W, Esc and
    Ctrl+Shift+A all close it the same way.
  - `openSubagentPane` hides the transcript's own × (`setHostedInPane(true)`).
  - `placeSubagentOverlay` puts the floating transcript below the pane's header, as the requests
    panel already was; at `y = 8` its × landed on the pane's ×.
- `src/SubagentTranscript.cpp/.h`: `setHostedInPane()` and `setHeaderRightInset()`. The floating
  overlay keeps its ×, which closes only the overlay.

## Audit: everything else with a close button

| Surface | Result |
|---|---|
| Settings pane | **Fixed** (above) |
| Subagent transcript as a pane (`ToolPane` Subagent) | **Fixed**: same double × as Settings |
| Subagent transcript overlay over a narrow pane | **Fixed**: overlapped the pane chrome's × |
| Switchboard (`BoardView`) | Fine: no pane-level close; the card detail's × closes the card, and sits below the header, which already takes the chrome inset |
| Turn details pane (`TurnTranscriptView`) | Fine: no close button of its own |
| File explorer, file preview, plan editor | Fine: no close button of their own; header insets already existed |
| Requests panel (overlay) | Fine: closes itself only, placed below the pane header |
| Reasoning panel, program transcript, restart banner, find bar | Fine: close only themselves and sit below the pane header (rows in the pane's layout) |
| Notifications popup, tab bar ×, window × | Fine: not in pane chrome |
| Remote share (`src/RemoteShare.cpp`) | Fine: no close glyphs |
| Web app (`app/index.html`, `app.js`, `style.css`) | Fine: no close buttons at all; the thread view has one "‹" back button |

## QA checklist

- [ ] Ctrl+Shift+A (or the gear): the Settings pane shows one ×, and the search box ends before the ⬓+ ◫+ ⇱ × row.
- [ ] Click that ×: Settings closes and focus returns to where it was (the prompt box, or vim in the terminal).
- [ ] Make Settings the only pane in the only tab (close the others), then click ×: a terminal pane takes its place; the window does not close.
- [ ] Esc on an empty search and Ctrl+Shift+A still close it.
- [ ] Open a subagent transcript as a pane: one ×, the chrome's; the title row is not covered.
- [ ] Open the floating subagent transcript in a narrow pane: its × is below the pane header, not on the pane's ×.
