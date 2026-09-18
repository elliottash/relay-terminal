---
id: E99H
type: work
status: needs-qa-llm
labels: [bug]
implemented_by: Relay agent (owner's pane), 2026-09-19
rank: zzzzs
created: '2026-09-18'
source: issues/bug_intake.txt, 2026-09-18
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-file-tools-absolute-paths/], related: [], github: null}
---
# File tools refuse absolute paths and parent traversal; the owner does not want that constraint

## Issue
"✗ Absolute paths and parent traversal are not allowed in file tools."

check on that, i dont think i want that constraint

## Decisions
- Owner, 2026-09-18: "check on that, i dont think i want that constraint" — the up-front rejection of absolute paths and `..` was removed. Confinement is unchanged: paths are resolved and must land inside the workspace; symlinks and secret files are still refused.

## QA checklist
- Re-run `tests/test_tools.py` and `tests/test_agent.py` (88 targeted tests passed for the implementer).
- Through a live worker: read one absolute path inside the workspace and one `sub/../file.txt` path — both succeed.
- Through a live worker: `/etc/passwd`, `../etc/passwd`, a symlink out, and `.env` are still refused.
- Evidence: `docs/qa_evidence/2026-09-19-file-tools-absolute-paths/evidence.md` (worker-only change; no GUI screenshots).
