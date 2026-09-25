---
id: EE42
type: work
status: needs-verification
labels: [feature, board, worker]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: c522363d-fa8e-4db1-afcd-a6e58451cd14
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
source: 'owner decisions on #EA37, 2026-09-25'
verify: {artifact: code, primary: script, also: [ai-text], human: optional, criteria: 'board_links on this board answers #EA37 with #EE42 among its reverse edges, and a comment naming a card leaves one mentioned-in line on it', effort: medium}
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-25-ee42-board-links/], related: [EA37, 9FX8], github: null}
---
# board_links: reverse-link index, mentioned-in thread lines, duplicate_of / discovered_from / supersedes, dangling_link, filter prefixes

## Issue
Phase 2 of the per-project Board object model (`docs/PROJECT-BOARD-DESIGN.md`, #EA37 decisions 4–6, owner 2026-09-25). Worker side, nothing drawn: a `board_links` request that rebuilds an uncommitted reverse index from the board's files on every refresh; one append-only `mentioned in #X · date · who` line added to the target's thread on every cross-reference (worker-side append under the board lock; the in-memory index wins on disagreement); `duplicate_of` (closes the card), `discovered_from` (written by the deliver flow) and `supersedes` added to work cards; a `dangling_link` signal for addresses that resolve to nothing; filter prefixes for the address forms (`#ID`, `skill:`, `case:`, `run:`, `path@sha`). #9FX8's shipped Linked chips (registry + front matter) gain the computed reverse side from this request.

## Done means
- `board_links {address}` answers forward and reverse edges for `#ID`, `skill:`, `case:`, `run:` and `path@sha` addresses, typed by the field they were read from, recomputed from the files on every call and written nowhere; `board_read`'s `reverse` and `board_links` come from the same edge function.
- `duplicate_of` set through `board_update_card` closes the card (`dropped`, `resolution: duplicate`); `discovered_from` and `supersedes` are accepted on work cards and refused when they name no card on this board.
- A board write that adds a new `#ID` reference leaves exactly one `mentioned in #X · date · who` entry on the target's thread; a second write with the same pair adds nothing.
- `relay-board.py check` reports `dangling_link` for the new fields and for prose addresses that resolve to nothing; `board_search` answers `links:#ID`, `skill:x`, `case:x`, `run:x`.
- Failure looks like: a reverse edge missing from `board_links` while `rg --hidden` finds the forward one, or a second identical mentioned-in line.

## Plan
**Goal.** Phase 2 of `docs/PROJECT-BOARD-DESIGN.md` §4, worker side only: one computed link index, the append-only mentioned-in line, three new work-card fields, `dangling_link`, filter prefixes. Nothing drawn (#9FX8 draws the chips).

**Findings.**
- `backend/relay_core/board.py` `links_index` already computes children / blocks / duplicated_by / related_from (#MJ76) and `check_links` already reports `dangling_link` for parent / blocked_by / duplicate_of; `board_tools._read` builds `reverse` from it.
- `duplicate_of` exists since #MJ76 but is only honoured by `board_move_card`; `board_update_card` accepts it only beside `resolution: duplicate` and never closes the card.
- The filter bar (`src/BoardModel.cpp` `plainTerms`) sends every term not starting with `#`, `@`, `label:`, `status:`, `waiting:`, `folder:` to `board_search`, so `links:#ID`, `skill:x`, `case:x`, `run:x` already reach the worker: no C++ change is needed.
- Signals (`signals.py`) are folded from test executions; nothing produces `check:` keys yet, so `dangling_link` is a `check` problem (the design page's wording, §4.3) and a `dangling` list on `board_links`.
- `board_tools.py` is held by four other land sessions: the logic goes in a new `board_links.py`, with small hooks there.

**Steps.**
1. `board.py`: one `CARD_LINK_FIELDS` table and `card_link_edges(card)`; `links_index` and `check_links` read it; `discovered_from`, `supersedes` join `WORK_FIELDS` and `FIELD_ORDER`.
2. New `board_links.py`: address regexes, `forward_edges(card, thread)` (front matter + body + non-event thread entries), ledger rows, `build(board)` → `LinkIndex` with `query(address)`, `dangling()`, `matching(term)`.
3. `board_tools.py`: validate the two new fields as linked ids; `duplicate_of` via update closes to `dropped`; `reverse` gains `discovered` / `superseded_by`; `run()` notes new card references after each write and appends the mentioned-in line under the thread lock, deduplicated per source→target.
4. `board_protocol.py`: `board_links` request; `board_search` answers the prefixes from the index.
5. Docs: protocol §19 `board_links`; `BOARD-FORMAT.md` fields and the mentioned-in line.
6. Tests: `tests/test_board_links.py`.

**Risks.** Prose `#ID` matching must not pick up colours or anchors: only four upper-case characters from the id alphabet with a letter, on a word boundary, and only ids that exist count for the mentioned-in line. A mentioned-in entry must never itself count as a reference (it is skipped as a source).

**Verify.** `PYTHONPATH=backend python3 -m unittest tests.test_board_links tests.test_board_links_shape`; `board_links` run against this repo's own `.board/`.

## Execution Summary
Worker side only; nothing drawn.
- **Index** — new `backend/relay_core/board_links.py`: address parsing (`#ID`, `skill:<id>[@sha256:…]`, `case:c-…`, `run:r-…[#path]`, `<path>[:line]@<sha>`, commits, paths; code spans and fenced blocks skipped), `card_edges`, `build(board)` → `LinkIndex` with `query`, `dangling`, `matching`. Sources: front matter, body, thread (not `event` or mentioned-in entries), `cases.jsonl` rows. Rebuilt whole per request, written nowhere.
- **One link table** — `board.CARD_LINK_FIELDS` / `card_link_edges` now feed `links_index` (so `board_read`'s `reverse` gains `discovered` and `superseded_by`), `check_links` and the new index.
- **Fields** — `discovered_from` and `supersedes` are work-card fields, validated as ids on this board (a memory's `supersedes` is left as written). `board_create_card` takes `discovered_from` and defaults it to the pane's one claimed card. `duplicate_of` set through `board_update_card` on an open card closes it to `dropped` / `resolution: duplicate` through `_move`, so every move gate applies. The result says `closed`, or `close_refused` when a gate refused.
- **Mentioned-in line** — `BoardTools.run` compares a card's references before and after each write (create, update, move, comment, claim, merge, split). Each new target gets one `kind=event mention=<src>` entry, `mentioned in #S · date · actor`, appended under the threads lock only if no entry for that source exists. It never fails the write. `append_to_thread` now writes nothing when `add` returns no text.
- **`dangling_link`** — `check` covers `discovered_from` and a work card's `supersedes` beside parent / blocked_by / duplicate_of. Prose addresses and old `links.related` path values are listed by `board_links`'s `dangling` (60 on this board, mostly example ids like `#AAAA`) rather than in the problems strip. Nothing in `signals.py` produces `check:` keys yet, so the design's "`check` gains `dangling_link`" is the form it takes.
- **Filter prefixes** — `board_search` answers `links:<address>`, `skill:`, `case:`, `run:` from the index (cached per snapshot `rev`), ANDed with the plain words. `board::Model::plainTerms` already forwards those terms as plain words, so `src/` is untouched.
- **Request** — `board_links {address | addresses≤200, dangling?}` → `board_links {edges, items: [{address, kind, exists, title?, status?, forward, reverse}], dangling?}`; protocol §19.24 and the 19.2 table row; `docs/BOARD-FORMAT.md` §2.2 and §3.
- Live on this repo: `board_links #EA37` answers in 0.52 s with 8 reverse edges (children, related_from, mentioned_in), and `links:#EA37` in the filter gives 9FX8, EE42, FVVY, TBRH, Y0QQ (`docs/qa_evidence/2026-09-25-ee42-board-links/tests.txt`).
- For #9FX8: the Linked chips can read the reverse side from `board_links {address}` (`items[0].reverse[].name`), or from `board_card.reverse` for card-to-card links.

## Tests
`PYTHONPATH=backend:. python3 -m unittest tests.test_board_links`
`PYTHONPATH=backend:. python3 -m unittest tests.test_board_links_shape`
`PYTHONPATH=backend:. python3 -m unittest tests.test_board`
`PYTHONPATH=backend:. python3 -m unittest tests.test_board_tools.ReleaseOnCloseTests`
manual: docs/qa_evidence/2026-09-25-ee42-board-links/tests.txt

## Human QA
1. On the Board, type `links:#EA37` in the filter box. Do exactly the cards that point at #EA37 stay listed (9FX8, EE42, FVVY, TBRH, Y0QQ on 2026-09-25)? Recommended: yes.
2. Comment `see #EA37` on any card, then open #EA37's thread. Is there one `mentioned in #… · date · you` line, and none more after a second comment? Recommended: yes.

