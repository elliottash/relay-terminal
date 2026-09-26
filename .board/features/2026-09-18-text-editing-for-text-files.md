---
id: 4TNY
type: work
status: planned
labels: [feature]
assignee: agent
rank: zzzz111
created: '2026-09-18'
acceptance: a text file opens in an editable pane with word wrap, highlighting and Ctrl+F
source: 'issues/feature_intake.txt, 2026-09-18: "add text editing for text files."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Text editing for text files: word wrap, syntax highlighting, find

## Issue

add text editing for text files.
add:
 word wrap
 syntax highlighting 
find (ctrl+F)
anything else? check warp etc. make it like a lightweight version of kate / kwrite.

## Planning notes

Freshness check, 2026-09-26: `FilePreview` now has an editable `QPlainTextEdit`, Ctrl+S save,
word wrap/Alt+Z, syntax highlighting and external-change handling (`src/FilePanes.cpp`; focused
coverage in `tests/filepanes_test.cpp`). #WWB2 owns the wrap button, #QEKA the persistence of its
setting, and #F8R7 the external/agent edit conflict path. No file-editor find UI or Ctrl+F wiring
appears in `FilePanes.cpp`; the pane-wide find elsewhere is not a file-buffer search.

## Done means

In an editable text-file pane, Ctrl+F finds text in that file, reports no matches, and supports
next/previous match without losing unsaved edits. The existing edit, save, wrap and highlighting
behavior continues to pass its focused tests.

## Plan

**Goal.** Finish the original lightweight-editor request by adding find in the file buffer and
verifying the pieces that have already landed.

**Findings.** `src/FilePanes.cpp` owns the text editor, wrap and syntax highlighting; the file
pane tests already cover these. The remaining Ctrl+F behavior is not implemented there.

**Steps.**
1. Add a compact find field in `FilePreview`, scoped to the current text document. Ctrl+F focuses
   it; Enter/Shift+Enter move through matches, Escape returns focus to the editor, and no-match
   feedback is visible. Keep rendered previews and binary files unaffected.
2. Cover search, wraparound, no-match, and unsaved-buffer search in `tests/filepanes_test.cpp`.
3. Run the focused file-pane test. Check the editor manually with a long highlighted file and an
   unsaved change; then update this card's acceptance and close if all original requirements pass.

**Risks.** Ctrl+F may be a window-level shortcut while a file pane has focus; route it to this
editor only in that context. Do not duplicate #QEKA's wrap-persistence work here.

**Verify.** `filepanes` targeted test plus a live Ctrl+F/Enter/Escape check in an editable text
file. The original request does not require a full Kate/KWrite feature set beyond these basics.
