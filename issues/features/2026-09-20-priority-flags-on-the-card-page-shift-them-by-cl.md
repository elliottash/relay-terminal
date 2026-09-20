---
id: DPJB
type: work
status: inbox
labels: [feature, switchboard]
rank: zzzzzzzzzzzzzzzw
created: '2026-09-20'
source: pane, 2026-09-20
links: {plans: [], commits: [], evidence: [], related: [VKFV], github: null}
---
# Priority flags on the card page: shift them by clicking, and let agents set priority

## Issue
allow priority flag shifts in the card page. and allow agents to set priority as well.

## Plan
**Goal.** The priority flag exists on a card *row* (#VKFV: click the ring to raise/lower, `board_priority`, sortable). This card takes it to the two places it is missing: the **card page** (the detail view) gets the same clickable flag in its header, and an **agent** is told it may set the flag, in the tool it already writes through.

**Findings.**
- The click path is already one function: `BoardView::setCardPriority(QString id, int step)` (`src/BoardPane.cpp` ~4540) clamps at −1…+3, updates the model, shows the `Flagged #ID at ±N` notice and sends `board_priority`. The row only supplies the click (`RowList::mousePressEvent` ~900 → `m_list->onPriority`).
- The flag's drawing is shared too: `drawPriorityFlag(QPainter&, QRect, int)` (`src/BoardPane.cpp` ~160), the ring at 0 and the four theme-token discs.
- `CardDetail` (~1274) is the card page: a header row of `#ID`, `#ID → prompt`, `Open file`, `×` (~1286), then title, pickers (`m_status`, `m_tab` → `onMove`) and the document. It shows no flag at all, so the flag cannot be shifted from the page where the card is read. Its `show(card)` (~1746) already has `front`, which carries `priority` when nonzero.
- Agent side: `board_update_card` **already accepts** `fields: {"priority": N}` — `relay_core/board.py` `clamp_priority` is applied in `BoardTools._update` (~1844), `0` removes the key, and `test_board_update_card_sets_the_flag_like_any_field` passes today (verified: `python3 -m unittest tests.test_board_tools.PriorityTests` → 5 ok on this tree). What is missing is that nothing *tells* a model so: the tool's own description (`board_tools.py` ~195) never names `priority`, and the Switchboard page agent's brief (`board_chat_brief.md`) tells it to "fix labels and statuses" but not the flag.

**Steps.**
1. `src/BoardPane.cpp`: a small `PriorityFlagButton` widget beside `drawPriorityFlag` — paints the shared ring/disc, a left click calls `onStep(+1)`, a right click `onStep(−1)`, tooltip in the row's own words ("Priority N — left-click raises the flag, right-click lowers it"), pointing-hand cursor, no focus.
2. `CardDetail`: the button at the head of the header row, before `#ID` (the same order a row reads: flag, `#ID`, title); `setPriority(int)` and `onPriority` on the class; `show()` sets it from `front.priority`; it is hidden when no card is open.
3. `BoardView`: `m_detail->onPriority = [this](int step) { setCardPriority(m_detail->cardId(), step); }` — one write path for row and page alike — and `setCardPriority` also hands the new value to an open detail, so the disc turns the moment it is clicked rather than at the worker's `board_changed`.
4. Agents: name the flag in `board_update_card`'s description ("set the priority flag with `fields: {priority: −1…+3}`, 0 clearing it") so the model can find it; add the flag to `board_chat_brief.md`'s "fix labels and statuses" line.
5. Tests and docs: a spec test that the description names `priority`; `docs/AGENT-SESSIONS-PROTOCOL.md` 19.2/19.3 already describe the field and that an agent sets it through `board_update_card` — no change needed there beyond confirming; `docs/SWITCHBOARD-DESIGN.md`'s card-page paragraph gets the flag.

**Risks.** A click on the detail's flag must not be read as a card edit or a move; it goes through `setCardPriority`, which is the row's own path. The detail is rebuilt from the worker on every `board_changed`, so the local set is a head start, not the record. An agent setting `priority` in a field write is already hash-checked and rate-limited like any other write, and it is logged in the thread.

**Verify.** `ctest --test-dir build -R board` (GUI model/pane) and `python3 -m unittest tests.test_board_tools -v` for the descriptions; live under Xvfb with an isolated `XDG_CONFIG_HOME`: open a card, click the header flag left → `Flagged #ID at +1` and `priority: 1` in the file, right-click → back to 0 with the key gone; Ctrl+Z undoes it.

## Evidence
**What changed.**

- `src/BoardPane.cpp`: a `PriorityFlagButton` widget (the row's own `drawPriorityFlag`, a left click `onStep(+1)`, a right click `onStep(−1)`, the row's tooltip, pointing-hand cursor, no focus), placed at the head of the card page's header before `#ID`; `CardDetail::setPriority` + `onPriority`; `show()` takes the value from the card's `front`; `BoardView` wires `onPriority` to the existing `setCardPriority` and hands the new value to an open detail so the disc turns under the click. Docs: `docs/SWITCHBOARD-DESIGN.md` 4.5.
- Agents: `board_update_card`'s description now names the flag (`{"priority": 1}`, −1…+3, 0 clearing it) — the field was already writable (#VKFV) but nothing in the model's view of the tool said so — and `board_chat_brief.md` tells the page agent the flag is its to set.
- Tests: `tests/boardpane_test.cpp` `theCardPagesFlagClicksThroughToBoardPriority`; `tests/test_board_tools.py` `SpecTests.test_the_update_tool_says_the_priority_flag_is_settable`.

**Verified.**

- `ctest --test-dir build -R boardpane` → 5 passed (the new case included); `ctest --test-dir build -R 'boardpane|boardmodel|boardsections'` → 2/2 passed.
- `python3 -m unittest tests.test_board_tools.SpecTests tests.test_board_tools.PriorityTests` → ok (11 cases: the description names the flag; `board_update_card` sets it, 0 clears it, a bad value is refused). Three *other* tests in that file are red on main for reasons that are not this card — filed as #VASY with the measured output.
- Live under Xvfb (`docs/qa_evidence/2026-09-20-card-page-priority-flag/`, `drive.sh` + `ocr.txt` + PNGs): the card page's header reads `| @ #9ZAH #ID → prompt (t) Open file (o) × |`; a left click took +2 → +3 with the file at `priority: 3`, the disc repainted in the theme's bright green, and the notice `Flagged #9ZAH at +3`; a second left click clamped at +3; right clicks walked +3 → +2 → +1 → 0 (the key gone, the empty ring) → −1 (yellow) with `Cleared the flag on #9ZAH` at 0; Ctrl+Z undid the last write; and clicking Alpha's flag in the *list* then opening its page showed the same flag at the same place (one flag, two places).

## QA checklist
- [ ] The card page's header shows the flag at its head, left of the `#ID`: an empty ring on a card with no priority, the disc in the theme's colour otherwise (yellow −1, white +1, pale green +2, bright green +3).
- [ ] Left click raises the flag by one and right click lowers it by one, from the page as from the row: +3 and −1 are the ends (a click past them leaves the value alone, and a click that lands on 0 still writes 0), the pane's notice names the value (`Flagged #ID at +N`, `Cleared the flag on #ID`), Ctrl+Z undoes it, and the card file's `priority:` matches — absent at 0.
- [ ] The page and the row are one flag: click in the list, open the card, and the page shows that value; click on the page, and the row behind it follows.
- [ ] The flag is drawn with the theme's tokens, not literal colours: a light theme (IBM Beige) still paints a legible +1 disc, and `theme_test` holds all four to 3:1 on the face.
- [ ] An agent can set the flag: `board_update_card`'s description names `priority` (visible in the model's tool list), `fields: {"priority": 2}` writes it, `0` removes the key, a non-integer is refused with `field: priority`, and the write is logged on the card's thread like any other.
- [ ] `board_chat_brief.md`'s new bullet reaches the page agent's prompt (no `<!--` left in `cleanup_brief()`/the chat brief) and the agent is told to say which cards it flagged.
