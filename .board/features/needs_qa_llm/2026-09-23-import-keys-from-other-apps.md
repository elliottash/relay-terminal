---
id: K7MP
type: work
status: needs-qa-llm
labels: [feature, models]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: Codex in Relay, 2026-09-23
links: {plans: [], commits: [4de23c464996f2ab39577772930cf9e90231de82], evidence: [docs/qa_evidence/2026-09-23-import-keys/, docs/qa_evidence/2026-09-25-verify-K7MP/], related: [P3KD], github: null}
---
# Import provider keys from other apps

## Issue
the warp one, maybe we should just have an import keys button at the bottom -- you might want to import from opencode or claude for example. are there other apps that save keys we could import from?

## Done means
One Import keys control sits below the provider groups and lets the user choose Warp, OpenCode, or Claude Code/Codex.
OpenCode imports only supported provider API keys from its documented local credential file. OAuth and unknown providers are skipped. Import results do not reveal keys.
The existing Warp and Claude Code/Codex imports remain usable, and the Providers page refreshes after a successful import.

## Plan
**Goal:** make key import one action at the bottom of Providers.

**Findings:** `RelayWindow.h` has a Warp-only row; `keystore.py` already imports Warp and plain API keys from Claude Code/Codex. OpenCode documents `auth.json` for API credentials.

**Steps:** 1. Add a source chooser at the bottom of Providers. 2. Add a constrained OpenCode API key importer and worker event. 3. Present import results and refresh provider status through pane and helper workers. 4. Verify with fixture tests, build, and an isolated UI capture.

**Risks:** source files may include OAuth credentials or unknown provider IDs; those are skipped. Existing Relay keys should not be overwritten silently.

**Verify:** keystore fixture test, Models UI test, app build, isolated screenshot.

## Execution Summary
The Providers tab now has one Import keys row below all three provider groups. Its chooser offers Warp, OpenCode, and Claude Code/Codex API keys. The worker imports OpenCode `type: api` records for matching provider IDs from `auth.json`, skips OAuth, unknown providers, and existing Relay keys, and returns only names/counts. The existing Warp and agent-tool importers also preserve existing Relay keys. Pane and helper-worker routes show import results and refresh presets.

![Import action below provider groups](docs/qa_evidence/2026-09-23-import-keys/01-import-at-bottom.png)

![Import sources](docs/qa_evidence/2026-09-23-import-keys/03-import-sources.png)

## Tests
`python3 -m unittest tests.test_keystore` — 27 passed.
`QT_QPA_PLATFORM=offscreen ./build/relay-settings-tests` — 48 passed.
`scripts/relay-build --target relay-settings-tests relay` — app built.
`docs/qa_evidence/2026-09-23-import-keys/drive.sh` — isolated Xvfb capture of Providers and source chooser.

## QA checklist
Verified 2026-09-25 by a verifying session at rev `2db966438ab8bc61e0f9f1ff89a51853286ad0f8` (clean-worktree sources). Evidence: `docs/qa_evidence/2026-09-25-verify-K7MP/`.

**Done means, item by item:**
- One Import keys control below the provider groups, choosing Warp / OpenCode / Claude Code–Codex — **passed** (live: `01-sources-bottom.png` row + `05-import-dialog.png` "Import API keys from" dialog; code `RelayWindowModels.cpp:490-510`).
- OpenCode imports only supported API keys; OAuth/unknown skipped; results never reveal keys — **passed by test evidence** (`tests.test_keystore` OK incl. the opencode fixture cases; `keystore.py:306`).
- Existing Warp and Claude Code/Codex imports remain usable; Providers refreshes after import — **passed by code + tests** (import_warp/import_agent_tools kinds unchanged; keystore suite green). The post-import refresh path was not exercised live (no real keys in the QA profile) — covered by the implementer's drive and suites.

**Tests, line by line:**
- `python3 -m unittest tests.test_keystore` — **passed** with `PYTHONPATH=backend` (OK). NB: without it the module fails to import (`relay_core` not on the path) — environmental quirk, not a defect.
- `relay-settings-tests` — **passed with a note**: 51/1; the 1 is the #E8V1 stale string (#SYTR), unrelated.
- Build — **passed** (clean-worktree builds of the test targets this sweep).
- Implementer's capture `docs/qa_evidence/2026-09-23-import-keys/` — **present**.

Unresolved: nothing for this card.

Reviewed 2026-09-25 by the verifying session (qa-verify-K7MP), rev `2db96643`.
