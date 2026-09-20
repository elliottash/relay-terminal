---
id: ZGF5
type: work
status: inbox
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
