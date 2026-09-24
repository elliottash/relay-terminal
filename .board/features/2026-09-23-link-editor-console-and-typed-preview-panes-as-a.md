---
id: E85D
type: work
status: executing
labels: [feature, panes, artifacts, preview]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: Owner in a Relay pane, 2026-09-23; implementation slice from reports/Editable workspaces for Relay.md
links: {plans: [], commits: [], evidence: [], related: [P2W8, F8R7, MEPR], github: null}
---
# Link editor, console and typed preview panes as an artifact workspace

## Issue
then lets go to your reports and write detailed cards for the editable artifacts and plugins features

## Discussion points
**Scope from #P2W8 and the editable-workspace report.** A workspace is a persisted relationship among a project root, source files, an editor, a terminal/agent console, output definitions and previews. Pane links must survive ordinary split/move/close/restore behavior, not require a new windowing system. A typed preview adapter identifies the artifact it renders, its source revision/build generation and state (`live`, `building`, `stale`, `failed`). The first adapters are existing Markdown/image/PDF views; later diagram, plot/table and CAD adapters can use the same contract. A generated output is read-only unless its plugin explicitly declares a reversible editing model. Dependency: #F8R7 for concurrent edits; the TeX plugin fills these roles in its own card.

## Done means
- A tab can form a workspace group with a project root, editor pane, full terminal/agent pane and typed preview pane; navigation from build diagnostics or preview returns to the linked source editor.
- Presets produce the requested terminal | TeX | PDF 1:1:1 layout and TeX over terminal on the left with PDF on the right; split sizes, pane roles, files and group identity restore after restart.
- A preview shows its output file, source revision/build generation and `live`, `building`, `stale` or `failed` state; stale output remains visible with an explicit stale indicator until a successful replacement arrives.
- Additional source/output pairs (Markdown, diagrams, scripts, artwork or CAD) can register a preview adapter without changing pane-group persistence; adapters declare whether generated output or a visual editor is authoritative.
- Layout/restore and generation-state tests pass, and a live UI run proves both layout presets and stale-output behavior.

## Plan
**Goal.** Provide the reusable pane and preview layer for multiple artifact types.

**Findings.** `RelayWindow::openPath` already opens and reuses file panes; tab layouts restore, while `FilePreview` chooses viewers by MIME. Neither records a source→output relationship or a shared workspace identity.

**Steps.** 1. Define a versioned workspace-group model with root, roles and saved pane membership. 2. Add two layout presets and preserve manual resizing/rearrangement on restore. 3. Define a typed artifact/preview adapter API with authority, source revision and build-generation fields. 4. Connect editor, console and preview navigation; mark stale/failed generations without discarding the last good output. 5. Add initial adapters for current Markdown/image/PDF views and tests.

**Risks.** Pane closure and moving across tabs must not leave broken references. Optional Qt PDF builds need an external-viewer fallback. A generated preview should never imply its contents match newer unsaved source.

**Verify.** Model/serialization tests plus live Xvfb screenshots for each preset and a stale preview.

## Tasks

- [ ] Define and persist workspace-group identity, roles and project root <!-- t:tj -->
- [ ] Implement 1:1:1 and 2:1 layout presets with restore <!-- t:k6 blocked_by=tj -->
- [ ] Add typed preview adapters and generation/authority metadata <!-- t:2z blocked_by=tj -->
- [ ] Link diagnostics/preview navigation to the source editor <!-- t:0x blocked_by=k6,2z -->
- [ ] Verify layout, stale output and optional PDF fallback live <!-- t:hn blocked_by=k6,2z,0x -->
