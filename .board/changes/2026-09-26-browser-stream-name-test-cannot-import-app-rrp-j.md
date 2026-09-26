---
id: D08Y
type: work
status: executing
labels: [bug, remote, tests]
assignee: agent
implemented_by: openai/gpt-6-sol via codex:ashe-ethz-ch
session: dc713c52-45bc-4954-ba65-72ecb17516de
discovered_from: C0Q8
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-26'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: low}
links: {plans: [], commits: [6f0a2e8fbcae], evidence: [], related: [C0Q8], github: null}
---
# Browser stream-name test cannot import app/rrp.js under Node

## Issue
`tests/test_remote_wire.py::BrowserStreamNameTests` fails on Node 18 because `tests/stream_name_peer.mjs` imports `app/rrp.js` as an ES module while Node treats that .js file as CommonJS. The test cannot reach its stream-name assertion.

## Done means
The Node test imports the same `app/rrp.js` module the browser uses and `BrowserStreamNameTests` passes without changing the stream-name assertion.

## Execution Summary
`app/package.json` marks browser assets as ES modules for Node peers, and `app/noise.js` permits import before a WebCrypto provider is installed. Commit `6f0a2e8f`, queue job `7ffaccd88db278e0` (publication pending).

## Tests
- tests/test_remote_wire.py::BrowserStreamNameTests::test_the_client_resumes_under_the_hubs_own_stream_names
- Manual run: browser stream, pairing and invite link classes — 9 passed.
