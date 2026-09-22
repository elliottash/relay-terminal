# Remote delivery, 2026-09-22

Checked from main at 82acbc04993a plus this session's changes.

- #WEVT (formerly WCLS): e4bfa994 already classified worker events. 48 remote wire tests pass.
- #ADTR (formerly AUDL): d56c80aa's hosted-drive audit.txt contains all 13 records, including the supposedly missing invite, knock, admit, guest prompt and decision. A new real-socket Sidecar regression confirms these records land beside devices.json; no audit implementation change needed.
- #SDR1: conversation metadata is scrubbed before both live delivery and replay. Regression confirms local paths are absent, titles survive, and the desktop event remains unchanged.
- #PRM2: the actual C++ pairing dialog is driven through startup, repeated started/remote_state, stop/restart and a 429 error. Initial settled startup sends one pair and one pair_code; four repeated announcements send neither. Stop clears code and QR link, restart asks once for each, and 429 remains visible with New code. This uses a local process as the stdio sink, not the hosted service.

Commands and results:

```
scripts/relay-build --target relay
# PASS
RELAY_KEYRING=off python3 -m unittest tests.test_remote_gui_host tests.test_remote_audit tests.test_remote_wire
# 104 tests, PASS
ctest --test-dir build -R '^(sharing|remotesettings)$' --output-on-failure
# 2 targets, PASS
python3 docs/qa_evidence/2026-09-22-remote-delivery/run.py
# PASS (actual dialog under isolated Xvfb)
```

`run.py` compiles driver.cpp against build/relay's RemoteShare object and Qt5 libraries. It writes pairing.png (rate-limit state) and sharing.png (quiet pane view). Screenshots visually inspected. Sharing tests exercise connected-device names, quiet rows, guest blocks, knock placement, option toggles and the off sentence. No real guest admission or owner's live Sharing pane was manipulated.

The iPhone/iPad install, notifications, touch ergonomics, LTE reconnect and working-day checks remain human verification. No claim is made that desktop simulation proves those checks.

Board format check has unrelated existing warnings/errors; no findings name the four bug cards touched here.
