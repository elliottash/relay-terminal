---
id: 9P3S
type: work
status: needs-verification
labels: [bug, files, editor, artifacts]
assignee: agent
implemented_by: openai/gpt-6-sol via codex:elliott-t-ash-gmail-com
session: e9ebd5f4-ef84-49d4-8044-ae778e9a2c7c
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [ai-visual], human: optional, criteria: 'Edit a TXT file while the agent changes it: typed text remains and incoming additions/removals are clear in green/red.', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: Owner in Relay pane, 2026-09-25
links: {plans: [], commits: [6f7976c626f1], evidence: [docs/qa_evidence/2026-09-25-9P3S/01-txt-coedit.png], related: [F8R7], github: null}
---
# TXT coediting can overwrite live typing and lacks persistent inline diffs

## Issue
When a person edits a TXT file in Relay while AI changes it, the incoming change can clobber their current edits. The editor needs to preserve both sides and show additions and removals inline in green and red while work continues.

> joint editing of TXT with AI doesnt work. im editing , and then it made a change and clobbered myh edits. it needs to be seamless. where you see the diffs come in with green and red highlights wile you work.
> — elliott · [session:962c4bf576434dc883562ea6806b6576](relay://session/962c4bf576434dc883562ea6806b6576) · 2026-09-25

## Done means
- Typing in an open TXT editor survives an AI write, including when the editor cannot answer a native file-tool request; a missing answer never causes a fallback disk overwrite.
- Incoming non-overlapping edits merge with live typing, while overlapping edits preserve both versions for explicit resolution.
- Incoming additions and removals remain visibly differentiated in green and red in the editor or its adjacent change display while the person keeps typing.
- Focused native-worker and file-pane tests cover the timeout race, merge/conflict cases, and visual diff cues.

## Plan
**Goal.** Keep the open TXT buffer authoritative during joint editing and make each incoming change visible.

**Findings.** `backend/relay_core/tools.py::_through_buffer` returns `None` after a missed editor reply, allowing a disk write for a file listed as open. `src/FilePanes.cpp::reconcileWith` protects dirty text from external writes, but only the native patch path shows agent changes, and its one-color highlight fades after four seconds.

**Steps.** 1. Refuse disk fallback for every open-buffer patch request that fails to answer; add a race test in `tests/test_open_buffers.py`. 2. Record and display incoming additions and removals for native patches and external file writes in `FilePreview`, using green/red cues that persist while editing. 3. Add focused `filesync` checks for retained typing and diff cues, then build and run targeted tests.

**Risks.** Guest tools and shell commands can still write disk directly, so the file watcher's merge path must show the same cues. Deleted text cannot occupy its former place in the live document; display its red old-text preview adjacent to the editor and anchor the location in the editor.

**Verify.** `python3 -m unittest tests.test_open_buffers`; `land.py try` with `filesync`; inspect a live TXT pane with an isolated profile.

## Tests
- `tests/test_open_buffers.py`
- `ctest -R filesync` — tests/filesync_test.cpp
- `ctest -R filepanes` — tests/filepanes_test.cpp

### Check
2026-09-25: Python open-buffer suite, filesync and filepanes passed in the isolated candidate. Relay tests_run `20260926T033649Z-fa12` recorded all three passing for revision `6f7976c626f1`. Screenshot: docs/qa_evidence/2026-09-25-9P3S/01-txt-coedit.png.

## Execution Summary
Landed `6f7976c6`. An unanswered editor patch now refuses the write instead of falling back to disk after the person may have begun typing. Native patches and external file writes show persistent green inserted text and a red removed-text preview; the live buffer retains non-overlapping unsaved typing and leaves overlaps for explicit resolution.

![TXT editor preserving unsaved typing while showing incoming red and green changes](docs/qa_evidence/2026-09-25-9P3S/01-txt-coedit.png)
