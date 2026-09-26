---
id: G2C7
type: work
status: needs-verification
labels: [feature, sessions, board, gui]
assignee: agent
implemented_by: glm/glm-5.3
session: 8d872521-826f-46ce-9c0c-bec14e25a45d
rank: zzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: code, primary: probe, also: [script], human: none, criteria: 'relay-conversations-tests, relay-boardfilter-tests and tests.test_conv_index pass as named in Tests; the four asks visible in the panes', sign_off: none, effort: medium}
source: 'pane 1, 2026-09-26 — follow-up to #PV7W; the original ask was never carded'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-26-g2c7-instant-filtering/], related: [PV7W, CCKY, R6J0, MDSG, WM4K], github: null}
---
# Instant title and #code filtering with highlights in Sessions and the Board

## Issue
first, i wanted to keep the multiple columns that i could sort by, i just wanted the session title to straddle the columns.

secoind, i wanted to make the text filter work immediately, say after 3 characters written, with the session titles (without waiting foran sqlite search). and matched terms are highlighted. exact matches on title are put at the top. then full-text matches show after, with the piece of the snippet matched.

third, i want to have instant filtering on # card codes as well. the mentioned codes in a session should show up before the preview snippet in the first column, with claimed codes in bold or highlighted in some way.

fourth , i want the same functionality on the board.

## Done means
Derived from the owner's four points (2026-09-26):

1. **Columns stay, title straddles.** The Sessions list keeps its multiple sortable columns (Updated, Turns, Model, Recap, …); each session's title spans the full width above/across them rather than being squeezed into the first column. Sorting from the column headers still works.
2. **Instant title filter.** Typing filters immediately (from ~3 characters) over session **titles**, with no wait on the SQLite full-text pass. The matched term is **highlighted** in results. **Exact title matches rank first**; full-text matches follow, each showing the **matched snippet piece**.
3. **Instant `#` code filter.** Typing a `#XXXX` code filters instantly. Card codes mentioned in a session are listed **before the preview snippet in the first column**; codes that are claimed are shown **bold/highlighted**.
4. **Same on the Board.** The Board list's filter gets the same instant title + `#code` filtering, highlighting, and ranking.

Failure looks like: the filter still waiting on the FTS query before anything moves; matches shown with no highlight; exact-title hits buried under full-text hits; `#` codes needing the worker round-trip; or the Board filter lacking what Sessions got.

## Plan
Recon (2026-09-26): the Sessions list is `SessionManager` in `src/Conversations.cpp` — a 7-column QTreeWidget whose header has been hidden and rows first-column-spanned since #1Q5V; `RowDelegate` paints title + muted sub on the spanned cell. Search is a 120 ms debounced worker round-trip over the SQLite FTS index (`backend/relay_core/conv_index.py`); `matches` (turn/kind/line/ranges) exist per item and already drive the preview and the unfolded quick look. The Board filter (`src/BoardModel.cpp`) is already instant over row fields and falls back to the worker for body text; `Model::score()` ranks; `Card.session` and `sessionChip(token, live)` already exist for claim display.

1. **Rows (Sessions).** Un-hide the tree header (labels + clickable sort as before). Keep each session row spanned, and have `RowDelegate` paint three lines: the title full-width; a columns line with updated · turns · open · model · tokens · recap drawn at `header()->sectionViewportPosition()` so they sit under their headers; the sub line (codes, then snippet) beneath. `sizeHint` gains the columns line. Preview children stay spanned.
2. **Instant title filter (Sessions).** Hold a light per-session meta cache in the pane (id → title/updated/turns/model/tokens/codes), filled by every payload seen plus one light worker `list` request on first show. On each keystroke (no debounce): ≥3 chars filters titles locally — exact matches first, then prefix, word-start, contains — and the delegate bolds the matched substring in the title. The debounced FTS query continues; its rows append after the instant ones, dedup by id, with the first match's snippet (and its ranges highlighted) shown in the sub line.
3. **#codes (Sessions).** conv_index.py extracts `#XXXX` codes (4 alnum) from indexed text into a per-conversation set, returned in the light list and item payloads. The sub line paints code chips before the snippet; codes whose cards are claimed now or carry a claim in their history are bold. RelayWindow feeds the pane the claimed/historic code sets from the Board model.
4. **Board parity.** While the filter is non-empty: rows keep their column but order by relevance (exact id > exact title > fuzzy title > text match, via the existing `score()`), matched terms are highlighted in the title, and text matches show the matched body snippet in the row. No worker protocol change.

Files: `src/Conversations.{h,cpp}`, `src/BoardModel.{h,cpp}`, `src/BoardPane.cpp` (row painting), `src/RelayWindow.h` (wiring), `backend/relay_core/conv_index.py`, tests `tests/conversations_test.cpp`, `tests/boardfilter_test.cpp`, `tests/test_conv_index.py`.

## Tasks
- [x] Sessions rows: header back, title spanning above the sortable columns (delegate + sizeHint) <!-- t:ch -->
- [x] conv_index.py: per-conversation #code set + light `list` reply; test_conv_index.py <!-- t:z2 -->
- [x] Sessions: meta cache + instant title filter, bolded match, exact-first ranking <!-- t:kn -->
- [x] Sessions: FTS rows after instant rows, dedup, highlighted snippet in the sub line <!-- t:b6 -->
- [x] Sessions: #code chips before the snippet, claimed-or-historic bold; wiring from the Board model <!-- t:jn -->
- [x] Board: relevance ordering, title highlights, matched body snippet while filtering; boardfilter tests <!-- t:y8 -->
- [x] Build via scripts/relay-build, targeted ctest + pytest, land in needs-verification <!-- t:tt -->


## Execution Summary
Landed in `64c95e0a` (2026-09-26), by this pane.

**Sessions rows.** `RowDelegate` paints three lines per session: the title spanning the full width; the column values (Updated, Turns, Requests, Model, Tokens, Recap) each drawn at its header section's x; then the tags/snippet line. The header is visible and clickable again (the sectionClicked → Sort wiring was still in place); `SessionManager::fitHeaderSections()` sizes sections to the values (capped at 150 px; Recap stretches), since Qt's ResizeToContents would size empty cells to their labels alone.

**Instant filter.** The pane holds a meta table (`id → {item, title, updated, codes}`) fed by every payload it sees plus one `meta_only` request per filter signature (id `conv-meta`, limit 5000). `applyInstant()` runs on `textChanged` before the 120 ms debounce: words ≥3 chars rank titles 0 exact / 1 word-start / 2 contains (pinned first, then newest, inside a rank), `#codes` must be in the session's code set (or its title). Matched words are washed in the title (kTitleRangesRole). The worker's reply appends below, deduplicated by id; while it is in flight the stale rows are held back. Its first matched line replaces the opening prompt with the index's ranges washed (kSubRole/kSubRangesRole).

**Codes.** conv_index.py schema v9: a `codes` column (JSON list, capped at 200) on `conversations`, written by every writer from the text it indexes (agent, guest, terminal sidecars, threads, summary) and backfilled in place from `entries` for v8 databases — no discard, terminal history survives. `#` + four alphanumerics, uppercased. Chips paint before the snippet; `SessionManager::setBoardRoot()` (wired in `linkSessionsPane` via `relay::boardRootFor`) scans the board's card front matter (`session:` set) and thread claim markers (`agent claimed this card`, `Claimed (`) at most every 10 s, and only while a `#` is being typed — the boxed, bold set.

**Board.** `board::Model` gains `textFilterActive()`, `filterMark(title, text)` (runs in the title + the first body line a term landed in, with its runs) and `filterRank(card)`; `rows()` and `cards()` order each section's cards by it while a text filter is on (exact id > exact title > all terms in title > some term in title > body only; stable inside a rank). `BoardPane`'s `RowDelegate` washes the title runs and paints the matched body line under the row (CardShape gained titleRuns/snippet/snippetRuns/snippetRect).

**Left for others.** `src/FilePanes.cpp` was broken mid-flight by another session while this landed (`'fold' is not captured`); the `relay` target built green in the land verify slot on tip + these paths. Two other sessions' uncommitted hunks in `src/BoardPane.cpp` and `src/RelayWindow.h` were left in the working tree, not landed (land.py selection).

## Tests
- `tests/test_conv_index.py` — 132 OK (4 new: `test_card_codes_are_collected_and_returned`, `test_meta_only_lists_every_conversation_once`, `test_v8_database_gains_its_codes_in_place` incl. the in-place v8→v9 migration with backfill, plus the MAX_CODES cap).
- `build/relay-conversations-tests` — 59 passed, 1 pre-existing failure filed as #TJJ3 (#E8V1's "Agent" rename vs its test's "Helper Agent", at HEAD before this work). New: `columnsAreBackUnderTheStraddlingTitle`, `instantTitleFilterOrdersExactFirstAndMarksTheMatch`, `codeChipsFilterInstantlyAndBoldClaimedOnes` (a throwaway `.board` with a claimed thread), `fullTextReplyPutsTheMatchedLineInTheSubLine`; updated `groupRowsSpanTheWidth` (Interactive sections) and `managerGroupsByProjectAndSearches` (the second request is the meta listing; it carries no query).
- `build/relay-boardfilter-tests` — 6 passed (new: `theFilterRanksItsMatchesAndMarksWhereTheyLanded`: relevance order exact-title → title-term → body-only, title runs, the matched body line, and rank 9 once the filter clears).
- `tests.test_session_protocol` — 38 tests; one pre-existing failure (`test_compact_resume_recap_and_plan_execute`) from other sessions' uncommitted backend work, untouched by this change (the diff here is only the `meta_only` flag parse + pass-through).
- The land commit built green in the land.py verify slot (tip + these paths) — `scripts/relay-build --target relay relay-conversations-tests relay-boardfilter-tests` in this tree.

## Try it
**One task** (yours, in your own Relay, on `main` at `64c95e0a` or newer): open Sessions and judge the four asks together —

1. the header columns are back, each session's title spans the width above the values line (Updated · Turns · Requests · Model · Tokens · Recap), sortable by clicking the headers;
2. type three letters (say `sor`) — the list answers on the keystroke, titles first with the matched part washed, exact title matches leading; the full-text matches then join below with the matched line in place of the opening prompt;
3. type `#g2c7` — instant code filtering, `#G2C7`-style chips before the snippet, the claimed-or-was-claimed ones boxed and bold;
4. in the Board pane, filter the same way — exact title first, matched words washed in card titles, and a card only its body matched shows the matched line under the row.

**One question**: does the Sessions list now read the way you meant — straddling title over sortable columns, instant filtering with highlights — and if not, which of the four is still wrong?

Staging: a full disposable-project stage (`stage.sh`, `staging-notes.md` with the numbered user path) lives in `docs/qa_evidence/2026-09-26-g2c7-instant-filtering/`; the app would not run as a second isolated instance on this machine, so the agent pass is the headless suites and the judgement pass runs in your own window. The fixture's seeded sessions (exact / word-start / contains / full-text-only titles and #code mentions) show every branch in five rows if you ever rebuild it.
