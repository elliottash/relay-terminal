---
id: CFG1
type: work
status: needs-verification
labels: [bug, switchboard, agents]
assignee: claude-code
rank: h
created: '2026-09-21'
source: 'Claude Code in a Relay pane, 2026-09-21 — found in the owner''s log while working #MDL1'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-console-configure-loop], related: [MDL1, AGNT, SWPH], github: null}
---
# Two consoles in one tab reconfigure the helper worker twice a second, forever

## Issue
Found by Claude Code, not reported by the owner: `~/.local/share/relay/logs/relay.log` held 15,865 `configured` events on 2026-09-21, about two a second for hours, for the Switchboard tab's two consoles (list page and card page). The worker log shows the same storm at 11:41 UTC, before any #MDL1 commit.

## Execution Summary
`RelayWindow::sendFromConsole` re-pointed the tab's console context and re-ran `startBoardWorker`
on *every* line a console sent; a console answers `configured` with lines of its own, so two
consoles in one tab took turns reconfiguring the worker. Now only an `ask` re-points the context;
every other line goes to the worker as it is.

## Tests
No unit seam: measured live. `docs/qa_evidence/2026-09-21-console-configure-loop/`: the card-page
phase of the #AGNT drive, once per binary — 654 `configured` in ~30 s (23/s peak) before, 3 after.

## QA checklist
- [ ] Open the Switchboard, open a card page, leave both for a minute: `relay.log` shows no
      repeating `configured` for the tab's consoles.
- [ ] Ask from the list page, then from the card page: each ask still gets that console's brief
      (the context block follows the ask).
- [ ] Options rows that reconfigure the helper (turn limits) still reach it.
