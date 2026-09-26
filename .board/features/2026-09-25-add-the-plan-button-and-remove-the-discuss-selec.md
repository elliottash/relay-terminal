---
id: KEM9
type: work
status: planned
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-25'
source: 'remote: iOS Safari'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Add the plan button and remove the discuss select box on the phone app. Discuss…

## Issue
Add the plan button and remove the discuss select box on the phone app. Discuss is redundant cause I can just press send in a message.

## Done means
The phone card page's reply row is `mic · spacer · Stop · Plan · Send` — the three-way mode
select is gone. Send (and a bare Enter) sends a Discuss turn, with an empty Send still resuming
the card's queue; the Plan button sends a Plan turn, with or without words, and stays tappable
while a turn runs. Comment-only stays reachable with Ctrl+Shift+Enter on a hardware keyboard.
Failure looks like: the select still on screen, Send sending whichever mode was last picked, Plan
missing or dead while a turn runs, or `tests/test_board_view.py` failing where it drives the row.

## Plan
**Goal** — Give the phone the desktop's reply shape (card #VZ69's decision): the box Discusses on
Send, a Plan button sits beside it, and the mode select goes away — "Discuss is redundant cause I
can just press send in a message".

**Findings**

- `app/board.js` builds the row in the card screen (~337–350): a `replyMode` `<select>` with
  Discuss / Plan / Comment only, appended as `replyRow.append(replyMic, replyMode, el('span',
  'rb-spacer'), replyStop, replySend)`.
- `paintReply()` (~1000) relabels option 2 to "Answer only" while `answering()` (newest thread
  entry is an unanswered question), disables Send while a turn runs unless the mode is comment,
  and sets the box placeholder to "Answer the question…" / "Reply on this card…".
- `sendReply(mode)` (~1023): comment → `board_comment` (kind decision when answering); empty
  discuss → `board_resume` (card #7JD1); otherwise `board_ask {id, text, mode}`. Plan already
  sends fine with an empty box — the desktop's Plan does exactly that.
- Keydown (~1769): bare Enter sends `replyMode.value`; Ctrl+Enter plans; Ctrl+Shift+Enter
  comments; Shift+Enter breaks lines.
- The desktop is already the target shape: `src/BoardPane.cpp` ~2780 — Enter discusses,
  Ctrl+Enter plans, Ctrl+Shift+Enter comments; the row carries a Plan button, kept enabled while
  a turn runs so a second ask queues (card #CTRN).
- `tests/test_board_view.py` drives the select at lines 760, 798, 815, 830, 850, 959, 979, 1163
  (the `pick()` helper), 1462, 1471, 1579, 1811.

**Steps**

1. `app/board.js` row (~337–350): delete `replyMode` and its option loop; add
   `const replyPlan = button('rb-plan', 'Plan')` (title "Start a Plan turn — Ctrl+Enter");
   append `replyRow.append(replyMic, el('span', 'rb-spacer'), replyStop, replyPlan, replySend)`.
   Shared tooltip becomes "Enter discusses · Ctrl+Enter plans · Ctrl+Shift+Enter comments ·
   Shift+Enter adds a line".
2. `paintReply()` (~1000): drop the option-2 relabel; `replySend.disabled = running` (discuss
   stays unsendable mid-turn, as today); `replyPlan` is never disabled — tappable while a turn
   runs (the ask queues, desktop parity) and while offline (`sendReply`'s message already says
   "Offline — Plan needs your desktop"). Placeholder flips unchanged.
3. Wiring (~1755–1780): `replySend` click → `sendReply('discuss')`; add
   `replyPlan.addEventListener('click', () => sendReply('plan'))`; delete the `replyMode`
   change listener; the keydown's bare-Enter branch sends `'discuss'` instead of
   `replyMode.value`.
4. `app/board.css`: remove the `.rb-reply-mode` rule; style `.rb-plan` as a quiet button
   sibling of `.rb-stop`, sized so mic · Stop · Plan · Send fits the 390px shot.
5. `tests/test_board_view.py`: reimplement `pick(browser, mode)` (~1163) to route — discuss →
   `.rb-reply-send` click, plan → `.rb-plan` click, comment → the Ctrl+Shift+Enter chord via
   `keyed()` — so every call site keeps its meaning; swap the option-text assertions (760, 815)
   for placeholder assertions ("Answer the question…" → back to "Reply on this card…"); replace
   1579's select-value check with "no `.rb-reply-mode` exists and `.rb-plan` is visible"; keep the
   exact-message assertions in `test_each_action_sends_exactly_the_contracts_message`, and add
   one: Plan clicked while a discuss runs still sends `board_ask {mode: "plan"}`.

**Risks**

- Comment-only loses its touch affordance: with the select gone it needs a hardware keyboard
  (Ctrl+Shift+Enter). Answering from a phone still works — type the answer and Send, the agent
  records the decision — but an offline phone can no longer queue a `board_comment`. If that
  matters, the fallback is a third quiet button; **owner's call — this plan ships without it**.
- Plan-while-running is a small behaviour change (today everything but comment is disabled
  mid-turn); it queues exactly as the desktop does. If the phone proves unable to show a queued
  turn sensibly, disable `replyPlan` while running instead and pin that in the test.

**Verify**

- `python3 -m pytest tests/test_board_view.py -x -q` (headless browser; covers the row, the
  contracts, the chord, the answering placeholder, the 390px layouts).
- Eyeball the refreshed shots (`phone-390x844-card-thread`, `phone-expanded-prompt`) for the new
  row at 390px.
- Manual: card page on a phone — Plan with an empty box starts a Plan turn; typed words + Send
  discuss; empty Send resumes the queue; a pending question flips the placeholder and Send
  delivers the answer.
