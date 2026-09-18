---
id: W5N2
type: work
status: in-progress
component: [gui, worker, terminal engine]
milestone: desktop-alpha
workstream: terminal
rank: 6d
created: '2026-09-17'
acceptance: from a phone, the owner follows and drives a desktop Relay pane's agent and terminal with notifications; two people share a pane with clear control handoff; all traffic end-to-end encrypted
source: '`issues/feature_intake.txt`, 2026-09-17: "another important feature i need: remote access on phone. multiplayer shared terminals. i like warp remote control and blink but they kind of suck. lets make a good version of that."'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-17-remote-p1/'], related: [C1HH, YR21, 05J2], github: null}
---
# Remote access from a phone and multiplayer shared terminals

## Status

**P0 done; a working slice of P1 runs against a demo pane source (2026-09-17).**

- Design and owner decisions: `docs/REMOTE-AND-MULTIPLAYER-DESIGN.md` (section 12 records the review
  against the code and three further decisions).
- Wire contract: `docs/REMOTE-PROTOCOL.md`. Section 14 lists what is built.
- Security review of P0 done; its findings are folded into the spec as normative rules and into the
  code. The unfixed ones are listed under "What is left" below.
- Evidence: `docs/qa_evidence/2026-09-17-remote-p1/` — 55 tests, including the web client in
  headless Chrome and the browser's Noise implementation checked against the desktop's.

You can pair a phone today:

```sh
python3 -m remote.cli dev --tls     # prints a QR; scan it, confirm the five-digit code
```

## What is built

| Part | Where |
|---|---|
| Noise `IK_25519_AESGCM_SHA256`, both halves | `remote/noise.py`, `app/noise.js` |
| WebSocket, framing, relay envelope | `remote/ws.py`, `remote/envelope.py`, `remote/httpd.py` |
| Rendezvous (registry, rooms, ciphertext relay) | `rendezvous/server.py` |
| Desktop hub: pairing, capabilities, streams, allow-lists | `remote/host.py`, `remote/identity.py` |
| Web app: pair, inbox, thread, composer, plans | `app/` |
| Dev harness with the QR and self-signed TLS | `remote/cli.py`, `remote/devtls.py` |

## What is left

1. **The GUI pane source.** `remote.panes.PaneSource` is the seam; today `DemoPaneSource` stands in.
   Wiring it to real panes, worker events and the composer is the rest of P1.
2. **Web Push delivery.** `/v1/push/send` accepts and does not deliver; VAPID signing is not written.
   The subscription keys deliberately never reach the rendezvous.
3. **Hosting actions (owner).** `app.relay-terminal.ai` and the `rv.` cloudflared ingress rule.
   Until then the harness serves the app itself over the tailnet.
4. **Security findings not yet fixed**, all in parts that are refused rather than half-built:
   the `secret_input` path (P3) needs a desktop-minted prompt nonce and a fresh termios read at
   write time; WebAuthn user verification must be bound to the desktop or dropped; transport
   switching (P2) needs the explicit `transport_switch` handshake. `keys`/`paste`/`line` and
   `secret_input` currently return `not_permitted`.

Related work: take-over shares the control token with `#C1HH`; `#YR21` improves remote
"waiting for input" notifications; `#05J2` reuses this feature's pairing machinery.

## Known constraints

- Depends on Relay's own engine (`docs/ENGINE.md`): KonsolePart cannot serialize screen state.
- Owner (2026-09-17): "the no account / cloud / etc are not strict constraints. ideally it could be done in a browser on relay-terminal.ai as a first version, later on we make android / phone apps." A Relay-operated service and accounts are acceptable; v1 is a browser client on relay-terminal.ai, native apps later. Content should stay end-to-end encrypted.
- Agent tools run without approval today; remote-originated prompts need a security review.
