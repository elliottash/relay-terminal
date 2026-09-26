---
id: EPMD
type: work
status: inbox
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-26'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Background panes button menu shows an extra "Stop …" row that should be removed

## Issue
The background-panes count button in the window chrome's top-right (the `backgroundCount-<state>` buttons built in `RelayWindow::buildWindowChrome`, src/RelayWindowCore.cpp ~1540) opens a menu with one focus row per background pane plus a "Stop <title>" row per pane. The user wants the extra "stop ..." row removed from that menu.

> "add a card -- in the background panes button at the top right, when you click on it, it has an extra "stop ..." row at the bottom that should be removed" — Elliott, pane session
> — elliott · [session:22af58c917144a3089850df9ef29ee3a](relay://session/22af58c917144a3089850df9ef29ee3a) · 2026-09-26
