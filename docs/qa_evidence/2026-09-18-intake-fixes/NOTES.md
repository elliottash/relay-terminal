# Five owner reports from the intake files, 2026-09-18

Implementer evidence, not a QA verdict. Everything here was driven under `Xvfb :190` with an
isolated `XDG_CONFIG_HOME`/`XDG_DATA_HOME`, in a throwaway workspace holding `README.md` (a
Markdown file with emphasis, a nested list, two links and a table), `notes.md` and `plain.txt`.
Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo`.

| Card | Report |
|---|---|
| #3W58 | "MD's arent printing markdown" |
| #VXTF | "in markdown, it should probably say 'source (MD)' rather than 'source' (ditto for rendered)" |
| #S1JP | "for a file preview pane, if you click on another link there, it should open a new pane" |
| #V9V1 | "need an open external button for all files … open internal at the top and open external second and open folder third" |
| #0EXJ | "add /light and /dark commands. light activates beige; dark activates copper" |

## #3W58 — Markdown not rendered

Both Markdown paths were exercised before anything was changed.

| Shot | What it shows |
|---|---|
| `3w58-already-fine-agent-markdown-in-the-terminal.png` | The agent (glm-5.3) asked for a heading, a bullet with `**bold**`/`*italic*`/`` `code` `` and a `>` quote; the terminal shows them styled with the markers gone. `src/MarkdownAnsi.cpp` was **not** the broken path. |
| `3w58-already-fine-md-preview-from-the-explorer.png` | `README.md` clicked in the explorer: rendered, with the view button offering "Source". Not the broken path either. |
| `3w58-before-md-line-link-shows-source-labelled-source.png` | Ctrl+click on `README.md:9` in `grep -rn` output: **raw Markdown**, while the button still offered "Source". This is the report. |
| `3w58-after-md-line-link-offers-rendered.png` | The same click after the fix: still the source (a line number is a position in the source, and line 9 is highlighted), but the button now offers "Rendered", so the render is one click away. |

## #VXTF — the view buttons name the format

| Shot | What it shows |
|---|---|
| `vxtf-before-source-unlabelled-and-under-the-pane-buttons.png` | The header before: "Source", and half of it under the pane's ⬓+ ◫+ ⇱ × row, which swallowed the click. |
| `vxtf-after-source-md-clear-of-the-pane-buttons.png` | "Source (MD)" on the rendered view, clear of the pane buttons; the explorer's folder line is inset too. |
| `vxtf-after-rendered-md.png` | After clicking it: the source, and the button now offers "Rendered (MD)". |
