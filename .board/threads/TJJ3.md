<!-- relay:entry 20260925T154631Z-jv author=agent kind=event model=glm-5.3 pane=8d872521 turn=13994c7524bc467f9a77efe825c107b3/e0836faae02c451c984388aad7b3fa70 -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-25-conversations-test-expects-the-old-helper-agent.md

<!-- relay:entry 20260925T220718Z-cn author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=46876a3b turn=246472d62ee64475bf4d75d89fb2c717/5fcdad8de1ef45d2ad6c8040d2b35f49 -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["c96b57745a6f"], "evidence": [], "related": ["G2C7"], … → {"plans": [], "commits": [], "evidence": [], "related": ["G2C7"], "github": null…

<!-- relay:entry 20260925T220730Z-4b author=agent kind=evidence model=deepseek/deepseek-v4.1-flash pane=46876a3b turn=246472d62ee64475bf4d75d89fb2c717/5fcdad8de1ef45d2ad6c8040d2b35f49 -->
Reproduced independently at tip 98bca3a5 while landing the Sessions double-click change, through the land verify slot rather than the shared build:

`land.py try dblclick --tests conversations` → 1/62 failed, the same case: `Actual "Agent (Ctrl+/)" Expected "Helper Agent (Ctrl+/)"` (src/Conversations.cpp:1808-1814 sets `Agent (%1)`; tests/conversations_test.cpp:2433 expects `Helper Agent (Ctrl+/%)`. My own new case in that binary (SessionsTableShowsRecapAndPreviewsOnDemand) passed.

So this is the tip, not a tree artefact: `ctest -R conversations` is red for anyone who verifies a change in `src/Conversations.*`, and land.py try/commit report it as their own failure. The landed commit c96b5774 (docs note about the Sessions row's mouse gesture) was appended here by mistake: my commit message named #TJJ3 as a cross-reference, and land.py links every #ID a message names. I removed it from links.commits; it is not a fix for this card. The fix here is one line plus the stale comment above `updateHelperRow`.

<!-- relay:entry 20260925T220730Z-zr author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=46876a3b turn=246472d62ee64475bf4d75d89fb2c717/5fcdad8de1ef45d2ad6c8040d2b35f49 -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started
