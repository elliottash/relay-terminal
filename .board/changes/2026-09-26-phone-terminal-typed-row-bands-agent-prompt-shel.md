---
id: 5YRN
type: work
status: executing
labels: [bug, remote, phone]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude:ashe-ethz-ch
session: 47171b5e-f82e-4bcd-a055-bf22f59d39e7
discovered_from: 8R3V
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-26'
verify: {artifact: visual, primary: script, also: [ai-visual], human: optional, criteria: 'A row the desktop marked MarkUserAgent / MarkUserShell / MarkPromptStart reaches the phone with its marks, through snapshots, diffs, scrolls and history pages, and the phone paints the same band the desktop''s default ''channel'' style does.', sign_off: none, effort: low, stakes: nuisance, blast: capability}
source: terminal pane 47171b5e, 2026-09-26
links: {plans: [], commits: [], evidence: [], related: [8R3V], github: null}
---
# Phone terminal: typed-row bands (agent prompt, shell command, prompt row) are missing

## Issue
On the desktop, rows the user typed wear a full-width band: violet for an agent prompt, cyan for a shell command, and a soft cyan tint on the shell's prompt row. The desktop paints them from line marks (`MarkUserAgent`, `MarkUserShell`, `MarkPromptStart`). `engine/tools/ScreenJson.h` `rowOf` already sends `marks`, but `remote/gui_host.py` rebuilds every row as `{row, segs}` and drops them, and `app/screen.js` would ignore them anyway. So the phone shows plain rows.

> also on phones, the agent prompt vs shell background colors aren't showing.
> — elliott · [session:126f58f09e8e410e9141b406f427a4e8](relay://session/126f58f09e8e410e9141b406f427a4e8) · 2026-09-26

## Done means
On the phone's terminal, a row you typed for the agent sits on a violet band with the agent's chip ink, a shell command you typed sits on a cyan band, and the shell's own prompt row has a soft cyan tint, matching the desktop's default "channel" band. This holds on the live screen and in scrollback pages, after the rows scroll up, and in the pane view's theme.

Failure is today's: those rows arrive as plain text on the terminal ground, because `marks` is lost in `remote/gui_host.py` and `wire.apply_scroll`, or `screen.js` ignores it.
