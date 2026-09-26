---
id: E85D
type: work
status: planned
labels: [feature, panes, artifacts, preview]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
blocked_by: [SJ00]
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
**Goal.** Provide the reusable pane and preview layer for multiple artifact types. *(Refreshed 2026-09-26: the layer is on main. What is left is one authority defect, one owner decision on multi-file groups, and the live verification.)*

**Findings (2026-09-26, against main at 6514be0c).** The thread's 6c4b363b was rewritten; on main it is `4ad5fe87`.
- *Group model, presets, adapters, states* — `4ad5fe87`: `src/ArtifactWorkspace.{h,cpp}` (group id, root, roles Editor/Console/Preview/Variables, `Authority`, `OutputState` Idle/Building/Live/Stale/Failed, generations with source revision, `AdapterRegistry` with markdown/image/pdf/text and per-plugin adapters), `src/RelayWindowWorkspace.cpp` (members saved in layout nodes, the preview's status strip that marks stale/failed output on the whole strip, `applyWorkspacePreset` for `1:1:1` and `2:1`, `openInWorkspaceEditor` for `file:line` links), palette items and the "Files and projects" category (`src/RelayWindow.cpp:559-564`, `:718`), `tests/artifactworkspace_test.cpp`. The Console role is a full shell `Pane` (decision D4).
- *Chains on top* — #R660's ordered members / upstream / head, chain chip, open-the-next, close/move/restore and placeholders landed inside the salvage commit `def2cf0b` (2026-09-25, +431 lines in `RelayWindowWorkspace.cpp`; schema 2 with an upgrade from 1). Live evidence is in `docs/qa_evidence/2026-09-25-tex-chains/` (`b52a33ad`): the 1:1:1 chain, generations 1 → 2 after an edit, restore after restart, close-the-rest dialog. #R660's own card still shows 0/5, which that card's owner needs to sync.
- *Tests* — `relay-artifactworkspace-tests` 28/28 on 2026-09-26. That binary is from 2026-09-25 16:29, before `def2cf0b`, so the verifier rebuilds it.
- **Defect: a PDF output is recorded as editable text.** `openWorkspaceChainSource` registers the output with the *source's* adapter: `adapterFor(absolute, …)` then `ensureOutput(defaultOutputFor(absolute), adapter->id, adapter->authority)` (`src/RelayWindowWorkspace.cpp:714-715`, from `def2cf0b`). `main.pdf` is therefore saved as `adapter: text, authority: editable` (`docs/qa_evidence/2026-09-25-tex-chains/06-workspace-state.json`). That breaks the Discussion's rule that "a generated output is read-only unless its plugin explicitly declares a reversible editing model", and it hides the pdf adapter's "no PDF viewer (Qt PDF)" reason from the strip. In that evidence the Preview pane is simply blank (`05-edited-and-rebuilt-generation-2.png`). `applyWorkspacePreset` does it right (`:401-403`). Related: `AdapterRegistry::adapterFor` falls back to the editable `text` adapter (`src/ArtifactWorkspace.cpp:478-481`), which suits the editor checks at `:371` and `:925` but not an output of unknown type.
- **Changed by #10RD (`ef296c8d`).** A `file:line` in a source *other* than the linked editor's file no longer replaces that editor's file. It opens beside the editor through `openPath`, as an ordinary pane that is **not** a group member (`src/RelayWindowWorkspace.cpp:928-933`). Groups still hold one member per role (`aRoleHasOneMember`). So in a multi-file project (`\input` chapters, #WYGY's Done means) a diagnostic in another file lands in an unlinked pane. That is owner question 1 in the thread.
- *Handed elsewhere, not this card:* preview → source jumps are inverse SyncTeX (#WYGY t:j2). Installing Qt PDF in packaged builds is decision D3, done by #9Y7X (executing). The "Open externally / Install viewer" buttons are #7WGJ. Manifest-declared `preview.adapter` (`backend/relay_core/task_plugins.py:758-768`) is validated but nothing in `src/` registers a plugin adapter from it yet. The C++ API is there (`AdapterRegistry::add`, test `aPluginAdapterWinsInItsOwnGroups`), and the first plugin that needs one (tables/plots) wires it in #33G0 t:9h.

**Steps.** 1. Register a group's output with the adapter resolved from the *output* path, with `Generated` authority when nothing matches, in `openWorkspaceChainSource` and any other `ensureOutput` caller. Add a model test that a `.tex` chain's `main.pdf` output is `pdf`/`generated`, and one that an unavailable pdf adapter's reason reaches the strip. 2. After the owner's answer to question 1, either let a group hold several editor members (relax one-member-per-role for Editor only, keep order, save/restore, a click on another group source opens or focuses its linked member), or record that other sources stay unlinked and narrow the Done-means wording with the owner. 3. Live verification under Xvfb with an isolated profile: the 2:1 preset (1:1:1 is already in the tex-chains evidence), a stale preview after a source save followed by its replacement, and the PDF-unavailable strip in a build without Qt PDF. Use an image output (a script writing `plot.png`) now, and add the PDF shot when #9Y7X lands. Land, record `## Tests`, move to `needs-verification`.

**Risks.** `src/RelayWindowWorkspace.cpp` is shared with #R660, and #2FQ9 is live in `FilePanes`/`RelayWindow.h`, so claim through `land.py` with `--dry-run`. Relaxing one-member-per-role touches restore and the chain order, so keep schema 2 readable.

**Verify.** `ctest -R '^artifactworkspace$'` after a rebuild, plus the live run above into `docs/qa_evidence/<date>-verify-E85D/`. Proposed `verify` block: `{artifact: visual, primary: script, also: [ai-visual], human: optional, criteria: "both presets lay out and restore; a stale preview is marked stale until a new build replaces it", effort: medium}`.
**2026-09-26 scope update.** A secondary source named by diagnostics or a click becomes another linked Editor member. The PDF output adapter fault is #SJ00; its submitted fix must publish before the chain receives fresh verification.

## Tasks

- [x] Define and persist workspace-group identity, roles and project root — `4ad5fe87` (schema 2 chain upgrade in `def2cf0b`) <!-- t:tj -->
- [x] Implement 1:1:1 and 2:1 layout presets with restore — `4ad5fe87`; live 1:1:1 restore in `docs/qa_evidence/2026-09-25-tex-chains/07` <!-- t:k6 blocked_by=tj -->
- [x] Add typed preview adapters and generation/authority metadata — `4ad5fe87` <!-- t:2z blocked_by=tj -->
- [x] Link diagnostics/preview navigation to the source editor — `4ad5fe87` for the linked editor's own file, narrowed by #10RD `ef296c8d`; preview → source is #WYGY t:j2 <!-- t:0x blocked_by=k6,2z -->
- [ ] Register outputs by their own path and adapter; unknown outputs are generated/read-only (fix `src/RelayWindowWorkspace.cpp:714-715`) <!-- t:q7 blocked_by=2z -->
- [ ] Other group sources (`\input` files) open as linked editor members, or stay unlinked, as the owner decides (question 1) <!-- t:m3 s=blocked blocked_by=0x -->
- [ ] Verify layout, stale output and optional PDF fallback live — 2:1 preset, stale preview, PDF-unavailable strip <!-- t:hn blocked_by=k6,2z,0x,q7,m3 -->

## Decisions
- 2026-09-26 — Owner: “yes to all.” A diagnostic or click naming another source file opens that file as another linked editor in the same group; each file remains in its own pane.
