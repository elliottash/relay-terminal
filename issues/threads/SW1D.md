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
