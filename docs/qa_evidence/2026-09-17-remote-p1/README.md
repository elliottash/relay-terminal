# Remote access: agent companion and shared terminal — evidence, 2026-09-17

Feature `#W5N2`, protocol `docs/REMOTE-PROTOCOL.md`.

| File | What it shows |
|---|---|
| `tests.txt` | The 63 remote tests: Noise (the browser implementation driven under Node against the desktop one), the allow-lists and sequencing, end-to-end pairing and capabilities, the real web client in headless Chrome, and a real `bash` shared and typed into from that browser |
| `share-qr.txt` | What `python3 -m remote.cli share --tls` prints: the scannable QR and the https address a phone opens |
| `pairing-qr.txt` | The same for `dev`, which runs the demo agent instead of a shell |

Reproduce:

```sh
cmake -S . -B build-engine -DRELAY_BUILD_APP=OFF -DRELAY_BUILD_ENGINE=ON
cmake --build build-engine --target relay-screen-bridge
python3 -m unittest tests.test_remote_noise tests.test_remote_wire tests.test_remote_host \
                    tests.test_remote_browser tests.test_remote_terminal
python3 -m remote.cli share --tls        # then scan the QR with a phone
```

What the browser test actually drives: pair by QR, confirm a five-digit code derived from the
handshake on both ends, store a non-extractable device key in IndexedDB, open the shared pane,
watch `bash` draw its prompt as a cell grid, press **Take over**, type `echo browser-typed-9876`,
and see it on both the phone's grid and the host's own screen state.

Not covered here: Web Push delivery (accepted, not delivered), scrollback paging (needs
`VtCore::historyLines`), answering a password prompt from the phone (refused by design until the
prompt-bound nonce exists), and Relay's GUI panes — the shared shells come from
`relay-screen-bridge`.
