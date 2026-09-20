---
id: WT9V
type: work
status: needs-verification
labels: [feature, switchboard, agent]
assignee: agent
implemented_by: glm/glm-5.3
rank: zzzzzzzzzzzzzzz
created: '2026-09-20'
source: pane 2, 2026-09-20
links: {commits: [0dc94f74], evidence: [docs/qa_evidence/2026-09-20-page-agent-question-cards/], github: null, plans: [], related: []}
---
# Page agent asks its questions in chat text, not as amber question cards

## Issue
The Switchboard page agent (protocol 19.18, `backend/relay_core/board_chat.py`) asks the owner
its questions as plain text in the chat reply. The structured path — a `question` comment
(numbered, with a recommendation) plus `waiting_on: owner`, which is what paints the amber
`waiting:` badge — exists and works (a card Discuss turn used it end to end on #Y2JW the same
night), but the page agent never takes it.

Why, from the code:

- `backend/relay_core/board_chat_brief.md` (the page agent's brief, in every turn's prompt) never
  mentions question cards. Its only asking instruction says the opposite: *"Ask before a
  restructure … described first — one line each — and done when the owner answers"* — ask in the
  conversation.
- The policy block (`board_policy.md`, rule 3: questions go on the card) does reach the page
  agent's system prompt through `Agent.system_prompt` → `board_tools.prompt_section`, but it is
  phrased for terminal panes ("name the card rather than burying the questions in the terminal"),
  and the page-specific brief is the closer instruction, so it wins.
- Many page-agent questions are about the board as a whole ("merge these two?"), with no single
  card to hang a `question` comment on — the model has no taught move for that case.

Nothing is technically blocking: `board_comment {kind: question}` and `board_update_card
{waiting_on: owner}` are both in the page agent's `ChatScope` toolset (`CHAT_BOARD_TOOLS`).

## Plan
Proposal (to discuss before executing):

1. Teach `board_chat_brief.md` the question move: when the page agent needs an answer that gates
   its work, post a `question` comment on the card involved — creating one if the question is
   board-level and no card covers it — set `waiting_on: owner` on that card, keep the chat reply
   to one line naming the card (mirroring policy rule 3's shape).
2. Reword the "ask before a restructure" bullet so it names the same move (describe the
   restructure *on the card(s)* as questions, not in chat prose).
3. Consider one line in `board_policy.md` rule 3 covering page-chat turns, so the rule reads as
   "wherever the agent asks".
4. Keep the survey turn as-is: it is read-only by design (`board_readonly_turn`), so its
   questions stay in chat — that is the confirmation path.

Tests: `tests/test_board_*` pin brief text (drift already bit #2MF1's cases), so a brief edit
updates the pinned assertions with it.

## QA checklist
Prompt-only change (two .md prompt files + pins); no code path moved, so QA is a live-model check of the taught move plus the pinned suites:

- [x] `tests/test_board_chat.py` green — 47 tests, incl. the two new BriefTest pins (implementer, logs/tests.txt)
- [x] `PromptSectionTests` green — 4 tests, incl. the rule-3 page-chat pin (implementer, logs/tests.txt)
- [x] `tests/test_board_turns.py` green — 14 tests; the sibling Discuss/Plan briefs are unaffected (implementer, logs/tests.txt)
- [x] The rendered `chat_brief()` and policy rule 3 carry the question move (implementer, logs/rendered-brief.txt)
- [ ] Live, needs a model: ask the page agent something that gates on the owner (e.g. "merge the two duplicate cards at the top") — the question should land as a numbered `question` comment with a recommendation on the card(s), the card should show the amber `waiting:` badge, and the chat reply should be one line naming the card.
- [ ] Live: a board-level question with no obvious card ("should we archive the done column?") should create a card for it and ask there, not only in chat.
- [ ] Live: the fresh-board survey still asks its one question in chat and writes nothing (the read-only turn is the taught exception).
