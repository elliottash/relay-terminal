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
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-17-remote-p1/, docs/qa_evidence/2026-09-17-remote-share-button/, docs/qa_evidence/2026-09-18-remote-multiplayer-desktop/], related: [C1HH, YR21, 05J2], github: null}
---
# Remote access from a phone and multiplayer shared terminals

## Status

**P0 done. P1's agent companion, the P2 screen stream and P3 take-over all run against Relay's own
panes, from the share button in the app. Confirmed on a real iPhone and iPad (2026-09-18).**

Against the acceptance line: a phone drives a pane's terminal **and** its agent, over an
end-to-end encrypted session. **Notifications are half-wired and multiplayer is not started**, so the card stays
open — see "What is left".

- Design and owner decisions: `docs/REMOTE-AND-MULTIPLAYER-DESIGN.md` (section 12 records the review
  against the code and three further decisions).
- Wire contract: `docs/REMOTE-PROTOCOL.md`. Section 14 lists what is built.
- Security review of P0 done; its findings are folded into the spec as normative rules and into the
  code. The unfixed ones are listed under "What is left" below.
- Evidence: `docs/qa_evidence/2026-09-17-remote-p1/` (the protocol) and
  `docs/qa_evidence/2026-09-17-remote-share-button/` (the app: click share, scan, compare codes,
  allow, then run a command and put a prompt to the agent from a browser; plus the client at iPad
  size typing with a hardware keyboard).

**In the app:** click the share chip beside the microphone in the composer strip (or the palette's
"Share this pane with a phone", `pane.share`). A dialog shows a QR; scan it, check the five-digit
code matches the one on the phone, and choose **Allow viewing** or **Allow typing**. The picker
above the QR chooses which of this machine's addresses the link points at — the phone has to be on
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

## What is left (refreshed 2026-09-18, 15:00, against `dd35ead`)

Since the last refresh, `cc79c01` and `dd35ead` landed a second batch of remote work: password
entry, the push crypto, the rendezvous's push delivery, an audit log and the `transport_switch`
handshake. Some of it is complete and some is half-wired; this list is what reading the code and
running the suites shows, not what the commit messages say. The remote suites run 95 tests with
4 failing; three of the four are tests written ahead of code that does not exist yet.

**Done since the last refresh**

- **Password entry from the phone.** `secret_input` is no longer refused: a desktop-minted,
  single-use, 45-second nonce bound to (pane, foreground pid, prompt generation), a per-device
  switch that is off by default, a fresh termios check in the hub and again in
  `Pane::submitRemoteSecret` at the write, an audit line that records the fact and not the bytes,
  and a password field in the web client. Tested in `SecretInputTests`. **Not yet tried on a real
  phone.**
- **Audit log** (`remote/audit.py`): local, 0600, split by month and size. Records pairing,
  revoke, prompt detection and password use. It does not yet record prompts, lines or control
  handoffs, which section 10 asks for.
- **`transport_switch`**: the handshake exists and is tested. No second transport exists to switch
  to, which is correct for now.

**Half-built: finish these first**

1. **Notifications — done (2026-09-18).** The two ends are connected: `push_subscribe` /
   `push_unsubscribe` inside the Noise session (`remote/wire.py`, `remote/host.py`), the hub's
   decisions in `remote/notify.py` (five triggers, the presence rule, a per-pane cooldown,
   constructed bodies), a `window_active` line from `src/RemoteShare.cpp` through the sidecar, and
   a "Notify me on this phone" row in the app that asks permission from a tap and says what to do
   on an iOS device that is not installed. `remote/push.py` now has tests, and they found two bugs
   a real phone would have hit: the RFC 8291 key order was reversed and the aes128gcm padding
   delimiter was missing. `tests/test_remote_push.py` is 52 tests; section 9 of the protocol has
   the message shapes. **Not yet tried on a real phone** — that needs item 7, because a push
   service has to be able to reach the rendezvous. There is no per-trigger switch in the sharing
   dialog yet; that is a surface decision (section 9, last paragraph).
2. **Scrollback paging.** `ScreenBridge` answers a `history` request, but from
   `VtCore::historyText`, which is plain text, and the owner's decision was styled history. The hub
   refuses `history_get`; `HistoryTests` fails against that refusal. Needs a const, styled
   `historyLines(from, count)` in both cores, then the bridge, `TerminalView`, the sidecar, the
   hub and a scroll-up gesture in `app/screen.js`.
3. **Voice from a phone.** The hub handler and two `VoiceTests` exist; `GuiPaneSource.transcribe`
   still raises. Needs a `voice` message to the GUI, a hook into the pane's existing transcription
   path, the reply, and a microphone button in the web client.
4. **Docs are behind the code.** `REMOTE-PROTOCOL.md` section 14 still says password entry is
   refused and push is not delivered, and is dated 2026-09-17.

**Not started**

5. **Multiplayer (P4)**: the second half of the acceptance line. No guest identity, invite links,
   roles, knock-to-admit, presence, control handoff between people, guest-prompt approval, or
   desktop surface for any of it. Today a device pairs to the owner's desktop *as the owner*.
6. **WebAuthn binding**: bind user verification to the desktop, or drop it from the threat table.
   Recommendation: drop it for v1; the per-device password switch already covers the case it was
   there for. Owner's call.

**Needs the owner**

7. **Hosting**: `app.relay-terminal.ai` and the `rv.` cloudflared ingress rule. Until then it is
   LAN or tailnet with a certificate warning. It also gates the real two-person test, because a
   guest is not on the owner's network; the code can be tested on the LAN without it.
8. **Native apps (P5)**: Android then iOS. Not in the acceptance line. Recommendation: move it to a
   card of its own so this one can close on what it promises.

**Other sessions' loose ends that touch this**

- `#XEMH` (remote browser-peer tests fail, `Rrp` not a named export) no longer reproduces:
  `test_remote_wire` and `test_remote_noise` pass, 42 of 42. It can be closed.
- `test_a_model_switch_mid_turn_shows_where_it_lands` fails in `test_remote_browser`. That test and
  the `app/app.js` model indicator it covers are `#3ES1`'s uncommitted work, not this card's.
- `engine/core/VtCore.h`, both cores and `CoreTest.cpp` carry `#TK9C`'s uncommitted edits. The
  scrollback work needs the same files, so it waits for that commit.

## Delivery plan (Opus subagents, this checkout, `main`)

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

Related work: take-over shares the control token with `#C1HH`; `#YR21` improves remote
"waiting for input" notifications; `#05J2` reuses this feature's pairing machinery.

## Known constraints

- Screen state comes from Relay's own engine. That was a limit while KonsolePart was the default;
  now that it is retired, every pane can be shared.
- Owner (2026-09-17): "the no account / cloud / etc are not strict constraints. ideally it could be done in a browser on relay-terminal.ai as a first version, later on we make android / phone apps." A Relay-operated service and accounts are acceptable; v1 is a browser client on relay-terminal.ai, native apps later. Content should stay end-to-end encrypted.
- Agent tools run without approval today; remote-originated prompts need a security review.

## What was found by using it (2026-09-18)

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
