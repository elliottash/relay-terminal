---
id: ADNM
type: work
status: done
labels: [bug, build]
implemented_by: glm/glm-5.3
verified_by: glm/glm-5.3
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-24'
source: 'Relay pane 804974a4, 2026-09-24, while landing #BGRN'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# main does not build: GlobalsPane::refreshVisible never landed (missing #MEMS half)

## Issue
Discovered while landing #BGRN: scripts/land.py's verify build of relay fails at tip because GlobalsPane::refreshVisible() is called but was never committed.
`scripts/land.py commit` for #BGRN materialised tip `347678b7397c` + its claimed paths into `/tmp/claude-1000/land/bgrn_active/verify/src` and the relay build failed:

```
/tmp/claude-1000/land/bgrn_active/verify/src/src/RelayWindow.h:3946:72:
  error: 'refreshVisible' is not a member of 'relay::globals::GlobalsPane'
```

- `git show main:src/RelayWindow.h` line 3946 calls `GlobalsPane::refreshVisible()` (landed with #MEMS `b1ab9a1e`, 2026-09-22).
- `git show main:src/GlobalsPane.h | grep -c refreshVisible` = 0; `git log --all -S 'static void refreshVisible' -- src/GlobalsPane.h` = no commits. The declaration/definition never landed.
- The complete half sits uncommitted in the working tree: `src/GlobalsPane.h` +3 (declaration, `#MEMS` comment), `src/GlobalsPane.cpp` +11 (`GlobalsPane::refreshVisible()` at line 248). Shared-tree builds pass because the tree carries it; any clean export of main fails.
- Session `c8sv` (Claude #C8SV) holds 6-minute-old snapshots of both files — likely working here now.

Fix is to land those two files as they sit. Blocking every C++ landing's verify build until then.

## Resolution
Fixed by #C8SV's `a4cd23f5`, which commits the call site and the `GlobalsPane::refreshVisible()` declaration/definition together. Confirmed from this session: the land.py verify build that failed at `347678b7397c` passed after `a4cd23f5` (`9a513210`'s gate built the exact tree, and `git show main:src/GlobalsPane.h` now carries the declaration). Thanks to the #C8SV session for landing it within minutes of the coordination note.
