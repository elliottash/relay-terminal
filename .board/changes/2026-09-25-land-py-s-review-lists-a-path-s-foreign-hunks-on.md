---
id: T2EE
type: work
status: inbox
labels: [bug, land, workflow]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
source: pane p47, 2026-09-25
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# land.py's review lists a path's foreign hunks only when the path also has contested ones; a path whose every hunk is foreign is dropped with one generic line

## Issue
Landing the working tree on 2026-09-25, `src/SubagentTranscript.h` and `src/SubagentTranscript.cpp` each held exactly one hunk, and both were FOREIGN (pane 25390d50's `openModelBox`). The full review listed the foreign hunks of `src/Pane.h`, `src/RelayWindow.h` and `src/RelayWindowCore.cpp` with a `--take-foreign path:n` suggestion each, but named these two paths only in the claim warnings, so the digest said nothing about them. The commit then failed the build gate with `src/Pane.h:2621: 'class relay::SubagentTabsView' has no member named 'openModelBox'` — landing one half of somebody else's change. Running the same commit with `--paths src/SubagentTranscript.h src/SubagentTranscript.cpp` printed the generic `nothing to land: every claimed path already matches the tip once the hunks you left out are taken off`, and only `--take-foreign src/SubagentTranscript.h:1 --take-foreign src/SubagentTranscript.cpp:1` made it print the hunks and name the pane. A session with a foreign-only path therefore has no line to read: it is told hunks were "left out" as if by its own selection.
