---
id: ACT1
type: work
status: done
labels: [bug, panes, sessions]
assignee: codex
rank: h
created: '2026-09-21'
source: 'Claude Code in a Relay pane, 2026-09-21 — found while landing #MDP1 t:a11'
links: {plans: [], commits: [7dd8fdad, 0babc4281ec481752daf9885af2138cea04335fc, 92f3e5b31e5d390ba517a7ef7d588d40b3569a5e], evidence: [docs/qa_evidence/2026-09-21-models-pane, docs/qa_evidence/2026-09-22-verify-ACT1/README.md], related: [QT8C, MDP1], github: null}
---
# A saved Activity pane drops its whole tab on reopen

## Issue
Found by Claude Code, not reported by the owner: `PaneChrome::serialize` writes the Activity pane (card #QT8C) into the saved layout as an `internals` node and `buildNode()` restores it, but `relay::windowstate::isUsableNode` never listed that kind. An unknown node makes its whole split unusable and the tab is dropped with it, so a tab holding a terminal beside an Activity pane came back as nothing — scrollback, directory and conversation gone. The models pane hit the identical trap on 2026-09-21 (`bb5fba2b`, driven in `docs/qa_evidence/2026-09-21-models-pane/`, `l-after-restart.png` of the first run).

## Execution Summary
One line in `src/WindowState.cpp`: an `internals` node whose value is an object is usable, like `settings`, `testsuites` and `models`.

## Tests
`ctest -R windowstate`
manual: docs/qa_evidence/2026-09-22-verify-ACT1/README.md

### Check 2026-09-25 14:10
- passed · ctest:windowstate — ctest -R windowstate passed for this revision on spark-dcc9, 2026-09-25T18:10:13Z
- not-applicable · manual:docs/qa_evidence/2026-09-22-verify-ACT1/README.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-22-verify-ACT1/README.md
- warning · manual:docs/qa_evidence/2026-09-22-verify-ACT1/README.md — manual evidence docs/qa_evidence/2026-09-22-verify-ACT1/README.md is not there
history: thread
## QA checklist
- [x] Open an Activity pane beside a terminal, quit, reopen with no arguments: the tab is back with both.

## Done means
Quitting with Activity beside a terminal and reopening with no arguments restores both panes in the original tab, retaining the terminal directory and scrollback identity.

## Plan
Verify the existing implementation with focused tests and a fresh isolated Xvfb profile. Record screenshots and protocol/layout evidence. Fix only a defect within this card, then record the measured verdict. Shared code changes require coordination; unrelated session work is excluded.

## Verdict
PASS — independent verification on 2026-09-22 UTC. Fresh isolated Xvfb/config run quit with Activity beside a terminal, reopened with no arguments, and restored both. Saved/restored tab trees match, including terminal cwd, scrollback UUID and Activity owner. `windowstate` passed in recorded run `20260922T011245Z-3882`; `tests_check` has no findings or blocking signals. Evidence: `docs/qa_evidence/2026-09-22-verify-ACT1/README.md`. No product-code changes were needed.
