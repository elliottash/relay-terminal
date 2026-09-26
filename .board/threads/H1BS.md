<!-- relay:entry 20260925T145741Z-sf author=agent kind=event model=claude-opus-5-5 pane=7dbb2c54 turn=3cd14271ffc645a7b2f70dba2081b68e/33919fb41b56487bb4ecb7edcdc55c29 -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-25-a-pane-s-scratch-tmpdir-is-too-long-for-chrome-s.md

<!-- relay:entry 20260925T150324Z-1r author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T150324Z-47 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260925T150810Z-p7 author=agent kind=event model=glm-5.3 pane=switchboard turn=8b1951c1cf08f2466527db920cc1129f/4b1d5fa3352b4c7886eb404deced3fcc -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T150831Z-vj author=agent kind=event model=glm-5.3 pane=switchboard turn=8b1951c1cf08f2466527db920cc1129f/4b1d5fa3352b4c7886eb404deced3fcc -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T150838Z-sn author=agent kind=comment mode=plan model=glm-5.3 turn=8b1951c1cf08f2466527db920cc1129f/4b1d5fa3352b4c7886eb404deced3fcc -->
I have the full picture now — the scratch formula in `backend/relay_core/scratch.py` / `src/AppPaths.h`, the TMPDIR hook in `backend/relay_core/agent.py`, and `tests/browser.py`'s inherited-environment Chrome launch. Writing the two sections.







Plan is on #H1BS. It fixes the measured failure inside `tests/browser.py` — Chrome gets a short TMPDIR of its own whenever the inherited one can't fit its 108-byte singleton-socket path, with a Chrome-free unit test and a reproduce-then-verify run of `tests.test_board_view` — and leaves the bigger option (shortening the pane scratch path itself, which interactive Chrome launches also need) as step 6, gated on your yes.

**Need:** answer on the card whether step 6 ships now — recommended, but it changes the #DVV2 path contract in three places while that card is still in needs-verification. Unanswered, Run proceeds with the tests-only fix, which alone meets Done means.

<!-- relay:entry 20260925T150838Z-so author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260925T151257Z-re author=owner kind=comment mode=discuss -->
Ship step 6 too, and implement the whole plan.

<!-- relay:entry 20260925T151343Z-3x author=agent kind=event model=glm-5.3 pane=switchboard turn=8b1951c1cf08f2466527db920cc1129f/4a613617f6004814bfff0e68ba0975a3 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T151351Z-8r author=agent kind=decision model=glm-5.3 pane=switchboard turn=8b1951c1cf08f2466527db920cc1129f/4a613617f6004814bfff0e68ba0975a3 -->
Owner: "Ship step 6 too, and implement the whole plan." — the scratch-path shortening is in scope; the plan's step 6 gate is removed and the land list covers all six files.

<!-- relay:entry 20260925T151358Z-d5 author=agent kind=comment mode=discuss model=glm-5.3 turn=8b1951c1cf08f2466527db920cc1129f/4a613617f6004814bfff0e68ba0975a3 -->
Recorded your decision on #H1BS and rewrote the plan so the shortening is step 5 of 6, unconditional, with the land list now covering all six files (the C++ header means land.py's verify build runs). Implementing it is **Run** — press Run on the card to hand it to a terminal pane.

<!-- relay:entry 20260925T151734Z-h0 author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Planned → Executing, session 5ca366c8

<!-- relay:entry 20260925T151734Z-jv author=owner kind=progress pane=switchboard pane_token=5ca366c8-3171-46e7-ac74-6f149a1dfbdd -->
Claimed (5ca366c8) · working on it from a terminal pane

<!-- relay:entry 20260925T153006Z-kg author=agent kind=event model=claude-opus-5-5 pane=5ca366c8 turn=b8fda29390ee42089a246c82b635fb08/ef7dcb93001d47b7b20d344f56b11bd6 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["DVV2", "7PEC"], "githu… → {"plans": [], "commits": ["7ce5c2ef"], "evidence": ["docs/qa_evidence/2026-09-25…; appended to `## Tests`

<!-- relay:entry 20260925T153012Z-ht author=agent kind=event model=claude-opus-5-5 pane=5ca366c8 turn=b8fda29390ee42089a246c82b635fb08/ef7dcb93001d47b7b20d344f56b11bd6 -->
- ✦ agent moved this card · Executing → Needs verification · Landed 7ce5c2ef: Chrome gets a short TMPDIR when the inherited one is too long, and session roots are named by 12 chars. tests.test_board_view with a pane-length TMPDIR went from 32 errors to 36/36 OK. · evidence docs/qa_evidence/2026-09-25-h1bs-chrome-tmpdir/ · implemented_by anthropic/claude-opus-5-5 via claude-code

<!-- relay:entry 20260925T153025Z-aq author=agent kind=note model=claude-opus-5-5 pane=5ca366c8 turn=b8fda29390ee42089a246c82b635fb08/ef7dcb93001d47b7b20d344f56b11bd6 -->
Incident during cleanup: to reproduce, I used `/home/elliott/.cache/relay/scratch/7dbb2c54-9215-4e1f-a044-52c4f6c7dc1a/tmp` as a stand-in TMPDIR. That is pane 7dbb2c54's real ledgered TMPDIR (row sc68bed), and I did not check first. Afterwards I ran `rm -rf` on it. I recreated the empty directory so that pane's mktemp still works, but any temp files it held are gone. A reproduction should use a fresh UUID-shaped dir.

<!-- relay:entry 20260925T154021Z-r0 author=agent kind=event model=claude-opus-5-5 pane=5ca366c8 turn=b8fda29390ee42089a246c82b635fb08/ef7dcb93001d47b7b20d344f56b11bd6 -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["7ce5c2ef"], "evidence": ["docs/qa_evidence/2026-09-25… → {"plans": [], "commits": ["7ce5c2ef", "b37a9e54"], "evidence": ["docs/qa_evidenc…; appended to `## Tests`

<!-- relay:entry 20260925T154024Z-45 author=agent kind=note model=claude-opus-5-5 pane=5ca366c8 turn=b8fda29390ee42089a246c82b635fb08/ef7dcb93001d47b7b20d344f56b11bd6 -->
Scratch sweep after the first landing: I removed the 22 `/tmp/com.google.Chrome.*` folders created during my test runs (11:14–11:40, 88 KB, none held by a running Chrome). I left alone the other ~32,350 older ones from earlier runs by every session, and `~/.jupyter` (only a `migrated` marker from 11:24; not mine). b37a9e54 stops new ones appearing. Clearing the backlog is the owner's call: every one I checked belongs to a Chrome that has exited.
