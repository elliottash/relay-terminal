---
id: 9ESY
type: work
status: needs-verification
labels: [bug, sessions, tests]
assignee: agent
implemented_by: glm/glm-5.3
session: c0052f54-4678-4ceb-aee5-0e026541c22c
rank: zzzzzzzzzzzzzzzzzzw
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, criteria: The conversations suite passes; the helper still selects through the popup., sign_off: none, effort: low, stakes: nuisance}
source: Relay pane, 2026-09-24
links: {plans: [], commits: [], evidence: ['tests/conversations_test.cpp (ctest -R conversations, 100% passed)'], related: [MXMG], github: null}
---
# sessionsDropdownsRespondToMouseChoices fails at HEAD: group combo will not re-open to \"none\"

## Issue
Found while closing out #MXMG: ConversationsTest::sessionsDropdownsRespondToMouseChoices fails reproducibly — `chooseComboItem(group, "none")` at tests/conversations_test.cpp:1609 returns false — in the current tree AND in a clean export of HEAD (built fresh in /tmp, so it predates #MXMG's uncommitted changes). Not filed by the session that introduced it.

## Done means
- `ctest -R conversations` passes, including `sessionsDropdownsRespondToMouseChoices`.
- `chooseComboItem` still drives the combo's real popup (arrow click, scroll, item click) and never falls back to `setCurrentIndex`; only when the synthetic click lands dead does it finish through the popup by keyboard.
- No product code changes: the Sessions combo delivers real clicks (proved by the first selection in the same test and the six other combos it drives); the failure was a synthetic-mouse artifact on a repositioned popup under a platform that cannot grab the mouse.

## Execution Summary
Diagnosis and fix, 2026-09-24:

- Instrumented `chooseComboItem` (temporarily): the second `chooseComboItem(group, "none")` opens the popup, the item's press and release are delivered to the popup viewport at the right local and global coordinates, `indexAt(point)` names the right index — and the selection logic never runs. The popup had re-opened repositioned (container y 86 → 54, aligning the current item), and the offscreen platform warns it "does not support grabbing the keyboard": without a real windowing-system grab, the container never sees the synthetic release.
- The product wiring is fine: the same test's first `chooseComboItem(group, "date")` selects and regroups, six other combos in the same test work, and the full keyboard path through the popup selects `"none"` correctly.
- Fix: `chooseComboItem` finishes by keyboard (arrows + Return on the still-open popup) when the synthetic click lands dead. Instrumentation removed; no product code touched.

## Tests
- `ctest --test-dir build -R conversations` — 100% passed, 0 failed (was: `sessionsDropdownsRespondToMouseChoices` failing).
- Single-run repro before the fix: `QT_QPA_PLATFORM=offscreen ./build/relay-conversations-tests sessionsDropdownsRespondToMouseChoices` failed at tests/conversations_test.cpp:1609; passes after.
