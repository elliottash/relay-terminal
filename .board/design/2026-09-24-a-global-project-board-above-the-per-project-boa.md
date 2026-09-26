---
id: V3R3
type: work
status: needs-verification
labels: [feature, board, switchboard, research]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: ada00cf0-62a9-4040-9c07-b0a0e535ce97
blocked_by: [BP15]
priority: 2
rank: zzzzzzzzzzzzzzzzzzzzw
created: '2026-09-24'
verify: {artifact: system, primary: script, also: [person], human: required, criteria: 'Targeted pytest (test_board_projects, test_projects_protocol) and ctest projects/projectspane pass; under Xvfb with three fixture projects, two sharing a root and one without a checkout, the Projects tab shows three distinct rows, the missing root as missing, the Focus cap, and a posted update; relay-board check refuses a global work card naming one project.', sign_off: none, effort: medium, stakes: rework}
source: owner in Relay pane, 2026-09-24
links: {plans: [], commits: [fab830d4f891, 5c4f7e56e03b, 03a9b0500e9a, 4cb0f2252d50], evidence: [docs/qa_evidence/2026-09-26-projects-feature/], related: [JN7X, 1QKM, GRT2, ZT58, B253], github: null}
---
# A Relay-wide Projects feature above per-project Boards

## Issue
Build a Relay feature for tracking projects across per-project Boards: mark critical projects,
set optional time budgets, and keep cross-project work in one place. Existing personal organizers
were offered as design examples, not as data to import or migrate. The owner clarified this on
2026-09-25. Private source links and details have been removed from this public card.

## Done means
The research and proposal describe a reusable Relay Projects feature with links to per-project
Boards, cross-project work, priority, optional budgets, privacy boundaries and a phased product
slice. The proposal does not depend on migrating one person's trackers or exposing their data.
Implementation (steps 1–5, 7), added 2026-09-26:
- `project` is a Board card type in the global Board, with the plan's statuses and fields, validated by `check` and rendered in BOARD.md.
- A work card in the global Board naming fewer than two projects in `projects` fails `check`.
- `state/projects.json` schema 2 links a machine root to a project card id and still reads a schema-1 file.
- `projects_list` / `projects_update` answer in the protocol shape of the plan and are documented in `AGENT-SESSIONS-PROTOCOL.md`.
- The Projects tab shows project cards with priority, status, health and its age, and next action, with a Focus group and the three filters. Two projects sharing a root stay distinct, and a missing root shows as missing.
- Budgets (step 6) are filed as their own card.

## Discussion points
The corrected product proposal is [docs/GLOBAL-PROJECT-BOARD-RESEARCH.md](../../docs/GLOBAL-PROJECT-BOARD-RESEARCH.md), with three general research passes under [docs/research/global-project-board/](../../docs/research/global-project-board/). The 2026-09-25 correction supersedes the earlier personal migration questions in the thread. Product decisions are listed in section 6 of the proposal. The examples were audited for private content in this repository.

## Decisions
2026-09-25 — Owner: “i want to build a feature for relay, not build my own board. those were just examples of organizing structures.” The proposal is for all Relay users; personal imports and migration are outside this card.

2026-09-26 — Owner, on the five questions in the thread: “i think -- work cards in global have to name at least 2 projects.” and “otherweise i agree, plan orchestration for subagents.” So: (1) the Projects tab is the surface, no new pane; (2) a `project` card type in the global Board, roots in the registry; (3) steps 1–5 ship as one slice; (4) budgets in a later card, agent spend with actuals first, planned hours plan-only, no focus-time meter; (5) a work card in the global Board must name **at least two** projects — work that belongs to one project goes on that project's Board. Personal, non-project work (email, refereeing, scheduling, teaching) is a separate card and does not hold this one up.

## Plan
Refined 2026-09-25 against the code as it stands (the proposal in `docs/GLOBAL-PROJECT-BOARD-RESEARCH.md` §3–4 is the product shape; this is how it maps onto Relay). Decisions of 2026-09-26 folded in.

**Goal.** A project is a fourth Board card type kept in the global Board, shown on the existing Projects tab. Portable identity and portfolio fields live in the card; machine-specific roots stay in the registry; Board rollups are computed at read time, never copied.

**Findings (code, 2026-09-25).**
- The global Board already exists at `$XDG_CONFIG_HOME/relay/switchboard` (a `board.yaml` with work tabs, `memory/`, `threads/`). The Globals tab edits it through `globals_*` messages; no Board pane opens it. Its agent instructions say it is not an inbox for work outside a project, and #916B dropped the personal inbox board.
- `board.CARD_TYPES` is `work, memory, alias`; each type has its own folder, status-to-folder map and field set (`ALLOWED_FIELDS`, `FIELD_ORDER`, `TASK_HEADING`), so a fourth type follows a worn path. Four-character ids, `#ID` addressing and the `board_links` reverse index (#EE42) give cross-references without new machinery.
- `relay::projects::Record` (`state/projects.json`, schema 1) is machine-specific by design: path, key, name, board, boardDir, reason, knownSince, lastAttached. It is the discovery cache and is removable; no portfolio field belongs in it.
- `ProjectsPane` (Sessions & Projects › Projects, #P7SJ) already lists known and pinned projects, live sessions with their `project_path`, and offers open Board, attach, forget. #EA37 decision 7 declined a project home page ("revisit at four tabs") and #P7SJ kept the pane at three tabs.
- `due.whose: mine|external` already landed from this research (#MJ76). Per-session provider cost exists (protocol §25) and a session carries its workspace, so agent spend per project is a join that is not built yet (usage → session → workspace → registry root → project card). Relay has no source at all for human hours.

**Steps.**
1. **Foundation (backend).** Add `project` to `CARD_TYPES`: folder `projects/`, statuses `active`, `paused`, `done`, `archived` (folder map like memory's), fields `name`, `priority` (the existing integer), `focus`, `health` (`{value, date}`, values on-track / at-risk / off-track), `next` (one line), `owner`, `due` (existing), `category`, `roots` (portable: a repository URL or a slug, never an absolute path). Validation in `check`, BOARD.md rendering, tests in the board pytest files.
2. **Registry link.** `Record` gains an optional `project` (card id); `kSchemaVersion` goes to 2 with a tolerant read of 1. A project card links a machine root by that id; a root absent on this machine is a valid link shown as such. `remember()` and the reasons list are untouched.
3. **View.** One worker request, `projects_list`, returns project cards joined with the registry plus per-Board open-card count and last Board activity (file mtimes, cached). `ProjectsPane` rows and details show priority, status, health with its age, and next action; a Focus group heads the list, its size a setting (default 5). New project, Edit and Archive go through `board_*` on the global root. Unlinked projects are created directly; a known workspace with no card is offered as one.
4. **Cross-project work.** A work card in the global Board carries `projects: [#ID, …]` and must name **at least two** (owner, 2026-09-26): work of one project belongs on that project's Board, and `check` refuses fewer. The Globals agent line changes from "not an inbox" to "a work card here spans at least two projects, named in `projects`". The card page's Linked panel (#EE42) lists a project's work cards; a project's per-project Board opens from the row.
5. **Review loop.** A project update is a thread entry (`kind: update`) with a health pick; the latest one is what the row shows and its age is the staleness signal. Filters on the Projects tab: no next action, update older than N days, due within N days.
6. **Budgets (own card, after 1–5).** Agent spend per project per month from the usage join, with a coverage label; planned hours as a plan-only field, no focus-time meter; threshold notices only, no blocking or model switching.
7. **Docs and hints.** `BOARD-FORMAT.md` (the type), `AGENT-SESSIONS-PROTOCOL.md` (`projects_list`), `ARCHITECTURE.md` (Sessions & Projects), a shortcut hint for New project.

**Protocol shape fixed here so the view and the worker can be built at once.** `projects_list {}` → `projects_listed {projects: [{id, name, status, priority, focus, health: {value, date}, next, due, category, roots: [{ref, path, present}], boards: [{path, open, last_activity}], sessions: n, updated}], focus_limit}`; `projects_update {id, health, text}` appends the thread entry and answers `projects_listed`; creates, edits and archives use the existing `board_create_card` / `board_update_card` / `board_move_card` with `root: global`.

**Orchestration.** Subagents work in this checkout, each on a named area, none commits another's files; every landing goes through `land.py begin/commit` with `#V3R3` in the message.
- *Wave 1, in parallel.* **A backend type** — `backend/relay_core/board.py`, `board_tools.py`, `relay-board.py check`, BOARD.md rendering, `tests/test_board*.py`: step 1 and the `projects` field with the two-project rule of step 4. **B registry** — `src/Projects.h`, `src/Projects.cpp`, `tests/projects_test.cpp`: step 2. Disjoint files.
- *Wave 2, after A and B, in parallel.* **C worker** — `backend/relay_core/agent.py` handlers for `projects_list` / `projects_update`, the join and the mtime cache, the Globals agent line in `agent_context.py`, `docs/AGENT-SESSIONS-PROTOCOL.md`: steps 3 (worker half) and 5 (backend half). **D view** — `src/ProjectsPane.h/.cpp`, the `RelayWindow` wiring that feeds it, the Focus-limit setting, New/Edit/Archive dialogs, the three filters: steps 3 (GUI half) and 5 (GUI half), built against the shape above. **E docs** — `docs/BOARD-FORMAT.md`, `docs/ARCHITECTURE.md`, the shortcut hint: step 7, written from A and B's landed code.
- *Wave 3, one session.* **F verify** — the three-fixture Xvfb check under an isolated `XDG_CONFIG_HOME`, evidence under `docs/qa_evidence/<date>-projects-feature/`, then the card to needs-verification. Budgets (step 6) are filed as their own card by F.

**Risks.** Step 4 reverses #916B's "no board outside a project" for one narrow case (owner's word given 2026-09-26). Reading every linked Board on the Projects tab must stay in the worker and cached by mtime. The registry schema bump must keep an old file readable. `ProjectsPane` and `Projects.h` are shared with #TBRH and #9FX8 sessions.

**Verify.** Board pytest for the type and `check`; `ctest -R projects` for the registry; the Projects tab under Xvfb with three fixture projects: two sharing one root and one with no root stay distinct, and a missing root is shown as missing rather than dropped.

## Execution Summary
Implemented by six subagents in three waves in workspace wt5bfbe4990132abc8, published through the queue (job 8b31fc5d8531df05).
- fab830d4 (wave 1): `project` card type (projects/, active/paused/done/archived; name, focus, health {value,date}, next, owner, due, category, portable roots). Work card `projects`, with a two-project rule on the global Board enforced by `check` and the board tools. BOARD.md Projects section, reverse link `project_work`. Registry `projects.json` schema 2 with `project` per root: schema 1 still reads, and a newer file is not overwritten.
- 5c4f7e56 (wave 2): `projects_protocol.py` (projects_list / projects_update, protocol §39). Roots are matched by linked card id, then git remote, then directory name; a root with no checkout is listed as missing. Board rollups are cached per Board in the worker. `update` thread kind. board_create/update/move honour `root: "global"`. The Board pane now reports cross-card problems. Globals agent line updated. Projects tab: Focus group (`projects/focus_limit`, default 5), status/priority/health-age/next rows, roots present/missing, New project (N, with a shortcut hint), Edit, Post update, Archive, Make a project, three filters. Docs: BOARD-FORMAT, ARCHITECTURE, RELAY.md Words.
- 03a9b050 (wave 3): Xvfb evidence. Fix: sessions in an unattached tab (project_path "") are now placed by their workspace. Post update accepts a health-only update.
- Follow-ups: budgets are #ZT58. Personal work is #B253. The canonical `.board/POLICY.md` needs regenerating (`relay-board.py policy`) once this lands, because its thread-kind list gains `update`.

## Tests
- `PYTHONPATH=$PWD/backend:$PWD python3 -m pytest tests/test_projects_protocol.py tests/test_board_projects.py tests/test_globals_protocol.py tests/test_agent_context.py tests/test_board_tools.py -q` → 406 passed.
- `tests/test_board_protocol.py`: 2 failures that also fail at HEAD. They need a workspace `.board/`, which the sparse tree excludes.
- `QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^projects'` → projects and projectspane passed (2/2), after the wave 3 fix.
- `scripts/relay-build` → relay builds.
- Xvfb, isolated XDG dirs: seven checks passed (README and screenshots 01–07 in the evidence folder).
