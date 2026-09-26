<!-- relay:entry 20260925T143144Z-xd author=agent kind=event model=claude-opus-5-5 pane=ed6889a9 turn=69ab8d2543a84e28903f984724d1ffd6/a3df11326f4c44b9aedaa7a04031a5d4 -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-25-scratch-sweep-calls-an-old-directory-new-when-so.md

<!-- relay:entry 20260925T143146Z-e1 author=agent kind=event model=claude-opus-5-5 pane=ed6889a9 turn=69ab8d2543a84e28903f984724d1ffd6/a3df11326f4c44b9aedaa7a04031a5d4 -->
- ✦ agent claimed this card · assignee agent, Inbox → Executing, implemented_by anthropic/claude-opus-5-5 via claude-code, session ed6889a9

<!-- relay:entry 20260925T143146Z-e2 author=agent kind=progress model=claude-opus-5-5 pane=ed6889a9 turn=69ab8d2543a84e28903f984724d1ffd6/a3df11326f4c44b9aedaa7a04031a5d4 pane_token=ed6889a9-4d9d-4075-9b39-8c8c0dc3cc15 -->
Claimed (ed6889a9) · working on it from a terminal pane

Sweep uses st_mtime as "created"; a directory's mtime moves when its children change. Fix: snapshot top-level names at turn start, report only names not in it.

<!-- relay:entry 20260925T143213Z-dv author=agent kind=event model=claude-opus-5-5 pane=ed6889a9 turn=69ab8d2543a84e28903f984724d1ffd6/a3df11326f4c44b9aedaa7a04031a5d4 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T143235Z-mr author=agent kind=event model=claude-opus-5-5 pane=ed6889a9 turn=69ab8d2543a84e28903f984724d1ffd6/a3df11326f4c44b9aedaa7a04031a5d4 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["DVV2", "WZ3K"], "githu… → {"plans": [], "commits": ["dc68472"], "evidence": [], "related": ["DVV2", "WZ3K"…; appended to `## Tests`

<!-- relay:entry 20260925T143238Z-n5 author=agent kind=event model=claude-opus-5-5 pane=ed6889a9 turn=69ab8d2543a84e28903f984724d1ffd6/a3df11326f4c44b9aedaa7a04031a5d4 -->
- ✦ agent moved this card · Executing → Needs verification · Landed dc68472: the sweep leaves out entries present at turn start; regression test added · evidence tests/test_scratch_ledger.py · implemented_by anthropic/claude-opus-5-5 via claude-code
