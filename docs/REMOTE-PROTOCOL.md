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
   The `paired` that follows carries the device's **connect token** (§8): minted by the desktop
   for this device record, handed over inside the Noise session, stored with the record, and
   presented to the rendezvous on every later `/v1/connect`. It is sent again in every
   `welcome`, so a record that has none takes it from there.
4. The secret is single-use and valid 5 minutes. **(security)** A second `pair_prove` for the same
   room **must** fail even if the first was refused by the user.

### 5.2 Pairing code (no camera)

**Built** (card `#FR1C`), as **4 letters and a 4-digit PIN** over CPace — the machinery §10.7
already describes, pointed at pairing instead of at an invite. A phone that has never seen this
desktop has no camera aimed at the screen and nowhere to paste a 140-character link; it has a
keyboard, so `ABCD` and `4829` is what it is asked for.

The desktop mints the code with `Host.pair_code()`: a pairing room of its own (§5.1's room and
128-bit secret, ttl 600 s), a meeting code for it, and a PIN. What a correct PIN earns, sealed
under the CPace key, is that room's **pairing fragment** `v=1&d=…&s=…&r=…` — the same string the
QR holds. From there §5.1 step 2 onward runs unchanged: Noise IK against `d`, `pair_prove` against
`s`, the five digits on both screens, the capability the owner allows, the connect token in
`paired`. Everything §10.7 says about codes holds: ten minutes, one use, the desktop counts
failures and the third burns the code, the rendezvous keeps only the code → room map and answers
an unknown code exactly as it answers an expired one.

Two rules are this section's own:

* **The pairing room is minted for the code and is in no QR.** A room behind both would let a scan
  and a typed code race for its single use, and the loser would be told its secret was wrong. One
  live pairing code at a time; a new one burns the old, and burning a code burns its room, so a
  fragment that outlives its code opens nothing.
* **A pairing code and an invite code are not interchangeable**, and the *fragment's own shape*
  decides which is which — `s=` is a pairing secret, `i=` an invite secret (`pairing.fragment_kind`).
  A client routes on that, never on the code or the page it was typed into. The desktop then
  refuses the mismatch a second time on the wire: `pair_prove` on an invite room is
  `not_permitted` ("that is an invite link, not a pairing code; knock instead"), and a `knock` on
  a pairing room finds no invite there.

The earlier sketch for this section — three words from a 2 048-word list — is not what was built;
five attempts per room (`pairing.MAX_ATTEMPTS`) still bounds `pair_prove` itself.

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
| `welcome` | desktop → client | `{desktop: {id, name, fingerprint}, proto: 1, capability, password_entry, connect_token, hub_epoch, features: [...], server_time}` |
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
| `paired` | desktop → client | `{device_id, capability, connect_token, desktop: {id, name, fingerprint}}` — sent only after the user allows; `connect_token` is §8's |
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

**A few events have a capability floor** (`EVENT_FLOOR` / `floor_for` in `remote/wire.py`, read by
the fan-out per device, as the allow-list is). `sessions`, `conversations` and `conversation` name
**other conversations**, so they are `full` — the owner's level in the three of section 16, where a
viewer observes this conversation and a partner types in it. Everything else is `view`. The rule has
to hold in both streams: `pane_state` drops its `sessions` block below `full`, and without a floor
here the same titles would arrive as worker events a moment later.

**A share that carries the screen does not carry tool text** (`SCREEN_REDUNDANT_EVENTS` /
`may_forward_with_screen` in `remote/wire.py`, applied in `Host._agent_event`). When the desktop
advertises the `screen` feature, `tool_output` and `tool_result` are dropped before the ring —
never forwarded, never replayed after a gap — because Relay prints a tool's output into the
terminal the client is already drawing, and the client's transcript renderer is switched off
whenever there is a screen. Measured on a Pixel 8 on 2026-09-20 (card #3H5T): a tool-heavy turn put
**1.33 MB of 1.46 MB** on the air as text the phone parsed and threw away, and three of its
forty-five screen markers painted 1.1–3.1 s late behind that queue, RRP being one ordered stream.
A share with **no** screen — a source with no `on_screen`, the headless/agent-companion shape —
gets both events exactly as before: there the transcript is all the client has. The fold still
works on either: `tool_output_get` (below) answers from the desktop's own store and is untouched.
A future client that wants a transcript *beside* a screen asks for the text, and
`may_forward_with_screen` is where that opt-in is read.

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
| `screen_diff` | desktop → client | `{pane, seq, cursor, base, history, lines: [<row>], scroll?}` — only rows that changed, and how the rest moved |
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

**A scroll is a shift, not a new screen.** A `screen_diff` may carry
`scroll: {top, bottom, by}`: rows `[top, bottom)` of the frame before it moved **up** by `by`
rows, a negative `by` moving them down, and `lines` then holds only the rows the shift could not
carry over — the ones that entered at the far end, and anything the program changed in the same
frame. The client shifts its own rows and paints `lines` on top, in that order. A frame that is
not describable that way — a resize, a clear or reset, the alternate screen, a colour change, a
first frame, or a client that fell behind — is a `screen_snapshot` as it always was, and a
snapshot never carries `scroll`. `by` is never zero and never as large as `bottom - top`.

It exists because the common case was the expensive one: a line of streamed output at the bottom
of the screen scrolls the viewport, and that used to be sent as every row. A 20 000-character
reply watched on a phone was 166 snapshots of 7 747 B against 86 diffs of 375 B — 1.50 MB for
20 KB of text, and 82 % of one core in Chrome (#3H5T,
`docs/qa_evidence/2026-09-20-perf-fixes/phone/RESULTS.md`).

**A client asks for it; it is not assumed.** `hello` (and a guest's `knock`) carries
`supports: ["screen_scroll"]`. A client that did not ask is sent snapshots, because one that
ignored the field would paint the new rows over rows that had moved. The screen stream's replay
ring is shared, so **one** attached client that did not ask takes the primitive away from all of
them until it leaves, and a resume that would replay a `scroll` to such a client is answered
`-1` — take a fresh snapshot — instead. `supports` never widens what a client may *do*: that is
the capability ladder, and it is granted by the owner, never asked for.

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
| `compose` | `agent` | `{pane, text, when: "now"\|"queue"\|"steer"}`. `steer` aims the line at the running turn's next tool call, as the desktop's own composer does, and is the owner's own devices' at `agent` and above; a guest's prompt waits for the owner to admit it and can never be a steer (`not_permitted`, section 10.4). **(security)** A remote `compose` is **always** routed to the agent. `agent: false` — the composer's shell route — requires `full`. Relay's router honours a `/shell ` prefix and a shell mode, so an `agent` device could otherwise run `curl … \| sh` with no agent in the loop, no take-over and no control token, while the pane's `control` still read `human` |
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

**A client paces itself inside those budgets, and a `rate_limited` is its own bug to fix.** One
request of a kind in flight at a time, wants that turn up meanwhile merged into the next one
rather than sent beside it, and the request sized for what the reader needs rather than repeated
— closing a scrollback seam asks for the whole gap up to the 200-row cap, not a page of it. A
refusal is logged and backed off from, never drawn: a person watching an agent type can do
nothing about a client asking too often, and putting "too many of those; slow down." under their
composer says the desktop is failing when the client is. The measured case is #3H5T: a phone
chasing the seam once per screen frame sent 188 `history_get` in five seconds and showed the
reader 121 refusals.

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
- **(security) a permit dies with the prompt it was minted for.** When a pane stops being at a
  prompt the desktop burns its nonce and advances the pane's prompt generation, so the next prompt
  gets a permit of its own. Letting it live out its 45 seconds made "bound to the prompt
  generation" untrue in the one case that matters: a process that asks twice — `ssh` wanting a key
  passphrase and then a password — kept the same pid and the same generation, so the phone still
  holding the first prompt's permit would have answered the second one on the strength of a
  decision the person made about the first. The security review of 2026-09-18 found this;
  `tests/test_remote_security.py` attacks it,
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
| `/v1/register` | POST | `{static_pubkey, challenge, proof, connect_secret, revoked_tokens}` → `{desktop_id, token, connect_tokens}`. `connect_secret` (32 bytes, base64) is what this desktop's devices' connect tokens are checked against and `revoked_tokens` the token ids that must stop working; both are sent at every registration |
| `/v1/revoke` | POST | `{desktop_id, token, token_ids}` → `{revoked, closed}`. A device revoked now stops working now: the ids join the refused set and their live channels are closed |
| `/v1/rooms` | POST | Open a room: `{desktop_id, token, ttl}` → `{room, expires_in}`. `expires_in` is the lifetime **granted** — the request's `ttl`, capped at 7 days — not a fixed five minutes, because an invite's room must outlive a pairing room (section 10.2) |
| `/v1/connect` | WS | Both sides attach: `?room=` for pairing and invites, `?desktop=&device=&ct=` for a paired device or an admitted participant, `ct` being its connect token. The server pairs up sockets and copies frames between them |
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

**(security) Opaque is not unchecked: `/v1/push/send` posts only to a Web Push service.** The
endpoint is a URL a *phone* chooses and this server then makes an authenticated POST to, and
registering a desktop here proves possession of a key rather than any right to be here — so
without a rule about which hosts, the route is a request-forgery proxy inside whatever network
the rendezvous is hosted on, reachable by anyone who can reach the server. The host must be one of
the browsers' push services (`PUSH_SERVICE_HOSTS`, matched as a name rather than resolved, so
there is no DNS rebind between the check and the request), or one a self-hoster named in
`RELAY_PUSH_HOSTS`. A URL carrying credentials (`https://fcm.googleapis.com@10.0.0.1/…`) is
refused outright. The desktop refuses a literal private, loopback or link-local address when the
phone first offers it (`remote/notify.py`), which is the earlier and friendlier half; this is the
one that binds.

**(security)** The desktop seals the notification body to the device's **pinned Noise static key**
*inside* the RFC 8291 payload, and the service worker **must** discard any push it cannot open with
that key. Otherwise a forged push still renders.

**(security)** Per-device connect tokens are issued at pairing, so `/v1/connect` is authenticated
per device rather than per desktop, and the concurrent-socket limit is counted per device. Counting
it per desktop lets anyone who learns a `desktop_id` open every slot and keep the owner's own phone
out. The desktop applies its own handshake rate limit and timeout as well, because it cannot rely on
the rendezvous to do it.

**Built 2026-09-20** (card #PH0N, Phase 1.3; `remote/pairing.py`, `remote/host.py`,
`rendezvous/server.py`, `app/rrp.js`, `remote/client.py`; `tests/test_remote_security.py`
`ConnectTokenTests`). A `desktop_id` is `SHA-256` of the static key, which is in the fragment of
every pairing and invite link, so anyone who has ever held a link can compute it; what answers
that is a token that names *this device*:

* **The token** is `<token id>.<mac>`: a 72-bit random id kept on the device record (or the
  participant record, §10), and 128 bits of `HMAC-SHA-256` over the length-prefixed triple
  (`desktop_id`, channel id, token id) under the desktop's **connect secret** — 32 bytes derived
  from the identity key (`HMAC(identity_private, "relay/connect-token/v1")`), never stored, and
  therefore the same after a restart and at whichever rendezvous the desktop moves to. The
  channel id is the device id for one of the owner's devices and the participant id for a guest,
  so a token replayed under another name fails, and one minted for desktop A does not open B.
* **Delivery** is inside the Noise session only: in `paired` (§6.2), in `admitted` (§10.2), and
  again in every `welcome`, which is how a record written without one takes it. The rendezvous
  sees a token for the first time when the device presents it as `ct` on `/v1/connect`.
* **The check is stateless per device.** The desktop sends its connect secret with every
  `/v1/register`, and the rendezvous verifies a `ct` with one HMAC against it: no table of issued
  ids, no round trip to the desktop, and nothing to lose at a restart, because the hub registers
  again before every reconnect (§8.1). An HMAC under a secret the rendezvous holds was chosen over
  a signature under the registered key because it costs the rendezvous nothing it did not
  already have: holding the secret lets it mint a token, which buys a channel slot, and the
  rendezvous is the thing that grants channel slots. It is not a content key — every byte on a
  channel is still sealed to the pinned Noise static key, and the hub closes a channel whose
  handshake key it did not pin. A signature would have bought a stateless check at a rendezvous
  that could not mint, which is no property this design needs, at the cost of an asymmetric
  verify per connect and a second key to rotate.
* **A missing or bad token is refused exactly as an unknown desktop is** — the same close code
  (4404) and the same sentence, sent before the desktop's offline state or its budget is looked
  at — so a stranger holding a `desktop_id` learns nothing from the refusal and takes no slot on
  the way. A room (`?room=`) carries no token by design: a pairing link, an invite or a meeting
  code is reached by whoever holds the room id, the desktop decides what that connection is worth,
  and the per-address cap (`MAX_CHANNELS_PER_PEER`) bounds it.
* **Channels are counted per token** — `MAX_CHANNELS_PER_TOKEN`, three: a phone reconnecting over
  a flaky link plus a tab it left open. A fourth is refused with 4429, and no device's use of its
  three touches another's. The per-desktop cap stays as the outer bound.
* **Revoking a device revokes its token.** The id goes on the desktop's revoked list, which every
  registration carries whole (`revoked_tokens`, at most 256, oldest dropped first), and is posted
  at once to `/v1/revoke` at every rendezvous the hub holds a bearer token for; the rendezvous
  refuses the id from then on and **closes its live channels** (4403), so a phone mid-session is
  cut at the rendezvous as well as by the hub's own `revoked`. A device revoked while the
  rendezvous is unreachable is caught by the next registration. Removing a guest revokes theirs
  the same way (§10.5); the row is kept until its own expiry, and afterwards the participant it
  names no longer exists, which the hub answers at `hello` as it answers a stranger.
* **The hub's own registration socket is unaffected**: it attaches as a desktop with its bearer
  token, on the other side of the relay.
* **Token ids never enter the metadata log.** `events` records that a registration carried
  tokens and how many ids were revoked, and that a `/v1/revoke` closed *n* channels — counts,
  never ids — and the revoked ids live in their own table, replaced wholesale at every
  registration, so a copy of seven days of metadata names no device.
* **There is no compatibility shim.** A record without a token — the Python client's file, the
  web client's IndexedDB row or a Relay viewer's record from before 2026-09-20 — gets the same
  refusal a stranger does, and pairing again is the answer. Only the owner's own devices exist
  today, so nothing is served by a rendezvous that lets a tokenless channel through. The one
  concession is on the desktop: a device or participant record loaded without a token id is
  given one at load, so a phone that reaches a `welcome` through a rendezvous not yet redeployed
  is handed its token there and carries on once the rendezvous is.

Implementation: Python (asyncio, `websockets`, SQLite) in `rendezvous/`, per the design's section 12
decision — the same toolchain `backend/` already requires, so `ci.yml` tests it with the existing
pytest job and a self-hoster needs nothing new. Retention: 7 days of metadata (ids, addresses, byte
counts), no content, no analytics.

Rate limits: 20 pairing rooms/hour and 600 push sends/hour per desktop; concurrent sockets counted
per device (three per connect token, 32 per desktop, eight per address for rooms). In P2, `/v1/ice` defaults guests to **relay-only** candidates: direct ICE would reveal
the host's address to everyone holding an invite link. The owner's own devices may go peer to peer.

**The hosted address.** The desktop's sidecar (`remote/gui_host.py`) runs its own in-memory
rendezvous, and the LAN, tailnet and cloudflare addresses in the share dialog are all routes to that
one server. The fourth entry, kind `hosted` and value `relay-terminal.ai`, is a *different*
rendezvous: the public one at `RELAY_HOSTED_RENDEZVOUS` (default `https://join.relay-terminal.ai`),
which serves the same routes and web app. It is offered when `GET /v1/health` answers within three
seconds, and otherwise listed with `available:false` and a one-sentence `reason`. Choosing it (the
ordinary `{"t":"address","value":"relay-terminal.ai"}` line) moves the **one** hub: it registers
there with the same challenge and proof of possession, its socket reconnects there, and the app
base of every new pairing, invite and code link becomes that origin. Choosing any other address
moves it back to the local rendezvous. Rooms, invite rooms and meeting codes live at the rendezvous
that minted them, so a switch ends each live code as `expired` (its invite burns, and the code is
burned at the old rendezvous with the old token), and channels on the old socket close; older
invite links work again only if their rendezvous is chosen again. Nothing about the protocol
changes: the hosted server sees what any rendezvous sees, which is ciphertext.

### 8.1 Always on

Until 2026-09-20 nothing was reachable until a share button was pressed, on one pane, in the Relay
session that happened to be running: the sidecar started on demand, the address was chosen per
share and forgotten afterwards, and the hosted registration went with the process. A phone paired
yesterday that opened the app after a desktop restart saw nothing (card #PH0N, gap A).

**The switch.** Options › Remote holds one on/off, remembered across restarts, and the address it
was last pointed at. Every `start` carries `"always": true|false`, and `"address": <value>` when
one is remembered: `relay-terminal.ai` for the hosted rendezvous, the word `tailscale` for the
tailnet — the word, because the tailnet name belongs to the machine and the setting outlives it,
and the sidecar resolves it against its own probe — or one of this machine's own addresses from
the `addresses` list. `cloudflare` is never an always-on address. The same `start` line arrives
again, verbatim, when the switch is turned on over a sidecar that was already running for an
ordinary share: that is the service coming up, not a re-announcement. On the way down the GUI
sends `unpane` for every pane and then `stop`.

With `always`, the sidecar brings the service up **there**, with no share having been asked for:

* it **publishes every pane**. The GUI sends the `pane` and `unpane` lines it already sends, for
  every pane with a screen rather than for the one that was shared. Guests are untouched: a
  participant's every message goes through `Host.guest_view`, which cuts a `panes` list down to
  the panes of their invite and drops anything naming a pane they are not on, so publishing more
  shows a guest nothing more. A device that connects while there is nothing open is sent an empty
  `panes` list and then the updates, rather than silence;
* it **stays registered**. The hub's outbound socket reconnects for ever — one second, doubling to
  a minute, jittered — and **registers again before each retry**, because a rendezvous that
  restarted has forgotten the token it issued and would refuse the same token, identically, until
  the app was restarted (`Host.serve`, `Host._register_again`);
* it **never falls back on its own**. A desktop told to be at relay-terminal.ai that quietly
  became reachable only on its own LAN is a desktop the phone cannot find, with nothing on either
  screen saying so. A rendezvous that is down is reported as down, in one sentence, and tried
  again.

**`remote_state`** is what the desktop draws its indicator from. The sidecar emits it whenever any
field changes, and it is the only message about the service as a whole:

```json
{"t":"remote_state","on":true,"address":"relay-terminal.ai","base":"https://join.relay-terminal.ai",
 "online":true,"devices":1,"reason":""}
```

`on` — the always-on service is up. `address` — the **picker's** value, not this machine's
resolved name: a switch set to `tailscale` reads back as `tailscale`, including while the service
is still on its way there, because that is what the person chose and the tailnet name belongs to
the machine rather than to the setting. `base` — the origin every new link carries. `online` —
registered *and* the socket up *at the chosen address*; a hub that is still at its own rendezvous
because the hosted one refused is on, and not online. `devices` — how many of the owner's own
paired devices hold a live channel right now, counted by device rather than by channel, and never
counting guests. `reason` — one sentence while `online` is false, and `""` while it is true.
`stop` turns the service off, drops the registration and emits a last `remote_state` with
`on:false`.

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
| `endpoint` | The URL the push service gave the browser | `https://`, ≤ 2048 characters, no whitespace, no credentials in the URL, and not a literal private, loopback or link-local address. The rendezvous checks it again and harder (§8) |
| `p256dh` | The subscription's public key, base64url | 65 bytes, uncompressed P-256 (`0x04` first) |
| `auth` | The subscription's auth secret, base64url | 16 bytes |
| `key` | The per-device **seal key**, base64url | 32 bytes, AES-256 |
| `kinds` | Which notifications this device wants | a list of the kinds in §9.2; an unknown one is refused with `unknown_type` and the stored list stands |

The desktop keeps all five on the device record (`remote/identity.py`) and nowhere else. A `410`
or `404` from the push service drops the subscription; so does revoking the device.

**Each device's pushes go through its own rendezvous.** A browser subscribes under the VAPID key
of the rendezvous it fetched `/v1/push/key` from, and only that server's signature is accepted for
that subscription. So the device record keeps an `origin` — the rendezvous it paired through, then
the one each `push_subscribe` arrived through — where `""` is the desktop's own local rendezvous
(also what a record written before the field means), and the hub posts `/v1/push/send` there,
whichever rendezvous it is registered with at the moment (§8, the hosted address). A push whose
origin cannot be reached is dropped, never queued, and logged once by origin, never by endpoint. A
phone that connects through a rendezvous whose key is not the one its subscription was made under
renews the subscription under the new key and sends `push_subscribe` again, which moves its origin
(`app/pushkey.js`); permission is already granted, so nothing is asked.

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
   posts bytes it cannot read, as `Content-Encoding: aes128gcm` — RFC 8291 §4 requires the header,
   and a push service that is not told what it is holding answers 400. A self-test never sees
   that, which is why it went missing until the security review of 2026-09-18.

The body:

```json
{"v": 1, "kind": "agent_finished", "pane": "<pane id>", "title": "The agent finished",
 "body": "Pane 2 · 1m 31s"}
```

`kind` is one of `agent_finished`, `waiting_input`, `password`, `failed`, `plan` — the same five a
device may ask for in §9.1 — or, since #SWPH, `card_waiting`, whose trigger, body and rules are
§17.5. The service worker uses it as the notification `tag`, so a second one
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

**The unit of sharing may be a tab** (owner, 2026-09-18: "share whole tab… so you can add more
panes and they immediately get access to the workspace"). The desktop gives the tab page an id; the
GUI's `pane` line carries it as `tab` for every pane shared under it, and an invite made with
`tab` names every shared pane the tab holds and grows with it. What that means for later panes:

* a pane added to the tab (split off, or moved in) is shared at once and **joins the scope** of
  every live invite and participant of that tab. The hub records `scope_grown` (10.6) per
  participant and per invite *before* the store changes, and changes it before the `panes` list
  announcing the pane is filtered, so a guest is never sent a list without a pane they now hold.
  The desktop is never silent about it: the new pane's share button is lit from its first frame and
  a toast says the tab's guests can see it;
* a pane that **leaves** the tab — closed, or moved to a tab that is not shared whole — leaves
  their scope at once (`scope_shrunk`), with any control token, pending control request and
  pending prompt of theirs on it. `may_see` reads the live record, so its next frame is already
  refused; a participant left with no pane is removed and their invite burned, as when a share ends;
* nothing else widens. An invite without `tab` stays exactly the panes it names even when they sit
  in a shared tab, a guest of one tab never gains a pane of another, and growth stops at
  `MAX_PANES_PER_TAB` (32). Every rule above applies to a pane that joined later exactly as to the
  first — a later pane's password prompt is refused to a guest the same way.
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
token; anyone else's are refused with `not_driving` (10.3). A role alone never reaches the keyboard:
`editor` is permission to *ask*, and the owner hands it over one pane at a time.

The desktop-minted password nonce that rides on a `panes` item (6.7) is **stripped** on its way to a
participant. A guest is never offered the password field, so they are never handed the permit
either.

Agent events reach a participant under a **narrower allow-list** than section 6.4's,
`GUEST_EVENTS` in `remote/wire.py`, a strict subset of `FORWARDED_EVENTS` enforced by a test: the
turn lifecycle, `status`, `queued`, `queue_changed` and `error`. The agent's words already reach
them on the screen, because Relay prints them into the terminal; stored transcripts and tool
output (`turn_transcript_get`, `tool_output_get`) are refused, since they hold every file the
agent read.

`question` and `question_closed` (the ask of sessions protocol 27) are forwarded and are not in
`GUEST_EVENTS`: a share participant sees a question only as the text the desktop printed into the
mirrored terminal. **The phone's pane view draws them** (`app/pane.js`, card #PH0N, 2026-09-20):
the questions one at a time above the prompt box, in the worker's own words, one button per
option with the recommended one marked, a Skip (the desk's `/skip`), and the prompt box for the
person's own words. A tap or a line typed under the ask goes as an ordinary `compose` **without**
`agent: false`, so the desktop's ask takes it before anything is routed (`Pane::submitRemote`):
a choice the model labelled "git status" is an answer, not a command. A `view` device reads the
question and is offered no button. Between questions the view steps by itself — the worker
sends no event per question — and `question_closed`, or the turn ending, takes the ask down.
The owner can still answer from a paired device: a remote line is routed first, exactly as a line
typed at the desk is, and the ask takes it when the router sends it to the agent
(`relay::input::askTakesRemoteLine`, `Pane::takeRemoteRoute`). A line the router sends to the
**shell** runs in the shell — an ask nobody has answered no longer locks a phone out of the
terminal, which is what the old rule did by handing the ask every line before routing it (owner,
2026-09-19). A device that cannot ask the router at all — `route: false`, or a worker that is not
up — can only reach the agent, so its line goes to the ask as before, and a `when: "steer"` line
is agent-bound by the sender's own choice and goes to the ask too. There is no `question_answer` a
guest may send; a guest's `compose` that the owner approves does reach the same path, which is the
part of this still to be decided.

**Outbound is an allow-list too.** `GUEST_SERVER_TYPES` in `remote/wire.py` names every
desktop→client **type** a participant may receive, and `Host.guest_view` drops anything else
before it is encoded. Denied-by-default in the same way `GUEST_TYPES` is, and for a sharper
reason: a new desktop→client message is added for the owner's own client, so without this a
`pane_state` or a `queue_edit_text` carrying the owner's models, sessions and queue text would
reach every guest on that pane the day it landed, rather than the day somebody decided it should.
Absent on purpose: `paired` and `revoked` (a guest has no device record), `push_state` and
`transport_switched` (a guest can ask for neither).

**A queue event carries only this guest's own text.** `queued` carries the prompt and
`queue_changed` a preview of every waiting item, so the rows of a shared queue are filtered per
recipient: a guest keeps their own rows whole — matched on the `guest:<id>` origin the hub itself
puts on an approved prompt — and sees everyone else's as a row with no text and an `author`
(`"you"`, a guest's name, or `"the owner"`). A share with two editors is otherwise a way to read
what the other one is asking for, and what the owner is.

**(security)** The words are stripped by one list of field names — `text`, `prompt`, `preview`,
`label` — applied to both events. Each had its own shorter list until the review of 2026-09-18,
which made the rule depend on which field the *producer* happened to use: `backend/relay_core/queue.py`
puts the words in `preview` on a queue row, the demo source puts them in `text` on a `queued`, and
a producer that put a `preview` on a `queued` would have handed every guest the owner's prompt on
the day it was written.

### 10.2 Invites and knocking

The invite link is `<app>/join#v=1&d=<desktop public key>&i=<invite secret>&r=<room>`. The secret is
128 bits, lives in the fragment, and is compared in constant time. An invite records
`{id, panes, role, expires, uses_left}`; expiry defaults to 24 hours and is capped at 7 days, which
is why a rendezvous room may be opened with a `ttl` (section 8) — the room is opened with the
invite's own lifetime, so a week-long link does not point at a room forgotten after five minutes.
Five wrong secrets burn the invite, as with pairing. The record keeps a **hash** of the secret and
never the secret: the plaintext exists once, in the link the owner hands out, so a restarted desktop
can still admit someone holding the link and can no longer re-display the link itself.

**A public link admits as many people as the owner asked for** (owner, 2026-09-18). The address
the app is served from does not change what an invite grants: `uses` is honoured over a cloudflared
quick tunnel exactly as on the LAN or the tailnet, because one link sent to a group is the point of
having a public link at all. A one-use clamp was built for that case and removed the same day; it
is recorded here so that nobody adds it back as an obvious improvement. What the `invite` reply
carries over a tunnel is a warning rather than a restriction: *"Over a public link, anyone this
link is forwarded to can knock. You admit each person by hand."*

**Admission is always by hand, and the owner is told.** There is no auto-admit: `admit_nobody` is
the hub's default approver, and every guest knocks and is let in by the owner, with a role. That is
what makes a multi-use or leaked link survivable — the link gets someone to the door, not through
it. The owner is emailed the first time each guest joins (`remote/email.py`, `notify` in
`~/.config/relay/email.json`), once per person and never on a reconnection, so a link that admits
five people does not mean five arrivals nobody saw. Any future away-mode that admits without a
person watching must refuse while a tunnel is up.

An invite link is not a pairing link and cannot be used as one: `pair_prove` on a channel whose room
belongs to a live invite is refused. No message on an invite channel reaches the paired-device
list.

| Type | Direction | Body |
|---|---|---|
| `knock` | participant → desktop, first message after the handshake | `{invite: <base64url secret>, name, platform}` |
| `knock_pending` | desktop → participant | `{code}`: the five-digit code of section 5, shown on both screens |
| `admitted` | desktop → participant | `{participant, role, panes, desktop_name, expires, hub_epoch, code, connect_token, desktop: {id, name, fingerprint}}`, followed by `panes` and `participants`. `connect_token` is §8's, minted for the participant id |
| `error` `not_admitted` | desktop → participant | The owner refused, or did not answer in 2 minutes. The channel closes |

A knock consumes nothing until it is admitted; admitting takes one use. On `admitted` the desktop
stores a **participant record** (the pinned device key, name, role, panes, invite id, expiry)
beside the device list, so the guest reconnects with an ordinary `hello` until it expires. A
participant record is never a device record: it cannot appear in, or be promoted through, the
paired-device list. Names pass through `clean_label`, and a second "alice" is shown as "alice (2)".
Knocks are rate-limited per invite (5 a minute) and at most 3 wait at once. An invite delivered by a meeting
code (§10.7) is stricter: its first knock claims it, and every later knock is refused.

That last rule is **structural** in `remote/guests.py`, not a convention: the two lists are
different classes in different files (`guests.json` beside `devices.json`) whose **field names
differ**, so a row of one kind loaded as the other raises and is dropped rather than half-read;
admitting refuses a key the device store already knows; **and pairing refuses a key the guest store
already knows**; and the handshake looks a static key up in the device store first, so a key is a
device *or* a participant and never both. There is no function anywhere from a role to a capability.

**(security)** Both directions of that refusal are needed, and until the review of 2026-09-18 only
one existed. `pair_prove` went straight to `DeviceStore.pair`, which cannot see the guest list, so a
guest's pinned key could also become a device record — and because the handshake reads the device
store first, their next connection would have been that guest holding a *capability* instead of a
role, with the pane scope gone. It needs the pairing secret, which is not the obstacle it sounds
like: a guest watching a shared pane can read the QR the moment the owner opens it on that pane.

**The reconnect.** A participant reaches `/v1/connect` with `?desktop=&device=<participant id>&ct=`
exactly as a device does (§8). Their `welcome` carries `{participant, role, panes, expires, connect_token}` and
deliberately **no `capability` and no `password_entry`** — those belong to a device record and a
guest has none — followed by the scoped `panes` and a `participants` list for each pane. A guest
whose record expired, who was removed, or whose share ended gets `bye {reason, discard: true}` and
the channel closes; `discard` is the instruction to forget the record rather than retry. That answer
is only given for a key this desktop pinned itself, so it tells an attacker nothing: an entirely
unknown key still has the handshake refused with no explanation at all.

### 10.3 Presence and control

| Type | Direction | Body |
|---|---|---|
| `participants` | desktop → everyone on the pane | `{pane, items: [{id, name, role, driving, online, you}]}`, sent on join, leave, role change, removal and every handoff. **Everyone** includes the owner's own paired devices watching that pane: a device is not a participant, so every row of its copy has `you: false` |
| `control_request` | editor → desktop | `{pane}`, as section 6.6. For a participant it is a request, not a grant |
| `control_pending` | desktop → that editor | `{pane}` while the owner decides; lapses after 60 s |
| `control` | desktop → everyone on the pane | `{pane, holder: "owner" \| "agent" \| "participant:<id>", name}`, plus `device` — the id of the owner's own device holding the pane — on the copy sent to the owner's devices and to the desktop. A guest's copy never carries it: to them the holder is `owner`, and which phone of the owner's it is is not theirs to know. The owner's phone needs it because `owner` alone cannot tell it whether the hand on the keyboard is its own |
| `control` (refusal) | desktop → the editor who asked | the same, plus `reason: "refused" \| "lapsed"`. A grant needs no private answer: the handoff above already said so, to everybody |
| `control_release` | holder → desktop | `{pane}`, as section 6.6 |

`online` is whether that participant has a socket open right now: the record outlives the socket
(10.2), and presence is about the socket. `name` on a `control` is the desktop's own name when the
holder is the owner, `"the agent"` for the agent, and the guest's name for a guest — never a device
id, because an owner's own paired phone driving **is** the owner driving.

One driver per pane. It is the same token as the human/agent handoff (`ARCHITECTURE.md` section 9),
so "the agent is driving" and "alice is driving" are one state, held in `remote/control.py` and
spelled three ways from there: the `holder` above, the `control` field of a `panes` item (6.3) and
the `driving` flag on a `participants` row. Everything that can change it goes through that one
book — a grant, a release, a revoke, the owner's keystroke, a `full` device typing, the desktop
handing the program to the agent, a pause, a socket closing, a removal, a demotion, an expiry —
and each change fans out one `control` and one `participants` to everyone on the pane.

**The owner's physical keystroke in the pane always takes control back**, without asking
(`control_take`, 10.5), and the holder is told with `control`; anything of theirs already in
flight is refused with `not_driving` like anyone else's. Input from anyone who is not the holder is
refused with `not_driving` — **including one of the owner's own `full` devices**. A device's typing
claims the keyboard by itself (6.6) only while the keyboard is free: once the owner has taken the
pane back at the desktop, or handed it to a guest, that device asks for it again with
`control_request` rather than taking it by typing. Otherwise the keystroke that took the pane back
would be undone by the phone's very next line, and the phone and the desktop would share the
keyboard instead of taking turns. A participant's input is refused at a password prompt exactly as a
device's is — the hub refuses it from the source's own fresh termios read before the source refuses
it again at the write — and they are never offered the password field or its nonce.

Losing the keyboard is never silent and never sticky: a holder who disconnects, is removed,
is demoted to viewer, whose record expires, or whose share is paused loses it at once, and the
pane goes back to the owner rather than to whoever asked first.

**Out of scope for v1, deliberately:** *following* (a guest's viewport tracking the owner's) and
selection highlights, both of which the design doc sketches. They need a second per-participant
view state on a stream that today is one screen for everybody, and neither is needed to type or to
ask; they are not half-built here.

### 10.4 Guest prompts

An editor sends an ordinary `compose`. The hub does not pass it on: it answers `prompt_pending
{id, pane}` and asks the owner (`prompt_ask`, 10.5), who sees the guest's name and the whole text —
never a preview, because approving a prompt is approving every tool call the agent will make from
it, on the owner's keys. `prompt_decided {id, pane, approved, reason?}` follows, where `reason` is
`refused`, `lapsed`, `paused` or `removed`.

An approved prompt goes to the pane's **agent** — never the router, whatever the text looks like —
with `origin: "guest:<participant>"`, and the desktop is given the guest's display name beside it
(`origin_name` on the sidecar's `compose` line, 10.5) so the queue row can say *alice* rather than
a hex id. That name crosses the GUI↔sidecar line only: no client is ever sent another guest's
prompt, with or without a name on it (10.1).

A pending prompt lapses after 10 minutes, a guest may have 3 waiting (a fourth is `busy`), and
`plan_execute` from a guest is treated as a prompt — approved, it runs the plan flow, whose id the
desktop minted and resolves itself. A prompt whose guest was removed, demoted to viewer, or whose
share ended while it waited is **dropped**: it never runs, and an answer that arrives afterwards
does nothing. A prompt sent while the share is paused is refused `paused` rather than parked.

The owner's other option, *guest prompts run immediately*, is per share and off by default
(`share_options`, 10.5). It skips the question and nothing else: the prompt is still recorded with
its text (10.6) and still reaches the agent and only the agent.

### 10.5 The owner's controls (desktop only)

Between the GUI and its sidecar (`remote/gui_host.py`), as line JSON, never on the wire:
`invite_create {pane, role, expires, uses}` → `invite {id, url, qr}`; `invite_revoke {id}`;
`knock {participant, name, platform, code, role, pane}` → `knock_answer {participant, admit, role}`;
`participants {items}` (each item carries `online` and the panes it is `driving`);
`control_ask {pane, participant, name}` → `control_answer {pane, participant, grant}`;
`control_take {pane}` (the owner's keystroke) and `control_revoke {pane}` (the same from the
sharing panel); `prompt_ask {id, participant, name, pane, text, when, plan}` →
`prompt_answer {id, approve}`; `role_set {participant, role}`; `participant_remove {participant}`;
`share_pause {pane?, on}`; `share_options {pane, prompts_immediate, present_only}`; `share_end`.
The desktop is told of every handoff and every stop with `control {pane, holder, name}` and
`share_state {pane, paused, reason}` — the same two facts the phones are sent, from the same one
state, so the desktop's "alice is typing" cannot drift from theirs. A `share_pause` or a
`share_options` with no `pane` (or `""`) applies to the whole share.

These names are `OWNER_ONLY` in `remote/wire.py`, which is folded into `NEVER_FROM_CLIENT`, so the
same message arriving over the wire from any device — the owner's own paired phone included — is
refused `not_permitted` by the check every inbound message already passes through, and the existing
"every type is classified" test covers them — **except the three answers.** Owner, 2026-09-20
(card #PH0N, decision 6): a `full` device may admit a knock, decide a guest's prompt and grant
the keyboard from away, so `knock_answer {participant, admit, role}`, `prompt_answer {id,
approve}` and `control_answer {pane, participant, grant}` are `full` in `CLIENT_TYPES` (still
never a guest's: `GUEST_NEVER`). A `full` device that can already run any command on the desktop
gains nothing by answering "may alice type here?". What is waiting is sent to every `full` device
as **`owner_asks {items}`** — the whole list, again whenever it changes, and once on `welcome` when
it is not empty — each item `{kind: "knock" | "prompt" | "control", id, pane, name, …}`: a knock
carries the invite's `role`, the five-digit `code` and `platform`; a prompt its whole `text` (the
owner approves the text, never a preview, 10.4); a control request names the participant in `id`.
The desktop's own dialog is asked exactly as before and **whichever answer arrives first is
applied**: the hub's `_await_knock` waits on the GUI and the devices together, and a late
`prompt_answer` or `control_answer` finds its item gone (`_run_prompt`, `_apply_control_answer`).
A late answer from a device is not an error: it is sent the list as it now stands. The sidecar
tells the GUI `request_gone {kind, id, pane}` for a row a phone decided, so the Sharing pane
does not count it down to nothing. `owner_asks` is in `SERVER_TYPES` and not in
`GUEST_SERVER_TYPES`; every device answer is audited (`knock_answer`, `prompt_answer`,
`control_answer`, with the device id).

**Pause** refuses every participant's input and prompts with `paused` while the screen keeps
streaming, and a paused holder keeps nothing: control returns to the owner, because a pause that
left somebody able to type would not be one. Guests are told why typing stopped with
`share_state {pane, paused, reason}`, `reason` being `owner` (the switch) or `away` (below).
**End** closes every participant session, burns the pane's invites and deletes the participant
records; the pane's pause, its switches and its control state go with it. Removing one participant
does the same for that one, and burns the invite they came in on.

A removed participant's row is kept, marked removed and holding no panes, until its own expiry
passes — at most the 7 days of the invite — and is then dropped. It grants nothing: every lookup
returns live records only. It exists so a guest whose phone was asleep when they were removed is
*told* their access ended, rather than having a socket close on them for no stated reason.

With *guests can act only while I am present* on (`share_options {present_only}`), the hub treats
an inactive desktop window as a pause, with `reason: "away"`. It reads the **same** `window_active`
line section 9's notification presence rule reads — one signal, two readers, passed to the notifier
by the hub — rather than a second one that could disagree with it. A **20-second grace period**
applies: alt-tabbing to read a stack trace does not take the keyboard off whoever is typing, and
only an absence longer than that pauses the share. Coming back lifts it immediately.

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
`guest_prompt` (with the whole text), `prompt_decided` (with `approved` and, when it was not, why),
`control_request`, `control_grant`, `control_refused`, `control_revoke`, `control_release`,
`control_take`, `share_pause`, `share_options`, the meeting-code kinds of §10.7 (`code_create`,
`code_attempt`, `code_used`, `code_burned`, `code_expired`, each carrying `code_kind` — `invite`
or `pair`, §5.2 — since the line's own `kind` is already taken), `scope_grown` and `scope_shrunk` (a
pane joining or leaving a tab shared whole, §10.1, with the tab, the pane and the participant or
invite), and the input a participant or a device sent:
`line` with its text, `keys` and `paste` as byte counts. Every one of them carries the participant
id, which is minted at the knock, so a refusal and an admission name the same person as the join
and the leave that follow; a device's input names the device id instead, and neither line ever
carries both.

Raw `keys` are a count and never their bytes for the same reason §6.7 redacts a password: a prompt
this desktop failed to detect would otherwise be typed into the log in full.
The one exception to *before* is `admitted`: admission is also where a key that is already a paired
device is refused, so a line written first would record an admission that did not happen. It is
written the moment the record exists and before the guest is told anything.

**(security)** Never uploaded, written 0600 **from the moment the file exists** — opened with the
mode rather than chmod-ed once the first line is in it, because between the two it is whatever the
umask says, and the first line of an audit log is a pairing or a knock — inside a 0700 directory as
`logs.py` requires of every Relay log, and size-capped, but not rotated the way `logs.py` rotates,
because nothing in it is ever deleted: a month past 5 MiB goes on in numbered parts
(`audit-YYYY-MM.2.jsonl`, `.3`, …), each capped, so files grow with volume, not time.

### 10.7 Joining with a meeting code and a PIN

An invite link is 141 characters: fine to paste, impossible to say. Owner request, 2026-09-18:
*"i tell my friend a code and they type it on relay-terminal.ai to join me"* — settled as a
**4-letter meeting code and a 4-digit PIN**, `BQRT` and `4829`. Card `#97EG`.

The two halves do different jobs, the way magic-wormhole splits a nameplate from its password:

| | Meeting code | PIN |
|---|---|---|
| Form | 4 letters from `ABCDEFGHJKMNPQRSTUVWXYZ` (no I, L, O): ~280,000 values | 4 digits: 10,000 values |
| Drawn by | the rendezvous, unique among live codes | the desktop, with a CSPRNG |
| Seen by the server | yes — it is how the room is found | **never** |
| Job | routing | authentication, through CPace |

**What the code phase is for.** A 4-digit PIN cannot be a bearer secret, and a 4-letter code cannot
carry the desktop's key. CPace (`CPACE-X25519-SHA512`, draft-irtf-cfrg-cpace, initiator-responder)
turns the PIN into a key exchange in which **every guess is an online attempt against the
desktop**: a network observer learns nothing it can test offline, and a rendezvous that tried to
sit in the middle would have to guess the PIN live, once per attempt, like anybody else. The phase
ends by handing the browser, sealed under the CPace key, exactly what an invite link carries — so
from the Noise handshake on, a code guest *is* an invite guest: §10.2's knock, the owner's admit
by hand, roles, §10.3 control, §10.4 prompts, the join notification. There is no second admission
path and no auto-admit.

**Two kinds of code, told apart by the fragment.** The same phase delivers a **pairing** fragment
for the owner's own phone (§5.2, card `#FR1C`); the sealed frame is byte-for-byte the same shape,
so a client reads the fragment it opened and routes on `s=` versus `i=` — never on the code, and
never on the page the code was typed into. Everything below applies to both; the rest of §10.7 is
written for the invite kind, and §5.2 says what differs. The desktop's own record of a code carries
the kind (`meetcode.KIND_INVITE`, `meetcode.KIND_PAIR`), which is what the audit's `code_kind` and
the two sidecar state names below are.

**Limits.** One use, 10 minutes. The **desktop** counts failed attempts, never the server — the
server is the party this design declines to trust with the count. The third failure burns the code
and its invite, and a fourth attempt is refused. Success burns the code too.

**The delivered invite accepts one knock.** It is a full invite — it carries the secret `i` — so a
fragment forwarded after the code phase could otherwise knock, and "one use" would be a promise
about the code alone. An invite minted by `code_create` has `uses = 1`, an expiry no longer than the
code's 600 s, and a stricter rule than a link invite: **the first knock claims it** for that
knocking key, and every later knock with it is refused, even while the first is still waiting. If
the owner refuses that knock the invite burns; it also burns with its code (three failures, or
expiry unused). A stranger's chance per code is therefore 3 in 10,000, and a right guess still only earns a
knock the owner has to admit. The known cost: anyone who learns the meeting code can burn it with
three wrong PINs, and the owner makes a new one.

The five-digit knock code of §5 still shows on both screens — on the knock row, in the Sharing
pane, and on the guest's waiting screen — and the compare step stays. CPace has already ruled out
anyone in the middle; the code is kept as a second, independent check.

**CPace inputs.** `PRS` = the PIN in ASCII; `CI` = `relay/meet/v1`; `sid` = the code room's id in
UTF-8; `ADa` = `guest` (the browser, initiator); `ADb` = `desktop` (responder). From `ISK`, with
HMAC-SHA256 keyed by `ISK`:

| Name | Over |
|---|---|
| `tag_b` — the desktop knows the PIN | `relay/meet/v1 desktop` ‖ `Ya` ‖ `Yb` |
| `tag_a` — the guest knows it | `relay/meet/v1 guest` ‖ `Ya` ‖ `Yb` |
| `seal_key` | `relay/meet/v1 seal` |

Tags are compared in constant time. Both implementations (`remote/cpace.py`, `app/cpace.js`) are
checked against the draft's published test vectors and against each other.

**Routes** (`rendezvous/server.py`, the same code at relay-terminal.ai and in the desktop's sidecar):

| Route | Caller | Body → reply |
|---|---|---|
| `POST /v1/codes` | desktop, authenticated like `/v1/rooms` | `{desktop_id, token, room, ttl}` → `{code, expires_in}`; `ttl` ≤ 600 |
| `GET /v1/codes/<CODE>` | anyone; case-insensitive; per-peer lookup limit on top of `MAX_CHANNELS_PER_PEER` | → `{room}`; an unknown code and an expired one get the **same** 404, so they cannot be told apart |
| `POST /v1/codes/<CODE>/burn` | desktop | `{desktop_id, token}` → `{}`; stops resolving at once |

The code → room map lives only for the code's `ttl`; it is never part of the 7-day metadata the
rendezvous keeps.

**The code room** is a room of its own, opened with `/v1/rooms` (`ttl` 600) and separate from the
invite's room. The desktop remembers which rooms are code rooms, and a client connecting to one is
handed to the code handler (`remote/meetcode.py`) — it can never reach the Noise `Channel` path or
any hub handler, before or after the sealed invite is delivered — and it takes a rendezvous slot
like any other client. Frames are UTF-8 JSON;
binary fields are unpadded base64url.

| # | Direction | Frame |
|---|---|---|
| 1 | guest → desktop | `{"t":"meet_a","y":Ya}` |
| 2 | desktop → guest | `{"t":"meet_b","y":Yb,"tag":tag_b}` |
| 3 | guest → desktop | `{"t":"meet_confirm","tag":tag_a}` — sent only after `tag_b` checked out |
| 4 | desktop → guest | `{"t":"meet_invite","nonce":n,"sealed":c}` — AES-256-GCM under `seal_key`, 12-byte random nonce, AAD = the code in upper case, plaintext = the invite fragment `v=1&d=…&i=…&r=…` |
| – | desktop → guest | `{"t":"meet_error","error":"wrong_pin"\|"burned"\|"expired"}`, then close |

An attempt that ends any other way than a valid `meet_confirm` within 30 s is a failure: a bad
tag, an invalid point, a close, a timeout. **An attempt counts from the moment its connection
opens**, not when it settles: a guest learns from `tag_b` whether its PIN was right before it
confirms, so three sockets opened at once must not buy more than three guesses — a fourth while
three are unsettled is told `burned`. The desktop's own link dropping mid-attempt is not the
guest's failure and is not counted. The failure that burns the code answers `meet_error burned`;
earlier ones answer `wrong_pin`; a used code answers `burned`.

The lookup limit is per client address. The sidecar is normally reached through `cloudflared` or
`tailscale serve`, so every request arrives from loopback; for a loopback peer **only**, the address
is taken from `CF-Connecting-IP`, else the last `X-Forwarded-For` entry. Any other peer is its own
socket address, whatever its headers say.

**Desktop GUI ↔ sidecar** (owner-only, in `wire.OWNER_ONLY` with the rest of §10.5):
`{"t":"code_create","pane":"p1","role":"editor"}` → `{"t":"code","code":"BQRT","pin":"4829","expires":600,"invite":"<id>"}`,
then `{"t":"code_state","code":"BQRT","state":"used"|"burned"|"expired","failures":N}` when the code
ends. `{"t":"code_revoke","code":"BQRT"}` withdraws a code (the share window sends it when the role
changes under a live code). One live code per pane: a new `code_create` burns the pane's previous
one. The PIN is a
secret: never logged. The QA hook `RELAY_REMOTE_CODE_FILE` names a file the GUI writes `BQRT 4829`
to, beside `RELAY_REMOTE_INVITE_FILE`.

A **pairing** code (§5.2) has two names of its own on the same line, because it is a different
surface — the pairing dialog, not the sharing panel — and neither must redraw on the other's news:
`{"t":"pair_code"}` → `{"t":"pair_code","code":"ABCD","pin":"4829","expires":600}` (no `invite`:
there is none), then `{"t":"pair_code_state","code":"ABCD","state":"used"|"burned"|"expired","failures":N}`;
`{"t":"pair_code_revoke","code":"ABCD"}` withdraws it, and its pairing room burns with it. Both
names are in `wire.OWNER_ONLY` and `wire.NEVER_FROM_CLIENT`: a message that mints a *device*
record is the last thing that may arrive from a client. `code_revoke` withdraws only a share code
and `pair_code_revoke` only a pairing code, so neither surface reaches into the other's.

**Audit** (§10.6), each written before its action and each carrying `code_kind` (`invite` or
`pair`): `code_create` (code, invite, role — never the PIN; a pairing code's `invite` is `null`),
`code_attempt` (a failed attempt, with the count and why — never the PIN), `code_used`,
`code_burned` (with `reason`: `failures`, `revoked` or `replaced`), `code_expired`. A second knock
on a code's invite is written as `knock_refused`.

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
| Rendezvous | `rendezvous/server.py` | Registry with proof of possession, derived desktop ids, pairing rooms, the ciphertext relay, metadata with 7-day retention. `/v1/push/key` and a `/v1/push/send` that signs with VAPID and delivers bytes it cannot read; rooms may be opened with a `ttl` for invites. Per-device connect tokens checked statelessly on `/v1/connect`, counted per token, revoked by `/v1/revoke` and by the list every registration carries |
| Per-device connect tokens (§8) | `remote/pairing.py`, `remote/host.py`, `remote/identity.py`, `remote/guests.py`, `rendezvous/server.py`, `app/rrp.js`, `remote/client.py` | Built 2026-09-20 (#PH0N 1.3). Minted at pairing and admission, handed over in `paired`/`admitted`/`welcome`, presented as `ct`; a missing or bad one is refused as an unknown desktop; three channels per token; a revoke or removal closes the live channel at the rendezvous. `tests/test_remote_security.py` (`ConnectTokenTests`, `ConnectTokenPeerTests` over `tests/connect_token_peer.mjs`), `tests/test_remote_host.py` (`ConnectTokenTests`). **The hosted rendezvous must be redeployed** (`rendezvous/deploy.sh`) before a phone paired against it can connect with one |
| Desktop hub | `remote/host.py`, `remote/identity.py`, `remote/panes.py` | Pairing, capabilities, live revoke and downgrade, streams and resume, the event allow-list, rate limits. `PaneSource` is the seam the GUI will implement; `DemoPaneSource` stands in |
| Web client | `app/` | Pairing with the confirmation code, inbox, thread, composer, plan blocks, reconnect. Installable; the service worker does not cache |
| Guest web client (§10) | `app/guest.js`, `app/rrp.js`, `/join` | The invite link, the knock and its five digits, the shared pane with the same screen painter and scrollback, presence, the editor's ask-to-type and prompt box, pause, role changes and removal. A guest record stored apart from the paired-device one, so one browser can be an owner here and a guest there. `tests/test_remote_guest_browser.py` |
| Python client | `remote/client.py` | For tests and scripts; also where the client-side pinning rule is tested |
| Relay-to-Relay | `remote/viewer.py`, `src/RemotePane.{h,cpp}` | A laptop's Relay opens a pane the desktop shares, natively: "Open a shared pane…" pairs it as one of the owner's own devices (the same pairing a phone does), and the pane is drawn by the same model the phone's is — screen, scrollback, queue, thinking, model and conversation menus, control handoff. With `--guest` the viewer joins somebody else's share with their meeting code and PIN (§10.7): `/join CODE`, `/connect CODE`, the palette's "Join a shared session…" or the plug at the top right. A second viewer process holds guest sessions, so joining a colleague never disturbs the owner's own device session. `tests/test_remote_viewer.py`, `relay-remotepane-tests` |
| Dev harness | `remote/cli.py` | `python3 -m remote.cli share` shares a real shell; `dev` runs the demo agent. Both print the pairing QR. `--tls`, `--tailscale` and `--public` are the three addresses, and the first two are the same code the share dialog uses |
| Screen stream (P2) | `engine/tools/ScreenBridge.cpp`, `remote/terminal.py`, `app/screen.js` | A real PTY parsed by Relay's own emulator, streamed as styled rows, painted as a cell grid on the phone |
| Scrollback (§6.5) | `engine/core/VtCore.h` (`historyLines`), `engine/tools/ScreenJson.h`, `engine/tools/ScreenBridge.cpp`, `remote/terminal.py`, `remote/gui_host.py`, `src/RemoteShare.cpp`, `app/screen.js` | Paging by absolute row, from the bridge and from a GUI pane. Every frame carries `base`, so the client keeps the seam between its history and the live block closed while output arrives. Pages are fetched one ahead of the reader and de-duplicated by row; output arriving while somebody is scrolled back moves nothing and offers a way to live instead; at most 2000 rows are kept on the phone. History is painted by the run painter the live screen uses, because both ends of the wire go through one serializer |
| Take-over (P3) | `remote/host.py`, `app/app.js` | `keys`, `paste`, `line`, `control_request`/`control_release`, an extra-keys row and a line box, refused at a password prompt |
| Password entry (§6.7) | `remote/host.py`, `src/Pane.h` (`submitRemoteSecret`), `app/app.js` | A desktop-minted single-use nonce bound to the prompt, a per-device switch that is off by default, a fresh termios check in the hub and again at the write, a password field in the client. Tested; not yet tried on a real phone |
| Notifications (§9) | `remote/push.py`, `remote/notify.py`, `remote/host.py`, `app/app.js`, `app/sw.js`, `src/RemoteShare.cpp` | Connected end to end. `push_subscribe`/`push_unsubscribe` inside the Noise session, five triggers with a checkbox each on the phone, the presence rule over `window_active`, a per-pane cooldown, constructed bodies, a 410 or a revoke dropping the subscription, and a "Notify me" row that asks permission from a tap. RFC 8291 is checked against the RFC's own Appendix A vector, `app/sw.js` opens a Python seal under Node, and a local push service takes a real delivery (`tests/test_remote_push.py`). **Not yet tried on a real phone**: that needs the hosted rendezvous reachable from the push service |
| Audit log | `remote/audit.py` | Local, 0600 from creation, split by month and size. Records pairing, revoke, prompt detection and password use so far |
| Security review of P1–P4 | `tests/test_remote_security.py` | Push, password entry, voice and multiplayer reviewed adversarially (2026-09-18). Eight findings, all fixed; the attacks stay in the suite. The ninth, the per-device connect token of §8, was built on 2026-09-20 |
| `transport_switch` (§2) | `remote/host.py`, `remote/client.py` | The handshake, tested. There is no second transport yet |
| Local attach | `remote/attach.py` | The desktop's own terminal joins the same shell, so both ends drive it |
| In the app | `src/RemoteShare.{h,cpp}`, `remote/gui_host.py` | The share button in the pane's chrome row, the QR and approval dialog, and a sidecar that carries one of Relay's own panes (`ARCHITECTURE.md` section 19). The address picker above the QR offers the tailnet name first |
| Always on (§8.1) | `remote/gui_host.py`, `remote/host.py` | `start` with `"always"` and an `"address"` brings the service up with no share asked for, and keeps it there: the hub's socket reconnects for ever with a jittered back-off and **registers again before each retry**, so a rendezvous restart is survived rather than ending the day's reachability, and it never moves to another address on its own. `remote_state` reports `on`, `address`, `base`, `online`, `devices` and one sentence of `reason` whenever any of them changes. `tests/test_remote_gui_host.py` (`AlwaysOnTests`, `AutoPublishTests`), `tests/test_remote_host.py` (`AlwaysOnLinkTests`). The desktop's half — the Options › Remote switch, the chrome indicator and "Disconnect all" — is card #PH0N's Phase 1.1 |
| How the phone gets a secure context | `remote/tailnet.py`, `remote/devtls.py`, `remote/httpd.py` | `tailscale serve` with a real certificate, or a self-signed one. Both reach the same `httpd.Server`: it takes several listeners with one set of routes, so the CSP, `/pair` and `/join` behave the same at every origin |
| Voice (§6.4) | `app/app.js`, `remote/gui_host.py`, `src/Pane.h` (`transcribeForRemote`) | A `MediaRecorder` clip from the phone, carried to the pane and transcribed by its own worker on the desktop's key; the text returns to the phone's prompt box, matched to the clip by id. Tested through a headless browser with Chrome's fake capture device; not yet tried with a real microphone on a real phone |
| The Switchboard on a device (§17) | `remote/board_state.py`, `remote/wire.py`, `remote/host.py`, `remote/gui_host.py`, `remote/notify.py` | The hub half, built 2026-09-20 (#SWPH): `board_request` from a `full` device against an allow-list of ten request types with per-field shapes and caps, the never-list, per-device read and write buckets, audit lines without text, the GUI's `board_event` scrubbed of every path and routed by `rid` or fanned out to `full` devices, never to a guest, and the `card_waiting` push. `tests/test_remote_board.py`. The desktop bridge (`src/BoardRemote.*`) and the phone's view (`app/board.js`) are the card's other two halves |

Every Relay pane is an engine pane, so every pane can be shared.

**How a phone gets a secure context** (2026-09-18). WebCrypto, service workers and the camera are
only available in one, and plain http on a LAN or tailnet address is not one. Relay offers two
answers and prefers the first:

* **`tailscale serve`** (`remote/tailnet.py`). Tailscale terminates TLS with a Let's Encrypt
  certificate for `<machine>.<tailnet>.ts.net` and proxies to a **plain http port on loopback** —
  so what goes behind it is the ordinary local listener, not the self-signed TLS one; pointing it
  at the TLS listener would put a proxy in front of a certificate it has no reason to trust. There
  is no warning to accept, nothing is dropped across an interstitial, and Web Push becomes possible
  at all, because a browser that has seen a certificate error will not register a service worker.
  It needs `sudo tailscale set --operator=$USER` once on the desktop, and **Serve** and **HTTPS
  Certificates** enabled for the tailnet in the Tailscale admin console. The helper detects all of
  that and returns one readable sentence per failure; the CLI prints it and the share dialog shows
  it under the address picker, because "there is no such option" and "you have not run one command
  yet" look identical in an empty list.
* **A self-signed certificate** (`remote/devtls.py`), which needs nothing and costs a warning.

Both end at the same `httpd.Server`, which takes several listeners over one set of routes, so the
CSP of section 3, the `/pair` and `/join` client-side routes and the WebSocket upgrade are the same
code answering at whichever origin the QR names. The Noise session is end-to-end **above** TLS
(section 4): the phone authenticates the desktop by the key it pinned from the QR, so which of
these is in front changes nothing it verifies. `python3 -m remote.cli share --tailscale` and the
share dialog's first address are the same `tailnet.publish()`, and both run `tailscale serve reset`
when sharing stops or the address is changed.

Section 10 is part built. What exists (2026-09-18): guest identity and the participant store
(`remote/guests.py`, `guests.json` beside `devices.json`), invite links and their rendezvous rooms,
knock-to-admit with the same five-digit code as pairing and a two-minute refusal, reconnect by the
pinned key, pane scoping and the `viewer`/`editor` roles enforced at every inbound message and every
fan-out, the `GUEST_EVENTS` allow-list, the owner's controls as desktop-only sidecar lines and as
`python3 -m remote.cli share --invite`, and the audit lines of 10.6 (`tests/test_remote_guests.py`).

**The hub half of 10.3 to 10.6 is built** (2026-09-18, `remote/control.py`, `remote/host.py`,
`tests/test_remote_control.py`): one control state per pane across the owner's devices, the agent
and the participants, with `control` and `participants` fanned out on every change; the owner asked
about a guest's prompt and about the keyboard through two approver seams shaped like the knock's
(`prompt_ask`/`prompt_answer`, `control_ask`/`control_answer`), answerable from the GUI sidecar or
from `python3 -m remote.cli share --invite`; the owner's keystroke taking control back
(`control_take`, and the local attachment's own keys in the CLI); `share_pause` and *only while I
am present* with its grace period, told to guests as `share_state`; the outbound allow-list of
10.1; the queue-text filter of 10.1; and the audit kinds of 10.6. Lapses are driven by injected
clocks, so the ten minutes and the sixty seconds are tested as numbers rather than waited on.

**The desktop half of 10.5 is built** (2026-09-18, `src/SharingPane.{h,cpp}`, `src/RemoteShare.cpp`,
`tests/sharingpane_test.cpp`, evidence in
`docs/qa_evidence/2026-09-18-remote-multiplayer-desktop/`). The share window gains "Invite someone
to this pane" — role, expiry, uses, the link and its QR — and everything that follows is a splitter
pane rather than a dialog: per shared pane the participants with their key fingerprints and who is
driving, the live invites with uses and expiry and a Revoke, and the knocks, control requests and
guest prompts, each with the countdown of its own kind and Refuse first and holding the focus. The
pane opens from the share button, from the palette (`pane.sharing`) and by itself when somebody
knocks, and opening it never takes the keyboard, because the next keystroke would land on Admit.
The owner's keystroke in a pane a guest is driving sends `control_take` from the same place the
agent's hand-over already ends.

Reconciled against the hub's lines as written rather than as sketched here: a `participants` item
carries `online` and the panes that person is `driving`, a `prompt_ask` carries the `plan` id when
a guest's `plan_execute` became a prompt, `share_state {pane, paused, reason}` says whether guests
are paused and whether it was the owner or their absence, and a participant's `expires` is an
absolute epoch while an invite's is already the seconds left.

What is **not** built: *following* and selection highlights, which 10.3 puts out of scope for v1
with a reason.

**The guest's web client is built** (`app/guest.js`, served at `/join`). It is a separate
session from the owner's phone rather than that one with buttons hidden: `app/app.js` hands
the page over before wiring any of its own controls, and the editor's half — the extra-keys
row, the line box, the key handler — is built only once the session's role first says
`editor`, so a viewer's page has no key handler at all. The record it stores holds a
participant id and a role and no device id and no capability, under its own key, and neither
loader will return the other's row — the client-side half of `remote/guests.py`'s rule, so a
browser can be the owner of one desktop and somebody's guest on another. The client handles
`knock_pending`, `admitted`, `participants`, `control`, `control_pending`, `prompt_pending`,
`prompt_decided`, `share_state` and `bye {discard}`; it follows a role change live from the
`you` row of `participants`, because a role belongs to the person rather than the pane.
`admitted` carries no `features` list, so the client assumes a screen and scrollback and
degrades on the refusal; a `features` field there would be a small improvement.

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

The hosted address (§8) is in the share dialog's picker: `relay-terminal.ai`, a different rendezvous
the hub moves to and back from live. Two things follow from moving, and the entry's `where` and
the note after a switch both say them. A switch **drops the guests and phones connected through the
old address**: their channels were on the socket that closed, and they reconnect through the
rendezvous they know. **Invites made earlier work again when their address is picked again**: their
rooms stay at the rendezvous that minted them and are not burned by a switch — only live meeting
codes are, as `expired`. Push is unaffected either way, because each device keeps its own origin
(§9.1). Tested in `tests/test_remote_hosted_address.py`.

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


## 16. One pane model, two views: `pane_state`

The desktop pane is the model. It publishes what it already shows, and every client — the phone,
a tablet, the laptop browser, and in time another Relay — draws that. The client formats nothing of
the pane's own work: every label, hint, clock and model name it shows was written by the desktop and
is drawn as it arrived, so a pane feature reaches every screen without being built twice. What a
client does write is the words on its own controls, which the Qt pane has no equivalent of: a
phone's action sheet and send menu, "New conversation", the QUEUE heading, the accessibility labels,
its own keyboard hints (`app/pane.js` `ACTION_WORDS` and `SEND_WHEN`; `src/RemotePane.cpp` has its
own set). This paragraph used to say the client writes none of the pane's words, which was never
true of those; owner, 2026-09-19: publishing them would still leave each client a fallback copy, so
the claim went rather than the strings (#0VT4). Owner, 2026-09-18: the client is "a remote
control, it doesn't have to be identical to the computer app", but "it acts and feels like the
terminal".

**The message** (desktop → client, at most one per pane per 100 ms; `remote/pane_state.py` is the
only thing that decides what a given device sees, and `src/PaneState.{h,cpp}` builds it):

```json
{"t":"pane_state","v":1,"pane":"p1","seq":42,
 "turn":{"phase":"idle|thinking|tool|waiting","clock":"thinking · 12 s · step 1/256 · Esc stops","busy":true},
 "thinking":{"visible":true,"header":"Thinking… · fake · 12 s","tail":"…the last 2000 characters"},
 "queue":{"paused":false,"pause_reason":"","running":{"label":"✦ please plan this out"},
          "rows":[{"id":"steer:steer-3","kind":"steer|agent|command","label":"↪ next tool call  ✦ …",
                   "state":"waiting|withdrawing|queued|editing|paused",
                   "actions":["remove","edit","to_queue","send_now","steer","up","down"]}],
          "hint":"↑ select a row · Ctrl+↑↓ move · Shift+Del remove"},
 "model":{"label":"kimi-k3 (main)","choices":[{"id":"m1","label":"kimi-k3 (main)","current":true}]},
 "composer":{"mode":"auto|shell|agent","placeholder":"…","modes":["auto","shell","agent"]},
 "context":{"label":"96% left","percent_left":96},
 "theme":"relay-dark",
 "allowance":{"label":"Free · 73% left","percent_left":73,"warn":false,"detail":"182,400 of 250,000 tokens today · resets at 02:00"},
 "sessions":{"rows":[{"id":"s1","title":"…","when":"14:02","current":true,"running":false}],
             "can_new":true,"can_open":true}}
```

- `seq` rises; a client ignores anything older than what it has drawn. `running` is null when
  nothing runs. Any field may be missing, and a client renders what it was given.
- **`allowance` is the Relay Free chip** ([RELAY-FREE.md](RELAY-FREE.md)), published only while the
  pane is on the hosted preset and a quota figure has arrived: the whole object is absent
  otherwise, and a state that stops carrying it hides the chip (the pane moved to a provider with
  a key). The desktop writes every word of it — `label`, `warn` (read off `percent_left`, warning
  at 10 % and below) and `detail`, which a view shows as the chip's title — so it holds no secret
  and offers nothing to press: every level sees it, and no client type exists for it.
- **`theme` is the desktop's own theme id** (`dark-copper`, `relay-dark`, `relay-light`, …), so the
  pane on the phone is the colour the pane on the desktop is (owner, 2026-09-19). The desktop
  republishes when its theme changes. It is an id and nothing else — lowercase letters, digits and
  hyphens, 40 characters at most — because the view writes it straight into the pane's
  `data-theme`; `app/pane-theme.css` carries a generated block per shipped theme, and an id it has
  no block for (a theme of the person's own) leaves the view on the theme it is already showing.
  Absent when the desktop names none. Every level sees it: it is how the pane looks, there is
  nothing to press and nothing in an id to leak.
- **A row's `actions` are the whole truth about it.** The client offers those and nothing else; the
  pane checks the row still offers the action when the answer arrives, because the client was
  necessarily looking at an older state.
- **Every id is minted by the desktop**: row ids are the pane's own (`steer:<request id>`,
  `entry:<n>`), and choice and session ids are per-publish tokens resolved against the pane's own
  table. No preset id, no path, no session file name, no provider address ever appears.
- **Three levels, read live per message** (owner, 2026-09-18), on the capabilities of section 6.2:

  | Level | Capability | Sees | May do |
  |---|---|---|---|
  | **viewer** | `view` | this conversation: the rows, the reasoning, the model it is on | nothing: no row `actions`, no `model.choices`, no composer `modes` |
  | **partner** | `agent` | the same | type here (to the agent), act on the rows it was offered, pick a model |
  | **owner** | `full` | the same, **plus the conversations before this one** | everything above, plus `conversation_new` and `conversation_open` |

  The **whole `sessions` block** is dropped below `full`: a partner is not shown the titles of the
  owner's other conversations, which is what "observes this convo" and "can type in this convo"
  mean literally. `sessions.can_open` says whether a row may be opened; the conversation the pane
  is already on never is.
- **A guest is sent none of it at all** — `pane_state` and `queue_edit_text` are absent from
  `GUEST_SERVER_TYPES`, which section 10.1 makes an allow-list, so the owner's queue text, models
  and other conversations cannot reach a share.

**Client → desktop.** These are `agent` except `pane_state_get`, which is `view`, and
`conversation_new` and `conversation_open`, which are `full` — the owner's three levels put the
conversations before this one above typing in this one. All are in `GUEST_NEVER` except `compose`,
which a guest editor may send: a guest's prompt is not passed on but held for the owner to admit
(section 10.4), and `agent: false` is refused whatever their role.

| Type | Body | What it does |
|---|---|---|
| `pane_state_get` | `{pane}` | the pane publishes now, and the asking device is answered |
| `queue_move` | `{pane,row,to}` | `to_queue`, `steer`, `up` or `down` |
| `queue_edit` | `{pane,row}` | withdraws the row and answers `queue_edit_text {pane,row,text}` so the client can edit it in its own prompt box; nothing lands in the desktop's |
| `queue_send_now` | `{pane,row}` | a waiting steer, now, interrupting the turn |
| `queue_remove` | `{pane,row}` | withdraws or removes it (`item` is the older spelling of `row`) |
| `queue_resume` | `{pane}` | runs the queue again after a Stop paused it (`queue.paused`, card #7JD1). It is the phone's **empty send**, which is Enter on an empty prompt box at the desk, and it names no row: the pane decides whether there is a pause to lift, and the `pane_state` that follows is the answer. Nothing is resumed on a pane that was not paused, exactly as an empty Enter there does nothing |
| `model_pick` | `{pane,choice}` | only a model the menu offered, which is only one with a stored key; the pane says "Model changed from <device>" |
| `conversation_new` | `{pane}` | **owner level.** The same as `/new`, refused while a turn runs |
| `conversation_open` | `{pane,session}` | **owner level.** Opens one of this pane's past conversations, named by a token from a `pane_state` — never a path or a session file name — resolved by the pane against the list it published. Refused while a turn runs, as the session manager's own rows are |
| `compose` | `{pane,text,when,msg_id?,agent?,origin_name?}` | `when` is `now`, `queue` or `steer` — a steer is delivered inside the running turn at its next tool call, and both clients fall back to it when the row they meant to edit has gone. Steering needs `agent` and above (the owner's rule for a paired device, 2026-09-19) and is refused for a guest, whose prompt waits for the owner and so can never be aimed at the turn running now. The text is 1–32,000 characters. `msg_id` is the client's own dedup id (`app/pane.js` mints 72 random bits), so a retried send is not two prompts. `agent: false` asks the desktop to route the line the way its own composer does, shell included, and is refused for anything but a `full` device and for every guest — a client only sends it when the state offered it a composer mode other than `agent`. `origin_name` is a guest's display name, which rides onto the queue row while the id stays in `origin` |

Keys, provider and endpoint settings, the keyring and conversation deletion are desktop-only and
have no type here at all (section 6.6, `NEVER_FROM_CLIENT`).

## 17. The Switchboard on a device

Card #SWPH, 2026-09-20. The Switchboard — the cards, their threads and the card actions — is the
best surface Relay has for driving work from a phone, and until this section it could not be
reached from one: remote control publishes panes that have a terminal screen, the Switchboard is a
tool pane with none, and it runs on a per-window `BoardWorker` (sessions protocol 19) that the hub
never sees. The shape follows section 16's rule: **the desktop's board is the model**. The GUI
bridges its own `BoardWorker` to the hub, the hub allow-lists what goes in and scrubs what comes
out (`remote/board_state.py`), and the device draws what the desktop's board draws. The device
never reads `issues/`, never names a file, and there is no second writer: every write goes through
the one worker, which already serialises them and answers `board_conflict`.

`welcome.features` carries `"board"` for a `full` device on a desktop whose GUI is there to answer;
a client shows its Switchboard row when it sees it and not otherwise.

### 17.1 Client → desktop: `board_request`

```
→ board_request {rid, request: {type, …}}        `full` only; in GUEST_NEVER
← board_event   {rid, event: {event, …}}         zero or more, each with the request's rid
```

`rid` is the device's own: a whole number 0…2^53 or a token of 1–64 characters of
`[A-Za-z0-9_.:-]`. It is echoed and never read. A `board_request` without a usable `rid` is answered
with an ordinary `error unknown_type`, because there is nothing to answer it under.

`request.type` is one of these eleven and nothing else. `id` is always a **card id** — four characters
of `[0-9A-HJKMNP-TV-Z]`, the board's own alphabet — so it can be neither a path nor a file name.
Each request is **rebuilt** from the fields below: a field not listed is not copied.

| `type` | Fields | Reads or writes | What the desktop does |
|---|---|---|---|
| `board_open` | — | read | answers `board`, then a `board_cards` per further batch |
| `board_refresh` | — | read | answers `board_changed` |
| `board_card_get` | `id` | read | answers `board_card`: the body, the tasks and the thread's tail |
| `board_search` | `query` ≤ 200 | read | answers `board_search {ids}`; the plain words of the filter |
| `board_comment` | `id`, `text` 1–8,000, `kind` | write | a thread entry. `kind` is `note` (the default), `question` or `decision` — a person's kinds; `evidence` and `progress` are what an agent and the desktop's own hand-off write |
| `board_move` | `id`, `status`, `reason` ≤ 500 | write | moves the card. `status` is one of the board's statuses (`inbox` … `retired`, `backend/relay_core/board.py` `ALL_STATUSES`); the worker's own gates — evidence before a QA lane, say — still apply and answer `error` |
| `board_create` | `tab`?, `title` ≤ 200, `request` ≤ 8,000, `labels` ≤ 16 | write | files a card. `request` is stored verbatim as its `## Issue`; one of `title` and `request` must have words. `tab` is a tab id `[a-z0-9][a-z0-9_-]{0,39}`, a label 1–40 characters of letters, digits, space and `_.:+-` |
| `board_ask` | `id`, `text` ≤ 8,000, `mode` | write | a Discuss (`discuss`, the default; needs `text`) or a Plan (`plan`; `text` is the note to the planner and may be empty) turn on the card. Since card #CTRN that turn runs on the card's own queue on the desktop's worker, like any console turn; the request is unchanged and no GUI has to be looking at the card |
| `board_cancel` | `id` | write | stops that card's turn **and pauses that card's queue**, as Esc does in a pane; answers `board_cancelled` |
| `board_resume` | `id` | write | runs that card's queue again after a `board_cancel` — `resume_queue` (sessions protocol 12.5) by the road a device can reach, and the only queue op it has. It is what a device's **empty send** is: at the desk Enter on an empty prompt box resumes, and in the card view a Discuss with nothing typed does the same (card #7JD1). Answers `board_resumed {card_id, resumed}`, and `resumed: false` means nothing was waiting — a device is sent none of a queue's state, so it asks blind and is told after |
| `board_action` | `id`, `action` | write | GUI-level: `execute` or `verify`, run through the same hooks as the desktop's buttons; answers `board_action_result` |

Text that is too long is **refused**, not cut: a comment that silently lost its last paragraph is
worse than one the phone is told to shorten. Control characters other than newline and tab are
stripped.

**(security) Never from a device.** `board_delete`, `board_update`, `board_priority`, `board_undo`,
`board_check`, `board_claim`, `board_cleanup*`, `board_folder*`, `board_init*`, `board_import*`,
`board_survey`, `set_board`, `project_*`, `forge_*` (GitHub sync) and `configure` are named in
`board_state.NEVER` and refused with `not_permitted`. So is **any request carrying a path**: a key
named `path`, `paths`, `root`, `folder`, `file`, `files`, `dir`, `directory`, `cwd`, `workspace`,
`project` or `repo` — or ending `_path`, `_root`, `_dir`, `_file`, `_folder`, `_cwd` — at any
depth, and a field not on the list whose value looks like an absolute path (`/a/b`, `~/a`, `C:\a`,
`file:/…`). The owner's own words may mention a path: `text`, `title`, `request`, `reason` and
`query` are text, and text is on the list. None of the eleven request types is a wire type of its own,
so `{"t":"board_delete"}` is an `unknown_type` like any other.

**A refusal is a `board_event`**, so a client has one path for the hub's refusals and the worker's:

```json
{"t":"board_event","rid":7,"event":{"event":"error","code":"not_permitted",
 "message":"never from a device: deleting a card is the desktop's, behind its own confirmation.","source":"hub"}}
```

`code` is `not_permitted` (the never-list, a path, a desktop with no GUI), `unknown_type` (an unknown
type, a bad id, status, enumeration or over-long text), `rate_limited`, or `busy` (the GUI did not
answer within 20 s — a wedged desktop is an error on the phone, not a spinner). `source: "hub"`
tells it from the worker's own `error` and from the desktop bridge's
(`{"event":"error","code":"board_refused"|"remote_off"|"board_not_found"|"board_not_initialized","request":"<type>","text":…}`),
which have none. A `view` or `agent` device is refused
before any of this with the ordinary `error not_permitted`, and a guest with GUEST_NEVER's.

**Rate limits**, per device, as two token buckets so that reading a board cannot starve a write:
**reads 10 a second with a burst of 20, writes 2 a second with a burst of 4.** The wire's own
per-type ceiling (`LIMITS["board_request"]`, 720 a minute) sits above both.

**Audit.** An accepted request records `board_request {device, type, card}`; a refused one records
`board_refused {device, type, code}`, where `type` is a name this module knows or `"unknown"`.
Never the text, the title, the reason or the query. A request refused **at the gate** — a `view` or
`agent` device's, or a guest's, neither of which reaches the Switchboard's handler — records the
same line (`participant` in place of `device` for a guest, `code: "not_permitted"`), at most once a
minute per channel: somebody let into a pane who tries the board is what the owner most wants the
log to show, and the log is not a guest's to fill.

### 17.2 Hub ↔ GUI

Two sidecar lines (`remote/gui_host.py`), which share names with the wire's messages and are not
them:

```json
{"t":"board_request","rid":41,"device":"<device id>","name":"Elliott's iPhone",
 "request":{"type":"board_move","id":"K7Q2","status":"planned","reason":"agreed on the phone"}}
{"t":"board_event","rid":41,"event":{"event":"board_written","kind":"move","card_id":"K7Q2"}}
{"t":"board_event","rid":null,"event":{"event":"board_changed","rev":8,"upserts":[…]}}
```

`rid` here is **the hub's own**, a rising integer; the hub keeps which device's which `rid` it
stands for (at most 512, for 30 minutes — a Plan turn answers long after it was asked) and maps it
back. The GUI echoes it on every event that answers the request and sends `null` for anything
nobody asked for. `name` is the device's name, for the desktop's status line ("Card moved from
Elliott's iPhone"). The `request` is exactly the rebuilt form of §17.1: `id` is the card (the
worker's own requests call it `card`), and `board_create`'s `request` is the worker's `text`.

### 17.3 Desktop → client: `board_event`

`{"t":"board_event","rid":<the device's rid>|null,"event":{…}}`, to `full` devices only, the
capability read as each one is sent. **Never to a guest**: `board_event` is absent from
`GUEST_SERVER_TYPES`. It is not kept in a stream, so a `resume` replays none; a client that
reconnects sends `board_open`.

**Routing.** An event with a `rid` goes to the device that asked, under its own `rid`. If it is a
*change* — `board_changed`, `board_thread_appended`, `board_activity`, `board_cancelled` — every
other connected `full` device is sent it too with `rid: null`, so two of the owner's devices never
show two boards; the GUI therefore sends a change **once**, and a client treats a repeated upsert
as the no-op it is. An answer whose asker has gone reaches nobody. An event with `rid: null` goes
to every connected `full` device.

**Which events pass.** `board`, `board_cards`, `board_card`, `board_changed`, `board_search`,
`board_thread_appended`, `board_written`, `board_activity`, `board_cancelled`, `board_resumed`,
`board_busy`,
`board_conflict`, `error`, `board_action_result`, and anything named `board_chat_*`. Every other
event is **dropped and counted** (`Book.dropped`), `board_state`, `board_created`,
`board_init_request`, `board_folder_changed`, the import and GitHub-sync events among them — the
same ones `wire.WITHHELD_EVENTS` withholds from a pane's stream, for the same reason.

**What is done to one that passes** (`board_state.clean_event`):

- every key named like a path (the list in §17.1) is dropped **at every depth**: a row's `path`, the
  board's `root`, `workspace` and `project`, a tab's `folder`, a problem's `path`;
- a string that **is** an absolute path is dropped with its key, unless it is the owner's text;
  inside an event with none of the owner's text in it (`error`, `board_busy`, `board_conflict`,
  `board_action_result`) and inside every other desktop-written string, an absolute path within
  the prose is replaced with `[path]`;
- the **card's text passes as text** — `body`, `issue`, `title`, `acceptance` and a thread entry's
  or a task's `text`. They are the owner's notes, already in git, and the device is the owner's. A
  path he wrote in a card stays where he wrote it;
- a pane session token (`session` on a row or a `board_written`, `pane_token` on a thread entry,
  `pane` on a `board_action_result`) is cut to the eight characters the desktop's chip draws;
- key material is redacted as in section 16 (`[redacted]`), the card's text included;
- **caps**: `body` and `issue` 200,000 characters, a thread entry's `text` 20,000, `title` 400, any
  other string 2,000; 500 cards (`cards`, `upserts`) and 500 thread entries per event — a thread
  keeps its **newest** — 500 objects or 5,000 scalars in any other list, 400 keys an object, ten
  levels deep. Anything cut sets `truncated: true` on the event;
- the event is then **fitted under one wire message** (1 MiB less 64 KiB): first `issue` goes (it is
  a copy of part of the body), then the oldest thread entries, then the last rows, then the tail of
  the body, and `truncated` is set.

**Two fields the hub adds.** `board_key` is an opaque name for the board an event is about — twelve
hex characters of a hash of its `root` under a salt that lives as long as the hub, so it identifies
a board across events and cannot confirm a guess at a path. `board_name` (on `board` only) is the
last component of the project's path, `relay-terminal`, as a heading for the view.

The payloads are otherwise the sessions protocol's (19.2–19.4, 19.10, 19.16) minus those fields: on
every event `id` is the desktop's own request id (`remote-7`) and `card_id` the card — except
inside a row, on `board_activity` and on the GUI's own
`board_action_result {id, action, ok, pane, message}`, where `id` is the card's. Two
`board_changed` shapes arrive for one write: the board tools' `{upserts: ["K7Q2"], write_id}`,
naming the card, and then the worker's `{rev, upserts: [row], removed}`, carrying it. The desktop
bridge (`src/BoardRemote.cpp`) strips the path keys itself before the hub does and sends `project`
as a name; when it strips `root` there is no `board_key`.

### 17.4 What a device is not given

The turn events of a `board_ask` (`delta`, `thinking_delta`, `tool_started`, `done`, …) are not in
§17.3's list: the phone sees the question land and the answer land, both as
`board_thread_appended`, and a failed turn as `error` with its `card_id`. Editing a card's text
(`board_update`) waits for a view that can hold a `base_hash` and show a conflict. Both are
additions to the two lists in `remote/board_state.py`, and nothing else.

**Card #CTRN changed what a `board_ask` *is*, and deliberately changed nothing here** (2026-09-21).
A card turn is now an ordinary console turn on the desktop worker — it queues, it has a request
ledger, its answer streams into the card console's transcript — but the verb, the two thread writes,
the stage advance and `board_cancelled` are exactly what they were, and none of the turn's new
events (`queued`, `queue_changed`, `agent_started`, …) is on §17.3's list. A device needs no GUI for
any of it: the **worker** writes the thread at both ends, so a Discuss started from a phone is
recorded whether or not a card page is open, on whatever desktop the hub reached. Two consequences
worth knowing, both of them behaviour a device sees rather than a message:

- **A second ask on a card that is working now queues** instead of answering `board_busy`. The phone
  sees nothing at all until that turn's answer lands as `board_thread_appended`; `board_busy` still
  comes back for a cleanup, for the console's own turn and for a write to a card that has work on it.
- **`board_cancel` stops the running turn and pauses that card's queue**, as Esc does in a pane —
  and the way back is the device's own (card #7JD1, 2026-09-21; owner: *"why don't we just copy the
  functionality and have enter resume"*). Two doors, both of them the pane's own doors copied:
  the device's **next `board_ask` on that card resumes the queue by itself** (sessions protocol
  12.5 — the worker clears the pause as any person's submit goes past it, so nothing new is on the
  wire for it), and its **empty send** is `board_resume {id}` — Discuss with nothing typed, which
  is the box and the key that resume at the desk. The ops that name a queue by `surface` are still
  not a device's: `board_resume` is the one queue op on §17.1's list, because it is the only one
  that needs neither a row id nor any of a queue's state to aim.

### 17.5 The `card_waiting` push

A sixth notification kind (§9.1–9.3): **a card whose `waiting_on` becomes `owner`.** The hub reads
it off the *cleaned* `board`, `board_cards`, `board_changed` and `board_card` events as they pass —
whether or not any device is connected, which is the point — keeping the last known value per card
per `board_key`.

- **A change, never first sight.** Nothing fires for a board seen for the first time, so opening
  the Switchboard does not ring the phone once per card already waiting; nothing fires for a card
  first seen before its board has been seen whole (`more` false), because a card not read yet cannot
  be told from a new one. Once the board is known, a card that *arrives in a change*
  (`board_changed`, `board_card`) already waiting on the owner is a change like any other; one
  that turns up in a snapshot (`board`, `board_cards`) is not, because a snapshot answers somebody
  opening a board — possibly another window's. A known card whose value differs between two
  snapshots is a change. Another project's board (another `board_key`) is first sight again.
  The card is read from the row's own `id` or a `board_card`'s `card_id`, never the event's `id`.
- **The body is the hub's**, as every push body is: `{"v":1,"kind":"card_waiting","pane":"",
  "card":"K7Q2","title":"A card is waiting on you","body":"#K7Q2"}`. The card's id and nothing else
  of it — **never the title, the question or any text**: a title is the owner's words about his
  work, and this lands on a lock screen. `pane` is empty because no pane is meant; `card` is what a
  tap opens.
- The **presence rule** applies unchanged; the cooldown is **per card, 60 s**, with **10 s between
  any two** so a cleanup that moves twenty cards rings once; a kind nobody wants spends neither.
- **On the phone it is part of "When something needs me"**, with `waiting_input` and `password`.
  A stored `kinds` list that names `waiting_input` and not `card_waiting` — every list written
  before this section — is read as wanting both (`notify.kinds_of`), so the owner's
  already-subscribed phone hears about a card without toggling the switch; the stored list and the
  `push_state` reply are left as the device sent them. The consequence is that the two cannot be
  separated from the client: a device hears `card_waiting` when it asked for it **or** for
  `waiting_input`.
