---
id: PBXC
type: work
status: needs-verification
labels: [bug, remote, switchboard]
assignee: codex
rank: m
created: '2026-09-22'
source: 'owner in Relay, 2026-09-22'
links: {plans: [], commits: [f4baccee], evidence: [docs/qa_evidence/2026-09-22-phone-board/], related: [SWPH], github: null}
---
# Bring the phone Board composer up to date

## Issue

the phone switchboard doesnt matrch the current app. and the promtp box there is too small

## Done means

The phone card composer uses a single Send action, with Discuss as its default and Plan / Comment accessible by touch. The prompt spans the card width and starts with several visible lines, growing with text. Keyboard shortcuts match the desktop. The thread and composer use the current app's shared theme. Sending, draft recovery, and the short keyboard viewport still work.

## Plan

**Goal:** update the phone Board's dated card interaction and cramped prompt.
**Findings:** app/board.js still mounts three send buttons and a one-row textarea; app/board.css places those beside the field on tablets and short viewports. src/BoardPane.cpp now uses the shared console composer with Enter discussing by default.
**Steps:** replace the send row with mode + Send; enlarge and theme the composer and flatten thread entries; adapt browser regression coverage and capture phone/tablet layouts.
**Risks:** on-screen keyboards leave little vertical space; preserve compact landscape layout and all existing wire actions.
**Verify:** targeted browser Board and generated-theme tests; screenshots at phone, keyboard and iPad sizes.

## Execution Summary

Replaced the three send buttons with a mode selector and Send, defaulting to Discuss. Added the desktop's Enter/Ctrl+Enter/Ctrl+Shift+Enter shortcuts, with their hints on the prompt and Send control. The full-width prompt starts at 112px, grows with text, and shrinks for the on-screen keyboard. Thread entries use quiet separators and the composer uses the shared generated desktop theme.

## Tests

- `tests/test_board_view.py`
- `tests/test_web_theme.py`
- `manual: docs/qa_evidence/2026-09-22-phone-board/README.md`

### Check 2026-09-22 00:30
- passed · unittest:tests.test_board_view — tests/test_board_view.py passed for this revision on spark-dcc9, 2026-09-22T04:30:06Z
- not-applicable · unittest:tests.test_web_theme — tests/test_web_theme.py is not in the project any more
- not-applicable · manual:docs/qa_evidence/2026-09-22-phone-board/README.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-22-phone-board/README.md
- notice · unittest:tests.test_board_view — tests/test_board_view.py: 24 of 26 are skipped for good (test_the_row_is_in_the_inbox_only_when_the_desktop_offers_the_board, test_cards_waiting_on_you_count_on_the_app_badge, test_a_project_with_no_board_says_so_where_the_cards_would_be…)
- notice · unittest:tests.test_board_view — tests/test_board_view.py: 8 of 26 are slow (test_the_row_is_in_the_inbox_only_when_the_desktop_offers_the_board, test_search_asks_the_desktop_and_shows_what_it_answers, test_a_board_changed_updates_the_list_in_place…)
- notice · unittest:tests.test_web_theme — tests/test_web_theme.py: 8 of 8 are not in the project any more (test_committed_file_is_current, test_pane_css_uses_only_generated_variables, test_pane_widgets_are_styled…)
history: thread
