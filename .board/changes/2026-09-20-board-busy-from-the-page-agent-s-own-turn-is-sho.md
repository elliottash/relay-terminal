---
id: ZGF5
type: work
status: planned
labels: [bug, switchboard, gui]
rank: zzzzzzzzzzzzzzy
created: '2026-09-20'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# board_busy from the page agent's own turn is shown as "already working on" the running cards

## Issue
check that the maximum agent cap in the switchboard is removed. i got this error that the agent was already working on 3 other cards so couldnt plan

## Evidence
Found 2026-09-20 while checking #0Z13 (the uncap), from the code:

- Worker (`board_protocol.py` `_busy_error`): every refusal branch emits the same shape — `"cards": running_cards` — including the `self.chat.busy()` branch ("the Switchboard page agent's turn"), whose specific reason only reaches the `text` field.
- GUI (`BoardPane.cpp`, the `board_busy` handler ~line 3790): the worker's `text` is discarded; the shown message is picked from `cleanup_running` / `cards.size()` / `card_id`. With 2+ card turns running, a refusal whose real reason is the page agent's turn renders as *"The agent is already working on #A, #B."* — the running cards are listed as the cause though none of them blocked the ask.

Post-#0Z13 this matters more: a fourth card's plan is refused only by a same-card turn, a cleanup, or a busy page agent, and the last of those is the one the pane cannot name. The pane should surface the worker's `text` reason when it is not a card-count refusal.

## Done means
When the Board worker refuses an ask with `board_busy`, the desktop Board pane shows the worker's own `text` reason — e.g. "…busy with the Board console's turn…" — instead of blaming whatever cards happen to be running. Failure looks like: the console's own turn blocks a plan while two card turns are running, and the pane still says "The agent is already working on #A, #B." The web board view (`app/board.js`) already shows `text`; no change there. A `board_busy` event with no `text` (an older worker) still gets the pane's reconstructed message, and the busy lamps/strip keep following the `cards` field.

## Plan
**Goal.** The desktop Board pane shows the real reason a `board_busy` refusal happened — the worker's `text` — instead of rebuilding a message from the running-cards list, which mislabels a console-turn refusal as "already working on #A, #B".

**Findings.**

- Worker: `backend/relay_core/board_protocol.py`, `_busy_error` (~1876–1920). Every refusal branch emits `"cards": running_cards` plus a `"text"` that names the actual blocker: "a Board cleanup" (1903), "the Board console's turn" (1907, the `self.console and turns.busy` branch at 1904), "a <mode> on #<card>" (1909), or the card phrase / "an agent turn" (1912–1913). The wire shape is fine and documented (`docs/AGENT-SESSIONS-PROTOCOL.md` ~3353); **no worker change**.
- Web board view: `app/board.js` (~1597) shows `errorText(event)` — the worker's `text` — and uses `cards` only to drive the busy lamps. Already correct; `tests/test_board_view.py` ~963 covers it. **No change.**
- Desktop pane: `src/BoardPane.cpp` ~7372–7405. The handler reads `cleanup_running` / `cards` / `card_id`, builds its own message (`running.size() > 1` → "The agent is already working on %1.", 7380–7382), and discards `text`. This is the bug. It then shows the message via `m_detail->showError(what + " Your message was not sent…")` (7403).

**Steps.**

1. `src/BoardPane.cpp`, the `board_busy` handler: read `event.value("text").toString()`; when non-empty, use it as `what` (keeping the existing " Your message was not sent and is not in …" suffix appended at the `showError` call). When `text` is empty, keep the current `cleanupRuns` / `running.size()` / `busyCard` reconstruction as the fallback, so an older worker still renders sensibly.
2. Leave everything else in the handler untouched: the `cards` array still feeds whatever lamp / busy-strip bookkeeping it feeds today, and the `cleanupRuns` → `m_busyCard = asked` path (7400–7401) stays.
3. `docs/BOARD-DESIGN.md` ~442 (the **Busy** bullet): update the sentence that says a `board_busy` "says which is running" to say the pane shows the worker's `text` reason, with the reconstructed message only as the no-`text` fallback.
4. Tests: `tests/boardmodel_test.cpp` already drives `board_busy` into the view at ~2473 (cleanup_running false) and ~3039 (card_id K7Q2). Add a case: `board_busy` with `cards: ["AAAA", "BBBB"]` **and** a `text` naming the console's turn → the shown notice contains the `text`, not "already working on #AAAA, #BBBB". Extend or add a no-`text` case proving the legacy reconstruction still fires.

**Risks.** Small. The worker's `text` ends "Stop it first, then …", so the pane's appended "Your message was not sent…" suffix still reads as one notice; confirm the wording in the test. No owner decision needed — the web view already treats `text` as the message, so this makes the desktop consistent.

**Verify.** `ctest --test-dir build -R boardmodel` (and build through `scripts/relay-build` first). Manual see-it-working: with two card turns running, start a Board-page ask while the page agent is mid-turn — the notice names the console's turn, not the cards.
