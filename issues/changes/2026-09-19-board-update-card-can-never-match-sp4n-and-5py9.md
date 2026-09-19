---
id: E6XC
type: work
status: inbox
labels: [bug, switchboard]
rank: zzzzzzzzr
created: '2026-09-19'
source: board cleanup pass, 2026-09-19
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# board_update_card can never match #SP4N and #5PY9: their file hashes are not 64 characters

## Issue
Found during the 2026-09-19 board cleanup: board_read returns a file hash that is not 64 characters for two cards, and board_update_card then refuses every base_hash for them, so their labels can never be fixed. Measured: #5PY9 (issues/features/2026-09-19-new-convo-had-incorrect-context-remaining.md) returns a 65-character hash — f386d3142e609b673383a78bab3ddbd7ad05d738184448fc605cbefb4a076fc56 — stable across two reads, and the tool answers "base_hash must be the 64-character hash" for the 65-char form and "does not have the base_hash you sent" for 64-char truncations of it. #SP4N (issues/changes/needs_qa_llm/2026-09-18-settings-as-a-full-pane.md) returns a 63-character hash — a005a8f73dea61b114bb1765c0d6634ac4e712cbedc355c9a56443a0e163024 — refused with the same "must be the 64-character" error. Both cards are stuck without labels: 5PY9 should be bug,gui; SP4N should gain feature beside settings,gui,ux,palette.
