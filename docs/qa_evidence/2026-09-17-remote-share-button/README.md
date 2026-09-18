# The share button, live — evidence, 2026-09-17

Feature `#W5N2`. Relay sharing one of its own panes with a phone, driven under Xvfb.

| File | What it shows |
|---|---|
| `implementer-01-pane-before-sharing.png` | An engine pane with output on screen |
| `implementer-02-strip-with-share-button.png` | The share chip in the composer strip, right after the microphone |
| `implementer-03-after-clicking-share.png` | The window after the click |
| `implementer-04-qr-dialog.png` | The dialog: a scannable QR, the address, and the self-signed certificate's fingerprint |
| `implementer-05-confirm-code.png` | The approval: what the device claims to be, its key, and the five-digit code to compare. **Refuse holds the focus** — allowing takes a deliberate click |
| `implementer-06-paired.png` | After allowing |
| `implementer-07-pane-after-phone-typed.png` | The desktop pane showing the line the browser typed, and the share chip lit |
| `pair.log` | What the browser saw, including its own copy of the five-digit code |
| `implementer-08-ipad-inbox.png`, `implementer-09-ipad-terminal.png` | The same client at iPad size, typing directly into the shell with a hardware keyboard |

The run pairs a headless Chrome standing in for a phone, so the codes in `implementer-05` and
`pair.log` are derived independently at each end from the Noise handshake and must match.

Reproduce:

```sh
cmake --build build
docs/qa_evidence/2026-09-17-remote-share-button/drive.sh
```

`RELAY_REMOTE_PAIR_FILE` is a QA hook: the pairing URL carries a one-time secret, so Relay writes
it out only when that variable is set, and never to a log.
