<!-- relay:entry 20260920T025149Z-7x author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-switchboard-needs-a-delete-button-for-issues.md

<!-- relay:entry 20260920T025156Z-ee author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260920T025156Z-te author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
switchboard needs a delete button for issues
```

</details>

<details><summary>after</summary>

```
switchboard needs a delete button for issues. right now, i dont see how to delete them.
```

</details>

<!-- relay:entry 20260920T040748Z-td author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +1

<!-- relay:entry 20260920T040751Z-nz author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +2

<!-- relay:entry 20260920T040807Z-q5 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T040807Z-q6 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260920T041248Z-pv author=agent kind=event model=glm-5.3 pane=switchboard turn=bf8c23dcfbae412692e0b14d6c70c7f1/3cb13fe6e69746e5a7ac078d1f491e71 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T041253Z-bm author=agent kind=question model=glm-5.3 pane=switchboard turn=bf8c23dcfbae412692e0b14d6c70c7f1/3cb13fe6e69746e5a7ac078d1f491e71 -->
1. "Nothing is deleted" is a written policy (board_policy.md rule 9): today a card is only ever closed by moving it to `done` or `dropped`. This plan keeps that rule for agents and adds a real, confirmed, 30-second-undoable delete for you at the GUI — is that what you want? My recommendation: yes, take the plan as written; if you would rather not have real deletion, the fallback is a "Close as dropped" entry in the `m` menu plus a hint pointing at it, and no `board_delete` at all.

<!-- relay:entry 20260920T041302Z-ap author=agent kind=comment mode=plan model=glm-5.3 turn=bf8c23dcfbae412692e0b14d6c70c7f1/3cb13fe6e69746e5a7ac078d1f491e71 -->
The investigation is done — the delete path genuinely doesn't exist anywhere (no `board_delete` in the protocol, no "Delete" string in `src/BoardPane.cpp`), while the undo machinery that would make a delete safe is already there. Writing the plan to the card now.



**Plan written to #CYM9.** It adds an owner-only, confirmed, 30-second-undoable delete — a trash button on the open card plus a Delete key, flowing through a new `board_delete` worker message into the existing write log, so Undo restores the card file and its thread — while agents keep no delete tool, per the board's policy.

One thing it needs from you: question 1 on the card — this relaxes the written "nothing is deleted" rule for the owner side, and I've recommended taking the plan as written (the fallback, no real deletion, is noted there too).

<!-- relay:entry 20260920T041302Z-me author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260920T041306Z-ny author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T041306Z-nz author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Executing · Execute: handed to a terminal pane

<!-- relay:entry 20260920T041306Z-o0 author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260920T042140Z-5e author=agent kind=progress turn=execute-cym9 -->
Executing: the backend half is in — BoardTools.delete_card (owner-only, like set_priority) unlinks the card file and both privacy variants of its thread with the bytes on the write record, so the existing undo() restores all of it byte-for-byte; board_delete on the wire refuses while a turn runs on the card, a cleanup, the page agent or a sync, then answers board_written + board_changed with the id in removed. Agents keep no delete tool. Now the GUI half.

<!-- relay:entry 20260920T042144Z-c2 author=agent kind=progress turn=execute-cym9 -->
Executing: the backend half is in — BoardTools.delete_card (owner-only, like set_priority) unlinks the card file and both privacy variants of its thread with the bytes on the write record, so the existing undo() restores all of it byte-for-byte; board_delete on the wire refuses while a turn runs on the card, a cleanup, the page agent or a sync, then answers board_written + board_changed with the id in removed. Agents keep no delete tool. Now the GUI half.

<!-- relay:entry 20260920T054805Z-wz author=agent kind=event model=glm-5.3 turn=execute-cym9 -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260920T054805Z-x0 author=agent kind=event model=glm-5.3 turn=execute-cym9 -->
- ✦ agent moved this card · Executing → Needs verification · the delete is in: worker, wire, GUI and docs, with unit tests and a live Xvfb run · evidence docs/qa_evidence/2026-09-20-switchboard-delete-card/ · implemented_by glm/glm-5.3
