# Folder explorer and file preview panes (plain Qt)

- **Status**: needs-qa-llm
- **Component**: gui
- **Milestone**: desktop-alpha
- **Workstream**: terminal
- **Acceptance evidence**: a non-Claude model QA session runs the checklist against the widgets inside
  Relay's windows and records it under `docs/qa_evidence/`
- **Assignee**: widgets implemented by Claude Opus 5 (Claude Code subagent), 2026-09-17; integration
  into windows, tabs and panes is done by the main session
- **Source**: `issues/feature_intake.txt`, "clicking a directory or the terminal working directory opens
  a dolphin-like file explorer pane with the folder" and "clicking a previewable file opens it in a pane";
  owner decision (2026-09-17) to use plain Qt rather than KDE parts, for macOS and Windows later
  (`docs/CONTROL-AND-FILE-PANES-RESEARCH.md`, option 2)

## Behavior as implemented

- `relay::FileExplorer`: folder list with folders first, Up button, hidden-file toggle, type-to-filter;
  Enter or double-click opens; Backspace or Alt+Up goes up; typing in the list starts the filter.
- `relay::FilePreview`: text and code with KSyntaxHighlighting (Breeze Dark), Markdown rendered or
  source, images fit or 100%, PDF when Qt PDF is available, otherwise a file-info panel with Open
  externally. Reload and Open externally in the header. 2 MiB text cap with a notice; 64 MiB image cap.
- Details: `docs/ARCHITECTURE.md`, "File and preview panes".

Optional dependencies on this machine: KSyntaxHighlighting enabled (`libkf5syntaxhighlighting-dev`);
Qt PDF not available for Qt5 here (it ships with QtWebEngine), so PDFs show the info panel.

## Implementer check (not a QA verdict)

`relay-filepanes-tests`: 10 passed (navigation, filter, hidden toggle, Enter opens, type-ahead,
viewer choice for .txt/.py/.md/.png/binary, truncation notice, missing path). A standalone harness
under Xvfb rendered highlighted C++, rendered Markdown and a fitted PNG in the dark theme:
`docs/qa_evidence/2026-09-17-file-panes/implementer-explorer-and-previews.png`.

## QA checklist

1. Open the explorer on a large folder (e.g. `/usr/bin`): listing stays responsive; typing filters.
2. Keyboard only: filter, Down, Enter into a folder, Backspace back, Enter on a file opens a preview.
3. Previews: `.py`, `.cpp`, `.json`, `.md` (toggle Source), `.png`/`.jpg`/`.svg` (toggle 100%), a PDF,
   a binary; a >2 MiB log shows the notice; a >64 MiB image is refused.
4. Hidden-file toggle shows dotfiles; long paths and file names elide instead of widening the pane.
5. Reload picks up an edited file; Open externally uses the desktop default application.
6. Inside Relay (after integration): clicking the terminal directory opens the explorer pane in that
   folder, and opening a file from it opens a preview pane.

## Integration (2026-09-17)

Wired into windows by Claude Opus 5: `ToolPane` wraps the explorer or preview and lives in the
same splitter layout as terminal panes (split, close, restore, Alt+arrow navigation, tab titles,
saved as `{"explorer": {"path"}}` / `{"preview": {"path"}}`). An existing explorer or preview in
the tab is reused. Entry points: the pane's directory line, `relay open PATH`, Actions › Open
folder in explorer / Open file…, and Ctrl+click on text files in terminal output.
`FilePreview::goToLine` jumps to `file:LINE`. Fixed during integration: the explorer filter did
not hide folders (QFileSystemModel name filters apply to files only), so Enter opened a folder;
covered by `typedFilterThenDownEnterOpensMatchNotFolder`. Implementer evidence:
`docs/qa_evidence/2026-09-17-file-panes/implementer-integrated-panes.png`.
