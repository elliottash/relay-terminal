---
id: MH7P
type: work
status: needs-verification
labels: [feature, models]
assignee: claude-code
rank: m
created: '2026-09-22'
source: 'Owner request relayed to a Claude Code subagent (a1) in a Relay pane, 2026-09-22'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-models-helper], related: [MDL1, AGNT, FEJQ], github: null}
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
