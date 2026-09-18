---
id: XXP5
type: work
status: needs-qa-llm
component: [gui, packaging]
milestone: beta
workstream: terminal
assignee: implemented by Claude Opus 5 (Claude Code session), 2026-09-18
rank: hf
created: '2026-09-18'
acceptance: a non-Claude model QA session runs the checklist and records it under `docs/qa_evidence/`
source: 'owner, 2026-09-18: "relay engine is working great, so konsole is no longer needed"'
links: {plans: [], commits: [], evidence: [], related: [9VXF], github: null}
---
# Retire KonsolePart: Relay's own engine is the only terminal

## Behaviour as implemented

The engine became every pane's default on 2026-09-17. It is now the only backend.

- `src/KonsoleBackend.{h,cpp}` deleted. `src/BackendFactory.cpp` constructs `EngineBackend` and
  nothing else. `relay::TerminalBackend` and its capability bits stay: a pane still offers only
  what `capabilities()` reports, so a future backend can still do less.
- Gone with it: `--engine`, `RELAY_ENGINE`, `relay::EngineKind`, `parseEngineKind`,
  `resolveEngineKind`, `engineAvailable`, and the palette's "New pane (Relay engine)" /
  "New pane (Konsole engine)". `--engine-core=ghostty|libvterm` and `RELAY_ENGINE_CORE` stay.
- Session state no longer writes `"engine"`; a session saved before today still restores, the
  key is ignored, and `"engine_core"` is still honoured.
- Theme: no `.colorscheme` or `.profile` is generated any more, and Relay no longer rewrites
  `XDG_CONFIG_DIRS` / `XDG_DATA_DIRS` around each shell. `EngineBackend::applyThemeColors()`
  was already the live path. `data/theme/konsole/Relay.profile` became
  `data/theme/terminal.conf` (font, line spacing, margin, cursor; same keys), and
  `data/theme/relayrc` is gone.
- Build: KDE Frameworks Parts and CoreAddons are no longer required — only the optional
  KSyntaxHighlighting. Qt Network is now linked explicitly (it used to arrive through Parts).
  The `.deb` drops its `konsole-kpart` dependency, and so do both PKGBUILDs.
- Right-click menu, take-control, delegate and share messages no longer name KonsolePart; they
  say what this pane can or cannot do.

## Why

The owner's call: the engine has run every pane since 2026-09-17 and is faster than the part it
replaced (200 MB in 1.2 s against 3.7 s, `docs/ENGINE-PERF.md`). Keeping a second backend meant
keeping a KParts plugin, a private D-Bus hook, generated Konsole profiles and an XDG shuffle
alive for a path nobody used.

## Checks

- `ctest`: 24/24 (`relay-backends-tests`, `relay-theme-tests`, `relay-screen-tests`,
  `backend-and-bash` among them).
- `ldd build/relay` names no KF5 library but the optional highlighter.
- App starts, runs a shell, splits, restores a saved layout from before the change.

## Left for QA

- A saved window layout written **before** this change (with `"engine": "konsole"` in it)
  restoring cleanly.
- The `.deb` and AUR packages installing without `konsole-kpart` present.
- `packaging/smoke-installed.sh` on a machine with no KDE Frameworks at all.

## Follow-up

Qt 6 is now the only thing between Relay and dropping KF entirely. The `AUTO` probe still picks
Qt 5 wherever it exists, because nothing has been built against Qt 6 end to end; the first
blocker is `qsizetype` narrowing in `src/ScreenPrompt.cpp`.
