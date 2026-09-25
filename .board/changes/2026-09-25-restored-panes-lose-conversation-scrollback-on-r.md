---
id: 69BV
type: work
status: needs-verification
labels: [bug, sessions, terminal]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 8a7e6f22-60c7-4a62-97db-486e48528031
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [probe], human: optional, criteria: 'After a reload, the two reported panes show their earlier conversation turns.', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: Codex guest in Relay, 2026-09-25
links: {plans: [], commits: [2c08deee786a, a5f9ce06007e], evidence: [docs/qa_evidence/2026-09-25-69BV-reload/measurements.md], related: [HEY7], github: null}
---
# Restored panes lose conversation scrollback on reload

## Issue
After Relay reloads, some panes retain their conversation but show only a short recap instead of the prior terminal scrollback. The reported panes are 266dc3f5 and 825839fb.

> there is a bug on reload. the session scrollback is sometimes missing, eg these panes:
>
> 266dc3f5
> 825839fb
> — elliott · [session:8d0a250058984f218d447c48e4a97d4d](relay://session/8d0a250058984f218d447c48e4a97d4d) · 2026-09-25

## Done means
- Reloading a pane restores its previous conversation and scrollback without appending a “Session loaded” notice, restore dividers, or an automatic resume recap.
- Existing saved scrollback from repeated restarts is cleaned of wrapped legacy dividers and “Session loaded” lines; another save and reload does not bring them back.
- Earlier turns are recovered when saved text is recap-only, and a reload does not reduce a fuller conversation sidecar to that recap.
- Targeted tests cover wrapped legacy chrome and transcript recovery; a separate live reload verifies the display.

## Plan
**Goal.** A restored pane resumes at its previous content without adding reload text, even after several restarts.

**Findings.** Pane `97149268` maps to `state/scrollback/d0d996b2…txt`: three divider pairs and multiple wrapped “Session loaded” rows are saved there. `Pane::dropRestoreMarks` matches only whole physical rows, so narrow panes keep wrapped dividers. `PaneSession.cpp` prints a new “Session loaded” line on every resume, and replay plus transcript fill print additional dividers.

**Steps.** 1. Add a tested saved-row cleanup that recognizes wrapped historical dividers and load notices, then use it on save and on replay input. 2. Keep session state restoration but stop printing reload notices and dividers. 3. Build and run targeted tests; inspect pane `97149268`'s saved rows through the cleanup to confirm repeated-restore idempotence.

**Risks.** The transcript can still have less formatting than original terminal text when the saved pane file already lost its history. Cleanup should remove only exact Relay-generated notices and leave user content intact.

**Verify.** `windowstate` and `transcriptreplay` C++ tests, exact-tree `relay` build, and a later live reload check.

## Tests
`ctest -R windowstate` — tests/windowstate_test.cpp
`ctest -R transcriptreplay` — tests/transcriptreplay_test.cpp

### Check
Pass (2026-09-25): `ctest --test-dir build -R '^(windowstate|transcriptreplay)$' --output-on-failure` — 2/2 passed. The new windowstate case removes wrapped historical dividers and load notices and is idempotent. `scripts/relay-build --target relay` completed successfully.

### Check 2026-09-25 14:10
- passed · ctest:windowstate — ctest -R windowstate passed for this revision on spark-dcc9, 2026-09-25T18:10:13Z
- passed · ctest:transcriptreplay — ctest -R transcriptreplay passed for this revision on spark-dcc9, 2026-09-25T18:10:13Z
history: thread
## Execution Summary
The original scrollback recovery (commit `2c08deee`) remains. This follow-up removes the `Session loaded` line and the automatic resume recap, and replays saved terminal text without adding divider rows. A reader now strips wrapped legacy dividers and load notices from old pane/session files before replay and from future saves. Prose blocks whose visible anchor was removed are not restored.

Pane `97149268` contains five historical load notices and six dividers spread across physical rows; the cleanup recognizes all 11 generated blocks while retaining conversation rows. See `docs/qa_evidence/2026-09-25-69BV-reload/measurements.md`. The live desktop has not been restarted onto this build, so an independent repeated-reload display check remains.
