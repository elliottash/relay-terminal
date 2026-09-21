<!-- relay:entry 20260921T231100Z-a2 author=codex-tui-a2 kind=progress -->
### Codex TUI agent · 2026-09-21 23:11
Claimed the TUI portion of "fix the TUI and the harness issues in separate relay subagents". #QG4C's 22:06 evidence identifies the missing delivery latch and active launch-command blockage. Read its complete thread. relay_board tools are absent from this subagent's catalog; using POLICY file fallback. Scope: Pane.h and C++ queue helper/tests; no backend changes or paid guest turns.

<!-- relay:entry 20260921T231102Z-a2 author=codex-tui-a2 kind=evidence -->
### Codex TUI agent · 2026-09-21 23:11
Implemented the reservation and guest-specific queue gate, with targeted regression coverage.
`ctest:queuesubmit` passed in recorded run `20260921T231057Z-24ff`; `tests_check` has no findings
or blocking signals. Board format check names only five pre-existing errors outside this card.
Moved to needs-verification with owner restart/live TUI instructions in
`docs/qa_evidence/2026-09-21-tui-guest-queue/README.md`. No paid guest turns run.
