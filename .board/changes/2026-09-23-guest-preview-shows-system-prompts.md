---
id: P9SN
type: work
status: needs-verification
labels: [bug, sessions, guest]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: User report in Relay pane, 2026-09-23
links: {plans: [], commits: [c8a6312b51d29266cd1cd2302df4397d2c113a59], evidence: [docs/qa_evidence/2026-09-23-guest-preview/], related: [D2PX], github: null}
---
# Guest preview uses setup text instead of conversation content

## Issue
the preview snippet is showing system prompts to the guest agents rather than convo content.

## Done means
- Guest session titles and snippets use the first actual user request, skipping Relay setup, handover, and interruption notes.
- A guest transcript with only setup text has no misleading prompt snippet.
- Existing indexed guest sessions refresh their displayed title and snippet after reindexing.

## Plan
**Goal:** identify user-authored content before deriving guest titles and row snippets.

**Findings:** Claude and Codex parsers treat every user-role message as a prompt. Relay passes some setup and handover context as user-role text, so it becomes the first prompt and title fallback.

**Steps:** filter known Relay-generated prefixes in both guest parsers; keep a first-user-prompt preview; trigger a one-time reindex of old guest rows; test representative wrappers.

**Risks:** preserve actual user requests following a context block in the same message, and do not hide a message merely because it discusses Relay.

**Verify:** targeted guest parser and index tests against fixture transcripts and a read-only sample of existing session text.

## Execution Summary
Claude and Codex parsing now discards Relay setup envelopes and saves the first actual user request as `first_prompt`. List snippets use that request, and a setup-only transcript has no prompt snippet. Parser-versioned cursors force a one-time reparse of unchanged guest transcripts while preserving their files and user metadata.

## Tests
- PASS: `PYTHONPATH=backend:tests python3 -m unittest test_guest_sessions test_conv_index` (231 tests).
- PASS: `git diff --check` for changed parser, index and test files.
- Evidence: `docs/qa_evidence/2026-09-23-guest-preview/README.md`.
