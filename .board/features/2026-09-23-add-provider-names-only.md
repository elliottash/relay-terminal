---
id: 7KPN
type: work
status: needs-verification
labels: [feature, models]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: Codex in Relay, 2026-09-23
links: {plans: [], commits: [07cb34a30b23a3e50da9f96212139a75575351ec, 8d9f24cf36764e54c494a2740c56b0e2cb78768a], evidence: [docs/qa_evidence/2026-09-23-provider-names/], related: [4BPE], github: null}
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
