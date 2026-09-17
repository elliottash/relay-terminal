# Portable terminal engine for macOS and Windows

- **Status**: open
- **Component**: gui
- **Milestone**: cross-platform
- **Workstream**: terminal
- **Acceptance evidence**: Relay runs on macOS and Windows with tabs, panes, composer, inline agent output and clickable paths
- **Assignee**: unassigned
- **Source**: owner, 2026-09-17: "relay having to work on mac and windows (not necessarily now)"

## Context

KonsolePart is Linux-first; its KDE runtime is experimental or missing on macOS and Windows
(`docs/CONTROL-AND-FILE-PANES-RESEARCH.md`). It also blocks screen text, alternate-screen
detection, OSC 8 links and click capture, which the delegate and clickable-path features need.

## Recommended direction

Own the emulator: libvterm (as Qt Creator 11+ does) with a Qt renderer, forkpty on Unix and
ConPTY on Windows; revisit libghostty-vt when its C API stabilizes. Keep KonsolePart on Linux
until parity. Needs a spike for rendering performance, fonts/ligatures, IME, selection and scrollback.
