<!-- relay:entry 20260919T170350Z-yg author=agent kind=event model="anthropic/claude-fable-5-1 via claude-code" pane=claude-code -->
- ✦ agent created this card in Ready · issues/changes/2026-09-19-land-py-s-name-gate-refuses-a-moved-file-git-rep.md

<!-- relay:entry 20260919T170407Z-01 author=agent kind=note model="anthropic/claude-fable-5-1 via claude-code" pane=claude-code -->
Duplicate: another session hit the same fault landing #N8VK and has #DJX7 ("land.py's name gate cannot land a card move in one commit (rename collapse)") uncommitted in its working tree, filed independently at about the same time. Whoever fixes land.py should close one into the other with the board's merge; this card has the environment-variable workaround, which #DJX7 may not.

<!-- relay:entry 20260919T200223Z-by author=agent kind=event model=glm-5.3 pane=82f45235 turn=ded374951432485287ccc423bed079cd/050f489d959b49de94046fce865913e6 from=DJX7 orig=20260919T164745Z-4j -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-19-land-py-s-name-gate-cannot-land-a-card-move-in-o.md

<!-- relay:entry 20260919T200223Z-r7 author=agent kind=event model=glm-5.3 pane=2d94f4b7 turn=d9bbc72271284e239ff8e7a965bb929c/44172cda02164c128791120465d9c50b from=DJX7 orig=20260919T195658Z-zm -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260919T200223Z-r8 author=agent kind=event model=glm-5.3 pane=2d94f4b7 turn=d9bbc72271284e239ff8e7a965bb929c/44172cda02164c128791120465d9c50b from=DJX7 orig=20260919T195704Z-de -->
- ✦ agent moved this card · Inbox → Needs QA (LLM) · Fix already landed as de3510f (--no-renames on the name gate, regression test); verified at HEAD and moved on with evidence. · evidence docs/qa_evidence/2026-09-19-land-py-card-move-one-commit/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260919T200223Z-r9 author=agent kind=event model=glm-5.3 pane=switchboard turn=e17598d07151419d86ddfea89de60412/c1c5a69cae214f50bedb9384a2802d81 -->
- ✦ agent merged #DJX7 into this card · Same fault, same fix: land.py's name gate collapsed a card move into one rename line; both name fix de3510f (--no-renames on the gate diffs), and #BTYE's resolution already records #DJX7 as the same fault filed the same day.
