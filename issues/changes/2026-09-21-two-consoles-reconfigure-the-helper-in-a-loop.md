---
id: CFG1
type: work
status: discussing
waiting_on: owner
labels: [bug, switchboard, agents]
assignee: codex
rank: h
created: '2026-09-21'
source: 'Claude Code in a Relay pane, 2026-09-21 — found in the owner''s log while working #MDL1'
links: {plans: [], commits: [4764200e], evidence: [docs/qa_evidence/2026-09-21-console-configure-loop, docs/qa_evidence/2026-09-22-verify-CFG1/README.md], related: [MDL1, AGNT, SWPH], github: null}
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
`ctest -R consolemode`
`ctest -R agentcontext`
manual: docs/qa_evidence/2026-09-22-verify-CFG1/README.md
manual: docs/qa_evidence/2026-09-21-console-configure-loop/NOTES.md

### Check 2026-09-21 21:12
- passed · ctest:consolemode — ctest -R consolemode passed for this revision on spark-dcc9, 2026-09-22T01:12:46Z
- passed · ctest:agentcontext — ctest -R agentcontext passed for this revision on spark-dcc9, 2026-09-22T01:12:46Z
- not-applicable · manual:docs/qa_evidence/2026-09-22-verify-CFG1/README.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-22-verify-CFG1/README.md
- warning · manual:docs/qa_evidence/2026-09-22-verify-CFG1/README.md — manual evidence docs/qa_evidence/2026-09-22-verify-CFG1/README.md is not there
- warning · card — none of the listed tests is named after anything this card changed (docs/qa_evidence/2026-09-21-console-configure-loop/NOTES.md, docs/qa_evidence/2026-09-21-console-configure-loop/configured-new.txt, docs/qa_evidence/2026-09-21-console-configure-loop/configured-old.txt…)
history: thread
## QA checklist
- [x] Open the Switchboard, open a card page, leave both for a minute: `relay.log` shows no
      repeating `configured` for the tab's consoles.
- [x] Ask from the list page, then from the card page: each ask still gets that console's brief
      (the context block follows the ask).
- [ ] Options rows that reconfigure the helper (turn limits) still reach it.

## Done means
Both Switchboard consoles remain idle for at least 60 seconds without repeated configure messages. Each console ask supplies its own brief. Changing the Options turn limit reaches the helper worker.

## Plan
Verify the existing implementation with focused tests and a fresh isolated Xvfb profile. Record screenshots and protocol/layout evidence. Fix only a defect within this card, then record the measured verdict. Shared code changes require coordination; unrelated session work is excluded.

## Verdict
PARTIAL PASS; not closed. Fresh isolated Xvfb/config verification on 2026-09-22 UTC measured 61 seconds with zero configure messages and successful card/list asks with their respective context blocks. The Options propagation check FAILS: changing Step limit per turn to 37 updates the terminal worker, but the helper remains at max_steps=500 until another action reconfigures it. `result.json`, `wire.jsonl`, logs and screenshots record the failure. Targeted tests pass but do not cover this gap.

A reviewable fix is in `docs/qa_evidence/2026-09-22-verify-CFG1/proposed-fix.patch`: apply the existing alsoBoardWorkers wrapper to the two Security turn-limit rows. Not applied pending the user-required coordination: src/RelayWindow.h is claimed by mdl1verify and other live sessions, and this harness exposes no Relay agent_message tool.
