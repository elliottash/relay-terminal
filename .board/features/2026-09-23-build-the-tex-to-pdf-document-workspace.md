---
id: WYGY
type: work
status: executing
labels: [feature, plugins, tex, pdf]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: b65a84fc-848b-48e8-8d87-d77e2cf7423d
blocked_by: [E85D]
rank: zzzzzzzzzzzzzzzzzzi
created: '2026-09-23'
verify: {artifact: code, primary: script, also: [person], human: required, criteria: 'Targeted pytest (test_workspace_plugins, test_tex_build) and ctest (artifactworkspace, filepanes) pass. An Xvfb run on the fixture captures: save → building → live with its revision; broken edit → failed with the last good PDF kept; a diagnostic click opening the editor line; forward and inverse SyncTeX; an \input edit marking the PDF stale; restore after relaunch. Evidence is in docs/qa_evidence/<date>-tex-workspace/.', sign_off: none, effort: high, stakes: rework, blast: capability}
source: Owner in a Relay pane, 2026-09-23; implementation slice from reports/Editable workspaces for Relay.md
links: {plans: [], commits: [], evidence: [], related: [MEPR, P2W8, F8R7, E85D, C0Q8], github: null}
---
# Build the TeX-to-PDF document workspace

## Issue
then lets go to your reports and write detailed cards for the editable artifacts and plugins features

## Discussion points
**First document plugin from #MEPR.** Reuse #F8R7's safe editor, #E85D's linked roles and layouts, and #C0Q8's manifest/runner contract. Start local with `latexmk` because it produces a PDF, log and SyncTeX map; Typst and Quarto become later runner definitions, not separate pane systems. Compile a saved source revision, show that revision beside the preview and mark it stale as soon as a newer edit exists. Remote compilation uses the terminal pane's SSH host and transfers PDF, log and SyncTeX data as one generation; its output must not claim to be current until all parts match. Qt PDF is optional in current packaging, so the plugin must report availability and retain an external-viewer path.

## Done means
- Opening a `.tex` project as a document workspace fills a linked source editor, a full terminal/agent console and a PDF preview, in either preset (1:1:1 or 2:1). Files pulled in by `\input`, `\include` or the bibliography join the group's sources: an edit to one marks the PDF stale and triggers a build.
- Saving a source revision debounces and runs `latexmk` in the project root. The UI reports `building`, `live`, `stale` or `failed`, keeps the last good PDF on failure, and names the saved revision that produced it. An older build finishing late never replaces a newer one.
- LaTeX errors and warnings appear as rows that open the owning source file at the line. Forward SyncTeX takes an editor position to the PDF. Inverse SyncTeX takes a PDF location to the editor, keeping page and zoom where possible.
- With Qt PDF built, the preview refreshes in place. Without it, the workspace says why and opens the generated PDF externally.
- A fixture project verifies the first build, an incremental edit, a failed build with the last good PDF kept, diagnostics, both SyncTeX directions, restore, and the optional-PDF fallback. SSH builds are #CWDZ (owner decision 2026-09-26).

## Plan
**Goal.** Deliver the local Overleaf-like loop on the pieces that already landed: save → PDF rebuilds beside it, the strip names the revision, errors are clickable, and SyncTeX works both ways. This card adds only the TeX-specific GUI ↔ worker wiring.

**Findings (re-checked 2026-09-26 12:50 on `main` 3ec4c6d0).**
- Landed, and relied on here:
  - `backend/relay_core/tex_build.py`: `request_build` debounce `:1484`, superseding generations, diagnostics, `forward_search :1051`, `inverse_search :1072`, `dependencies :1546`, `-norc` by default.
  - The TeX runtime in `workspace_plugins.py` emits `tex_status` (`:540`) and offers the agent `tex_*` tools.
  - The group model in `src/ArtifactWorkspace.h` (`applyBuildStatus`, generations, presets) and `src/RelayWindowWorkspace.cpp`.
  - The shell → TeX → PDF chain (`def2cf0b`), the docked agent with `/build` (#PBZ4), buffer safety (#F8R7) and Qt PDF in Debian packages (#9Y7X).
- Still missing:
  - `rg tex_status src/` is empty. `src/PaneEvents.cpp:69` routes `workspace_console` but no TeX events.
  - `WorkspaceManager.TYPES` (`workspace_plugins.py:782`) has no GUI build or sync request, so a build runs only when the agent calls `tex_build`.
  - `rg -i synctex src/` finds only comments.
  - Diagnostics reach only the agent.
  - Included files never join `group.sources`.
- The pane that sent `workspace_activate` (`src/PaneRuntime.cpp:1213`) owns the runtime. Its worker is where `tex_build` and `tex_sync` go, and its events are what the group must follow.
- #SJ00 (pane dc713c52, publication pending) fixes #E85D t:q7, which registers the PDF output with the PDF adapter. Step 2 builds on that fix and does not redo it.

**Steps.**
1. **Protocol 36.** Add requests `tex_build {workspace_id, debounce?}` and `tex_sync {workspace_id, direction: forward|inverse, file,line,column | page,x,y}` to `WorkspaceManager.TYPES`. Answer with the existing `tex_status` event and a new `tex_location` event. Add a pytest for each in `tests/test_workspace_plugins.py`. Document both in `docs/AGENT-SESSIONS-PROTOCOL.md` §36.
2. **Status strip and save.** `PaneEvents.cpp` routes `tex_status` to the window. `RelayWindowWorkspace.cpp` finds the group by `workspace_id` and calls `ws::applyBuildStatus` on its PDF output. The strip shows state and a short revision, and the `QPdfView` reloads keeping its page and zoom. A save in `FilePanes.cpp` of any path in `group.sources` sends `tex_build`, and the builder debounces. Add an `artifactworkspace_test` case feeding a recorded `tex_status` sequence, including a late older generation.
3. **Diagnostics.** On a `failed` or warning status, show the diagnostics as a collapsible row list under the strip. Each row is an `OutputLinks` `file:line` that opens or focuses the group's editor at that line. The PDF view keeps the last live generation.
4. **SyncTeX.** Forward: an editor action "Show in PDF" sends `tex_sync forward`, and `tex_location` scrolls the preview (`QPdfPageNavigator::jump`). Inverse: Ctrl+click in the preview maps the point to page coordinates and sends `tex_sync inverse`, and `tex_location` moves the linked editor's cursor. Add a shortcut hint for "Show in PDF" (RELAY.md rule).
5. **Included files.** After each live build, merge `dependencies()` source files into `group.sources`, so they count in the revision and trigger builds on save. Opening them as linked editor panes is #E85D t:m3's open owner question. Until that is answered they open on demand through diagnostics and SyncTeX, not automatically.
6. **Live verification** under Xvfb with an isolated `XDG_CONFIG_HOME` and the fixture from `tests/test_tex_build.py`. The captures are listed under Verify.

**Order.** Steps 1 → 2 are one commit and unblock the rest. Steps 3, 4 and 5 are independent after that. Step 6 comes last. Publish each commit with `relay-land submit HEAD --card '#WYGY'` (queue mode, not `land.py`).

**Risks.**
- `RelayWindowWorkspace.cpp`, `FilePanes.cpp` and `PaneEvents.cpp` are shared with #E85D, #R660, #PBZ4 and #SJ00. Keep each hunk small and let the queue merge.
- SyncTeX coordinates: `synctex` uses PDF points from the top-left and `QPdfView` uses widget pixels scaled by zoom. The conversion needs its own test.
- A build that runs on every save can be heavy. The builder already debounces (0.4 s) and supersedes a running build.
- Nothing here needs an owner decision except t:m3 (above), and step 5 has a safe default.

**Verify.** See `verify` and Done means. Tests: `pytest tests/test_workspace_plugins.py tests/test_tex_build.py`, `ctest -R 'artifactworkspace|filepanes'`, then the live Xvfb run. Evidence goes in `docs/qa_evidence/<date>-tex-workspace/`.

## Tasks

- [x] TeX builder, manifest and local fixture (01e4e216 builder + fixture; manifest 13909113, v2 b51072ff) <!-- t:eq -->
- [x] Bind source/console/PDF roles and both layouts (#E85D 4ad5fe87; chain #R660 def2cf0b) <!-- t:2g -->
- [ ] Protocol-36 tex_build/tex_sync; GUI applies tex_status; save triggers a debounced build <!-- t:qk -->
- [ ] Diagnostics as clickable file:line rows; last good PDF kept on failure <!-- t:d4 blocked_by=qk -->
- [ ] Forward and inverse SyncTeX in the GUI <!-- t:j2 blocked_by=qk -->
- [ ] Included/bibliography files join the group's sources <!-- t:k8 blocked_by=qk -->
- [x] SSH TeX builds transferred to follow-up #CWDZ; local flow is this card's scope <!-- t:en card=CWDZ -->
- [ ] Verify local end-to-end build, PDF preview, diagnostics, SyncTeX and restore live <!-- t:xq blocked_by=d4,j2,k8 -->

## Decisions
- 2026-09-26 — Owner: “yes to all.” Ship local TeX builds first; track SSH builds separately. Keep project `latexmkrc` disabled by default until project plugins are explicitly enabled. PDF annotations and review comments are outside this card.
