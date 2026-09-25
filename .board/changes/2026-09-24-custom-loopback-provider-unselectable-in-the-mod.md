---
id: BYG1
type: work
status: needs-verification
labels: [bug]
assignee: agent
implemented_by: kimi/kimi-k3
session: 857ae200-ed0f-46b2-85d4-1c9065bb0a08
rank: zzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, criteria: row has has_stored_key True and key_source 'local'; usableRow admits it; Options status says no key needed rather than keyring/env, sign_off: none, effort: low}
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Custom loopback provider unselectable in the model picker (has_stored_key False for key_source "local")

## Issue
Noticed while staging #M5FZ: a custom provider whose base_url is a loopback URL (e.g. http://127.0.0.1:8731/v1 for a local model server) is reported by customproviders.row with has_stored_key False (key_source "local" is excluded by `bool(source) and source != "local"`), and the GUI's model picker (ModelCatalog usableRow) then treats the row as unusable — it shows "no key yet" and clicking it does nothing, even though the endpoint needs no key at all and the worker would happily serve it. A loopback custom endpoint cannot be selected in the picker without also putting a dummy key in the environment.

## Done means
A custom provider whose base_url is loopback reports `has_stored_key: True` with `key_source: "local"`, so `ModelCatalog::usableRow` and the pane's stored-model rule admit it and the picker can select it with no key anywhere. Options shows a truthful status for it (not "key stored in the keyring"). `tests/test_customproviders.py` LoopbackTests asserts the row.

## Execution Summary
Fixed in 73644db0. `customproviders.row()` reported `has_stored_key: False` for `key_source: "local"`, which is what `ModelCatalog::usableRow`, the pane's stored-model rule and the Options providers page all read as the key requirement — so a keyless loopback endpoint showed "no key yet" and could not be selected. The row now reports the requirement satisfied for a local source (a loopback endpoint sends no key at all), and the Options status line has a truthful branch for it ("no key needed (local endpoint)") instead of falling through to "key stored in the keyring".

Verified: `tests.test_customproviders` 27/27 (LoopbackTests now asserts `(True, "local")`); the exact landed tree built clean through land.py's verify build.
