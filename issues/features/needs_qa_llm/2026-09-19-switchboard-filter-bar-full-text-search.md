---
id: PFNS
type: work
status: needs-qa-llm
labels: [feature, switchboard]
implemented_by: glm/glm-5.3
rank: zzzzzzzzw
created: '2026-09-19'
source: pane 1, 2026-09-19
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-board-filter-full-text/], related: [], github: null}
---
# Switchboard filter bar: full text search

## Issue
switchboard filter bar should be full text search

## QA checklist
Evidence: `docs/qa_evidence/2026-09-19-board-filter-full-text/` (NOTES.txt, drive.sh, screenshots, OCR, relay.log, worker logs).

- [ ] `scripts/relay-build` builds; `ctest --test-dir build -R "^board"` and `PYTHONPATH=backend:tests python3 -m unittest tests.test_board_protocol tests.test_board_tools` pass.
- [ ] In the Switchboard pane, type a word that appears only in a card's body: the list keeps exactly that card's section, counts follow, nothing folded.
- [ ] Type a word that appears only in a thread entry (not in any body or title): that card is still found.
- [ ] Type `switchboard`: it must NOT match every card — the thread headers' `pane=switchboard` metadata is excluded from the searchable text.
- [ ] Filter terms still compose: `label:voice <body word>` matches only cards with both.
- [ ] A `board_list` tool result (any agent) carries no `text` on its rows — only the GUI's rows have it.
- [ ] On a real board (this repo: 270 cards, 1.6 MiB `board` message) the Switchboard still opens promptly; the message stays far under the worker's 8 MiB line buffer.
