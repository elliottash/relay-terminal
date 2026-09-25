---
id: MJ76
type: work
status: needs-verification
labels: [feature, switchboard]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 01e59cca-6810-48b0-ab5a-d72e37275561
rank: zzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [probe], human: optional, criteria: 'Inspect the Board UI for correct owner, date, snooze, resolution, commit, and hierarchy displays.', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: Relay pane, 2026-09-24
links: {plans: [], commits: [510de4f150cb, 850876a27cce, 3512773d0179, 43528a3843de], evidence: [docs/qa_evidence/2026-09-25-mj76-card-metadata/notes.md], related: [], github: null}
---
# Card metadata: due, snooze, close reason, human owner, commit links, and a card hierarchy

## Issue
can cards have deadlines?

ok, before we put that on a card, research what other project management tools we need. assignees? see what other systms do. keep it agile and optional

research what other metadata other systems like github issues track. this is for cards specifically, not the general project management

yeah i agree, include 1-4. and i would also allow for linkages between cards. as we would often have a hierarchy of cards.

## Decisions
- 2026-09-24, owner: "keep it agile and optional" — every new key is optional and absent from the file when unset (as `priority: 0` is today).
- 2026-09-24, owner: "yeah i agree, include 1-4. and i would also allow for linkages between cards. as we would often have a hierarchy of cards." — scope is close reason, human owner, commit links, due/snooze/dated milestones, and card linkage/hierarchy.
- 2026-09-24, owner: "ok, put this design on a card" — commit links are an ordered sequence, kept in commit order and shown as one sequence on the card page.
- Out of scope: estimates/points, sprints/cycles, time tracking, custom fields, watchers/votes, multiple assignees, start dates/timelines.

## Tasks
- [x] Step 1: schema — owner, resolution, duplicate_of, due, snooze in WORK_FIELDS/FIELD_ORDER; validators; link validation (existing ids, no self, no cycles); rows and read carry them; tool descriptions <!-- t:vv -->
- [x] Step 2: close reason — board_move_card resolution/duplicate_of, defaults on done/dropped, cleared on reopen; thread text; page and Done-tab tag <!-- t:ng blocked_by=vv -->
- [x] Step 3: owner — page field, row chip, policy/protocol wording (assignee = agent, owner = person) <!-- t:9d blocked_by=vv -->
- [x] Step 4a: commit sequence — normalize_commits (resolve, dedupe by prefix, sort by commit date); land.py appends the landed sha to every #ID card in the message (--no-cards to skip) <!-- t:n8 blocked_by=vv -->
- [x] Step 4b: commit sequence UI — board_read returns commits [{hash,date,subject,author,signature}]; card page draws the sequence with per-commit and combined diff links <!-- t:rw blocked_by=n8 -->
- [x] Step 5a: dates — due/snooze fields, board.yaml milestones dates, computed overdue/due_soon/snoozed on rows; Snoozed filter, date chip, overdue sort nudge <!-- t:mv blocked_by=vv -->
- [x] Step 5b: hierarchy — parent/blocked_by on create and update; links_index reverse map; children + progress and reverse links on page and row <!-- t:jh blocked_by=vv -->
- [x] Tests: test_board.py (validators, links_index, date_flags, normalize_commits), test_board_tools.py (refusals, move defaults, rows), test_land.py (card append), boardmodel_test (row fields, sort), boardpane_test (children block, chips) <!-- t:pn blocked_by=ng,9d,rw,mv,jh -->
- [x] Docs: protocol 19.2/19.3/new 19.23, SWITCHBOARD-FORMAT milestones, board_policy.md + regenerated POLICY.md <!-- t:ex blocked_by=pn -->


## Planning notes
Design from the 2026-09-24 research against GitHub Issues, GitLab, Linear and Jira. Card-level only; the global project board (docs/GLOBAL-PROJECT-BOARD-RESEARCH.md) is separate.

### What exists today
- Front matter already has `assignee`, `priority`, `labels`, `component`, `workstream`, `milestone` (free text), `parent`, `blocked_by`, `waiting_on`, and `links: {plans, commits, evidence, related, github}`.
- `parent` is on 11 cards and `blocked_by` on 3; nothing in `src/` reads `parent`, ids are not validated, and no card shows the reverse direction.
- `links.commits` is on 378 cards and is read by `qa_verifiers.card_commits` (independent-verifier choice from commit trailers), by the QA prompt ("newest hash" = revision under test) and by the Board's tests-touched step. Agents append hashes by hand, so it is often missing; QA falls back to `git log --grep '#ID'`.

### 1. Close reason
- `resolution`: one of `done | not-planned | duplicate | obsolete | cannot-reproduce` (GitHub state_reason, Jira resolution, Linear canceled/duplicate).
- `duplicate_of: <id>`: required when `resolution: duplicate`; must name an existing card.
- board_move_card to `done` defaults `resolution: done`; to `dropped` takes an optional resolution (default `not-planned`). Absent on open cards.

### 2. Human owner vs agent assignee
- New optional `owner`: the person accountable (Linear's human assignee + agent delegate). `assignee` keeps its meaning: the agent/harness working the card (`agent`, `codex`, `claude-code`).
- Single value, free text; the card row shows owner, the agent chip shows assignee.

### 3. Commit links as an ordered sequence
- `links.commits` is the card's commit sequence: one short hash per commit made for it, **oldest first** by commit date. On main they are interleaved with other sessions' commits, so it is a list, never a range.
- Filled automatically: `land.py commit` appends the new sha to every card id named `#ID` in the message; board_update_card and any append re-sort by commit date and de-duplicate by prefix (full vs short hash).
- "Newest" (the QA revision under test) is defined as the last entry of the sorted list.
- Card page: the sequence as rows (short hash, date, subject, author/signature trailer), each opening its diff, plus a combined view of just those commits' diffs.

### 4. Dates
- `due`: `{date: YYYY-MM-DD, whose: mine | external}` (same shape as the global-board research's `due`; a bare date is accepted and means `mine`).
- `snooze`: `YYYY-MM-DD`; the card is hidden from active sections until that date, then reappears; shown in a Snoozed filter.
- Dated milestones: optional `milestones: {beta: 2026-11-01}` in board.yaml; a card in a dated milestone with no `due` inherits a soft due date.
- Overdue / due within 7 days / snoozed are computed at read time (never written): a chip on the row and a sort nudge. `priority` is not changed by a date.

### 5. Linkage and hierarchy
- Stored forward only, one place per relation: `parent` (child → parent), `blocked_by` (list), `links.related` (list), `duplicate_of`. The reverse (children, blocks, duplicated by) is computed at read time, never written, so it cannot drift.
- Validation on write: ids must exist; `parent` and `blocked_by` must not form a cycle; a card cannot be its own parent.
- Parent card page: a Children list with each child's status and done/total progress (GitHub sub-issues); the row shows the progress count.
- Child and linked cards show the reverse links: "child of #X", "blocks #Y", "duplicated by #Z".
- board_create_card takes `parent` and `blocked_by`; board_update_card takes all of them; board_read returns the computed reverse links.
- A task item's `card:` mirror and `parent` stay distinct: a checklist item can point at a card without that card being a child.

## Done means
- A card with none of the new keys reads, writes and renders byte-for-byte as before.
- Each new key round-trips through board_update_card and board_read; bad values (unknown resolution, missing duplicate_of target, parent cycle) are refused with a sentence saying why.
- Landing a commit whose message names `#ID` adds its hash to that card's `links.commits` in commit order; QA's "newest hash" is the last entry.
- On the Board: overdue/due-soon chip, snoozed cards hidden until their date, owner shown apart from the agent assignee, a parent lists its children with progress, and a linked card shows the reverse link.
- Tests cover validation, reverse-link computation, commit ordering and snooze filtering (pytest for board.py/board_tools.py, boardmodel_test for the C++ side).

## Plan
Five steps, each landable on its own and each leaving a board with none of the new keys unchanged. Order: schema first (everything else reads it), then the two file-only features, then the two that touch the Board UI. Python side is `backend/relay_core/board.py` (schema, validation, computed fields) and `board_tools.py` (tool args, `_row`, `_read`, `_move`); C++ side is `src/BoardModel.{h,cpp}` (row → `Card`, sort) and `src/BoardPane.cpp` (card page, chips). Tests: `tests/test_board.py`, `tests/test_board_tools.py`, `tests/boardmodel_test.cpp`, `tests/boardpane_test.cpp`.

### Step 1 — schema and validation (board.py, board_tools.py)
1. Add to `WORK_FIELDS`: `owner`, `resolution`, `duplicate_of`, `due`, `snooze`. Add them to `FIELD_ORDER` beside their neighbours (`owner` after `assignee`; `resolution`, `duplicate_of` after `blocked_by`; `due`, `snooze` after `milestone`).
2. Constants: `RESOLUTIONS = ("done", "not-planned", "duplicate", "obsolete", "cannot-reproduce")`; `DUE_WHOSE = ("mine", "external")`.
3. Validators beside `clamp_priority` / `validate_verify`: `validate_date(value)` (ISO `YYYY-MM-DD`, refuses anything else), `validate_due(value)` (a bare date → `{date, whose: mine}`; an object with `date` and optional `whose`), `validate_resolution`.
4. Link validation in `board_tools._update` and `_create`, for `parent`, `blocked_by`, `duplicate_of`, `links.related`: every id must be a card on this board (`self._card`), a card may not name itself, and `parent`/`blocked_by` may not close a cycle (walk up to 50 hops via the board's card index). Refusal codes `board_refused`, one sentence each, naming the offending id.
5. `board_check` (`Board.problems`) reports a dangling `parent` / `blocked_by` / `duplicate_of` on an existing board as a warning, never an error, so old cards keep loading.
6. `_row` gains `owner`, `due` (the normalised object or null), `snooze`, `resolution`, `parent`, `blocked_by` and the four computed booleans/lists of Step 5. `_read` returns the same plus `children` and `reverse` (Step 5).
7. Tool descriptions for `board_update_card` / `board_create_card` name the new keys in one sentence each.

### Step 2 — close reason (board_tools._move)
1. `_move` takes an optional `resolution` arg (and `duplicate_of`). Moving to `done` writes `resolution: done` unless one is given; moving to `dropped` writes the given one, default `not-planned`; `resolution: duplicate` without a valid `duplicate_of` is refused. Moving a closed card back to an open status clears both.
2. The move's thread entry carries the resolution in its text ("dropped: duplicate of #ABCD").
3. Card page: `resolution` shown beside status on a closed card; `duplicate_of` as a `#id` link. Done tab row: a short "dup" / "not planned" tag after the title when the resolution is not `done`.

### Step 3 — owner (small)
1. Schema from Step 1. Card page: `add("owner", "owner")` before `assignee`. Row: `owner` in the `Card` struct; drawn only when set, muted, before the assignee chip.
2. `board_policy.md` and protocol 19.2: `assignee` is the agent; `owner` is the person accountable.

### Step 4 — commit sequence (land.py, qa_verifiers.py, board.py, BoardPane.cpp)
1. `board.py`: `normalize_commits(repo, hashes)` — resolve each to a full sha (`git rev-parse --verify`), drop ones git cannot resolve **only when the list is being re-sorted by a writer that has git**, de-duplicate by prefix, sort by committer date ascending, write short (12-char) hashes. Called from `board_tools._update` whenever `fields.links` is written and from `_move`.
2. `scripts/land.py commit`: after `update-ref` succeeds, find every `#[A-Z0-9]{4}` in the message that is a card on the board, and append the new sha to its `links.commits` through the same normaliser, writing the card file directly (land.py already owns the working tree write for its own paths; the card file is written after the swap and is *not* part of the landed commit — it lands with the session's next commit or with `land.py`'s own `--board` follow-up commit). Skip silently when `.board/` is absent. A flag `--no-cards` turns it off.
3. `qa_verifiers.card_commits` keeps its current merge but takes "newest" as the last entry of the sorted list; the QA prompt in `BoardModel.cpp` ("the newest hash in links.commits") is unchanged in wording and now correct by construction.
4. `board_read` returns `commits: [{hash, date, subject, author, signature}]` (from `git log -1 --format` per hash, cached per read, at most 20) beside `front.links.commits`.
5. Card page (`BoardPane.cpp`, the `links` loop): the `commits` key is drawn as its own block — one row per commit, oldest first: short hash (link `relay-commit:<sha>` → opens `git show` in a pane, as evidence paths already open), date, subject, signature trailer muted. A "diff of these N commits" link runs `git show <sha1> <sha2> ...` in a pane.

### Step 5 — dates, hierarchy and reverse links
**Dates**
1. `board.yaml` gains optional `milestones: {name: YYYY-MM-DD}`; `Board.config` exposes it; `docs/SWITCHBOARD-FORMAT.md` documents it.
2. Computed in `board.py` (`date_flags(card, today, config)`): `effective_due` (card `due.date`, else the dated milestone), `overdue` (effective_due < today, card open), `due_soon` (within 7 days), `snoozed` (`snooze` > today). Sent on every row; never written to the file.
3. Pane: a snoozed card is hidden from every open section and listed under a Snoozed filter (beside the existing Private toggle); the row of an overdue card gets a red date chip, due-soon an amber one, drawn where the priority flag is; the `Card` struct gains `dueDate`, `overdue`, `dueSoon`, `snoozed`. Sorting: within the Manual and Priority sorts, overdue rises above equal-priority peers (one comparison in `sortCompare` before the rank tiebreak).
4. `board_update_card` accepts `due` as a bare date or object, `snooze` as a date; `null` clears.

**Hierarchy**
5. `board_create_card` takes `parent` and `blocked_by`; `board_update_card` takes `parent`, `blocked_by`, `duplicate_of` (validated per Step 1.4).
6. Computed reverse index in `board.py` (`Board.links_index()`, one pass over the loaded cards, cached per `rev`): `children[id]`, `blocks[id]`, `duplicated_by[id]`, `related_from[id]`. `_read` returns `children: [{id, title, status, done}]` and `reverse: {blocks: [...], duplicated_by: [...], child_of: id|null}`; `_row` returns `children_total`, `children_done`, `parent`.
7. Card page: a **Children** block under the front-matter line, one line per child (`#id title · status`), with `done/total` progress; a **Links** block listing `child of #X`, `blocked by #A, #B`, `blocks #C`, `duplicate of #D` / `duplicated by #E`, `related #F`. Every `#id` is the existing card link. Row: `⮋ 2/5` progress badge after the tasks badge when `children_total > 0`.
8. A cycle or dangling id on an already-written board is shown muted on the page as "(missing)" rather than failing the read.

### Docs and policy (with Step 5)
- Protocol 19.2 row and 19.3 write tables gain the new keys; a new 19.23 "Card metadata: dates, resolution, owner, hierarchy" says what is stored, what is computed, and that all of it is optional.
- `board_policy.md` rule text: a duplicate closes with `resolution: duplicate` and `duplicate_of`; a card that is part of a larger one sets `parent`.
- `.board/POLICY.md` regenerates from it.

### Not in this card
- Estimates, sprints, time tracking, custom fields, watchers or votes, multiple assignees, start dates or a timeline view (owner, 2026-09-24).
- Reminders when a due date passes: the board shows it; a notification is a separate ask.
- Importing GitHub `state_reason` / sub-issues into these fields (`board_import`, `forge_sync`): follow-up card once the fields exist.

## Tests
- `tests/test_board.py`
- `tests/test_board_tools.py`
- `tests/test_land.py`
- `tests/test_qa_verifiers.py`
- `tests/test_tests_protocol.py`
- `ctest -R board`
- `ctest -R boardpane`
- manual: docs/qa_evidence/2026-09-25-mj76-card-metadata/notes.md

A clean archive of `43528a38` passed 703 Python tests (1 skipped). `scripts/relay-build --target relay-board-tests relay-boardpane-tests` and the two C++ tests passed. `land.py` built the exact UI commit tree.

## Execution Summary
Implemented optional close reason, human owner, due/snooze dates, dated milestones, commit sequences and card hierarchy. Board rows and card pages show the computed metadata; `land.py commit` records hashes on named cards. Code landed in `510de4f1`, `850876a2` and `3512773d`.
Evidence: docs/qa_evidence/2026-09-25-mj76-card-metadata/notes.md

Revision-order follow-up `43528a38` makes `tests_check` and QA choose the last, newest hash of the card sequence.
