---
id: H0P3
type: work
status: planned
labels: [feature, remote, design]
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-24'
source: owner, Relay pane, 2026-09-24
links: {plans: [], commits: [], evidence: [], related: [PH0N, 05J2], github: null}
---
# A phone identity that survives storage loss: passkey-derived device key, or an account on relay-terminal.ai

## Issue
my phone pairing keeps canceling after my phone goes to sleep. how do we set up a persistent connection? we can consider all options including an account system on relay-terminal.ai. but my computer needs to be able to remember my phone without having to re-pair.

## Done means
After the phone's site storage is wiped (Safari "Clear History and Website Data", an app reinstall, or a new iPhone restored from backup), opening the app reconnects to the same desktop after one Face ID prompt — no QR code, no desktop confirmation dialog — and the desktop still shows exactly one device row, same name and capability, with an audit `recover` line. A phone without PRF support pairs exactly as today, and a phone whose re-derived key no longer matches is refused and falls back to today's QR flow — never locked out. Failure looks like the pairing form asking for a QR after storage loss, or a second device row appearing for the same phone.

## Plan
**Goal.** Option B from the 2026-09-24 assessment: the phone's X25519 device key becomes a function of a WebAuthn passkey (PRF extension) for the rendezvous origin, so a wiped IndexedDB or a new iPhone regenerates the same key the desktop already pins, and a recovery path re-delivers the connect token and desktop pin without a QR or a desktop dialog. No account layer. This lands *after* #Q5QJ and #SAW4 (both landed in `14d7f422`, pending the owner's two-day phone check) — those bugs, not a missing identity layer, caused the re-pairing.

**Findings.**
- The desktop identifies a device at the Noise handshake by pinned static key alone — `remote/host.py:399` `_handshake()` → `DeviceStore.by_key()` — with no dialog; `_welcome` (`remote/host.py:1795`) already re-mints the connect token on every connect. So a wiped phone that re-derives the same key needs only a way to *reach* a handshake.
- The phone keeps its whole record (static key, `device_id`, connect token, desktop pin) in one IndexedDB row — `app/rrp.js` `openDb()`/`saveRecord()`; the static key is WebCrypto X25519 (`app/rrp.js:177`, `433`), and `app/cpace.js:121` already has the PKCS8-prefix import a PRF-derived scalar needs.
- The hosted rendezvous (`rendezvous/server.py`) refuses tokenless `?desktop=` connects; tokenless room connects pass under a per-address cap (`server.py:52`). Registration is wholesale-replaced per desktop (revoked-token list, `server.py:213` `register()`), which is the pattern a live-passkey list should copy. The server already stores desktop static keys; WebAuthn needs only ES256 assertion verification — `cryptography` is already a dependency of `remote/push.py`'s VAPID signer and must be added to the service's environment.
- WebAuthn is only possible over HTTPS on the hosted origin (join.relay-terminal.ai); the desktop's own local rendezvous keeps today's QR flow. #PH0N dropped WebAuthn from its threat table for a different purpose; this use (key derivation, server holds only credential public keys) does not reopen that decision.

**Steps.**
1. New `app/passkey.js`: `SALT_V1` (fixed 32-byte salt), `register()` — `navigator.credentials.create()` with `residentKey: 'required'`, `userVerification: 'required'`, `extensions.prf.eval.first = SALT_V1`, rpId = the serving origin's registrable domain — and `derive(prfBytes)` — HKDF-SHA256 (`salt='relay/device-key'`, `info='v1'`) → 32-byte scalar → X25519 key pair via the `app/cpace.js:121` PKCS8 concat. If `create()` does not return the PRF value, one follow-up `get()` with the same eval (the standard two-call dance).
2. `app/rrp.js` pairing path (identity creation at `:177`/`:190`, `pair()` at `:433`): when PRF is available, the static key pair comes from the passkey instead of `generateKey`, and `credentialId` is stored in the record. `pair_prove` gains a `passkey: {id, cose}` field.
3. `remote/identity.py`: `Device.passkey_credential: dict | None`; kept in `devices.json` alongside `connect_token_id`. `remote/host.py` `_on_pair_prove` stores it; the registration payload to the rendezvous gains the desktop's live passkey list, replaced wholesale exactly like `revoked_tokens` — revoking a device drops its passkey.
4. `rendezvous/server.py`: `passkeys(desktop_id, credential_id, cose_public_key, created)` table + wholesale replacement in `register()`; `GET /v1/recover/begin` returns a challenge; `POST /v1/recover/complete` verifies the ES256 assertion (challenge, origin, credential id against the stored COSE key) and returns `{desktop_id, path}` — a one-time recovery connect path that behaves like a room connect (tokenless, per-address cap). The server stores only credential public keys; the PRF output never leaves the phone except as the public static key in the Noise handshake.
5. `remote/host.py` `_handshake()`: a channel that arrived on a recovery path and whose key `by_key()` recognises becomes that device and proceeds to `hello`/`welcome` (token re-minted there); an unknown key is refused as today. No approver dialog either way. Audit `recover` event; desktop toast "*<name>* re-attached from its passkey".
6. `app/rrp.js` startup with no stored record: try `navigator.credentials.get()` with `userVerification: 'required'` and the same PRF eval → re-derive the key → `/v1/recover/*` → connect `?desktop=<id>&recover=<path>` → on `welcome`, rebuild the record (device id, fresh token, desktop fingerprint re-pinned from the handshake). Any failure at any step falls through to today's pairing form, untouched.
7. `docs/REMOTE-PROTOCOL.md`: new section on passkey recovery — key derivation, what the rendezvous holds, the local-rendezvous limitation, and the fallback contract.

**Risks.**
- *Owner decision:* this plan assumes option **B**, the 2026-09-24 recommendation the owner has not yet answered. If multi-desktop or team use is coming, **C** (account registry on relay-terminal.ai) is the right shape instead and this card should be redirected before Run — say so rather than building B.
- PRF is iOS 18+ / current Chrome only, and a PRF value can rotate if the authenticator downgrades user verification. Mitigation: `userVerification: 'required'` plus one retry; a rotated key simply fails `by_key()` and falls back to the QR flow — status quo, no lockout.
- New `cryptography` dependency on the rendezvous host; recovery exists only through the hosted rendezvous (HTTPS), which the docs must state plainly.
- WebAuthn cannot be driven end-to-end in CI: unit-test the derivation and the assertion verifier against synthetic credentials; the PRF path itself is verified by hand on the owner's phone.

**Verify.**
- Node/Python pair tests, same pattern as `tests/connect_token_peer.mjs` + its Python driver: a fixed PRF byte string derives the same X25519 public key in JS and Python (HKDF vector).
- Rendezvous tests (alongside the existing `meet_code`/registration suite): recovery begin/complete with a synthetic ES256 credential made by `cryptography`; wrong challenge, wrong origin, and a revoked device's passkey are all refused; a recovery path is one-time.
- Hub tests (`tests/test_remote_noise.py` harness, plus a new `passkey_peer.mjs`): a recovery channel whose key is pinned gets `welcome` with a fresh connect token and no approver call; an unknown key is refused with no dialog.
- By hand: pair the phone with PRF on, wipe the site data in Safari, reopen — one Face ID prompt, the same device row, an audit `recover` line, no QR.
