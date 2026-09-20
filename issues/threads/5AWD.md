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

<!-- relay:entry 20260919T225221Z-r4 author=agent kind=event model=glm-5.3-flash pane=9bbaa851 turn=52ce28cc579d49f0a21b8478a228ad52/cc6f950d0c624c549211590b76bcb3de -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["2a6fa27"], "evidence": ["docs/qa_evidence/2026-09-19-… → {"commits": ["b25fd164", "8147cc55", "3e12fa58"], "evidence": ["docs/qa_evidence…; replaced `## What "done" means`

<!-- relay:entry 20260919T225221Z-r5 author=agent kind=rewrite model=glm-5.3-flash pane=9bbaa851 turn=52ce28cc579d49f0a21b8478a228ad52/cc6f950d0c624c549211590b76bcb3de -->
- ✦ rewrote ## What "done" means

<details><summary>before</summary>

```
One blank line between blocks of different kinds in the terminal transcript: the ✦ line the user
typed, the `▸ model` header, the agent's prose, and the ▸ tool-call rows. Never between two blocks
of the same kind (a run of tool rows stays single-spaced), never right after the header (it sits
on top of what follows), and a ✦ line is set off from the previous turn even across the shell
prompt between them. Notes, errors, inline diffs and streamed tool output carry no kind and stay
attached to the block above. "Half spacing" is not available on a terminal grid, so the gap is a
full blank row. Rule: `src/TranscriptGaps.h`; hooks: `Pane::beginBlock` in `src/Pane.h`.
```

</details>

<details><summary>after</summary>

```
One blank line between blocks of different kinds in the terminal transcript: the ✦ line the user
typed, the `▸ model` header, the agent's prose, the ▸ tool-call rows, the "✦ N tool calls" link that
sums a run of them up, and a "Recap ·" block. Never between two blocks of the same kind (a run of
tool rows stays single-spaced, a recap's own lines stay together), never right after the header (it
sits on top of what follows it), and a ✦ line is set off from the previous turn even across the
shell prompt between them. Notes, errors, inline diffs and streamed tool output carry no kind and
stay attached to the block above. The "✦ N tool calls" link is a Call block: it is set off from the
prose above it, but it crowns a run of tool rows with no extra gap. "Half spacing" is not available
on a terminal grid, so the gap is a full blank row. Rule: `src/TranscriptGaps.h`; hooks:
`Pane::beginBlock` in `src/Pane.h`.
```

</details>

<!-- relay:entry 20260919T225228Z-gn author=agent kind=event model=glm-5.3-flash pane=9bbaa851 turn=52ce28cc579d49f0a21b8478a228ad52/cc6f950d0c624c549211590b76bcb3de -->
- ✦ agent updated this card · replaced `## QA checklist`

<!-- relay:entry 20260919T225235Z-7x author=agent kind=progress model=glm-5.3-flash pane=9bbaa851 turn=52ce28cc579d49f0a21b8478a228ad52/cc6f950d0c624c549211590b76bcb3de -->
Owner QA report (screenshot of a real turn showing everything single-spaced): the landed rule from b25fd164 was already live in the user's GUI (binary built 15:52 EDT, after the 14:57 EDT commit), but every boundary in the shot — the `✦ N tool calls · …` turn link and the `Recap · …` block — was left unkinded by the first pass. Extended the rule: `Pane::printTurnLink` now begins a `Call` block, and `Ink::Recap` maps to a new `Recap` block in `Pane::printInline`. Added `transcriptgaps_test.cpp` coverage for both. `scripts/relay-build` green; `ctest --test-dir build` 100% passed (63/63). `drive.sh` now has a fourth scene (`/recap`) and the regenerated shots/OCR show the new gaps: in 03, a blank row between the closing prose and `✦ 1 tool call · 1 s (Ctrl+click)`; in 04, a blank row between that link and `Recap · 18:45 → 18:46 · <1m`. Note: the second-pass code was swept into the shared-tree reconciliation as 8147cc55 (src/Pane.h, src/TranscriptGaps.h) and 3e12fa58 (transcriptgaps_test.cpp); evidence landed in 7558c9b2 (drive.sh + 01-03 shots), eac9bbc5 (04-recap shots), and 19083755 (README). I also corrected the card's links.commits: the original commit actually on main is b25fd164, not 2a6fa27 (which exists as an object but is on no branch).

<!-- relay:entry 20260919T225239Z-1t author=agent kind=event model=glm-5.3-flash pane=9bbaa851 turn=52ce28cc579d49f0a21b8478a228ad52/cc6f950d0c624c549211590b76bcb3de -->
- ✦ agent moved this card · In progress → Needs QA (LLM) · Second pass implemented and evidence captured. The original rule was live; the QA gap was at the turn link and Recap block, which now have kinds. Moving back to needs-qa-llm for the recommended verifier (Codex/openai) to close the checklist with a real provider. · evidence docs/qa_evidence/2026-09-19-helpful-line-breaks/README.md · implemented_by glm/glm-5.3-flash
