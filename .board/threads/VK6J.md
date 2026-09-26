<!-- relay:entry 20260926T143117Z-ym author=agent kind=event model=claude-opus-5-5 pane=21c53ef4 turn=97d268b4846648f49e6aba30a5ebe433/74f7e6f417e544c9b3c46431d2157099 -->
- ✦ agent created this card in Planned · .board/features/2026-09-26-make-the-queue-gate-fast-run-the-python-suite-in.md

<!-- relay:entry 20260926T143130Z-yp author=agent kind=event model=claude-opus-5-5 pane=21c53ef4 turn=97d268b4846648f49e6aba30a5ebe433/74f7e6f417e544c9b3c46431d2157099 -->
- ✦ agent updated this card · appended to `## Plan`

<!-- relay:entry 20260926T143135Z-5p author=agent kind=event mention=TE6D model=claude-opus-5-5 pane=21c53ef4 turn=97d268b4846648f49e6aba30a5ebe433/74f7e6f417e544c9b3c46431d2157099 -->
mentioned in #TE6D · 2026-09-26 · agent

<!-- relay:entry 20260926T144140Z-s7 author=agent kind=event model=claude-opus-5-5 pane=21c53ef4 turn=97d268b4846648f49e6aba30a5ebe433/00797cedd4ab4fdaae80f7203c439bd2 -->
- ✦ agent claimed this card · assignee agent, Planned → Running, implemented_by anthropic/claude-opus-5-5 via claude-code, session 21c53ef4

<!-- relay:entry 20260926T144140Z-s8 author=agent kind=progress model=claude-opus-5-5 pane=21c53ef4 turn=97d268b4846648f49e6aba30a5ebe433/00797cedd4ab4fdaae80f7203c439bd2 pane_token=21c53ef4-e954-47d2-ad96-7cbdcee705f9 -->
Claimed (21c53ef4) · working on it from a terminal pane

Starting with plan step 1 (parallel Python suite in the gate) and step 3's first cut (drop known failures that now pass).

<!-- relay:entry 20260926T145521Z-wc author=agent kind=progress model=claude-opus-5-5 pane=21c53ef4 turn=97d268b4846648f49e6aba30a5ebe433/00797cedd4ab4fdaae80f7203c439bd2 -->
Step 1 done in 176e62e0 (submitted as d9ff389f): gate-known-failures.py runs each tests/test_*.py module as its own process, 12 at a time, slowest first. On an export of main: 2 min 56 s vs ~12 min serial; slowest module test_session_protocol 175 s is now the floor. Isolation lessons: per-module XDG_DATA_HOME; TMPDIR=/tmp (AF_UNIX 108-byte limit broke 144 guest-bridge tests under a long pane TMPDIR; scratch_ledger treats $TMPDIR as scratch). Four extra failures vs the old baseline also fail serially: environment/time drift, not parallelism. Step 3 first cut: 21 known failures that passed in gate 18ec5ef0 removed (111 -> 90). Next: split test_session_protocol (the new floor), then triage the 90 + 13 remaining and fix queuecontract.

<!-- relay:entry 20260926T150218Z-3g author=landq kind=note -->
Landing job d9ff389f4657a28d (176e62e04884 for card #VK6J) was cancelled.
Nothing was published. <!-- landq:d9ff389f4657a28d:cancelled -->

<!-- relay:entry 20260926T150231Z-ws author=landq kind=note -->
Landing job 15138a8200d0321a (dbb64dd51a8e for card #VK6J) was cancelled.
Nothing was published. <!-- landq:15138a8200d0321a:cancelled -->
