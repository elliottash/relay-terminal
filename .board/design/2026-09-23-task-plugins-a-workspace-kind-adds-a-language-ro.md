---
id: MEPR
type: work
status: discussing
labels: [feature, design, panes, composer, agent-tools]
waiting_on: owner
rank: zzzzzzzzzzzzzzzzz
created: '2026-09-23'
source: Owner in a Relay pane, 2026-09-23
links: {plans: [], commits: [], evidence: [], related: [P2W8, MDA7, 2GV0], github: null}
---
# Task plugins: a workspace kind adds a language router, a runner, tools and a layout (first two: TeX-to-PDF, and a Python/Stata kernel IDE)

## Issue
can you also scope "plugins" for tasks? an "overleaf for pdfs" is one i can imagine first. an interactive prograaming or statistical IDE (a la ipython or provenance) is another, where its like relay, but instead of the bash command detection, you have detection of python / stata commands (for example).

## Discussion points
**Definition.** A *task plugin* is a workspace kind a tab can be switched into. Relay has one kind today: a Bash pane whose composer routes each line to the shell or to the agent (`router.classify`, `docs/ARCHITECTURE.md` section 5). A plugin declares the same things for another task, in one manifest:

1. **Router.** Replaces step 5 of `router.classify` (`bash -n` plus PATH resolution) with a language check: is this text runnable in *this* language? Python: `ast.parse`/`compile` plus the IPython conventions (`%magic`, `name?`, `!cmd`). Stata: a conservative first-word check against the command list with its abbreviations, `capture`/`quietly`/`by:` prefixes, `//` and `*` comments. R, Julia and SQL later. Steps 1–4 stay (control characters, `/shell` and `/agent`, the natural-language pattern), so "why is the coefficient negative" still reaches the agent. The mode chip reads `PY` or `STATA` instead of `SHELL`; `!` still forces the shell (IPython's `!ls` is Relay's `!` prefix already) and `*` still forces the agent. Route assist (protocol 11) is unchanged because it only sees text.
2. **Runner.** Where a runnable line goes and what "build" means. Two shapes: a *kernel* (a persistent process the worker owns, Jupyter wire protocol: ipykernel, stata_kernel or pystata, IRkernel, IJulia — one client, many languages) or an *artifact command* (latexmk, typst, quarto: source → output file plus diagnostics).
3. **Tools.** A deferred tool group (`tool_groups.GROUPS`, fetched through `load_tools` like the existing on-demand groups) offered to the pane's agent and bridged to guests through `guest_board_bridge.py`: `run_cell`, `interrupt`, `restart`, `get_variables`, `describe_dataframe`, `page_dataframe`, `save_figure` for a kernel; `build`, `diagnostics`, `synctex_lookup` for a document. The human's composer and the agent's `run_cell` share one kernel, so state is shared — the Provenance model (`~/.warp/skills/provenance/SKILL.md`: always run Python through `run_cell`, never a throwaway shell python).
4. **Layout and panes.** The linked pane group from #P2W8 with its roles filled: editor(s), console, preview(s), plus plugin-specific tool panes (a Variables pane over the kernel namespace; a click on a DataFrame opens #MDA7's native table).
5. **Skill.** Instructions the agent loads while the workspace is active: what the tools are for and house conventions ("state the standard-error clustering on every regression").
6. **Activation.** (a) File type: opening `.tex`/`.typ`/`.qmd` offers "Open as document workspace", or always, per setting. (b) Foreground program: the pane's foreground argv classifies as a REPL (`python`, `ipython`, `stata`, `R`, `julia`, `psql`) the way `GuestSpec.binaries` classifies claude/codex today, and the router switches to that language while it runs. (c) Explicit: a palette action, `/workspace tex`, or a per-project default under the project's `.relay/` so a paper repo opens as TeX and an analysis repo as Python.

**Plugin 1 — "Overleaf for PDFs" (document workspace).** Layouts 1:1:1 and 2:1 from #P2W8. Editor: the existing `FilePreview` text editor once #P2W8's shared-file safety lands. Runner: `latexmk -pdf -interaction=nonstopmode -synctex=1` on save, debounced, or `-pvc` watch; Typst (`typst watch`, sub-second) and Quarto/pandoc are the same runner with another command. Diagnostics: parse the `.log` (LaTeX Workshop's rules are the reference) into clickable `file:line` rows shown in the console pane as a ▸ fold like a tool call (`OutputLinks` resolves `file:line` already). Preview: `QPdfView` reloading in place, keeping page and scroll, with a chip saying live / building / stale / failed and the source revision it was built from. SyncTeX: Ctrl+click in the editor → `synctex view -i line:col:file -o pdf` → jump; click in the PDF → `synctex edit -o page:x:y:pdf` → `goToLine`. `\input`, `\include` and `.bib` files open into the same group. Remote: run on the host over the pane's ssh connection and fetch pdf + synctex.gz + log as one generation. Packaging: Qt PDF is off in the `.deb` and AUR builds, so the plugin must say so and open the PDF externally there.

**Plugin 2 — interactive programming / statistical IDE (kernel workspace).** The pane keeps its Bash shell underneath, so `!pip install` and `!git status` still work. The composer's auto mode routes Python to a kernel the worker starts (ipykernel over the Jupyter protocol; Stata through `stata_kernel` or Stata 17+'s `pystata`; R through IRkernel). The console pane prints each cell the way a tool call prints: input echo, stream output, an error fold, images and tables through #MDA7's inline layer; agent cells carry the ✦ intent line the way `type_into_program` does. Tools as in item 3. A Variables tool pane. The editor pane holds the script (`.py` with `# %%` cells; `.ipynb` later) and Ctrl+Enter there runs the cell in the same kernel. The pane's transcript is the history, so `/export` gives a notebook-like record.

**Phase 0, cheap and useful alone: a REPL-aware composer.** Today a line typed while `ipython` or `stata` runs in the pane is *queued*: prompt_toolkit and Stata's console put the tty in raw mode, and section 9's "send the line to the program" rule fires only in canonical mode, so the user has to Ctrl+H. Phase 0 makes the language router active whenever the foreground program is a known REPL, writes a runnable line to it (bracketed paste for multi-line), and sends natural language to the agent with the screen as context. No kernel and no new pane: router rules plus one change in `Pane::sendLineToProgram`, covered by the router tests.

**Others the same shape covers,** one manifest each, later: SQL (psql/duckdb → table preview), Mermaid/Graphviz/PlantUML (source → SVG through Qt Svg), OpenSCAD/CadQuery (source → PNG/STL through the CLI with an explicit Regenerate), Manim/matplotlib scripts (→ image or video), Marp/Beamer slides (→ PDF), notebooks (`.ipynb` as editor plus kernel).

**Packaging.** Built-in plugins first, under `backend/relay_core/plugins/<name>/` with a `plugin.yaml` manifest from day one (router, runner, tools, layout, skill, activation). Third-party plugins later through the same manifest, with tools supplied by an MCP server the worker launches; Relay has no MCP client today, so that is its own card, and Provenance's `provenance-kernel` server is the first candidate.

### Decisions to settle
1. Kernel or REPL-in-the-pty for the IDE plugin? Recommendation: Phase 0 REPL routing now; a kernel for the real plugin, because it gives shared state with the agent, structured output and interrupt.
2. Stata bridge: `stata_kernel` (Jupyter kernel driving a licensed Stata), `pystata` (Stata 17 and later only), or console `stata -q` in the pty? Which Stata is installed on the machines you work on?
3. First document runner: latexmk, Typst or Quarto? Recommendation: latexmk first, Typst second, because its compile speed makes the live preview shine.
4. Qt PDF in the `.deb` and AUR packages? Without it plugin 1 has no in-Relay preview (same question on #P2W8).
5. Third-party plugins in the first release (manifest plus MCP servers), or built-in only?
6. Does "Overleaf for PDFs" also mean review comments and annotations on the PDF, or only source, compile, preview and sync?

## Done means
- **Phase 0.** With `ipython`, `python`, `stata` or `R` in the pane's foreground, a typed statement runs in it without Ctrl+H, a multi-line block arrives intact, and a typed question goes to the agent with the screen as context. Router tests cover the Python and Stata checks.
- **Plugin 1.** Opening a `.tex` as a document workspace gives editor, console and PDF panes in either layout; a request typed in the console edits the `.tex` while the user is typing in it and neither loses edits (#P2W8); saving triggers a build; errors are clickable rows; the PDF refreshes in place with a live / building / stale / failed chip; SyncTeX works in both directions; the group closes and restores together; a build without Qt PDF says so and opens the PDF externally.
- **Plugin 2.** In a Python workspace the composer's auto mode runs Python in a kernel and prints the output inline, `!` runs the shell, natural language reaches the agent; the agent's `run_cell` shares that kernel and its cells are printed with an intent line; a Variables pane lists the namespace and opens a DataFrame as a table; the same for Stata through the chosen bridge.
- **Manifest.** Both plugins are described by a `plugin.yaml` the loader reads, so a third one needs no C++ change beyond a new preview type.
