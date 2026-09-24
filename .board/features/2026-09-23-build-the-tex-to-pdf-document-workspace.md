---
id: WYGY
type: work
status: executing
labels: [feature, plugins, tex, pdf]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
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
**Goal.** Deliver the requested local Overleaf-like TeX/PDF workspace through Relay's reusable artifact/plugin layer.

**Findings.** `FilePreview` already has optional `QPdfView`; `OutputLinks` resolves `file:line`; Debian/AUR builds may omit Qt PDF. `latexmk` and SyncTeX are external tools and must be detected per workspace.

**Steps.** 1. Add a built-in TeX manifest and dependency check. 2. Bind source, console and PDF roles to #E85D's layouts. 3. Run debounced, cancellable `latexmk` builds against saved revisions and record generation ids. 4. Parse diagnostics and refresh the PDF without changing navigation; implement SyncTeX in both directions. 5. Add external-PDF fallback and SSH execution/transfer. 6. Exercise a disposable TeX fixture locally and remotely.

**Risks.** Shell escaping and untrusted project build rules require explicit command configuration/enablement. `latexmk` can continue after source changes; generation ids prevent stale completion from appearing live. SyncTeX paths differ between local and SSH source roots.

**Verify.** Runner/parser tests, document fixture under Xvfb, live screenshots of both layouts and a failed-build state; remote fixture when SSH is available.

## Tasks

- [ ] Add TeX manifest, dependency discovery and local fixture <!-- t:eq blocked_by=#C0Q8 -->
- [ ] Bind source/console/PDF roles and both layouts <!-- t:2g blocked_by=#E85D -->
- [ ] Implement revision-tagged latexmk builds, diagnostics and refresh <!-- t:qk blocked_by=eq,2g,#F8R7 -->
- [ ] Implement forward and inverse SyncTeX <!-- t:j2 blocked_by=qk -->
- [ ] Add optional Qt PDF fallback and SSH output generations <!-- t:en blocked_by=qk -->
- [ ] Verify local/remote end-to-end and restore behavior <!-- t:xq blocked_by=j2,en -->
