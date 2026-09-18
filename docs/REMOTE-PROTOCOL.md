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

**The handshake is written (2026-09-18).** `next_seq` names the frame count the receiver must have
applied when the switch completes — the offer itself included, so an ack pins the boundary exactly.
The desktop answers `transport_switched {effective}` or refuses with `stale_seq`; a refused offer
leaves the session where it was (`Host._on_transport_switch`, tested in `tests/test_remote_host.py`).
No second transport exists yet, which is precisely why the nonce-exactness is worth having in place
before one does.

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
| `error` | both | `{code, message, id?}`. Codes: `unknown_type`, `not_permitted`, `no_such_pane`, `busy`, `rate_limited`, `stale_seq`, `internal`, and from section 10 `not_admitted` and `not_driving` |
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
`status`, `context`, `mode_changed`, `model_changed`, `model_applied`, `model_switch_refused`,
`plan_written`, `recap`, `subagent_started`, `subagent_progress`, `subagent_finished`, `subagent_handoff`.
The client shows the three model events as the desktop does (`↻ X takes over at the next step · Y
is not interrupted`, `→ now on X`, `✗ <the reason, naming both models>`) and keeps a per-pane model
indicator from them (issue 3ES1).

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

| Type | Direction | Body |
|---|---|---|
| `screen_snapshot` | desktop → client | `{pane, seq, rows, cols, alt, cursor, base, history, lines: [<row>]}` — every row |
| `screen_diff` | desktop → client | `{pane, seq, cursor, base, history, lines: [<row>]}` — only rows that changed |
| `screen_get` | client → desktop | `{pane}` — ask for a fresh snapshot after a reconnect |
| `history_get` | client → desktop | `{pane, id, before_row, count}` (`count` ≤ 200) |
| `history` | desktop → client | `{pane, id, from_row, total, lines: [...], more}` |

**The scrollback cursor is absolute.** `before_row` is the row the page ends just below: the reply
holds rows `[before_row - count, before_row)`, numbered from 0 = the oldest line the desktop still
holds. Omitting it (or sending a negative one) asks for the newest page. `from_row` is the row of
the first line returned, `total` is how many scrollback rows there were at the moment of the
answer, and `more` says whether anything older than `from_row` exists. Every line carries its own
absolute `row` as well, so a client de-duplicates by number rather than by arithmetic.

It is absolute rather than counted back from the newest row because a phone pages while the shell
is still printing. A cursor counted from the end moves by however many lines arrived between two
requests, so consecutive pages overlap or leave a hole in the middle of what the person is
reading; an absolute row moves only when a full scrollback ring evicts its oldest line, and then
only for content that is being discarded anyway. A client that finds a page does not join the one
below it starts again from that page, which is what an eviction looks like from the outside.

**The seam.** A screen frame carries `base`, the absolute scrollback row of its first line, and
`history`, how many scrollback rows exist. A client holding a page of history needs `base` to know
where its rows stop: output pushes lines off the live screen into the scrollback, so the live
block starts further down and the rows in between are in neither half. A client must keep its
column contiguous — the row after its last history row is `base` — by fetching what appeared in
between; when the gap is more than a page or two it may leave it until the reader scrolls towards
it, but it may not paint a column with a hole in it. A `base` *below* the end of what a client
holds means the scrollback shrank (a clear, a reset, the alternate screen) and the held rows are
no longer those rows.

For the same reason a client asks for its first page with `before_row: base` rather than by
omitting it: the desktop's own viewport may be sitting back in its scrollback, and the newest page
would then overlap what is already on the client's screen.

The host never sends scrollback unasked, and asking never changes what the desktop shows.

A `<row>` is `{row, segs: [[text, fg, bg, attrs], ...]}`: runs of identical style. The client needs
no index arithmetic — a run carries its own text — and no second emulator. `fg` and `bg` are packed
`relay::CellColor` (high byte the kind, low 24 bits the palette index or RGB) and `attrs` is the
`relay::CellAttr` bitfield. A double-width glyph is sent once and its tail cell is dropped, because
it already occupies two columns in a monospace grid.

RRP/1 sends these as JSON. The CBOR encoding (§3) is reserved for when a measurement says the
volume needs it; a run-length row of text is already far smaller than a cell-per-column array.

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

- **There is exactly one consumer of `VtCore::updateFrame` per session.** It consumes the dirty
  state (`engine/core/VtCore.h`), so a second caller stops the first one seeing changes. In the GUI
  that consumer is `TerminalView` and the hub reads the frame it already produced. In
  `relay-screen-bridge` — the headless PTY used by remote access today — the bridge is the only
  consumer and calls it directly.
- **`history_get` is served by a const `VtCore::historyLines(from, count, out)`**, in both cores.
  It **must not** move the viewport: `scrollViewportToRow` is shared state and would drag the
  desktop user's own screen. It returns the same `relay::Line` the viewport frame carries, so one
  serializer (`engine/tools/ScreenJson.h`) shapes the live screen and history alike.

**Sizing.** The host is authoritative. The client scales its font so the host's column count fits,
and **must not** send its own size. There is no resize message in RRP/1 at all, which is what makes
the Warp mobile-viewer resize bug structurally impossible rather than merely discouraged.

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
  is detected, delivered with the `panes` status change (`secret_nonce` on the pane item, 45 s TTL),
  single use, bound to (pane, foreground pid, prompt generation), and expires in seconds. The
  desktop discards any `secret_input` whose nonce is unknown, spent or stale. A client-chosen nonce
  would bind nothing; **this is implemented**: `Host._items` mints, `Host._on_secret_input` burns,
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

**WebAuthn — decided (2026-09-18): dropped from the threat table.** A user-verification check
inside the web app enforces nothing — hostile JavaScript skips its own `if`, and the desktop, which
is the party that cares, sees no proof — so it is not listed as a mitigation anywhere. What
protects `secret_input` instead is all desktop-side: the per-device switch (off by default, owner
toggled), the desktop-minted single-use nonce, the fresh termios re-check at the moment of the
write, and the rate limit. Binding a WebAuthn credential to the desktop at pairing remains a
possible future hardening for take-over; it is not a claim the protocol makes today.

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
| `/v1/rooms` | POST | Open a room: `{desktop_id, token, ttl}` → `{room, expires_in}`. `expires_in` is the lifetime **granted** — the request's `ttl`, capped at 7 days — not a fixed five minutes, because an invite's room must outlive a pairing room (section 10.2) |
| `/v1/connect` | WS | Both sides attach: `?room=` for pairing, `?desktop=` for a paired device. The server pairs up sockets and copies frames between them |
| `/v1/push/send` | POST | A desktop posts an opaque payload and the endpoint to deliver it to |
| `/v1/ice` | GET | STUN/TURN credentials (P2) |

A room is **not** consumed by its first connection. Single use is a property of the *secret*,
not of the room: the desktop burns a pairing room the moment a secret is proved, and an invite
counts its own uses. That is what lets an invite with `uses > 1` work without a second mechanism
at the rendezvous, and it changes nothing about what this server can see — a room is an id and a
lifetime, and every byte through it is ciphertext.

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

### 9.1 Subscribing

A subscription is made on the phone and travels to the **desktop**, never to the rendezvous (§8).
Any paired device may subscribe: the capability floor is `view`.

```
→ push_subscribe {endpoint, p256dh, auth, key, kinds}   client → desktop
← push_state {subscribed: true, kinds: [...]}           desktop → client (with the request's id)
→ push_unsubscribe {}
← push_state {subscribed: false, kinds: []}
```

| Field | What | Checked |
|---|---|---|
| `endpoint` | The URL the push service gave the browser | `https://`, ≤ 2048 characters, no whitespace |
| `p256dh` | The subscription's public key, base64url | 65 bytes, uncompressed P-256 (`0x04` first) |
| `auth` | The subscription's auth secret, base64url | 16 bytes |
| `key` | The per-device **seal key**, base64url | 32 bytes, AES-256 |
| `kinds` | Which notifications this device wants | a list of the kinds in §9.2; an unknown one is refused with `unknown_type` and the stored list stands |

The desktop keeps all five on the device record (`remote/identity.py`) and nowhere else. A `410`
or `404` from the push service drops the subscription; so does revoking the device.

**The choice of kinds is per device and is made on the device.** Which of these is worth
interrupting you is not something a desktop can decide for a phone — it depends on whose phone it
is and what the day looks like — so the desktop stores the list and does not own it. Sending
`push_subscribe` again **replaces** the list, which is how a phone changes its mind without the
browser asking for permission a second time; absent or empty `kinds` means all of them, so an
older client is notified rather than silenced, and a device that wants none unsubscribes. The
reply carries what is now stored, so a client that reloads reads its own settings back rather than
guessing them.

### 9.2 What a push is made of

Two layers, and the outer one is the standard:

1. the hub builds a body and **seals** it: `nonce (12) || AES-256-GCM(key, body, "relay-push-v1")`,
   where the body is the JSON below and `key` is that device's seal key;
2. the sealed bytes are the plaintext of an ordinary RFC 8291 `aes128gcm` payload, encrypted to
   `p256dh` and `auth`, padded per RFC 8188 (`0x02` delimiter on the last record);
3. the payload goes to `/v1/push/send` with the endpoint. The rendezvous signs it with VAPID and
   posts bytes it cannot read.

The body:

```json
{"v": 1, "kind": "agent_finished", "pane": "<pane id>", "title": "The agent finished",
 "body": "Pane 2 · 1m 31s"}
```

`kind` is one of `agent_finished`, `waiting_input`, `password`, `failed`, `plan` — the same five a
device may ask for in §9.1. The service worker uses it as the notification `tag`, so a second one
of a kind replaces the first.

**(security)** A service worker **must** discard any push it cannot open with the seal key
(`app/sw.js` does). That is what makes a forged "password prompt" from a compromised rendezvous
impossible rather than merely unlikely: the rendezvous has the VAPID key, which authenticates the
sender to the push service and opens nothing.

### 9.3 The rules the hub applies

`remote/notify.py`, in this order: the presence rule, then whether any subscribed device asked for
this kind (a kind nobody wants does not spend the cooldown), then a **per-pane cooldown of 60 s**,
then the body, which is sent only to the devices that asked for that kind. A pane is named by an ordinal this desktop assigned ("Pane 2") and never by its title.

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

**Implemented (2026-09-18).** Five triggers, being the ones with an event to hang on: an agent turn
that finished more than 30 s after its `agent_started`, a pane whose status became `waiting_input`,
a pane whose status became `password`, a turn that failed (the worker's `error`), and
`plan_written`. A command that outlasted you and a subagent finishing are not wired: the first has
no event and the second would need a second cooldown of its own. The presence signal is a
`window_active {active}` line from `src/RemoteShare.cpp` to the sidecar, driven by
`QGuiApplication::applicationStateChanged`; a hub nobody tells — `python3 -m remote.cli share`, the
tests — has no window to be looking at and pushes. The password trigger is suppressed when the pane
reports that the agent, not the person, has the keyboard.

The "configurable" part of the first paragraph is §9.1's `kinds`: a checkbox per kind under the
"Notify me on this phone" row in the app, remembered across reloads. They start on rather than off,
because a notification you have never seen is not one you can decide about, and turning one off is
a tap.

## 10. Multiplayer additions (P4)

Additive to everything above; the same wire, the same handshake. The host desktop is the hub of a
star: every participant has a pairwise Noise session with it, and removing someone means closing
their session. There is no group key.

**Scope of v1.** Invites are bare unguessable links and a guest is known by the device key pinned
when the owner admits them. Accounts (passkeys, GitHub) need the hosted rendezvous and are a
follow-up; nothing below depends on them. The owner's controls (create an invite, admit, change a
role, remove, pause, end) exist **on the desktop only**: the same messages from any remote device,
an owner's own phone included, are refused with `not_permitted`.

### 10.1 Roles and what a participant can reach

| Role | May |
|---|---|
| `owner` | The host user and their own paired devices. Everything in sections 5 to 9 |
| `editor` | See the shared pane; submit agent prompts, which wait for the owner (10.4); ask for terminal control and, while holding it, send `keys`, `paste` and `line` |
| `viewer` | See the shared pane. The default for a new invite |

A participant is **scoped to the panes of their invite**. `panes` lists only those; any message
naming another pane is `not_permitted`, and no event for another pane is ever fanned out to them.
Whatever their role a participant never gets: `secret_input`, `compose` with `agent:false` (the
routing that can reach the shell), `set_mode`, model changes, reset, queue edits of other people's
items, `voice`, `history_get` beyond the shared pane, push notifications, or the device list.

What a participant **may** send is a second allow-list, `GUEST_TYPES` in `remote/wire.py`, keyed by
the role each type needs; everything absent from it is refused, so this is denied-by-default in the
same way `CLIENT_TYPES` is. The list above is written out beside it as `GUEST_NEVER`, with a reason
each, so the refusals are something a reader can find rather than infer. `agent_stop`,
`queue_remove` and `recap_request` are refused for the same reason: they are not among an editor's
two actions, and they spend the owner's provider key or edit somebody else's queue.

An editor's `keys`, `paste` and `line` are accepted only while that editor holds the pane's control
token; anyone else's are refused with `not_driving` (10.3). Until control handoff exists nobody but
the owner ever holds it, so today all three are refused with `not_driving` — which is the honest
answer rather than a placeholder: a role alone never reaches the keyboard.

The desktop-minted password nonce that rides on a `panes` item (6.7) is **stripped** on its way to a
participant. A guest is never offered the password field, so they are never handed the permit
either.

Agent events reach a participant under a **narrower allow-list** than section 6.4's,
`GUEST_EVENTS` in `remote/wire.py`, a strict subset of `FORWARDED_EVENTS` enforced by a test: the
turn lifecycle, `status`, `queued`, `queue_changed` and `error`. The agent's words already reach
them on the screen, because Relay prints them into the terminal; stored transcripts and tool
output (`turn_transcript_get`, `tool_output_get`) are refused, since they hold every file the
agent read.

### 10.2 Invites and knocking

The invite link is `<app>/join#v=1&d=<desktop public key>&i=<invite secret>&r=<room>`. The secret is
128 bits, lives in the fragment, and is compared in constant time. An invite records
`{id, panes, role, expires, uses_left}`; expiry defaults to 24 hours and is capped at 7 days, which
is why a rendezvous room may be opened with a `ttl` (section 8) — the room is opened with the
invite's own lifetime, so a week-long link does not point at a room forgotten after five minutes.
Five wrong secrets burn the invite, as with pairing. The record keeps a **hash** of the secret and
never the secret: the plaintext exists once, in the link the owner hands out, so a restarted desktop
can still admit someone holding the link and can no longer re-display the link itself.

An invite link is not a pairing link and cannot be used as one: `pair_prove` on a channel whose room
belongs to a live invite is refused. No message on an invite channel reaches the paired-device
list.

| Type | Direction | Body |
|---|---|---|
| `knock` | participant → desktop, first message after the handshake | `{invite: <base64url secret>, name, platform}` |
| `knock_pending` | desktop → participant | `{code}`: the five-digit code of section 5, shown on both screens |
| `admitted` | desktop → participant | `{participant, role, panes, desktop_name, expires, hub_epoch, code, desktop: {id, name, fingerprint}}`, followed by `panes` and `participants` |
| `error` `not_admitted` | desktop → participant | The owner refused, or did not answer in 2 minutes. The channel closes |

A knock consumes nothing until it is admitted; admitting takes one use. On `admitted` the desktop
stores a **participant record** (the pinned device key, name, role, panes, invite id, expiry)
beside the device list, so the guest reconnects with an ordinary `hello` until it expires. A
participant record is never a device record: it cannot appear in, or be promoted through, the
paired-device list. Names pass through `clean_label`, and a second "alice" is shown as "alice (2)".
Knocks are rate-limited per invite (5 a minute) and at most 3 wait at once.

That last rule is **structural** in `remote/guests.py`, not a convention: the two lists are
different classes in different files (`guests.json` beside `devices.json`) whose **field names
differ**, so a row of one kind loaded as the other raises and is dropped rather than half-read;
admitting refuses a key the device store already knows; and the handshake looks a static key up in
the device store first, so a key is a device *or* a participant and never both. There is no function
anywhere from a role to a capability.

**The reconnect.** A participant's `welcome` carries `{participant, role, panes, expires}` and
deliberately **no `capability` and no `password_entry`** — those belong to a device record and a
guest has none — followed by the scoped `panes` and a `participants` list for each pane. A guest
whose record expired, who was removed, or whose share ended gets `bye {reason, discard: true}` and
the channel closes; `discard` is the instruction to forget the record rather than retry. That answer
is only given for a key this desktop pinned itself, so it tells an attacker nothing: an entirely
unknown key still has the handshake refused with no explanation at all.

### 10.3 Presence and control

| Type | Direction | Body |
|---|---|---|
| `participants` | desktop → everyone on the pane | `{pane, items: [{id, name, role, driving, you}]}`, sent on join, leave, role change and handoff |
| `control_request` | editor → desktop | `{pane}`, as section 6.6. For a participant it is a request, not a grant |
| `control_pending` | desktop → that editor | `{pane}` while the owner decides; lapses after 60 s |
| `control` | desktop → everyone on the pane | `{pane, holder: "owner" \| "agent" \| "participant:<id>", name}` |
| `control_release` | holder → desktop | `{pane}`, as section 6.6 |

One driver per pane. It is the same token as the human/agent handoff (`ARCHITECTURE.md` section 9),
so "the agent is driving" and "alice is driving" are one state. **The owner's physical keystroke
in the pane always takes control back**, without asking, and the participant is told with
`control`. Input from anyone who is not the holder is refused with `not_driving`. A participant's
input is refused at a password prompt exactly as a device's is, and they are never offered the
password field.

### 10.4 Guest prompts

An editor sends an ordinary `compose`. The hub does not pass it on: it answers `prompt_pending
{id}` and asks the owner, who sees the guest's name and the whole text. `prompt_decided {id,
approved}` follows; an approved prompt goes to the pane's agent (never the router) with origin
`guest:<participant>`, and its queue row names its author. A pending prompt lapses after 10
minutes, a guest may have 3 waiting, and `plan_execute` from a guest is treated as a prompt.

The owner's other option, *guest prompts run immediately*, is per share and off by default.

### 10.5 The owner's controls (desktop only)

Between the GUI and its sidecar (`remote/gui_host.py`), as line JSON, never on the wire:
`invite_create {pane, role, expires, uses}` → `invite {id, url, qr}`; `invite_revoke {id}`;
`knock {participant, name, platform, code, role, pane}` → `knock_answer {participant, admit, role}`;
`participants {items}`; `control_ask {pane, participant}` → `control_answer {pane, participant,
grant}`; `control_take {pane}` (the owner's keystroke); `prompt_ask {id, participant, pane, text}` →
`prompt_answer {id, approve}`; `role_set {participant, role}`; `participant_remove {participant}`;
`share_pause {on}`; `share_end`.

These names are `OWNER_ONLY` in `remote/wire.py`, which is folded into `NEVER_FROM_CLIENT`, so the
same message arriving over the wire from any device — the owner's own paired phone included — is
refused `not_permitted` by the check every inbound message already passes through, and the existing
"every type is classified" test covers them.

**Pause** refuses every participant's input and prompts with `paused` while the screen keeps
streaming. **End** closes every participant session, burns the pane's invites and deletes the
participant records. Removing one participant does the same for that one, and burns the invite
they came in on.

A removed participant's row is kept, marked removed and holding no panes, until its own expiry
passes — at most the 7 days of the invite — and is then dropped. It grants nothing: every lookup
returns live records only. It exists so a guest whose phone was asleep when they were removed is
*told* their access ended, rather than having a socket close on them for no stated reason.

With *guests can act only while I am present* on, the hub treats an inactive desktop window (the
`window_active` line of section 9) as a pause.

**Guest identity without an account, and why v1 has no "Approve always".** Owner decision 4's "Approve always for that guest" binds to
the **device key pinned at join**, not to a login, because decision 3 allows an invite to be a bare
unguessable link.

**(security)** Because of that, "Approve always" is scoped to (guest, pane, session), expires, shows
a persistent indicator while it is in force, and can be revoked in one tap. It is **not offered at
all** for a bare-link invite with no identity: approving one prompt's text is not approving the tool
calls that prompt will make, and decisions 3 and 4 interact badly — an anonymous link-holder would
otherwise get permanent unreviewed access to an agent holding the owner's keys and shell.

Every v1 invite is a bare link, so v1 offers **Approve once** only; "Approve always" arrives with
invites that require an identity.

### 10.6 Audit log

Local only, `~/.local/share/relay/remote/audit-YYYY-MM.jsonl`: pairings, invites made and revoked,
knocks, admissions and refusals, joins and leaves, role changes, control handoffs, prompts submitted
and how they were decided (text), lines sent by others (text; raw keys and pastes as byte counts),
password-field use (redacted, §6.7), pause, end, revocations. Each line names who, by participant or
device id. It is written **before** the action it records.

The kinds, as written: `invite_create`, `invite_revoke`, `knock`, `knock_refused` (a wrong or spent
secret), `refused`, `admitted`, `join`, `leave`, `role_set`, `participant_remove`, `share_end`,
`guest_prompt`, `control_request`. Every one of them carries the participant id, which is minted at
the knock, so a refusal and an admission name the same person as the join and the leave that follow.
The one exception to *before* is `admitted`: admission is also where a key that is already a paired
device is refused, so a line written first would record an admission that did not happen. It is
written the moment the record exists and before the guest is told anything.

**(security)** Never uploaded, written
0600 inside a 0700 directory as `logs.py` requires of every Relay log, and size-capped — but not
rotated the way `logs.py` rotates, because nothing in it is ever deleted: a month past 5 MiB goes on
in numbered parts (`audit-YYYY-MM.2.jsonl`, `.3`, …), each capped, so files grow with volume, not time.

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
| Pages join with no hole and nothing repeated while the shell prints | `tests/test_remote_terminal.py` (the bridge, and through the hub over a real shell) |
| A page reaches the device that asked for it, and a wedged desktop is an error | `tests/test_remote_gui_host.py` |
| Dragging the terminal down on a phone pages history in, styled, and new output moves nothing | `tests/test_remote_browser.py` |
| The column stays contiguous across the seam while output arrives, small burst and large | `tests/test_remote_browser.py` |
| Every frame carries `base`, a diff included, and a late joiner's snapshot remembers it | `tests/test_remote_gui_host.py` |
| End to end | Loopback rendezvous plus a headless browser client in `ci.yml` |

## 14. What exists today (2026-09-18)

P0 is written; P1 runs against a demo agent source, and the P2 screen stream and P3 take-over run
against **real shells** — including Relay's own panes, from the share button in the app.

| Part | Where | State |
|---|---|---|
| Noise `IK_25519_AESGCM_SHA256` | `remote/noise.py`, `app/noise.js` | Both halves, cross-checked against each other in `tests/test_remote_noise.py` under Node |
| Framing and the relay envelope | `remote/envelope.py`, `remote/ws.py` | Done. WebSocket server and client are standard-library only |
| Rendezvous | `rendezvous/server.py` | Registry with proof of possession, derived desktop ids, pairing rooms, the ciphertext relay, metadata with 7-day retention. `/v1/push/key` and a `/v1/push/send` that signs with VAPID and delivers bytes it cannot read; rooms may be opened with a `ttl` for invites |
| Desktop hub | `remote/host.py`, `remote/identity.py`, `remote/panes.py` | Pairing, capabilities, live revoke and downgrade, streams and resume, the event allow-list, rate limits. `PaneSource` is the seam the GUI will implement; `DemoPaneSource` stands in |
| Web client | `app/` | Pairing with the confirmation code, inbox, thread, composer, plan cards, reconnect. Installable; the service worker does not cache |
| Python client | `remote/client.py` | For tests and scripts; also where the client-side pinning rule is tested |
| Dev harness | `remote/cli.py` | `python3 -m remote.cli share` shares a real shell; `dev` runs the demo agent. Both print the pairing QR |
| Screen stream (P2) | `engine/tools/ScreenBridge.cpp`, `remote/terminal.py`, `app/screen.js` | A real PTY parsed by Relay's own emulator, streamed as styled rows, painted as a cell grid on the phone |
| Scrollback (§6.5) | `engine/core/VtCore.h` (`historyLines`), `engine/tools/ScreenJson.h`, `engine/tools/ScreenBridge.cpp`, `remote/terminal.py`, `remote/gui_host.py`, `src/RemoteShare.cpp`, `app/screen.js` | Paging by absolute row, from the bridge and from a GUI pane. Every frame carries `base`, so the client keeps the seam between its history and the live block closed while output arrives. Pages are fetched one ahead of the reader and de-duplicated by row; output arriving while somebody is scrolled back moves nothing and offers a way to live instead; at most 2000 rows are kept on the phone. History is painted by the run painter the live screen uses, because both ends of the wire go through one serializer |
| Take-over (P3) | `remote/host.py`, `app/app.js` | `keys`, `paste`, `line`, `control_request`/`control_release`, an extra-keys row and a line box, refused at a password prompt |
| Password entry (§6.7) | `remote/host.py`, `src/Pane.h` (`submitRemoteSecret`), `app/app.js` | A desktop-minted single-use nonce bound to the prompt, a per-device switch that is off by default, a fresh termios check in the hub and again at the write, a password field in the client. Tested; not yet tried on a real phone |
| Notifications (§9) | `remote/push.py`, `remote/notify.py`, `remote/host.py`, `app/app.js`, `app/sw.js`, `src/RemoteShare.cpp` | Connected end to end. `push_subscribe`/`push_unsubscribe` inside the Noise session, five triggers with a checkbox each on the phone, the presence rule over `window_active`, a per-pane cooldown, constructed bodies, a 410 or a revoke dropping the subscription, and a "Notify me" row that asks permission from a tap. RFC 8291 is checked against the RFC's own Appendix A vector, `app/sw.js` opens a Python seal under Node, and a local push service takes a real delivery (`tests/test_remote_push.py`). **Not yet tried on a real phone**: that needs the hosted rendezvous reachable from the push service |
| Audit log | `remote/audit.py` | Local, 0600, split by month and size. Records pairing, revoke, prompt detection and password use so far |
| `transport_switch` (§2) | `remote/host.py`, `remote/client.py` | The handshake, tested. There is no second transport yet |
| Local attach | `remote/attach.py` | The desktop's own terminal joins the same shell, so both ends drive it |
| In the app | `src/RemoteShare.{h,cpp}`, `remote/gui_host.py` | The share chip beside the microphone, the QR and approval dialog, and a sidecar that carries one of Relay's own panes (`ARCHITECTURE.md` section 19) |
| Voice (§6.4) | `app/app.js`, `remote/gui_host.py`, `src/Pane.h` (`transcribeForRemote`) | A `MediaRecorder` clip from the phone, carried to the pane and transcribed by its own worker on the desktop's key; the text returns to the phone's prompt box, matched to the clip by id. Tested through a headless browser with Chrome's fake capture device; not yet tried with a real microphone on a real phone |

Every Relay pane is an engine pane, so every pane can be shared.

Section 10 is part built. What exists (2026-09-18): guest identity and the participant store
(`remote/guests.py`, `guests.json` beside `devices.json`), invite links and their rendezvous rooms,
knock-to-admit with the same five-digit code as pairing and a two-minute refusal, reconnect by the
pinned key, pane scoping and the `viewer`/`editor` roles enforced at every inbound message and every
fan-out, the `GUEST_EVENTS` allow-list, the owner's controls as desktop-only sidecar lines and as
`python3 -m remote.cli share --invite`, and the audit lines of 10.6 (`tests/test_remote_guests.py`).

What is **not** built, and is the next piece: presence beyond the initial `participants` list,
control handoff (10.3) — so an editor's `keys`, `paste` and `line` are refused `not_driving` — guest
prompt approval (10.4) — so an editor's `compose` is answered `prompt_pending` and parked in a queue
nothing drains but its ten-minute expiry — `share_pause`, and the desktop UI and web client for any
of it. The two seams are `Host.ask_owner_about_prompt` and `Host.ask_owner_about_control`.

`history_get` now works from both sources. The engine's `VtCore::historyLines` is const and moves
nothing — not the viewport, the dirty state, the selection or the search — so a phone paging back
cannot scroll the screen the owner is looking at, and the same `screenjson::rowOf()` that shapes a
live row shapes a history row, so the two cannot drift. Over a real shell the hub puts the request
to `relay-screen-bridge` under a per-pane lock, so two devices paging at once queue rather than
read each other's pages; from a GUI pane the sidecar sends a `history` line and waits for the
answer by an id it minted, and a desktop that never answers is an error the phone shows rather
than a scroll gesture held open. `welcome` advertises `history` only when the source has
scrollback behind it.

`voice` from a GUI pane now works: the sidecar sends the clip to the GUI as a `voice` line with an
id it minted, the pane writes it to a 0600 temp file whose name and extension it chooses itself and
hands it to the same worker `transcribe` request its own microphone uses, and the `transcribed`
answer comes back by that id. A remote transcript never reaches the desktop's prompt box, and the
desktop's own is never sent to a device. A clip the GUI does not answer within 90 seconds is an
error the phone shows, not a spinner that never stops.

The desktop endpoint runs as Python today. `RemoteHub` in the GUI (§1) is still the target for P2,
where screen frames come from `TerminalView` in process; for P1, where everything the hub needs
already crosses the GUI↔worker line, the Python host is what the phone talks to.

## 15. Two ordering bugs worth remembering

Both were found by running the thing, not by reading it, and both produce the same symptom: a
session that dies the moment traffic gets busy.

**Encrypt and write are one step.** A Noise cipherstate is a counter. Fan-out spawns a send per
channel, so two sends can encrypt in one order and reach the socket in another; the peer then sees
a nonce gap and, correctly, drops the session. The desktop holds a per-channel lock across encrypt
and write; the client chains its sends on a promise.

**A nonce is reserved before the first await.** WebCrypto is asynchronous, so a client that reads
`this.nonce`, awaits `subtle.encrypt`, and then increments will hand two frames the same nonce as
soon as two calls overlap. The counter is incremented synchronously, before any await, and incoming
frames are processed in arrival order for the same reason.

Neither is visible at one message per second. A 20 fps screen stream finds them immediately.
