---
id: 3XZV
type: work
status: needs-qa-llm
assignee: agent
implemented_by: glm/glm-5.3
rank: zzzzzzzzzzz
created: '2026-09-19'
links: {commits: [4f5acd43, d7aebe6f], evidence: [docs/qa_evidence/2026-09-20-switchboard-sections/], github: null, plans: [], related: []}
---
# improve switchboard sections

## Issue
dont require that new switchboard sections collect something. eg i want to create a research section, i can put things in there manually.

make these default switchboard sections:

inbox

discussing 

planning

planned

executing

needs verification

needs QA

done

make these section moves deterministic based on stage. inbox on entry. discussing on first thread entry. planning when planning activated. planned when plan is ready.  executing after you hit execute, but before the agent says its done. the agent then moves to needs verification. after verification, moves to needs qa or back to an earlier stage if needed.: discussing, planning, planned., execuiting.

## Plan
**Goal**

Two changes to the Switchboard's sections. First, a section no longer has to collect a status: a board can add a manual section (e.g. "research") and park cards in it by hand. Second, the default sections become the stage lifecycle — inbox, discussing, planning, planned, executing, needs verification, needs QA, done — and the moves between them are made by Relay at the stage events, not left to an agent's judgment.

**Findings**

- Sections are views of statuses: `board.yaml` `columns:` + `column_statuses:`, read by `B.column_statuses_of()` and `COLUMN_IDS`/`COLUMN_STATUSES` in `backend/relay_core/board.py` (~2000-2034); defaults in `DEFAULT_CONFIG`/`CONFIG_TEXT` (~965-984). Work statuses live in `WORK_STATUS_FOLDER` (~170).
- A new section must collect ≥1 status in both editors: GUI `src/BoardSections.cpp` `SectionPlan::addRefusal` ("A new section needs at least one status to collect…") and worker `backend/relay_core/board_tools.py` `_sections` (~2189: a column outside `COLUMN_IDS` must appear in `column_statuses`). A no-status column is dropped from the drawn list (`src/BoardModel.cpp` `Model::sections()`, `if (statuses.isEmpty()) continue`), and placement is status-only (`sectionForCard`).
- Stage statuses today: work cards have inbox, discussing, ready, in-progress, needs-*, deferred, done. `planning`, `planned`, `needs-verification` do not exist; `executing` exists only for plan cards. Moves are advice to agents (`backend/relay_core/board_policy.md`, the `executeTask`/`verifyTask` briefs in `src/BoardModel.cpp`), except Execute, which the GUI already moves to in-progress (`src/BoardPane.cpp` ~4226-4230).
- Hooks for deterministic moves already exist: `board_ask` appends the owner's comment then starts the turn (`backend/relay_core/board_protocol.py` ~1252-1276); a finished Plan turn appends its answer (`_on_answer` ~495-510); `BoardTools.begin_card_turn`/`end_card_turn` fence what the model may write.
- QA: `QA_STATUSES` (board_tools ~115); the Verify button appears when status starts with `needs-qa` (`src/BoardPane.cpp` `inQaLane()` ~1456); `verifyTask` says "to `done` when the checklist holds, or back to `in-progress`".

**Steps**

1. `backend/relay_core/board.py`: add work statuses `planning`, `planned`, `executing`, `needs-verification` (folder `""`, like inbox) to `WORK_STATUS_FOLDER`; put them in `_STATUS_ORDER` in stage order; add them to `COLUMN_IDS` and `COLUMN_STATUSES`; make `DEFAULT_CONFIG`/`CONFIG_TEXT` columns exactly `inbox, discussing, planning, planned, executing, needs-verification, needs-qa, done`. Keep `ready`/`in-progress` valid so existing boards are untouched.
2. `board.py`: new front-matter field `section` (`WORK_FIELDS`, `ALLOWED_FIELDS`, `FIELD_ORDER`) — the id of the manual section a card is parked in. `check` validates its shape and warns when it names no configured column.
3. Manual sections: `_sections` in `board_tools.py` accepts a column that collects nothing (drop the refusal at ~2189 for status-less ids; update the tool description); `src/BoardSections.cpp` `addRefusal` allows an empty status pick ("a section you fill by hand") and the row shows "manual"; `Model::sections()` keeps configured no-status columns; `sectionForCard` lets a card's `section` win over its status while that column exists.
4. Moving in and out: `board_move_card` and the GUI's `board_move` request take a `section` argument — dropping on a manual column sets (or moves) `section:` and leaves `status` alone; dropping on a status column clears it. GUI `moveCard` and quick-add send `section` when `dropStatus()` is empty (`src/BoardPane.cpp` ~3100, ~4024-4060); card rows carry `section` (`board_tools.py` ~1491, protocol 19.2).
5. Deterministic stage moves, worker side — one helper (e.g. `stage_advance(card, event, reason)` in `board_tools.py`), each move also writing an event entry to the thread saying why:
   - first non-`event` thread entry on an `inbox` work card → `discussing` (from the owner-entry append in `board_ask` ~1255 and `_comment`);
   - a Plan turn starting on an open work card → `planning` (in `board_ask`, after the entry append);
   - a Plan turn that leaves a `## Plan` section on the card → `planned` (at turn end, `_on_answer`/`end_card_turn`).
6. Executing: the Execute handler in `src/BoardPane.cpp` (~4226) moves the card to `executing` instead of `in-progress`; the `executeTask` brief (`src/BoardModel.cpp` ~393) says "when it lands, move to `needs-verification`".
7. Verification: offer Verify on `needs-verification` too (`inQaLane()`); `verifyTask` branches by lane — from `needs-verification`, pass → `needs-qa-llm` with the `## Verdict`, fail → back to whichever of discussing/planning/planned/executing the failure warrants, with the failures on the thread; from a QA lane it still closes to `done` with the `Verified-By` trailer.
8. Wording: `statusTitle`/`sectionMeaning`/`defaultSectionStatuses` entries and the `setConfig`/`statusChoices` fallbacks in `src/BoardModel.cpp`; update `WARP.md`, `docs/SWITCHBOARD-FORMAT.md`, `docs/SWITCHBOARD-DESIGN.md` §3 ("Tabs and columns"), `docs/AGENT-SESSIONS-PROTOCOL.md` (19.2 row field, `board_move`, the briefs), `backend/relay_core/board_policy.md` and the board briefs' lane wording.
9. This repo's own board: `issues/board.yaml` columns → the new eight; migrate its cards `ready`→`planned`, `in-progress`→`executing` (front matter only — none of these statuses live in a subfolder, so no file moves); `scripts/relay-board.py check` clean afterwards.

**Risks**

- **Needs a decision:** after verification passes, is the needs-QA step the existing cross-model QA run (verifier closes to `done`, stamps `verified_by`)? The plan assumes yes — two model checks in a row (verify, then QA).
- **Needs a decision:** `executing` becomes both a plan status and a work status, so one `executing` section would collect plan cards too. Assumed fine (one list); say so if plan cards should stay apart.
- **Needs a decision:** a card parked in a manual section stays there through stage moves (`section` wins until moved out); its stage shows in the card detail, not in the list.
- Removing a manual section leaves dangling `section:` values — `check` warns and the cards fall back to their status section.
- Old boards keep their configured columns and statuses; only new boards (and this repo's) get the new defaults.

**Verify**

- `tests/test_board.py`: the four statuses are valid for create/move; `section` round-trips and `check` warns on a dangling one; each `stage_advance` hook fires from its `board_ask`/`_comment`/plan-end call site; `_sections` accepts a manual column and still refuses a typo'd id.
- `tests/boardmodel_test.cpp`: the new default columns; `sections()` keeps a manual column; a card with `section` lands there; `dropStatus()` empty on a manual column; new titles and meanings.
- Live under Xvfb (isolated `XDG_CONFIG_HOME`): create (inbox) → comment (discussing) → Plan (planning → planned) → Execute (executing) → land (needs-verification) → Verify (needs-qa → done); add a "Research" section collecting nothing and drag a card into it.

## QA checklist
- [ ] Live GUI walk under Xvfb with an isolated `XDG_CONFIG_HOME`, on a fresh board: create (inbox) → comment (discussing) → Plan (planning, then planned when the `## Plan` lands) → Execute (executing) → the agent's landing move (needs-verification) → Verify (needs-qa-llm, then done with the `Verified-By` trailer).
- [ ] Add a "Research" section collecting nothing in the section editor; drag a card into it (parked, status unchanged in the detail); drag it out onto a status column (unparked); `relay-board.py check` warns if the section is removed while a card is still parked.
- [ ] `pytest tests/test_board.py tests/test_board_tools.py` and `ctest --test-dir build -R 'board(model|sections)'` still pass on the new statuses, `section` round-trip, `stage_advance` hooks and the editor refusal that now allows no statuses — written per the plan's Verify list, skipped this round on the owner's "no tests" instruction.
- [ ] An old board (columns without the new eight) still renders exactly as before; `ready`/`in-progress` cards keep their sections.
- [ ] Evidence: `docs/qa_evidence/2026-09-20-switchboard-sections/` (build green, worker smoke, board check; commit 4f5acd43).
