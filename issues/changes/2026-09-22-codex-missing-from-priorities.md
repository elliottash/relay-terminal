---
id: CDP7
type: work
status: needs-verification
assignee: codex-models-a1
labels: [bug, models]
rank: mcdp7
created: '2026-09-22'
source: 'User report in Relay; observed on sphinxpad, 2026-09-22'
links: {plans: [], commits: [3cb7ff33ba054175c93c10b699f0c08804e12f07], evidence: [docs/qa_evidence/2026-09-22-CDP7/README.md], related: [MPA2], github: null}
---
# Codex does not appear in Priorities

## Issue
i couldnt get codex to show up in the priorities tab.

## Done means
- Provider and guest model catalog changes reach an already open Priorities pane without restarting Relay.
- Searching by provider or model finds eligible Codex/OpenRouter models; intentional empty-query tier membership remains unchanged.
- Focused regressions cover refresh and search behavior, with recorded evidence.

## Plan
**Goal:** Resolve the reported missing models independently of intentional unranked search filtering.

**Findings:** `src/ModelPicker.cpp::buildTier` intentionally adds unranked models only while searching. Catalog updates flow through `src/Pane.h`, `src/RelayWindow.h`, and `src/ModelsPane.cpp`; provider eligibility and names live in `src/ModelCatalog.cpp`.

**Steps:**
1. Trace provider save, guest discovery, catalog propagation and search eligibility.
2. Add focused regressions and fix the demonstrated catalog/search defects.
3. Build targeted tests, stage UI evidence if feasible, and land with test evidence.

**Risks:** Concurrent shared checkout edits; coordinate any RelayWindow.h changes and preserve tier-list semantics.

**Verify:** Targeted model catalog/picker tests and isolated Qt UI evidence.

## Execution Summary
The shared helper-catalog omission also hides Codex even when Providers has discovered it. Fixed through the same catalog routing change as VPR7. A worker-shaped Codex row (label but no provider field) is searchable and addable to Main and High in the staged test. Empty-query unranked hiding and exclusion from Flash/Lite are intentional and unchanged. No separate Codex search/filter defect was reproduced; sphinxpad verification is still needed to establish whether this shared cause explains the reported machine.

Staged UI evidence: `docs/qa_evidence/2026-09-22-CDP7/priorities.png`.

## Tests
- `xvfb-run -a build/relay-modelspane-tests` — 21 passed.
- `QT_QPA_PLATFORM=offscreen build/relay-settings-tests providersAndPrioritiesReadTheSameServedPane everyProviderRowHasAModelsLinkIntoTheAvailableTab` — 4 passed.
- `python3 -m unittest discover -s tests -p test_openrouter_catalog.py` — 13 passed.
- `python3 -m unittest discover -s tests -p test_guest_harness_provider.py` — 68 passed.
- `ctest -R ^modelspane$` — tests/modelspane_test.cpp
- `tests/test_openrouter_catalog.py`
- `tests/test_guest_harness_provider.py`
- manual: docs/qa_evidence/2026-09-22-CDP7/README.md
