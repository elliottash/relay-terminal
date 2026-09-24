---
id: BYG1
type: work
status: inbox
labels: [bug]
rank: zzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Custom loopback provider unselectable in the model picker (has_stored_key False for key_source "local")

## Issue
Noticed while staging #M5FZ: a custom provider whose base_url is a loopback URL (e.g. http://127.0.0.1:8731/v1 for a local model server) is reported by customproviders.row with has_stored_key False (key_source "local" is excluded by `bool(source) and source != "local"`), and the GUI's model picker (ModelCatalog usableRow) then treats the row as unusable — it shows "no key yet" and clicking it does nothing, even though the endpoint needs no key at all and the worker would happily serve it. A loopback custom endpoint cannot be selected in the picker without also putting a dummy key in the environment.
