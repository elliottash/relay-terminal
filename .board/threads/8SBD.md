<!-- relay:entry 20260920T223413Z-jf author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-20-bug-when-you-copy-from-pane-line-breaks-are-drop.md

<!-- relay:entry 20260920T223426Z-0j author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260920T223426Z-x8 author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
bug: when you copy from pane , line breaks are dropped rather than replaced with a space
```

</details>

<details><summary>after</summary>

```
bug: when you copy from pane , line breaks are dropped rather than replaced with a space

so you get words concatenated together
```

</details>

<!-- relay:entry 20260920T223428Z-5t author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T223428Z-ea author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260920T224125Z-pt author=agent kind=event model=kimi-k3 pane=switchboard turn=d6e0ed1953ee41ad809b7d23fb6d0a23/6974b71fdefe439c93ed667209d7d952 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T224140Z-gd author=agent kind=comment mode=plan model=kimi-k3 turn=d6e0ed1953ee41ad809b7d23fb6d0a23/6974b71fdefe439c93ed667209d7d952 -->
**Done:** the plan is on #8SBD. It pins the bug to `LibVtermCore::selectedText()` joining soft-wrapped rows through `Line::text()`, which trims the trailing space at the wrap point — so "foo bar" copies as "foobar". The fix is a trim-free join at continuation boundaries (keeping URLs that wrap mid-token intact), a failing test in `engine/tests/CoreTest.cpp` first, plus checks of the ghostty core and the fold selection path for the same flaw. Nothing needed from you — it's ready for Execute.

<!-- relay:entry 20260920T224140Z-jh author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260921T210756Z-a1 author=codex kind=evidence -->
### Codex · 2026-09-21 21:07
The #K9KC commit audit confirmed the fold-selection path independently: print "alpha beta gamma xyz delta" at 100 columns, resize to 20, then select both rows. Actual copy: "alpha beta gamma xyzdelta". With prose replacement disabled, the same resize preserves the space. In 8147cc55, wrap::rows drops the edge-space cell and visualSelectedText concatenates the displayed row ranges, omitting it. This is distinct from the native trim path already in the plan. Reproducer: run-audit-probe.py copyPreservesSpaceAtWrappedEdge under docs/qa_evidence/2026-09-21-card-file-links/.

<!-- relay:entry 20260921T212648Z-b1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 17:26
Claimed this card. Fixing the dropped edge space, in both the core's selectedText and the fold path; the audit probe under docs/qa_evidence/2026-09-21-card-file-links/ is the reproduction and will be the before/after evidence.

<!-- relay:entry 20260921T214022Z-c1 author=claude-code kind=evidence -->
### Claude Code · 2026-09-21 17:40
Landed in two commits: 4e622af8 (the core's selectedText, plan steps 1–3) and 4a4663e0 (the re-wrapped block's own path, step 4). Character-exact join in both. Step 3 could not be run here — this build has RELAY_ENGINE_WITH_GHOSTTY=OFF and no prebuilt libghostty-vt, so GhosttyCore was left alone and the new cases are written to run against it on a build that has it. Tests: CoreTest::selectionKeepsThePrintedSpaceAtASoftWrap (fails with "the quickbrown" when the fix is reverted), CoreTest::selectionAcrossAMidTokenWrapJoinsWithNothing, ViewTest::copyingAcrossAWrappedEdgeKeepsItsSpace, ViewTest::copyingAcrossAWrappedTokenInsertsNothing. On the landed tree ViewTest 56/56, CoreTest 45/45, FoldLayerTest 32/32, audit probe 6/6 both ways. Evidence: docs/qa_evidence/2026-09-21-block-rows-selection/. Moved to needs-verification with a QA checklist.
