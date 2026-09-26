---
id: D4BJ
type: work
status: dropped
labels: [bug, settings, tests]
discovered_from: WBFM
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [], github: null, merged_into: SYTR}
---
# Half-landed rename: settings/modelspane tests expect "Helper Agent (Alt+Q)" but SettingsPane.cpp says "Agent (Alt+Q)"

## Issue
On clean main, ctest subtests `theHelperConsoleFollowsTheModeAndAsksAsTheOptionsPane` (in `settings` and `modelspane` suites) expect the literal "Helper Agent (Alt+Q)" but src/SettingsPane.cpp:1611 at HEAD produces "Agent (Alt+Q)": a rename of the helper console landed in the tests but not the source, or vice versa. Measured at commit 2db9664^ (pristine tip before C1) in a land.py verify slot by the C1 agent; C1 did not touch these files.

> (discovered during #WBFM track C1 verification — subagent report, not a direct user message)
> — elliott · [session:6173581165764472b6c340786cfdcc26](relay://session/6173581165764472b6c340786cfdcc26) · 2026-09-25

## Resolution
Merged into [#SYTR](../2026-09-25-four-qt-tests-fail-at-head-after-the-e8v1-helper-agent-rename.md) on 2026-09-25: Same fault filed twice within the hour: the half-landed "Helper Agent"→"Agent" rename from #E8V1 (commit e6febb9f). SYTR names all four stale Qt tests and files; D4BJ was the C1/C2 agents' independent sightings of two of them.

Nothing was thrown away: the text above is also kept on #SYTR under `## Merged in`, and this card stays here so `#D4BJ` keeps resolving.
