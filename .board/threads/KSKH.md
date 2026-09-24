<!-- relay:entry 20260923T182359Z-h1 author=agent kind=event model=glm-5.3 pane=d47c1a2d turn=24d6aa1ed84749d2b507b965a4e38c65/d0595f344db14c9abfa3f8ebbc25cd62 -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-23-card-consoles-aren-t-independent-set-tab-takes-a.md

<!-- relay:entry 20260923T182410Z-xz author=agent kind=event model=glm-5.3 pane=d47c1a2d turn=24d6aa1ed84749d2b507b965a4e38c65/d0595f344db14c9abfa3f8ebbc25cd62 -->
- ✦ agent updated this card · appended to `## Issue`

<!-- relay:entry 20260923T182416Z-8m author=agent kind=event model=glm-5.3 pane=d47c1a2d turn=24d6aa1ed84749d2b507b965a4e38c65/d0595f344db14c9abfa3f8ebbc25cd62 -->
- ✦ agent updated this card · appended to `## Done means`

<!-- relay:entry 20260923T182419Z-1k author=agent kind=event model=glm-5.3 pane=d47c1a2d turn=24d6aa1ed84749d2b507b965a4e38c65/d0595f344db14c9abfa3f8ebbc25cd62 -->
- ✦ agent claimed this card · assignee agent, Inbox → Executing, implemented_by glm/glm-5.3, session d47c1a2d

<!-- relay:entry 20260923T182419Z-w0 author=agent kind=progress model=glm-5.3 pane=d47c1a2d turn=24d6aa1ed84749d2b507b965a4e38c65/d0595f344db14c9abfa3f8ebbc25cd62 pane_token=d47c1a2d-5224-474f-a103-3fc0a9e65207 -->
Claimed (d47c1a2d) · working on it from a terminal pane

Fixing both faults: the set_tab poisoning (backend) and the cross-card console delivery (GUI). Tests: tab_of in tests/test_board_chat.py, the delivery predicate in tests/boardpane_test.cpp.

<!-- relay:entry 20260923T183025Z-8t author=agent kind=event model=glm-5.3 pane=d47c1a2d turn=24d6aa1ed84749d2b507b965a4e38c65/d0595f344db14c9abfa3f8ebbc25cd62 -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260923T183034Z-tw author=agent kind=event model=glm-5.3 pane=d47c1a2d turn=24d6aa1ed84749d2b507b965a4e38c65/d0595f344db14c9abfa3f8ebbc25cd62 -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260923T183038Z-e8 author=agent kind=event model=glm-5.3 pane=d47c1a2d turn=24d6aa1ed84749d2b507b965a4e38c65/d0595f344db14c9abfa3f8ebbc25cd62 -->
- ✦ agent moved this card · Executing → Needs verification · Both faults fixed with tests; verified as far as the shared checkout allows (relay target red from another session's in-flight work) · evidence docs/qa_evidence/2026-09-23-kskh/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260923T183306Z-mr author=agent kind=evidence model=glm-5.3 pane=d47c1a2d turn=24d6aa1ed84749d2b507b965a4e38c65/d0595f344db14c9abfa3f8ebbc25cd62 -->
Landed: `aba738b7` (code + tests; only this session's hunks — the other sessions' in-flight edits in src/BoardPane.h and src/RelayWindow.h were left uncommitted in the working tree, picked out with land.py --only-hunk) and `466cc8f4` (card, thread, `docs/qa_evidence/2026-09-23-kskh/`). One leftover for the owner: the doubled-key conversation files from this afternoon (`<tab>/card:<X>/card:<X>.json` digests in `~/.local/share/relay/helper-sessions/`) still hold those cards' console transcripts; after a restart the fixed keys (`<tab>/card:<X>`) start fresh, and the card *threads* — the real record — were always written correctly. Not migrating them while the live worker still autosaves under the old keys.

<!-- relay:entry 20260923T183316Z-s8 author=agent kind=event model=glm-5.3 pane=d47c1a2d turn=24d6aa1ed84749d2b507b965a4e38c65/d0595f344db14c9abfa3f8ebbc25cd62 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-23-kskh/"], … → {"commits": ["aba738b7", "466cc8f4"], "evidence": ["docs/qa_evidence/2026-09-23-…

<!-- relay:entry 20260923T183342Z-as author=agent kind=progress model=glm-5.3 pane=d47c1a2d turn=24d6aa1ed84749d2b507b965a4e38c65/d0595f344db14c9abfa3f8ebbc25cd62 -->
- ✦ agent is preparing Try it for this card

<!-- relay:entry 20260923T183359Z-s6 author=agent kind=note model=glm-5.3 pane=d47c1a2d turn=24d6aa1ed84749d2b507b965a4e38c65/d0595f344db14c9abfa3f8ebbc25cd62 -->
Try it could not be staged: the app binary does not build in this shared checkout right now — `scripts/relay-build --target relay` fails on three errors in another session's in-flight "run in background" work (`Pane::agentReady`, `WindowManager::refreshBackgroundTasks`/`backgroundCount`), and `relay-build` builds only the canonical `build/`, so there is no second directory to build the landed tip `aba738b7` from. Re-run Try it once that work lands and `build/relay` links; the staging then needs only a throwaway board with two cards, a Plan started on one and a Discuss on the other, and the card consoles watched for cross-talk. What could be checked without the binary is in `docs/qa_evidence/2026-09-23-kskh/` (the on-disk doubled-key proof, the fixes, and the green targeted tests).

<!-- relay:entry 20260923T184507Z-nd author=agent kind=evidence -->
Check · 1 missing-evidence, 1 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T190223Z-9c author=agent kind=evidence -->
Check · 1 missing-evidence, 1 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T190306Z-ej author=agent kind=evidence -->
Check · 1 missing-evidence, 1 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.
