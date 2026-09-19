<!-- relay:entry 20260919T182953Z-xt author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-helpful-line-breaks-in-the-terminal.md

<!-- relay:entry 20260919T183351Z-cg author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260919T183351Z-g5 author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
helpful line breaks in the terminal
```

</details>

<details><summary>after</summary>

```
add line breaks in the terminal between content types. 

there should be a line break between agent messages and tool calls for example. sequences of tool calls are single-spaced. ditto with user messsages, line breaks between them (could be half spaced if that looks better)
```

</details>

<!-- relay:entry 20260919T183424Z-sc author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260919T183447Z-7s author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260919T183447Z-vv author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → In progress · Execute: handed to a terminal pane

<!-- relay:entry 20260919T183447Z-vw author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260919T184114Z-mf author=agent kind=progress model=claude-fable-5-1 -->
Read the code. The transcript prints every block single-spaced: the ✦ line, the `▸ model` header, prose deltas, ▸ tool rows. Taking "done" as: one blank line between blocks of different kinds — prose vs tool rows, and before every ✦ line the user typed (including across turns, over the shell prompt) — none inside a run of tool rows, and the `▸ model` header sits directly on what follows it. Rule in src/TranscriptGaps.h (headless test), hooks in Pane.h's printInline/drawCallRow/turnHeader. Building now; live shots under Xvfb next.

<!-- relay:entry 20260919T185737Z-c4 author=agent kind=evidence model=claude-fable-5-1 commit=2a6fa27 -->
Landed 2a6fa27 (#5AWD): src/TranscriptGaps.h + tests/transcriptgaps_test.cpp (the rule), Pane::beginBlock hooks, ARCHITECTURE note, evidence under docs/qa_evidence/2026-09-19-helpful-line-breaks/ (three Xvfb shots against a loopback stub with prose + tool calls, README with the byte trace). One fix found live: the gap for a tool row must print before LineCursor::start()/result() are asked, or the running row is never rewritten in place. Two beginBlock lines sit inside the internals session's (#QT8C) uncommitted tool_started/tool_result branches and will ride along with their commit; on main they landed inside the same branches at the same points. Moving to needs-qa-llm with a QA checklist on the card.

<!-- relay:entry 20260919T185737Z-fz author=agent kind=event model=claude-fable-5-1 -->
- ✦ agent moved this card · In progress → Needs QA (llm) · evidence docs/qa_evidence/2026-09-19-helpful-line-breaks/README.md

<!-- relay:entry 20260919T185817Z-g2 author=agent kind=progress model=claude-fable-5-1 commit=0a919c3 -->
Card move landed as 0a919c3. ctest (C++ suites, the backend suite excluded as unchanged): see the line below in this entry's follow-up; the new transcriptgaps test is in it.

<!-- relay:entry 20260919T185856Z-gx author=agent kind=progress model=claude-fable-5-1 -->
ctest --test-dir build (backend suite excluded, no backend change): 100% passed, 0 failed out of 61, transcriptgaps included. Ready for QA: checklist on the card, evidence in docs/qa_evidence/2026-09-19-helpful-line-breaks/.

<!-- relay:entry 20260919T200320Z-1r author=agent kind=event model=glm-5.3 pane=switchboard turn=e17598d07151419d86ddfea89de60412/c1c5a69cae214f50bedb9384a2802d81 -->
- ✦ agent updated this card · labels: (unset) → ["feature", "terminal"]

<!-- relay:entry 20260919T213041Z-f2 author=agent kind=event model=glm-5.3-flash pane=06c920d0 turn=52ce28cc579d49f0a21b8478a228ad52/a10d0b62695a4a35b91f4d6607afe8d3 -->
- ✦ agent moved this card · Needs QA (LLM) → In progress · Owner QA report with a screenshot: no blank lines anywhere in a real turn. The landed rule is live in their GUI, but every boundary in the shot — the ✦ N tool calls link and the Recap block — is unkinded by design, so no gap can fire there. Reopening to extend the rule: the turn link becomes a Call, the recap gets its own kind. · implemented_by glm/glm-5.3-flash
