---
id: YJK8
type: work
status: ready
component: [gui]
milestone: cross-platform
workstream: terminal
rank: 5t
created: '2026-09-17'
acceptance: Relay runs on macOS and Windows with tabs, panes, composer, inline agent output and clickable paths
source: 'owner, 2026-09-17: "relay having to work on mac and windows (not necessarily now)"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Portable terminal engine for macOS and Windows

## Context

KonsolePart is Linux-first; its KDE runtime is experimental or missing on macOS and Windows
(`docs/CONTROL-AND-FILE-PANES-RESEARCH.md`). It also blocks screen text, alternate-screen
detection, OSC 8 links and click capture, which the delegate and clickable-path features need.

## Progress

2026-09-17: the engine exists (`engine/`, [docs/ENGINE.md](../../docs/ENGINE.md)) and is wired
into the app behind a per-pane flag (`--engine=relay`, `RELAY_ENGINE`, palette "New pane (Relay
engine)"); KonsolePart is still the default on Linux. Details and the remaining gaps:
`issues/features/needs_qa_llm/2026-09-17-engine-integration.md`. This issue stays open for the
macOS and Windows targets (ConPTY, Mach-O symbol localization, notarization, AltGr, DirectWrite).

## Recommended direction

Own the emulator: libvterm (as Qt Creator 11+ does) with a Qt renderer, forkpty on Unix and
ConPTY on Windows; revisit libghostty-vt when its C API stabilizes. Keep KonsolePart on Linux
until parity. Needs a spike for rendering performance, fonts/ligatures, IME, selection and scrollback.
