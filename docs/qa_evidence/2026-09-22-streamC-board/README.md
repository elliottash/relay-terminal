<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# The phone Board's Markdown and its reply box — #MDX6 and #RCN8, 2026-09-22

Two cards from `docs/qa_evidence/2026-09-22-phone-ux-drive/`, fixed in `app/boardmd.js` and
`app/board.js` and held by `tests/test_board_view.py` (32 tests, real headless Chrome, ~23 s).

```sh
python3 -m unittest tests.test_board_view
RELAY_BOARD_SHOTS=docs/qa_evidence/2026-09-22-streamC-board python3 -m unittest tests.test_board_view
python3 docs/qa_evidence/2026-09-22-streamC-board/render_drive.py
```

## #MDX6 — the renderer loses characters

`markdown-before-after.md` is the measurement: `render_drive.py` loads the renderer as it was
(`boardmd-before.js`, `git show 458582b2:app/boardmd.js`) and the renderer as it is, in one
browser, from one origin, and draws the same strings through each. Six of seven bodies came out
differently — `\tmake all` was `ake all`, a tab-indented `- b` had lost its bullet, a
tab-indented line of a fence was gone, `__init__` was **init**, and a pipe table under a sentence
was four literal `|`-lines.

The cause was one pair that disagreed: `indentOf()` counted an indent in **columns** (a tab worth
four) and both call sites then cut that many **characters** off the line. They now agree —
`indentWidth` measures, `stripIndent` cuts, and a tab that straddles the cut leaves its remaining
columns behind as spaces.

`test_a_card_body_reaches_the_reader_character_for_character` renders eight bodies and compares
the lines character for character.

## #RCN8 — the reply box and its modes

`phone-390x844-line-about-another-card.png` is the fourth fault, fixed: a refusal about #80E3,
arriving while #2DW4 is open on a 390×844 phone, drawn above the reply box where the reader is
and carrying the card it is about. It used to be painted into `.rb-list-col`, which that layout
sets to `display: none`.

Five tests, each of which fails against `25067cb8:app/board.js` and passes against the fix:

| test | what it holds |
|---|---|
| `test_a_half_typed_reply_survives_anything_that_re_opens_its_card` | the `card_waiting` push for the question being answered, and an iPad tap on the open card's own row, both leave the box alone; switching cards still parks and restores the draft |
| `test_return_sends_the_mode_the_selector_shows` | Comment only + Return is a `board_comment`, not `board_ask {mode: "discuss"}`; Ctrl and Ctrl+Shift still plan and comment; Shift+Enter still breaks a line |
| `test_a_second_send_before_the_first_lands_sends_nothing_more` | three taps in one task send one `board_ask` and no `board_resume`; a refusal puts the lamp out and gives the words back |
| `test_a_change_to_the_open_card_while_it_is_being_read_is_read_again` | a `board_changed` arriving during a `board_card_get` is re-read when that read lands, and one read is still in flight at a time |
| `test_a_refusal_about_another_card_is_drawn_where_the_reader_is` | the refusal and a `board_activity` land on the card page on a phone and on the list's line on an iPad |

The last two were the card's "plausible rather than reproduced" pair. Both reproduce: against the
old code the double tap sends `board_ask` **and** `board_resume`, and the second `board_changed`
never asks again.

## Provenance

`main` at `25067cb8`, Ubuntu 24.04 aarch64, Chrome at `/usr/bin/google-chrome`. The screenshots
are the suite's own, written by `RELAY_BOARD_SHOTS`; the rest of them are the layouts the existing
tests already covered.
