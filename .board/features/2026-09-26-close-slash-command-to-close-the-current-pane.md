---
id: 9E4G
type: work
status: executing
labels: [feature]
assignee: agent
implemented_by: kimi/k3
session: c77029c3-2cf9-4bb3-ab5c-3abb27108b7f
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-26'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: low}
source: pane session c77029c3, 2026-09-26
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# /close slash command to close the current pane

## Issue
Add a `/close` slash command that closes the current pane, the same action as the `pane.close` keybinding (Ctrl+W). Currently no slash command closes a pane.

> does relay have a / command to close a pane — yess add that
> — elliott · [session:f4606bc78b84466db80339a6833c88a8](relay://session/f4606bc78b84466db80339a6833c88a8) · 2026-09-26

## Done means
Typing `/close` in a pane's prompt closes that pane, exactly as the `pane.close` keybinding (Ctrl+W) does — the pane is removed from its tab, and when it was the tab's last pane the tab/window cascade follows the same path as the keybinding. The command appears in the slash popup and `/help`. Failure looks like: `/close` reported as an unknown command, or the pane staying open.

## Execution Summary
`/close` is a Relay built-in slash command. Typing it in a terminal or agent pane runs the same `closePane` path as the right-click menu's Close and the `pane.close` keybinding (Ctrl+W), via the pane's existing `onWindowAction("close")` callback, so the tab/window cascade is identical. In an agent console (not a leaf, no `onWindowAction`) it prints a status line instead of failing silently. The Options action map teaches `pane.close` → `/close`, and the slash test registry carries the name in Pane's order.

Commits: `99ebb28b` (the command), `227d6576` (this evidence), submitted as publish jobs `f9e9758c4ac54ae8` and `028ecd816073ef65`.

## Tests
- `ctest --test-dir build -R slash` — the slash-command suite (registry resolution, popup offering, unknown-command line).
- `ctest --test-dir build -R "settings$|actionpalette"` — the Options action-map and palette tests around `actionSlashCommands`.
- Build check: `scripts/relay-build --check "Close this pane"` proves the shipped binary holds the new command.
