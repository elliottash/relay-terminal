# LaTeX workflows for Relay, September 2026

## How do Overleaf editing and collaboration work?

### Takeaway
Overleaf presents a switchable rich-text Visual Editor and direct LaTeX Code Editor over a shared project, with real-time coediting and a separate review layer. Its Visual Editor is an authoring aid, not a general replacement for LaTeX source or the compiled PDF.

### Cited Findings
- The Code Editor edits LaTeX commands directly; the Visual Editor provides a Word-like rich-text interface, and users can switch between them. Compilation happens on Overleaf servers. — [Overleaf: How do I use Overleaf?](https://docs.overleaf.com/getting-started/how-do-i-use-overleaf)
- The redesigned editor has a file tree, Code/Visual toggle, source area, Recompile control, typeset PDF, logs, Review, and Chat controls. — [Overleaf: Redesigned editor](https://docs.overleaf.com/getting-started/how-do-i-use-overleaf/redesigned-overleaf-editor)
- Visual Editor can preview images and some table structures, but complex LaTeX tables, custom macros, and markup errors may leave a partial preview or expose LaTeX code; complex table formatting still requires source editing. — [Overleaf: Generating and inserting tables](https://docs.overleaf.com/writing-and-editing/generating-and-inserting-tables); [Overleaf: Pasting images](https://docs.overleaf.com/writing-and-editing/inserting-images/pasting-images-into-your-project)
- Overleaf says simultaneous editing uses operational transformation: edits are sent to the server every few seconds, rebased there, and changes are pushed to clients over WebSockets. — [Overleaf: Collaborating](https://docs.overleaf.com/collaborating/collaborating-in-overleaf)
- Comments attach to selected editor text or code lines, appear in a review sidebar, and support replies, resolution, reopening, editing, and deletion. — [Overleaf: Commenting](https://docs.overleaf.com/collaborating/commenting)
- Editors can switch between Editing and Reviewing; Reviewers can make tracked suggestions and comments but cannot directly edit or add files. Reviewing turns on Track Changes. — [Overleaf: Reviewing and reviewers](https://docs.overleaf.com/collaborating/reviewing-and-reviewers)
- Track Changes is premium and supports accepting or rejecting proposed edits. On a copied project, tracked changes are applied and cease to appear as tracked. A free-plan invited Reviewer can comment but cannot use tracked changes. — [Overleaf: Track changes](https://docs.overleaf.com/collaborating/track-changes); [Overleaf: Reviewing and reviewers](https://docs.overleaf.com/collaborating/reviewing-and-reviewers)
- Overleaf's plan table lists one named Editor/Reviewer on the free plan, six on Student, ten on Standard, and unlimited on Professional; viewers are unlimited. Project-level premium capabilities depend on the project owner's subscription. — [Overleaf: Premium features](https://docs.overleaf.com/getting-started/free-and-premium-plans/premium-features)

### Inferences
- A Relay workspace can deliver substantial value with source editing, terminal, and PDF preview without reproducing Overleaf's rich-text editing or collaboration protocol. A Visual Editor with reliable round trips through arbitrary LaTeX would be a separate, high-complexity product effort, given Overleaf's own documented partial rendering of complex constructs.
- If Relay later supports coediting, file-watcher synchronization alone will not provide Overleaf-style simultaneous cursor-level editing, review permissions, and anchored comments. These require shared document state, conflict handling, identity, and persistence in addition to a terminal pane.

### Gaps
- The public Overleaf help pages do not specify OT transform implementation, exact autosave latency guarantees, or the internal representation of comments and tracked changes. Do not infer API compatibility from the UI description.

## How does compilation and PDF source synchronization work?

### Takeaway
The core reproducible pipeline is project sources → configured TeX engine and build orchestrator → PDF plus logs and SyncTeX map. A live preview is a rebuild and viewer-refresh loop; forward and reverse jumps require matching artifacts from the same successful compile.

### Cited Findings
- Overleaf's Recompile runs the project's main `.tex` document. Auto compile runs every few seconds; Fast draft skips image processing; normal error handling may still produce a PDF; Stop on first error stops immediately. Temporary auxiliary files are reused unless the user recompiles from scratch. — [Overleaf: Recompiling your project](https://docs.overleaf.com/getting-started/recompiling-your-project)
- Overleaf uses Perl-based `latexmk`, with optional project `latexmkrc` custom rules. Multiple passes and bibliography processing are needed for references, citations, and similar generated data. — [Overleaf: The latexmkrc file](https://docs.overleaf.com/managing-projects-and-files/the-latexmkrc-file); [Overleaf: Fixing errors in generated files](https://docs.overleaf.com/troubleshooting-and-support/fixing-latex-errors/fixing-errors-in-generated-files)
- A project's main document and `latexmkrc` should be at the project root. Overleaf allows selection of TeX Live version and compiler; a version mismatch can explain local/Overleaf differences. — [Overleaf: Main document](https://docs.overleaf.com/getting-started/recompiling-your-project/the-main-document); [Overleaf: Selecting a TeX Live version and LaTeX compiler](https://docs.overleaf.com/getting-started/recompiling-your-project/selecting-a-tex-live-version-and-latex-compiler)
- Overleaf's PDF viewer and editor use generated `output.synctex.gz` for source-to-PDF and PDF-to-source jumps. Double-clicking PDF performs reverse search. The browser PDF viewer lacks these jumps and loses position on each compile. — [Overleaf: Moving between editor and PDF](https://docs.overleaf.com/navigating-in-the-editor/working-with-the-pdf-viewer/moving-between-the-editor-and-pdf); [Overleaf: PDF viewer options](https://docs.overleaf.com/navigating-in-the-editor/working-with-the-pdf-viewer/pdf-viewer-options-and-navigation)
- SyncTeX is line-granular rather than character-granular. Stale compiles, bibliographies generated through `.bbl`, multi-column layouts, a main file in a subfolder, and problematic paths can reduce or break mapping accuracy. — [Overleaf: Moving between editor and PDF](https://docs.overleaf.com/navigating-in-the-editor/working-with-the-pdf-viewer/moving-between-the-editor-and-pdf)
- Overleaf can place editor and PDF in separate browser tabs while retaining SyncTeX jumps. — [Overleaf: Editor and PDF layout](https://docs.overleaf.com/navigating-in-the-editor/working-with-the-pdf-viewer/editor-and-pdf-layout-and-sizing)
- As documented for current plans, Overleaf lists 10-second free and 240-second premium compile timeouts, 2,000 files, 7 MB editable material per project, and 2 MB per editable text file. It recommends under 100 MB for Git/GitHub-integrated projects. — [Overleaf: Plan limits](https://docs.overleaf.com/getting-started/free-and-premium-plans/plan-limits)

### Inferences
- Relay should track a compile generation consisting of the selected root file, engine, command/configuration, and source snapshot or modification times. Only publish a PDF and SyncTeX pair from the same completed generation; retain the last successful PDF when a build fails, while visibly marking it stale.
- Debounce editor and filesystem events, cancel or supersede queued builds, and keep compilation off the UI thread. The dependency graph includes `\input`/`\include`, bibliography, images, classes, and generated files, so watching only the open `.tex` file misses meaningful changes.
- Preserve page and zoom on refresh; distinguish a render refresh from a new successful compile. A source jump to a stale PDF should request or await compilation rather than imply exact mapping.

### Gaps
- Official Overleaf docs do not publish an end-to-end compile scheduling or cache invalidation algorithm. The generation model above is a Relay design proposal, not a claim about Overleaf internals.

## What do Overleaf Git and GitHub integrations permit?

### Takeaway
Overleaf's direct Git remote is a workable bridge to local editing, but neither Git nor GitHub sync transmits Overleaf's full collaboration semantics. GitHub sync is manual and has significant history, feature, and review-data limits.

### Cited Findings
- Premium Git integration exposes an Overleaf project as a Git remote that can be cloned, pulled, and pushed; it uses token-based authentication and is available on Overleaf Cloud and Server Pro 4.0+. — [Overleaf: Git integration](https://docs.overleaf.com/integrations-and-add-ons/git-integration-and-github-synchronization/git-integration)
- Overleaf Git integration permits only one linear branch, hard-coded as `master`. Overleaf History and Git history are different; Git commits are generated as a translation and may attribute a set of Overleaf-side changes to the most recent editor. — [Overleaf: Advanced Git operations](https://docs.overleaf.com/integrations-and-add-ons/git-integration-and-github-synchronization/git-integration/advanced-git-operations)
- Premium GitHub sync can create a new Overleaf project from an existing GitHub repository, or a new GitHub repository from an existing Overleaf project. It cannot connect an existing Overleaf project to an existing GitHub repository. — [Overleaf: GitHub synchronization](https://docs.overleaf.com/integrations-and-add-ons/git-integration-and-github-synchronization/github-synchronization)
- GitHub synchronization is not automatic: the user explicitly pulls or pushes through the integration. Overleaf writes a GitHub commit on push; conflicting concurrent changes can produce an Overleaf branch that the user must merge. — [Overleaf: GitHub synchronization](https://docs.overleaf.com/integrations-and-add-ons/git-integration-and-github-synchronization/github-synchronization)
- Overleaf GitHub sync supports github.com, not GitHub Enterprise or GitLab directly. The Overleaf Git system does not support branches, symlinks as symlinks, execute bits, Git LFS, or nested submodules; Overleaf recommends individual GitHub commits under 100 files and projects under 100 MB. — [Overleaf: GitHub synchronization](https://docs.overleaf.com/integrations-and-add-ons/git-integration-and-github-synchronization/github-synchronization)
- Overleaf warns that GitHub-to-Overleaf pulls can lose or displace track changes and comments, and advises against mixing active GitHub work with those review features. Overleaf-to-GitHub pushes bundle multiple collaborators' edits under the connected owner's GitHub identity, with no one-to-one commit mapping. — [Overleaf: GitHub synchronization](https://docs.overleaf.com/integrations-and-add-ons/git-integration-and-github-synchronization/github-synchronization)

### Inferences
- A Relay workspace should treat local Git history as its own record and offer Overleaf Git remote integration as an optional explicit sync workflow. Promising seamless round-trip preservation of Overleaf review data through Git would conflict with Overleaf's warning.
- GitHub and Overleaf can be two remotes of one local repository, but pushes and conflict resolution need to be deliberate, visible operations. File-change-driven automatic Git push would be a poor analogue of real-time coediting.

### Gaps
- These docs do not establish a supported public API for Relay to read/write Overleaf comments, tracked changes, collaborator cursors, or build results. That integration should not be assumed from Git access.

## What do local tools offer, and what architecture fits Relay?

### Takeaway
Existing local editors validate the source + external build tool + embedded PDF + SyncTeX pattern. For Relay, a local-first TeX build loop and PDF pane can reuse current file panes; full Overleaf-style collaboration is a distinct architecture.

### Cited Findings
- VS Code LaTeX Workshop auto-builds on dependency file changes or save, defaults to `latexmk`, allows configurable recipes, and passes `-synctex=1`, `-file-line-error`, and an output directory in its default `latexmk` tool. — [LaTeX Workshop: Compile](https://github.com/James-Yu/LaTeX-Workshop/wiki/Compile)
- LaTeX Workshop's internal viewer uses PDF.js, reloads project PDFs after successful builds, supports source-to-PDF and PDF-to-source SyncTeX, and can place the PDF in an editor group to the right. It also offers a browser viewer and experimental external viewers. — [LaTeX Workshop: View](https://github.com/James-Yu/LaTeX-Workshop/wiki/View)
- TeXstudio provides an internal PDF viewer beside the source, with forward/inverse SyncTeX when the compiler uses `-synctex=1`; its build/view commands and compiler are configurable. — [TeXstudio: Viewing a document](https://texstudio-org.github.io/viewing.html); [TeXstudio: Configuring](https://texstudio-org.github.io/configuration.html)
- `latexmk` chooses a needed sequence of TeX and related commands and offers continuous preview mode that watches source and included files. — [CTAN: latexmk](https://ctan.org/pkg/latexmk)
- Tectonic downloads needed package files from bundles, reruns TeX automatically until output stabilizes, and normally suppresses intermediate files. Its `-X compile` supports `--synctex`, `--outdir`, `--keep-intermediates`, `--only-cached`, and `--untrusted`; its `-X watch` rebuilds on input changes. — [Tectonic project](https://tectonic-typesetting.github.io/en-US/); [Tectonic: compile](https://tectonic-typesetting.github.io/book/latest/v2cli/compile.html); [Tectonic: watch](https://tectonic-typesetting.github.io/book/latest/v2cli/watch.html)
- Qt PDF provides `QPdfDocument` and a `QPdfView` widget with multi-page scrolling, fit modes, search, and navigation; PDF.js offers a display layer and embeddable viewer for web surfaces. — [Qt PDF](https://doc.qt.io/qt-6/qtpdf-index.html); [Qt QPdfView](https://doc.qt.io/qt-6/qpdfview.html); [PDF.js getting started](https://mozilla.github.io/pdf.js/getting_started/)
- Relay's current source already has text/file panes and conditional `QPdfDocument`/`QPdfView` PDF preview; its architecture documentation says Qt PDF is optional in builds and notes Qt5 support with Qt6 conditional support. — [Relay architecture](../../docs/ARCHITECTURE.md); [Relay file panes](../../src/FilePanes.cpp); [Relay build configuration](../../CMakeLists.txt)

### Inferences
- **Smallest coherent Relay path:** use the existing editable file pane for `.tex`/`.bib`, a terminal for tools and logs, and the existing PDF preview as a linked pane. Add project root selection, an explicit/default build command (`latexmk` first), source dependency watching, debounced rebuild, diagnostic parsing, and a stable PDF refresh. This preserves arbitrary LaTeX and supports offline work.
- **Build-tool choice:** `latexmk` with an installed TeX Live/MiKTeX environment maximizes compatibility with Overleaf's pipeline and arbitrary project `latexmkrc`; Tectonic offers simpler provisioning and self-contained behavior but has a different engine/bundle model and needs project-by-project compatibility validation. Expose the command and engine rather than hard-coding one path.
- **Viewer choice:** Qt PDF fits Relay's current native widget stack and existing implementation; PDF.js fits a web-based pane and has precedent in LaTeX Workshop, but introduces web rendering/bridge machinery. Either requires an additional SyncTeX integration layer for click-to-source and cursor-to-PDF navigation; a PDF widget alone does not provide that mapping.
- **Trust and locality:** TeX builds can read project inputs and may invoke external commands depending on configuration. Relay should run builds with an explicit workspace, visible command/log, and configurable permission boundary, especially for downloaded projects. Tectonic's `--untrusted` option is a possible control for its own engine, not a general sandbox for arbitrary build recipes.
- **Remote/SSH tradeoff:** if source resides on a Relay SSH host, run the TeX toolchain near that source and transfer the resulting PDF, SyncTeX, and diagnostics together. Mapping paths between remote source and local viewer is required. If the PDF is only copied to the client, edits and builds can race; tag outputs with a build generation and reject stale updates.
- **Collaboration tradeoff:** Overleaf itself can remain the coediting/review surface, while Relay works through its Git remote for local sessions. A native Relay coediting service would require its own document sync, presence, roles, comment anchors, tracked edits, history, and conflict semantics.

### Gaps
- No cited primary source establishes that Tectonic is fully compatible with every Overleaf/TeX Live project; the notes recommend a compatibility test rather than assuming equivalence.
- The cited Qt PDF API does not document SyncTeX parsing or source navigation. An implementation spike should verify a suitable SyncTeX library/CLI and path mapping on Relay's supported operating systems.
- Relay's packaging matrix must be checked before promising Qt PDF in every distribution: the existing architecture explicitly describes conditional availability. — [Relay architecture](../../docs/ARCHITECTURE.md)
