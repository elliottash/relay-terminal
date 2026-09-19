---
id: J5DN
type: work
status: ready
labels: [bug, engine]
component: [engine]
workstream: terminal
assignee: agent
rank: zzzzzzu
created: '2026-09-19'
acceptance: a pane on GhosttyCore shows the same band behind a line the user typed as a libvterm pane, built and seen on a machine where that core compiles
source: 'noted while landing #f47e71c, 2026-09-19'
links: {plans: [], commits: [f47e71c], evidence: [], related: [K3RT], github: null}
---
# GhosttyCore does not parse the row-role OSC, so the band is missing under it

## Issue

`f47e71c` marks a line the user typed with the private `OSC 7772;shell|agent` and keeps it in the
line's `relay_marks`, which `TerminalView` paints as a band. Only the vendored libvterm fork
implements it: `LibVtermCore` lets 7772 through its OSC allowlist and calls
`vterm_state_relay_mark_cursor_line()`, and the fork widened `relay_marks` from 4 bits to 8
(`vterm.h`, `state.c`). `GhosttyCore` has the same `Line::marks` field and the same OSC 133
handling, but nothing for 7772.

Under Ghostty the line is bold in the default foreground and unbanded. Nothing else breaks: an
unknown OSC is ignored.

## Why it is not done

GhosttyCore does not compile on this machine (`RELAY_ENGINE_WITH_GHOSTTY=OFF`; see the owner's
memory note "Engine cores on this machine"), so the change could not be built or seen. It needs a
session on a machine where that core builds.

## Approach

Mirror `LibVtermCore`: accept OSC 7772, map `shell` / `agent` to `MarkUserShell` / `MarkUserAgent`,
OR the bit into the cursor line's marks, and make sure all 8 bits survive the trip to history and
back (the libvterm side needed a mask widened in two places). `CoreTest::theRowRoleOscMarksItsLine`
is core-agnostic and will cover it once the core is in the test's core list.
