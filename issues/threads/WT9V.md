<!-- relay:entry 20260920T040917Z-87 author=agent kind=event model=glm-5.3 pane=e5d13cf2 turn=5d4205425ef845a0830cd21750e1e4f7/e13e306a0ef3447dba61184260e5e887 -->
- ✦ agent created this card in Inbox · issues/features/2026-09-20-page-agent-asks-its-questions-in-chat-text-not-a.md

<!-- relay:entry 20260920T040924Z-ky author=agent kind=event model=glm-5.3 pane=e5d13cf2 turn=5d4205425ef845a0830cd21750e1e4f7/e13e306a0ef3447dba61184260e5e887 -->
- ✦ agent updated this card · replaced `## Issue`

<!-- relay:entry 20260920T040924Z-kz author=agent kind=rewrite model=glm-5.3 pane=e5d13cf2 turn=5d4205425ef845a0830cd21750e1e4f7/e13e306a0ef3447dba61184260e5e887 -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
it asked them in plain text. thats OK, but i thought we build specialized amber question cards that would come in, in a more structured way
```

</details>

<details><summary>after</summary>

```
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
```

</details>

<!-- relay:entry 20260920T040930Z-xw author=agent kind=event model=glm-5.3 pane=e5d13cf2 turn=5d4205425ef845a0830cd21750e1e4f7/e13e306a0ef3447dba61184260e5e887 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T040939Z-rk author=agent kind=question model=glm-5.3 pane=e5d13cf2 turn=5d4205425ef845a0830cd21750e1e4f7/e13e306a0ef3447dba61184260e5e887 -->
1. How wide should the rule be? (a) every question the page agent asks becomes a `question` comment on a card (creating one when the question is board-level); (b) only questions that gate work — restructures, imports, card-rewrites — while conversational clarifications stay in chat. **Recommend (a)**: it is one taught move, the amber badge is then the single place to look for "the board is waiting on me", and a question asked only in chat is lost when the page closes (the page conversation lives in worker memory, not on disk).

<!-- relay:entry 20260920T040939Z-x3 author=agent kind=event model=glm-5.3 pane=e5d13cf2 turn=5d4205425ef845a0830cd21750e1e4f7/e13e306a0ef3447dba61184260e5e887 -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260920T040943Z-bp author=agent kind=event model=glm-5.3 pane=e5d13cf2 turn=5d4205425ef845a0830cd21750e1e4f7/e13e306a0ef3447dba61184260e5e887 -->
- ✦ agent moved this card · unchanged · Diagnosis filed with a proposed plan; one scoping question waits on the owner.

<!-- relay:entry 20260920T043724Z-2y author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T043724Z-vm author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Discussing → Executing · Execute: handed to a terminal pane

<!-- relay:entry 20260920T043724Z-vn author=owner kind=progress pane=switchboard pane_token=1ec66cc6-8719-466d-be88-35f1025d7052 -->
Executing (1ec66cc6) · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260920T051712Z-dy author=agent kind=progress model=glm-5.3 pane=09a04e4c turn=272d340f0b5947dc92a0609f1522fd9a/103be25575c344c1b79934db25a2274a -->
Landed 0dc94f74 on main — all four plan steps, prompt-only (no code): the brief's new "Ask on a card, not in chat" bullet (question comment + waiting_on: owner + create-the-card-when-board-level + one-line chat reply, survey excepted), the restructure bullet reworded to the same move, rule 3's page-chat clause, and pins so it cannot drift (BriefTest in test_board_chat.py, the rule-3 clause in PromptSectionTests). Evidence: docs/qa_evidence/2026-09-20-page-agent-question-cards/ (tests.txt: 47+4+14 OK; rendered-brief.txt). The one test_board_tools.py hunk was held as contested (hkap-executing also claims that file) and confirmed after review — it contains only the new #WT9V test.

<!-- relay:entry 20260920T051729Z-75 author=agent kind=event model=glm-5.3 pane=09a04e4c turn=272d340f0b5947dc92a0609f1522fd9a/103be25575c344c1b79934db25a2274a -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"commits": ["0dc94f74"], "evidence": ["docs/qa_evidence/2026-09-20-page-agent-q…; appended to `## QA checklist`

<!-- relay:entry 20260920T051733Z-p4 author=agent kind=event model=glm-5.3 pane=09a04e4c turn=272d340f0b5947dc92a0609f1522fd9a/103be25575c344c1b79934db25a2274a -->
- ✦ agent moved this card · Executing → Needs verification · Implemented in 0dc94f74: the brief teaches the question move, the restructure bullet names it, rule 3 covers page-chat turns, and both are pinned. Evidence and QA checklist on the card; the live-model items are the verifier's. · evidence docs/qa_evidence/2026-09-20-page-agent-question-cards/ · implemented_by glm/glm-5.3
