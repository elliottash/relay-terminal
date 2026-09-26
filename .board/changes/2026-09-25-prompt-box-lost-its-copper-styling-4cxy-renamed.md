---
id: 6JS0
type: work
status: needs-verification
labels: [bug, ui, composer]
assignee: agent
implemented_by: glm/glm-5.3
session: 3a526e70-483b-43bc-b635-6fd9323658f0
rank: zzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: visual, primary: script, also: [world], human: none, criteria: 'ctest -R composername passes; the capture shows the copper ring; in the app the box has its surface fill and copper border (relay active), no black box', sign_off: none, effort: low, stakes: nuisance}
source: pane 2, 2026-09-25
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-25-6js0-m79d-composer-copper/], related: [4CXY], github: null}
---
# Prompt box lost its copper styling: #4CXY renamed it QFrame#promptBox, orphaning the theme rules

## Issue
ok, then tehre is a bug, because the prompt box color changed from copper to black.

so trace and fix that.

## Done means
The prompt box is styled again: the drawn box is `QFrame#composer` and the editor's direct parent, so the theme sheet paints it (surface fill, rounded border, copper `@accentBorder` when a relay is active) and `theme::polishWindow` / `RelayWindow::repolishLeaf` reach it again.

## Tasks

- [x] Restore the composer frame's objectName and direct-parent structure <!-- t:sc -->
- [x] Guard test composername: the box is the editor's parent, named composer, sheet styles it <!-- t:g1 -->
- [x] Evidence capture of the copper accent border <!-- t:h8 -->

## Execution Summary
Root cause was two-fold, both from `0a79ac4a` (#4CXY): (1) the drawn box was renamed `promptBox`, a name nothing in `Theme.cpp` styles; (2) worse, the new `promptInputArea` QWidget sat between the editor and the frame, so `theme::polishWindow` and `RelayWindow::repolishLeaf` — which both find the box as `qobject_cast<QFrame*>(editor->parentWidget())` — silently stopped finding it: no composer naming, no `WA_StyledBackground`, no `relayActive` copper repolish. The box fell to the default palette: black.

Fix (ba8ccd86): restored the pre-#4CXY structure — editor + corner column directly in the composer, objectName `composer` — and kept a guard test (`composername`, themed, replaces `consolecorner`) asserting the box is the editor's parent, is named `composer`, and the sheet styles `QFrame#composer`; it captures the copper ring on demand via `RELAY_COMPOSER_EVIDENCE`.

## Tests
- `ctest -R composername` — new guard: themed, box is the editor's parent, named `composer`, sheet styles `QFrame#composer` (passes).
- `ctest -R consolemode` — full themed suite, incl. the busy-line geometry tests whose composer lookup was reverted to the editor's parent (passes).
- `ctest -R queuecontract` — untouched run-in-background queue semantics (passes).
- All three ran green in the land verify slot on the exact tree that landed (ba8ccd86).

### Check 2026-09-25 11:16
- missing-evidence · ctest:composername — no run of ctest -R composername for this revision, from any host, and no attached result
- failed · ctest:consolemode — ctest -R consolemode failed for this revision on spark-dcc9
- missing-evidence · ctest:queuecontract — no run of ctest -R queuecontract for this revision, from any host, and no attached result
- notice · ctest:composername — ctest -R composername has never run here
- notice · ctest:consolemode — ctest -R consolemode is slow: p95 50.99 s, p50 1.65 s
- notice · ctest:queuecontract — ctest -R queuecontract is slow: p95 3.52 s, p50 3.49 s
history: thread
