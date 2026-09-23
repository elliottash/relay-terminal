---
id: MPA2
type: work
status: needs-verification
labels: [bug, models]
assignee: codex
rank: mmpa2
created: '2026-09-22'
source: 'Codex in Relay, 2026-09-22'
links: {plans: [], commits: [3cb7ff33ba054175c93c10b699f0c08804e12f07, 8d03da032c8c58fbf48d4c99b866d25455f1fa0b], evidence: [docs/qa_evidence/2026-09-23-MPA2/], related: [4BPE, VPR7, CDP7], github: null}
---
# Available models missing from Priorities on sphinxpad

## Issue
there is an issue with the model priority pane, some of the models in available are not showing up there (observed on sphinxpad)

## Planning notes
At 1f0546d8, ModelPicker::buildTier in src/ModelPicker.cpp draws only stored tierList entries for an empty query. Search adds eligible allUsable catalog entries not already in that section. Available uses curatable instead. Thus availability alone does not make an unranked model visible in Priorities before searching. Tests/modelpicker_test.cpp explicitly checks that section behavior. Need the affected model names and whether searching finds them to distinguish this discoverability issue from a catalog/filter defect. SSH to sphinxpad returned No route to host during investigation.

## Done means
- Reproduce the reported missing models or explain exactly which list membership causes their absence.
- If defective, correct the filtering or discovery behavior with a focused regression test.

## Plan
Trace the specific OpenRouter refresh and Codex reports linked in this thread. Review fixes already landed for #VPR7/#CDP7, rebuild model tests, simulate availability toggles and list add/remove/undo with isolated settings, and rerun full-app worker-event staging. Avoid duplicating completed fixes or changing intentional priority-list membership. Record machine verification limits if sphinxpad remains unreachable.

## Execution Summary
Reviewed already-landed fixes for #VPR7 and #CDP7 rather than duplicating changes. The Models pane now uses the served pane consistently and falls back to the same helper catalog as Providers. Guest catalogs above six models no longer default to unavailable. Rebuilt and reran modelcatalog/modelpicker/modelspane tests, including availability toggle, add, remove, reorder, undo, and late provider refresh. Full-app worker-event staging passed with an open OpenRouter search gaining a model and seven Codex models available. No additional defect reproduced. Sphinxpad SSH remains unreachable; these later fixes have not been installed there by this session.

## Tests
- `ctest -R ^modelcatalog$`
- `ctest -R ^modelpicker$`
- `ctest -R ^modelspane$`
- `tests/test_openrouter_catalog.py`
- `tests/test_guest_harness_provider.py`
- manual: docs/qa_evidence/2026-09-23-MPA2/README.md
