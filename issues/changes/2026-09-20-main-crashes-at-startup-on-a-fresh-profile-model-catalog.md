---
id: 561P
type: work
status: inbox
labels: [bug, models, gui]
component: [gui]
priority: 1
rank: zzzzzzzzzzzzzzzz
created: '2026-09-20'
source: 'Claude Code session on #7BM4, 2026-09-20, while building a clean export of main for human QA'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-switchboard-tooling-hub/startup-crash-a2204ba4.log], related: [7BM4], github: null}
---
# main crashes at startup on a fresh profile: heap corruption in the model catalog parser

## Issue
A clean export of `main` at `a2204ba4` (`git archive main | tar -x`, `cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo`, its own `backend/`), started with `--clean-shell --fresh` on an isolated profile (`RELAY_KEYRING=off`, fresh XDG dirs, no keys), aborts within a second of launch:

```
malloc(): unaligned fastbin chunk detected
gui_crash signal=6 name=SIGABRT … uptime_s=0
relay::models::catalogFrom(QJsonArray const&)+0xd7c   → src/ModelCatalog.cpp:162
Pane::modelCatalog() → Pane::currentEntryKey() → Pane::refreshPickers() → Pane::changed()
→ Pane::configurePreset(QString const&, bool, QString const&) → Pane::handle(QJsonObject const&)
```

The same launch of a clean export at `f72960e2` (earlier the same day) starts and runs. Between the two, the only commit touching `src/ModelCatalog.*` is `c2110e8f` ("an open-ended provider's long tail is hidden until checked"), which adds `Entry::openEnded` and `inAnyList()`; `src/Pane.h` changed in the same commit. Line 162 is `entry.openrouter = str(row, "openrouter")`, where malloc detects a heap already corrupted, so the write that corrupts it is earlier in that loop or in what `configurePreset` did before it.

Reproduce (no display needed beyond Xvfb):

```
X=$(mktemp -d /tmp/claude-1000/x.XXXX) && git archive main | tar -x -C $X && cmake -S $X -B $X/build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build $X/build -j16 --target relay
HOME=$(mktemp -d) RELAY_KEYRING=off xvfb-run -a $X/build/relay --workspace $PWD --clean-shell --fresh
```

It blocks human QA of anything on a fresh profile (#7BM4's walkthrough is waiting on it). A running Relay built before `c2110e8f` is unaffected until it is rebuilt.
