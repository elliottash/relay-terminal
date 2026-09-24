---
id: FW3Z
type: work
status: needs-qa-llm
labels: [bug]
implemented_by: Relay agent (model self-unidentified), 2026-09-17
rank: zzzr
created: '2026-09-17'
source: Relay pane, cleanup audit 2026-09-17
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-17-cleanup-quick-wins/], related: [], github: null}
---
# Cleanup quick wins: dead-code removal, logging for silent failures, stale build dirs, docs index, deploy.sh

## Request
review and implement the wins

## QA checklist
- [ ] Grep confirms each removed symbol has no remaining references
- [ ] `relay` builds and launches; window icon still loads (or logs `window_icon_unavailable`)
- [ ] Session save/delete still works; failed index update now logs
- [ ] `remote/terminal.py` finds the screen bridge in `build-engine/` or `build/`
- [ ] docs/README.md links all resolve
- [ ] `./deploy.sh -n` dry run lists transfers without deploying

Implementer evidence: docs/qa_evidence/2026-09-17-cleanup-quick-wins/
