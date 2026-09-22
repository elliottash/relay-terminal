---
id: RCN8
type: work
status: executing
labels: [bug, remote, switchboard]
assignee: claude-code
rank: zrcn8
created: '2026-09-22'
source: 'Measured by Claude Code reviewing and driving the phone app, 2026-09-22'
links: {plans: [], commits: [f4baccee], evidence: ['docs/qa_evidence/2026-09-22-phone-ux-drive/'], related: [PBXC, SWPH], github: null}
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
