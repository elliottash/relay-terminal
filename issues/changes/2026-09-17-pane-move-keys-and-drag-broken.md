---
id: ERES
type: work
status: ready
component: [gui]
milestone: desktop-alpha
workstream: terminal
rank: zz
created: '2026-09-17'
labels: [bug]
acceptance: Ctrl+Alt+arrow moves the focused pane in a split layout on the owner's KDE desktop, and dragging a pane by its grip moves it; both covered by a regression test or recorded evidence
source: '`issues/bug_intake.txt`, 2026-09-17: "ctrl + alt + right doesnt work to move the pane right. dragging panes doesnt work."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Ctrl+Alt+arrow pane moves and pane dragging do not work

## Report

Owner, 2026-09-17, on a real KDE desktop with the Relay engine as the default: Ctrl+Alt+Right does not move
the pane right, and dragging a pane by its grip does nothing. Both were shown working under Xvfb when they
were implemented (`docs/qa_evidence/2026-09-17-pane-tab-buttons-moving/`).

## Checked so far

- Not a desktop shortcut clash: `~/.config/kglobalshortcutsrc` binds "Switch One Desktop to the Right" to
  Meta+Ctrl+Right, not Ctrl+Alt+Right, and "Switch to Next Desktop" is unbound.

## Suspects

- The prompt-box-only input change (`src/InputPolicy.*`, terminal widgets set to `Qt::NoFocus`, focus bounced
  back to the composer) may swallow or redirect the key and the mouse press on the grip.
- `PaneChrome` drag uses an event filter on the grip; the engine view's mouse handling changed in the same
  commit (it no longer grabs focus on press).
- Pane moves may need two panes in the same splitter with the right orientation; check the no-op paths in
  `runAction("pane.moveRight")`.
