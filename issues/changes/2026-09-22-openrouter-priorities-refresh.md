---
id: VPR7
type: work
status: needs-verification
assignee: codex-models-a1
labels: [bug, models]
rank: mopr7
created: '2026-09-22'
source: 'User report in Relay; observed on sphinxpad, 2026-09-22'
links: {plans: [], commits: [3cb7ff33ba054175c93c10b699f0c08804e12f07, 8d03da032c8c58fbf48d4c99b866d25455f1fa0b, 786393012dce25bca3eae91c372e0c3cac6e753f], evidence: [docs/qa_evidence/2026-09-22-VPR7/README.md], related: [MPA2], github: null}
---
# New OpenRouter models require a restart to appear in Priorities

## Issue
when i added a provider, i had to researt relay to see the model (openrouter) in the priorities tab.

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
Full-app worker-pipe regression now proves refresh: a helper-only late key_stored/presets event updates an already open OpenRouter search without restart. Identical driver fails on the pre-fix binary. See live/baseline screenshots and stage.py in the evidence folder.

Provider settings could use m_active while Priorities continued serving another pane. Also Providers used helper presets before the served worker answered, while Priorities read only the empty served catalog. modelsSection now resolves the served target; applyModelsTarget falls back to the same helper catalog on open and refresh; the provider models link preserves that target.

Staged UI evidence: `docs/qa_evidence/2026-09-22-VPR7/priorities.png`.

## Tests
- `ctest -R ^modelpicker$` — tests/modelpicker_test.cpp
- `ctest -R ^modelcatalog$` — tests/modelcatalog_test.cpp
- `python3 docs/qa_evidence/2026-09-22-VPR7/stage.py` — full-app late helper events and Codex availability passed; pre-fix binary failed as expected.
- `xvfb-run -a build/relay-modelspane-tests` — 21 passed.
- `python3 -m unittest discover -s tests -p test_openrouter_catalog.py` — 13 passed.
- `python3 -m unittest discover -s tests -p test_guest_harness_provider.py` — 68 passed.
- `ctest -R ^modelspane$` — tests/modelspane_test.cpp
- `tests/test_openrouter_catalog.py`
- `tests/test_guest_harness_provider.py`
- manual: docs/qa_evidence/2026-09-22-VPR7/README.md
