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
