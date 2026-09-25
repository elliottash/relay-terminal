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
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-25-69BV-reload/measurements.md], related: [HEY7], github: null}
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
- Reloading a pane with a saved conversation shows its earlier turns even when the pane's scrollback file contains only a short recap.
- A reload does not replace a fuller conversation text sidecar with a shorter recap-only snapshot.
- A targeted regression check proves both recovery and sidecar preservation; failure is a pane retaining its session but losing earlier visible turns.

## Plan
**Goal.** Restore prior conversation turns after reload, including when saved pane text is a recap-only fragment.

**Findings.** The reported panes map to saved pane files `1a09e2d3…txt` (19 KB) and `81e219d7…txt` (7.5 KB); the latter contains no earlier turn despite a four-turn transcript. `Pane::initRestore` replays only the pane file. `syncSessionText` resets the conversation boundary after replay, so a later save can overwrite the fuller session sidecar with a recap.

**Steps.** 1. Request transcript coverage when restoring a pane with a saved session, using the existing fill path. 2. Render the transcript when no saved-text anchor matches, and preserve fuller sidecars during the restore boundary. 3. Add targeted tests for the recovery and preservation rules.

**Risks.** A transcript may have fewer formatting details than the original terminal text; replay the saved text where it covers turns and fill only what is missing.

**Verify.** Build through `scripts/relay-build`, run the targeted C++ tests, and inspect the two reported panes' saved files and transcript coverage.

## Tests
`ctest:transcriptreplay`

### Check
Pass (2026-09-25): `ctest --test-dir build -R '^transcriptreplay$' --output-on-failure` — 1/1 passed. `scripts/relay-build --target relay` completed successfully. The reported pane 825839fb has four transcript turns but zero saved prompt markers; the regression case covers this recap-only state.

## Execution Summary
Restored panes now request transcript coverage for their saved session. When saved text has no matching prompt or reply, Relay draws the transcript before replaying the saved fragment. After a restored session loads, its save boundary begins before the replayed text, so a later save does not reduce the conversation sidecar to only the recap.

The reported pane 825839fb has a four-turn transcript but a 7.5 KB pane file with zero prompt markers; 266dc3f5 has a one-turn transcript and a 19 KB pane file. The current running app has not been restarted onto the new binary; a separate reload check is still needed for visual confirmation.
