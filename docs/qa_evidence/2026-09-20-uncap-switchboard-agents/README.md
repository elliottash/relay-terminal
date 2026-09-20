# Uncap the Switchboard's card agents (#0Z13)

Request (owner, 2026-09-19): *"remove the cap on number of agents in the switchboard"* — the
concurrent card-turn cap of protocol 19.16 (`board_turns.MAX_RUNNING` 3, ceiling 12,
Options › Agent › Switchboard "Cards the agent works at once").

## What changed

- `backend/relay_core/board_turns.py` — `MAX_RUNNING`, `MAX_CARD_TURNS_CEILING`, `full()`,
  `set_max_running()` and the running-count gate in `start()` are gone. Any number of cards may
  run a turn at once; a second turn on the *same* card is still refused, and the `MAX_SESSIONS`
  (6) LRU of kept conversations is untouched — a running card's session is never dropped.
- `backend/relay_core/board_protocol.py` — `_card_turn_limit()` and its `_point()` call are
  gone; `_busy_error()` lost its "past the cap" branch (a stale `board.limits.max_card_turns`
  key from an older GUI is ignored).
- `src/RelayWindow.h` — the Options row and the `board` block in the board worker's
  `configure` payload are removed.
- `docs/AGENT-SESSIONS-PROTOCOL.md` 19.16 — the cap row and the "How many at once" paragraph
  replaced by a note that there is no cap and the old key is ignored.
- Tests: `tests/test_board_turns.py` gains `test_as_many_cards_run_at_once_as_are_started`
  (5 concurrent turns, past the old default and the old ceiling's reach in one test);
  `tests/test_board_protocol.py` gains `test_a_stale_board_block_caps_nothing` (the old option
  key caps nothing) and `test_the_fifth_card_runs_too_no_card_turn_is_refused_for_number`
  (5 cards all running, no `board_busy`).

## Evidence

- `logs/test_board_turns.txt` — 14 tests OK (unittest).
- `logs/test_board_protocol.txt` — 148 tests OK (unittest), including the unchanged refusals
  that must stay: same-card second turn, cleanup exclusivity, page-agent exclusivity.
- `logs/ctest-settings-board.txt` — ctest `settings`, `modelsettings`, `board` pass after the
  Options row removal.
- `scripts/relay-build` — green, 42s (2026-09-19.22H.03 build).

## What a verifier should check

- Plan/Discuss on 4+ different cards at once: all run, each streams into its own card, none is
  refused with `board_busy`.
- A second ask on a card that is already running is still refused, naming the card.
- Options › Agent › Switchboard no longer lists "Cards the agent works at once", and no
  `board/max_card_turns` setting is read anywhere.
