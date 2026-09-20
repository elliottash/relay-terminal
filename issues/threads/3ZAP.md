<!-- relay:entry 20260920T035843Z-8t author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-in-the-switchboard-make-it-where-if-you-click-on.md

<!-- relay:entry 20260920T041059Z-4c author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +1

<!-- relay:entry 20260920T041100Z-wz author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T041100Z-x0 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260920T041658Z-3w author=agent kind=event model=glm-5.3 pane=switchboard turn=8eb3e54e575445aaab2fa61bd2584609/835a659c440b4c14a4d01898019fd794 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T041709Z-rs author=agent kind=comment mode=plan model=glm-5.3 turn=8eb3e54e575445aaab2fa61bd2584609/835a659c440b4c14a4d01898019fd794 -->
No existing hashtag handling. Let me find the Switchboard UI code.

The Switchboard UI lives in `src/BoardPane.cpp`. Let me look at how the card detail renders text and what's already clickable.

Two promising spots: `BoardPane.cpp:4724` already copies `#` + something, and `1444` handles anchor clicks in the card detail. Let me read those regions.

The file is over the read limit; I'll probe it with targeted searches.

Line 4724 is the existing "copy card id" action. Now let me see how labels and anchors render — the `anchorClicked` handler at 1444 and label chips around 2914–3160.

I need the actual code around those lines. Let me find the functions that build the detail document's HTML.

The detail pane class seems to be `BoardDetail`. Let me find it and its document builder.

The detail widget is a private class inside `BoardPane.cpp`. Let me see what's around the anchor handler and where card refs/labels get turned into HTML.

Let me find the functions after line 4772 (the list was truncated) and what contains line 5350.









The test file exercises the detail document — let me read it to learn the document's shape, and find the meta/body builders.







Labels are painted badges in the list. Let me check how the filter chips and detail meta show labels — whether they carry a `#`.



The detail body is plain inserted lines — no anchors today. Let me see what links `metaText` builds and how the meta link handler behaves.

One more surface to check — the chat log in the Switchboard's agent panel.







Let me pin down where the card body text itself is inserted in `CardDetail`.

I now have everything I need: the card detail (`CardDetail`) renders the body with `setMarkdown` and the thread via `insertLine`, with no anchors today, and there's a copy+toast precedent in `copyReference()`. Writing the plan.

**Done:** plan written on #3ZAP. It makes hashtags in the card detail (body rendered via `setMarkdown` plus thread lines via `insertLine`) clickable `tag:` anchors that copy `#bug`-style text to the clipboard with a "Copied #…" toast, reusing the existing `copyReference()` precedent, with a new `boardmodel_test` case and a live Xvfb check for verification.

One thing for you to confirm (in the plan's Risks): `#K7Q2`-style card references currently open the card elsewhere — the plan has them **copy** too, exactly as you asked, rather than diverging per tag shape.

<!-- relay:entry 20260920T041709Z-rt author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260920T051949Z-vr author=owner kind=comment mode=discuss -->
clicking a hash tag in a pane takes you to the switchboard without copying.

references to other cards in a card issue / thread / plan get assigned links that zoom the other card without copying. 

clicking the ash tags in the main switchboard list, or at the top of the card, does the copy.

<!-- relay:entry 20260920T052101Z-j4 author=agent kind=decision model=kimi-k3 pane=switchboard turn=96a231219f9549edb519d50ebd61c1c6/d2fb561931c64fe4ae6570f369fb6714 -->
Owner refined the scope: "clicking a hash tag in a pane takes you to the switchboard without copying. references to other cards in a card issue / thread / plan get assigned links that zoom the other card without copying. clicking the ash tags in the main switchboard list, or at the top of the card, does the copy." So: pane hashtag links stay navigate-only; `#K7Q2`-style refs in card text zoom to that card; label hashtags in the list rows, the card header, and the card body/thread copy.

<!-- relay:entry 20260920T052149Z-tf author=agent kind=event model=kimi-k3 pane=switchboard turn=96a231219f9549edb519d50ebd61c1c6/d2fb561931c64fe4ae6570f369fb6714 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T052159Z-q3 author=agent kind=comment mode=discuss model=kimi-k3 turn=96a231219f9549edb519d50ebd61c1c6/d2fb561931c64fe4ae6570f369fb6714 -->
Let me check the current rendering spots so the revised plan is accurate.





That clarifies it — the copy affordance belongs on the label surfaces, while `#K7Q2` refs navigate. Updating the plan to match.





**Updated #3ZAP** — recorded your clarification as a decision and rewrote the plan:

- **Copy + "Copied #…" toast** happens on the label surfaces: list-row badges, card-header labels (via `metaText`), and `#bug`-style tags in the card body/thread.
- **`#K7Q2`-style refs** in issue/thread/plan become `card:` links that zoom to that card, no copy (reusing the cleanup panel's existing `card:` handling).
- **Pane hashtag links** keep navigating to the switchboard — unchanged.

One detail I settled in the plan: a 4-char tag that matches no existing card id is treated as a label (copies), since shape alone is ambiguous. Say the word if you'd rather it do nothing. The card is still in **Planned** — hit **Execute** when you want it built.

<!-- relay:entry 20260920T052232Z-es author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T052233Z-xy author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Executing · Execute: handed to a terminal pane

<!-- relay:entry 20260920T052233Z-xz author=owner kind=progress pane=switchboard pane_token=7dcb4e9a-6472-49c3-98c0-ccc00eab1488 -->
Executing (7dcb4e9a) · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

clicking the card

<!-- relay:entry 20260920T063040Z-3z author=agent kind=event model=glm-5.3 pane=7dcb4e9a turn=fdb3e925694241459fe5be66cc0026aa -->
- ✦ agent moved this card · Executing → Needs verification · Implemented and verified on tip+change (board suite 77/77 on the landed tree; live Xvfb drive 12/12 in the evidence dir); QA checklist on the card. · evidence docs/qa_evidence/2026-09-20-switchboard-hashtag-copy/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260920T063040Z-4q author=agent kind=event model=glm-5.3 pane=7dcb4e9a turn=fdb3e925694241459fe5be66cc0026aa -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"plans": [], "commits": ["d8de5b77", "6127a540"], "evidence": ["docs/qa_evidence/2026-09-20-switchboard-hashtag-copy/"], "related": [], "github": null}

<!-- relay:entry 20260920T063040Z-5m author=agent kind=note model=glm-5.3 pane=7dcb4e9a turn=fdb3e925694241459fe5be66cc0026aa -->
Landed as d8de5b77 (the linkify pass over body/thread Markdown and plain lines, `tag:` copies + `card:` zooms in the detail and the meta labels, and the row badge as one copy-only gesture — press, release and double-click all guarded) and 6127a540 (evidence). One finding for the verifier and for #CYM9: the shared checkout's board suite hangs in the uncommitted delete-confirm test; I reproduced it in a baseline worktree *without* this change, so it is that session's, not this one's — on tip+this-change the suite is 77/77 (logs/exact-tree-ctest.txt). Qt note discovered on the way: a swallowed press still leaves QAbstractItemView's pressedIndex stale, so a control-in-a-row that must not select has to swallow the release too; the priority flag never noticed because selecting the row it sits on is harmless there.
