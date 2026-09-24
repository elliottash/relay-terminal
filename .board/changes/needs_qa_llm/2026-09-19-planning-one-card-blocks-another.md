---
id: DR4K
type: work
status: needs-qa-llm
labels: [bug, switchboard]
component: [worker, gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (1M context), 2026-09-19
rank: zzzzzzzk
created: '2026-09-19'
acceptance: a Plan on one card runs while another card is being planned, each card's turn has its own conversation and its own Stop, and a running card says what it is doing rather than "thinking…"
source: 'conversation, 2026-09-19: "multiple agents working on planning switchboard cards doesnt seem to work"; asked what happened: "it seems like the planning agent was getting stuck. and if i was planning in one card, i couldnt plan in another card."'
links: {plans: [], commits: [fc82f3b, 2f833d5], evidence: [docs/qa_evidence/2026-09-19-several-cards-at-once/], related: [], github: null}
---
# Planning one card blocked every other card, and a running plan looked stuck

## Issue
"multiple agents working on planning switchboard cards doesnt seem to work"

"it seems like the planning agent was getting stuck. and if i was planning in one card, i couldnt
plan in another card."

## What was actually wrong

Two faults, reproduced against the real worker before anything was changed
(`docs/qa_evidence/2026-09-19-several-cards-at-once/before/two-cards-refused.log`):

1. **One card at a time, by construction.** The whole Switchboard worker had one
   `TurnSupervisor`, one `Agent` with one conversation and one `CardScope` slot on that agent's
   board tools. `BoardProtocol._busy_error` refused the second `board_ask` with `board_busy` —
   *"busy with a question on #ZW95"* — and `_ask` called `turns.reset()` whenever the card
   changed, so moving to another card threw away the conversation of the one you left. The rule
   was written for a cleanup, which does have to own the board; a card's Plan does not.
2. **"Stuck" was the pane, not the agent.** A Plan turn is minutes of `search_files`, `read_file`
   and `list_directory` before its first word (16 steps of a 256-step budget in the live run), and
   the card drew a motionless italic "thinking…" for all of it: `CardDetail::render` had one
   static line, and the card branch of `BoardView::handleEvent` consumed `delta` and the terminal
   events only — `status`, `tool_started` and `tool_result`, all already tagged with the card,
   went on the floor. The cleanup notice beside it had been drawing exactly those lines since 19.9.

## Change

**Worker — `fc82f3b`.** New `backend/relay_core/board_turns.py`: a `CardSession` per card with its
own `Agent` (built from the pane agent's provider config, roles and failover switches), its own
conversation, its own `cancel_event` and its own `BoardTools` — which is where `card_scope` lives,
so enforcing what a Plan may touch needed no change in `board_tools.py` at all. Turns on different
cards run at the same time, `MAX_RUNNING` = 3. Still refused: a second turn on the same card, a
cleanup while anything runs and anything while a cleanup runs, and a fourth concurrent card. The
refusal carries `cards` and names them. A card's session outlives its turn, so a second question
on an unchanged card continues its conversation (six kept, LRU). `board_cancel {card?}` is new:
the worker-wide `cancel` is the pane agent's turn, so stopping one card's turn names the card.
Protocol: `docs/AGENT-SESSIONS-PROTOCOL.md` 19.16, with 19.10's "Busy" paragraph rewritten.

**Pane — `2f833d5`.** `m_cardTurns` by card id (mode, unsent question, answer so far, progress
line) in place of the single `m_askCard`: opening a card that is already working gives it back its
strip, its stream and its step, and any other card is idle. Stop sends `board_cancel {card}`. A
progress line in the busy strip (`boardBusyWhat`) through the same `toollabel` the cleanup notice
and the pane's tool lines use. A card with a turn running wears an agent-coloured ✦ in the list in
place of its status glyph — where you see that #B is planning while you read #A.

## QA checklist

- [ ] Open a card, press `p`, and watch the strip: "✦ Agent is planning…" followed by what it is
      doing this second ("Requesting model · step 4/256", "reading Pane.h"), and "✕ Stop planning".
- [ ] Esc back to the list while it runs: that card's row shows an agent-coloured ✦; the others
      keep their status glyphs.
- [ ] Open a second card and press `p`. It plans too. Both rows are marked; neither turn disturbs
      the other, and each card's thread gets its own answer and its own `## Plan`.
- [ ] Go back to the first card mid-turn: its strip, the answer so far and its step line are all
      there, not an empty card.
- [ ] "✕ Stop planning" on one of two running cards stops that one only; the other finishes.
- [ ] Ask a fourth card while three run: refused on the card, the typed message stays in the box,
      and the line names the three cards that are working.
- [ ] Start a cleanup while a card plans: refused, naming the card. Start a card while a cleanup
      runs: refused, as before, with nothing written to the card.
- [ ] Ask the same card twice in a row without editing it: the second question continues the
      conversation (no reseed); edit the card and ask again: it reseeds.
- [ ] `ctest` and `./scripts/test.sh`: `tests/test_board_turns.py`, `tests/test_board_protocol.py`,
      `tests/boardmodel_test.cpp`.

## Implementer check

Claude Opus 5 (1M context), 2026-09-19. Live under Xvfb on a throwaway board with `glm-coding`
(`glm-5.3`): two cards planned at once, both plans written, neither card disturbed — plus the
before/after worker runs over stdio. Evidence and shots:
`docs/qa_evidence/2026-09-19-several-cards-at-once/NOTES.md`.

## Deliberately left

- **The step budget is still 256 with no per-turn time limit.** A Plan that wanders now says so
  on the card rather than looking dead, which is what the owner hit; whether a card turn should
  also have a ceiling (and what it should be) is a product decision, not an obvious fix.
- **`MAX_RUNNING` is 3 and not an option.** Every concurrent turn is a paid provider stream, so
  the number belongs with the owner if he wants it exposed.
