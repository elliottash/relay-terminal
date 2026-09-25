---
id: 7PEC
type: work
status: needs-verification
labels: [bug, remote, switchboard]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 7dbb2c54-9215-4e1f-a044-52c4f6c7dc1a
rank: zzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [person], human: optional, criteria: 'on the phone, type a Discuss, background the app until the link drops, come back: the words are back in the box', sign_off: none, effort: low}
source: pane 7dbb2c54 (guest Claude Code), 2026-09-25; owner said "yes" to filing and fixing
links: {plans: [], commits: [], evidence: [tests/test_board_view.py], related: [RCN8, SWPH], github: null}
---
# Phone Board: a Discuss sent into a dead socket vanishes, and unsent drafts die with the page

## Issue
i think i was doing that on my phone, but it got lost. see if there is a bug where phone threads can get lost.

## Done means
Words typed on the phone Board's card page are never lost without a trace:
- A Discuss or Plan whose `board_ask` the desktop has not accepted within a bounded time, or whose link drops before it is accepted, gives its words back to that card's reply box (unless something else was typed since) and says it did not reach the desktop.
- An unsent reply draft survives a page reload or iOS evicting the page: it is kept per card in `localStorage` and is back in the box when that card is opened again; sending or clearing it removes the stored copy.

It fails if, in `tests/test_board_view.py`, a `board_ask` the fake desktop never answers leaves the box empty, or a reload loses a typed draft.

## Execution Summary
**What was lost, and how it was found.** The owner's phone discussion of Sessions search (titles spanning the columns, instant filter, highlighted matches) is nowhere on this machine: not in any Relay session, card thread or guest transcript, and `~/.local/share/relay/remote/audit-2026-09.jsonl` shows the phone's last request at 08:52. Every one of the 52 `board_ask`s the audit logged since 09-23 is in its card's thread, so nothing that *reached* the desktop was dropped. The loss is on the phone, in `app/board.js`:

1. `sendReply` emptied the box and its draft as soon as the `board_ask` was handed to the socket, and gave the words back only if the desktop answered with an error. A socket iOS has killed without closing takes the send and answers nothing, with no timeout, so the words were gone from both ends.
2. `drafts` was an in-memory `Map`, so a half-typed reply died whenever iOS unloaded the backgrounded page; `reset()` also cleared it.

**The fix (`app/board.js`).**
- `giveBack(rid, why)`: a Discuss or Plan on a card that was idle when sent, not accepted within `ASK_WAIT_MS` (20 s), puts its words back in that card's box (or its draft) and says so on the card line; so does *any* unaccepted ask when the link drops (`onLink`). A card that was already working is exempt from the timer, because there the desktop queues the ask and answers nothing until its turn (REMOTE-PROTOCOL §17.4).
- `takeBack`: if the question lands after all (`board_thread_appended` with that rid), the box is emptied again when it still holds exactly those words, and the line says it arrived.
- `Drafts` (a `Map` that saves itself): per-card drafts in `localStorage['relay-board-drafts']`, newest fifty, empties not kept; every write to the box (typing, dictation, a question's option, a restore) updates it, sending removes it, and `reset()` no longer throws the words away.

## Tests
`TMPDIR=/tmp/claude-1000/7pec RELAY_KEYRING=off python3 -m unittest tests.test_board_view` — 36 tests, OK (a short TMPDIR is needed inside a Relay pane until #H1BS). Both new tests were also run against `HEAD:app/board.js` and **fail there**.

- `tests/test_board_view.py::BoardViewTests::test_a_discuss_the_desktop_never_takes_gives_its_words_back` — an unanswered Discuss comes back after the (test-shortened) wait with a line; a late `board_thread_appended` for that rid empties the box again; a link drop gives an unaccepted ask back at once.
- `tests/test_board_view.py::BoardViewTests::test_an_unsent_draft_survives_the_page_being_reloaded` — a typed draft is in the box after a reload; sending it leaves nothing in `localStorage`.
