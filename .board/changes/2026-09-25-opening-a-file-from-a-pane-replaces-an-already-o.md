---
id: 10RD
type: work
status: needs-verification
labels: [bug, panes, files]
assignee: agent
implemented_by: openai/gpt-6-sol via codex:elliott-t-ash-gmail-com
session: e9ebd5f4-ef84-49d4-8044-ae778e9a2c7c
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [person], human: optional, criteria: 'Open two TXT files from one pane, see both side by side, and confirm edits in the first remain.', sign_off: none, effort: medium}
links: {plans: [], commits: [ef296c8d91b3], evidence: [src/RelayWindow.h], related: [S1JP, 9P3S], github: null}
---
# Opening a file from a pane replaces an already open file

## Issue
File clicks from terminal and explorer panes should preserve each open file in its own pane and focus an existing pane for repeat opens. The current generic preview routing reuses an unrelated file pane.

> clicking a file from a pane should not replace the file thats already open. it should open mutliple panes.
> — elliott · [session:962c4bf576434dc883562ea6806b6576](relay://session/962c4bf576434dc883562ea6806b6576) · 2026-09-25

## Done means
Opening a different file from a terminal, explorer, or file pane creates a separate pane in that tab and preserves the original file and unsaved edits. Opening the same file again focuses its existing pane. A linked workspace editor also retains its current file when another source is clicked.

## Plan
Goal: Keep one visible pane per opened file in a tab.

Findings: `RelayWindow::openPath` reuses any preview for ordinary clicks. `openInWorkspaceEditor` can also replace an existing source in a linked workspace.

Steps:
1. Match file previews by path before reuse; create a new pane for a different path.
2. Keep a linked workspace editor on its current source and let another source follow the ordinary pane path.
3. Add focused regression coverage, build, and run the relevant tests.

Risks: Workspace navigation previously reused its linked editor; the change should preserve its group and original source. Verify with a manual two-file check or a visual capture.

## Tests
`ctest -R filepanes` — passed in Board run `20260926T034254Z-08c2` (49 cases passed, 2 optional skips).
`ctest -R artifactworkspace` — passed in the same run (28 cases passed).
`python3 scripts/land.py try 10rd-pane-open --target relay-filepanes-tests` — exact tree built.
`python3 scripts/land.py try 10rd-pane-open --target relay-artifactworkspace-tests` — exact tree built.

Full `relay` build failed at main tip 8f1b7102 on unrelated `Pane.h` errors (`m_linkedShell` undeclared; `onForkSession` callback signature mismatch). The passing suites do not exercise whole-window click routing, so the visual two-file check remains for verification.

## Execution Summary
Commit `ef296c8d91b3`: `openPath` now reuses a preview only for the same path and otherwise docks a new file pane. Repeat opens focus the existing pane without reloading an edit buffer. A linked workspace editor keeps its current source when another file is opened. Full app visual verification is pending because main's `Pane.h` currently fails to compile.
