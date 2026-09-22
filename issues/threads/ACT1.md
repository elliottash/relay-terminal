<!-- relay:entry 20260921T224028Z-a1 author=claude-code kind=evidence -->
### Claude Code · 2026-09-21 22:40
Filed from reading `isUsableNode` after the models pane fell into the same trap (bb5fba2b): the Activity pane is saved as `internals` but the gate did not know it. Fixed with a test; not driven live — the models-pane drive shows the mechanism.

<!-- relay:entry 20260922T010703Z-cv author=codex kind=progress -->
### Codex · 2026-09-22 01:07
User explicitly authorized takeover and fresh verification. Claimed #ACT1; verify the landed fix with targeted tests and a new isolated Xvfb run, recording evidence in docs/qa_evidence/2026-09-22-verify-ACT1/. No other cards are owned by this session.

<!-- relay:entry 20260922T011122Z-cv author=codex kind=plan -->
### Codex · 2026-09-22 01:11
Recorded verification plan and observable done criteria. Existing implementation retained; no duplicate code change needed.

<!-- relay:entry 20260922T011247Z-7h author=agent kind=evidence -->
Check · 1 not-applicable, 1 passed. The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260922T011811Z-cv author=codex kind=evidence -->
### Codex · 2026-09-22 01:18
Verified independently under isolated Xvfb/config; both terminal and Activity restored on no-argument reopen. Layout tree, cwd, scrollback ID and Activity owner survived unchanged. Recorded windowstate test passed; tests_check clear. Updated Tests, QA checklist and Verdict, linked new evidence, and moved to done. Evidence: docs/qa_evidence/2026-09-22-verify-ACT1/README.md.
