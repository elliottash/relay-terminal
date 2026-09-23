---
id: BRS2
type: work
status: needs-verification
labels: [bug, guest, mcp]
assignee: codex
rank: mbrs2
created: '2026-09-23'
source: 'User report in Relay, 2026-09-23'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-BRS2/], related: [4NXH, GSK7], github: null}
---
# Resuming a guest conversation drops Relay MCP tools

## Issue
debug this, which is happening a lot: "
Relay’s board tools aren’t available in this turn, "

## Done means
- Resuming another saved session preserves the pane-owned bridge and guest instructions for both harnesses.
- A failed resume leaves the original harness and bridge usable and closes the failed replacement.
- Regressions exercise the actual provider resume path without a paid model call.

## Plan
The live session has no relay_board tools and its Codex app-server argv has no bridge overrides. `guest_harness_provider.resume_session` creates a replacement harness without board_bridge or instructions, unlike new_provider. Preserve the provider's original instructions and bridge descriptor on replacement, test successful and failed resumes, and validate the provider/adapter/bridge tests. Use file fallback because the affected session cannot discover board tools.

## Execution Summary
Fixed resume_session dropping the bridge descriptor and guest instructions on harness replacement. Preserved the provider-owned bridge and original instructions; failed replacement processes are closed while the original session stays usable. Current live app-server launch lacked MCP overrides, matching the reproduced cause. Restart Relay to load the updated worker.

## Tests
- `tests/test_guest_harness_provider.py`
- `tests/test_guest_harness_codex.py`
- `tests/test_guest_harness_claude.py`
- `tests/test_guest_board_bridge.py`
- manual: docs/qa_evidence/2026-09-23-BRS2/README.md
