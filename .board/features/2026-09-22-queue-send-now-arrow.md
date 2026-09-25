---
id: QSN1
type: work
status: needs-verification
labels: [feature, queue, ui]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: mqsn1
created: '2026-09-22'
source: Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [2f1697795be2d3e00164b4eeeb3a59beefff7581], evidence: [docs/qa_evidence/2026-09-22-queue-send-now/], related: [VZ8C], github: null}
---
# Queue send-now arrow

## Issue
add a right arrow to the queue that indicates send now. hover over says send now (ctrl + enter)

## Done means
- Each sendable queued agent prompt has a right arrow on its row, beside remove; no header arrow.
- Hover shows Send now and the live Ctrl+Enter shortcut.
- Clicking sends that row without losing an unrelated draft or changing the remaining queue order.

## Decisions
User: "not in the queue header -- on the row"

## Tests
- `manual: docs/qa_evidence/2026-09-22-queue-send-now/README.md`
- `ctest -R '^consolemode$'`

### Check 2026-09-24 18:49
- not-applicable · manual:docs/qa_evidence/2026-09-22-queue-send-now/README.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-22-queue-send-now/README.md
- passed · ctest:consolemode — ctest -R consolemode passed for this revision on spark-dcc9, 2026-09-24T22:49:05Z
- notice · ctest:consolemode — ctest -R consolemode is slow: p95 6.69 s, p50 1.62 s
history: thread
## Execution Summary
Added → beside × on sendable agent queue rows, using the existing interrupt operation. Hover shows Send now (Ctrl+Enter), following the live binding. The isolated real-Pane click/tooltip test passes and preserves the remaining queue and unrelated draft. Worker-owned rows and shell/TUI commands have no equivalent operation and do not offer the arrow. Broader consolemode has an existing repeated-Enter assertion failure tracked by #VZ8C.

![Queue row arrows and send-now tooltip](docs/qa_evidence/2026-09-22-queue-send-now/01-arrow.png)
