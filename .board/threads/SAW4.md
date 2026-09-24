<!-- relay:entry 20260924T130615Z-0v author=agent kind=event model=claude-fable-5-1 pane=b0371114 turn=479dadd170c9409dad1db2f1f2ce59f3/2a7ab073531c4d9a8a4117b919507eeb -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-24-the-phone-app-s-default-screen-is-the-pairing-fo.md

<!-- relay:entry 20260924T130702Z-3e author=agent kind=evidence model=claude-fable-5-1 pane=b0371114 turn=479dadd170c9409dad1db2f1f2ce59f3/2a7ab073531c4d9a8a4117b919507eeb -->
Diagnosis (Claude Fable, 2026-09-24).

**Symptom.** `~/.local/share/relay/remote/devices.json` holds 13 device records, all "iOS Safari": 09-17, 09-18 ×4, 09-20, 09-22, 09-23 16:24, 17:43, 21:41, 09-24 00:00:17, 00:01:35, 00:20:51. The rendezvous `events` table shows each pairing as `room` + `code` + 3 `channel_open`, and **no `channel_open` at all between pairings** while the desktop stayed registered (00:14 → 05:32 → 08:15 today, no flap). So after every session the phone never dialled again; the owner paired afresh each time, twice 78 s apart (00:00:17 and 00:01:35). The owner's earlier report on #PH0N (Codex entry, 09-22 14:22): after a desktop restart the phone showed "pairing code form".

**Why the pairing form.** `app/index.html` shows `screen-welcome` (the code form) by default; every other screen is `hidden`. `connectStored()` runs once at `DOMContentLoaded` and only switches screens after `loadDevice()` resolves. `loadDevice` → `dbGet` → `openDb()` opens IndexedDB with no timeout, no retry and no error surface. If the open hangs or errors — WebKit has a long history of IndexedDB requests that never fire after a Home Screen web app is killed in the background and cold-started, which is exactly what iOS does to a suspended PWA — the app sits on the pairing form with a stored record it never read. Nothing tells the person "this phone is paired to <desktop>; storage is not answering". A `revoked` message and the Forget button are the only code paths that delete the record; neither ran.

**Also exposed.** The device private key is a non-extractable X25519 `CryptoKey` structured-cloned into IndexedDB (`rrp.js pair()`); WebKit's support for cloning X25519 keys is recent, and a key that reads back unusable would fail in `#handshake` with an opaque error and endless 3 s retries. `MAX_CHANNELS_PER_TOKEN` (3) counts stale sockets a sleeping iPhone leaves behind through the Cloudflare tunnel, so a phone woken three times in a few minutes can be refused 4429; refusals are not logged, so this is unconfirmed.

**Fix.**
1. Default screen is a "checking this device…" state, not the pairing form. The pairing form appears only when `loadDevice()` *resolved* to no record.
2. Bound the IndexedDB open (a few seconds), retry once, and on failure show "This phone is paired to <name>; its storage is not answering — reopen the app" using a small mirror of the record's presence and desktop name in `localStorage`.
3. Verify the record by reading it back right after `saveDevice`; if the CryptoKey does not survive the round trip, fall back to an extractable key stored as bytes, and say so once in the pairing thread.
4. Rendezvous: when a token is at its channel limit, close the **oldest** channel on that token rather than refuse the newest, and log refusals by desktop.
5. Persist `last_seen` (batched), show it in the Sharing pane, and offer "remove devices not seen in 30 days" so the 13 records become one.

<!-- relay:entry 20260924T130702Z-tn author=agent kind=event model=claude-fable-5-1 pane=b0371114 turn=479dadd170c9409dad1db2f1f2ce59f3/2a7ab073531c4d9a8a4117b919507eeb -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260924T132205Z-dd author=agent kind=evidence model=claude-opus-5-5 pane=b0371114 turn=479dadd170c9409dad1db2f1f2ce59f3/9f023a82c1004f1d99c0e10e85a4bad0 -->
Landed in 14d7f422 and deployed to join.relay-terminal.ai; all 30 served files match main. Changes: a neutral start screen is the default; the IndexedDB open is bounded at 4 s and retried once; a localStorage hint lets the app say "paired with <desktop>, storage not answering" and retry on its own, or "pairing key no longer in storage"; the key is read back at pairing; a reconnect uses the in-memory record and no longer dies on a failed read; revoke and Forget clear it. Tests: tests/test_remote_browser_storage.py passes 3 of 3 (stalled storage recovers with no second pairing; a lost record is named; a first visit still reaches the form). test_remote_browser and test_remote_guest_browser pass 39 of 39. Not done: evicting the oldest channel on a per-token 4429 (rendezvous). No 4429 is observed yet. Owner check: after a night of sleep, open the Home Screen app and confirm it reaches the inbox with no new device record on the desktop.

<!-- relay:entry 20260924T132206Z-nd author=agent kind=event model=claude-opus-5-5 pane=b0371114 turn=479dadd170c9409dad1db2f1f2ce59f3/9f023a82c1004f1d99c0e10e85a4bad0 -->
- ✦ agent moved this card · Discussing → Needs verification · Landed 14d7f422 and deployed; awaiting the owner's phone after a sleep. · implemented_by anthropic/claude-opus-5-5 via claude-code
