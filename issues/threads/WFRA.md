<!-- relay:entry 20260924T025131Z-28 author=agent kind=event model=claude-fable-5-1 pane=9b541d35 turn=cf99c92a9b1e48559ab6b383c65134c7/a328f0aa299c4326bff4c99a3b066dc2 -->
- ✦ agent created this card in Planned · issues/features/2026-09-23-the-qa-ladder-a-verify-block-on-every-card-propo.md

<!-- relay:entry 20260924T025224Z-fg author=agent kind=event model=claude-fable-5-1 pane=9b541d35 turn=cf99c92a9b1e48559ab6b383c65134c7/a328f0aa299c4326bff4c99a3b066dc2 -->
- ✦ agent updated this card · parent: (unset) → BX7B; component: (unset) → ["worker", "gui"]; replaced `## Done means`

<!-- relay:entry 20260924T025743Z-wf author=agent kind=event model=claude-fable-5-1 pane=9b541d35 -->
- ✦ agent claimed this card · Planned → Executing · assignee agent

<!-- relay:entry 20260924T025743Z-wg author=agent kind=progress model=claude-fable-5-1 pane=9b541d35 -->
Claimed · subagent wfra-backend of pane 9b541d35, Python side: `verify` schema and validation in board.py, board_update_card fields.verify, claim reminder, BOARD.md column, check, policy and deliver text, protocol doc.

<!-- relay:entry 20260924T030003Z-gu author=agent kind=progress model=claude-fable-5-1 pane=9b541d35 -->
Claimed · subagent wfra-gui of pane 9b541d35, GUI side: `VerifyPlan` + `verifyStripText` in `src/BoardModel.*`, the Verify strip on the card page in `src/BoardPane.cpp` under the Try it strip, cases in `tests/boardmodel_test.cpp` and `tests/boardpane_test.cpp`. The backend half (`wfra-backend`) holds the card's status.

<!-- relay:entry 20260924T031234Z-wh author=agent kind=event model=claude-fable-5-1 pane=9b541d35 -->
- ✦ agent updated this card · replaced `## Plan`, `## Tasks`, `## Execution Summary`, `## Tests`; verify: (unset) → {artifact: code, primary: script, also: [ai-text], human: optional, …}; links.commits: [] → [39eedbfb6874]

<!-- relay:entry 20260924T031234Z-wi author=agent kind=event model=claude-fable-5-1 pane=9b541d35 -->
- ✦ agent moved this card · Executing → Needs verification · Python side landed in 39eedbfb: schema, validation, check, BOARD.md column, tools, policy, skill, protocol 19.21; the card-page strip is the GUI session's · implemented_by anthropic/claude-fable-5-1

<!-- relay:entry 20260924T031954Z-wfras author=agent kind=event model=claude-fable-5-1 pane=9b541d35 -->
- ✦ agent noted owner steer 2026-09-23: agent-facing only · Verify column dropped from BOARD.md and board_list rows; the block travels in board_read / the board_card event alone, the deferred card's `unverified until …` is the only user-facing row text, the claim reminder stays in the tool result
