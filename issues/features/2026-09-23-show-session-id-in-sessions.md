---
id: S7D4
type: work
status: needs-verification
labels: [feature, sessions, gui]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: User request in Relay pane, 2026-09-23
links: {plans: [], commits: [a5c05608d93dd431742551a7fdccbe9919e146a5], evidence: [docs/qa_evidence/2026-09-23-session-ids/], related: [D2PX], github: null}
---
# Show the session ID in Sessions

## Issue
show the session id.

## Done means
- Every session row shows its complete session ID where it can be selected and copied.
- The ID does not crowd out the title, recap, or open badge in a narrow Sessions pane.

## Plan
**Goal:** expose each row's stable session ID without making the list hard to scan.

**Findings:** the Sessions model already receives `session_id` and stores it in `kIdRole`, but only paints title, badges, and opening prompt.

**Steps:** add an ID line or control to the row using its existing data; add a focused widget assertion and visual evidence.

**Risks:** long UUIDs may be clipped in narrow panes; a tooltip or copy action must preserve the full value.

**Verify:** build the Sessions widget target, run the row test, and inspect an isolated screenshot.

## Execution Summary
Sessions rows show an ID line under the title and opening prompt. A narrow column elides the middle; the tooltip gives the full ID and the row context menu copies it. ![Sessions list with session IDs](docs/qa_evidence/2026-09-23-session-ids/sessions-with-ids.png)

## Tests
- PASS: `scripts/relay-build --target relay-conversations-tests`.
- PASS: `RELAY_SHOT_DIR=$PWD/docs/qa_evidence/2026-09-23-session-ids xvfb-run -a build/relay-conversations-tests sessionRowsExposeFullIds` (3 QtTest checks; tooltip and context-menu copy verified).
- PASS: `scripts/relay-build --target relay` and the exact commit tree build in `land.py`.
- Evidence: `docs/qa_evidence/2026-09-23-session-ids/sessions-with-ids.png`.
