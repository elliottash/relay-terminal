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

## #S1JP — a link inside a preview opens a new pane

| Shot | What it shows |
|---|---|
| `s1jp-before-link-replaced-the-file-header-still-says-readme.png` | `notes.md` clicked inside the `README.md` preview: the QTextBrowser loaded it in place, and the header still reads "README.md". `FilePreview::open()` never ran, so Reload and ↗ still pointed at the old file. |
| `s1jp-after-link-opens-its-own-pane-beside-the-original.png` | The same click after the fix: `notes.md` in its own pane beside the README preview, which still holds its file, rendered. The tab reads "w2; README.md; notes.md · 4". |
| `s1jp-after-back-link-focuses-the-pane-already-open.png` | The "README" link inside `notes.md`: focus moves to the pane already showing it. Still four panes — clicking back and forth does not pile them up. |
| `s1jp-after-a-txt-link-gets-the-text-viewer.png` | A link to `plain.txt`: its own pane with the monospace text viewer, not plain text dumped into the Markdown view. |
