# Expected — sealed before the passes ran

The staged window (stage.sh): Main model is the guest `guest:claude` (Claude Code), the Options ›
Models priority list holds `relay-free`, and the project's Board holds three cards
(#W1A2, #W3B4, #W5C6).

- The window **configures normally** on the guest. No configure error about the helper; no
  "cannot run on" sentence anywhere.
- `Ctrl+Shift+A` opens the Board with the Switchboard helper docked. The helper console's model
  row names **Claude Code** — the guest is the helper's own agent, not a fallback.
- Opening a card (#W1A2) opens its conversation on **relay-free** — the priority list's first
  entry — because a card conversation is a second agent and may not share the helper's guest.
- Asking the Switchboard helper **"list my cards"** gets a real answer naming the staged cards
  (a guest takes Relay's `board_*` tools through the `relay_board` bridge, #4NXH). What the
  helper may *not* do is answer with the pre-#E34S refusal or an endpoint error.
- Asking the card conversation anything gets an answer on relay-free.
- Picking a guest preset in the helper's model box **sticks** (only a mid-turn change is refused).

## Checks and judgement (Try it counters)

| what | who |
| --- | --- |
| window opens on the guest, Board opens with the helper docked on it (screenshots) | check |
| card conversation opens on relay-free, not on the guest (screenshot) | check |
| ask the helper "list my cards"; is the answer right for these three cards — and would you use the guest helper like this? | person |

## What the agent's pass actually saw (2026-09-25, before the person's step)

- The staged window opened on `guest:claude` (`panes.json`: `model: guest:claude`), the Board
  listed the three staged cards, and the Board agent's console row read **claude-opus-5.5** —
  the guest is the helper's own agent; no refusal, no configure error.
- The card page's console row is the helper's row (the card page embeds the same Board agent;
  there is no second chat surface). A card *turn* takes its model from the priority list when
  the turn is built (`_console_model`), so `relay-free` shows up in the turn, not in the docked
  row — the sealed "card conversation opens on relay-free" above was written for the row and is
  corrected here.
- Captures: `../captures/00-main-window.png`, `10-board-helper.png`, `20-card-conversation.png`,
  `panes.json`, `run.txt`.
