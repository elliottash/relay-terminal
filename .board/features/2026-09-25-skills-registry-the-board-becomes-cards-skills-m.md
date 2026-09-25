---
id: 9FX8
type: work
status: needs-verification
labels: [feature, switchboard, skills, board, qa]
component: [gui, worker, skills]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: c522363d-fa8e-4db1-afcd-a6e58451cd14
rank: zzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
verify: {artifact: visual, primary: script, also: [ai-visual, person], human: required, criteria: 'the Board''s three tabs read as one surface; a skill page answers version, profile, cases and staleness at a glance without a QA plan in view', sign_off: none, effort: high, stakes: rework, blast: capability}
source: 'owner, Relay conversation, 2026-09-24 and 2026-09-25; the Skills registry phase of #1QKM'
links: {commits: [e5b1a648d096, d138dc638e9e, b48afea250ef, 6feff54f7f97, 488ff0c853f7, 7ea7d8f05fe5, cd1b0fedcde0, ebcd0f879450], evidence: [docs/qa_evidence/2026-09-25-9FX8-skills-tab/], github: null, plans: [], related: [1QKM, SZ1H, HS7V, MSJ0, 95VZ, Y2MP, P7SJ, GSK7]}
---
# Skills registry: the Board becomes Cards | Skills | Memories, Globals gets global skills, and a skill page shows version, profile, cases and staleness

## Issue
the board should become 3 tabs, cards, skills, memories [...] globals also needs global skills [...] globals is showing local memories. local memories go in teh board [...] analyze these issues and add comprehensive plans to cards. clarify anything with me with questions.

## Plan
**Goal.** Skills become the Board's second object with a surface of their own (#1QKM §5, the last unbuilt phase of the #BX7B sequence): the project Board is three top-level tabs — **Cards** (today's list, unchanged), **Skills** (the servers this workspace can route to, with their registry row) and **Memories** (this board's `type: memory` cards) — and Globals gains a **Skills** section for the global ones. A skill page shows what the registry holds per server: version and provenance, the declared profile, the case history and pass rate, last verified, and a stale flag. All of it is computed from what already exists (`SkillIndex`, the case ledger); nothing is typed into prose.

**Findings.**
- `backend/relay_core/skills.py`: `SkillIndex` reads sources in order (project `.relay/skills` → global Relay → workspace/home `.claude` `.codex` `.warp` → Warp's and Claude's bundles → Relay's bundled dir; #HS7V made `.relay` canonical, which settles #1QKM open question 4), parses `profile:` (`parse_profile`, `PROFILE_KEYS`, warnings kept), and `skills_list` already carries four ledger statistics per profiled skill (`cases.stats`: `cases`, `last_served`, `pass_rate_30`, `stale`). `cases.skill_version` is the sha256 of `SKILL.md`. Source label per skill exists (#HS7V "visible provenance labels").
- `src/SkillsDialog.{h,cpp}`: the existing manager (list, exclusions, Refine, import from a git repository with preview, Check updates; protocol §11 `skills_*` events). It is a dialog off the pane, not a registry: no version, profile, cases or staleness, and no per-skill page.
- `src/BoardPane.{h,cpp}`: one list page, no tabs (owner decision 2026-09-18: bug/feature are labels, the filter box slices). The 2026-09-24 decision ("3 tabs, cards, skills, memories") is about *objects*, not categories: `board.yaml` `tabs:` (features, bugs, design…) stay as the Cards list's categories; the three new tabs sit above them.
- `src/GlobalsPane.cpp`: sections User memory / Aliases / Instructions / All records / Suggestions (`kUserMemory…kSuggestions`); no skills. Memory boundary per the owner: project memories on the Board, user memories in Globals (#Y2MP, #P7SJ).
- `issues/memory/` holds `type: memory` cards with `MEMORY_STATUS_FOLDER` (active / archive / suggestions / rejected) — the Memories tab is a view over these, no new store.
- #SZ1H owns catalogue hygiene (`requires`, identity by content hash, hiding by tool availability, catalogue line length). This card consumes its output (a `requires` status and a "same as" mark on the row) and does not redo it.
- `board_tools._server_for_card` / `record_case` name the server a case row is about; there is no field on a *card* naming the server it builds or fixes (added by #G9ZD as `server:`; the skill page lists those cards when it exists, and falls back to ledger rows' `card` ids until then).

**Steps.**
1. Worker: a `skills_registry` request (board worker, beside `skills_list`) returning one row per skill visible from this workspace: `id`, `name`, `source` (project / relay-global / claude / codex / warp / bundled), `path`, `version` (short sha256), `profile` + `profile_warnings`, `excluded`, `stats` (the four from `cases.stats`), `last_verified` (newest passing row's `when`), `rot` and a one-line `stale_reason`, `cases` (last 10 ledger rows for the server, confidential rows as ids only, none off-workspace — the rule `BoardTools.ledger` already applies), `cards` (ids of cards whose rows name this server; `server:` field once #G9ZD lands), and `changelog` (last 5 `git log` lines of the manifest when it sits in a repository, else empty). Tests in `tests/test_skills.py` + `tests/test_board_tools.py`: sources and order, version, stats, confidential rows, excluded flag.
2. GUI, Board: a three-way segmented control in the pane header — Cards | Skills | Memories — persisted per board in the pane's settings. Cards is the existing page untouched (its `board.yaml` categories, sections, filter, detail view). Skills is a list (name, source, version, cases, pass rate, last verified, stale) with the same filter box; a row opens the skill page: trigger text, the profile as a one-line strip (artifact, primary rung, human, effort, stakes, rot, confidential, money — the same words the Verify strip uses), version and provenance (path, source, hash, changelog), cases (date, served by, signal, verdict, cost; ids only when confidential), linked cards, and actions: **Load** (prefills the console prompt with the skill's name), **Exclude / Include** (today's exclusion list), **Refine** and **Open file** (from SkillsDialog), **Try it** when the skill folder holds a `Try it` case (#SZ1H). Memories is this board's memory cards (active first, retired under a fold) with Globals' record view reused for the detail (edit, retire, pin, paths).
3. GUI, Globals: a **Skills** section between Aliases and Instructions listing global skills only (every non-project source), same list and page as step 2, plus SkillsDialog's import-from-repository and Check-updates actions moved into the section's toolbar. `SkillsDialog` stays reachable from Actions until parity is verified, then a follow-up card retires it.
4. Staleness: the row and page show `stale` from `cases.stats` with its reason ("last passed 2026-08-01; rot high, so 7 days"); the page's **Re-verify** action opens a console with the skill loaded and the prompt "serve one case of <skill> and record it with board_case" — agent work, the person sees the flag change. A skill with no rows shows "no cases yet", never stale (the ledger's rule).
5. Routing stays with the agent: the Skills tab is the person's view of what the agent can route to; the row's `regularity` and trigger line are what the agent already reads. No new router.
6. Docs and protocol: `docs/AGENT-SESSIONS-PROTOCOL.md` gains the `skills_registry` request/event shape under §11; `docs/BOARD-DESIGN.md` §4 records the three tabs and the reconciliation with the 2026-09-18 no-tabs decision; `docs/ARCHITECTURE.md` names the Skills tab as the registry from #1QKM §5.
7. Tests and evidence: `tests/boardpane_test.cpp` gains a case that feeds a `skills_registry` event and checks the Skills tab draws the rows and the page opens; `tests/globalspane_test.cpp` (or the nearest) the Globals section; screenshots under `docs/qa_evidence/<date>-9FX8-skills-tab/` from the offscreen platform, as #BX7B did.

**Risks.**
- **UI needs eyes**: three tabs change the Board's first impression; the Cards tab must look exactly as before or the 2026-09-18 decision is undone by accident.
- **Which index**: the Board worker's `SkillIndex` must read the same directories as a console pane in this workspace (`skills.from_request`, `default_directories(workspace)`), or the tab lists skills the agent cannot load. Step 1 builds the index from the board's workspace path, the same call.
- **Memories tab vs Globals › All records**: both would show project memories today; the tab takes project scope and Globals keeps user scope (the owner's boundary), and Globals' "All records" is renamed or filtered to say so — decision below.
- **Volume**: 61 skills on the owner's machine before #SZ1H hides the unrunnable third; the list needs the filter box from day one.
- **Owner decisions** (questions in the thread): categories under Cards, which skills the project tab lists, SkillsDialog's fate, what a case row shows.

**Verify.** `PYTHONPATH=backend python3 -m unittest tests.test_skills tests.test_board_tools.RegistryTests`; `ctest --test-dir build -R '^(boardpane|globalspane)$'`; the screenshots; then a person opens a project with skills, switches the three tabs, opens one skill page and one memory, and confirms the Cards tab is unchanged (`human: required`, criteria in `verify`).
**Amendment (owner decisions, 2026-09-25).** Step 2's Skills tab lists **project skills only** (decision 2): the registry request still returns every visible skill with its `source` (Globals' Skills section needs the global ones), and the Board's tab filters to project sources — `.relay/skills` and the workspace's `.claude`/`.codex`/`.warp` — with the source label kept on the row. Cards is the tab the Board opens on (decision 1), and a card pane (#P2W8) never carries these tabs.

## Done means
- The Board pane shows Cards | Skills | Memories; Cards is pixel-for-pixel today's list page, is the tab the pane opens on, and the choice persists per board.
- The Skills tab lists the **project** skills (`.relay/skills`, workspace `.claude`/`.codex`/`.warp`) with source, version, cases, pass rate, last verified and stale; a row opens a page with the profile strip, provenance, the last ten cases (ids only when confidential) and the linked cards; Load, Exclude, Refine, Open file and Re-verify work. Global skills are not on the Board tab — Globals' Skills section lists them with the same list and page plus import and update actions (decision 2).
- The Memories tab shows this board's memory cards and Globals no longer shows them.
- Failure looks like: a project skill the console can load that the tab does not list, a global skill appearing on the Board tab, a stale flag that disagrees with `cases.stats`, or a QA plan (the verify block) drawn on a skill page.

## Execution Summary
- **Step 1** (e5b1a648): the `skills_registry` worker request, one row per visible skill with
  version, profile, `cases.stats`, `last_verified`, `stale_reason`, the last ten cases, linked
  cards, changelog and the `project` flag.
- **Step 2** (b48afea2): the Board's Cards | Skills | Memories tabs. Skills lists project rows,
  and each skill has its own page. Memories shows Expired first, then Active and Suggestions,
  with Retired and Rejected folded.
- **Step 3** (7ea7d8f05fe5): the skill list and skill page became one widget,
  `skills::SkillRegistryView` (`src/SkillRegistryView.*`, library `relay-skillregistry`). The
  Board's Skills tab shows its project half. **Globals › Skills** (between Aliases and
  Instructions; stable id 5, the existing ids unchanged) shows the global half. Its toolbar has
  SkillsDialog's Import from repository… and Check updates, whose dialogs SkillsDialog now calls
  from the same file. SkillsDialog stays reachable from Actions; #JVEJ retires it (decision 3).
  Globals lists no project-scoped memory: "All records" is now "All global records", and its
  intro counts the project memories left out and points to the Board's Memories tab.
- **Step 4** (7ea7d8f05fe5): the row shows "stale" with the reason in its tooltip, and the page
  shows it inline. A skill with no rows shows "no cases yet", never stale. The page's
  **Re-verify** drafts `/skill <name> serve one case of <name> and record it with board_case`
  into a console and never sends: on the Board, its own console; in Globals, the Sessions pane's.
  The row's Source column carries the short version.
- Fixes carried with it: the Linked chips drew twice when the page redrew before a new row was
  shown; provenance printed "sha256 sha256:…"; a `skills_registry` with `workspace: ""` (a tab
  with no project) failed with "workspace must be an existing directory".
- cd1b0fedcde0 took back another pane's test (#E0Y0) that 7ea7d8f05fe5 had landed by mistake
  with this card's screenshot hooks. ebcd0f879450 landed the hooks alone.

## Tests
- `PYTHONPATH=backend python3 -m unittest tests.test_skills.RegistryTests tests.test_board_tools.RegistryTests`
  → 6 OK (adds `test_globals_request_with_an_empty_workspace_is_answered`).
- `relay-globalspane-tests` on the landed tree → 15 passed, including
  `skillsSectionListsGlobalSkillsOnlyAndOpensAPage` (mixed payload → only `project: false` rows;
  a page opens; stale reason; no cases yet; Re-verify drafts and sends nothing; Refine; errors)
  and `projectMemoriesAreNotListed`.
- `relay-boardpane-tests` → 19 passed on the step-3 tree, including
  `theSkillsTabListsProjectSkillsAndOpensAPage` and
  `theMemoriesTabShowsExpiredFirstAndOpensTheCard`. On main since baaefc87 (#FYEY) the suite
  aborts earlier, in `longFindingsRemainReadableAndScrollable` (malloc_consolidate). 579c7d5c
  passes and baaefc87 fails, so the abort is not this card's. The two tab tests pass when run
  alone on ebcd0f879450.
- Evidence: `docs/qa_evidence/2026-09-25-9FX8-skills-tab/` (five offscreen screenshots,
  `tests.txt`, `README.md`).

## Human QA
- Open a project that has `.relay/skills`, switch Cards → Skills → Memories → Cards. Is the Cards
  tab exactly the list page you had before, and do the three tabs read as one surface?
- On Board › Skills, open a skill page. Can you tell version, profile, cases and staleness at a
  glance, with no QA plan drawn on it? Is a global skill absent from the list?
- Open Globals (Ctrl+Shift+G) › Skills. Are only global skills listed (no project ones)? Do
  Import from repository… and Check updates behave as they did in the Skills dialog? Does
  Re-verify put the draft in the Sessions console without sending it?
- Globals › All global records and User memory: are this project's memories gone, and your user
  memories still there?

## Decisions
Owner, 2026-09-25 (answers to the four questions in the thread):
1. **Tabs are objects, Cards is the default.** "the current board goes into cards, yes, which i think is the default open pane. no tabs for a card pane (cf #P2W8)" — today's list is the Cards tab and opens first; the `board.yaml` categories stay inside Cards; a card pane (#P2W8) never carries these tabs.
2. **The Skills tab lists project skills only.** "project skills. global skills are in the global manager." — project sources (`.relay/skills`, workspace `.claude`/`.codex`/`.warp`) on the Board; every non-project source lives in Globals' Skills section. Step 2's list and Done means are read through this decision; the `skills_registry` request still returns every visible skill with its `source` so both surfaces share it.
3. **SkillsDialog retires into Globals › Skills** by a follow-up card once the section has the import-from-repository and Check-updates actions (step 3 as written).
4. **Case rows show their `input` reference** (a path) when the pane is on the board's own workspace, ids only otherwise (the ledger's existing rule).
