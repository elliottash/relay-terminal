---
id: TYH4
type: work
status: needs-verification
labels: [feature, sessions]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 3478d988-a618-47c5-a4f4-7196fdc7261f
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: Codex in Relay pane, 2026-09-23
links: {plans: [], commits: [cb0200d9dd263b572a3f4c9dd5139e06a1a55450], evidence: [docs/qa_evidence/2026-09-23-TYH4/validation.md], related: [], github: null}
---
# Link printed session IDs to new panes

## Issue
allow agents to print session ids that will convert to links an open in a new pane.

## Done means
A standalone saved Relay, Claude, or Codex session ID printed in agent output becomes a link. Activating it opens that saved conversation in a new pane, or reveals its existing pane. Unknown IDs never open an unrelated session; ordinary hashes and file links retain their behavior.

## Plan
**Goal:** Make printed session IDs actionable in terminal output.

**Findings:** `src/OutputLinks.cpp` already recognizes `session:<id>`, but `src/Pane.h` routes it to a Sessions search. `backend/relay_core/session_protocol.py` can query indexed rows by `session_ids`, and `Pane::openSavedSession` already opens saved Relay and guest sessions in new panes.

**Steps:** 1. Recognize standalone UUID and 32-character hex session IDs in output links. 2. Resolve an activated ID through the conversation index and pass its saved row to `openSavedSession(..., true)`. 3. Add targeted parser and protocol tests.

**Risks:** Arbitrary hashes share the same shape; clicking an unknown one must show a clear failure and open no pane.

**Verify:** Run targeted output-link and session protocol tests, build Relay with `scripts/relay-build`, and capture an isolated GUI click if the environment supports it.

## Tests
`python3 -m unittest tests.test_session_protocol.ProtocolHandlerTests.test_clicked_session_id_resolves_exact_saved_row`
`ctest --test-dir build -R '^outputlinks$' --output-on-failure`
`scripts/relay-build --target relay`

The broader `conversations` CTest currently fails in `sessionsDropdownsRespondToMouseChoices` at `chooseComboItem(group, "none")`; the failing code is from another active checkout change that removes the Continue group, outside this card.

## Execution Summary
`src/OutputLinks.cpp` recognizes standalone Relay hex IDs and guest UUIDs as session links. `backend/relay_core/session_protocol.py` resolves clicked IDs by exact index lookup; `src/Pane.h` and `src/RelayWindow.h` open the saved row through the existing new-pane resume path, including from helper consoles. Unknown IDs yield an error and no new pane. Parser and protocol tests pass; `scripts/relay-build --target relay` succeeds.

No GUI screenshot was captured in this implementation session. The verifying session should click a printed saved ID in an isolated Relay profile and record the resulting pane.
