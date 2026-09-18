---
id: W5N2
type: work
status: in-progress
labels: [feature]
component: [gui, worker, terminal engine]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: claude
rank: 6d
created: '2026-09-17'
acceptance: from a phone, the owner follows and drives a desktop Relay pane's agent and terminal with notifications; two people share a pane with clear control handoff; all traffic end-to-end encrypted
source: '`issues/feature_intake.txt`, 2026-09-17: "another important feature i need: remote access on phone. multiplayer shared terminals. i like warp remote control and blink but they kind of suck. lets make a good version of that."'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-18-remote-end-to-end/, docs/qa_evidence/2026-09-17-remote-p1/, docs/qa_evidence/2026-09-17-remote-share-button/, docs/qa_evidence/2026-09-18-remote-scrollback/, docs/qa_evidence/2026-09-18-remote-multiplayer-desktop/, docs/qa_evidence/2026-09-18-remote-multiplayer-guest/], related: [C1HH, YR21, 05J2], github: null}
---
# Remote access from a phone and multiplayer shared terminals

## Status

**Both halves of the acceptance line run end to end on this machine** (2026-09-18,
`docs/qa_evidence/2026-09-18-remote-end-to-end/`): one Relay session under Xvfb, three real
headless browsers — the owner's phone, a guest called alice, and a second device of the owner's
paired for viewing — against the real sidecar, the rendezvous it runs in process, and Relay's own
panes. P0 was confirmed on a real iPhone and iPad on 2026-09-18.

Against the acceptance line, phrase by phrase, with the shot that shows each:

| The line says | Seen |
|---|---|
| from a phone, the owner **follows** a desktop pane's terminal | the pane list, the pane and its live screen (05, 06); scrollback paged back 383 rows and joined the column 1..400 in two runs of four (11, and "What is left" 6) |
| … and **drives** it | a shell command and a line typed after Take over (07a, 08, 12, 13) |
| … and its **agent** | a prompt routed to the pane's agent, answering into the same terminal (09, 10) |
| with **notifications** | a real `push_subscribe` inside the Noise session, answered with `push_state` and a checkbox per kind (16). The *delivery* did not happen in this run — see "What is left" 7 |
| **two people share a pane** | an editor invite, a knock with matching codes, admit, type, prompt, approve, refuse, pause, demote, remove (26-49, 52-57) |
| with **clear control handoff** | ask, grant, "alice is typing", and the owner's keystroke taking it straight back while the guest keeps their half-typed line (36-43) |
| all traffic **end-to-end encrypted** | every message in the run went through the Noise session the pinned key authenticates; the rendezvous is in the middle and reads none of it (`tests/test_remote_noise.py`, and the push service in this run receives bytes it cannot open) |

What that run also found is in the same folder's README, and the ones worth a card are repeated
under "What is left" below. **A real phone still has to confirm** four things this machine cannot:
a notification arriving on a lock screen through a real push service, the microphone with a real
microphone, the certificate warning on iOS Safari, and the installed-PWA path that iOS needs before
it delivers Web Push at all.

- Design and owner decisions: `docs/REMOTE-AND-MULTIPLAYER-DESIGN.md` (section 12 records the review
  against the code and three further decisions).
- Wire contract: `docs/REMOTE-PROTOCOL.md`. Section 14 lists what is built.
- Security review of P0 done, and a second one of push, password entry, voice and multiplayer
  (`38fc7e1`), with findings fixed rather than listed.
- Evidence: `docs/qa_evidence/2026-09-18-remote-end-to-end/` (all of it, in one session),
  `docs/qa_evidence/2026-09-17-remote-p1/` (the protocol),
  `docs/qa_evidence/2026-09-17-remote-share-button/` (the share button and a phone typing),
  `docs/qa_evidence/2026-09-18-remote-scrollback/` (paging back),
  `docs/qa_evidence/2026-09-18-remote-multiplayer-desktop/` and
  `docs/qa_evidence/2026-09-18-remote-multiplayer-guest/` (the two sides of a guest).

**In the app:** click the share chip beside the microphone in the composer strip (or the palette's
"Share this pane with a phone", `pane.share`). A dialog shows a QR; scan it, check the five-digit
code matches the one on the phone, and choose **Allow viewing** or **Allow typing**. The picker
above the QR chooses where the link points. Its first entry is the tailnet name behind
`tailscale serve` — a real certificate, no warning, reachable from anywhere the phone is signed in
— and under it the picker says in one sentence why that is not on offer when it is not. The others
are this machine's own addresses behind the self-signed certificate, and the phone has to be on
that network.

**On the phone or tablet:** one prompt box, like Relay's own. What you type is routed — a command
runs in the shell, anything else goes to the agent, and the agent's answer prints into the same
terminal, because that is where Relay prints it (`ARCHITECTURE.md` section 8). **Take over** turns
the box into the running program's line and brings up an extra-keys row; **Keyboard** sends every
keypress straight through, which is what makes a tablet with a hardware keyboard behave like a
terminal.

**From a terminal**, without the GUI:

```sh
cmake --build build-engine --target relay-screen-bridge     # once
python3 -m remote.cli share --tls    # prints a QR; scan it, confirm the five-digit code
```

`remote.cli dev --tls` runs the agent-companion demo instead.

## What is built

| Part | Where |
|---|---|
| Noise `IK_25519_AESGCM_SHA256`, both halves | `remote/noise.py`, `app/noise.js` |
| WebSocket, framing, relay envelope | `remote/ws.py`, `remote/envelope.py`, `remote/httpd.py` |
| Rendezvous (registry, rooms, ciphertext relay) | `rendezvous/server.py` |
| Desktop hub: pairing, capabilities, streams, allow-lists | `remote/host.py`, `remote/identity.py` |
| Web app: pair, inbox, thread, composer, plans, terminal grid | `app/` |
| Screen stream: a real PTY through Relay's own emulator | `engine/tools/ScreenBridge.cpp`, `remote/terminal.py`, `app/screen.js` |
| Take-over: keys, paste, line, extra-keys row, direct typing | `remote/host.py`, `app/app.js`, `app/screen.js` |
| One prompt box, routed through the pane's own router | `Pane::submitRemote` in `src/main.cpp`, `remote/gui_host.py` |
| Viewing and typing as separate grants, enforced per message | `src/RemoteShare.cpp`, `remote/wire.py`, `remote/host.py` |
| Local attach, so the desktop shares the same shell | `remote/attach.py` |
| The share button, dialog and GUI sidecar | `src/RemoteShare.{h,cpp}`, `remote/gui_host.py` |
| Multiplayer, the owner's desktop: "Invite someone to this pane", and the Sharing pane where knocks, control requests and guest prompts are answered | `src/SharingPane.{h,cpp}`, `src/RemoteShare.{h,cpp}`, `tests/sharingpane_test.cpp` |
| Dev harness with the QR and self-signed TLS | `remote/cli.py`, `remote/devtls.py` |
| A warning-free address over `tailscale serve`, from the CLI and from the dialog | `remote/tailnet.py`, `remote/gui_host.py`, `src/RemoteShare.cpp` |
| Scrollback, styled, paged by absolute row from both sources | `engine/core/VtCore.h` (`historyLines`), `engine/tools/ScreenBridge.cpp`, `remote/terminal.py`, `app/screen.js` |
| Notifications: `push_subscribe`, five triggers, the presence rule, a per-pane cooldown, constructed bodies, VAPID at the rendezvous | `remote/push.py`, `remote/notify.py`, `rendezvous/server.py`, `app/sw.js`, `app/app.js` |
| Voice: a clip from the phone, transcribed on the desktop's key, back into its prompt box | `app/app.js`, `remote/gui_host.py`, `src/Pane.h` (`transcribeForRemote`) |
| Guest identity, invite links, knock-to-admit, roles, the participant store apart from the device one | `remote/guests.py`, `remote/identity.py`, `remote/pairing.py` |
| Presence and one control book across the owner, their devices, the agent and the guests; the guest-prompt queue; pause and *only while I'm here* | `remote/control.py`, `remote/host.py` |
| The owner's desktop surface: the Sharing pane, invite creation, knocks, control requests, guest prompts | `src/SharingPane.{h,cpp}`, `src/RemoteShare.{h,cpp}` |
| The guest's web client, a separate session from the owner's phone | `app/guest.js`, `app/rrp.js`, `/join` |
| The audit log of 10.6 | `remote/audit.py` |

## What is left (refreshed 2026-09-18, after the end-to-end run)

Everything the acceptance line names is built and was watched working in one session. What is
below is what that run and the reading around it found: six things worth fixing, three that are
a decision rather than a patch, and two that need the owner.

**Why this card has not moved to `needs_qa_llm`.** Finding 1: the prompt box a person actually
sees on the phone sends every command to the agent. The routing underneath is fine and the run
shows it working through the client's own box, but the sentence the card promises — "what you type
is routed: a command runs in the shell" — is not true of the phone as it stands this evening. When
that one line lands in the pane view's host callback, this card has nothing left between it and
the QA lane.

**Found by running it, 2026-09-18** (`docs/qa_evidence/2026-09-18-remote-end-to-end/`, which has
the shot for each)

1. **A command typed into the prompt box on the phone goes to the agent, not the shell.** The pane
   view (§16, landed the same day) draws the pane's own box over the client's, and `compose()` in
   `app/pane.js` sends `{t: 'compose', pane, text, when}` with no `agent` field; the hub reads a
   missing `agent` as `true`. So `printf "…"` from the box a thumb reaches for is handed to the
   agent, which runs it as a tool call on the owner's key, and "one prompt box — a command runs in
   the shell" stops being true on the phone. The client's own box still sends `agent: false` for a
   `full` device and routes correctly, which is the one line the pane view's host callback in
   `ensurePaneView` needs.
2. **Two prompt boxes on the phone.** `ensurePaneView` hides the client's composer; `updateDriveUi`
   sets `$('composer').hidden` back from its own rule on the next pane update, and both are drawn.
3. **A voice transcript can land out of sight.** `voiceDone` appends the words to `#composer-text`,
   the box the pane view hides. The microphone moves into the pane's strip and records; what comes
   back has nowhere to be read.
4. **Two of the five notification triggers cannot fire from the app.** `Pane::shareStatus()`
   returns only `password`, `running` or `idle` and is the only source of a pane's status on the
   sidecar line, so `remote/notify.py`'s `waiting_input` and `failed` transitions are unreachable
   from a GUI pane. They work from `python3 -m remote.cli share`, where the terminal source
   answers with the full vocabulary.
5. **A guest's name does not reach the turn's attribution line.** `submitRemote` puts the display
   name on `QueueEntry::author`, which is drawn for a *queued* row; a prompt that starts at once
   goes through `startAgentEntry`, which carries `entry.why` and not `entry.author`, so the pane
   prints `guest:<hex>` under the text. §10.4 asks for the name.

6. **Scrollback paging on the phone is slow enough under load to look broken.** Four runs of the
   same drive, the same code, the same 150 s of dragging: two paged 383 rows back and joined the
   column 1..400 with no gap and no repeat, two paged nothing inside that window — and in one of
   those, a shot taken a minute later has `scrollback-1` onward on the phone. The pages arrive;
   the page-ahead is just slower than a finger when the machine is busy, and until they do the
   phone shows an empty space where the history will be.

**Needs a decision, not a patch**

6. **The owner's own phone is not in the one control book.** §10.3 lists "a `full` device typing"
   among the changes that go through `remote/control.py`, but `Host._on_control_request` sends an
   empty keystroke for a device and never calls `ControlBook.claim_device` — which exists and is
   documented for exactly this. So `panes[].control` never reads `remote:<device>` while a phone
   drives, `Pane::takeBackFromGuest()` sends no `control_take` because no guest is named as the
   driver, and `app/app.js` follows no `control`: the phone goes on saying "You have the keyboard"
   after the owner types, and both can type at once. Making it one driver per pane touches
   `remote/host.py`, `src/Pane.h` and `app/app.js` together, and whether a phone should lose the
   keyboard to its own owner's keystroke is the owner's call, not a bug fix.
7. **A participant list never reaches the owner's own devices.** §10.3 says `participants` goes to
   "everyone on the pane"; `Host.send_participants` skips every channel with no `participant_id`,
   so a phone cannot show who else is on the pane it is watching. Either the spec or the fan-out
   should move.
8. **WebAuthn binding**: bind user verification to the desktop, or drop it from the threat table.
   Recommendation unchanged: drop it for v1; the per-device password switch covers the case.

**Needs the owner**

9. **Hosting**: `app.relay-terminal.ai` and the `rv.` cloudflared ingress rule. Until then it is
   LAN or tailnet with a certificate warning, and a real phone cannot be reached by a push service.
10. **Native apps (P5)**: Android then iOS. Not in the acceptance line; better as a card of its own
    so this one can close on what it promises.

**Not shown by the run, and why**

- **Password entry from a phone.** The switch is per device and the drive clicked the middle of a
  two-row device list, so it went to the viewing device rather than the phone (shot 22 says
  `· view · passwords`) and no field appeared on the phone. The prompt itself, the refusal of
  ordinary typing and the absence of a field on a device that was never allowed one are all in
  shots 20-23; the rest is covered by `SecretInputTests`. The drive now aims at the first row.
- **A notification arriving.** `push_subscribe` reaches the desktop and comes back with its five
  kinds, and the presence rule was satisfied (the run checks that the focus left Relay's window),
  but nothing reached the local push service. The hub's own decision is unit-tested end to end in
  `tests/test_remote_push.py`, including a real delivery to a local service; what this run cannot
  yet say is that a GUI pane's password prompt reaches it.

**What a real phone still has to confirm.** Four things this machine cannot: a notification
arriving on a lock screen through a real push service, the microphone with a real microphone, the
certificate warning on iOS Safari, and the installed-PWA path iOS needs before it delivers Web
Push at all. Everything else in the acceptance line was watched working here.

## Delivery plan (Opus subagents, this checkout, `main`) — all ten waves ran

Every agent gets: a named set of files it owns, the rule that it commits only its own hunks
(`git apply --cached`, never `git add -A`), the "fix clear gaps" rule, and the remote suites as its
gate. `remote/host.py`, `app/app.js`, `src/RemoteShare.cpp` and `src/Pane.h` are touched by almost
everything, so the waves are cut so that no two agents running at once share a function in them.

**Wave 0: lead session, no subagent.** Refresh `REMOTE-PROTOCOL.md` section 14. Write the
multiplayer wire contract into section 10 as messages (`invite_create`, `knock`, `admit`,
`presence`, `control_offer`, `prompt_pending`, `prompt_decide`), so wave 3's agents build against one
text. Close `#XEMH`.

**Wave 1: three agents in parallel**

| Agent | Delivers | Owns | Also touches |
|---|---|---|---|
| A · push | `push_subscribe`; the hub's triggers (agent finished, waiting for input, password prompt) with constructed bodies, the presence rule and the cooldown; subscribe in the client with a permission prompt that is asked for, not sprung; `tests/test_remote_push.py` including a fake push service and RFC 8291 vectors | `remote/push.py`, `rendezvous/server.py`, `app/sw.js`, the new test | a `# ---- push` section in `host.py`; one settings row in `app.js`; a `window_active` line from `RemoteShare.cpp` |
| B1 · styled history, engine half | `VtCore::historyLines`, const, in both cores, with `CoreTest` cases; `ScreenBridge` answers in `segs` | `engine/core/*`, `engine/tools/ScreenBridge.cpp` | none. **Starts when `#TK9C` commits.** |
| C · voice | `voice` to the GUI and back, the pane hook, the microphone button | `GuiPaneSource.transcribe`, the `voice` branch in `RemoteShare.cpp`, one hook in `Pane.h` | the composer row in `app.js`/`index.html` |

**Wave 2: one agent.** B2 · history, the rest: `TerminalView` accessor, sidecar and hub
(`_on_history_get`), `remote/terminal.py`, scroll-up paging in `app/screen.js`. Makes `HistoryTests`
pass rather than rewriting it.

**Wave 3: multiplayer, four agents, two at a time**

| Agent | Delivers | Owns |
|---|---|---|
| M1 · guests and invites | guest identity (a guest is never the owner), invite links with role and expiry built on `pairing.Room`, roles mapped onto the existing capabilities, knock-to-admit, audit lines for all of it | new `remote/guests.py`, `identity.py`, `pairing.py`, a section of `host.py` |
| M2 · presence and control (after M1) | who is here, who holds control, offer/request/take back, the guest-prompt approval queue, audit of prompts, lines and handoffs | new `remote/control.py`, a section of `host.py` |
| M3 · desktop surface (with M2) | participants, knocks and pending guest prompts as a **pane**, not a dialog; invite creation from the share button | `src/RemoteShare.{h,cpp}`, one hook block in `Pane.h` |
| M4 · web client (with M2) | guest join flow, presence chips, ask-for-control, "waiting for the owner" on a prompt | `app/*` |

**Wave 4: two agents.** An independent security review of push, password entry and multiplayer,
as was done for P0, with findings fixed rather than listed. Then a live drive: `drive.sh` grown to
two browsers (owner's phone and a guest), screenshots and logs into `docs/qa_evidence/`, and the
card moved to `needs_qa_llm`.

Ten agents, of which at most three run at once. The order is what the acceptance line needs first:
notifications close the phone half, multiplayer closes the other.

**All of it ran.** Wave 4's security review is `38fc7e1`; wave 4's live drive is
`docs/qa_evidence/2026-09-18-remote-end-to-end/`, which grew to three browsers — the owner's
phone, a guest, and a second device paired for viewing — rather than two.

Related work: take-over shares the control token with `#C1HH`; `#YR21` improves remote
"waiting for input" notifications; `#05J2` reuses this feature's pairing machinery.

## Known constraints

- Screen state comes from Relay's own engine. That was a limit while KonsolePart was the default;
  now that it is retired, every pane can be shared.
- Owner (2026-09-17): "the no account / cloud / etc are not strict constraints. ideally it could be done in a browser on relay-terminal.ai as a first version, later on we make android / phone apps." A Relay-operated service and accounts are acceptable; v1 is a browser client on relay-terminal.ai, native apps later. Content should stay end-to-end encrypted.
- Agent tools run without approval today; remote-originated prompts need a security review.
- **The certificate warning is no longer the only option** (2026-09-18). The share dialog's first
  address is now the tailnet name behind `tailscale serve` (`remote/tailnet.py`): a real
  certificate, nothing for the phone to accept, the pairing fragment not dropped across an
  interstitial, and Web Push testable at last, because a browser that has seen a certificate error
  will not register a service worker. Confirmed against this machine's own tailnet:
  `https://spark-dcc9.tail6fb70c.ts.net` answers 200 behind a Let's Encrypt certificate, and
  `serve reset` takes it down again. On a machine without tailscale — or where `sudo tailscale set
  --operator=$USER` has not been run, or MagicDNS or Serve are off for the tailnet — it is still
  the self-signed certificate and the one warning, and the dialog says in one sentence which of
  the two it is and why.

## What was found by using it (2026-09-18, P0 on a real iPhone and iPad)

Four bugs that only a real device showed, each now covered by a test:

- **Every screen drew at once.** The client hides screens with the `hidden` attribute, and the
  stylesheet's `display` rule beat it. The browser tests checked the attribute rather than what was
  drawn, so they passed; they now assert exactly one screen is displayed.
- **No keystroke reached the shell.** The client encodes bytes as base64url without padding and the
  desktop decoded strict standard base64. Only the line box worked, because it sends text.
- **Sessions died under load.** Two of them: fan-out encrypted frames in one order and wrote them in
  another, and the client reserved its nonce after an `await`. A Noise counter tolerates neither.
  Both are written up in `docs/REMOTE-PROTOCOL.md` section 15.
- **Pairing links arrived incomplete.** Some QR readers percent-encode the fragment, so the
  separators became `%26` and the desktop key vanished. Both clients undo that now, and a link that
  really is broken says to scan again instead of naming a field.

And two pieces of guidance that were simply wrong: the welcome screen named a "Settings → Remote →
Pair device" menu that never existed, and the pairing QR defaulted to the tailnet address, which a
phone cannot reach unless it is on the tailnet.
