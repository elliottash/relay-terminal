# Clickable file and folder paths open Relay panes

- **Status**: open
- **Component**: gui, shell-integration
- **Milestone**: desktop-alpha
- **Workstream**: terminal
- **Acceptance evidence**: clicking a directory path in terminal output opens the explorer pane; clicking a file opens the preview pane
- **Assignee**: unassigned
- **Source**: `issues/feature_intake.txt`, "parse all folders and filenames and highlight them", "clicking a directory or the terminal working directory opens a dolphin-like file explorer pane", "clicking a previewable file opens it in a pane"

## Findings (2026-09-17)

Konsole 23.08.5 source, `src/filterHotSpots/FileFilterHotspot.cpp`: clicking a file hotspot opens
directories and non-text files with `KIO::OpenUrlJob` (the system default app). Text files go to
the profile's `TextEditorCmd` when set. KonsolePart emits no click signal and exposes no screen text.

## Options

1. **Fork-free, now:** `relay open PATH` shell command, clicking the pane's directory label, the
   actions palette, and the profile text-editor command pointing at a Relay helper (text files only).
2. **Konsole fork / patched KonsolePart:** patch `FileFilterHotSpot::activate` to hand every click to
   the host; also expose screen text and alternate-screen state. Full coverage on Linux; cost is
   building and shipping Konsole and rebasing the patch.
3. **Relay-owned terminal engine** (libvterm + ConPTY): full control on every platform; see
   `issues/features/2026-09-17-portable-terminal-engine.md`.

Option 1 is being implemented with the plain-Qt file panes.
