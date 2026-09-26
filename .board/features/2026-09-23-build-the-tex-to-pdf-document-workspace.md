---
id: WYGY
type: work
status: planned
labels: [feature, plugins, tex, pdf]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
blocked_by: [E85D]
rank: zzzzzzzzzzzzzzzzzzi
created: '2026-09-23'
source: Owner in a Relay pane, 2026-09-23; implementation slice from reports/Editable workspaces for Relay.md
links: {plans: [], commits: [], evidence: [], related: [MEPR, P2W8, F8R7, E85D, C0Q8], github: null}
---
# Build the TeX-to-PDF document workspace

## Issue
then lets go to your reports and write detailed cards for the editable artifacts and plugins features

## Discussion points
**First document plugin from #MEPR.** Reuse #F8R7's safe editor, #E85D's linked roles and layouts, and #C0Q8's manifest/runner contract. Start local with `latexmk` because it produces a PDF, log and SyncTeX map; Typst and Quarto become later runner definitions, not separate pane systems. Compile a saved source revision, show that revision beside the preview and mark it stale as soon as a newer edit exists. Remote compilation uses the terminal pane's SSH host and transfers PDF, log and SyncTeX data as one generation; its output must not claim to be current until all parts match. Qt PDF is optional in current packaging, so the plugin must report availability and retain an external-viewer path.

## Done means
- Opening a `.tex` project as a document workspace fills linked source editor, full terminal/agent console and PDF preview in either requested preset (1:1:1 or 2:1); `\input`, `\include` and bibliography files open in the same group.
- Saving a source revision debounces and invokes the configured `latexmk` build in the project root. The UI reports `building`, `live`, `stale` or `failed`, keeps the last good PDF on failure and identifies which saved revision produced it. A newer build cannot be overwritten by an older completion.
- LaTeX errors and warnings link to the owning source file/line. Forward SyncTeX takes an editor position to the PDF; inverse SyncTeX takes a PDF location to the editor, preserving page/zoom where possible.
- With Qt PDF built, the preview refreshes in place. Without it, the workspace says why and opens the generated PDF externally. A remote project builds on its SSH host and fetches output, map and diagnostics as one generation.
- A fixture project verifies first build, incremental edit, failed build/last good PDF, diagnostics, both SyncTeX directions, restore, optional-PDF fallback and remote generation integrity.

## Plan
**Goal.** Deliver the local Overleaf-like TeX/PDF workspace. Save a source and the PDF beside it rebuilds; the strip says which revision it shows. Errors are clickable. SyncTeX works both ways. Build it on the shared artifact/plugin layer, not as its own pane system.

**State (audit 2026-09-26).** Already landed:
- The builder, in `01e4e216`. `backend/relay_core/tex_build.py` has a debounced latexmk runner that supersedes older builds, with atomic generations, log/blg diagnostics and SyncTeX in both directions. It has a `Transport`/`PathMap` seam, but only `LocalTransport` exists (`tex_build.py:119-260`). A project `latexmkrc` is skipped with `-norc` unless `trust_project_rc` is set (`:1400-1474`). There are 34 tests in `tests/test_tex_build.py`.
- The worker side, in `13909113`. The TeX runtime and lazy `tex_build`/`tex_diagnostics`/`tex_forward_search`/`tex_inverse_search`/`tex_dependencies` tools have native/guest parity (`workspace_plugins.py:486-630`), and the runtime emits `tex_status` (`:540`). The manifest is `plugins_bundled/tex/plugin.json` (v2), with `/build` and `/errors`.
- The group model, in `4ad5fe87` (#E85D). Outputs carry state, generation and source revision, and `applyBuildStatus` exists (`src/ArtifactWorkspace.h:50-95`). It has the 1:1:1 and 2:1 presets and file-watch live/stale. An unavailable PDF adapter gets "Open externally" (`src/RelayWindowWorkspace.cpp:118`). The shell → TeX → PDF chain landed in `def2cf0b` (#R660 code). The docked agent on the `.tex` editor with plugin `/` commands landed in `b70c33bf` and `4a5a4267` (#PBZ4). Agent edits go into the open buffer as of `7e4e0c45` (#F8R7). Qt PDF ships in the Debian packages as of `f9c7d590` (#9Y7X).

What is missing, with the evidence:
- The GUI never consumes `tex_status`: `rg tex_status src/` is empty. A builder status reaches the group only through the test hook `driveWorkspace("status")` (`RelayWindowWorkspace.cpp:957-966`), so a real build never shows as building or failed.
- The GUI has no way to start a build. `WorkspaceManager.TYPES` (`workspace_plugins.py:782`) has no build message. A build runs only when the agent calls `tex_build` (`:580`), so saving does not build.
- There is no SyncTeX in the GUI: `rg -i synctex src/` finds only comments.
- Diagnostics reach the agent only (`tex_diagnostics`). Nothing shows as clickable rows.
- Files pulled in by `\input`, `\include` or the bibliography never join `group.sources`, so they neither open in the group nor count in its revision.
- Remote builds have no SSH transport.
- This host has latexmk, pdflatex and synctex, but no xelatex.

**Split (non-overlapping).** Other cards own:
- #E85D: group identity, roles, presets, preview adapters and the generation model.
- #R660: the chain and its lifecycle.
- #PBZ4: the docked agent and the `/` commands.
- #F8R7: buffer safety.
- #9Y7X and #7WGJ: Qt PDF packaging and the install-viewer button.
- #C0Q8: the manifest and enablement.

This card owns only the TeX-specific wiring listed in the steps below.

**Steps.**
1. Protocol. Add `tex_build` (`workspace_id`, source revision) and `tex_sync` (forward/inverse) to protocol 36, both answered with `tex_status` or a location. Document them in `docs/AGENT-SESSIONS-PROTOCOL.md`.
2. Status and save. The pane that holds a `relay.tex` group applies each `tex_status` to the group output with `ws::applyBuildStatus`. The strip then shows building/live/stale/failed and the revision, and the PDF reloads in place, keeping its page and zoom. Saving any group source sends `tex_build`, and the builder debounces.
3. Diagnostics. Errors and warnings become clickable `file:line` rows (OutputLinks) that open the linked editor. A failed build keeps the last good PDF.
4. SyncTeX. The editor gets "Show in PDF" (forward), and Ctrl+click in `QPdfView` jumps to the linked editor line (inverse).
5. Included files. After a build, the `tex_dependencies` result is added to `group.sources`.
6. Remote, only if Q1 keeps it here. Build over the pane's SSH host with an SSH `Transport`, writing output to `~/.cache/relay/tex/<hash>` on the host. `PathMap` covers the SyncTeX paths, and the PDF, log and `.synctex.gz` arrive as one generation.
7. Test on a disposable fixture under Xvfb.

**Risks.**
- A project `latexmkrc` is Perl, which is why it stays `-norc` by default (Q2).
- `latexmk` can finish after newer edits. Generation ids and `seq` stop a stale completion from showing as live.
- SyncTeX paths differ between local and SSH source roots.
- `RelayWindowWorkspace.cpp` and `FilePanes.cpp` are shared with #E85D, #R660 and #PBZ4, so land through `land.py`.

**Verify.**
- Protocol/runtime tests in `tests/test_workspace_plugins.py` and `tests/test_tex_build.py`, plus an `artifactworkspace_test` case for `tex_status`.
- Under Xvfb with the fixture, capture: the first build, an edit → save → live, a broken edit → failed with the last good PDF, a diagnostic click, both SyncTeX directions, and restore. Evidence goes in `docs/qa_evidence/<date>-tex-workspace/`.
**2026-09-26 scope update.** Deliver the local source/build/diagnostics/PDF/SyncTeX flow. SSH builds are #CWDZ. Do not run a project's `latexmkrc` by default; a project must explicitly enable its executable plugin content. Review annotations are outside this card.

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
