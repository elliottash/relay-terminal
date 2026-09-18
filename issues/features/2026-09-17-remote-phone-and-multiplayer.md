---
id: W5N2
type: work
status: in-progress
labels: [feature]
component: [gui, worker, terminal engine]
milestone: desktop-alpha
workstream: terminal
rank: 6d
created: '2026-09-17'
acceptance: from a phone, the owner follows and drives a desktop Relay pane's agent and terminal with notifications; two people share a pane with clear control handoff; all traffic end-to-end encrypted
source: '`issues/feature_intake.txt`, 2026-09-17: "another important feature i need: remote access on phone. multiplayer shared terminals. i like warp remote control and blink but they kind of suck. lets make a good version of that."'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-17-remote-p1/', 'docs/qa_evidence/2026-09-17-remote-share-button/'], related: [C1HH, YR21, 05J2], github: null}
---
# Remote access from a phone and multiplayer shared terminals

## Status

**P0 done. P1's agent companion, the P2 screen stream and P3 take-over all run against Relay's own
panes, from the share button in the app. Confirmed on a real iPhone and iPad (2026-09-18).**

Against the acceptance line: a phone drives a pane's terminal **and** its agent, over an
end-to-end encrypted session. **Notifications and multiplayer are not built**, so the card stays
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
| Dev harness with the QR and self-signed TLS | `remote/cli.py`, `remote/devtls.py` |

## What is left

1. **Notifications.** Nothing is delivered. `/v1/push/send` accepts a payload and drops it;
   VAPID signing is not written. "Agent finished", "waiting for input" and "password prompt" are
   the point of a companion app and none of them reach a phone yet. This is the largest gap
   against the acceptance line.
2. **Multiplayer (P4).** Not started: no invites, roles, knock-to-admit, presence, guest prompt
   approval or audit log. The second half of the acceptance line.
3. **Web Push delivery.** `/v1/push/send` accepts and does not deliver; VAPID signing is not written.
   The subscription keys deliberately never reach the rendezvous.
4. **Hosting actions (owner).** `app.relay-terminal.ai` and the `rv.` cloudflared ingress rule.
   Until then Relay serves the app itself, over the LAN or the tailnet, with a self-signed
   certificate the phone warns about once.
5. **Security findings not yet fixed**, all in parts that are refused rather than half-built:
   `secret_input` needs a desktop-minted prompt nonce, so answering a password prompt from the
   phone returns `not_permitted`; WebAuthn user verification must be bound to the desktop or
   dropped from the threat table; transport switching needs the explicit `transport_switch`
   handshake before WebRTC arrives. Ordinary input *is* refused while a pane is at a password
   prompt, checked from a fresh termios read at write time.
6. **Scrollback paging** (`history_get`) needs the const `VtCore::historyLines` in both cores. A
   phone sees the live screen only.
7. **Voice from a phone** is refused: `remote/gui_host.py` has no route to the pane's worker for it,
   though the desktop's own transcription path would take the clip as it stands.
8. **Native apps (P5)** — Android then iOS, per the owner's decision.

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
