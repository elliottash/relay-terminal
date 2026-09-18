---
id: P2WD
type: work
status: needs-qa-llm
labels: [change, bug]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'Ctrl+, then Ctrl+Shift+A leaves both panes on screen and both title-bar buttons lit; each key and each button closes only its own pane; "Options" chosen inside Actions opens Options beside it instead of replacing it; a theme change still redraws both; `ctest` and `./scripts/test.sh` pass'
source: 'owner, 2026-09-18: "another bug i noticed is that you cant have the options menu and actions menu both open simultaneously"'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-options-and-actions-side-by-side/'], related: [SPBN], github: null}
---
# Options and Actions are two panes, open at the same time

## Issue

Owner, 2026-09-18: "another bug i noticed is that you cant have the options menu and actions menu
both open simultaneously".

## Report

It was built that way and the comment said so — "Two keys, one pane". `openSettingsPane()` asked
`settingsPaneIn(page)` for *any* pane whose `settings()` was non-null, and if the mode it found
was not the mode asked for it called `setMode()` on it. So Ctrl+Shift+A with Options open did not
open Actions beside it; it turned the Options pane into the Actions pane, and Ctrl+, turned it
back. The pane is where a setting is read *and* where the action that uses the setting is run, so
the one arrangement the two-pane layout exists for — a setting on screen next to the action that
depends on it — was the one arrangement it refused.

The title bar had already grown a light per pane type (#SPBN), and its own comment recorded the
consequence honestly: "the Settings pane is one pane in two modes, and its `paneType` follows the
mode, so Actions and Options are never both lit."

## Change

Each mode is its own pane. Nothing about either pane's contents changed.

- `settingsPaneIn(page, mode)` replaces `settingsPaneIn(page)`: the pane **in that mode**,
  preferring the focused one when a page holds two of a kind. `settingsPanesIn(page)` returns all
  of them, for the callers that mean every pane (a keymap reload or a theme change redraws both).
- `openSettingsPane(mode)` looks that pane up, and creates one beside the focused leaf when there
  is none. The `setMode()` swap is gone, so a key never reaches into the other mode's pane.
- `toggleSettingsPane()` is unchanged in shape and now means what it says: the key closes the
  pane when that pane has the focus, and otherwise opens or focuses its own.
- The two `scrollToGroup()` callers (the ssh menu, the agents menu) name `Mode::Actions`, which is
  the pane they had just opened.
- "Options" chosen from inside Actions opens the Options pane beside the list it was chosen from,
  and the list stays: reading a setting next to its action is the point.
- The title-bar buttons needed no change — they already key off `paneType`, which is per mode —
  so both light up, and each closes only its own pane. Their comment no longer claims otherwise.

A row inside Actions that reveals an option still turns *that* pane into an Options pane in place
(`SettingsPane::revealOption`), which is why the lookup prefers the focused pane: a page can hold
two Options panes for as long as the second one is open.

`m_returnPane` / `m_returnFocus` stay single: opening Actions from a focused Options pane records
the Options search box, so closing Actions returns focus there, and closing Options then falls
back to the pane it was opened from. That is the behaviour either order gives.

## Evidence

`docs/qa_evidence/2026-09-18-options-and-actions-side-by-side/` — `drive.sh` and its screenshots
under Xvfb with an isolated `HOME`, `XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR`:

- `implementer-both-ibm-beige-2-both.png`, `implementer-both-relay-dark-2-both.png` — Options,
  then Actions: both panes on screen, the tab reading "project; Options; Actions · 3", and the
  bolt and the gear both lit in their own header colours.
- `implementer-close-ibm-beige-1-actions-gone.png` — Ctrl+Shift+A with Actions focused closes
  Actions only: Options is still there and only the gear is lit.
- `implementer-close-ibm-beige-2-both-gone.png` — Ctrl+, then closes Options.

## QA checklist

- [ ] Ctrl+, opens Options; Ctrl+Shift+A then opens Actions beside it and both stay.
- [ ] The reverse order (Actions first, then Options) gives the same two panes.
- [ ] Both title-bar buttons are lit while both panes are open; clicking one closes only its pane
      and leaves the other lit.
- [ ] Ctrl+Shift+A while the Actions pane has focus closes Actions; while Options has focus it
      moves focus to Actions rather than swapping the Options pane.
- [ ] Esc in one pane closes that pane and puts focus back where it came from.
- [ ] "Options…" chosen in the Actions list opens Options and leaves the Actions list open.
- [ ] Ctrl+? (shortcuts) and the ssh / agents menus still land on the Actions pane, scrolled to
      their group, with an open Options pane untouched.
- [ ] Changing the theme or reloading the keymap redraws both panes, not just one.
- [ ] Closing the last pane of the last tab still puts a terminal beside it instead of closing
      the window.
- [ ] A second window, and a second tab, light their own buttons from their own panes.
