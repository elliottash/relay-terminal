---
id: CFG1
type: work
status: done
labels: [bug, switchboard, agents]
assignee: codex
rank: h
created: '2026-09-21'
source: 'Claude Code in a Relay pane, 2026-09-21 — found in the owner''s log while working #MDP1'
links: {plans: [], commits: [c3312a38f031bbdd8734bae95b80bca1ac4e7f5f, 4764200e, 92f3e5b31e5d390ba517a7ef7d588d40b3569a5e], evidence: [docs/qa_evidence/2026-09-21-console-configure-loop, docs/qa_evidence/2026-09-22-verify-CFG1/README.md], related: [MDP1, AGNT, SWPH], github: null}
---
# Two consoles in one tab reconfigure the helper worker twice a second, forever

## Issue
Found by Claude Code, not reported by the owner: `~/.local/share/relay/logs/relay.log` held 15,865 `configured` events on 2026-09-21, about two a second for hours, for the Switchboard tab's two consoles (list page and card page). The worker log shows the same storm at 11:41 UTC, before any #MDP1 commit.

## Execution Summary
`RelayWindow::sendFromConsole` re-pointed the tab's console context and re-ran `startBoardWorker`
on *every* line a console sent; a console answers `configured` with lines of its own, so two
consoles in one tab took turns reconfiguring the worker. Now only an `ask` re-points the context;
every other line goes to the worker as it is.

The fresh verification found an additional gap: the two Security turn-limit rows changed only
the active terminal worker. Both rows now use the existing `alsoBoardWorkers()` wrapper, so
number changes and resets also reconfigure live helper workers. The post-fix live assertions
pass for step limit 37 and tool-call limit 43 without a subsequent ask or context switch.

## Tests
`ctest -R consolemode`
`ctest -R agentcontext`
manual: docs/qa_evidence/2026-09-22-verify-CFG1/README.md
manual: docs/qa_evidence/2026-09-22-verify-CFG1/post-fix/provenance.txt
manual: docs/qa_evidence/2026-09-21-console-configure-loop/NOTES.md

### Check 2026-09-23 19:23
- passed · ctest:consolemode — ctest -R consolemode passed for this revision on spark-dcc9, 2026-09-23T23:23:39Z
- missing-evidence · ctest:agentcontext — no run of ctest -R agentcontext for this revision, from any host, and no attached result
- not-applicable · manual:docs/qa_evidence/2026-09-22-verify-CFG1/README.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-22-verify-CFG1/README.md
- not-applicable · manual:docs/qa_evidence/2026-09-22-verify-CFG1/post-fix/provenance.txt — manual evidence, recorded by hand: docs/qa_evidence/2026-09-22-verify-CFG1/post-fix/provenance.txt
- not-applicable · manual:docs/qa_evidence/2026-09-21-console-configure-loop/NOTES.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-21-console-configure-loop/NOTES.md
- notice · ctest:consolemode — ctest -R consolemode is slow: p95 2.43 s, p50 0.97 s
history: thread
## QA checklist
- [x] Open the Switchboard, open a card page, leave both for a minute: `relay.log` shows no
      repeating `configured` for the tab's consoles.
- [x] Ask from the list page, then from the card page: each ask still gets that console's brief
      (the context block follows the ask).
- [x] Options rows that reconfigure the helper (turn limits) still reach it.

## Done means
Both Switchboard consoles remain idle for at least 60 seconds without repeated configure messages. Each console ask supplies its own brief. Changing the Options turn limit reaches the helper worker.

## Plan
Verify the existing implementation with focused tests and a fresh isolated Xvfb profile. Record screenshots and protocol/layout evidence. Fix only a defect within this card, then record the measured verdict. Shared code changes require coordination; unrelated session work is excluded.

## Verdict
Original verification: the loop and brief checks passed, while Options propagation failed. The coordinated two-row fix is now implemented. Its fresh post-fix driver PASS is recorded in `post-fix/result.json`: 61 idle seconds, zero configures during idle, both ask types with their matching briefs, and the same helper receives max_steps=37 and max_tool_calls=43 with zero later asks or context switches.

Independent parent review passed on 2026-09-22. Recomputed every protocol assertion from the captured wire: 61 seconds idle, each ask preceded by its own context, and step/tool limits 37/43 reach the same helper without a later ask or context switch. Inspected the final screenshot. The exact committed tree built successfully through land.py (c3312a38). Closed with all three QA items passed; parent-review.txt records the review. No owner approval is pending. Build provenance and exact binary SHA256 are in `post-fix/provenance.txt`. The prior failed trace is preserved in the evidence root.
