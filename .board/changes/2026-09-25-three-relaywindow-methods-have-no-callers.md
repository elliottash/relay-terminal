---
id: ZR3N
type: work
status: inbox
labels: [bug, ui]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
links: {plans: [], commits: [], evidence: [qa_evidence/2026-09-25-265n-cleanup/audit-relaywindow-methods.txt], related: [], github: null}
---
# Three RelayWindow methods have no callers anywhere (dead code)

## Issue
Found by the #265N report-only audit: `closedItemForSession`, `openConsoleModelPicker` and
`serializeTabs` are declared (and inline-defined) in `src/RelayWindow.h` and their names appear
nowhere else in `src/` or `tests/` — no call sites, no `&RelayWindow::` connections, no
string-dispatched command ids (`grep -rn` over src+tests excluding the header returns nothing for
each; `serializeTabs` is the plural sibling of the live `serializeTab`, both inline at
RelayWindow.h:585/…, and only the singular is ever invoked). `openConsoleModelPicker` sits next to
the live `openConsoleModelBox` (RelayWindow.h:1330) and looks like a leftover of the same picker
work.

Suggested disposition: delete the three (or wire `openConsoleModelPicker` up if it was meant to
replace the box). Reached from the same audit run as bug ZR2M (consolemode target does not
compile at HEAD); method and caveats are in the evidence file.
