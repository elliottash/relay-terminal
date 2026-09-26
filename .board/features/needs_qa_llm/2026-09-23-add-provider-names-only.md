---
id: 7KPN
type: work
status: needs-qa-llm
labels: [feature, models]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: Codex in Relay, 2026-09-23
links: {plans: [], commits: [07cb34a30b23a3e50da9f96212139a75575351ec, 8d9f24cf36764e54c494a2740c56b0e2cb78768a], evidence: [docs/qa_evidence/2026-09-23-provider-names/, docs/qa_evidence/2026-09-25-verify-7KPN/], related: [4BPE], github: null}
---
# Name providers, not models, in Add provider

## Issue
in the add provider modal, dont put model names, just provider names

## Done means
The Add provider choices show provider names without model names.
Providers with more than one key type remain distinguishable and open the correct key prompt.
The key prompt also names the provider without a model name.

## Plan
**Goal:** identify each provider choice without naming a model.

**Findings:** `RelayWindow.h` uses each preset's model-bearing `label` and `note` in the picker and passes `label` to the key prompt; `provider` and `plan` are separate fields.

**Steps:** 1. Render provider names and plan types in the picker. 2. Pass the provider name to the key prompt. 3. Build and inspect the modal in an isolated app window.

**Risks:** Kimi and z.ai have multiple key types, so their plan names must remain visible.

**Verify:** app build and isolated UI capture of the Add provider picker.

## Execution Summary
The Add provider preview and picker use company names from the provider field, without the model-bearing preset label or note. The plan column still distinguishes Kimi and z.ai key types. The selected provider name also appears in the API key prompt. See `docs/qa_evidence/2026-09-23-provider-names/01-add-provider.png`.

## Tests
`scripts/relay-build --target relay` — passed after final edit.
`docs/qa_evidence/2026-09-23-provider-names/01-add-provider.png` — isolated Xvfb capture shows provider names and separate plans, without model names.
`docs/qa_evidence/2026-09-23-provider-names/02-key-prompt.png` — selecting Kimi's pay-as-you-go row opens a prompt headed “Key for kimi.”

## QA checklist
Verified 2026-09-25 by a verifying session at rev `2db966438ab8bc61e0f9f1ff89a51853286ad0f8`. Evidence: `docs/qa_evidence/2026-09-25-verify-7KPN/`. The landed code survives verbatim at HEAD in `src/RelayWindowModels.cpp:460-487` (moved out of RelayWindow.h by #243T's split).

**Done means, item by item:**
- The picker lists companies, not models (select Kimi → "kimi pay-as-you-go", not "Kimi K2.5…") — **passed**: `04-picker.png` shows kimi/zai/minimax/openai/anthropic/google/deepseek with plan columns only, zero model names.
- Its rows use the `provider` field's name, so no model name is left to trim — **passed** (code: `providerName` at RelayWindowModels.cpp:472-474).
- The key step's title reads "Key for kimi" — **passed**: `05-key-prompt.png`.
- The "+ add provider" row's summary names providers — **passed**: `01-model-settings.png` ("a custom endpoint, or kimi, z.ai, minimax, openai, anthropic, google, deepseek").

**Tests, line by line:**
- "Build: `scripts/relay-build --target relay`" — **passed** (green earlier this sweep at HEAD-era tree; full-tree builds later broke on another session's in-flight #Y2BA work, unrelated).
- Implementer's screenshots `docs/qa_evidence/2026-09-23-provider-names/` — **present**, and re-confirmed live today.

Unresolved: nothing.

Reviewed 2026-09-25 by the verifying session (qa-verify-7KPN), rev `2db96643`.
