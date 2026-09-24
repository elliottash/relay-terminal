---
id: QHEE
type: work
status: needs-verification
labels: [feature, models, ui]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 31883d9c-ec40-4d0e-a526-58a4648ecccd
rank: zzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: Owner in Relay guest session, 2026-09-23
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-QHEE/], related: [00G1, 24XJ], github: null}
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

### Check 2026-09-23 23:23
- passed · ctest:modelspane — ctest -R modelspane passed for this revision on spark-dcc9, 2026-09-24T03:23:08Z
- passed · ctest:settings — ctest -R settings passed for this revision on spark-dcc9, 2026-09-24T03:23:08Z
- not-applicable · manual:docs/qa_evidence/2026-09-23-QHEE/ — manual evidence, recorded by hand: docs/qa_evidence/2026-09-23-QHEE/
- notice · ctest:modelspane — ctest -R modelspane is slow: p95 1.86 s, p50 1.60 s
history: thread
