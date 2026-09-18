# Remote access from a phone and multiplayer terminals: research and design (proposal, 2026-09-17)

Issue: `issues/features/2026-09-17-remote-phone-and-multiplayer.md`. Nothing here is implemented.

Owner request (2026-09-17): "another important feature i need: remote access on phone. multiplayer shared terminals.
i like warp remote control and blink but they kind of suck. lets make a good version of that."
Owner clarification (same day): "the no account / cloud / etc are not strict constraints. ideally it could be done in a
browser on relay-terminal.ai as a first version, later on we make android / phone apps." Content stays end-to-end
encrypted (E2E). Sources are numbered `[n]` (list at the end). **(unverified)** marks claims not checked against a
primary source in this pass. Competitor marketing pages are labelled as such.

## 1. Summary

- **v1 is a phone-first web app at relay-terminal.ai** (installable PWA) that reaches the user's desktop Relay through a
  small **Relay-operated rendezvous service**: signaling, a ciphertext-only WebSocket relay fallback, and Web Push.
  WebRTC data channels go peer-to-peer when possible. Everything inside is E2E encrypted with keys pinned at **QR
  pairing**, so the service routes bytes it cannot read. No account is needed to pair your own phone.
- **Agent-first phone UX**: an inbox of panes and agent threads, composer with voice and queue, collapsible tool
  stream, plan review, and notifications for "finished", "waiting for input" and "password prompt". The terminal is
  read-only until you press **Take over**, which adds an extra-keys row and a line-input box.
- **Stream state, not bytes**: terminal screens go out as screen-state diffs from Relay's engine (the mosh model), agent
  activity as filtered worker-protocol events. That fixes the two bugs that hurt Warp's viewer: the phone resizing the
  host session, and garbled text.
- **Multiplayer is a star around the host desktop**: owner, editor and viewer roles; one driver per pane, using the same
  handoff model as human/agent control; invite links that expire; admission by knocking; an audit log; accounts
  (passkeys and GitHub) only at this phase.
- **Phases**: P1 agent companion (works on KonsolePart), P2 terminal view (needs the engine), P3 take-over, P4
  multiplayer, P5 native Android and then iOS apps.

## 2. Research

### 2.1 Existing products

| Product | How it connects | Auth | Weaknesses found (evidence) |
|---|---|---|---|
| **Warp Remote Control / Session Sharing** | Session state is uploaded to Warp's cloud and anyone with the link can view it; edit access is granted separately [1][2]. Works from a phone browser; supports Claude Code, Codex and OpenCode [1][3] | Viewers must sign in to a Warp account; sharer grants view or edit [2] | Content stored in the cloud about 1 week and cannot be deleted sooner; **Secret Redaction is not applied**, "treat sharing a session like sharing your screen" [2]. No E2E encryption is described [1][2]. Open bugs: a mobile viewer **resizes the local session** and distorts wrapping, "making the feature unusable" (#13474, 2026-07-08) [4]; CJK text garbled and IME input rejected in the web viewer (#16060, 2026-09-17) [5]; `/remote-control` fails with "Websocket closed before starting session" (#10108) [6]. A competitor page says it is browser-only on phones, with no push notifications or queued messages [7] |
| **Blink Shell (iOS)** | Native SSH and mosh client; Blink Code wraps VS Code web, Codespaces or code-server [8][9] | SSH keys (Secure Enclave) [10] | $19.99/yr subscription; App Store rating 3.1/5 (419 ratings) when fetched, complaints about subscription migration, nag screens and redraw bugs [11][12]. An open bug leaves a mosh session **blank after about 20 min in the background** on iOS 26 (#2177) [13]. mosh syncs only the visible screen, so **scrollback is lost** [14]. It has no idea of an agent turn and no push when an agent blocks (competitor page [15]). Blink Code needs pre-configured non-interactive auth and some shortcuts get suppressed [9] |
| **Claude Code Remote Control** | Local process makes **outbound HTTPS only** and polls the Anthropic API; server routes messages; TLS, not E2E; **transcript stored on Anthropic servers** [16] | claude.ai login; URL or QR into the Claude app [16] | Good ideas: queues messages while reconnecting; push "when Claude decides" or "when actions required"; **no push while you're at the terminal**; "Check in from your phone" nudge on long turns [16]. Limits: one remote session per process; server mode gives up after about 10 min offline [16] |
| **Codex in the ChatGPT app** | "Secure relay layer … without exposing them directly to the public internet"; QR pairing; same account on both ends [17] | ChatGPT account, MFA, SSO or passkey [17] | Host must stay awake and online [17]. Phone approves actions and reviews diffs and terminal output; notifies on finish or when input is needed [17] |
| **Cursor web and mobile** | Agents run in **Cursor's cloud**, not your machine; web app installable as a PWA [18][19] | Cursor account, paid plans [18] | Different model (cloud VMs); shows a PWA is acceptable for a mainstream developer phone client [18] |
| **Happy / Happier (open source)** | CLI wrapper ↔ server ↔ Expo native apps plus a PWA; **server is a dumb relay of AES-256-GCM blobs**, key shared by QR [20][21] | QR shared secret; self-hosted server can add OIDC, GitHub org or mTLS [20] | Closest prior art for the recommended design (third-party wrapper, not a terminal) |

### 2.2 Shared terminals

| Tool | Model | Takeaways |
|---|---|---|
| **sshx** (MIT) | CLI → server mesh (gRPC between nodes, WebSocket to browsers) → web canvas; **Argon2 + AES E2E**, key in the URL fragment (83 bits, stretched by Argon2id for a short link); live cursors; **predictive echo à la mosh**; automatic reconnection; latency display [22][23]. Self-hosting "not supported at the moment" [22] | Key-in-fragment works; the server never sees plaintext. Relay can use a full-entropy key because a QR code carries it |
| **tmate** | tmux 2.x fork + relay; official relays being **disabled 2026-12-11** (search summary) [24] | Depending on a hosted relay is a real risk; keep ours small and self-hostable |
| **upterm** (Go) | SSH-based host → `uptermd` relay; authorize by `authorized_keys` or GitHub/GitLab users; self-hostable single binary [24][25] | Authorizing by GitHub identity is natural for developers |
| **tty-share** | TLS to server; `--readonly`; **E2E is still on the TODO list** [26] | Read-only mode is a basic expectation |
| **ttyd / GoTTY** | Local WebSocket + xterm.js; **read-only unless `-W`**; basic auth [27] | No reachability or E2E on their own |
| **VS Code Live Share** | New shared terminals are **read-only by default**; only hosts start them; read/write equals host access [28] | Default to read-only; the host can always intervene |
| **Zed** | Channels with Guest/Member/Admin roles; follow collaborator; **no shared terminal yet**, planned [29] | Role naming; follow mode |
| **Replit Multiplayer** | Shared shell, observation mode (follow a collaborator), up to 4 collaborators (search summary) [30] | Follow/observe is expected |
| **Teleport** | Join modes **observer / peer / moderator**; moderator can watch and terminate but not type; moderated sessions require extra participants before starting (Enterprise) [31] | Clean vocabulary for roles and for owner-must-be-present |

### 2.3 Transport and reachability

- **mosh**: syncs *screen state* over UDP (SSP), roams across IP changes, speculative local echo: "median keystroke
  response time from 503 ms to nearly instant (… more than 70% of the keystrokes…)"; visible screen only [14][32].
- **Eternal Terminal**: TCP, journals output and replays it on reconnect, so scrollback survives [33].
- **WebRTC data channels**: about 10–25% of real-world sessions need TURN, and far more on managed corporate networks
  (vendor blogs, not measured) [34]. **iroh** reports about 90% direct connections and 95% of bytes direct, with
  stateless encrypted relays as fallback [35]. **Tailscale**: DERP relays over 443 are slower than direct; Serve is
  tailnet-only; Funnel is limited to `*.ts.net` and ports 443/8443/10000 [36][37].
- **Cloudflare**: Tunnel fully supports WebSockets. Free/Pro/Business terms restrict serving video and large files [38].
  Durable Objects bill WebSocket messages at 20:1 and do not bill hibernated duration [39]. Realtime TURN costs
  $0.05/GB after 1 000 GB free [40].
- **Web Push on iOS**: iOS/iPadOS ≥16.4, **only for Home Screen web apps** (manifest `display: standalone`), permission
  only after a user gesture, Badging API supported, **no Apple Developer membership needed** [41][42]. Only the default
  action shows on iOS (search summary) [42]. Payloads are encrypted to the subscription keys (RFC 8291) **(unverified
  here)**. Native APNs/FCM push needs an app plus a server holding the push credentials **(unverified here)**.
- **Browser E2E has a code-delivery problem**: whoever serves the JavaScript can serve a key-stealing version. Meta's
  Code Verify extension checks WhatsApp Web's code against hashes held by Cloudflare [43]. Passkey **WebAuthn PRF** can
  derive encryption keys in Safari 18+ and current Chrome/Firefox, with iOS roaming-key caveats [44].
- **Phone terminal input**: xterm.js issues report Android GBoard composition corrupting or duplicating input,
  predictive text confusing backspace, and poor touch support [45][46].

## 3. Architecture options (reach a desktop from a phone; multiplayer)

| Option | Works in v1 web client? | Setup for the user | Privacy | Reachability | Verdict |
|---|---|---|---|---|---|
| **(a) WebRTC P2P, QR pairing, signaling + TURN** | Yes (browser data channels) | Scan a QR code | E2E if keys are pinned at pairing; signaling sees metadata only | ~75–90% direct; the rest need TURN [34][35] | **Use as the fast path** |
| **(b) User's tailnet** (desktop serves the PWA and a WebSocket on its Tailscale IP) | Yes, if Tailscale runs on the phone | Tailscale on both devices | Best: no Relay server at all | Always (DERP fallback) [36] | **Optional "private mode"** for self-hosters |
| **(c) Relay-operated relay service, E2E** | Yes (WebSocket) | Scan a QR code | Server sees who talks to whom, when and how much; never content | Always (outbound 443 from both ends, like Claude Code [16]) | **Use as the always-works fallback and home for signaling and push** |
| **(d) SSH/mosh to the desktop** | No (browsers can't speak SSH/UDP) | Open ports or a tailnet, SSH keys | Good | Poor behind NAT | Rejected for v1; Blink users can still SSH in |

**Recommendation: (c) + (a) together, with (b) as an option.** The desktop keeps one outbound WebSocket to the
rendezvous (`rv.relay-terminal.ai`). Pairing and the E2E handshake always go through it. Both sides then try a WebRTC
data channel and move traffic there if ICE succeeds; otherwise traffic stays on the WebSocket relay. **v1 needs no
TURN**, because the WebSocket relay is the fallback, and it avoids UDP ports behind a Cloudflare Tunnel. The E2E layer
is independent of transport, so switching transports mid-session is invisible.

**E2E layer.** Each desktop has a long-term X25519 identity key in the Secret Service keyring (same `keystore.py` path
as API keys). Each phone or browser creates a non-extractable key in IndexedDB. Sessions use a **Noise `IK`-style
handshake** with both static keys pinned at pairing (libsodium-style primitives, e.g. `@noble/curves` in JS if
WebCrypto X25519 is missing **(unverified per browser)**). The WebRTC DTLS fingerprint is not trusted on its own; the
inner Noise session authenticates it. The server can drop or delay traffic but cannot read or forge it.

**The web client's weak point (be honest in the privacy page):** a compromised relay-terminal.ai could serve malicious
JS [43]. Mitigations:

1. Serve the app from a static origin separate from the rendezvous (different processes and credentials).
2. Strict CSP, no third-party scripts, SRI on every asset.
3. Reproducible build with hashes published in the GitHub release, plus a "verify this build" page.
4. **Private mode (b)**: the desktop serves the same PWA over the tailnet, so relay-terminal.ai is not involved.
5. Native apps (P5) remove the problem for users who install them.

### 3.1 Hosting

relay-terminal.ai is a static site today: `deploy.sh` rsyncs `site/` to nginx on one shared host behind a Cloudflare
Tunnel. That host serves about a dozen sites through one nginx, so server-side changes are risky.

| Component | What it does | Where (recommended first) | Alternative |
|---|---|---|---|
| **Web app (PWA)** | Static HTML/JS/CSS, service worker, manifest | **`app.relay-terminal.ai`**, a separate origin so marketing-page scripts can never share key storage. Same host, one new docroot and vhost (owner action). Fallback with no server change: `site/app/` under the existing `deploy.sh` | Cloudflare Pages |
| **Rendezvous `relay-rendezvous`** | Device registry (SQLite: device id, public key, push subscription, desktop id), signaling, ciphertext WebSocket relay, Web Push sender (holds the VAPID key), rate limits, invites (P4) | **Python (asyncio, `websockets`, SQLite) in `rendezvous/`** (owner decision, section 12), run as a systemd service on the same host and published by a **cloudflared ingress rule `rv.relay-terminal.ai → localhost:PORT` that bypasses nginx**. Same toolchain as `backend/`, so `ci.yml` tests it with the existing pytest job and a self-hoster needs nothing new | Cloudflare Workers + one Durable Object per desktop (hibernating WebSockets [39]); Fly.io |
| **TURN** (later, only if P2P matters for latency) | Relay UDP for WebRTC | Cloudflare Realtime TURN [40] | coturn needs public UDP ports, which don't pass through the Tunnel |
| **Push** | Web Push now; APNs/FCM in P5 | Rendezvous | n/a |
| **Accounts (P4)** | Passkeys, GitHub OAuth | Rendezvous | Magic links need an email provider (extra cost and deliverability work) |

**Small-scale cost (estimates, unverified).** An agent thread is text: roughly 1–5 MB/hour per watching phone. A busy
terminal stream capped at 10–20 fps is roughly 10–50 MB/hour. 100 users × 2 h/day × 20 MB ≈ 120 GB/month through the
relay if no session went direct, which fits an existing dedicated server's allowance at ~$0 marginal. Durable Objects:
Workers paid plan base plus pennies at this scale [39]. Realtime TURN: free under 1 TB/month [40]. The Tunnel's
large-file/video clause [38] should not apply to small interactive streams **(owner to confirm)**. Rendezvous
metadata logs (ids, IPs, byte counts) are kept 7 days, with no content and no analytics.

### 3.2 What changes in ROADMAP decisions

| Decision today | Proposed |
|---|---|
| Non-goal "A Relay account … or a Relay server" | A Relay-run rendezvous exists; **accounts optional** (not needed to pair your own devices; needed to invite people) |
| Non-goal "A browser-based terminal" | A browser **companion** for a desktop Relay; not a standalone web terminal |
| No telemetry | **Unchanged.** Operational metadata only, short retention, documented |
| BYOK only | **Unchanged.** API keys never leave the desktop. Voice from the phone is transcribed by the desktop with the desktop's OpenRouter key |
| No per-action approvals | Unchanged for the owner's own devices; guest prompts get a moderation option (section 7) |

## 4. What is streamed

The protocol is **RRP/1** (Relay Remote Protocol): framed messages inside the Noise session, JSON for control and
agent events, compact binary (CBOR or MessagePack) for screen diffs. The desktop GUI process is the only endpoint
(`RemoteHub` in `src/`). It multiplexes panes and filters worker events; phones never talk to workers directly.

| Stream | Content | Why this shape |
|---|---|---|
| **Screen** | `screen_snapshot {pane, rows, cols, cells, cursor, alt, title, cwd, seq}` then `screen_diff {seq, rows: [dirty rows]}`, built from the `ViewportFrame` **`TerminalView` has already produced** (`engine/view/TerminalView.cpp`), not from a second `VtCore::updateFrame` call: that call *consumes* the dirty state (`engine/core/VtCore.h`), so a second consumer would stop the local view repainting (section 12) | Host size stays authoritative, so the phone **never resizes the pane** (Warp #13474 [4]). Resync is one snapshot, not a replay. Floods cost at most N frames/s, not the PTY byte rate. Text is already decoded (avoids Warp #16060 [5]). The phone needs a cell-grid renderer, not a second emulator |
| **Scrollback** | On demand: `history_get {pane, before_row, count}` → pages of styled lines. Needs a **new const `VtCore::historyLines(from, count, out)`** in both cores; `historyText` is plain text and `scrollViewportToRow` would drag the desktop user's own viewport (section 12) | Fixes mosh's lost scrollback [14] without streaming history continuously |
| **Agent** | Filtered worker events (`agent_started`, `delta`, `thinking_delta/done`, `tool_started/result`, `turn_summary`, `done`, `error`, `cancelled`, `queued`, `queue_changed`, `subagent_*`, `plan_written`, `recap`, `context`, `mode_changed`) plus on-demand `turn_transcript_get` and `tool_output_get` (`docs/AGENT-SESSIONS-PROTOCOL.md`) | Already structured with `turn_id`; phones render cards, not a terminal transcript |
| **Pane state** | `panes {items: [{id, title, cwd, program, control: human\|agent\|remote:<device>, status: idle\|running\|waiting_input\|password\|finished\|failed}]}` | Drives the inbox and notifications. Status comes from `state.json`, the termios password check and OSC 133 marks |
| **Input (phone → desktop)** | `compose {pane, text, when}` (goes through `route` like the composer), `agent {cancel\|queue_remove\|agent_stop\|set_mode\|recap_request}`, `keys {pane, bytes}` (take-over only), `paste {pane, text}`, `voice {pane, audio(opus/webm), final}` | Allow-list. **Never allowed remotely:** `store_key`, `import_warp`, `configure` with key material, skills import, keybinding and settings changes, plan-file writes outside the plan flow |
| **Board and files** (later) | Read cards and threads, open a file preview | Reuses Board and file-pane models |

**Latency and bandwidth.** Direct P2P adds only network round-trip time. The single-region relay adds a detour for
far-away users **(unverified; measure in P1)**. Coalesce frames at ≤20 fps while foreground and ≤4 fps on a hidden
tab; agent deltas every 50 ms. Typing echo during take-over can later use mosh/sshx-style predictive echo [14][22].

**Reconnection.** Every stream carries `seq`. On reconnect the client sends `resume {last_seq per stream}`. The hub
replays from a bounded ring (e.g. the last 2 000 agent events per pane), otherwise it sends a fresh snapshot plus
`turn_transcript` and `queue_changed`. Phone input sent while offline is queued with an id and applied at most once.
As with Claude Code [16], the desktop keeps working if the rendezvous is down; only remote access pauses.

**KonsolePart.** It cannot produce frames or screen text (`docs/ENGINE.md` status table), so **Screen needs panes on
the Relay engine** (`--engine=vterm`, later the default). Agent streams, pane state, notifications and voice work
today, which is why P1 ships first.

## 5. Phone client

| Option | For | Against | Verdict |
|---|---|---|---|
| **PWA at app.relay-terminal.ai** | Owner's v1 ask; no store review; one codebase for phone, tablet and laptop guests; Web Push on iOS 16.4+ once installed [41] | iOS push and durable storage require Home Screen install (Safari may evict site data of non-installed sites **(unverified here)**); background sockets die, so reconnect on focus; JS-delivery trust [43] | **v1** |
| Native Android/iOS (Kotlin/Swift or Expo/React Native as Happy does [20]) | APNs/FCM with rich actions ("Stop", "Reply"); Keychain/Keystore; better keyboard control; no JS-delivery problem | Two store pipelines; push credentials on the server | **P5**, Android first per owner; same RRP/1 |
| Qt for mobile (reuse C++) | Could reuse engine renderer | Weak mobile UX, big binaries, little shared code with a thin client | No |

Stack: TypeScript, no framework or a small one (Preact/Solid), canvas cell renderer, Noise and crypto from audited
libraries, `app/` in the repo built by CI. The client never uses `innerHTML` on terminal or agent text: output is
attacker-controlled. Markdown renders through a sanitizer, and OSC 8 links open only after a confirm sheet.

### 5.1 Phone UX (agent-first)

1. **Inbox (home)**: desktops → panes as rows with a status chip (Running 3m, Waiting for input, Password prompt,
   Finished, Failed), last agent line, queue count. Sorted "needs you" first. Badge count through the Badging API [41].
2. **Pane → Thread tab (default)**: turn cards (prompt, collapsible thinking with elapsed time, tool rows from
   `turn_summary` that expand via `tool_output_get`, answer). Subagents are nested chips with live status. No approval
   prompts, matching desktop behaviour. **Stop** is always visible.
3. **Composer** (bottom, thumb reach): text box with the same routing (a `$` prefix forces shell); **hold-to-talk
   mic**: audio goes E2E to the desktop, which transcribes with its OpenRouter key and returns text for editing before
   send; Send / Queue / Interrupt as a segmented control; `@file` picker from the pane's cwd.
4. **Plan review**: `plan_written` opens the Markdown with **Execute**, **Execute in fresh context**, **Ask for
   changes** (a comment becomes a prompt).
5. **Terminal tab**: live read-only screen, fit-to-width font scaling with pinch zoom and horizontal pan (no pane
   resize), pull up for scrollback pages, tap a prompt mark to jump.
6. **Take over** (P3): claims the pane (desktop shows "Controlled from Pixel 9 · Take back"). Adds an **extra-keys row**
   (Esc, Tab, sticky Ctrl, Alt, ←↑↓→, `|` `~` `/` `-`, PgUp/PgDn, ^C, ^D) and a **line-input box** that sends a
   whole line on Enter, which avoids Android composition corruption [45]. Direct key mode is optional. **Hand back**
   returns control. A desktop keystroke always wins, as in the delegate/take-over issue.
7. **Password prompt**: notification → a secure field sends the bytes plus Enter. It is never logged, never kept in
   the ring, and is off per device until enabled.
8. **Notifications** (Web Push): agent turn finished (> 30 s, configurable), agent or program waiting for input
   (OSC 133 prompt after a running command, or termios read), password prompt, command > 30 s finished, turn failed,
   plan ready, subagent finished. Suppressed while the desktop session is active and focused, like Claude Code's
   presence rule [16]. The desktop encrypts the body to the subscription keys, so the rendezvous only forwards
   ciphertext. Text is minimal ("api · agent finished").
9. **Board** (later): card list per tab, card thread; add a card by voice.

## 6. Pairing, devices and accounts

| Method | Use | Phase |
|---|---|---|
| **QR device pairing** | Desktop Settings → Remote → "Pair phone" shows a QR code for `https://app.relay-terminal.ai/pair#v=1&d=<desktop pubkey>&s=<128-bit one-time secret>&r=<room>`. The fragment never reaches a server. The phone proves the secret, sends its public key, and the desktop shows "Pixel 9 · Chrome wants access: View / Agent / Full · Allow". Secret valid 5 min, single use | **P1 default, no account** |
| **Pairing code** (no camera: laptop guest) | 3-word code + PAKE (CPace/SPAKE2, magic-wormhole style) | P1 |
| **Passkeys** | Accounts for multiplayer identity; PRF-derived key to back up device keys across the user's browsers [44] | P4 |
| **GitHub OAuth** | "Invite @alice": identity and display name for teammates (as upterm authorizes by GitHub user [25]) | P4 |
| Google OAuth, magic link | Recovery or non-GitHub guests; magic link needs an email service | Later, only if asked |

Accounts identify people; they never unlock content. Content keys still come from pairing or invites.

## 7. Multiplayer

- **Unit of sharing:** a pane, or a tab of panes, never "the whole desktop". The host desktop is the hub: every
  participant has a pairwise Noise session with the host, and the host fans out (star). No group key agreement is
  needed, and removing someone means closing their session. Works over the internet through the rendezvous; LAN
  guests go direct through ICE host candidates.
- **Roles** (Teleport/Zed vocabulary [29][31]): **Owner** (the host user: everything, including revoke and end),
  **Editor** (may request terminal control and submit agent prompts), **Viewer** (read only; default for new invitees,
  as in Live Share [28]).
- **Terminal control:** one driver per pane at a time, extending the human/agent model in `ARCHITECTURE.md` §9: `control
  = owner | agent | participant:<id>`. An editor taps **Request control**; the current driver or owner approves, or
  auto-approves if the owner opted in. The owner's physical keystroke always takes control back. The agent delegate
  flow uses the same token, so "agent in control" and "Alice in control" look the same.
- **Agent thread and queue:** editors can submit prompts. Queue items show the author (`queued · by alice`). Owner
  setting per share: *Guest prompts run immediately* or *Guest prompts wait for owner approval* (default). This moderates
  who talks to the owner's agent, which holds the owner's keys and shell; it is not a per-tool approval. Only the
  owner can change model, mode or reset.
- **Presence:** avatars on the pane header, who is viewing which pane, who is driving, per-participant selection
  highlights, "Follow" mode (Zed/Replit [29][30]). Viewers' viewports never change the pane size.
- **Invites:** link `https://app.relay-terminal.ai/join#<invite secret>` with role, expiry (default 24 h, max 7 d), use
  count, and optional required GitHub identity. Joining **knocks**; the owner admits ("alice (GitHub) wants to join as
  Editor"). Revoke from the participants list: immediate disconnect, the invite is burned, and the rendezvous is told.
- **Moderated sessions:** option "Guests can act only while I'm present" (desktop unlocked and Relay focused in the
  last N minutes), plus **Pause all guests** and **End share** in the pane header and tray.
- **Audit log (local on the host):** `~/.local/share/relay/remote/audit-YYYY-MM.jsonl` records pairings, joins, role
  changes, control handoffs, prompts submitted (text), shell lines sent by others (line text; raw keys as byte counts),
  password-field use (redacted), revocations. Viewable in Settings → Remote. Never uploaded.

## 8. Threat model

Remote access is **remote code execution by design**: a Full device or an Editor with control can run anything the
owner can.

| Threat | Mitigation |
|---|---|
| Stolen or unlocked phone | Per-device capability (View / Agent / Full). Password entry is a second per-device switch, off by default, and every write needs a desktop-minted single-use permit bound to the live prompt (`REMOTE-PROTOCOL.md` §6.7). Devices idle 30 days expire. **Revoke** in desktop Settings deletes the pinned key locally, so it works even if the rendezvous is down. **Not** WebAuthn: a user-verification check inside the web app enforces nothing, because hostile JavaScript skips its own `if` and the desktop sees no proof, so it was dropped from this table on 2026-09-18 rather than counted as a mitigation |
| Leaked pairing QR or invite link | One-time, short expiry, desktop-side confirmation with the device name, knock-to-admit for invites |
| Compromised or malicious rendezvous | Sees metadata only. Pinned static keys stop MITM, including of WebRTC DTLS. It can deny service. Self-hostable binary, and tailnet private mode (b) |
| Compromised static web origin (malicious JS) | Separate origin and credentials, CSP + SRI, reproducible builds with published hashes, private mode, native apps later [43] |
| XSS from terminal or agent output rendered on the phone | No `innerHTML`; sanitized Markdown; links need confirmation; CSP forbids inline script |
| Prompt injection amplified by remote prompts | Remote prompts are tagged `origin: remote:<device>` in the transcript and shown on the desktop. Guest prompts wait for owner approval by default. The agent's existing previews, guards and Stop remain |
| "Forgot it was on" | Remote access **off by default**. A persistent desktop indicator ("2 remote devices connected") like a screen-share badge. **Disconnect all** in the tray. Optional auto-off after N hours of no desktop activity |
| Secrets on screen | E2E; nothing stored server-side (unlike Warp's week-long unredacted storage [2]); password field never logged |
| Key storage | Desktop identity key in the Secret Service keyring; phone keys non-extractable in IndexedDB; API keys never leave the desktop and are never sent over RRP |

## 9. Phases

| Phase | Scope | Needs | Effort |
|---|---|---|---|
| **P0 Spec** | RRP/1 message spec (`docs/REMOTE-PROTOCOL.md`), Noise suite choice, security review of this doc, rendezvous API, pairing written so settings sync (`#05J2`) reuses it | Owner answers below | S (≈1 wk) |
| **P1 Agent companion** | `relay-rendezvous` (registry, signaling, WebSocket relay, Web Push); `RemoteHub` in the GUI; PWA with QR pairing, inbox, thread, composer, queue, Stop, plan review, voice via desktop, notifications; device list and revoke; desktop indicator; remote privacy page on the site and the `ROADMAP.md` non-goal edits (section 3.2). **Works with KonsolePart** (no Screen stream); generic "waiting for input" is best-effort until `#YR21` (section 12) | Hosting actions (app origin, cloudflared ingress rule) | M (≈5–7 wks) |
| **P2 Terminal view** | Screen snapshot/diff from the view's frame, **`VtCore::historyLines` in both cores**, scrollback pages, resync, WebRTC P2P upgrade (desktop: libdatachannel, MPL-2.0, GPL-compatible under MPL §3.3), latency display | Engine panes (`--engine=relay`) | M (≈4–5 wks) |
| **P3 Take over** | `keys`/`paste`/line input, extra-keys row, control token with the delegate/take-over work, secure password field with a desktop-minted permit (the WebAuthn re-check was dropped, §6.7) | P2; delegate/take-over control model | S–M (≈2–3 wks) |
| **P4 Multiplayer** | Accounts (passkeys + GitHub), invites, knock, roles, presence and follow, guest prompt moderation, audit log, pause and end; guests use the same web app | P3 | L (≈6–10 wks) |
| **P5 Native apps** | Android (Kotlin or React Native) then iOS; APNs/FCM through the rendezvous with encrypted payloads; notification actions (Stop, Reply) | P1–P3 protocol stable | L (≈8–12 wks per platform, less with a shared RN codebase) |
| Optional | Private tailnet mode (desktop serves the PWA); self-host guide for `relay-rendezvous`; predictive echo; Board on phone | | S each |

## 10. Owner decisions needed

1. **App origin**: add `app.relay-terminal.ai` (a new vhost on the shared nginx host; safer key isolation), or ship
   under `relay-terminal.ai/app/` with no server change?
2. **Rendezvous location**: a systemd binary on the existing host through a cloudflared ingress rule (≈$0, one more
   service on a shared box), or Cloudflare Workers + Durable Objects (no box risk, Cloudflare-specific)?
3. **Accounts timing**: pairing-only until multiplayer (recommended), or accounts from P1 so any browser can list your
   desktops? Which providers: passkeys + GitHub, or also Google or magic link?
4. **Guest agent prompts**: default to *wait for owner approval* (recommended) or *run immediately* for Editors?
5. **Password entry from the phone**: allow (off per device by default), or never?
6. **Native order**: confirm Android first, and native (Kotlin/Swift) vs React Native/Expo?
7. **Multiplayer reach**: internet guests by invite (recommended), or same-network/tailnet only at first?
8. **Private tailnet mode and self-hosted rendezvous**: in P1–P2 for the privacy story, or later?
9. **Ship P1 before the engine is the default**, i.e. agent-only remote while terminals stay KonsolePart?
10. **Update ROADMAP non-goals** ("Relay server", "browser-based terminal") as in section 3.2, and publish a remote
    privacy page (metadata kept, retention, E2E limits of a web client).

## Sources

- [1] Warp docs, Remote Control: https://docs.warp.dev/agent-platform/cli-agents/remote-control/ · [2] Warp docs, Agent Session Sharing: https://docs.warp.dev/agent-platform/local-agents/session-sharing/
- [3] Warp on X (remote control announcement): https://x.com/warpdotdev/status/2045253339055595649 (search summary) · [4] warpdotdev/warp #13474: https://github.com/warpdotdev/warp/issues/13474
- [5] warpdotdev/warp #16060: https://github.com/warpdotdev/warp/issues/16060 · [6] warpdotdev/warp #10108: https://github.com/warpdotdev/warp/issues/10108 (search summary)
- [7] Sesori, Warp Remote Control alternatives (competitor): https://sesori.com/alternatives/warp-remote-control/ · [8] Blink Shell: https://blink.sh/
- [9] Blink Code docs: https://docs.blink.sh/advanced/code · [10] Moshi, "Best iOS terminal app 2026" (competitor; search summary): https://getmoshi.app/articles/best-ios-terminal-app-coding-agent
- [11] ShellDrop vs Blink (competitor): https://shelldrop.sh/blog/shelldrop-vs-blink-shell/ · [12] App Store reviews, Blink Shell: https://apps.apple.com/us/app/blink-shell-build-code/id1594898306?see-all=reviews&platform=ipad
- [13] blinksh/blink #2177: https://github.com/blinksh/blink/issues/2177 · [14] mosh: https://mosh.org/
- [15] Moshi vs Blink (competitor): https://getmoshi.app/compare/blink · [16] Claude Code docs, Remote Control: https://code.claude.com/docs/en/remote-control
- [17] OpenAI, Codex remote connections: https://learn.chatgpt.com/docs/remote-connections · [18] TechCrunch, Cursor web app: https://techcrunch.com/2025/06/30/cursor-launches-a-web-app-to-manage-ai-coding-agents/ (search summary)
- [19] Cursor docs, Web & Mobile: https://cursor.com/docs/cloud-agent/web-and-mobile (search summary) · [20] happier-dev/happier: https://github.com/happier-dev/happier (search summary)
- [21] Happy security docs: https://happy.engineering/docs/security/ (search summary) · [22] ekzhang/sshx README: https://github.com/ekzhang/sshx
- [23] "How terminal-sharing tools put your shell in a browser" (83-bit key, Argon2id): https://dev.to/lovestaco/how-terminal-sharing-tools-put-your-shell-in-a-browser-328 (search summary) · [24] Self-hosted uptermd (tmate relay shutdown note): https://github.com/Self-Host-Server/upterm (search summary)
- [25] owenthereal/upterm: https://github.com/owenthereal/upterm (search summary) · [26] tty-share docs: https://tty-share.com/how-it-works/ (search summary)
- [27] tsl0922/ttyd: https://github.com/tsl0922/ttyd · [28] Microsoft Learn, Live Share share a terminal: https://learn.microsoft.com/en-us/visualstudio/liveshare/use/share-server-visual-studio-code (search summary)
- [29] Zed docs, Collaboration and Channels: https://zed.dev/docs/collaboration/channels (search summary) · [30] Replit docs, Multiplayer: https://docs.repl.it/replit-workspace/workspace-features/multiplayer (search summary)
- [31] Teleport docs, Moderated Sessions / Joining Sessions: https://goteleport.com/docs/admin-guides/access-controls/guides/moderated-sessions/ (search summary) · [32] Winstein & Balakrishnan, "Mosh", USENIX ATC 2012: https://mosh.org/mosh-paper.pdf (not opened)
- [33] Eternal Terminal: https://eternalterminal.dev/ (search summary) · [34] BlogGeek.me, TURN: https://bloggeek.me/webrtcglossary/turn/ (search summary; vendor estimates)
- [35] iroh FAQ: https://docs.iroh.computer/about/faq (search summary) · [36] Tailscale docs, DERP servers: https://tailscale.com/docs/reference/derp-servers (search summary)
- [37] Tailscale docs, Funnel: https://tailscale.com/docs/features/tailscale-funnel (search summary) · [38] Cloudflare Tunnel FAQ and WebSockets docs: https://developers.cloudflare.com/cloudflare-one/faq/cloudflare-tunnels-faq/, https://developers.cloudflare.com/network/websockets/ (search summary)
- [39] Cloudflare Durable Objects pricing: https://developers.cloudflare.com/durable-objects/platform/pricing (search summary) · [40] Cloudflare Realtime TURN FAQ: https://developers.cloudflare.com/realtime/turn/faq/ (search summary)
- [41] WebKit, Web Push for Web Apps on iOS and iPadOS: https://webkit.org/blog/13878/web-push-for-web-apps-on-ios-and-ipados/ · [42] Apple Developer Forums, web push actions on iOS 16.4: https://developer.apple.com/forums/thread/726793 (search summary)
- [43] Meta Engineering, Code Verify: https://engineering.fb.com/2022/03/10/security/code-verify/ (search summary) · [44] Corbado, Passkeys & WebAuthn PRF: https://www.corbado.com/blog/passkeys-prf-webauthn (search summary)
- [45] xtermjs/xterm.js #3600 (Android input corruption): https://github.com/xtermjs/xterm.js/issues/3600 (search summary) · [46] xtermjs/xterm.js #5377 (limited touch support): https://github.com/xtermjs/xterm.js/issues/5377 (search summary)

## 11. Owner decisions (2026-09-17)

Recommendations accepted for 1, 2, 5, 6, 7, 8, 9 and 10. Changes:

- **3. Accounts:** pairing-only until multiplayer, but an invite can also be shared as a long unguessable link (e.g. by
  email), no account needed. When accounts arrive: passkeys plus GitHub and Google sign-in.
- **4. Guest agent prompts:** a guest's first command asks the owner to approve, with **Approve once** or
  **Approve always** (for that guest).

## 12. Design review against the code (2026-09-17)

This section records what a pass over `engine/`, `src/`, `backend/` and `docs/ARCHITECTURE.md`
changed in the design above, and the owner decisions that came out of it.

### 12.1 Corrections

| Claim in the first draft | What the code says | Now |
|---|---|---|
| Screen diffs come from `VtCore::updateFrame` dirty rows | `updateFrame` **consumes** the dirty state (`engine/core/VtCore.h`, pinned by `engine/tests/CoreTest.cpp`), and `engine/view/TerminalView.cpp` is its only caller. A second consumer would steal dirty rows and stop the local view repainting | `RemoteHub` taps the `ViewportFrame` the view already built. No core change, and the phone is guaranteed to see what the desktop sees |
| `history_get` returns pages of styled lines | There is no styled history read. `VtCore::historyText(maxLines)` is plain text, and `scrollViewportToRow` is stateful and shared, so a phone paging back would drag the desktop user's viewport | P2 adds a const `VtCore::historyLines(from, count, out)` to **both** cores (libvterm and ghostty) with tests. P2 grows to ≈4–5 weeks |
| libdatachannel licence "(verify licence fit)" | MPL-2.0 §3.3 permits distributing a larger work under the GPL; Relay is GPL-3.0-or-later | Resolved, no constraint |
| Voice needs a phone-side transcription path | `backend/relay_core/voice.py` already takes a **path**, accepts `.webm`, and treats the clip as untrusted data with an anti-injection system prompt | P1 writes the received blob into the pane's 0700 runtime dir and reuses that path |
| `--engine=vterm` | `--engine=relay` is the flag; `--engine=vterm` is a documented alias (`docs/ENGINE.md`) | Written as `--engine=relay` |

### 12.2 Decisions (2026-09-17)

- **Scrollback:** add the styled `historyLines` API to both cores rather than shipping plain-text
  scrollback. The phone's scrollback must not look worse than the live screen above it.
- **Rendezvous:** Python (asyncio, `websockets`, SQLite) in `rendezvous/`, not a Go or Rust binary.
  The server only fans out ciphertext, so throughput is not the constraint, and `backend/` already
  requires the toolchain that `ci.yml` and every self-hoster would need.
- **`waiting for input` notifications:** ship P1 with the known gap and document it. Per
  `ARCHITECTURE.md` section 9, detecting a program blocked in `read()` needs
  `/proc/<pid>/syscall`, which `sudo`, `doas`, `pkexec`, `su` and other users' processes do not
  expose; only their *password* prompts are detected. Password prompts and agent events are
  reliable today and carry most of the value. `#YR21` (screen-text input detection) improves the
  general case later with no protocol change.

### 12.3 Settled in the protocol spec, not here

`docs/REMOTE-PROTOCOL.md` carries these; they are noted here so the design is not read as silent
on them.

- **`RemoteHub` is one per process**, like `NotificationCenter` ("one centre per process, shared by
  every window", `src/Notifications.h`), keyed by the pane session token that `Notification::source`
  already carries, in a new `src/RemoteHub.{h,cpp}` — not in `src/main.cpp`, which is 9 678 lines.
- **Remote password entry** (owner decision 5) must not weaken the `Secret` invariants of
  `ARCHITECTURE.md` section 9: a password reaches only the masked field and `relay::input::Secret`,
  never the queue, ledger, session file or logs. So it travels in its own frame type, outside the
  `seq` ring and outside replay, is never queued while offline, and is redacted in the audit log,
  with tests mirroring `tests/inputpolicy_test.cpp`.
- **Guest identity without an account.** Owner decision 4's "Approve always" for a guest needs an
  identity, and decision 3 allows invites that are a bare unguessable link. The approval is bound to
  the **device key pinned at join**; revoking the device or burning the invite revokes it.
- **Pairing is specified on its own**, so settings sync (`#05J2`, which plans to use "the same
  pairing machinery as remote access") reuses it instead of growing a second one.

### 12.4 Security review (2026-09-17)

P0's security review found four critical and nine high findings. They are recorded as normative
rules in `docs/REMOTE-PROTOCOL.md`; the ones that changed the design rather than only the wording:

- **The rendezvous must never hold push subscription keys.** `p256dh` and `auth` are content keys,
  so a rendezvous holding them could forge a "password prompt" notification and read every body.
  The subscription now goes to the desktop inside the Noise session, and the body is sealed to the
  device's pinned key inside the RFC 8291 payload. The push subscribe endpoint is gone.
- **`agent` was shell access.** A remote `compose` entered the composer's own routing, which honours
  a shell prefix, so an `agent` device could run commands with no agent in the loop. A remote
  `compose` is now always routed to the agent; the shell route needs `full`.
- **Revocation and downgrade have to reach live sessions.** They only governed the next handshake,
  and a session may live for days.
- **Pairing needed an authentication string.** The dialog was approving a name the device chose for
  itself. Both ends now derive a five-digit code from the handshake hash and the user compares them.
- **`plan_execute` took a path**, which `planning.read_plan` would have read from anywhere on disk
  and fed into a prompt and back to the phone. It takes a desktop-minted id now, and the general
  rule is that no path, filename or worker request type is ever accepted from the wire.
- **Registration needed proof of possession** and a `desktop_id` derived from the key, or anyone
  knowing a public key could take a token for that desktop.
- **One transport at a time.** A Noise cipherstate has no reorder window, so "frames simply arrive
  on the other path" would have forced a replay window on the very path the rendezvous sits on.

Outstanding, in parts that are refused rather than half-built: the `secret_input` path needs a
desktop-minted prompt nonce and a fresh termios read at write time (P3), WebAuthn user verification
must be bound to the desktop or dropped from the threat table, and the `transport_switch` handshake
is unwritten (P2).

### 12.5 Security review of the four things P0's review did not cover (2026-09-18)

Web Push, password entry from a phone, voice from a phone and multiplayer all landed after
section 12.4, so each of their **(security)** paragraphs was a claim nobody had attacked. A second
review did, with a test per claim in `tests/test_remote_security.py`. Eight findings, all fixed in
`remote/` and `rendezvous/`; the ones that change a rule rather than a line:

- **The push route was a request-forgery proxy.** A `view` phone chooses the endpoint URL and the
  rendezvous — the one process on the hosted side with a network — makes an authenticated POST to
  it. Nothing said which hosts, and registering a desktop proves possession of a key rather than
  any right to be there, so anyone who could reach the server could aim it at a private address.
  `/v1/push/send` now posts only to a browser's push service, matched by name so there is no DNS
  rebind to race; `RELAY_PUSH_HOSTS` is how a self-hoster adds their own.
- **A password permit outlived its prompt.** The nonce is bound to a prompt generation, but the
  generation only moved when a *new* permit was minted, so for its 45 seconds the permit survived
  the prompt ending. A process that asks twice — `ssh` wanting a key passphrase and then a
  password — keeps its pid, so the phone would have answered the second prompt with the decision
  the person made about the first. The permit is burned and the generation advanced the moment the
  pane stops being at a prompt.
- **"A key is a device or a participant and never both" held in one direction.** `admit` refuses a
  key the device store knows; nothing stopped `pair_prove` pairing a key the *guest* store knows,
  and the handshake reads devices first, so that guest's next connection would have carried a
  capability instead of a role. A guest watching a shared pane can read the pairing QR the moment
  the owner opens it there, so the secret is not the obstacle it sounds like.
- **One address could take every channel slot** on a desktop and keep the owner's phone out, which
  section 8 of the protocol forbids by saying sockets are counted per device. Per-device connect
  tokens do not exist; the budget is capped per peer address meanwhile, and the protocol now says
  plainly that the rule is unmet and why finishing it is the owner's call.

The rest: a `queued` event could carry another person's words to a guest through a field the
filter did not strip, an unaddressed transcript with two clips in flight went to whichever phone
came first, the audit log was umask-mode between creation and its first chmod, and the phone's
queue Remove button sent a field the desktop did not read.

### 12.6 What shipped, and what the phases mean now (2026-09-18)

P0, P1's companion, P2's screen stream and P3's take-over are in and used from a real iPhone and
iPad. The phase table in section 9 still reads as a plan; against it:

| Phase | Where it stands |
|---|---|
| P0 spec | Done, with the security review folded in |
| P1 agent companion | Pairing, panes, prompts and the agent are in. **Notifications are not**, which is most of this phase's value |
| P2 terminal view | Screen stream, take-over and direct typing in. No scrollback paging |
| P3 take over | In, except answering a password prompt from the phone |
| P4 multiplayer | Not started |
| P5 native apps | Not started |

Two things the design did not anticipate, both confirmed by use:

- **The agent needs no stream of its own.** Relay prints agent output into the pane's terminal
  (`ARCHITECTURE.md` section 8), so the screen stream already carries it. The phone shows one
  prompt box, and the answer arrives where the commands do.
- **Sharing is no longer engine-gated.** KonsolePart's retirement removed the constraint this
  document treats as central to P1 versus P2.

### 12.7 Still open

- **Hosting actions are owner-only and on the critical path**: the `app.relay-terminal.ai` vhost and
  the `rv.` cloudflared ingress rule are server-side changes on a box running a dozen sites, exactly
  what `deploy.sh` refuses to touch. P1 can be built and tested against a loopback rendezvous, but
  cannot be demonstrated on a real phone until they are done. Schedule them early in P1.
- **QA evidence for a phone client** does not fit `docs/qa_evidence/` and the `needs_qa_llm` lane as
  they stand. Proposal: a loopback rendezvous plus a headless-browser check in `ci.yml`, with
  screenshots as the evidence artefact. To be settled when P1 starts.
- Sections 6 and 7 and the P4 row still describe accounts as arriving with multiplayer; section 11
  loosened that (link invites without accounts, plus Google sign-in). Reconcile when P4 is planned.
