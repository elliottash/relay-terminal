---
id: K0FT
type: work
status: planned
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-25'
source: 'remote: iOS Safari'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Phone app needs to have the same filter options as the main app for the board —…

## Issue
Phone app needs to have the same filter options as the main app for the board — like filter out of discussing or needs verification, etc.

## Done means
The phone's Board page can keep whole sections off it the way the desktop's section checkboxes do — untick Discussing or Needs verification and their cards are gone from the list, the counts, and both Recent and Stages views, with the choice remembered on reload. The search box also understands the desktop's filter terms (`status:`, `label:`, `waiting:`, `folder:`, `@assignee`, `#ID`) alongside plain words. Failure would be: on a phone, a hidden section's cards still listed, `status:planning` matching nothing or different cards than the same filter on the desktop, or plain-word search broken by the term parsing.

## Plan
**Goal** — Give the phone app's Board list the desktop's filter options: a way to keep whole sections (Discussing, Needs verification, …) off the page, and the desktop's search-filter term language. Both compose with what the phone already has (tabs, plain-word search, Recent/Stages views). No hub protocol change: everything needed is already in the `board` event rows and config.

**Findings** — the phone list is `app/board.js` (single file, no build step; static under `app/`, checked with `node --check` and served by the remote hub):

- List page UI is built in `renderBoard()` around lines 260–360: `rb-tabs`, `rb-search`, `rb-views` (Recent/Stages/grouping), `rb-list`. Existing filters: `tabTakes()` (line 523, board.yaml tab `status:`/`label:` filters), `matchesWords()` (line 534, plain substring words over row fields only), the `board_search` answer ids (`searchFor` line 221; request sent at line 1791 with the whole raw query as words), per-section collapse and the grouping toggle, persisted in localStorage (`relay-board-collapsed`, `relay-board-grouping`, lines 368–389).
- Desktop parity to copy: `src/BoardModel.cpp` `plainTerms()` (line 1719) strips scoped terms before asking the worker; `matches()` (lines 1727–1780) applies `label:x`, `status:x`, `waiting:me|owner|x`, `folder:x` (folder or board.yaml tab), `@assignee`, `#ID` caselessly, then plain words over row fields, all terms ANDed. Section checkboxes: `BoardPane.cpp` `buildListTools()` (line 5281) builds one checkbox per section, unticked goes in `m_hidden` (line 768) and those sections' cards leave the page and the counts; the honest count line is at BoardPane.cpp:7625/7768. Note (#1Q5V): desktop label *chips* are gone — labels filter via `label:` terms, so the phone does not need chips either.
- On the phone, sections come from `sectionsOf(config, rows)` (line 120) and a section id *is* the status id, so hiding a section = hiding that status — same semantics as the desktop's `m_hidden`.
- Tests: `tests/test_board_view.py` drives a real browser over the `tests/fixtures/board/` fixtures (see `test_search_asks_the_desktop_and_shows_what_it_answers`, line 410; `test_board_css_uses_only_the_generated_theme`, line 1863); `tests/test_remote_board.py` covers hub-side validation of `board_search` (lines 200–216) and does not pin the app's query wording.

**Steps**

1. Filter terms in the phone search box (`app/board.js`): port `plainTerms` and scoped-term matching from `src/BoardModel.cpp:1719–1780` — decide `status:`, `label:`, `waiting:` (`me` → owner), `folder:` (board.yaml tab), `@`, `#` locally from the row; leave only plain words to `matchesWords()`. Mirror the desktop exactly (single value per `status:`; comma lists stay a board.yaml-tab feature).
2. Send `board_search` only the joined plain terms (not the raw query), and hold the answer only while the box's plain terms still equal what was asked — the desktop's `setSearchResult` pattern (`src/BoardModel.cpp:1740`). Update `searchFor`/`searchIds` accordingly (lines 218–225, 541–547, 1788–1799). If the plain terms are empty (`status:planning` alone), no `board_search` request is sent.
3. Section hide: add a Filter control to the list page — a `rb-`-styled button beside the views row opening a bottom sheet with one checkbox per section, all ticked by default, 44px targets. Unticked sections' cards leave `shownRows()` (line 541) in both Recent and Stages views and leave the section counts. Tab filters and hidden sections both apply, exactly as the desktop composes its filter with `m_hidden`. Persist as `relay-board-hidden` in localStorage, loaded beside the collapsed set.
4. Honest count: when the filters narrow the page, say so in the line under the list header — `«62 of 84 cards · 2 sections hidden»` — the counterpart of the desktop's count line (BoardPane.cpp:7625).
5. CSS in `app/board.css` only via the generated theme variables (the `rb-*` conventions, per `test_board_css_uses_only_the_generated_theme`).

**Orchestration** — none: one file pair plus tests; Run hands it to a single terminal pane.

**Risks**
- Term parsing must not break plain-word search or the existing `board_search` flow; the `test_board_view.py` search tests cover this.
- Hidden sections are phone-local state (like folding today) and do not sync to the desktop's checkboxes. Syncing them would be a layout-node/protocol change — out of scope unless the owner asks.
- The card's ask is parity, not a new design: if the owner also wants comma lists in `status:` or per-label chips, that is a decision for them (question on the card if needed).

**Verify** — extend `tests/test_board_view.py` against the existing fixtures: `status:planning` shows only planning; `waiting:me` only waiting-on-owner; `@` and `#ID` and `label:` behave; unticking Discussing and Needs verification removes those cards in Recent and Stages, fixes the counts, and survives a reload; plain-word search still hits `board_search` with only the plain terms. Run `pytest tests/test_board_view.py tests/test_remote_board.py` (C++ untouched, so no relay build needed; `node --check app/board.js` as a smoke check).
