<!-- relay:entry 20260919T184251Z-pf author=owner kind=event pane=switchboard -->
- ✦ owner created this card in In progress · issues/features/2026-09-19-remove-the-one-half-spacing-in-the-terminal-text.md

<!-- relay:entry 20260919T184300Z-g8 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · In progress → Inbox · changed in the Switchboard

<!-- relay:entry 20260919T184318Z-w5 author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260919T184318Z-w6 author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
remove the one-half spacing in the terminal text.
```

</details>

<details><summary>after</summary>

```
remove the one-half spacing in the terminal text. it should be single-spacing (while preserving the spacing between paragraphs etc)
```

</details>

<!-- relay:entry 20260919T200305Z-xv author=agent kind=event model=glm-5.3 pane=switchboard turn=e17598d07151419d86ddfea89de60412/c1c5a69cae214f50bedb9384a2802d81 -->
- ✦ agent updated this card · labels: (unset) → ["bug", "terminal"]

<!-- relay:entry 20260919T201002Z-kq author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260919T201350Z-sy author=agent kind=event model=glm-5.3 pane=switchboard turn=894bdf58cf424945b512ecae0025f8a4/e63d67ab96454908bd7fad87553bf5be -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260919T201356Z-2q author=agent kind=comment mode=plan model=glm-5.3 turn=894bdf58cf424945b512ecae0025f8a4/e63d67ab96454908bd7fad87553bf5be -->
The extra spacing comes from one shipped value: `LineSpacing=5` in `data/theme/terminal.conf`, read by both the terminal engine (`src/EngineBackend.cpp` → `TerminalView::setLineSpacing`, folded into the cell height in `updateMetrics()`) and the remote-pane viewer (`src/RemotePane.cpp` `terminalFont()`). Paragraph gaps are blank grid rows, so they're untouched. Writing the plan now.







The hash moved between reads (the file was touched); retrying immediately with the fresh one.

Plan written to #T7AH. The one-and-a-half look is a single value — `LineSpacing=5` in `data/theme/terminal.conf`, which both the terminal engine and the remote-viewer read and add to every row — so the plan is a one-line change to `LineSpacing=0`, with paragraph gaps (blank grid rows) untouched by construction. The one thing to watch when executing: an installed `terminal.conf` can shadow the repo's copy, so the verify step runs with `RELAY_THEME_DIR` pointed at the source tree.

<!-- relay:entry 20260919T201637Z-x1 author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260919T201637Z-x2 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → In progress · Execute: handed to a terminal pane

<!-- relay:entry 20260919T201637Z-y6 author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260919T201652Z-vh author=agent kind=progress model=glm-5.3 pane=6819a1a8 turn=46a45a61ae0b4cd2b7b7fa11d3558b72/6fc8cfbd41534f128b9c83f27009383b -->
Executing the plan: staging `data/theme/terminal.conf` through `scripts/land.py begin`, then the one-line `LineSpacing=5 → 0` edit, then tests + Xvfb live check before commit.

<!-- relay:entry 20260919T210935Z-st author=agent kind=event model=glm-5.3 pane=17136521 turn=46a45a61ae0b4cd2b7b7fa11d3558b72/047e349aedad4d62b35b339319d4e6ff -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260919T210935Z-su author=agent kind=progress model=glm-5.3 pane=17136521 turn=46a45a61ae0b4cd2b7b7fa11d3558b72/047e349aedad4d62b35b339319d4e6ff -->
Executed. The one-line edit is in (`data/theme/terminal.conf` `LineSpacing=0`), and the live Xvfb A/B measured it: row pitch 22.0px → 17.0px (exactly the removed 5px, constant across a 9-row run in both captures) and the paragraph gap stayed one full blank row (2.00 pitches) in both. Suites: test.sh green; ctest 62/63 with the `buttonfit` failure pre-existing at HEAD and filed as #QAJQ. Evidence in `docs/qa_evidence/2026-09-19-single-spaced-terminal-rows/`. Committing now.

<!-- relay:entry 20260919T210939Z-hb author=agent kind=event model=glm-5.3 pane=17136521 turn=46a45a61ae0b4cd2b7b7fa11d3558b72/047e349aedad4d62b35b339319d4e6ff -->
- ✦ agent moved this card · In progress → Needs QA (LLM) · One-line conf change landed and measured on screen (pitch 22→17px, paragraph gap intact); suites green except a pre-existing buttonfit failure filed as #QAJQ. · evidence docs/qa_evidence/2026-09-19-single-spaced-terminal-rows/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260919T211107Z-8k author=agent kind=event model=glm-5.3 pane=17136521 turn=46a45a61ae0b4cd2b7b7fa11d3558b72/047e349aedad4d62b35b339319d4e6ff -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-19-single-sp… → {"commits": ["8408b58b"], "evidence": ["docs/qa_evidence/2026-09-19-single-space…
