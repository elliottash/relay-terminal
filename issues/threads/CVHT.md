<!-- relay:entry 20260919T042942Z-7g author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-bolding-main-points.md

<!-- relay:entry 20260919T043101Z-4a author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260919T043101Z-zm author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
bolding main points
```

</details>

<details><summary>after</summary>

```
encourage agents to put the main point in bold, especially saying when they are done, when they find a problem and when saying what they need from the user. 

is it possible to add colored bolds for that? eg purple for done, blue what they need form the user, and red for problems.
```

</details>

<!-- relay:entry 20260919T043110Z-yw author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260919T043408Z-q6 author=agent kind=event model=glm-5.3 pane=switchboard turn=d8ab68af7bf844d8a7e046621317b3cf/845542d42cb14b4e927ea8d7ff5e2c3f -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260919T043420Z-h1 author=agent kind=comment mode=plan model=glm-5.3 turn=d8ab68af7bf844d8a7e046621317b3cf/845542d42cb14b4e927ea8d7ff5e2c3f -->
I have what I need: the prompt lives in `backend/relay_core/agent.py` (`SYSTEM`), the renderer is `src/MarkdownAnsi.cpp/.h` (bold = `;1`, palette of theme-indexed ANSI colours), with tests in `tests/markdownansi_test.cpp` and `tests/test_agent.py`. Writing the plan to the card.

Plan written to #CVHT. It covers both asks — one new line in the agent's `SYSTEM` prompt (`backend/relay_core/agent.py`) teaching `**Done:**` / `**Problem:**` / `**Need:**` bold labels, and keyword-triggered colouring of those bold runs in `MarkdownAnsi` (new palette entries, with tests in `markdownansi_test.cpp` and `test_agent.py`).

One decision for you, noted in the plan's Risks: literal purple/blue/red RGB would be burnt into the scrollback and break theme switching (the 2026-09-18 regression), so the plan uses the theme's indexed magenta/blue/red instead — say the word if you'd rather have exact colours regardless of theme.

<!-- relay:entry 20260919T043613Z-0z author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260919T043613Z-ft author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → In progress · Execute: handed to a terminal pane

<!-- relay:entry 20260919T043613Z-fu author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260919T043638Z-ey author=agent kind=event model=glm-5.3 pane=5b9e3f00 turn=cc23e1827ecb4aba8bb77bb7b79fd5db/0a41cad2ed1b497987c267fed75cba43 -->
- ✦ agent updated this card · implemented_by: (unset) → claude

<!-- relay:entry 20260919T044959Z-by author=agent kind=progress model=deepseek/deepseek-v4.1-flash pane=5b9e3f00 turn=cc23e1827ecb4aba8bb77bb7b79fd5db/704af3ec295e46b487fb1a60832768df -->
Implemented all four steps. Prompt: one new line in `SYSTEM` (`backend/relay_core/agent.py`) teaching the `**Done:**` / `**Problem:**` / `**Need:**` labels. Palette: `done = "1;35"`, `need = "1;34"`, `problem = "1;31"` (indexed, as the plan decided). Renderer: a bold run's first word is held (bounded at 32 chars) and classified case-insensitively, colon and punctuation stripped, against the three closed word lists; a match colours the whole run, anything else stays plain bold, and the hold is flushed at a marker, a line end, `finish()` and `resetInline()`. Tests: `tests/markdownansi_test.cpp` (`labelledBoldsAreColoured`, the three new palette fields, labelled bold in the streaming corpus) and `tests/test_agent.py::SystemPromptTests`. `relay-markdown-tests` passes 14/14; `tests/test_agent.py` passes 30/30. Running the full `./scripts/test.sh` and `ctest` plus the live Xvfb check now — the suite already shows unrelated failures from the in-flight plan-mode work in this worktree (`test_roles.py` "planning", `test_sessions.py` compaction), which I will keep out of this card's commit.

<!-- relay:entry 20260919T050804Z-cm author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=5b9e3f00 turn=cc23e1827ecb4aba8bb77bb7b79fd5db/704af3ec295e46b487fb1a60832768df -->
- ✦ agent updated this card · replaced `## QA checklist`

<!-- relay:entry 20260919T050812Z-j1 author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=5b9e3f00 turn=cc23e1827ecb4aba8bb77bb7b79fd5db/704af3ec295e46b487fb1a60832768df -->
- ✦ agent moved this card · In progress → Needs QA (LLM) · Implemented and verified: prompt line + labelled bold colours in MarkdownAnsi, unit and live Xvfb evidence in docs/qa_evidence/2026-09-19-bolding-main-points/ · evidence docs/qa_evidence/2026-09-19-bolding-main-points/

<!-- relay:entry 20260919T050845Z-j2 author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=5b9e3f00 turn=cc23e1827ecb4aba8bb77bb7b79fd5db/704af3ec295e46b487fb1a60832768df -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-19-bolding-m… → {"commits": ["168f6ed"], "evidence": ["docs/qa_evidence/2026-09-19-bolding-main-…

<!-- relay:entry 20260919T050854Z-gb author=agent kind=evidence model=deepseek/deepseek-v4.1-flash pane=5b9e3f00 turn=cc23e1827ecb4aba8bb77bb7b79fd5db/704af3ec295e46b487fb1a60832768df -->
Commit `168f6ed` — "Labelled bolds: the reply's main point is bold, and Done / Problem / Need are coloured (#CVHT)". Only this card's hunks are in it: the worktree carries other uncommitted work (plan-mode routing in `agent.py` among it), so the `agent.py` change was staged as a one-line patch against `HEAD` and the staged file verified equal to `HEAD` + that line. Checks: `relay-markdown-tests` 14/14, `tests/test_agent.py` 30/30, `ctest` 53/54 (the one failure is `backend-and-bash` on four cases that already fail on `HEAD`: `test_roles` planning, `test_sessions` compaction, `test_remote_wire` `plan_route`/`question`). Live Xvfb run in `docs/qa_evidence/2026-09-19-bolding-main-points/`: the labels come out magenta/blue/red (the palette's bright 13/12/9, since `1;35` is bold plus an index) in three separate row bands, the unlabelled `**Bold**` stays in the foreground, and the same ANSI bytes render in a user theme's own `terminal.palette` — no burnt-in RGB.
