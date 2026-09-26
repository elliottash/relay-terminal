---
id: 80X1
type: work
status: needs-verification
labels: [feature, workflow, land]
assignee: codex
parent: 3MH4
discovered_from: 3MH4
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, criteria: 'Focused real-Git tests prove private native and guest workspace routing, canonical Board, resume, legacy behavior and failure refusal', sign_off: none, effort: high, stakes: rework, blast: capability}
source: 'Approved #3MH4 implementation workstream, 2026-09-26'
links: {plans: [], commits: [f5606d58d763, bf6231c20a65, 5d40b83c2239, 34077c2b2802, b2d455429dea], evidence: [], related: [], github: null}
---
# B2: Native and guest pane workspace integration

## Issue
Separate canonical project identity from execution cwd; allocate before process startup and route tools, Board and guests consistently.

## Done means
Queue development launches get a leased, private workspace before any process starts; failure refuses launch. Native, guest, Board, scratch and child agents retain one canonical project/Board identity. Legacy paths and resume remain stable. Focused tests prove isolation and refusal with temporary Git repositories.

## Plan
1. Add a pure workspace preparation and status adapter around the registered tree and accepted configuration APIs.
2. Pass immutable identity through worker, tools, guest launch, Board, scratch and child creation.
3. Make the Board CLI resolve registered project roots; document protocol and run focused tests.

## Execution Summary
Added pre-launch workspace preparation, worker and guest routing, private child workspaces, canonical Board and scratch lookup, queue commit guidance, queue status helper, and protocol 37. This checkout remains unregistered/legacy.

## Tests
### Check
Pass: `python3 scripts/land.py try b2-80x1 --verify-cmd "PYTHONPATH=backend python3 -m pytest -q tests/test_workspace_context.py tests/test_guest_launch.py tests/test_subagents.py tests/test_board_tools.py tests/test_tools.py tests/test_guest_harness_provider.py -k not\ test_preset_rows"` — exact tree verified.
Pass: `PYTHONPATH=backend python3 -m pytest -q tests/test_workspace_context.py` (2 tests, real temporary Git repo).
Pass: `python3 scripts/land.py try b2-80x1-policy --verify-cmd "PYTHONPATH=backend python3 -m pytest -q tests/test_workspace_context.py"` — exact tree after B1 landed; test changes target config after activation and proves the accepted quota still governs two workspaces.
Independent C1 verification should stage native and guest pane launch in an isolated profile after B3 wiring.
