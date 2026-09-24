---
id: VKFV
type: work
status: needs-qa-llm
assignee: agent
implemented_by: glm/glm-5.3
rank: zzzzzzzzzzzzi
created: '2026-09-19'
links: {plans: [], commits: [81ff60f8, c0208584, eec29cdd, 97aa5639, 246125b8], evidence: [docs/qa_evidence/2026-09-20-switchboard-priority-flags/], related: [], github: null}
---
# switchboard improvements

## Issue
remove the current first colum, those icons arent useful because they just reflect sections. instead add a flag icon, and its an empty circle i can click on to priotize. left click increases priority, right click reduces. priority zero (default) is clear. priority -1 is yellow. +1 is white, +2 is pale green, +3 is bright green. and you can sort on that. 

but the # code as a second column before the title. 

clean up should annotate bug, feature, gui, etc and should also suggest tags.  those should become a second set of filter  next to the section list. 

there seem to be 3 date columns, remove the leftmost of the 3.

## Plan
**Goal.** Rebuild the card row's left end: the status glyph goes, a clickable priority flag (−1…+3, left/right click, sortable) takes its place, and the `#ID` becomes a fixed second column before the title. Drop the leftmost of the three date-like things on a row (the age badge — see Risks). Make **Clean up** annotate `bug`/`feature`/area labels, and turn labels into a second filter row beside the section checkboxes.

**Findings.**
- Rows are shaped by `cardShape()` in `src/BoardPane.cpp` (~203–280) and painted by `RowDelegate::paint` (~448–487): glyph (`statusGlyph`, `glyphInk`, `kGlyphWidth`), then title, then the `#ID` *after* the title (`shape.idRect`), then badges, then the `createdRect`/`updatedRect` date columns (`dateColumnWidth`, `dateColumnsFit`). The right-hand badges end with the age badge (`Badge::Age`, appended in `board::rowBadges`, `src/BoardModel.cpp` ~693–698, `cardAge`) — so a row's right end reads `[5 w] [created] [updated]`: three date-like columns, and the leftmost is the age badge.
- `ColumnHeader` (`src/BoardPane.cpp` ~569–716) holds the Card/Created/Updated sort cells; sorts live in `board::Sort`/`SortColumn`/`nextColumnSort` (`src/BoardModel.h` ~82–110, `BoardModel.cpp` ~160–276), persisted by the pane via `setSortOrder` (~2443).
- `board::Card` has no priority. The worker's row is built in `backend/relay_core/board_tools.py` (~1491); front-matter fields are `COMMON_FIELDS`/`ALLOWED_FIELDS` in `backend/relay_core/board.py` (~199–230); agent writes go through `board_update_card` (`IMMUTABLE_FIELDS` board_tools.py ~103, writable-set check ~1784).
- A row click has no `base_hash`, so `board_update` won't do; priority needs its own message shaped like `board_move` (sent at `src/BoardPane.cpp` ~3845, dispatched in `backend/relay_core/board_protocol.py` ~1080).
- The section checkboxes are `m_checks`, a wrapped row in `buildListTools` (~2741–2753, `syncSectionChecks` ~2876), with the hidden set persisted in the pane's layout node (~3744–3763). `Model::rows(collapsed, hidden)` filters (`BoardModel.cpp` ~1250), `Model::matches` already speaks `label:` (~1332), and `Model::allLabels()` exists (~1307).
- Clean up is `board_cleanup` (protocol 19.9); its instruction is the text file `backend/relay_core/board_cleanup_brief.md`, whose step 5 already touches labels.
- The glyph column is also the one place a running turn shows (`m_working`, ~448–453) — keep that mark.

**Steps.**

*A. Priority flag (replaces the first column)*
1. `backend/relay_core/board.py`: add `priority` to `COMMON_FIELDS` and the `ALLOWED_FIELDS` sets — an int clamped to −1…+3, default 0, written to front matter only when nonzero. Send it in the row dict (`board_tools.py` ~1491).
2. `backend/relay_core/board_protocol.py`: new `board_priority {id?, card, priority}` — clamps, writes, answers `board_written` + `board_changed`; no `base_hash` (one clamped int, like a drag's rank). Also let `board_update_card` set `priority` (it is not immutable) so agents and cleanup can.
3. `src/BoardModel.h/.cpp`: `Card::priority` (parsed in `fromJson`); `Sort::PriorityHigh`/`PriorityLow` (+ `sortId` `"priority"`/`"priority-low"`, titles) and `SortColumn::Priority`; `nextColumnSort` cycles high → low → Manual; `Model::sorted()` keys on priority with ties keeping the section's order. Retire `statusGlyph`/`glyphInk` from the row path (and the function, if nothing else uses it).
4. `src/BoardPane.cpp` `cardShape()`: `glyphRect` becomes `priorityRect` (same 15 px box, ~10 px circle) — empty ring at 0, yellow at −1, white at +1, pale green at +2, bright green at +3, as four new board palette tokens in `src/Theme.cpp` beside the Switchboard styles. The `#ID` moves to a fixed second column (mono, width of `#WWWW`), title after it; update `dateColumnsFit` and `ColumnHeader`'s left margin (~646), and add a ⚑ Priority cell as the header's first cell. While a turn runs on the card, the ✦ still draws there in `theme::Agent`.
5. Clicks in `RowList::mousePressEvent` (~760), mapping the press through the same shape function: left = min(p+1, +3), right = max(p−1, −1); update the model locally, then send `board_priority`. Tooltip: "Priority N — left-click raises, right-click lowers."

*B. Date columns*
6. Remove the age badge from `board::rowBadges` (`BoardModel.cpp` ~696–698); retire `Badge::Age`, `cardAge` and its `badgeDropOrder` entry if nothing else uses them. Rows keep Created and Updated. *(If the owner meant the Created column, this is the same one-line change one rect over — see Risks.)*

*C. Labels: clean up annotates, the list filters*
7. `src/BoardPane.cpp`: a second wrapped chip row under the section checkboxes (`boardLabelChecks`), one chip per label from the board config's labels ∪ `allLabels()`. Ticking chips keeps only cards carrying every ticked label (AND, like `label:` terms), composing with the text filter and the section checkboxes; persist beside `hidden_sections` in the pane's layout node (e.g. `label_filter`). Model side: a label set applied wherever `matches()` gates rows (`rows()`, `cards()`, `openCount()`, `hiddenCount()`).
8. `backend/relay_core/board_cleanup_brief.md` (v2): strengthen step 5 — every work card gets `bug` or `feature` plus its obvious area labels from the board's own vocabulary (`gui`, `voice`, …); labels outside the vocabulary are **suggested** in the report (a "Suggested tags" line), never written. The report's Labels line counts annotations. No code change: the brief is the instruction.

*D. Docs*
9. `docs/AGENT-SESSIONS-PROTOCOL.md`: `priority` in the 19.2 row, `board_priority` in the 19.3 table, `board_update_card`'s writable set. `docs/SWITCHBOARD-DESIGN.md`: the row/column table (flag column, `#ID` column, no age badge, label chips).

**Risks.**
- *The "3 date columns" reading.* Rows have two real date columns (Created, Updated); the third date-like thing is the age badge (`5 w`) sitting directly left of Created, so the plan removes that. Question #1 on the card asks the owner to confirm — if they meant Created, step 6 removes `createdRect` instead and the header drops to one date cell.
- *Clicks clamp* at the ends (+3 stays +3, −1 stays −1); wrap-around was not asked for.
- *White at +1* must stay legible in every shipped theme — it is a theme token (checked by `theme_test`), not a hard-coded colour in the row.
- *Sort interplay:* like every non-Manual sort, Priority ordering hides the manual drag order on screen (existing rule, `BoardModel.h` ~66–70).
- *Cleanup cost:* annotating labels is one `board_update_card` per card; a large board may hit the run's write limits — the brief already says to stop and report.

**Verify.**
- Targeted tests: `ctest --test-dir build -R boardmodel` (priority parse/clamp, sort cycle, badges without Age, label filtering), `-R theme`; `pytest tests/test_board_tools.py tests/test_board_protocol.py tests/test_board.py` for the field, the message and the clamp.
- Live under Xvfb with an isolated `XDG_CONFIG_HOME`: left/right clicks step −1…+3 with the four colours and persist after reload; `#ID` in its own column; no age badge; the ⚑ header sorts; label chips filter the list; a dry-run **Clean up** previews label annotations and suggested tags.

## QA checklist
- [ ] Flag column: an unflagged card shows an empty ring; left-click steps +1 → +2 → +3 and stops, right-click steps down to −1 and stops (yellow at −1, white at +1, pale green at +2, bright green at +3). Each click leaves the `Flagged #ID at ±N` notice, Ctrl+Z undoes it, and the card file carries `priority:` (absent at 0).
- [ ] `#ID` is a fixed mono column before the title on every row, ids aligned; the age badge is gone — a row's right end is badges, Created, Updated, nothing date-like before them.
- [ ] The ⚑ header cell sorts high → low → Manual; while a non-Manual sort is on, a drag within a section answers with the usual notice; while a turn runs on a card the ✦ draws over its flag.
- [ ] Label chips under the section checkboxes: ticking keeps only cards carrying every ticked label, the count says "N shown", unticking restores; the choice survives a window save/restore (`labels` in the board layout node).
- [ ] Clean up (dry run) annotates `bug`/`feature` + existing area labels and *suggests* tags outside the vocabulary in its report instead of writing them (brief v2; needs a keyed model — the live run here could not start one).
- [ ] Theme: with a light theme (IBM Beige) the +1 dot is the theme's own text token, not literal white — `theme_test` holds all four tokens to 3:1 on the face.

Implementer evidence: `docs/qa_evidence/2026-09-20-switchboard-priority-flags/` — live Xvfb/OCR run of the clicks, clamps, notices, both sort orders, the chips, and the pixel colours of the discs and the ring, plus the crashing first pass and its fix. Unit tests added: `tests/test_board.py`, `tests/test_board_tools.py`, `tests/test_board_protocol.py`, `tests/boardmodel_test.cpp`, `tests/theme_test.cpp` (all green on the implementer's tree: 399 python cases, board + boardsections + theme ctest).
