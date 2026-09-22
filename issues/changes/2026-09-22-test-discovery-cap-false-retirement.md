---
id: TC5K
type: work
status: discussing
labels: [bug, tests]
rank: mtc5k
created: '2026-09-22'
source: Codex measured during M7RY verification, 2026-09-22
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-user-memory/], related: [M7RY, 7BM4], github: null}
---
# Truncated test discovery falsely marks existing tests retired

## Issue
Observed while verifying #M7RY: `TestsCommands('.', 'issues').check_card('M7RY')` reports `tests/test_user_memory_tools.py is not in the project any more`, despite the file being present, committed and its four unittest cases passing.

## Planning notes
Measured after commit 6d8cbe29: `test_probe.discover(Path.cwd())` returns 5,000 entries (92 CTest and 4,908 unittest), `truncated: true`, with no user-memory tests. `test_probe.discover_unittest_file(Path.cwd(), Path.cwd() / 'tests/test_user_memory_tools.py')` returns all four tests. `MAX_TESTS` is 5,000. The truncated catalog is being treated as complete when classifying absent references as retired. Preserve uncertainty on truncated discovery or resolve referenced files directly before declaring retirement.

## Done means
- A referenced existing test beyond the global discovery limit is not labeled retired.
- Truly removed tests still have a reliable retirement result.
