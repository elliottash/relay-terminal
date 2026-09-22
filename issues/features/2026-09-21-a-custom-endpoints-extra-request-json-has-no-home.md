---
id: XJSN
type: work
status: discussing
labels: [feature, models]
assignee: codex
waiting_on: owner
rank: n
created: '2026-09-21'
source: 'Claude Code in a Relay pane, 2026-09-21 — left over from #MDL1 t:a15'
links: {plans: [], commits: [], evidence: [], related: [MDL1], github: null}
---
# A custom endpoint's extra request JSON has no home

## Issue
Not the owner's words: raised by Claude while retiring the advanced provider dialog for #MDL1
(`cdc4b9bb`). That dialog was the only place to type a per-request JSON body for a provider. A
custom provider stores a name, base URL, model ids and a reasoning style
(`backend/relay_core/customproviders.py`) and carries no request body, so the field had nowhere to
go. Nothing regressed for a built-in provider: `provider/extra` is still written by
`configurePreset` from the worker's own preset row.

## Discussion points
Whether anyone needs it decides the work. If custom endpoints must carry a body, the field belongs
on "+ add provider" in the models pane's providers tab, with a column in `customproviders.py` and
the worker sending it as the row's `extra`. If not, the card is dropped and the design says so.

## Planning notes
Code review, 2026-09-22 (Codex): `CustomProvider.as_preset()` currently supplies `{}` for `extra`.
`session_protocol.provider_config()` already reads `preset.extra`, and the transport already
supports it. The missing path is storage/catalog/form rather than a new transport.
`save()` and `_record_served()` reconstruct the dataclass, so both must preserve the new field.
The add/edit form lives in `RelayWindow::modelsSection` (`askForCustom`).

## Done means
If the owner chooses to retain the field: a custom provider can save, edit and clear an optional
JSON object; malformed JSON or a non-object stays in the form with an actionable error. The object
survives restart and asynchronous model discovery and reaches that provider's outgoing requests.
Existing custom providers without the field and built-in providers behave as before.

## Plan
**Goal:** restore optional extra request JSON on the custom-provider path, if retained.
**Findings:** `backend/relay_core/customproviders.py`, `src/RelayWindow.h` (`askForCustom`),
`backend/relay_core/session_protocol.py`, `tests/test_customproviders.py`, protocol §28.6.
**Steps:**
1. Record the owner's retain/drop decision.
2. Add validated object storage and preset/catalog propagation; preserve it when model probing
   reconstructs the provider or an existing provider is edited.
3. Add an optional multiline JSON field to add/edit, prefilled on edit, validated before closing.
4. Document merge behavior and exercise save/reload, clear, invalid input, probe preservation,
   model switching and a captured request to a loopback endpoint.
**Risks:** the transport's existing merge precedence must remain consistent. Shared
`src/RelayWindow.h` currently contains other sessions' edits; snapshot and land only owned hunks.
**Verify:** focused custom-provider tests and an isolated live form-to-request round trip.
