# #WT9V — the page agent asks its questions on a card, not only in chat

Implemented 2026-09-20 · evidence from the implementing pane.

## What changed

The Switchboard *page* agent (protocol 19.18) asked the owner its questions as plain chat
text, even though the structured path — a `question` comment plus `waiting_on: owner`, which
paints the amber `waiting:` badge — was in its toolset the whole time. Two prompt files are
the fix; no code changed:

- `backend/relay_core/board_chat_brief.md` — a new bullet teaches the move: an answer that
  gates the agent's work is asked as a `question` comment (numbered, with a recommendation)
  on the card involved, `waiting_on: owner` set on it, a board-level question with no card
  creates the card first, and the chat reply is one line naming the card — the page
  conversation is not kept on disk, so a question left only in chat is lost when the page
  closes. The survey's opening question stays in chat: that turn writes nothing by design.
  The old "Ask before a restructure" bullet now names the same move (the restructure is
  asked as questions *on the card(s) it touches*, not described in chat prose).
- `backend/relay_core/board_policy.md` — rule 3 now reads "… rather than burying the
  questions in the terminal **or the board page's chat** — wherever you ask, the card is
  where the question waits", so the rule covers page-chat turns and not just panes. This
  block already reached the page agent through `Agent.system_prompt` →
  `board_tools.prompt_section`.

Pinned so the move cannot drift out of the prompt (the lesson of #2MF1):

- `tests/test_board_chat.py` — `BriefTest` pins the brief's question-move phrases and the
  survey prompt's "writes nothing / the owner's answer is the confirmation".
- `tests/test_board_tools.py` — `PromptSectionTests.test_rule_3_covers_wherever_the_agent_asks`
  pins the policy's new clause.

## Verification (implementer)

`logs/tests.txt` — all green, run with `scripts/test.sh`'s isolation
(`RELAY_KEYRING=off`, scratch `XDG_DATA_HOME`):

- `tests/test_board_chat.py` — 47 tests OK (was 45; the two `BriefTest` cases are new).
- `tests/test_board_tools.py :: PromptSectionTests` — 4 OK (the rule-3 pin is new).
- `tests/test_board_turns.py` — 14 OK (sibling Discuss/Plan briefs unaffected).

`logs/rendered-brief.txt` — `board_chat.chat_brief()` (comment-stripped, as every turn's
prompt carries it) and `board_tools.policy_text()` rule 3, as they now render.

Nothing technical changed in the tools: `board_comment {kind: question}` and
`board_update_card {waiting_on: owner}` were already in the page agent's `ChatScope`
(`CHAT_BOARD_TOOLS`), so no GUI/worker change was needed for the badge to paint.

## What QA should still look at (needs a live model)

The rule is prompt text; the suites prove it ships in the prompt, not that a model takes it.
Open a Switchboard page, ask the page agent something that should gate on the owner (e.g.
"merge the two duplicate cards at the top"), and check: the question lands as a numbered
`question` comment with a recommendation on the card(s), the card shows the amber
`waiting:` badge, and the chat reply is one line naming the card. Then a board-level
question with no obvious card ("should we archive the done column?") should create a card
for it rather than ask only in chat. The fresh-board survey still asks in chat and writes
nothing.
