---
id: 5BAS
type: work
status: inbox
labels: [bug, build]
assignee: null
rank: m9
created: '2026-09-20'
source: 'Claude Code (#PF4K orchestrator), clean-export build of main 0beeadfc on sphinxpad, 2026-09-20'
links: {plans: [], commits: [], evidence: [], related: [0TJ9, 8YQ9, GMCF], github: null}
---
# main does not compile on Qt6 (Pane.h:1028), and boardmodel_test.cpp does not compile at all

## Issue
build it on sphinxpad as well to see how performs there

## Findings
A clean export of `main` at 0beeadfc (`git archive`, RelWithDebInfo, `-k 0`) on sphinxpad (Ubuntu 26.04, g++ 15; Qt 5.15 and Qt 6.10):

1. **The app itself fails on Qt6** — `src/Pane.h:1028`: `m_sessionTextMark = std::min(m_sessionTextMark, paneHistoryLines(kSessionTextScan).size());` → `no matching function for call to 'min(int&, qsizetype)'`. Qt6's `size()` is `qsizetype`; Qt5's is `int`, so the shared `build/` on spark (Qt5) and land.py's build gate cannot see it. Landed in fed72147 (#0TJ9). This is the 26.04 package's build: the `.deb` for Ubuntu 26.04 and Debian 13 is Qt6, so the next release build fails here. Fix: `std::min<qsizetype>` or `int(...)` at that line.
2. **`tests/boardmodel_test.cpp:3635` fails on both Qt5 and Qt6** — `view.onModelPick = [&picked](const QString &data) { picked = data; };` assigns a `void` lambda to `std::function<bool(const QString &)>` (`src/BoardPane.h:73`). Landed in 28b56483 (#8YQ9). `relay-board-tests` cannot build, so `ctest -R board` cannot run; land.py's gate builds only the `relay` target, which is why it passed. Fix: `return true;` (or whatever the box's contract is — the comment at `BoardPane.h:464` says the return value means "accepted").

The Ubuntu 26.04 CI leg landed today (dc091a86) fails on both of these on the next push. Not fixed here because both files belong to live sessions (`0tj9`, `picker-gui`/`helperpicker` hold `src/Pane.h`; the #8YQ9 test is that card's); this card is the notice, and a comment is on each card's thread.
