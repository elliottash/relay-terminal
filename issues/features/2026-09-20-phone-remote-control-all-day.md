---
id: PH0N
type: work
status: executing
labels: [feature, remote]
component: [gui, remote]
milestone: beta
workstream: remote
assignee: claude-code
rank: 6c
created: '2026-09-20'
source: 'owner, 2026-09-20, Claude Code session'
acceptance: from the iPhone, over any network, all day and without touching the desktop, the owner opens the app and sees every pane on the desktop with who needs him, reads what an agent did, steers or stops it, answers its question, switches model, starts a new conversation, and gets a lock-screen notification when a turn finishes or an agent waits; the desktop keeps working through drops and sleep of the phone; content stays end-to-end encrypted
links: {plans: [], commits: [80706293, 38cf319b, 2c59a720, f39cb626, 8fc8d60b, baebd13a, 8e1e740f, e97c5fc8, 0d021852, ee12ac1a, 954c9f9f, 8e9b2e05], evidence: [docs/qa_evidence/2026-09-20-phone-remote-research/], related: [W5N2, 0VT4, 97EG, T4BS, JQ7R, KBFT, WMXN, GT7X, PF4K], github: null}
---
# Phone remote control, all day: always on, reachable from anywhere, and the last mile on the iPhone

## Issue

help me plan building out the mobile phone remote control. i want smooth easy control of my computer agents through relay terminal from my phone.

understand the system and grab all the related cards and see where we are at.

for coordination, note i am working on profiling on a pixel in session 190b9863-8c9d-4f45-9720-a79823df1a6e

but my main phone is an iphone.

research how other systems do this, eg the codex / claude apps, to build the best system.

browser app would be great, but i am fine to build an iphone app for this if it will be the best solution.

i want to be able to access my computer and run stuff all day from work / the bus / etc.

## Decisions

- Owner, 2026-09-20: "kick it off with efficient orchestration on opus subagents". The eight
  recommendations in Planning notes are taken as the working assumptions (always-on off by
  default and on for the owner's machine; every pane with a screen published to the owner's own
  devices; PWA first, Capacitor shell only if Phase 3 says so; connect tokens in Phase 1; WebAuthn
  dropped; `full` devices admit, decide and grant; diff view later; one origin) until the owner
  says otherwise.

## Planning notes

### Where it stands (2026-09-20)

Most of the phone client exists and was proved in one headless session on 2026-09-18 under #W5N2
and #0VT4: QR pairing with the five-digit check, `view`/`agent`/`full` grants, the pane list, the
live screen, the pane view drawn from `pane_state` (queue rows, reasoning, model picker,
conversations sheet, new conversation), one routed prompt box, take-over with the extra-keys row,
voice transcribed on the desktop, push *subscription*, multiplayer with knock and control handoff,
meeting code + PIN (#97EG). P0 was used on a real iPhone and iPad once (four real-device bugs
found and fixed). The evidence is `docs/qa_evidence/2026-09-18-remote-end-to-end/` and the
sibling folders on #W5N2.

What stops "all day from the bus" is not the UI. It is seven gaps, in order of weight:

| | Gap | Where |
|---|---|---|
| A | **Nothing is shared until the share button is pressed on one pane, in this Relay session.** The sidecar starts on demand (`RemoteShare::ensureSidecar`, `src/RemoteShare.cpp:139`), a pane is published only by `sharePane` (`:455`), the address is chosen per share and never remembered, and the hosted registration is dropped at restart (`use_hosted`, `remote/gui_host.py:886`). A phone paired yesterday that opens the app after a desktop restart sees nothing. | `src/RemoteShare.{h,cpp}`, `remote/gui_host.py`, `remote/host.py` |
| B | **The hosted rendezvous is live but stale.** `https://join.relay-terminal.ai` answers 200 today (`/`, `/v1/health`), but the `app.js`, `pane.js` and `sw.js` it serves differ from `main` (last `app/` commit `c1880026`, 2026-09-20). There is no deploy script; `deploy.sh` pushes only the marketing site. | `rendezvous/`, `app/`, elliott-main-1 |
| C | **A push notification has never been seen on a real phone.** Subscription and sealing are tested (`tests/test_remote_push.py`, 62 cases; the end-to-end run's FAIL was "no push arrived at the service"). iOS delivers Web Push only to a Home-Screen-installed PWA on a real-certificate origin; join.relay-terminal.ai now is one. The manifest ships only an SVG icon, and iOS wants a PNG for the home screen. | `app/sw.js`, `app/manifest.webmanifest`, `remote/push.py` |
| D | **Phone UI holes.** No Stop once the pane view is mounted (`#composer-stop` is hidden by `app/app.js:423` and `:575`; `app/pane.js` has none). No Recap button, and the hook that would answer one sends a reason the worker rejects (#WMXN). The planner's questions (sessions protocol 27) are forwarded but no client draws them (`docs/REMOTE-PROTOCOL.md:777`), so the phone sees only the mirrored text. Knock admit, guest-prompt decisions and control grants are `OWNER_ONLY` (`remote/wire.py:88-101`), so the owner cannot admit a friend from the bus. The inbox has no "needs you" order and no status chips. | `app/app.js`, `app/pane.js`, `remote/wire.py`, `src/Pane.h` (one hook) |
| E | **Backgrounding.** iOS kills the socket within seconds of the app leaving the foreground; the client reconnects on `visibilitychange` and calls `rrp.resume()` (`app/app.js:1365`, `:154`), but a prompt typed while offline is not queued and there is no "sent when back online" state. | `app/app.js`, `app/rrp.js`, `app/pane.js` |
| F | **Two security items become live once every desktop is permanently registered at a public rendezvous.** Per-device connect tokens are "Not built" (`docs/REMOTE-PROTOCOL.md:565`): anyone who ever held a link can compute the `desktop_id` and fill its channel budget. WebAuthn binding is undecided on #W5N2. | `rendezvous/server.py`, `remote/pairing.py`, `remote/host.py` |
| G | **Real-device QA outstanding**: #KBFT's nine iPad/iPhone keyboard checks, password entry from the phone, a real microphone, lock-screen push. | the owner's iPhone |

### What the other systems do, and what to copy (research 2026-09-20)

Full notes in the thread. The short version:

- **Codex in the ChatGPT app** (2026-05-14): a *blind relay*, the host's public key in the QR,
  pinned in the phone's keychain, the relay sees metadata only. That is what Relay already has
  (Noise IK, key in the QR fragment, `rendezvous/`). Phone can start threads, steer, approve,
  review diffs, change model and effort; cannot edit files or run shell. `mobile_keep_awake`
  holds the Mac awake while a phone is attached.
- **Claude Code Remote Control**: the CLI stays on the laptop, outbound HTTPS only, transcript
  stored on Anthropic's servers (the one thing not to copy). Worth copying: **queue and replay**
  across drops; **two push toggles** ("when Claude decides" / "when actions required"); push
  **suppressed while you are at the terminal** (we have `window_active`); the terminal nudge
  "check in from your phone"; server mode with many sessions, which is our pane list done
  natively; a message waits while the laptop sleeps and lands when it wakes.
- **Cursor iOS** (2026-06-29, native): Live Activities on the lock screen with agent state,
  push on finish / needs input / ready for review; no editor or terminal by design.
- **Happy Coder** (open source, Expo + PWA, E2E through a dumb relay): handback on any laptop
  keypress (we have it), machine list, diffs, both chat and terminal views.
- **Warp** remote control: web only, mirrors the desktop into Warp's cloud, no push.
- Everyone who shipped a good one made the phone a **steering surface, not a terminal**. Relay's
  edge is many panes and guest agents (Claude Code, Codex) in one window, all already terminals
  with a screen stream, so the phone gets them for free.

### Browser app or iPhone app

**Recommendation: keep the PWA first, add a thin native shell only after the PWA has been used
for a week from LTE.** Everything on the critical path (A, B, D, E, F) is desktop-side plumbing
and deploy, and the same web code would sit inside a native app anyway. What a native shell adds
is real: APNs delivery without the installed-PWA dance, Keychain key storage, Face ID unlock,
notification actions, and a **Live Activity** per running pane on the lock screen. None of that
is what is missing today. When Phase 3 shows push or reconnect is not reliable enough on the
PWA, Phase 4 wraps `app/` in Capacitor and ships through TestFlight (no store review for a
single-owner tool; Termius, Blink and VibeTunnel show the category is not refused).

Not taken: Swift-native from scratch (two codebases for one steering surface), Qt for iOS,
pixel streaming (owner decision on #0VT4), Tailscale-only (fine as the private mode, not the
default for a phone on LTE).

### Questions for the owner (answers go into `## Decisions`)

1. **Always on by default?** Recommendation: a fresh install starts with remote control off;
   Options › Remote has one switch, remembered across restarts, with the persistent chrome
   indicator while on. Your machine: on.
2. **Which panes reach your own phone automatically?** Recommendation: every pane that has a
   screen, guest agents included, since a Claude Code pane is a terminal like any other. The
   alternative is agent panes only.
3. **Native app: PWA first, then a Capacitor shell over the same `app/` through TestFlight** (not
   Swift from scratch). Recommendation: yes.
4. **Per-device connect tokens in Phase 1** (a protocol change, about two days): recommendation
   yes, because Phase 1 makes every desktop permanently reachable by name.
5. **WebAuthn**: drop it from the threat table; the per-device password switch covers the stolen
   phone case. Recommendation: drop.
6. **Let your own `full` devices admit knocks, decide guest prompts and grant control** (today
   `OWNER_ONLY`). Recommendation: yes; a `full` phone can already run any command.
7. **A diff view on the phone** (the diff decisions live in the desktop's diff view, #GT7X): Phase
   2 or later? Recommendation: later, after the day-long use says whether it is missed.
8. **One origin**: keep the app on join.relay-terminal.ai rather than adding app.relay-terminal.ai.
   The design wanted a separate origin so marketing scripts could never share key storage; the
   join origin serves no marketing scripts. Recommendation: keep one origin.

## Plan

**Goal.** The acceptance line above, on the owner's iPhone, over LTE, for a working day.

**Findings.** The gap table in Planning notes, with paths.

**Steps.** Four phases; each lands through its own cards or this one's tasks, by Opus subagents
with one named area each, committing through `scripts/land.py`.

*Phase 1: always on (desktop and hosting).*

1. `Options › Remote` gains "Remote control" (on/off, remembered in settings). On: the sidecar
   starts at Relay launch, registers at the hosted rendezvous (`use_hosted` on start, the chosen
   address remembered; falls back to tailnet, then local, and says which), holds one outbound
   socket, re-registers after a drop. Off: today's behaviour. `SettingsPane`, `RemoteShare`,
   `remote/gui_host.py`.
2. The owner's own paired devices receive every pane automatically: each pane with a screen is
   published when it opens and withdrawn when it closes, with no share button pressed. Guests
   are untouched (still per pane or tab, by invite). The chrome shows "N devices" while any is
   connected, with "Disconnect all" (design §8, "forgot it was on").
3. Per-device connect tokens minted at pairing and checked by the rendezvous (`docs/REMOTE-PROTOCOL.md` §8,
   the rule marked "Not built"); revoked with the device.
4. `rendezvous/deploy.sh`: rsync `remote/`, `rendezvous/`, `app/` to `/opt/relay-rendezvous` on
   elliott-main-1, restart `relay-rendezvous.service`, check `/v1/health` and that the served
   `app.js` hash equals the repo's. Run it, and add the step to `docs/RELEASING.md`. PNG and
   maskable icons in the manifest.

*Phase 2: the phone's day.*

5. Inbox: a status chip per pane (running · 3m, waiting for you, finished, failed, password),
   "needs you" rows first, the app badge count; guest-agent panes shown like any pane.
6. Pane view: a Stop button that is always visible while a turn runs; Recap (fix #WMXN by sending
   `manual`); question rows drawn from `question` events with tap-to-answer choices, closed by
   `question_closed`; `full` devices may admit knocks, decide guest prompts and grant control
   (lift those three from `OWNER_ONLY` to `full`, with tests in `tests/test_remote_wire.py`).
7. Queue and replay: a prompt typed while the socket is down is kept with its `msg_id`, shown
   as "sends when back online", sent once on resume; the same for a Stop. Reconnect on `pageshow`
   as well as `visibilitychange`.
8. Notifications as two switches, like Claude Code's: "when an agent finishes or needs me" and
   "when something needs an action" (mapped onto the five kinds; the presence rule and cooldown
   unchanged); tapping one opens that pane. The terminal prints "check in from your phone" when
   a turn passes a threshold and a device is paired.

*Phase 3: proof on the iPhone, from another network.*

9. Install the PWA from join.relay-terminal.ai to the Home Screen; pair; leave the desk. Over
   LTE for a working day: a lock-screen push from a finished turn and from a waiting agent, a
   prompt sent from the bus, a Stop, a model switch, a new conversation, a password entry, a
   voice clip with the real microphone, an admit of a knock, and the nine #KBFT keyboard checks.
   Reconnect counts and gaps from the audit log. Evidence under
   `docs/qa_evidence/2026-09-2x-iphone-all-day/` with screenshots from the phone.

*Phase 4: native shell, only if Phase 3 says so.*

10. Capacitor around `app/`; APNs through the rendezvous with the same sealed bodies; Keychain
    for the device key; Face ID to open; a Live Activity per running pane; TestFlight. Android
    the same way, later.

**Risks.**

- `src/Pane.h` is claimed by three live sessions (#EB4A, #93WR's, the perf orchestrator's
  subagents). Steps 2 and 6 need one hook block there; schedule those hunks last and small.
- Step 1 turns a per-share feature into a service. The desktop indicator, "Disconnect all" and
  the audit log are what keep "forgot it was on" honest; they land in the same step, not after.
- Step 4 touches a box running a dozen sites. The service already routes straight from the
  tunnel to port 8791, so nginx is not involved; the deploy script only writes under
  `/opt/relay-rendezvous` and restarts one unit.
- iOS may still throttle Web Push on an installed PWA (Focus, summaries). Phase 3 measures it;
  Phase 4 is the answer if it fails.
- The perf session (#PF4K, #GMCF) is on `ci.yml`, prompts and tests, not on `remote/` or `app/`;
  #5BAS (main does not build on Qt6) only matters for the sphinxpad laptop test.

**Verify.** Per step: `tests/test_remote_gui_host.py` (start-at-launch, remembered address),
`tests/test_remote_host.py` (auto-publish, withdraw on close), `tests/test_remote_security.py`
(connect tokens), `tests/test_pane_view.py` (Stop, question rows, offline queue),
`tests/test_remote_wire.py` (the three lifted kinds), `tests/test_remote_push.py` (two switches
over five kinds); `ctest -R "remotepane|sharing|panestate"`. Then Phase 3's real-device
evidence, which is the acceptance.

## Tasks

- [x] Phase 1.1 Remote control switch in Options › Remote; sidecar at launch; hosted registration remembered <!-- t:a1 -->
- [x] Phase 1.2 Own devices get every pane automatically; chrome indicator and Disconnect all <!-- t:a2 -->
- [x] Phase 1.3 Per-device connect tokens <!-- t:a3 -->
- [x] Phase 1.4 `rendezvous/deploy.sh`, deployed; served app equals main; PNG icons <!-- t:a4 -->
- [x] Phase 2.5 Inbox status chips, needs-you order, badge <!-- t:b5 -->
- [x] Phase 2.6 Stop, Recap (#WMXN), question rows, `full` admits and decides <!-- t:b6 -->
- [x] Phase 2.7 Offline queue and replay; reconnect on pageshow <!-- t:b7 -->
- [x] Phase 2.8 Two notification switches; tap opens the pane; "check in from your phone" <!-- t:b8 -->
- [ ] Phase 3.9 A working day on the iPhone over LTE, evidence folder <!-- t:c9 -->
- [ ] Phase 4.10 Capacitor shell, TestFlight (only if Phase 3 says so) <!-- t:d1 -->
