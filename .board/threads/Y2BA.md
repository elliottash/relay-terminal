<!-- relay:entry 20260920T005930Z-6q author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-allow-pressing-new-card-multiple-times-it-splits.md

<!-- relay:entry 20260920T041018Z-q5 author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority -1

<!-- relay:entry 20260925T141353Z-0p author=agent kind=event model=claude-fable-5-1 pane=be471986 turn=1fcbe747559b4edca3c622e83cb41fbe/4e0734b8d76242b3897fcbede8595652 -->
- ✦ agent updated this card · labels: (unset) → ["feature", "switchboard", "panes"]; priority: -1 → (unset); title: allow pressing new card multiple times, it splits the second "new card" pane ver… → Several cards open at once: solo card panes, then a Card pane kind of its own; replaced `## Plan`; tasks: 0/3 done

<!-- relay:entry 20260925T141353Z-4h author=agent kind=rewrite model=claude-fable-5-1 pane=be471986 turn=1fcbe747559b4edca3c622e83cb41fbe/4e0734b8d76242b3897fcbede8595652 -->
- ✦ rewrote title

<details><summary>before</summary>

```
allow pressing new card multiple times, it splits the second "new card" pane ver…
```

</details>

<details><summary>after</summary>

```
Several cards open at once: solo card panes, then a Card pane kind of its own
```

</details>

<!-- relay:entry 20260925T141802Z-q2 author=y2ba-cards kind=progress -->
### y2ba-cards · 2026-09-25 14:18
Claimed #Y2BA; inspecting solo-card entry points and layout restoration before implementation.

<!-- relay:entry 20260925T141830Z-q3 author=y2ba-cards kind=progress -->
### y2ba-cards · 2026-09-25 14:18
Blocked before A0 implementation: the board layout restore branch is in src/RelayWindowCore.cpp:1187-1208, outside #Y2BA’s allowed file list. A0 requires restoration of pinned solo card panes. Need scope expanded to that restore branch (or the restore work assigned to its owner) before A0 can be landed fully. No code edits or commits made.

<!-- relay:entry 20260925T150131Z-jp author=agent kind=note model=glm-5.3 pane=69d471f7 turn=1fcbe747559b4edca3c622e83cb41fbe/52cccb83d761491fba0f3a860633c7fd -->
### Umbrella session · 2026-09-25 (paused, not abandoned)

Holding this slice at the owner's pause. On resume the builder relaunches with `src/RelayWindowCore.cpp` added to its permitted files (the A0 restore branch at :1187); everything else in the recorded plan stands. No code has been changed for this card yet.

<!-- relay:entry 20260925T160500Z-a0 author=y2ba-cards kind=progress -->
### y2ba-cards · 2026-09-25 16:05
Step 1 (A0 solo card panes) landed in 0e198884: `BoardView::pinSolo`, `RelayWindow::openBoardCardInNewPane`, Shift+Enter on a row, the page's ⤴ Own pane button, a new card created while another card's page is open opens in its own pane, `#ID` links prefer a solo pane on that card, layout node `{"board": {workspace, solo: true, card}}` and restore re-pins it (src/RelayWindowCore.cpp restore branch). `ctest -R boardsolo` passes (5 cases); land.py's gate built the exact landed tree. Starting step 2 (Kind::Card / CardController).

<!-- relay:entry 20260925T191737Z-2c author=agent kind=progress model=glm-5.3 pane=22f05421 turn=46dc7f7d90f746138bbd7773c040726f/b5e58fcd7c594b2ca928c9cdc4b4fe32 -->
Builder died at the guest restart. Landed earlier: steps 1–2 (0e198884, 459c2e59). Still uncommitted in the tree from the killed builder (verified by git status, never begun under land.py): the Shift+Enter hint in src/BoardPane.cpp and the stronger tests/boardsolo_test.cpp — the resume builder must review those hunks (`git diff -- src/BoardPane.cpp tests/boardsolo_test.cpp`) and land them via `land.py begin --from-head` + the confirm review. The task checkboxes are stale relative to the commits: the resume builder verifies which of t:ss/t:wb the two commits actually cover and ticks only those. Remaining after that: the live pass in docs/qa_evidence/2026-09-25-card-panes/ scored 8 pass / 8 fail with three named failures — second card pane shows Alpha not the clicked Bravo, layout.json saves only one card-pane node, Plan turns never stream into the card pane — then re-drive the pass, docs (t:g5), needs-verification.

<!-- relay:entry 20260925T191737Z-dv author=agent kind=event model=glm-5.3 pane=22f05421 turn=46dc7f7d90f746138bbd7773c040726f/b5e58fcd7c594b2ca928c9cdc4b4fe32 -->
- ✦ agent moved this card · Executing → Planned · Builder session died at the guest restart; steps 1-2 are on main, leftovers and the live-pass failures remain. Back to planned for the re-queued builder.

<!-- relay:entry 20260925T203000Z-r1 author=y2ba-fix2 kind=progress -->
### y2ba-fix2 · 2026-09-25 20:30
Resumed. The killed builder's `BoardPane.cpp` hint and `boardsolo_test.cpp` landed in d850939f (`boardsolo` passes). The three live failures are gone at HEAD: re-drove `docs/qa_evidence/2026-09-25-card-panes/drive.sh` on a clean `git archive` build under Xvfb, **16 passed, 0 failed** — two card panes on Alpha and Bravo, both Plan turns streamed at once, `layout.json` holds 2 card nodes, both panes come back after a restart. The earlier failures were fixed by 459c2e59/d850939f plus OCR/check fixes in the drive script. Docs: `docs/ARCHITECTURE.md` Board section, "Several cards open at once: card panes". Removed b48afea2 (#9FX8) from links.commits, where it had been appended by mistake. Still open: t:wb's remainder — `CardDetail` in `src/CardPane.{h,cpp}` behind a `CardController`; `Kind::Card` itself landed in 459c2e59.

<!-- relay:entry 20260925T214443Z-5a author=agent kind=event mention=E0Y0 model=k3 pane=fc1a77ed turn=46dc7f7d90f746138bbd7773c040726f/06f47ac8e1234e868ad8ca43b00adc74 -->
mentioned in #E0Y0 · 2026-09-25 · agent

<!-- relay:entry 20260925T224500Z-r2 author=y2ba-move kind=progress -->
### y2ba-move · 2026-09-25 22:45
Step 2 remainder landed: `CardDetail` (and `PriorityFlagButton`, `drawPriorityFlag`, the shared action-depth guard) moved verbatim out of `src/BoardPane.cpp` into new `src/CardPane.h`; the page's std::function callback surface is the plan's `CardController`, BoardView the host that implements it. No `CardPane.cpp`: the class is fully inline, an empty TU would be noise. Full build clean; `ctest -R 'boardsolo|boardpane|windowstate'` 3/3 after the move. The live evidence in `docs/qa_evidence/2026-09-25-card-panes/` (16/16) predates this code-motion-only refactor. Also landed #E0Y0's label editor via --take-foreign (dd02b23a, owner approved; its pane was idle ~1h) — it sat inside CardDetail and blocked the cut.

<!-- relay:entry 20260925T224500Z-r3 author=agent kind=event model=k3 pane=fc1a77ed turn=46dc7f7d90f746138bbd7773c040726f/06f47ac8e1234e868ad8ca43b00adc74 -->
- ✦ agent moved this card · Running → Needs verification · All three tasks landed: A0 solo card panes (0e198884), Kind::Card (459c2e59), hint+tests (d850939f), docs + 16/16 live pass (103e2f60), and the step-2 remainder — CardDetail now in src/CardPane.h (9a13cfe2). boardsolo/boardpane/windowstate pass after the move. Live evidence predates the code-motion refactor; tests re-passed after it. · evidence docs/qa_evidence/2026-09-25-card-panes/ · implemented_by kimi/k3

<!-- relay:entry 20260925T224500Z-r4 author=agent kind=event mention=HKY4 model=k3 pane=6aaee940 turn=068046613259444ba9b13f19ad048b93/74501cccec2f4288befba1efc58c0835 -->
mentioned in #HKY4 · 2026-09-25 · agent
