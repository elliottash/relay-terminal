# Several cards planned at once (#DR4K, protocol 19.16, 2026-09-19)

Owner, 2026-09-19: *"multiple agents working on planning switchboard cards doesnt seem to work"*,
and, asked what happened: *"it seems like the planning agent was getting stuck. and if i was
planning in one card, i couldnt plan in another card."*

Both halves were real, and they were two different faults:

1. **One card at a time.** The whole Switchboard worker had one `TurnSupervisor`, one conversation
   and one `CardScope`, so the second `board_ask` was refused with `board_busy`. Reproduced against
   the real worker *before* any change — `before/two-cards-refused.log` below.
2. **"Stuck" was the card, not the agent.** A Plan turn spends minutes in `search_files` and
   `read_file` before its first word, and the card drew a motionless italic "thinking…" for all of
   it. The events that say what it is doing (`status`, `tool_started`, `tool_result`, each already
   tagged with the card) were being dropped by the pane.

Landed in `fc82f3b` (worker: `relay_core/board_turns.py`, one agent, conversation and scope per
card) and `2f833d5` (pane: per-card turn state, the progress line, `board_cancel {card}`, the ✦ on
a working row).

## How this was run

`drive.sh` in this folder, on the build of `2f833d5`. Xvfb at 1500x950 with its own `HOME`,
`XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR`; the only
thing shared with the desktop is the D-Bus socket, so the `glm-coding` key can be read from the
keyring. The key is never printed and is in no file here. Model: `glm-5.3`.

The workspace is a **throwaway git repo** — `issues/board.yaml`, three cards (`#ZW95`, `#SPBN`,
`#K7Q2`) and a toy `src/complete.py`. This repository's own `issues/` was not touched.

Paid calls: two Plan turns (plus the two of the before/after worker runs).

## The shots

| File | Shows |
|---|---|
| `implementer-01-board` | The Switchboard on the throwaway board, every section folded as a new pane starts. |
| `implementer-01b-inbox-open` | Inbox open: the three cards, each with its status glyph. |
| `implementer-02-first-card-planning` | `p` on the first card. The strip over the reply box: **"✦ Agent is planning… Requesting model · step 3/256"** and **"✕ Stop planning"**. The step line is the new part — this is what a card showed as a motionless "thinking…" before. |
| `implementer-03-list-while-one-plans` | Esc back to the list while that turn runs: the planning card's row wears an agent-coloured **✦** in place of its status glyph. |
| `implementer-04-second-card-planning` | A **second** card planned while the first is still going: its own strip, its own step line. This is the refusal the owner hit, gone. |
| `implementer-05-list-with-two-running` | The list with **two** rows marked at once (#SPBN and #K7Q2), the third untouched. |
| `implementer-06-list-after` | Both finished: no marks left, and each card carries a thread reply. |
| `implementer-07-a-plan-on-its-card` | #K7Q2's `## Plan` on the card — Goal, Findings with real paths, numbered Steps, Risks, Verify — written by one of the two concurrent turns. |

`implementer-cards-after.txt` is the three card files as the run left them: **#SPBN and #K7Q2 each
have their own `## Plan`**, written by turns that overlapped, and #ZW95 (never asked) is untouched.
`implementer-notes.txt` is the OCR of each shot, for grepping.

## Before and after, at the worker

Driven straight against `backend/worker.py` over stdio (no GUI), same throwaway board, same model.

**Before** (`before/two-cards-refused.log`) — the second Plan, 8 s into the first:

```
[  3.0] >>> board_ask {card: ZW95, mode: plan}
[  7.9] <<< tool_started board_read / search_files …
[ 11.0] >>> board_ask {card: SPBN, mode: plan}
[ 11.0] <<< error board_busy "The Switchboard agent is busy with a question on #ZW95. Stop it first…"
```

**After** (`after/two-cards-run.log`) — the same two asks, six seconds apart:

```
[  9.0] >>> board_ask {card: SPBN, mode: plan}
[  9.0] <<< status "Requesting model · step 1/256"  card_id=SPBN
[  9.4] <<< tool_started read_file      card_id=ZW95      ← the first turn, still going
[ 13.3] <<< tool_started board_read     card_id=SPBN
…
[ 86.4] <<< done card_id=SPBN mode=plan
```

Both wrote their own `## Plan`; neither touched the other's card.

## Checks that are not pictures

- `tests/test_board_turns.py` — the pool: two cards at once, a second turn on the same card
  refused, the cap, the scope opened per turn and closed when its thread unwinds, stop one and
  leave the other, LRU eviction of idle conversations, and the unwind race the pane triggers by
  asking again the moment it sees `done`.
- `tests/test_board_protocol.py` — the same from the protocol side against `tests/fake_cards.py`:
  the refusal names every running card, `board_cancel` stops one, a card keeps its conversation
  between turns, a repointed worker forgets them all.
- `tests/boardmodel_test.cpp::aCardKeepsItsOwnTurnWhileAnotherCardIsOnScreen` — the pane: plan one
  card, open another and plan it too, the first finishing leaves the second's strip up, coming back
  to each shows the right thing, and a `board_busy` keeps the typed message in the box.
- `ctest` and `./scripts/test.sh` pass (one unrelated failure at the time of writing:
  `test_remote_wire`'s allow-list is missing another session's `board_folder_changed`).
