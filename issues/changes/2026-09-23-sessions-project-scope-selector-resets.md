---
id: SC7P
type: work
status: needs-verification
labels: [bug, gui, sessions]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: Codex in a Relay pane, 2026-09-23
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-sessions-scope/], related: [916B], github: null}
---
# Make the Sessions project scope selector effective

## Issue
i think the "this project" vs "all projects" option there doesnt work

## Done means
- Switching to All projects sends an unfiltered all-project Sessions query, even after a specific Project filter was selected.
- Switching back to This project sends a project-scoped query, even after a specific Project filter was selected.
- The scope selector and Project filter show the query actually requested after the worker replies.

## Plan
**Goal.** Make the scope selector reliably control which projects the Sessions list searches.

**Findings.** `queryRequest()` sends the selected scope, but a specific Project filter makes the backend force `scope: all`. `setResults()` then follows that scope. Choosing This project currently leaves the conflicting Project filter in place, so the selector reverts on the reply.

**Steps.** Clear the specific Project filter when the user explicitly changes scope, then send one query with the selected scope. Add a widget test for selecting a project, switching to This project, and switching back to All projects.

**Risks.** A `project:` operator typed in search also requests other projects; the selector should continue to reflect the worker's effective scope in that case.

**Verify.** Build and run the focused GUI test and conversations suite; capture the scope selector after switching back to This project.

## Execution Summary
The Sessions scope selector now clears a narrower Project chooser selection before querying when the user chooses This project or All projects. Reselecting the current scope does the same, so an existing Project filter cannot silently keep the list narrowed. The Project chooser still supports deliberate filtering after a scope choice. The backend's `project:` search operator continues to control scope as documented.

![This project selected with Any project filter](docs/qa_evidence/2026-09-23-sessions-scope/sessions-this-project.png)

## Tests
- `scripts/relay-build --target relay-conversations-tests` — passed.
- `QT_QPA_PLATFORM=offscreen RELAY_SHOT_DIR="$PWD/docs/qa_evidence/2026-09-23-sessions-scope" build/relay-conversations-tests scopeChoiceClearsConflictingProjectFilter` — passed.
- `QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^conversations$' --output-on-failure` — passed (1/1).
- `python3 -m unittest tests.test_conv_index.IndexTests.test_scope_and_filters tests.test_conv_index.ProtocolTests.test_scope_project_uses_the_requested_workspace tests.test_conv_index.ProtocolTests.test_project_operator_reports_the_scope_it_switched_to` — passed (3/3).
- Manual evidence: `docs/qa_evidence/2026-09-23-sessions-scope/notes.md`.
