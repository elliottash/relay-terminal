---
id: ZR2M
type: work
status: done
labels: [bug, tests]
implemented_by: anthropic/claude-opus-5-5 via claude-code
verified_by: anthropic/claude-opus-5-5 via claude-code
resolution: done
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
links: {plans: [], commits: [], evidence: [qa_evidence/2026-09-25-265n-cleanup/audit-consolemode-build.txt], related: [], github: null}
---
# relay-consolemode-tests does not compile at HEAD: tests/headers use pane_waits.h helpers nothing includes

## Issue
At clean HEAD (tip of 2026-09-25 ~23:15 UTC, after 6bb47ddf), `relay-consolemode-tests` fails to
build: 44 compile errors, all "not declared in this scope" for `xcxdPump`, `waitForStopButton`,
`waitForBusyText`, `xcxdHasButton`, `waitUntil`, `waitForToast` and friends. Those helpers moved to
`tests/pane_waits.h` in #8ABD ("console-mode tests wait on pane state, not on timers"), and the
case headers comment on it — `tests/xcxd_ui_cases.h:52` even says "included first" — but
`tests/consolemode_test.cpp` never includes `pane_waits.h`, and neither do the case headers.
Found while delivering #265N (the `#include "pane_waits.h"` line rides in this card's own test
hunk, which is why the shared build/ never noticed: its binary predates the refactor and was never
relinked).

Measured: `git archive main | tar -x` into a clean dir, `cmake -S . -B b && cmake --build b
--target relay-consolemode-tests` → `gmake Error 2`, first errors at
`tests/xcxd_ui_cases.h:89 'waitForStopButton' was not declared in this scope` (44 error lines in
total). Full log under the evidence path. The one-line fix is
`#include "pane_waits.h"` after `}  // namespace cases` in `tests/consolemode_test.cpp`, before
the xcxd case headers; #265N's landing carries it, but the fault predates this card and any fresh
export of HEAD cannot build the target until it lands.
