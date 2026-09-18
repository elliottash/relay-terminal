---
id: XEMH
type: work
status: inbox
labels: [bug, remote, tests]
rank: zzzzj
created: '2026-09-18'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Remote browser-peer tests fail: named export Rrp not found (app/rrp.js seen as CommonJS)

## Issue
[Filed under the unrelated-fault rule, not a user request: while verifying the file-tools path change, 4 failures + 6 errors in tests.test_remote_host / test_remote_wire / test_remote_noise.] Full backend suite (./scripts/test.sh): "Ran 943 tests … FAILED (failures=4, errors=6)". All 10 are in the remote modules; identical count with the file-tools change stashed, so it is pre-existing in the current working tree (which carries large uncommitted remote work: remote/*.py, app/*, tests/test_remote_host.py, new remote/audit.py). Representative failure, tests/test_remote_wire.py:225 BrowserPairLinkTests.test_a_plain_link_parses: pair_link_peer.mjs exits 1 — "SyntaxError: Named export 'Rrp' not found. The requested module '../app/rrp.js' is a CommonJS module, which may not support all module.exports as named exports." (node v18.19.1). Likely app/rrp.js lost its ESM form or package.json type during the in-flight remote/browser work.

## Checked 2026-09-18 (the #W5N2 session)
No longer reproduces at `6e1e98f`: `python3 -m unittest tests.test_remote_wire tests.test_remote_noise`
runs 42 tests, all passing, the Node peers included. `app/rrp.js` exports `Rrp` by name. The failure
was the half-landed remote work in the tree at the time, since committed in `cc79c01` and `dd35ead`.
This card can be closed; the remaining remote failures (voice, scrollback) are tracked on `#W5N2`.
