<!-- relay:entry 20260919T234654Z-zx author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-after-execute-in-a-card-change-execute-to-execut.md

<!-- relay:entry 20260919T234710Z-gz author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260919T234710Z-h0 author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
after "execute" in a card, change "execute" to "executing"
```

</details>

<details><summary>after</summary>

```
after "execute" in a card, change "execute" to "executing" with the pane id in parenthesies. when you click on it, it takes you to the pane.
```

</details>

<!-- relay:entry 20260920T022956Z-ra author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T023841Z-za author=agent kind=event model=glm-5.3 pane=switchboard turn=28fcbe407e234b77a2d4ff824648dd52/3528fd7f66544d909ee0635b15386755 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T023857Z-3t author=agent kind=comment mode=plan model=glm-5.3 turn=28fcbe407e234b77a2d4ff824648dd52/3528fd7f66544d909ee0635b15386755 -->
The card's source is large; I'll pull the relevant sections with targeted searches.





















































































I have the full picture now — the execute hand-off, the thread renderer, the worker's entry attrs, and the existing `focusPane` path. Writing the plan to the card.

**Plan written to #HKAP.** It turns the post-Execute thread entry into `Executing (<8-char pane id>)` carried as a `pane_token` attr through the worker, renders it as a clickable anchor, and reveals the pane via the existing `WindowManager::focusPane` — with the protocol doc and the boardmodel/pytest tests updated to match.

One thing it needs from you: whether the Verify hand-off (`Verify · handed to a new terminal pane…`, src/BoardPane.cpp:4266) should get the same treatment while the plumbing is in — my recommendation is yes.

<!-- relay:entry 20260920T040959Z-1f author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +1

<!-- relay:entry 20260920T040959Z-7k author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +2

<!-- relay:entry 20260920T041149Z-xq author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T041149Z-xr author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Executing · Execute: handed to a terminal pane

<!-- relay:entry 20260920T041149Z-xs author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260920T041626Z-1x author=agent kind=progress -->
Executing: starting the plan — pane token out of onExecuteCard, through board_comment as pane_token, rendered as a relay-pane anchor that focuses the pane. Will post again when it builds and passes the targeted tests.

<!-- relay:entry 20260920T052233Z-ay author=agent kind=event model=glm-5.3 pane=155e08a0 turn=180c54761d2e4cca9bf969fcd583c7a3/45c457553b524a0899c648d7a74d66d1 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"plans": [], "commits": ["56b921f6"], "evidence": ["docs/qa_evidence/2026-09-20…; appended to `## QA checklist`

<!-- relay:entry 20260920T052239Z-4e author=agent kind=event model=glm-5.3 pane=155e08a0 turn=180c54761d2e4cca9bf969fcd583c7a3/45c457553b524a0899c648d7a74d66d1 -->
- ✦ agent moved this card · Executing → Needs verification · Implemented in 56b921f6: Executing (<pane id>) entry with pane_token attr, clickable to its pane; targeted tests and the Xvfb drive green. · evidence docs/qa_evidence/2026-09-20-execute-pane-link/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260920T052239Z-w3 author=agent kind=progress model=glm-5.3 pane=155e08a0 turn=180c54761d2e4cca9bf969fcd583c7a3/45c457553b524a0899c648d7a74d66d1 -->
Landed as 56b921f6 (evidence: docs/qa_evidence/2026-09-20-execute-pane-link/). All six plan steps in; the C++ board suite, the 3 new pane_token tests and the live Xvfb drive are green — ocr.txt shows `Executing (3022e237)` on screen, the click landing on the pane, and the stale link staying inert. The two board test modules' other failures pre-date this card (same lists against the pre-edit snapshot). Note for whoever lands next in this checkout: the shared tree carried #CYM9/#BXCN/#NQP9's uncommitted edits in the same files; the commit was hunk-selected to mine only and their edits were left byte-identical in the tree. The plan's open question — the same treatment for Verify's hand-off entry — is still open; say the word and it's a small follow-up.

<!-- relay:entry 20260920T121346Z-qv author=agent kind=decision model=glm-5.3 pane=155e08a0 turn=180c54761d2e4cca9bf969fcd583c7a3/b9e0da1ce59443728684aafc33972219 -->
2026-09-20, owner: "yes, same" — the plan's open question is answered: Verify's hand-off entry gets the same treatment as Execute's, per the recommendation in ## Plan → Risks ("one more line in steps 2 and 4; say the word and it goes in").

<!-- relay:entry 20260920T121346Z-qw author=agent kind=event model=glm-5.3 pane=155e08a0 turn=180c54761d2e4cca9bf969fcd583c7a3/b9e0da1ce59443728684aafc33972219 -->
- ✦ agent moved this card · Needs verification → Executing · Owner approved the follow-up ("yes, same"): Verify's hand-off entry gets the same treatment before verification. · implemented_by glm/glm-5.3

<!-- relay:entry 20260920T124009Z-k1 author=agent kind=event model=glm-5.3 pane=155e08a0 turn=180c54761d2e4cca9bf969fcd583c7a3/b9e0da1ce59443728684aafc33972219 -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["56b921f6"], "evidence": ["docs/qa_evidence/2026-09-20… → {"commits": ["56b921f6", "31468891"], "evidence": ["docs/qa_evidence/2026-09-20-…; appended to `## QA checklist`

<!-- relay:entry 20260920T124031Z-7j author=agent kind=progress model=glm-5.3 pane=155e08a0 turn=180c54761d2e4cca9bf969fcd583c7a3/b9e0da1ce59443728684aafc33972219 -->
Follow-up landed as 31468891: Verify's hand-off entry now reads "Verifying (<first 8 of the verifier pane's token>) · handed to a new terminal pane on <runner>" with the token as pane_token — the link reveals the verifier's pane exactly like Execute's. boardmodel's verify test extended and green; the Xvfb drive's step 4 keyed `v` on a needs-verification fixture card (anthropic signature → guest:codex) and saw Verifying (f24b2171) with pane_token=f24b2171-… on the thread. By commit time #BXCN/#CYM9/#NQP9 had landed their work, so this one needed no hunk dance — two of their still-uncommitted hunks remain in the tree for their sessions.

<!-- relay:entry 20260920T124031Z-9x author=agent kind=event model=glm-5.3 pane=155e08a0 turn=180c54761d2e4cca9bf969fcd583c7a3/b9e0da1ce59443728684aafc33972219 -->
- ✦ agent moved this card · Executing → Needs verification · Follow-up implemented in 31468891 (Verify's hand-off entry names and links the verifier's pane); tests and live drive green. · evidence docs/qa_evidence/2026-09-20-execute-pane-link/ · implemented_by glm/glm-5.3
