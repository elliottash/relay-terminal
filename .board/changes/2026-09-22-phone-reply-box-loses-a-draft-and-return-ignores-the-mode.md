---
id: RCN8
type: work
status: needs-verification
labels: [bug, remote, switchboard]
assignee: claude-code
implemented_by: anthropic/claude-opus-5 via claude-code
rank: zrcn8
created: '2026-09-22'
source: Measured by Claude Code reviewing and driving the phone app, 2026-09-22
links: {plans: [], commits: [f4baccee, 4c854a29], evidence: [docs/qa_evidence/2026-09-22-phone-ux-drive/, docs/qa_evidence/2026-09-22-streamC-board/], related: [PBXC, SWPH], github: null}
---
# The phone Board's reply box loses a half-typed draft, and Return ignores the mode selector

## Issue
**1. Re-opening the card you are already on empties the box.** `openCard()`
(`app/board.js:693-697`) saves the draft only on a *switch* —
`if (openId && openId !== wanted) drafts.set(openId, replyBox.value);` — but then assigns
`replyBox.value = drafts.get(wanted) || ''` unconditionally. Opening the card that is already open
therefore writes an empty string over what is being typed. Two ways in: on an iPad (≥900 px) the
list column stays beside the open card (`app/board.css:547`), so tapping that card's own row does
it; on a phone a `card_waiting` push for the card being answered does it
(`app/app.js:2223` → `board.open(id)` → `openCard(sameId)`) — which is the normal case, since the
notification is for the very question being answered.

**2. Return sends the wrong mode.** The Send button uses the selector
(`app/board.js:1657`: `sendReply(replyMode.value)`); the keydown handler derives the mode from
modifiers alone and never reads it (`app/board.js:1662`:
`const mode = event.ctrlKey ? (event.shiftKey ? 'comment' : 'plan') : 'discuss';`). The selector is
sticky across cards and `paintReply` keeps showing the user's choice, so on an iPad with a keyboard:
pick **Comment only**, type, press Return → `board_ask {mode: "discuss"}`, which starts a real
agent turn on the desktop queue (protocol §17.1/§17.4). Return should honour the select when no
modifier is held.

**3. A refusal about another card is drawn into a hidden element.** `lineFor()`
(`app/board.js:389`) sends anything about a card other than the open one to the list's line, and
`board_activity` (`:1453`) always does. On a phone with a card open,
`.rb[data-card="open"] .rb-list-col { display: none }` (`app/board.css:81`) has removed that
column. So a comment queued offline on #AAAA that the worker refuses while you are reading #BBBB is
lost with its refusal painted into a hidden node.

Two more, plausible rather than reproduced: a second Send before the first resolves re-enters
`sendReply` with an empty box (`busy` is only set in the `.then`, `app/board.js:959`), which in
Discuss mode is a `board_resume` and in Plan mode an accepted empty `board_ask`; and a
`board_changed` for the open card that arrives while a `board_card_get` is in flight is skipped
and never re-read (`app/board.js:1379`, guarded by `!cardAsked`).

## Done means
A half-typed reply survives anything that re-opens the card it is on, including the push
for the question being answered. Return sends the mode the selector shows. A refusal about any card
is drawn somewhere the reader can see it on a phone with a card open. It fails if the reply box
empties on a re-open, or if Return sends `discuss` while the selector says Comment only.

## Execution Summary
**The draft.** `openCard()` saved the draft only on a switch and then assigned the box
unconditionally, so opening the card that was already open wrote an empty string over what was
being typed. Re-opening the open card now leaves the box, the card and its line alone; a switch
still parks the draft and brings it back.

**Return.** The keydown handler derived the mode from modifiers and never read the select, so
**Comment only** plus Return sent `board_ask {mode: "discuss"}` — a real turn on the desktop's
queue — while the control beside it said otherwise. A bare Return now sends `replyMode.value`;
Ctrl and Ctrl+Shift still plan and comment, Shift+Enter still breaks a line. The Send button's
tooltip says "Enter sends the mode shown".

**Where a line lands.** `lineFor()` sent anything about another card to the list's line, which a
phone with a card open sets to `display: none`. It now returns the card page whenever the list is
off screen (`listShowing()`), and `sayAbout(cardId, …)` — which replaced all seventeen
`say(lineFor(…))` call sites — names the card when the line is not about the one being read.
`board_activity` picks its own line, since its text already carries `#ID`. An iPad shows both
columns and keeps the list's line for the list.

**Both faults the card called plausible are real, and are fixed.** `busy` was set in the promise's
`.then`, so a second tap re-entered `sendReply` with the box `clearReply` had already emptied and
sent `board_resume` (Discuss) or an accepted empty `board_ask` (Plan); it is now set when the ask
goes out and cleared on a refusal, which also restores the words. And a `board_changed` for the
open card arriving during a `board_card_get` was dropped for good; `getCardAgain()` remembers it in
`cardStale` and reads once more when the answer lands, still one read in flight at a time.

`app/board.css` was claimed for this stream and needed no change: fault 3 was a routing bug in the
JS, and the rule that hides the list column behind an open card is right.

## Tests
`RELAY_KEYRING=off python3 -m unittest tests.test_board_view` — 32 tests, OK, 22 s. Re-run by the
orchestrating session after the landing. Each of the five below was run against
`25067cb8:app/board.js` as well and **fails there**, so they hold the fix rather than the code.

- `tests/test_board_view.py::BoardViewTests::test_a_half_typed_reply_survives_anything_that_re_opens_its_card`
  — the `card_waiting` push (dispatched as `app/sw.js` posts it, through `app/app.js`'s own
  handler) and an iPad tap on the open card's row both leave the box alone; switching cards still
  parks and restores.
- `tests/test_board_view.py::BoardViewTests::test_return_sends_the_mode_the_selector_shows` —
  Comment only + Return is a `board_comment` and **no** `board_ask` at all; Plan and Discuss send
  the matching `board_ask`; Ctrl and Ctrl+Shift still win; Shift+Enter sends nothing.
- `tests/test_board_view.py::BoardViewTests::test_a_second_send_before_the_first_lands_sends_nothing_more`
  — three taps in one task send one `board_ask` and no `board_resume`; the lamp lights on the tap;
  a refusal puts it out and gives the words back.
- `tests/test_board_view.py::BoardViewTests::test_a_change_to_the_open_card_while_it_is_being_read_is_read_again`
- `tests/test_board_view.py::BoardViewTests::test_a_refusal_about_another_card_is_drawn_where_the_reader_is`
  — an offline comment on #80E3, refused while #2DW4 is open at 390×844, lands on the card page
  named `#80E3 · …`, with `.rb-list-col` measured at 0 px; a `board_activity` does too; on an iPad
  both go back to the list's line.
- `manual: docs/qa_evidence/2026-09-22-streamC-board/` — `phone-390x844-line-about-another-card.png`
  is that refusal on screen, plus the suite's layout screenshots.
