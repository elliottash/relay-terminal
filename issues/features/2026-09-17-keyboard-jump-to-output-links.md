---
id: GWXM
type: work
status: ready
component: [gui]
milestone: desktop-alpha
workstream: terminal
rank: 4n
created: '2026-09-17'
acceptance: after `ls`, `grep -n` or a compiler error, Alt+PageUp / Alt+PageDown (or the chosen binding) highlights the previous / next path or URL in the pane, scrolling scrollback as needed; Enter opens it with the same routing as a click (explorer, preview, browser); Esc cancels
source: '`issues/feature_intake.txt`, "add a shortcut to scroll through files / folders / links in the output, maybe alt page up / alt page down."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Keyboard shortcut to step through files, folders and links in output

## Dependency

Needs the pane's screen and scrollback text plus a way to highlight a range. KonsolePart 23.08
exposes neither. Options, detailed in `docs/NEXT-STEPS-RESEARCH.md` section A:

- Konsole 24.02+ (KF6) exports `Session::getAllDisplayedText` / `getDisplayedText` over D-Bus
  (displayed lines only, no highlight or scrollback walk).
- A patched KonsolePart, or a PTY proxy with a libvterm shadow screen.
- The Relay-owned engine.

Related: `issues/features/2026-09-17-clickable-paths.md` (click routing to Relay panes) and
`issues/features/2026-09-17-portable-terminal-engine.md`. Check the binding against
`docs/KEYBINDING-PRESETS.md` (Alt+PageUp may conflict with preset scroll bindings).
