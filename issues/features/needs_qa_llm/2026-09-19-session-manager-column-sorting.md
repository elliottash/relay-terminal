---
id: EV45
type: work
status: needs-qa-llm
assignee: agent
implemented_by: glm/glm-5.3
rank: zzzzzzzzzz
created: '2026-09-19'
links: {commits: [ddbcdc4c, 9bfef232], evidence: [docs/qa_evidence/2026-09-19-session-manager-column-sorting/], github: null, plans: [], related: []}
---
# session manager column sorting

## Issue
in the session manager, clicking the column headers does not sort by that column. it should!

## Plan
**Goal.** Clicking a column header in the session manager (Session, Updated, Turns, Model) sorts the list by that column; a second click toggles the direction; the header shows the arrow, and "Show more" pages continue in the same order.

**Findings.**
- The list is the `QTreeWidget m_tree` in `src/Conversations.cpp` (`SessionManager`, ~line 610): 4 columns, built by `rebuildTree()` in worker order. `setSortingEnabled` is never called and `QHeaderView::sectionClicked` is not connected, so a header click does nothing today.
- Qt's built-in tree sort must **not** be used: it would reorder the group rows ("Continue", the date groups) alphabetically and sorts display text ("14 min ago", "9 · 2 open"), which is wrong.
- The order comes from the worker: `queryRequest()` (~978) sends `sort` when ≠ "recent"; `ConversationIndex.search()` in `backend/relay_core/conv_index.py` (`SORTS` ~194, the `order` map ~1735) accepts "recent", "oldest", "longest", "relevance", pinned first in every order. `session_protocol.py` (~1201) passes `sort` through, and `search()` raises on unknown ids — so the backend list is the one place to extend. Worker-side sorting also keeps `offset`/`next_offset` paging correct.
- The visible sort control is the `m_sort` combo (~569) with those same four ids; typing switches recent/relevance unless the user picked a sort (`m_sortChosen`, ~835).

**Steps.**
1. `backend/relay_core/conv_index.py`: extend `SORTS` and the `order` map in `search()` with `shortest` (`c.turns ASC, COALESCE(c.updated, 0) DESC`), `title`/`title_desc` (`COALESCE(NULLIF(c.custom_title, ''), c.title, '') COLLATE NOCASE ASC|DESC, COALESCE(c.updated, 0) DESC`), `model`/`model_desc` (`CASE c.source WHEN 'terminal' THEN 'terminal' WHEN 'claude' THEN 'Claude Code' WHEN 'codex' THEN 'Codex' ELSE c.model END COLLATE NOCASE ASC|DESC, COALESCE(c.updated, 0) DESC` — the CASE mirrors what column 3 displays, `addSessionRow`). Update `search()`'s docstring. No change in `session_protocol.py`.
2. `docs/AGENT-SESSIONS-PROTOCOL.md` §14.3: the `sort` value list (~1083) and the sentence describing it (~1095).
3. `src/Conversations.{h,cpp}`: two pure helpers beside `dateGroupOrder()` so they are unit-testable: `QString nextHeaderSort(int column, const QString &current)` (Updated: recent↔oldest, Turns: longest↔shortest, Session: title↔title_desc, Model: model↔model_desc) and the column/order a sort id shows (`headerSortColumn`, `headerSortOrder`; none for "relevance").
4. `SessionManager` constructor: five new `m_sort` entries ("Fewest turns", "Title A→Z", "Title Z→A", "Model A→Z", "Model Z→A"); connect `m_tree->header()`'s `sectionClicked` to `m_sort->setCurrentIndex(findData(nextHeaderSort(...)))` — the combo's existing `currentIndexChanged` chain re-queries and sets `m_sortChosen`, so typing later does not override the click; on every sort change, a small `updateSortIndicator()` sets `setSortIndicatorShown`/`setSortIndicator` from the helpers. Keep `setSortingEnabled(false)` (Qt's own sort is never used). `queryRequest()` sends the new ids unchanged.

**Risks.**
- The deliberate tree shape must survive: the "Continue" band stays first, group rows keep their own order (rows sort within them), and thread rows keep their started order under an owner — all of which is why sorting happens worker-side, not in the tree.
- Pinned rows stay in front of every sort (existing worker behaviour); a title sort therefore lists pinned first. Kept for consistency.
- Question (small): should the Sort combo gain the five new entries, or stay at four with the header the only way to reach the new sorts? Recommendation: add them — the combo must never show an order the list is not in (the pane's own rule for the scope menu).
- The combo label wording ("A→Z") is a placeholder; rename freely.

**Verify.**
- Backend: new cases beside the existing sort assertions in `tests/test_session_threads.py` (~241–248) and `tests/test_conv_index.py` — order of `shortest`, `title`, `title_desc`, `model`, `model_desc`; an unknown sort still raises `ValueError`.
- GUI: a new test in `tests/conversations_test.cpp` beside `searchingSortsByBestMatchUntilTheUserSaysOtherwise` (~752): emit `sectionClicked`, assert the request's `sort` (Updated → "oldest", again → "recent"; Session → "title"), that the combo follows, the indicator's column/order, and that typing afterwards keeps the clicked sort.
- Run `scripts/relay-build`, then `ctest --test-dir build -R conversations` and `pytest tests/test_conv_index.py tests/test_session_threads.py -k sort`. Live check: open the session manager (Ctrl+Shift+Y), click each header, watch the arrow and the order; "Show more" continues in the same order.

## QA checklist
Implementer evidence: `docs/qa_evidence/2026-09-19-session-manager-column-sorting/` (notes + captured test output), commit `ddbcdc4c`.

- [ ] Backend orders: `shortest`, `title`, `title_desc`, `model`, `model_desc` order as protocol 14.3 says — ties newest first, pinned first, the Model CASE mirrors the column (terminal / Claude Code / Codex / model), an unknown sort still raises `ValueError` (`IndexTests.test_shortest_title_and_model_sorts`, `ThreadStoreTests.test_sort_and_paging`).
- [ ] Header clicks: each column sorts, a second click toggles, the Sort combo follows (all nine orders are listed, so the combo never names an order the list is not in), the arrow shows the clicked column and direction, and `relevance` hides it (`headerClickSortsByThatColumn`).
- [ ] The click is the user's own sort: typing afterwards does not switch to `relevance`; with no sort chosen, typing still switches recent↔relevance and the arrow follows.
- [ ] Tree shape survives: Continue band first, group rows keep their order (rows sort within them), thread rows keep started order under an owner; Qt's tree sort stays off.
- [ ] Paging: "Show more" continues in the clicked order (worker-side sort + offset).
- [ ] Live sweep: Ctrl+Shift+Y, click each header, watch the arrow and the order, page with "Show more".
