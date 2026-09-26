<!-- relay:entry 20260924T223142Z-z2 author=agent kind=event model=glm-5.3 pane=f9a856f8 turn=3ee8495f30094cfab1f73dd05908b960/185a451dc1d14af581877a9a8ac6ff80 -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-24-try-it-staging-cleanup-runs-pkill-x-relay-and-ki.md

<!-- relay:entry 20260924T223206Z-6e author=agent kind=event model=glm-5.3 pane=f9a856f8 turn=3ee8495f30094cfab1f73dd05908b960/185a451dc1d14af581877a9a8ac6ff80 -->
- ✦ agent updated this card · appended to `## Issue`

<!-- relay:entry 20260925T052635Z-yc author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T052635Z-yd author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260925T052926Z-q2 author=agent kind=event model=glm-5.3 pane=switchboard turn=655d86558855f758fdd111c9eaf2cb07/4df579037ee543d58d5259051ba66791 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T052957Z-h7 author=agent kind=event model=glm-5.3 pane=switchboard turn=655d86558855f758fdd111c9eaf2cb07/4df579037ee543d58d5259051ba66791 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T053004Z-cj author=agent kind=comment mode=plan model=glm-5.3 turn=655d86558855f758fdd111c9eaf2cb07/4df579037ee543d58d5259051ba66791 -->
I'll read the card and its related context first.

Card read. Now let me find the staging code the card is about — the Try-it evidence landed in `87bf2f6d` under `docs/qa_evidence/2026-09-24-tryit-Z82M/`.



`stage.sh` itself is clean — it writes pid files for the fake upstream and gateway but has no teardown, and never records the sandboxed Relay/Xvfb pid. The `pkill -x relay` was the verifying agent's ad-hoc cleanup. Let me find the Try-it guidance the card says should carry the rule, and check `staging-notes.md`.

The guidance that reaches Try-it turns is `backend/relay_core/board_tryit_brief.md` (versioned beside the board policy, per `AGENT-SESSIONS-PROTOCOL.md:7989-7994`). Let me read it and the tryit protocol's cleanup handling.

The Try-it brief never says how to tear a sandbox down — that's the gap. There's also a `board_cleanup_brief.md` (a cleanup turn the protocol runs) and a cleanup gate in `tryit_protocol.py`. Let me read both.

The whole-board "cleanup" is a different mechanism (card tidying). The dangerous teardown was the Try-it agent's own improvisation between `stage.sh` runs. Let me check `docs/DRIVING-APPS.md` (the brief defers app-driving to it) and whether tests pin the brief's text.





The brief is assembled per-platform — `TI._platform_brief()` carries the Linux Xvfb recipe. Let me read that part of `tryit_protocol.py`.



Now let me re-read the Z82M `stage.sh` in full and check the reference recipe `ai-pass.sh` the brief tells agents to copy — if it teaches a bad teardown, that's part of the fix.

I have the full picture now — including that the repo's own reference recipe (`ai-pass.sh:17`) already kills by recorded pid, and Z82M's `stage.sh` blindly `rm -rf`s its sandbox on fixed ports, which is what forced the improvised `pkill`. Let me re-read the card for its current hash, then write the sections.

Writing `## Done means` first.

Now the plan.

Plan written on #8YNJ, with `## Done means` set first. The plan fixes both halves: a pid-file/never-by-name teardown rule added to the Try-it brief all three platforms send (`board_tryit_brief.md` + `_platform_brief()`), and a concrete `unstage.sh` + self-cleaning rerun for the Z82M staging whose fixed-port `rm -rf` forced the improvised `pkill` in the first place — pinned by the existing tryit prompt tests. Nothing needs from you: the card itself already chose the mechanism, so it's ready to Run.

<!-- relay:entry 20260925T053004Z-rk author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
