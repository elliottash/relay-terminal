---
id: R660
type: work
status: planned
labels: [feature, panes, artifacts, preview]
rank: zzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
source: 'Owner in a Relay pane, 2026-09-25; slice of #P2W8 (D6)'
links: {plans: [], commits: [], evidence: [], related: [P2W8, E85D, WYGY, F8R7], github: null}
---
# Linked pane chains: shell → TeX editor → PDF preview as one group that opens, restores and closes together

## Issue
also note and build the case of multiple linked panes -- eg shell -> TEX -> PDF.

## Plan
Slice 8 of #P2W8 (decision D6, D4). **Wave 2: waits for the hold on #E85D to lift** (`src/ArtifactWorkspace.{h,cpp}`, `src/RelayWindowWorkspace.cpp` are untracked in the tree) and for #WYGY's role bindings.

**Goal.** A chain of linked panes, shell → TeX editor → PDF preview, that is one group: each member can open the next, the group has a direction and a head, it restores after a restart with its sizes, moving one member keeps the link, closing the head asks about the rest, and the headers show the chain.

**Findings.** `ArtifactWorkspace` already models a persisted group id, plugin kind, root, layout, source files, output generations and member→role links for Editor/Console/Preview/Variables, with `1:1:1` and `2:1` presets and navigation from source links to the linked editor (`src/ArtifactWorkspace.h:37-65`, `:104-142`, `src/RelayWindowWorkspace.cpp:20-59`, `:201-253`, `:318-445`). Its Console role is a full terminal `Pane` (D4). What it lacks is the chain: order among members, "open the next" from a member, more than one member per role, and the header chrome.

**Steps.**
1. Model: `ArtifactWorkspace` gains an ordered member list with an `upstream` per member (shell → editor → preview), `head()`; serialization bumps the group's version with a reader for the old shape.
2. Open-the-next: from a shell pane, `relay open main.tex` or a click on `main.tex` in its output offers "Open beside, linked" (joins the group as editor); from the editor, Build (WYGY) opens or refreshes the preview as the group's next member; from a preview, inverse SyncTeX goes to the linked editor (WYGY t:j2).
3. Chrome: a chain chip in every member's header ("⛓ shell › main.tex › main.pdf", the current member bold); click = focus that member; the presets from #E85D applied to the whole chain.
4. Lifecycle: closing the head asks "Close the linked panes too?"; moving a member across tabs moves the group; restore rebuilds the chain in order with the saved sizes; a member whose file is gone restores as a placeholder with "Reopen".
5. Docs: `docs/ARCHITECTURE.md` workspace section; a `relay.tex` walkthrough in `docs/TASK-PLUGINS.md`.

**Files.** `src/ArtifactWorkspace.{h,cpp}`, `src/RelayWindowWorkspace.cpp`, `src/PaneChrome.h` (chip), `src/Pane.h` (the shell's open-beside offer), tests `tests/artifactworkspace_test.cpp`.

**Verify.** Live under Xvfb with TeX Live: shell → `relay open main.tex` → Build → PDF; edit a line, Build, the PDF refreshes with the generation shown; restart Relay, the three panes come back linked and sized; close the shell, the dialog offers to close the rest. Evidence under `docs/qa_evidence/<date>-linked-chain/`.

## Tasks

- [ ] Ordered members with upstream and head; serialization <!-- t:9s blocked_by=#E85D -->
- [ ] Open-the-next from shell, editor and preview <!-- t:vq blocked_by=9s,#WYGY -->
- [ ] Chain chip and presets across the chain <!-- t:qr blocked_by=9s -->
- [ ] Close/move/restore lifecycle <!-- t:fy blocked_by=9s -->
- [ ] Docs and live evidence <!-- t:mk blocked_by=vq,qr,fy -->
