# Card chip in the pane header (#C7PF)

Owner ask (2026-09-19): "when pane is executing a card, put the # tag in the pane header …
its also clickable to get to the card (keep the one in the prompt box as well)".

## What changed

- `src/Pane.h` — `m_cardChip`, a `QLabel#paneCardChip` in the header row right after the `auto`
  badge: `#` + the id of the card this pane's agent turn is working. Shown while a handed card is
  parked (`m_boardTaskCard`, from the moment Execute hands it — even before the pane's agent is
  configured) or while the running turn carries cards (`m_turnCard`, first of `m_turnCards`).
  Tooltip names every attached card with its title from the board index and says a click opens it.
  A release on the chip without a drag fires `onOpenCard` (the same exchange the prompt box's
  work chip menu makes); dragging from it still moves the pane.
- The turn's cards are keyed by the ask's request id (`m_turnCardAsk`) and the queue item the
  worker makes of it (`m_turnCardItem`, set on `queued`), so the `agent_finished` that ends this
  turn — and only this turn — takes the chip down. An interrupting prompt that sets its own cards
  before the old turn's finish arrives keeps them.
- `updateWorkChip` / the prompt box's work chip are untouched.
- No give-way-ladder change: the chip is counted into `HeaderWants::chips` by `updateHeader`'s
  generic widget walk (whole or absent, like the phone chip); `relay::panes::headerFit` untouched.
- `src/Theme.cpp` — `QLabel#paneCardChip { color: @muted; font-size: 9pt; font-weight: 600; }`
  beside `QLabel#paneCwd` (one rule; every theme derives from the same template).
- `tests/themeswitch_test.cpp` — the five-theme loop checks the new selector's exact spelling and
  that the chip is not a focus mark (no `[relayActive` variant).
- `docs/ARCHITECTURE.md` — one sentence in the pane-header anatomy; rung (5) of the ladder now
  names the chip among what never gives way.

## Build and tests

- `scripts/relay-build` — green in 47 s (2026-09-20.00H.05).
- `ctest --test-dir build -R '^panes$'` (`tests/panelayout_test.cpp`, the ladder), `-R panestate`,
  `-R panestatus`, `-R themeswitch` — all pass. (Note: the ladder test registers as `panes`,
  not `panelayout`.)

## Live drive (Xvfb :94, isolated XDG_CONFIG_HOME, throwaway board)

`build/relay` in `/tmp/c7pf-drive` with a fresh config; a scratch board was initialized there
(`.switchboard/`), one card filed (`#GZM2 drive demo card chip`), then **Execute** (`x` on the
card in the board list). The pane's agent was never configured, so the card stayed parked —
exactly the "from the moment the card is handed" branch. Driven with xdotool; every claim below
is an OCR read (`tesseract`) of the screenshot named beside it.

1. **Chip beside the title at handoff.** The Execute pane's header reads `» c7pf-drive #GZM2 …`
   (4× crop, `02-execute-header-crop-4x.png`; full window `01-execute-pane-header-shows-card.png`).
   The original terminal pane's header in the same window reads `c7pf-drive` alone — no chip
   where no card is attached. The board moved the card to Executing on disk.
2. **Tooltip names the card.** Hovering the chip shows `#GZM2 — drive demo card chip` and
   "Click to open this card in the Switchboard" (`03-chip-tooltip-names-card.png`) — the title
   comes from the board index.
3. **Chip follows its pane, not the board pane.** With the Switchboard closed (Ctrl+Shift+S on
   the board leaf) the chip is still beside the title at the pane's new split position
   (`04-board-closed-chip-still-there.png`, crop `05-board-closed-header-crop-4x.png` reads
   `c7pf-drive #GZM2`).
4. **Click opens the card.** A click on the chip reopens the Switchboard on `#GZM2`'s detail
   (title + Edit/Delete row visible, `06-chip-click-opens-the-card.png`).

Raw drive screenshots and OCR sidecars stayed in `/tmp/c7pf-drive/` (scratch, not committed).

## Not drivable headless — on the QA checklist

- **The turn lifecycle** (`startAgentEntry` → chip shows; `agent_finished` → chip hides) needs a
  configured provider; the drive had none. The parked branch and the same `refreshCardChip()`
  are what the drive exercised; the hide path is the `m_turnCardItem` match in the
  `agent_finished` handler (`src/Pane.h`, grep `#C7PF`).
- **The prompt box's work chip naming the card** appears only once the parked task flushes as a
  turn (`runBoardTask`), i.e. also needs a configured agent; its code is untouched.
- **Dragging the pane from the chip** rides the same press/release pair as the directory label;
  not driven live.
