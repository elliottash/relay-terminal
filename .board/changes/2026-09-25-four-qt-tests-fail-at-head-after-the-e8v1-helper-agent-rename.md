---
id: SYTR
type: work
status: inbox
labels: [bug, tests, settings]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-25-verify-bug-cards], related: [], github: null, merged_from: [D4BJ]}
---
# Four Qt tests fail at HEAD after the E8V1 helper-agent rename

## Issue
Commit `e6febb9f` ("#E8V1 Label the one pane model and show console kind") renamed the helper-agent button to plain "Agent (Alt+Q)" (src/ModelsPane.cpp:610-614) and the jobs role label to "system-pane agent" (src/JobsTab.cpp:208) without updating the four tests that expect the old strings. They now fail at HEAD and pollute every models-card verification run:

- `tests/modelspane_test.cpp:782` — `ModelsPaneTests::theHelperIsOneRowUnderAllFiveTabsAndBuildsOneConsole` (25 passed, 1 failed)
- `tests/settingspane_test.cpp:1607` — `SettingsPaneTests::theHelperConsoleFollowsTheModeAndAsksAsTheOptionsPane` (51 passed, 1 failed)
- `tests/jobstab_test.cpp:181` — jobs under "main" expected `["agent turns","subagents","helper agent"]`, actual `"system-pane agent"` (23 passed, 1 failed)
- `tests/conversations_test.cpp:2439` — `ConversationsTest::theHelperConsoleSitsAtTheBottomCollapsedAndAsksAsTheSessionsPane` (59 passed, 1 failed)

Found during verification of #HJ1T/#N4PW/#4BPE; commands and full lines in the evidence file. The failures are why those cards' suites show 1 failed each.

## Done means
The four tests expect the strings the shipped UI actually shows (or the rename is reverted), so `relay-modelspane-tests`, `relay-settings-tests`, `relay-jobstab-tests` and `ctest -R conversations` all pass at HEAD.

## Tests
`xvfb-run -a build/relay-modelspane-tests -silent`, `xvfb-run -a build/relay-settings-tests -silent`, `xvfb-run -a build/relay-jobstab-tests -silent`, `ctest --test-dir build -R "^conversations$"` — all green.

## Merged in
### #D4BJ — Half-landed rename: settings/modelspane tests expect "Helper Agent (Alt+Q)" but SettingsPane.cpp says "Agent (Alt+Q)" (2026-09-25)

Merged from `.board/changes/2026-09-25-half-landed-rename-settings-modelspane-tests-exp.md` (discussing): Same fault filed twice within the hour: the half-landed "Helper Agent"→"Agent" rename from #E8V1 (commit e6febb9f). SYTR names all four stale Qt tests and files; D4BJ was the C1/C2 agents' independent sightings of two of them.

#### Issue
On clean main, ctest subtests `theHelperConsoleFollowsTheModeAndAsksAsTheOptionsPane` (in `settings` and `modelspane` suites) expect the literal "Helper Agent (Alt+Q)" but src/SettingsPane.cpp:1611 at HEAD produces "Agent (Alt+Q)": a rename of the helper console landed in the tests but not the source, or vice versa. Measured at commit 2db9664^ (pristine tip before C1) in a land.py verify slot by the C1 agent; C1 did not touch these files.

> (discovered during #WBFM track C1 verification — subagent report, not a direct user message)
> — elliott · [session:6173581165764472b6c340786cfdcc26](relay://session/6173581165764472b6c340786cfdcc26) · 2026-09-25
