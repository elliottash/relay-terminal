---
id: H1BS
type: work
status: inbox
labels: [bug, tests]
rank: zzzzzzzzzzzzzzzzzz
created: '2026-09-25'
source: pane 7dbb2c54 (guest Claude Code), measured 2026-09-25
links: {plans: [], commits: [], evidence: [], related: [DVV2, 7PEC], github: null}
---
# A pane's scratch TMPDIR is too long for Chrome's singleton socket, so the browser tests cannot start in a Relay pane

## Issue
Found while working #7PEC (2026-09-25), measured: in a guest Claude Code pane, TMPDIR=/home/elliott/.cache/relay/scratch/7dbb2c54-9215-4e1f-a044-52c4f6c7dc1a/tmp. `google-chrome --headless=new --user-data-dir=<mktemp -d> about:blank` dies with `FATAL:chrome/browser/process_singleton_posix.cc:313] Socket path too long: /home/elliott/.cache/relay/scratch/7dbb2c54-…/tmp/com.google.Chrome.e7rpEL/SingletonSocket` (a Unix socket path is limited to 108 bytes), so every test that uses tests/browser.py (`python3 -m unittest tests.test_board_view`) fails with "Chrome exited while starting." — 8/8 errors. With TMPDIR=/tmp/claude-1000/7pec the same run is 36/36 OK. The per-session scratch TMPDIR from #DVV2 needs a shorter path (or tests/browser.py a short profile/TMPDIR of its own).
