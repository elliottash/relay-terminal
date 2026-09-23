# The conversation id on the phone (Copy id in the Conversations sheet)

- `python3 -m unittest tests.test_remote_pane_state` — 41 tests OK (classification, round trip,
  refusal, wrong-session stop, answer cleaning).
- `python3 -m unittest tests.test_pane_view` — 26 tests OK (row layout ≥200px, ask by token,
  clipboard copy, refusal toast).
- `python3 -m unittest tests.test_remote_security tests.test_web_viewport` — 76 tests OK.
- `./scripts/relay-build` — clean, 2026-09-22.00H.04.
- `phone-390x844-sessions-sheet.png` — the sheet at phone width: open rows 276px wide (were 8px),
  Copy id chip 76px on every row.
