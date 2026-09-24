# #PGBZ — WebKit X25519 IndexedDB readback: browser-storage evidence

Commit: `9c6bea96b759e7012bdc9f8c5a71cad820044ddc` on `main` (2026-09-24).
Session: Relay pane 472d0dbf, taking over the released Codex session 0504c59c
whose implementation and tests were finished in the tree but unlanded.

## What was verified here (real Chrome, real rendezvous + host)

```
PYTHONPATH=backend python3 -m unittest tests.test_remote_browser_storage   # 3 runs, 6 tests each, OK
PYTHONPATH=backend python3 -m unittest tests.test_remote_browser           # 25 tests, OK
```

The six storage tests, three new in this commit:

- `test_usable_key_with_broken_cryptokey_prototype_pairs_and_reconnects` —
  with `window.CryptoKey` replaced so `instanceof CryptoKey` is false
  (WebKit bug 182097 shape), pairing completes, exactly one request reaches the
  host, and the reloaded record reconnects into the inbox.
- `test_lost_x25519_readback_uses_sealed_key_and_reconnects` — with the
  X25519 probe's IDB readback nulled (WebKit bug 312279 shape), pairing takes
  the sealed route (`sealNonextractable`), reconnects after a fresh load, and
  `(await loadDevice()).devicePrivate.extractable === false`.
- `test_unusable_key_storage_refuses_before_consuming_pairing_offer` — with
  all key readback nulled, pairing refuses with "could not keep a pairing key"
  and **zero** requests consume the one-time offer.

Plus the three pre-existing #SAW4 storage tests, still green.

One flake fixed while verifying: the refuse-route test polled
`#pair-state` with `document.getElementById(...).textContent`, and
`Browser.wait_for` propagates an evaluation error instead of retrying it, so a
poll before the pair screen rendered crashed the first run. The expression is
null-safe now (`?.textContent || ''`).

## What is left to the person (the card's `verify` criteria)

On the affected iPhone: pair, fully close and reopen the browser, and confirm
it reconnects without re-pairing; a genuinely lost key still shows the clear
error. No automated harness can stand in for the phone.
