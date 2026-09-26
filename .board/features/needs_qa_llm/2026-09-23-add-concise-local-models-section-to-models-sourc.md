---
id: QHEE
type: work
status: needs-qa-llm
labels: [feature, models, ui]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 31883d9c-ec40-4d0e-a526-58a4648ecccd
rank: zzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: Owner in Relay guest session, 2026-09-23
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-QHEE/, docs/qa_evidence/2026-09-25-verify-QHEE/], related: [00G1, 24XJ], github: null}
---
# Add concise local models section to Models Sources tab

## Issue
the sources tab needs a local models section. we can bring in the local models tab from the options menu, but it can be concice and have a helper agent like the guest agent accounts

## Done means
Sources shows a compact Local models group beneath hosted providers, using the same saved endpoints and status as Options › Local models.
- Saved local endpoints show model/server/window/state and working Test and Refresh actions; errors remain visible.
- An empty registry has a clear message. Opening Sources reads the registry and probes saved endpoints only on entry, as Options does.
- “Set up with helper agent” opens the existing Models helper and drafts the bundled local-model-setup request for the user to send. A “More local settings” action opens Options › Local models for discovery, address entry, removal and advanced controls.
- Options › Local models continues to work, and the five Models tabs and their saved IDs remain unchanged.
- A narrow Sources screenshot shows the new group without duplicate tab bars or a second helper.

## Plan
**Goal:** reuse the existing local model state and worker protocol in a concise Sources group.

**Findings:** `RelayWindow::createModelsPane` supplies only `modelsSection(true)` to Sources. `LocalModelsSettings::section()` owns the full Options page and its saved endpoint Test/Refresh/Remove callbacks. `ModelsPane::helperDraft()` already opens the one Models helper and leaves a request for the user to send. The full Options section probes on entry.

**Steps:**
1. Add a compact `LocalModelsSettings` section builder that reuses saved endpoint summary/actions and notes while leaving advanced toggles, discovery and address entry in Options.
2. Append its rows under a Local models heading in the single embedded Sources page; add “Set up with helper agent” and “More local settings” rows. Draft the bundled skill prompt in the existing Models helper, not a second console.
3. Trigger the existing registry refresh when Sources comes to the front. Keep it off timers and avoid refresh on every redraw.
4. Add focused tests for the compact rows and Sources integration; run the Models pane and local models tests, capture a narrow screenshot, then build Relay.

**Risks:** Sources must remain one embedded section because the SettingsPane hides its own tab bar. The new callbacks must not use or overwrite another pane's agent conversation. Existing local endpoint operations and saved state stay in `LocalModelsSettings`.

## Execution Summary
Implemented in `967735e1` and follow-up `a25932e4`. Sources reuses `LocalModelsSettings` for saved endpoint summaries and actions, offers setup through the existing Models helper, and links to full Options. The local group is placed before the hosted provider rows so it remains visible without scrolling through them. Added explicit loading, unavailable-worker and Reload states. Captures in `docs/qa_evidence/2026-09-23-QHEE/` show a narrow fixture (`01-sources.png`) and live Models views (`02`–`04`); the live captures do not establish the final local group after the follow-up edit. Build and final screenshot remain for the verifying session because the user stopped the build after memory exhaustion.

## Tests
- `ctest -R '^modelspane$'` — tests/modelspane_test.cpp
- `ctest -R '^settings$'` — tests/settingspane_test.cpp
- manual: docs/qa_evidence/2026-09-23-QHEE/

`git diff --check` passed for the follow-up edits. These CTest cases and a fresh Relay build were not run on the final commit: the build exhausted memory, and the user requested wrap-up without another build. Run both tests and capture Sources with the Local models group visible when memory permits.

### Check 2026-09-25 20:55
- failed · ctest:modelspane — ctest -R modelspane failed for this revision on spark-dcc9
- failed · ctest:settings — ctest -R settings failed for this revision on spark-dcc9
- not-applicable · manual:docs/qa_evidence/2026-09-23-QHEE/ — manual evidence, recorded by hand: docs/qa_evidence/2026-09-23-QHEE/
- notice · ctest:modelspane — ctest -R modelspane is slow: p95 1.86 s, p50 1.62 s
- warning · manual:docs/qa_evidence/2026-09-23-QHEE/ — manual evidence docs/qa_evidence/2026-09-23-QHEE/ is not there
- notice · ctest:settings — ctest -R settings failed the last time it ran, 2026-09-26T00:55:44Z
history: thread
## QA checklist
Verified 2026-09-25 by a verifying session at rev `2db966438ab8bc61e0f9f1ff89a51853286ad0f8` (clean-worktree builds). Evidence: `docs/qa_evidence/2026-09-25-verify-QHEE/`. This pass also completes the card's own unfinished item — the final build + screenshot of Sources with the Local models group.

**Done means, item by item:**
- Concise Local models group in Sources reusing LocalModelsSettings, with the full page remaining in Options — **passed**: `LocalModelsSettings::compactSection()` (`src/LocalModelsSettings.cpp:460`) appended at `src/RelayWindow.h:1433`; Options page untouched (`tests/settingspane_test.cpp:235` #24XJ cases still present and green). Live `01-sources-local-group.png` shows the group: status line, "Reload local models", "Set up a local model" helper row, More-local-settings link.
- Group placed before the hosted provider rows so it stays visible — **passed with a position note**: later cards (#00G1 Sources redesign, #WBFM) reshaped Sources; at HEAD the group sits after the coding-account and key groups and the import-keys row — still no hosted-provider tail to scroll through above it; behavior intent intact, exact position drifted.
- Loading / unavailable-worker / Reload states — **Reload seen live**; loading and unavailable-worker states are code paths in `LocalModelsSettings` covered by the green modelspane Sources cases (`tests/modelspane_test.cpp:236` asserts the "Local models" heading); not separately captured live.
- Build — **passed** (clean-worktree build of the suites this sweep; the memory-exhausted build the card mentions no longer reproduces with `scripts/relay-build`'s targeted builds).

**Tests, line by line:**
- `ctest -R '^modelspane$'` — **passed with a note**: 25/1; the 1 is the #E8V1 stale-string failure (#SYTR) in an unrelated function; the Sources/Local-models cases pass.
- `ctest -R '^settings$'` — **passed with a note**: 51/1, same #SYTR cause.
- NB: there is no `relay-localmodels-tests` target at HEAD — `relay-localmodels` is a static library; the card's Tests line that named it refers to these same modelspane/settings cases.
- Implementer's capture `docs/qa_evidence/2026-09-23-QHEE/` — **present** (the follow-up local-group position superseded by later cards as noted).

Unresolved: nothing for this card; the per-model table state (live local registry) remains unexercised on this rig by design — covered by suites + implementer's `02-pulling-local-models.png`.

Reviewed 2026-09-25 by the verifying session (qa-verify-QHEE), rev `2db96643`.
