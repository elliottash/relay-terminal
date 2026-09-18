# Remote access P1 slice — evidence, 2026-09-17

Feature `#W5N2`, protocol `docs/REMOTE-PROTOCOL.md`.

| File | What it shows |
|---|---|
| `tests.txt` | The 55 remote tests: Noise (including the browser implementation driven under Node against the desktop one), the allow-lists and sequencing, the end-to-end pairing and capability tests, and the real web client in headless Chrome |
| `pairing-qr.txt` | What `python3 -m remote.cli dev --tls` prints: the scannable pairing QR and the https address a phone opens |

Reproduce:

```sh
python3 -m unittest tests.test_remote_noise tests.test_remote_wire \
                    tests.test_remote_host tests.test_remote_browser
python3 -m remote.cli dev --tls          # then scan the QR with a phone
```

Not covered here: Web Push delivery (accepted, not delivered), the terminal stream (P2),
take-over and password entry (P3), and the GUI pane source — the harness runs
`remote.panes.DemoPaneSource`.
