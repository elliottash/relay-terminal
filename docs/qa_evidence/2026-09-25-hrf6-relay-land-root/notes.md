# #HRF6 evidence — 2026-09-25

## The new root is live and the old one drains

$ python3 scripts/land.py who   # default root, no env overrides
land sessions under /home/elliott/.local/state/relay/land:
69bv_follow — idle 10m, contact: Codex pane #69BV reload chrome
    .board/changes/2026-09-25-restored-panes-lose-conversation-scrollback-on-r.md  snapshot 10m old
    .board/threads/69BV.md  snapshot 10m old
    docs/qa_evidence/2026-09-25-69BV-reload/measurements.md  snapshot 10m old
    src/Pane.h  snapshot 10m old
    src/PaneSession.cpp  snapshot 10m old
    src/WindowState.cpp  snapshot 10m old
    ... (20 sessions listed under ~/.local/state/relay/land)

$ ls /tmp/claude-1000/land; du -sh /tmp/claude-1000/land
verify-slots
1.2G	/tmp/claude-1000/land

Sessions began under the claude root this morning; every one was adopted into the
relay root by the first land.py command after the change (26 sessions at 2026-09-25 ~11:40).

## Test suite
$ python3 -m unittest tests.test_land
Ran 91 tests in 18.171s

OK
