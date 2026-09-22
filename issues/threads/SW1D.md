<!-- relay:entry 20260922T020000Z-a1 author=claude-code kind=comment -->
Filed from the "rethink the buttons on the board" conversation on #YZ8G. Sequenced after #1CXD's
Area B lands (same file, `src/BoardPane.cpp`'s tool-row construction).

<!-- relay:entry 20260922T030000Z-s1 author=codex kind=progress -->
### Codex · 2026-09-22 03:00
Claimed with owner-authorized takeover. Dependency #1CXD landed. Implementing the Hygiene stages and Performance presentation; preserving #74Y5 driver hunks. Board MCP unavailable; using file fallback.

<!-- relay:entry 20260922T030001Z-s2 author=codex kind=evidence -->
### Codex · 2026-09-22 03:00
Implementation and targeted tests pass; isolated Xvfb application evidence is in
`docs/qa_evidence/2026-09-21-sw1d/`. Moved to needs-verification with Execution Summary and Tests.
The broader shared-tree boardpane run encountered the parent #74Y5 uncommitted named-driver
assertion (`board_move_card`); that test and all parent hunks are excluded from this delivery.

<!-- relay:entry 20260922T030001Z-s3 author=agent kind=evidence -->
Check · 1 not-applicable, 1 passed. The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T030002Z-s3 author=codex kind=evidence -->
### Codex · 2026-09-22 03:00
Landed e325845d59af145ea6b692d38e7c526ad48b9f2e after exact-tree build gate. Both full
boardpane and profilepane suites pass in the isolated landing tree (2/2, 0.25 seconds).
Recorded profilepane run 20260922T013125Z-cf42 passes and tests_check returns no findings.
Parent driver changes remain uncommitted by this delivery. Card stays needs-verification.

<!-- relay:entry 20260922T030002Z-s4 author=agent kind=evidence -->
Check · 1 not-applicable, 3 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T030003Z-s1 author=codex kind=evidence -->
### Codex · 2026-09-22 03:00
Follow-up fixes existing board-model tests for Hygiene-first cleanup and the visible Performance
pane chrome title. All 87 board cases and all four targeted CTest suites pass; recorded run
20260922T013718Z-2031 opens no signals. GUI screenshot 04-performance-pane.png confirms the
actual pane title. Tests check retains only an advisory historical board-suite speed notice.
Preserved parent verify-brief assertions; card remains needs-verification.

<!-- relay:entry 20260922T030003Z-s2 author=codex kind=evidence -->
### Codex · 2026-09-22 03:00
Follow-up landed 7133156a0d1955c4ecf35c68ac705b2cd4a218d0. Exact-tree application build passed;
isolated-tree board and panestatus suites also passed 2/2 (2.37 seconds). All parent-owned
changes were preserved. Shared combined four-suite run and GUI evidence are recorded above.

<!-- relay:entry 20260922T030003Z-s3 author=agent kind=event model=gpt-6-astra pane=fe4e4144 turn=a8aa6eac04d048c7ba96c1232b953cda/e17af86989a0419d9ff038915f2577f0 -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260922T030003Z-s4 author=agent kind=event model=gpt-6-astra pane=fe4e4144 turn=a8aa6eac04d048c7ba96c1232b953cda/e17af86989a0419d9ff038915f2577f0 -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["e325845d59af145ea6b692d38e7c526ad48b9f2e", "7133156a0… → {"plans": [], "commits": ["e325845d59af145ea6b692d38e7c526ad48b9f2e", "7133156a0…
