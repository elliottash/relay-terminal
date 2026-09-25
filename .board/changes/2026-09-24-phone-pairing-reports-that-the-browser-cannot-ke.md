---
id: PGBZ
type: work
status: needs-verification
labels: [bug, remote]
assignee: agent
implemented_by: glm/glm-5.3
session: 472d0dbf-8927-4998-8448-6cf1237c2b0c
rank: zzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [person], human: required, criteria: 'On the affected iPhone, pairing completes and reconnects after fully closing and reopening the browser; a genuinely lost key still produces a clear error.', sign_off: none, effort: high, stakes: rework, blast: capability}
source: owner, 2026-09-24, Codex session in Relay
links: {plans: [], commits: [9c6bea96b759e7012bdc9f8c5a71cad820044ddc], evidence: [docs/qa_evidence/2026-09-24-pgbz-webkit-x25519-readback/], related: [SAW4, SMDX], github: null}
---
# Phone pairing reports that the browser cannot keep its key after IndexedDB readback

## Issue
i just had a bug when pairing my phone

the phone says 

this browser could not keep the pairing key, so the paing would not surve a restart. update ios or use another proser then pair again

## Done means
- Pairing succeeds and reconnects after restart when IndexedDB can store the nonextractable X25519 key, even if a prototype check fails.
- When Safari cannot read back a stored X25519 key, pairing uses a tested, encrypted fallback; the key remains nonextractable after being restored, and no plaintext private key is persisted.
- A missing or unusable persisted key still stops pairing with an actionable error.
- IndexedDB writes are confirmed committed and real-browser tests cover both storage routes and reconnecting.

## Plan
**Goal:** Pair and reconnect on iOS even when WebKit misreports or cannot deserialize a stored X25519 CryptoKey.

**Findings:** `app/rrp.js` rejects keys by `instanceof CryptoKey` and resolves IndexedDB writes before transaction completion. WebKit documents both usable keys with a broken prototype chain (bug 182097) and null readback for nonextractable X25519 keys (bug 312279). The latter needs a storage fallback.

**Steps:** 1. Probe whether this browser can roundtrip and use a nonextractable X25519 key before consuming the pairing secret. 2. If it cannot, encrypt an extractable X25519 private key with a nonextractable AES-GCM key that is itself tested in IndexedDB; store only ciphertext and AES key, and import X25519 nonextractable on load. 3. Verify restored keys with X25519 derivation, await transaction completion, and cover both routes with real-browser tests. 4. Land and document the phone recheck.

**Risks:** The encrypted fallback gives same-origin script with access to both IDB values a route to private material; direct nonextractable storage remains preferred. Other WebKit storage failures can still prevent pairing. The affected iPhone is needed to verify its actual behavior.

**Verify:** Targeted browser storage tests and a pairing/restart check on the phone.

## Tests
- `tests.test_remote_browser_storage` — 6 tests in real Chrome against the real rendezvous and host, run 3× green: broken-prototype route pairs + reconnects; lost-X25519-readback route seals, reconnects after reload, restored key non-extractable; unusable store refuses with zero host requests; the three #SAW4 storage tests still green.
- `tests.test_remote_browser` — 25 tests, green.
- Fixed a first-run flake found while verifying: the refuse-route poll hit `#pair-state` before it rendered and `Browser.wait_for` propagates evaluation errors; the expression is null-safe now.
- Evidence: docs/qa_evidence/2026-09-24-pgbz-webkit-x25519-readback/.

## Execution Summary
The released Codex session (0504c59c) had finished the implementation and its tests in the working tree but landed nothing. This session reviewed every hunk, verified it, and landed it as `9c6bea96` via `scripts/land.py` (`begin --base main` + the two-step confirm, since the hunks predated the claim).

`app/rrp.js`: before consuming a one-time pairing offer, `deviceKeyForPairing()` round-trips a non-extractable X25519 key through IndexedDB and proves it with `sameX25519Key()` (an actual `deriveBits`, so WebKit's broken `instanceof CryptoKey` prototype no longer fails a usable key — bug 182097); `dbPut`/`dbDelete` now resolve on `tx.oncomplete`, so writes are confirmed committed. When the X25519 readback is null (bug 312279), pairing seals an extractable X25519 key under a non-extractable AES-GCM key that itself passed a roundtrip (`sealPrivate`/`openPrivate`), `saveDevice` stores only ciphertext + AES key, and `loadDevice` imports X25519 non-extractable — no plaintext private key is ever persisted. A genuinely unusable store still refuses with the actionable error before any request is sent. `docs/REMOTE-PROTOCOL.md` documents both routes.

Left for the verifying session and the owner: the QA checklist, and the on-phone recheck (pair, fully close and reopen the browser, reconnect without re-pairing; a genuinely lost key still errors clearly).

## Try it
Staged by the implementer (this pane); run `docs/qa_evidence/2026-09-24-pgbz-webkit-x25519-readback/stage.sh` — it verifies the checkout serving the phone holds `9c6bea96` and prints the steps below.

**Open:** the Remote pairing QR/link on this desktop's Relay, on the affected iPhone.

**Task:** pair from the iPhone, fully close Safari (swipe it away in the app switcher), then open the link again — the app should reconnect without asking to pair, and the "could not keep the pairing key" error must not appear. Optional cross-check: a Private Window (fresh storage) is the genuinely-lost-key route and should still refuse with the clear error.

**Record:** what happened as `observed.md` (screenshots welcome) in `docs/qa_evidence/2026-09-24-pgbz-webkit-x25519-readback/`, then answer the Human QA question. The desktop's remote host must have been restarted since the fix landed, and the phone must reload the page (not a cached copy) — stage.sh says so too.

## Human QA
1. On the affected iPhone: did pairing complete without the "could not keep the pairing key" error, and did the app reconnect after fully closing and reopening the browser?
