# RRP/1: the Relay Remote Protocol (spec, 2026-09-17)

Phase P0 of `issues/features/2026-09-17-remote-phone-and-multiplayer.md` (`#W5N2`). The design and
the owner decisions behind it are in [REMOTE-AND-MULTIPLAYER-DESIGN.md](REMOTE-AND-MULTIPLAYER-DESIGN.md);
this document is the wire contract. Section 14 says which parts exist today.

Requirement words are normative: **must**, **must not**, **should**, **may**. A rule marked
**(security)** is covered by a test before the phase that introduces it can ship.

## 1. Scope and roles

| Term | Meaning |
|---|---|
| **Desktop** | A running Relay GUI process. It is the only endpoint that holds panes, workers and keys. One `RemoteHub` per process (like `NotificationCenter`, `src/Notifications.h`), in `src/RemoteHub.{h,cpp}` |
| **Client** | A phone, tablet or laptop browser running the Relay web app, or later a native app |
| **Rendezvous** | `rv.relay-terminal.ai`: signaling, a ciphertext-only relay, the device registry and Web Push. It never holds a content key |
| **Pane** | A Relay pane, addressed by its **session token** — the same string `Notification::source` already carries |
| **Participant** | P4 only: a client other than the owner's own devices |

The desktop is the hub. Every client has its own pairwise session with the desktop, and the desktop
fans out. There is no group key and no client-to-client traffic.

Clients never talk to a worker (`backend/worker.py`). The hub subscribes to the same GUI-side signals
the desktop UI uses, filters them, and re-emits them as RRP messages.

## 2. Layers

```
  client                                                      desktop
  ┌───────────────────────────┐                      ┌──────────────────────────┐
  │ RRP messages (§6)         │                      │ RRP messages             │
  ├───────────────────────────┤                      ├──────────────────────────┤
  │ Noise_IK_25519_ChaChaPoly_SHA256  (§4)           │  same                    │
  ├───────────────────────────┤                      ├──────────────────────────┤
  │ frames (§3)               │                      │  frames                  │
  ├───────────────────────────┤                      ├──────────────────────────┤
  │ WebSocket to rendezvous  ─┼── or ── WebRTC data ─┼─ WebSocket to rendezvous │
  └───────────────────────────┘        channel (P2)  └──────────────────────────┘
```

The Noise session is **independent of the transport**, but **exactly one transport carries frames
at a time**. A Noise cipherstate is a strictly incrementing counter with no reorder window, so two
live paths would either kill the session on every upgrade or force a replay window — and a replay
window hands the rendezvous, which sits on the WebSocket path, the ability to re-inject a captured
`compose` or `keys` frame on the other one.

Switching is therefore explicit. The sender stops writing to the old path, sends
`transport_switch {next_seq}`, drains and closes the old path, and only then resumes on the new one.
**(security)** A receiver **must** reject any frame whose nonce is not exactly the next expected
one, on any path, and **must not** implement a reorder or replay window.

**(security)** The WebRTC DTLS fingerprint is never trusted on its own; authentication comes only
from the inner Noise session.

## 3. Framing

**Over a WebSocket** one binary message is one frame; the message boundary is the frame boundary
and there is no length prefix. **Over a stream transport** (a WebRTC data channel, P2) boundaries
have to be restored, so a frame is:

| Field | Bytes | Meaning |
|---|---|---|
| `len` | 4, big-endian | Length of `body`, at most 1 048 576 |
| `body` | `len` | One Noise transport message |

**(security)** Both peers **must** drop a connection that announces a `len` above the maximum,
rather than try to resynchronise, and **must not** allocate before checking it.

The plaintext inside one Noise message is:

| Field | Bytes | Meaning |
|---|---|---|
| `enc` | 1 | `0` = JSON (UTF-8), `1` = CBOR |
| `payload` | rest | One RRP message object |

Control, pane and agent traffic is JSON; screen and history traffic is CBOR. Both encodings carry
the same object shape, so a message is described once in §6.

**(security)** In RRP/1 every client→desktop message is JSON, so a desktop **must** reject `enc = 1`
from a client: there is no reason to expose a CBOR decoder to attacker-chosen bytes. Decoding
**must** also be bounded — maximum nesting depth, element count and string length — because a 1 MiB
frame of pathological JSON is otherwise a cheap way to stall the GUI thread.

### 3.1 The relay envelope

The desktop holds **one** socket to the rendezvous but may be talking to several clients, so the
rendezvous needs to tell channels apart without being able to read them. On the desktop's socket
every message is:

| Field | Bytes | Meaning |
|---|---|---|
| `kind` | 1 | `0x01` data · `0x02` channel open · `0x03` channel close |
| `channel` | 16 | Channel id, minted by the rendezvous |
| `payload` | rest | A frame as above, or JSON metadata for open and close |

On a client's socket the payload travels bare, because a client has exactly one channel. This
envelope is the **only** cleartext the rendezvous sees, and it contains no content: a kind, an
opaque id, and for an open the room or device id the client claimed.

## 4. Identity, handshake and keys

**Suite: `Noise_IK_25519_AESGCM_SHA256`.** IK, because the client already knows the desktop's
static key from pairing, so the first client message carries the client's static key encrypted and
the session is established in one round trip.

AESGCM rather than the more usual ChaChaPoly for one reason: **the client is a browser**. WebCrypto
offers X25519, AES-GCM, SHA-256 and HMAC, and does **not** offer ChaCha20-Poly1305. AESGCM is
therefore the one standard Noise suite both ends implement with no third-party crypto at all — no
vendored JavaScript crypto library holding the device key, and no build step in the client. On the
desktop the same primitives come from libsodium or `cryptography`. (BLAKE2s is not used because
libsodium ships BLAKE2b; SHA-256 is in every toolbox.)

| Key | Where it lives | Lifetime |
|---|---|---|
| Desktop static X25519 | Secret Service keyring via `secret-tool`, service `org.relayterminal.Relay`, under its **own attribute namespace** (`key=remote-identity`), else a 0600 file in a 0700 directory | Until the user resets remote access |
| Client static X25519 | Non-extractable `CryptoKey` in IndexedDB (web), Keystore or Keychain (native) | Until the device is revoked or expires |
| Session keys | Memory only | One connection |

**(security)** The identity key is **not** read through `backend/relay_core/keystore.py`. That module
honours a `RELAY_<ID>_API_KEY` environment variable ahead of the keyring, which is right for an API
key you may want to inject for one run and wrong for an identity that every paired phone has pinned:
anything able to set a variable in Relay's environment could otherwise substitute it. **(security)**
A missing identity key **fails closed** — remote access is off until the user pairs again. It is
never silently regenerated, because a new identity makes every pinned key mismatch and invites a
"trust the new one" prompt, which is the attack.

**(security)** The desktop **must** keep, for each paired device, the client's static public key,
and **must** abort the handshake if the key presented differs from the pinned one, even when the
rendezvous says the device id matches.

**(security)** The client **must** pin the desktop static key it learned at pairing, **must** abort
any handshake whose responder key differs, and **must not** offer a "trust this new key" path. A
changed desktop key means re-pairing from a QR code shown on the desktop. In Noise IK the
initiator's choice of responder key *is* the authentication of the desktop, so a client that accepts
a server-offered key has no security at all.

**(security) Revocation and downgrade apply to live sessions immediately.** Revoking a device
**must** send `revoked`, close every open session with that key, and drop its rings and its `msg_id`
cache. A capability change **must** be evaluated at every enforcement point on every message and on
every replayed event, read from the current device record — never cached in the session at
`welcome`. Deleting the pinned key locally means revocation also works while the rendezvous is
unreachable.

**(security)** API keys, keyring material and the Noise static private keys **must not** appear in
any RRP message, in any phase.

### 4.1 Rekeying and limits

A session **must** rekey (Noise `REKEY`) every 2^20 messages or 12 hours, whichever comes first, and
**must** end after 7 days. Nonces are never reused across a transport switch, because the nonce
counter belongs to the Noise session, not the socket.

## 5. Pairing

Pairing establishes the pinned key pair and nothing else. It is specified on its own so that
settings sync (`#05J2`, which plans to reuse "the same pairing machinery as remote access") can use
it without a second mechanism. A pairing produces a **device record**; what that device may then do
is a separate grant (§5.3).

### 5.1 QR pairing (default)

1. Desktop → Settings → Remote → **Pair device**. The desktop registers a room with the rendezvous
   and shows a QR code for
   `https://app.relay-terminal.ai/pair#v=1&d=<desktop static pubkey, base64url>&s=<128-bit one-time secret, base64url>&r=<room id>`.
   **(security)** The fragment is never sent to any server, by either side.
2. The client joins the room, runs the Noise IK handshake against `d`, and proves `s` inside the
   established session (`pair_prove`, §6.2). A client that cannot prove `s` is disconnected and the
   room is burned.
3. The desktop shows **"Pixel 9 · Chrome wants access: View / Agent / Full"** with the client's
   fingerprint, and the user chooses. Nothing is pinned until the user allows.
4. The secret is single-use and valid 5 minutes. **(security)** A second `pair_prove` for the same
   room **must** fail even if the first was refused by the user.

### 5.2 Pairing code (no camera)

Three words from a 2 048-word list plus a PAKE (CPace or SPAKE2 over Curve25519), magic-wormhole
style, ending in the same `pair_prove` step. Five attempts per room, then the room is burned.

### 5.3 Capabilities

A device record holds one capability, chosen at pairing and changeable in Settings:

| Capability | May |
|---|---|
| `view` | Subscribe to `panes`, `agent` and `screen`; read history and transcripts |
| `agent` | `view`, plus `compose`, queue control, `agent_stop`, `set_mode`, plan actions |
| `full` | `agent`, plus `keys`, `paste` and take-over (P3) |

Password entry (§6.7) is a **separate switch**, off by default, available only to `full` devices.

**(security)** Capability is read from the live device record at **every** enforcement point — each
inbound message, each fan-out and each replayed event — never cached in the session at `welcome`.

Two things the pairing dialog has to say plainly, because the names understate them:

* **`view` is not "read only" in the everyday sense.** It can read transcripts and stored tool
  output, which is every command the agent ran and every file it read, plus the title and `cwd` of
  every pane.
* A device is scoped to the **whole desktop**. There is no per-pane pairing in RRP/1; multiplayer
  (§10) is where sharing narrows to one pane. An optional pane scope on the device record is the
  obvious extension and is not specified here.

## 6. Messages

Every message is an object with `t` (type). A request **may** carry `id`; its reply carries the same
`id`. Messages that belong to a stream carry `seq` (§7). Unknown fields are ignored; unknown types
are answered with `error {code: "unknown_type"}` and otherwise ignored, so a newer peer degrades
rather than disconnects.

### 6.1 Session control

| Type | Direction | Body |
|---|---|---|
| `hello` | client → desktop | `{client: "relay-web/<version>", proto: 1, caps: [...], locale, tz}`. **(security)** `caps` is a feature hint only; the desktop's grant is authoritative and `caps` is never consulted at an enforcement point |
| `welcome` | desktop → client | `{desktop: {id, name, fingerprint}, proto: 1, capability, password_entry, hub_epoch, features: [...], server_time}` |
| `client_state` | client → desktop | `{visible}` — drives the frame-rate and notification presence rules |
| `resume` / `resumed` | both | §7 |
| `transport_switch` | both | §2 |
| `ping` / `pong` | both | `{at}`; a peer **must** answer `ping` within 5 s. Clients display the round trip |
| `error` | both | `{code, message, id?}`. Codes: `unknown_type`, `not_permitted`, `no_such_pane`, `busy`, `rate_limited`, `stale_seq`, `internal` |
| `bye` | both | `{reason}` before a clean close |

`features` is how a phase is negotiated: a P1 desktop omits `screen`, so a newer client hides the
terminal tab instead of failing.

### 6.2 Pairing and devices

| Type | Direction | Body |
|---|---|---|
| `pair_prove` | client → desktop | `{secret, name, platform, pubkey_fingerprint}` |
| `paired` | desktop → client | `{device_id, capability}` — sent only after the user allows |
| `revoked` | desktop → client | `{reason}` then close. The client wipes its stored keys |

### 6.3 Panes

| Type | Direction | Body |
|---|---|---|
| `panes` | desktop → client | `{seq, items: [{id, window, tab, title, cwd, program, control, status, unread, queue}]}` — the full list on connect, then on change |
| `pane_focus` | client → desktop | `{pane}` — subscribe; the desktop starts `agent` and, if permitted, `screen` for it |
| `pane_blur` | client → desktop | `{pane}` |

`control` is `human | agent | remote:<device_id>` (P3; `participant:<id>` at P4), extending the model
in `ARCHITECTURE.md` section 9. `status` is `idle | running | waiting_input | password | finished | failed`.

`status` is assembled from three sources that already exist: shell events (`shell/event.py` writes
`state.json`), the termios password check (`checkPasswordPrompt`), and OSC 133 marks (`PromptMark`
in `engine/core/CellTypes.h`). **`waiting_input` is best-effort**: per `ARCHITECTURE.md` section 9,
`sudo`, `doas`, `pkexec`, `su` and other users' processes do not expose `/proc/<pid>/syscall`, so
only their password prompts are detected. Clients **must not** present it as authoritative. `#YR21`
improves it later without a protocol change.

### 6.4 Agent

Agent messages are the worker's own events, filtered and re-tagged with `pane`. The names are the
worker's (`backend/relay_core/`, contract in [AGENT-SESSIONS-PROTOCOL.md](AGENT-SESSIONS-PROTOCOL.md)),
so there is one vocabulary and no translation table to drift:

`agent_started`, `delta`, `thinking_delta`, `thinking_done`, `tool_started`, `tool_result`,
`turn_summary`, `agent_finished`, `agent_stopped`, `cancelled`, `error`, `queued`, `queue_changed`,
`status`, `context`, `mode_changed`, `model_changed`, `plan_written`, `recap`, `subagent_started`,
`subagent_progress`, `subagent_finished`, `subagent_handoff`.

Forwarded as `{t: "agent", pane, seq, event: {...the worker event verbatim...}}`.

**(security)** The hub forwards an allow-list, never everything. `key_stored`, `key_removed`,
`key_tested`, `warp_imported`, `presets`, `model_roles`, `configured`, `agent_options`, the
`skills_*` family, `fork_state` and `state_loaded` **must not** be forwarded in any phase — key
material, provider configuration, local paths and opaque conversation state respectively. The lists
live in `remote/wire.py`, and a test enumerates every event `backend/` emits, so a new one is denied
until somebody classifies it.

**(security)** A forwarded `error` is normalised first: `worker.py` puts `str(exc)` in the body, and
both `attachments.py` and `voice.py` interpolate local paths into their messages.

On demand, mirroring the worker's own requests: `turn_transcript_get {pane, turn_id}`,
`tool_output_get {pane, tool_id}`, `checkpoints {pane}`, `sessions`, `recap_request {pane, reason}`.

### 6.5 Screen and history (P2)

| Type | Direction | Body (CBOR) |
|---|---|---|
| `screen_snapshot` | desktop → client | `{pane, seq, rows, cols, cells, cursor, alt, title, cwd, history_rows, viewport_top}` |
| `screen_diff` | desktop → client | `{pane, seq, rows: [{row, cells}], cursor, viewport_top}` |
| `history_get` | client → desktop | `{pane, id, before_row, count}` (`count` ≤ 200) |
| `history` | desktop → client | `{pane, id, from_row, lines: [...], more}` |

`cells` is the flattened `relay::Cell` grid: for each cell, the first codepoint (or a cluster
reference), `fg`, `bg`, `attrs`, `width` and the OSC 8 link id, plus the line's `marks`,
`continuation` and cluster table. Text is already decoded, which is what avoids Warp's garbled-CJK
class of bug.

**OSC 8 links need a core API that does not exist.** `Cell::link` refers to a `VtCore::hyperlinkUri`
that was never written; the core offers only `hyperlinkAt(row, col)` in viewport coordinates, so
there is no id → URI table for P2 to send. P2 adds one alongside `historyLines`. **(security)** The
URI comes from terminal output, so the desktop sends only `http` and `https` and the client refuses
any other scheme independently — a confirm sheet is not an answer to `javascript:`, `data:` or
`file:`.

Two rules come from the engine and are not negotiable:

- **The hub reads the `ViewportFrame` `TerminalView` has already produced**; it **must not** call
  `VtCore::updateFrame` itself. That call consumes the dirty state (`engine/core/VtCore.h`) and has
  exactly one consumer (`engine/view/TerminalView.cpp`), so a second caller would stop the desktop
  repainting.
- **`history_get` is served by a const `VtCore::historyLines(from, count, out)`**, to be added in
  both cores in P2. It **must not** move the viewport: `scrollViewportToRow` is shared state and
  would drag the desktop user's own screen.

**Sizing.** The host is authoritative. The client scales to fit and **must not** send its own size;
there is no resize message in RRP/1, which is what makes the Warp mobile-viewer resize bug
structurally impossible.

**Rate.** At most 20 frames/s while the client reports itself foreground, at most 4 frames/s
otherwise (`client_state {visible}`), and agent deltas coalesced at 50 ms.

### 6.6 Input (client → desktop)

| Type | Requires | Body |
|---|---|---|
| `compose` | `agent` | `{pane, text, when: "now"\|"queue"}`. **(security)** A remote `compose` is **always** routed to the agent. `agent: false` — the composer's shell route — requires `full`. Relay's router honours a `/shell ` prefix and a shell mode, so an `agent` device could otherwise run `curl … \| sh` with no agent in the loop, no take-over and no control token, while the pane's `control` still read `human` |
| `agent_stop`, `queue_remove`, `set_mode`, `recap_request` | `agent` | `{pane, ...}` |
| `plan_execute` | `agent` | `{pane, plan_id}`. **(security)** Never a path. `plan_id` must be one the desktop minted in a `plan_written` event, resolved against the hub's own table. `planning.read_plan` accepts any absolute path, so a path from the wire would read `~/.aws/credentials` into a prompt, into the session file, and back out to the phone in the transcript |
| `voice` | `agent` | `{pane, id, format, data}` — base64 audio, at most what fits one frame. **(security)** The filename and extension are **desktop-generated** (`<uuid>.webm`, mode 0600, unlinked after the reply); `id` and `format` are checked against fixed alphabets and never used as path components, because `voice.read_clip` picks its handling from the extension and its own docstring says it is not written against a hostile caller. The model is desktop configuration, never from the wire. The reply is `transcribed {id, text}`, for the user to edit before sending |
| `keys` | `full`, P3 | `{pane, bytes}` — raw key bytes, take-over only |
| `paste` | `full`, P3 | `{pane, text}` |
| `line` | `full`, P3 | `{pane, text}` — a whole line plus newline, which is what avoids Android composition corruption |
| `control_request`, `control_release` | `full`, P3 | `{pane}` |

**(security) Never accepted over RRP, in any phase:** `store_key`, `import_warp`, `configure` with
key material, skills import, keybinding or settings changes, and file writes outside the plan flow.
The hub **must** implement this as an allow-list of types, not a deny-list.

**(security) The allow-list binds the worker vocabulary, not only the RRP type list.** The worker
accepts around sixty request types — `rewind`, `fork`, `load_state`, `resume`, `reset`, `set_model`,
`conversation_delete`, `index_rebuild`, `transcribe` with a path, `keybindings` and more. The hub
**must** build each worker request from a fixed table of literal shapes: no field of a client message
is ever spread into a worker request, and a client never names a worker `type`.

**(security) No path, filename or worker request type is ever accepted from the wire.** Anything
naming a resource — a plan, an attachment, a tool output, a turn — is an id the desktop minted and
resolves itself. The `@file` picker follows the same rule when it arrives: `attachments.load`
deliberately permits files outside the workspace because a local user picked them, so a remote
reference must be an id from a desktop-served listing, symlink-resolved and confined to the pane's
cwd.

**(security) Every inbound type is rate-limited per device**, not only the expensive ones: `voice`
and `compose` spend the owner's provider key, and `history_get` and `tool_output_get` are cheap to
ask for and expensive to answer.

**(security) `keys`, `paste` and `line` are refused while the pane is at a password prompt.** When
`relay::input::secretPrompt` is true for a pane, `secret_input` (§6.7) is the only accepted input;
everything else gets `error {code: "not_permitted"}`. Otherwise a `full` device without the password
switch could watch for `status: "password"` and send `line {text: "hunter2"}`, which reaches the
program exactly as a password would and is additionally written to the replay ring and the audit
log in plain text — making the whole of §6.7 optional from the attacker's side.

**(security) Remote prompts are tagged, and labelled for the model.** A `compose` becomes a prompt
carrying `origin: "remote:<device_id>"`, recorded in the transcript and shown on the desktop. The
prompt is also wrapped in a labelled untrusted-origin block for the model, the way
`agent.validate_context` already labels foreground-program context. A device's display name is never
interpolated into that block.

### 6.7 Password entry (P3, off by default)

A password from a client is deliberately **not** an ordinary input message, because
`ARCHITECTURE.md` section 9 guarantees that a password reaches only the masked field and
`relay::input::Secret` — never the queue, the request ledger, the session file or logs.

`secret_input {pane, nonce, bytes}` therefore:

- **(security) carries no `seq`, is not written to the stream ring, and is never replayed** on
  resume;
- **(security) the `nonce` is minted by the desktop**, not the client. It is created when the prompt
  is detected, delivered with the `panes` status change, single use, bound to (pane, foreground pid,
  prompt generation), and expires in seconds. The desktop discards any `secret_input` whose nonce is
  unknown, spent or stale. A client-chosen nonce would bind nothing;
- **(security) the prompt is re-checked from a fresh termios read immediately before the write**,
  with the foreground pid unchanged. `checkPasswordPrompt` polls at 1 s (250 ms while a command
  runs) and `submitSecret` trusts the cached flag, so remotely the window is that poll plus the
  round trip plus however long the person takes to type. If the prompt has ended, the line would be
  written to the **shell** instead — into bash history, onto the screen, into the `screen` stream
  every `view` device sees, and into the audit log;
- **(security) never queued while offline**, enforced on the desktop by the nonce rather than left
  to the client;
- **(security)** it goes to `relay::input::Secret` and the bytes are wiped in place on both sides
  afterwards, including the receive buffer;
- **(security)** the audit log records only `secret_input {pane, device, at}`, never the bytes;
- **(security)** it is rate-limited and locked out per device, so a `full` device cannot brute-force
  `sudo` remotely.

**WebAuthn.** A user-verification check inside the web app enforces nothing — hostile JavaScript
skips its own `if`, and the desktop, which is the party that cares, sees no proof. Either the client
registers a WebAuthn credential id with the desktop at pairing and the desktop then issues a
challenge and verifies the assertion before accepting `secret_input` or a take-over, or the claim is
dropped and replaced by a desktop-side confirm. It **must not** be listed as a mitigation while it
is only a client-side check.

Tests mirror `tests/inputpolicy_test.cpp`, which already pins the desktop-side rules, and add the
case it does not cover: the prompt ending between the message arriving and the write.

## 7. Sequencing, resume and backpressure

Each stream — `panes`, and `agent` and `screen` per pane — has its own `seq`, a counter starting at
1 for the life of the desktop process. On reconnect the client sends
`resume {streams: {"<stream>": <last seq>}}`. The desktop replies `resumed {streams: {...}}` and then:

| Situation | Behaviour |
|---|---|
| `seq` still in the ring | Replay from `seq + 1` |
| `seq` too old, or the ring was reset | `screen`: one fresh `screen_snapshot`. `agent`: `turn_transcript` for the open turn plus `queue_changed` and `context`. `panes`: the full list |

The ring holds the last 2 000 agent events per pane and the last 4 screen snapshots plus their
diffs. Rings are memory-only and die with the process; a restarted desktop starts `seq` at 1 again
and announces a new `hub_epoch` in `welcome`, which **must** make a client discard its stored `seq`.

**Client → desktop** input carries a client-chosen `msg_id`. The desktop keeps the last 256 per
device and applies each at most once, so input queued while offline is never applied twice. This is
**idempotency, not anti-replay**: anti-replay is the Noise nonce counter (§2). A client that sends
257 messages and then reuses an old id would pass the cache, so `msg_id` **must** be monotonic per
device and the cache is a low-water mark.

**(security)** Replayed messages are re-filtered through the current capability on the way out; a
device downgraded between the original event and the resume **must not** receive it.

**Backpressure.** If a transport's send buffer exceeds 4 MB the desktop drops screen diffs first
(the next frame is a snapshot), then coalesces agent deltas, and never drops control or input.
**(security)** Dropping happens **before encryption**, in the emitter. Discarding an already
encrypted frame would leave a permanent nonce gap, and "fixing" that by tolerating gaps reopens
replay.

## 8. Rendezvous API

The rendezvous sees device ids, addresses, timings and byte counts. It **must not** be able to read
or forge content. Its whole API is:

| Endpoint | Method | Purpose |
|---|---|---|
| `/v1/challenge` | POST | → `{challenge, ephemeral_public}` for proof of possession |
| `/v1/register` | POST | `{static_pubkey, challenge, proof}` → `{desktop_id, token}` |
| `/v1/rooms` | POST | Open a pairing room: `{desktop_id, token, ttl}` → `{room}` |
| `/v1/connect` | WS | Both sides attach: `?room=` for pairing, `?desktop=` for a paired device. The server pairs up sockets and copies frames between them |
| `/v1/push/send` | POST | A desktop posts an opaque payload and the endpoint to deliver it to |
| `/v1/ice` | GET | STUN/TURN credentials (P2) |

**(security) `desktop_id` is derived from the key**: the first 128 bits of `SHA-256(static_pubkey)`,
computed by the server, never taken from the request. An id therefore cannot be squatted or rebound,
and a client that already pinned the key can compute the id itself.

**(security) Registration proves possession of the private half.** The server mints an ephemeral
X25519 key and a challenge; the desktop replies `HMAC(X25519(ephemeral_public, static_private),
challenge)`. Without this, anyone who learns a public key could register it and be handed a token
that attaches as that desktop and receives its client channels. Challenges are single use and
short-lived.

**(security) There is no push subscription endpoint, on purpose.** A Web Push subscription's
`p256dh` and `auth` are **content keys**. Storing them here would let a compromised rendezvous
forge a notification — including "password prompt" — and read every notification body. So the
subscription travels to the **desktop** inside the Noise session, and `/v1/push/send` takes only the
opaque endpoint URL plus ciphertext. The rendezvous holds the VAPID signing key, which authenticates
it to the push service and opens nothing.

**(security)** The desktop seals the notification body to the device's **pinned Noise static key**
*inside* the RFC 8291 payload, and the service worker **must** discard any push it cannot open with
that key. Otherwise a forged push still renders.

**(security)** Per-device connect tokens are issued at pairing, so `/v1/connect` is authenticated
per device rather than per desktop, and the concurrent-socket limit is counted per device. Counting
it per desktop lets anyone who learns a `desktop_id` open every slot and keep the owner's own phone
out. The desktop applies its own handshake rate limit and timeout as well, because it cannot rely on
the rendezvous to do it.

Implementation: Python (asyncio, `websockets`, SQLite) in `rendezvous/`, per the design's section 12
decision — the same toolchain `backend/` already requires, so `ci.yml` tests it with the existing
pytest job and a self-hoster needs nothing new. Retention: 7 days of metadata (ids, addresses, byte
counts), no content, no analytics.

Rate limits: 20 pairing rooms/hour and 600 push sends/hour per desktop; concurrent sockets counted
per device. In P2, `/v1/ice` defaults guests to **relay-only** candidates: direct ICE would reveal
the host's address to everyone holding an invite link. The owner's own devices may go peer to peer.

## 9. Notifications

The desktop decides; the rendezvous only forwards. Triggers, all off-by-default-configurable:
agent turn finished after more than 30 s, agent or program waiting for input, password prompt,
command finished after more than 30 s, turn failed, plan ready, subagent finished.

**Presence rule:** the desktop **must not** send a push while that desktop's window is active and
focused — the user is already looking at it.

**(security)** The hub **constructs** push bodies; it never forwards a `NotificationCenter` body,
which already interpolates the pane's `cwd`. Bodies carry no command text, no output, no prompt
text, **no `cwd`, no program name and no OSC-derived pane title** — a terminal title is set by
program output and is frequently the command itself (`ssh prod-db`, `psql customers`), and these
land on a lock screen. Use a user-assigned pane label or an opaque index.

**(security)** The password-prompt push is the one exception worth spelling out. `checkPasswordPrompt`
is a pure termios test, so **any** program can enter that state and print `[sudo] password for …`.
Making it a push trigger means a program the agent ran — perhaps on instructions from a poisoned
repo file — can ring the owner's phone and present a credential prompt out of context. So: the push
names the foreground program, the phone's secure field displays it, the desktop **should** suppress
it entirely for a program the agent spawned rather than one the user's own shell line started, and a
per-pane cooldown stops a loop spamming prompts. The secure field is never opened by the push
payload alone; the client re-queries the pane's live status over the Noise session first.

## 10. Multiplayer additions (P4)

Additive to everything above; the same wire, the same handshake.

| Type | Body |
|---|---|
| `invite_create` | `{pane, role, expires, uses, require_github?}` → `{url}` |
| `knock` | participant → desktop: `{name, identity?}`; the owner admits or refuses |
| `participants` | `{items: [{id, name, role, pane, driving, following}]}` |
| `role_set`, `participant_remove` | owner only |
| `control_request`, `control_grant`, `control_revoke` | one driver per pane; the owner's physical keystroke always wins |
| `guest_prompt` | a participant's `compose`; the owner sees **Approve once** / **Approve always** |

**Guest identity without an account.** Owner decision 4's "Approve always for that guest" binds to
the **device key pinned at join**, not to a login, because decision 3 allows an invite to be a bare
unguessable link.

**(security)** Because of that, "Approve always" is scoped to (guest, pane, session), expires, shows
a persistent indicator while it is in force, and can be revoked in one tap. It is **not offered at
all** for a bare-link invite with no identity: approving one prompt's text is not approving the tool
calls that prompt will make, and decisions 3 and 4 interact badly — an anonymous link-holder would
otherwise get permanent unreviewed access to an agent holding the owner's keys and shell.

**Audit log**, local only, `~/.local/share/relay/remote/audit-YYYY-MM.jsonl`: pairings, joins, role
changes, control handoffs, prompts submitted (text), lines sent by others (text; raw keys as byte
counts), password-field use (redacted, §6.7), revocations. **(security)** Never uploaded, written
0600 inside a 0700 directory as `logs.py` requires of every Relay log, size-capped and rotated.

## 11. Versioning

`proto: 1` in `hello` and `welcome`. A desktop **must** refuse a `proto` it does not implement with
`error {code: "unknown_type"}` and a human-readable message naming the versions it speaks. Within
version 1, capability is negotiated by the `features` list, never by version bumps, so P1 desktops
and P3 clients interoperate at P1's level.

## 12. What P0 leaves to its phase

- The exact CBOR schema for `cells` — written with the first `screen_snapshot` implementation in P2,
  against `relay::Cell` as it is then.
- TURN credentials and the ICE policy — P2.
- The web app's own storage schema — P1, client-side only, not part of this contract.

## 13. Test plan

| What | How |
|---|---|
| Handshake, rekey, key pinning, revocation while offline | Unit tests both sides, plus a cross-implementation vector file so C++ and TypeScript agree |
| Input allow-list and the forbidden-event list | A test that enumerates every RRP type and every worker event name and asserts the classification, so a new event is denied by default |
| `secret_input` never reaches the ring, the queue, logs or replay | Extends `tests/inputpolicy_test.cpp` |
| Resume: in-ring, out-of-ring, new `hub_epoch`, duplicate `msg_id` | Hub tests against a fake transport |
| The screen stream does not disturb the local view | Engine test: hub reads frames while `TerminalView` keeps repainting |
| `historyLines` matches `historyText` and does not move the viewport | `engine/tests/CoreTest.cpp`, both cores |
| End to end | Loopback rendezvous plus a headless browser client in `ci.yml` |

## 14. What exists today (2026-09-17)

P0 is written and a working slice of P1 runs, against a demo pane source rather than the GUI.

| Part | Where | State |
|---|---|---|
| Noise `IK_25519_AESGCM_SHA256` | `remote/noise.py`, `app/noise.js` | Both halves, cross-checked against each other in `tests/test_remote_noise.py` under Node |
| Framing and the relay envelope | `remote/envelope.py`, `remote/ws.py` | Done. WebSocket server and client are standard-library only |
| Rendezvous | `rendezvous/server.py` | Registry with proof of possession, derived desktop ids, pairing rooms, the ciphertext relay, metadata with 7-day retention. Web Push delivery is **not** wired up: `/v1/push/send` accepts and does not deliver |
| Desktop hub | `remote/host.py`, `remote/identity.py`, `remote/panes.py` | Pairing, capabilities, live revoke and downgrade, streams and resume, the event allow-list, rate limits. `PaneSource` is the seam the GUI will implement; `DemoPaneSource` stands in |
| Web client | `app/` | Pairing with the confirmation code, inbox, thread, composer, plan cards, reconnect. Installable; the service worker does not cache |
| Python client | `remote/client.py` | For tests and scripts; also where the client-side pinning rule is tested |
| Dev harness | `remote/cli.py` | `python3 -m remote.cli dev` runs all of it and prints the pairing QR code |

Not implemented, and refused explicitly rather than silently: `history_get`, `keys`, `paste`,
`line`, `control_request`, `control_release` (P2 and P3), and `secret_input`, which stays refused
until the desktop can re-read termios at write time and mint the prompt-bound nonce §6.7 requires.

The desktop endpoint runs as Python today. `RemoteHub` in the GUI (§1) is still the target for P2,
where screen frames come from `TerminalView` in process; for P1, where everything the hub needs
already crosses the GUI↔worker line, the Python host is what the phone talks to.
