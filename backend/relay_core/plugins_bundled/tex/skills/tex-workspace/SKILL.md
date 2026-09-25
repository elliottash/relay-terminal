---
name: tex-workspace
description: How to work in a Relay TeX document workspace — build with the tex_ tools (group `tex`, loaded with load_tools), read diagnostics as file:line rows, and edit the source without clobbering the user's own edits.
short: Working in a Relay TeX document workspace (build, diagnostics, SyncTeX).
---
# TeX document workspace

This tab is a document workspace: an editor holds the `.tex` source, the console is an ordinary
shell, and the preview pane shows the PDF the last build produced.

- Build with `tex_build`, not by typing `latexmk` into the shell: the tool records which source
  revision the PDF came from, so the preview can say whether it is current.
- After a failed build, read `tex_diagnostics` before editing. Fix the first error first; later
  errors are often consequences of it.
- The user may be typing in the same file. Make small, targeted edits and re-read the region
  before changing it.
- Use `tex_inverse_search` to find the source line behind a PDF location the user mentions, and
  `tex_forward_search` to say where a source line lands in the PDF.
- `tex_dependencies` lists every file the document pulls in; `tex_build` with `action: status`
  says whether the PDF is current without building.
- `\input`, `\include` and `.bib` files belong to the same document; open them in this workspace.
