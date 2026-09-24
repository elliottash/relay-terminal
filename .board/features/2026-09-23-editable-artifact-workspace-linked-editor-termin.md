---
id: P2W8
type: work
status: discussing
labels: [feature, panes, files, agent-ui]
waiting_on: owner
rank: zzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: Owner in a Relay pane, 2026-09-23; research in reports/Editable workspaces for Relay.md (commit d1249048)
links: {plans: [], commits: [], evidence: [], related: [MDA7, 2GV0], github: null}
---
# Editable artifact workspace: linked editor, terminal/agent and preview panes, with human and agent editing the same file

## Issue
do you know the canvas feature that chatgpt had? does the chatgpt app, claude app, other harnesses, have that for local work? also research overleaf. i want to set up relay that i can have an editable document in one pane and a linked terminal pane next to it, i type requests in the paen and it edits the document, and i can also edit manually at the same time. 

are there any AI co-work systems lke this in a terminal? 

another interesting modality would be 3 panes with the terminal, TEX, and PDF. it could be 1:1:1 or 2:1 with the TEX and terminal on the left, with terminal beneath it. 

do deep research on related features to see what we can build for relay. 

i guess this works for working on scripts as well. artwork / CAD as well. err on the side of broadening this feature.

## Discussion points
Research is in `reports/Editable workspaces for Relay.md` (commit d1249048) with the per-topic notes under `research_notes/Editable workspaces for Relay/`. Short version: ChatGPT's side-by-side Canvas is retired for current models; Claude Docs is the closest to "keep typing while the agent edits"; neither links the document to a local file. Overleaf is the reference for source + compile + PDF + SyncTeX. Terminal agents (Codex, Claude Code, Aider, Zed, Cursor) edit project files but expose uneven change records and no shared editor buffer.

**What Relay has today** (`docs/ARCHITECTURE.md` section 10): `relay::FilePreview` edits local text and Markdown (atomic save through `QSaveFile`, `●` for unsaved), edits SSH-hosted text with an Overwrite/Reload choice, renders Markdown, images and PDF (`QPdfView`, only when Qt PDF is built in — the `.deb` and AUR packages leave it off). `RelayWindow::openPath` places a preview beside the anchor pane and reuses one already open; tool panes split, close and restore like terminal panes and the layout is saved. `relay open PATH`, the explorer and clickable `file:line` output all land in the same preview.

**What is missing, in dependency order.**
1. *Shared-file safety.* `FilePanes` has no `QFileSystemWatcher`; a local save does not compare the disk file with the revision it loaded; the agent's `edit_file`/`write_file` and a shell's `sed -i` write the disk file while the buffer may be dirty, so someone loses edits. Needed: watch the file, three-way merge an external change into a dirty buffer (base = loaded revision), revision-aware save, and apply the agent's own edits to the *open buffer* as revision-aware patches (cursor and undo preserved, each patch a marked undo step) rather than through the disk. This is the foundation for "I type requests in one pane and edit manually at the same time".
2. *Linked pane group.* A workspace id shared by an editor pane, a console (terminal/agent) pane and a preview pane, so they open, close, restore and move together, `edit_file` on the open file lands in that editor, and the two requested layouts exist as named presets: **1:1:1** (terminal | TEX | PDF) and **2:1** (TEX above terminal on the left, PDF on the right).
3. *Artifact runner.* source → command → output with debounce on save, incremental rebuild, a diagnostics parser into clickable `file:line` rows (`OutputLinks` already resolves those), the preview reloading in place and showing **live / building / stale / failed** plus the source revision it was built from. TeX first (latexmk), then Typst and Quarto/pandoc as the same runner with a different command.
4. *SyncTeX both ways.* Editor line → PDF page/position through `synctex view`, PDF click → editor line through `synctex edit`; `QPdfView`'s page navigator can jump to a page and location.
5. *Typed previews beyond PDF.* Script → plot/table/console output (with #MDA7's inline images and tables), diagram source → SVG, artwork file → canvas, CAD source → rendered view with an explicit Regenerate step. Each preview declares what it renders and which representation is authoritative.
6. *Remote.* For a project on an SSH host, run the build beside the source over the pane's own connection and fetch the PDF, SyncTeX map and log as one generation (the `RemoteFile` path already exists for text).

The per-task packaging of 2–5 (TeX workspace, Python/Stata kernel IDE, and the others) is scoped in the design card on task plugins; this card owns 1 and the pane-group/layout work everything else sits on.

### Decisions to settle
1. Agent edits: applied live into the open buffer as marked undo steps (Claude Docs style), or shown as a patch to accept first (Cursor/Zed style)? Recommendation: live by default, with a per-workspace "review before apply" toggle.
2. When the agent and the user change the same lines: three-way merge with the conflict shown inline under a chip (recommended), or last writer wins?
3. Qt PDF in the `.deb` and AUR packages? It is off today, so those builds have no in-Relay PDF pane.
4. The console pane in a group: a full terminal pane (recommended, so `latexmk`, `git` and the REPLs work there) or an agent console without a shell?
5. Start with phase 1 (shared-file safety) before any TeX or kernel work? Recommended: yes, everything else loses edits without it.

## Done means
- A file open in an editor pane is watched: an external change (agent `edit_file`, `sed -i`, `git checkout`) merges into a dirty buffer three-way and never overwrites unsaved typing; a save whose disk revision moved offers merge, overwrite or reload instead of silently winning.
- The agent's edits to a file that is open land in that buffer as marked undo steps while the user keeps typing, with a change view listing what each turn changed.
- An editor, a console (terminal/agent) pane and a preview pane can be linked into one group that opens, closes, restores and moves together, with the **1:1:1** and **2:1** layouts as named presets.
- The same works for a file on an SSH host through the pane's own connection.
- Focused tests: watcher and merge cases, save races, layout save and restore; a QA run of the TeX flow once #MEPR's plugin 1 lands.
