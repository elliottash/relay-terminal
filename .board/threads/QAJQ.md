<!-- relay:entry 20260919T202926Z-94 author=agent kind=event model=glm-5.3 pane=6819a1a8 turn=46a45a61ae0b4cd2b7b7fa11d3558b72/6fc8cfbd41534f128b9c83f27009383b -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-19-tabprojectchip-stylesheet-sits-under-the-9pt-fon.md

<!-- relay:entry 20260919T211940Z-r4 author=agent kind=evidence model=glm-5.3 pane=dca8ae5e turn=7935ced5c8a54973b71b071747972a2a/feff85a793114699ada45cc8d521d957 -->
Reproduced independently while verifying #PFNS (2026-09-19): full `ctest --test-dir build` passes everything except `buttonfit`; `./build/relay-buttonfit-tests` → 204 passed, 1 failed: `ButtonFitTest::stylesheetFontsStayAtOrAboveTheFloor() 'm.captured(1).toDouble() >= relay::theme::FloorPt' returned FALSE. (dark-copper: "font-size: 8.5pt" is under the 9pt floor)` at tests/buttonfit_test.cpp:139. The rule is `QToolButton#tabProjectChip { … font-size: 8.5pt; }` at src/Theme.cpp:542 and is in HEAD (git show HEAD:src/Theme.cpp | grep -c 8.5pt → 1; it came in with 7e3fb9ff, #916B), not in any working-tree edit.

<!-- relay:entry 20260924T043037Z-7n author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260924T043037Z-mn author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260924T043156Z-ft author=agent kind=event model=kimi-k3 pane=switchboard turn=6557630943778001b95e90c6a2afc137/f0a15ae309fb470b83cf6fb43b7e3428 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260924T043227Z-sj author=agent kind=event model=kimi-k3 pane=switchboard turn=6557630943778001b95e90c6a2afc137/f0a15ae309fb470b83cf6fb43b7e3428 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260924T043240Z-gb author=agent kind=comment mode=plan model=kimi-k3 turn=6557630943778001b95e90c6a2afc137/f0a15ae309fb470b83cf6fb43b7e3428 -->
The `tabProjectChip` rule no longer appears in `src/` — let me check whether it was renamed or already fixed.







**Plan written on #QAJQ.** Re-checking during planning, the offending `tabProjectChip` rule is already gone — no such selector anywhere in the tree, and all 57 `font-size` declarations in `src/Theme.cpp` are now ≥ 9pt — so the plan is a two-path one: build and run `relay-buttonfit-tests`; if green, find the commit that removed the rule (`git log -S tabProjectChip`) and close the card with that evidence; if it still fails, raise the named rule to the 9pt floor and re-run. Nothing needed from you — it's ready to Run.

<!-- relay:entry 20260924T043240Z-gc author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
