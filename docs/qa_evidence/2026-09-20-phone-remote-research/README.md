# Phone remote control of coding agents — research, 2026-09-20

Scope: how Anthropic, OpenAI and the third-party field do "steer my computer's agents from my phone",
and what Relay should copy. Everything below is sourced; anything I could not confirm is marked
(unverified).

---

## 1. Anthropic — Claude Code Remote Control

Source: https://code.claude.com/docs/en/remote-control (fetched 2026-09-20);
launch coverage https://alternativeto.net/news/2026/2/anthropic-introduces-remote-control-to-claude-code-for-seamless-device-handoffs (2026-02-25)

- **The CLI stays on your machine.** `claude remote-control` (server mode), `claude --remote-control`
  (interactive), or `/remote-control` inside a session. "Claude keeps running locally the entire time,
  so your code execution and filesystem access stay on your machine." claude.ai/code and the Claude
  iOS/Android app are "a window into that local session".
- **Transport is outbound-only polling + a streaming relay.** "Your local Claude Code session makes
  outbound HTTPS requests only and never opens inbound ports." It registers with the Anthropic API and
  polls for work; the server routes messages between phone and laptop over a streaming connection. No
  E2E encryption: "the session transcript, including your messages, Claude's responses, and tool
  activity, is stored on Anthropic servers" — that stored transcript is what keeps devices in sync and
  lets the session reconnect. ZDR orgs cannot use it.
- **Pairing is account-based, not key-based.** Sign in with claude.ai (`/login`); API keys, long-lived
  `setup-token` tokens, Bedrock/Vertex/Foundry and any custom `ANTHROPIC_BASE_URL` are all refused. The
  session shows a URL and, on spacebar, a **QR code** that opens it in the Claude app. Sessions also
  appear by name under **Code** in the mobile app with a computer icon + green dot when online.
- **What the phone can do:** send messages mid-turn (queued), attach photos/files, approve permission
  prompts, answer `AskUserQuestion`, stop a running subagent/workflow, **view the diff of uncommitted
  changes** (computed on your machine on request, falls back to branch-vs-default-branch when clean),
  **switch model** and **effort level**, rename, and run a whitelist of slash commands as text
  (`/compact`, `/clear`, `/context`, `/usage`, `/model x`, `/effort high`, `/mcp`, `/config key=value`).
  `@` autocompletes paths from the *local* project.
- **What it cannot do:** no raw terminal view, no file editing, no `/plugin`, `/resume`, no custom
  output styles, no interactive pickers. Non-permission dialogs forwarded to the phone expire after
  5 minutes (`dialogExpiry`) and take the no-action default.
- **Sleep / network drop is designed for.** "If your laptop sleeps or your network drops, Claude Code
  reconnects automatically when your machine comes back online. While the connection is rebuilding,
  Claude Code queues messages, permission prompts, and status updates … and delivers them once the
  connection recovers." Limits: server mode gives up after ~10 min of no network and the process exits;
  an interactive session retries indefinitely; presence-heartbeat failures disconnect after ~30 min;
  stopped servers can be resumed for ~4 hours (`--continue`, `--session-id`).
- **Hard limitation: the local process must keep running.** Close the terminal and the session goes
  offline "within seconds". Docs tell you to use tmux/screen on a remote box.
- **Push notifications:** two toggles in `/config` — *Push when Claude decides* (model-chosen: long task
  finished, needs a decision; you can ask in-prompt "notify me when the tests finish") and *Push when
  actions required* (permission prompts and questions). No per-event config. Pushes are **suppressed
  while you are focused on the terminal**, and `CLAUDE_CLIENT_PRESENCE_FILE` extends that to "any time
  I'm at the machine" via a marker file a screen-lock hook creates/deletes. Docs call out iOS Focus
  modes/notification summaries and Android battery optimisation as the usual delivery failures.
- **Nudges toward the phone:** after a long turn the terminal shows "Still working — Check in from your
  phone"; after several permission prompts it shows "Approve tool calls from your phone" with the URL.
- **Enterprise-grade device auth exists** (Trusted Devices beta): per-device credential enrolled only
  shortly after a full sign-in, plus Face ID/Touch ID/passkey step-up if the sign-in is >18 hours old.
- **Server mode is multi-session**: `--capacity` default 32, `--spawn worktree` gives each new session
  its own git worktree — i.e. you can *start new conversations from the phone*, not just resume.
- **Adjacent Anthropic products:** *Claude Code on the web* / cloud sessions run on Anthropic infra and
  can be pulled down with `/teleport`; *Dispatch* (research preview, 2026-03-17) pairs phone→Mac by QR
  for general computer use, Pro+ only (https://claude.com/blog/dispatch-and-computer-use).

## 2. OpenAI — Codex on mobile

Sources: https://9to5mac.com/2026/05/14/openai-brings-codex-control-to-chatgpt-for-iphone-and-android/ (2026-05-14);
https://codex.danielvaughan.com/2026/05/15/codex-mobile-chatgpt-app-relay-architecture-remote-agent-control/ (2026-05-15);
https://chatgpt.com/codex/mobile/

- Shipped **inside the existing ChatGPT iOS/Android app** on 2026-05-14 — no separate app — and is
  available on **every plan including Free**.
- **QR pairing carrying a public key.** Codex for Mac shows a QR containing "the host's Ed25519 public
  key and relay connection details"; the phone stores the host identity in iOS Keychain / Android
  Keystore after first pair, so trust persists.
- **Relay, not P2P, and the relay is blind.** "OpenAI operates a relay service that routes encrypted
  sessions between devices signed into the same ChatGPT account… The relay can see connection metadata
  (session IDs, device IDs, handshake control messages) but cannot decrypt application payloads." This
  is the notable difference from Anthropic, whose relay stores plaintext transcripts.
- **Compute stays on the Mac**: filesystem, plugins, MCP servers, skills, browser/computer-use config.
- **Phone can:** start new threads, continue existing ones with full history, steer active work,
  approve commands/actions at the same approval gates as desktop, review diffs with syntax
  highlighting, view test results and screenshots, change model and reasoning effort mid-session,
  receive push for approval gates.
- **Phone cannot:** edit files directly, run manual shell commands, configure plugins or `config.toml`.
- **Sleep:** handled bluntly — a `mobile_keep_awake = true` setting keeps the Mac from sleeping while a
  mobile connection is live.
- **Platform limit at launch:** phone can only connect to the **macOS** Codex desktop app; Windows
  "coming", no date. (Linux host support: unverified.)

## 3. Third-party phone remotes

### Happy Coder (open source, the closest analogue to Relay's design)
Sources: https://github.com/slopus/happy; https://happy.engineering/;
https://news.ycombinator.com/item?id=44904039 (Show HN, 2025-08);
https://www.blog.brightcoding.dev/2026/02/19/happy-coder-the-secure-mobile-cli-revolution

- `npm i -g happy-coder`, then run `happy claude` or `happy codex` instead of the bare command — a
  **wrapper around the vendor CLI**, supporting both Claude Code and Codex.
- **End-to-end encrypted through a dumb relay server**: "code and encryption keys stay on your devices,
  only encrypted session data passes through the backend", using TweetNaCl ("same encryption as
  Signal"). MIT-licensed and **self-hostable**. Free; ~23.8k GitHub stars as of the 2026 roundup.
- Four packages: Expo app (iOS/Android/web), CLI, "Happy Agent" for remote session management, and the
  relay server. There is also a macOS desktop app.
- **Seamless handback**: phone takes over by "restarting the session in remote mode"; pressing any key
  on the laptop keyboard takes control back.
- Push notifications when the agent needs permission or errors; session spawning from the phone;
  machine list; diffs; file browsing; model switching; chat **and** terminal views.
- **Voice is a real agent, not dictation**: "Happy lets you drive the agent by voice, not just
  dictation… It takes your rambling and makes it comprehensible to Claude Code." Runs on ElevenLabs,
  20 free minutes per 30 days then paid, or bring your own key
  (https://happy.engineering/docs/features/voice-coding-with-claude-code/).

### Omnara
- YC S25, launched Feb 2026, $9/mo, iOS app. Also runs the agent locally, but **stores plaintext
  conversations server-side** — co-founder: "We don't have true E2EE yet because our service needs
  access to message content for cross-device sync, notifications, and agent execution."
  (https://codeongrass.com/blog/best-app-to-control-coding-agents-from-mobile/)

### Warp
Sources: https://docs.warp.dev/agent-platform/cli-agents/remote-control/; https://x.com/warpdotdev/status/2045253339055595649
- `/remote-control` chip publishes a running third-party agent session (Claude Code, Codex, OpenCode)
  **to Warp's cloud** and copies a shareable link. Open it in any browser — "no app install required" —
  or in the Warp desktop app.
- The web view **mirrors the desktop view including thinking steps, tool use and terminal output** —
  more than Anthropic shows. Real-time for all viewers.
- Multiplayer by design: grant view-only or edit (steer) access, teammates join the same conversation
  with live cursors; only the publisher can revoke.
- **No native mobile app** — it is a responsive web page opened on the phone.

### Cursor for iOS
Sources: https://cursor.com/changelog/ios-mobile-app (2026-06-29); https://cursor.com/help/ai-features/mobile-app
- **Native iOS app**, public beta, paid plans only, requires **iOS 26+**; no Android yet.
- Controls agents running **in the cloud and on your own computer**: start agents, watch them work,
  review and merge PRs.
- **Live Activities on the lock screen** track agent status, plus push when an agent finishes, needs
  input, or is ready for review. Voice prompts, screenshots, annotations.
- Deliberately not an editor: no full editor, terminal or file browser.

### DIY / niche
- **VibeTunnel** (https://github.com/amantus-ai/vibetunnel): `vt claude` publishes real terminal
  sessions to a browser; remote access via **Tailscale (recommended), ngrok, LAN or Cloudflare
  tunnel** — no vendor cloud. Native iOS app + responsive web. Session recording as asciinema.
- **Termux + Tailscale** (https://www.skeptrune.com/posts/claude-code-on-mobile-termux-tailscale/): SSH
  from the phone into a tmux session. Zero infrastructure, but a terminal on a touchscreen.
- **Live-Activity-only tools**: `ledge` (https://github.com/abhaymettu/ledge) and `ClaudeLive` — a Mac
  daemon consumes Claude Code **hooks** and pushes to APNs with your own key, showing
  working / waiting-for-permission / waiting-for-input / done, the running tool, elapsed time and the
  last prompt on the Lock Screen and Dynamic Island. Good evidence that *status glanceability* is worth
  building separately from the chat view.
- **Pushary**, **Greenlight**, **claude-remote-approver**: notification-and-one-tap-approve services.
- **Tactic Remote**, **Nimbalyst**, **AgentsRoom**, **MobileFlow**, **Grass**: native/mobile front ends
  for multiple agents; per-tool detail unverified.
- Not in this category: Jules, Devin, GitHub Copilot coding agent are **cloud-hosted** agents with
  mobile web/app review surfaces; they never touch your laptop (unverified in detail — I did not fetch
  primary sources for these three).

### What users praise / complain about
- Praise clusters on: push when a permission prompt appears, the diff view, and instant handback to the
  keyboard. Anthropic itself cites "the 99.9th percentile turn duration in Claude Code exceeded 45
  minutes" as the reason push exists
  (https://pushary.com/claude-code-notifications).
- Complaints cluster on: the laptop process dying (session offline instantly), one-remote-session-per-
  process outside server mode, no terminal view in the official client, notification delays from iOS
  Focus/summaries and Android battery optimisation, and Omnara-style plaintext server storage.

## 4. iOS platform constraints (2026)

Sources: https://www.magicbell.com/blog/pwa-ios-limitations-safari-support-complete-guide;
https://blog.codercops.com/blog/progressive-web-apps-2026;
https://tips.ojapp.app/en/pwa-ios-2026-complete-guide/;
Apple dev forums threads 772520 / 741130; https://lushbinary.com/blog/android-ios-push-notification-websocket-capacitor-signal-guide-2026/

- **Web Push works on iOS 16.4+ but only for a PWA added to the Home Screen.** A Safari tab can never
  receive push, even if permission is granted, and the permission prompt must be triggered by a direct
  user gesture (a button tap).
- **iOS 26 removed most of the Add-to-Home-Screen friction**: every site added to the Home Screen now
  opens in standalone mode by default. It is still a manual Share-sheet flow you must teach the user.
- Background Sync and Periodic Background Sync are **absent** on iOS. Storage quotas are tighter than
  Chrome's and **cached data is evicted after extended non-use** — so never make the PWA's local cache
  the only copy of anything. Screen Wake Lock finally works inside home-screen PWAs since iOS 18.4.
- Every iOS browser is WebKit, so capabilities are Apple's to grant.
- **No app, web or native, keeps a socket alive in the background.** iOS suspends an app as soon as it
  leaves the foreground. Silent push (`content-available`) *can* wake a native app to reconnect, but is
  explicitly best-effort: throttled if frequent, delayed/dropped in Low Power Mode, dropped when the
  background budget is exhausted, and **never delivered after the user force-quits the app**. "Silent
  push notifications should never be relied upon for real-time data."
- Therefore the only reliable design on either platform is: **visible push carries the payload**
  (or enough of it), and the socket is re-established when the user opens the app.
- **Native wins that a PWA cannot have on iOS**: Live Activities / Dynamic Island (what Cursor and
  `ledge` use), notification actions rich enough for one-tap approve-from-the-lock-screen (unverified
  whether Web Push notification `actions` render on iOS — Safari's support has historically been
  partial), Face ID / passkey step-up, Keychain-backed key storage, Shortcuts/Action button.
- **Capacitor** wraps an HTTPS URL in a native shell and gives you APNs/FCM push plus native plugins,
  keeping one web codebase; the standing App Store risk is **guideline 4.2 ("just a website")**, which
  is cleared by shipping real native value — push, biometric unlock, share sheet, Live Activities.
  I found **no evidence of a "remote terminal" category rejection**; Termius, Blink, Prompt and
  VibeTunnel all ship on the App Store. TestFlight (up to 10k external testers, 90-day builds) is the
  obvious distribution path for a single-owner tool and avoids review entirely for a while.

## 5. Voice

- **Claude Code's own `/voice`** is dictation only, and it is **not available remotely**: it needs a
  local microphone and "does not work in cloud sessions or SSH sessions"
  (https://code.claude.com/docs/en/voice-dictation). So on the phone, Anthropic gives you nothing
  beyond the OS keyboard's dictation.
- **Cursor iOS** lists "voice prompts" as a first-class mobile input (changelog above).
- **Happy** is the only one doing more than dictation: an ElevenLabs-backed voice *agent* that
  restructures rambling speech into a clean request before handing it to Claude Code.
- Pragmatic read: iOS keyboard dictation into the normal prompt field costs nothing, works offline on
  recent devices, and is what most users will actually use. A voice-agent layer is a later, separable
  feature, and it is the one place a paid dependency creeps in.

---

## What the best system copies — recommendations for Relay

1. **Keep the existing PWA-first plan, and wrap it with Capacitor for iOS rather than choosing.**
   The whole field converged on "the phone is a thin steering surface, the agent runs at home", which is
   pure UI — there is no native compute requirement. A Capacitor shell over the same web app buys APNs,
   background-capable push handling, Keychain key storage, Face ID unlock and (later) Live Activities,
   while the browser build still serves iPad/Pixel/Android tablet/desktop with no second codebase.
   Ship the PWA first (it is already designed), add the shell when the first concrete gap bites — the
   likely triggers are notification-action reliability and Live Activities. Distribute via TestFlight;
   App Store review for a remote-terminal app is not a demonstrated risk, and 4.2 is cleared by the
   native push/biometric/Live-Activity work you would be adding anyway.

2. **Keep the rendezvous relay, keep E2E, and copy Codex's pairing rather than Anthropic's.**
   Codex proves a blind relay is enough: Ed25519 host key in the QR, relay sees only session/device IDs
   and handshake, phone stores the host identity in Keychain/Keystore after first pair. Anthropic's
   plaintext-transcript-on-server model is the one thing in their design Relay should *not* copy, and
   it is also why they must refuse ZDR customers. Relay's existing QR + E2E design is right; make the
   QR carry the desktop's public key and pin it.

3. **Outbound-only from the desktop, always.** Anthropic's "outbound HTTPS only, never opens inbound
   ports" is the property that makes this usable from a corporate LAN, behind CGNAT, on the bus.
   Prefer a persistent WebSocket to the rendezvous with WebRTC as an optional direct upgrade on a
   trusted LAN (sphinxpad tests) — not as the primary path. Tailscale-only designs (VibeTunnel, Termux)
   are the fallback for people who want no cloud at all; offer it as a mode, not the default.

4. **The phone UI is a chat-plus-decisions view, not a terminal.** Everyone who shipped a good one
   (Anthropic, Codex, Cursor) refused the terminal; the ones that show a terminal (VibeTunnel, Termius)
   are praised as fallbacks, not as daily drivers. The concrete screen list, all confirmed as
   table-stakes by two or more systems: **session list with online/offline dot per pane** · **transcript
   with tool activity** · **permission prompt as a card with approve/deny** · **question card** ·
   **diff view** · **model + effort switcher** · **stop/interrupt** · **start a new conversation** ·
   **attach a photo**. Relay's advantage over all of them is that it already has *many* panes and
   guest agents in one window — lead with the multi-session list, which is Anthropic's `--capacity 32`
   server mode done natively.

5. **Notifications: two toggles, presence suppression, and treat push as the payload.**
   Copy Anthropic's exact taxonomy — *push when the agent decides something is worth telling you* and
   *push when an action is required* — because it maps onto Relay's existing needs-input state, and
   copy `CLAUDE_CLIENT_PRESENCE_FILE`: never push while the owner is at the desktop. Because neither a
   PWA nor a native app can hold a socket in the background, the notification must carry enough text to
   be useful on the lock screen, and the app reconnects and replays on open. Queue-and-deliver on
   reconnect (Anthropic queues messages, permission prompts and status updates through a drop) is the
   behaviour to implement on the desktop side.

6. **Add the glanceable status layer the official clients lack.** `ledge`/`ClaudeLive` exist only
   because Anthropic's app makes you open it to see whether an agent is stuck. A Live Activity (native
   shell) or, on the PWA, a single always-current notification per busy pane — state word, tool,
   elapsed time — is cheap and is the thing that makes "all day from the bus" work.

7. **Voice: ship OS dictation now, nothing else.** The phone keyboard's mic button into Relay's prompt
   field costs zero and matches what Claude and Cursor effectively give you. Revisit a Happy-style
   voice agent only if the owner finds dictation-then-fix too slow; it adds a paid third-party
   dependency for a marginal gain.

8. **Two failure modes to design for explicitly, because they are the top complaints.** (a) The desktop
   process exiting kills everything — Relay is a long-lived GUI app, so it should hold the rendezvous
   connection at the *application* level and keep serving every pane, which is strictly better than
   Anthropic's per-CLI-process model and worth saying out loud. (b) Sleep: Codex's answer is a
   `mobile_keep_awake` setting; Relay should offer the same opt-in (inhibit suspend while a phone is
   attached and an agent is running) plus Anthropic's queue-and-replay for when it sleeps anyway.
