<!-- relay:entry 20260923T164127Z-aa author=codex kind=evidence -->
Guest parity audit after #XP7N: Board 7 tools missing, Relay app 13 missing, session 2 missing, plus conditional keybinding and program-control tools. Source catalogs: backend/relay_core/board_tools.py, app_tools.py, activity_tools.py, agent.py, guest_board_bridge.py. #4NXH intentionally limited the original Board bridge to five tools.

<!-- relay:entry 20260923T180112Z-w3 author=agent kind=decision model=gpt-6-sol pane=6330edbf turn=1146f84a5b7d4390a2b9887b7a77b357/382d5e9a979246afa2b624e849b13677 -->
Owner, 2026-09-23: “guests should be able to create cards. build tool parity now. once its fully done, check it works. i want parity for the other differences you mentioend as well.” This supersedes the original five-tool bridge scope, including its exclusion of board_create_card.

<!-- relay:entry 20260923T180127Z-07 author=agent kind=event model=gpt-6-sol pane=6330edbf turn=1146f84a5b7d4390a2b9887b7a77b357/382d5e9a979246afa2b624e849b13677 -->
- ✦ agent updated this card · appended to `## Decisions`; replaced `## Plan`

<!-- relay:entry 20260923T180130Z-ry author=agent kind=event model=gpt-6-sol pane=6330edbf turn=1146f84a5b7d4390a2b9887b7a77b357/382d5e9a979246afa2b624e849b13677 -->
- ✦ agent moved this card · Planned → Executing · Owner requested implementation of full guest tool parity, including Board creation; beginning bridge and verification work. · implemented_by openai/gpt-6-sol via codex

<!-- relay:entry 20260923T182322Z-2h author=agent kind=event model=gpt-6-sol pane=6330edbf turn=1146f84a5b7d4390a2b9887b7a77b357/382d5e9a979246afa2b624e849b13677 -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260923T182329Z-yk author=agent kind=event model=gpt-6-sol pane=6330edbf turn=1146f84a5b7d4390a2b9887b7a77b357/382d5e9a979246afa2b624e849b13677 -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260923T182357Z-3n author=agent kind=event model=gpt-6-sol pane=6330edbf turn=1146f84a5b7d4390a2b9887b7a77b357/382d5e9a979246afa2b624e849b13677 -->
- ✦ agent moved this card · Executing → Needs verification · Guest tool catalogs and delegation semantics now match native capabilities; 668 focused tests pass and installed Codex/Claude turns exercised disposable Board/app/session calls. · evidence docs/qa_evidence/2026-09-23-GPA8-guest-tool-parity/verification.md · implemented_by openai/gpt-6-sol via codex

<!-- relay:entry 20260923T182719Z-zq author=agent kind=event model=gpt-6-sol pane=6330edbf turn=1146f84a5b7d4390a2b9887b7a77b357/382d5e9a979246afa2b624e849b13677 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-23-GPA8-gues… → {"plans": [], "commits": ["423326d77f5ba247f8dc3223c34aa1f8f8118612"], "evidence…
