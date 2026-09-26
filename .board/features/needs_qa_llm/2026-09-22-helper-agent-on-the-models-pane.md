---
id: MH7P
type: work
status: needs-qa-llm
labels: [feature, models]
assignee: claude-code
rank: m
created: '2026-09-22'
source: 'Owner request relayed to a Claude Code subagent (a1) in a Relay pane, 2026-09-22'
links: {plans: [], commits: [3c6edc69], evidence: [docs/qa_evidence/2026-09-22-models-helper, docs/qa_evidence/2026-09-25-verify-MH7P], related: [MDL1, AGNT, FEJQ], github: null}
---
# Helper agent on the Models pane

## Issue
there needs to be a helper agent on the model page

## Done means
- The Models pane (Ctrl+Shift+M) has the Options/Sessions helper, done the same way: one collapsed
  "Helper Agent (Alt+Q)" row at the bottom right, under all four tabs (providers, available,
  priorities, jobs). It expands into an agent console that is built on the first expand and folds
  back with ⌄.
- Alt+Q with the Models pane focused opens it.
- The embedded providers `SettingsPane` shows no second row.
- The worker accepts the `models` context and gives it a brief about the four tabs. The
  `screen` line says which tab is showing, the filter text, the pane being served, and the class
  (priorities) or job (jobs) in focus.

## Tests
- `tests/modelspane_test.cpp` `theHelperIsOneRowUnderAllFourTabsAndBuildsOneConsole`: with a
  stub `onCreateConsole`, one visible row on every tab, no row in the providers `SettingsPane`,
  exactly one console built across a click and a second `focusHelper()`, spec name/surface/brief
  `models`, persist `helper`/tab id, screen contents, and the fold. With no factory, no row.
- `tests/test_agent_context.py` `test_the_models_pane_has_a_brief_of_its_own`, plus `models` in
  the console-defaults loop.
- Run: `scripts/relay-build --target relay-modelspane-tests && xvfb-run -a ctest --test-dir build
  -R '^modelspane$' --output-on-failure` (22/22), and
  `python3 -m unittest tests.test_agent_context` (24/24).

## QA checklist
Verified 2026-09-25 by a verifying session at rev `2db966438ab8bc61e0f9f1ff89a51853286ad0f8` (tests from a clean worktree; live drive on the shared-tree binary whose ModelsPane.cpp matches HEAD). Evidence: `docs/qa_evidence/2026-09-25-verify-MH7P/`. Note: #E8V1 (`e6febb9f`) later renamed the row ("Helper Agent (Alt+Q)" → "Agent (Alt+Q)", head "Models helper" → "Models agent") and #N4PW grew the pane from four tabs to five; the card's wording predates both.

**Done means, item by item:**
- One collapsed helper row at the bottom right under the tabs, expanding into a console — **passed** (`01-models-pane.png`: collapsed "⌄ Agent (Alt+Q)"; `02`/`04-altq-console.png`: expanded "Ask the Models agent…" composer). The fold-back `⌄` exists in code (`src/ModelsPane.cpp:565-578`) but was **not exercised live** this pass — repeated coordinate clicks missed the small button, and the unit test that clicks it (`theHelperIsOneRowUnderAllFiveTabsAndBuildsOneConsole`) aborts earlier on #E8V1's renamed strings (see #SYTR). Flagging as missing live evidence, low risk.
- Alt+Q with the Models pane focused opens it — **passed** (live).
- Embedded providers `SettingsPane` shows no second row — **passed** (live: one helper row per tab incl. Sources; code keeps `providers()->onCreateConsole` unset).
- Worker accepts the `models` context with the tab/filter/focus brief — **passed by test evidence** (`tests.test_agent_context` OK incl. `test_the_models_pane_has_a_brief_of_its_own`); the C++ spec/screen assertions sit behind the stale string in #SYTR.

**Tests, line by line:**
- `relay-modelspane-tests` / `ctest -R '^modelspane$'` — **failed as written**: 25 passed, 1 failed; the failure is this card's own named test hitting #E8V1's renamed strings (`tests/modelspane_test.cpp:782`), i.e. the test is stale, the feature is present (filed as #SYTR with the other three).
- `python3 -m unittest tests.test_agent_context` — **passed** (OK; card recorded 24/24).

Unresolved: the live fold-back click (missing live evidence above); everything else confirmed. The stale tests are #SYTR's.

Reviewed 2026-09-25 by the verifying session (qa-verify-MH7P), rev `2db96643`.
