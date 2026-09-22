---
id: MPA2
type: work
status: discussing
labels: [bug, models]
assignee: codex
waiting_on: owner
rank: mmpa2
created: '2026-09-22'
source: 'Codex in Relay, 2026-09-22'
links: {plans: [], commits: [], evidence: [], related: [4BPE], github: null}
---
# Available models missing from Priorities on sphinxpad

## Issue
there is an issue with the model priority pane, some of the models in available are not showing up there (observed on sphinxpad)

## Planning notes
At 1f0546d8, ModelPicker::buildTier in src/ModelPicker.cpp draws only stored tierList entries for an empty query. Search adds eligible allUsable catalog entries not already in that section. Available uses curatable instead. Thus availability alone does not make an unranked model visible in Priorities before searching. Tests/modelpicker_test.cpp explicitly checks that section behavior. Need the affected model names and whether searching finds them to distinguish this discoverability issue from a catalog/filter defect. SSH to sphinxpad returned No route to host during investigation.

## Done means
- Reproduce the reported missing models or explain exactly which list membership causes their absence.
- If defective, correct the filtering or discovery behavior with a focused regression test.
