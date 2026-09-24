---
id: 5V08
type: work
status: needs-verification
labels: [feature, files, editor]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 85199371-f03a-45df-b83a-241dba712742
rank: zzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [person], human: optional, criteria: 'Open a representative DOCX, edit formatted text, save and reopen it; the edit and surrounding formatting appear as expected.', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: Relay pane, 2026-09-24
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-5v08-docx/docx-editor.png], related: [], github: null}
---
# Edit DOCX files with formatting in Relay

## Issue
feature request: rich editing of DOCX files in relay terminal. you can borrow code from ~/tracelaw for that

## Plan
**Goal:** Add an embedded rich editor for local DOCX files.

**Findings:** `src/FilePanes.cpp` currently routes DOCX to file info. Tracelaw's `pipeline/docx_parser.py` parses Word runs and preserves structural anchors; its DOCX export edits the original package. `FilePreview` already owns open, save and dirty UI.

**Steps:**
1. Add a DOCX bridge that reads paragraph/run formatting and applies anchored edits to the original package without rebuilding unrelated parts.
2. Show DOCX content in a rich Qt editor with formatting controls and wire it into FilePreview open, edit, dirty and save behavior.
3. Add focused round-trip and pane tests, then build the affected target.

**Risks:** DOCX has features Qt cannot render exactly; preserve unsupported parts in the archive and refuse edits that cannot map safely. Start with local files and surface that scope in the pane.

**Verify:** Run targeted tests and inspect a saved DOCX in the pane and with a document parser.

## Done means
Opening a local DOCX in Relay shows formatted document text in an editable pane with obvious formatting controls.
Saving writes a valid DOCX atomically, retaining untouched package parts and refusing an external-change conflict rather than silently overwriting it.
A round-trip test covers text and formatting changes and confirms unrelated DOCX content survives.

## Execution Summary
Added an embedded DOCX view with bold, italic and underline controls, heading and table display, dirty state, Save, and reload in `src/DocxEditor.*` and `src/FilePanes.*`. Local DOCX paragraphs with Word objects that cannot be mapped safely are read only. The `backend/relay_core/docx_edit.py` bridge edits only anchored simple paragraphs in the original DOCX ZIP, retains untouched entries, validates the result, and refuses a stale file hash.

The representative editor view shows formatted text and a two-cell table: ![DOCX editor showing headings, formatted text, and a table](docs/qa_evidence/2026-09-24-5v08-docx/docx-editor.png)

Rich editing is currently local; a remote DOCX pane states this limitation.

## Tests
`PYTHONPATH=backend python -m unittest tests.test_docx_edit -v`
`ctest --test-dir build-fast -R '^filepanes$' --output-on-failure`
`scripts/relay-build --fast --target relay`
manual: docs/qa_evidence/2026-09-24-5v08-docx/docx-editor.png

### Check
2026-09-24: Both DOCX bridge tests and the file pane suite passed; the fast Relay app target built. A saved fixture reopened in python-docx with edited italic text and an intact table. The screenshot was captured from the Qt pane.
