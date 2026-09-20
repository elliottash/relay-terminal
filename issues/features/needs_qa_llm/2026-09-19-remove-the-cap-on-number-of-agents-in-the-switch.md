---
id: 0Z13
type: work
status: needs-qa-llm
assignee: agent
implemented_by: glm/glm-5.3
rank: zzzzzzzzzzr
created: '2026-09-19'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-uncap-switchboard-agents/], related: [], github: null}
---
# remove the cap on number of agents in the switchboard

## Issue
remove the cap on number of agents in the switchboard

## Tasks

- [x] Remove the concurrent cap from board_turns.py (MAX_RUNNING, ceiling, full(), set_max_running, the start() gate) <!-- t:nv -->
- [x] Drop _card_turn_limit and the cap branch from board_protocol.py's _busy_error <!-- t:em -->
- [x] Remove the Options row and the configure board-limits block (RelayWindow.h) <!-- t:cx -->
- [x] Update protocol doc 19.16 <!-- t:b6 -->
- [x] Rewrite the cap tests; add no-cap and stale-key tests <!-- t:pr -->
- [x] Targeted tests + relay-build green; evidence captured <!-- t:rx -->

## QA checklist
Evidence: `docs/qa_evidence/2026-09-20-uncap-switchboard-agents/` (unit tests 14+148 OK, ctest settings/modelsettings/board OK, relay-build green).

- [ ] Plan or Discuss on 4+ different cards at once: every one runs and streams into its own card; no `board_busy` refusal for number.
- [ ] A second ask on a card already running is still refused, naming that card.
- [ ] Options › Agent › Switchboard no longer lists "Cards the agent works at once"; no `board/max_card_turns` anywhere.
- [ ] An older GUI's `board.limits.max_card_turns` key is ignored (covered by `test_a_stale_board_block_caps_nothing`).
