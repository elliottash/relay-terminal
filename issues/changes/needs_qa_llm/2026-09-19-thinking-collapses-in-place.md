---
id: T8CN
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Oz (Warp) agent session, 2026-09-19
rank: zzzz103
created: '2026-09-19'
acceptance: thinking streams expanded in place with markdown, auto-collapses to a one-line 'Thought · N s' header on done unless the user interacted, and agent prose reads brighter with more breathing room
source: 'conversation, 2026-09-19: "examine how warp terminal shows the thinking tokens. it takes up more of the pane and is kind of dynamic. it uses markdown as well. its nice and readable. see what we can learn from that"'
links: {plans: ['2fc44c93-2cf1-40ac-ad43-2e3d7abbe4c2'], commits: [], evidence: [docs/qa_evidence/2026-09-19-thinking-fold/], related: [], github: null}
---
# Thinking presentation: collapse in place, markdown, breathing room

## Issue
"examine how warp terminal shows the thinking tokens. it takes up more of the pane and is kind of dynamic. it uses
markdown as well. its nice and readable. see what we can learn from that"

"yes file the issue with the logic, then protoptype it. also, take a look at how warp terminals look to see what we can
learn from that: they have more space. higher contrast colors, they somteimes have boxes for further contrast. its
better. we dont have to copy it but i want to learn from it. i like how warp gives more breathing room. we need the
agent messages to be brighter white for example."

## How Warp does it (as seen in the product)

- Reasoning is a message in the output stream, rendered inline in the conversation flow.
- Header: "Thinking" while streaming, "Thought for {elapsed}" when done, chevron right/down, muted.
- Body: the same markdown rendering as the answer, only in a disabled-text colour and without code
  buttons. Thinking is real markdown — bold, lists, code tint — muted.
- Height: a clipped internal scroll view, about 120 px while streaming and about 360 px when done,
  scroll pinned to the bottom; any mouse-wheel unpins.
- Starts expanded; on finish it auto-collapses only if the user never toggled it while streaming;
  user interaction always wins.
- A setting offers show-and-collapse (default), always show, never show.
## Tasks
- [x] Thinking streams expanded by default (capped height, internal scroll) instead of 2-line compact
- [x] On `thinking_done`: collapse in place to one-line header "Thought · N s ⌄ · model" (unless user toggled while
      streaming); header reopens the text
- [x] `user_toggled_while_streaming` flag on ▴/×; interaction disables auto-collapse
- [x] Thinking text through MarkdownAnsi (muted note ink) instead of plain QPlainTextEdit
- [x] Three-way setting replacing `agent/show_thinking` bool: show-and-collapse (default) / always / never
- [x] Agent prose ink brighter (higher-contrast white); spacing/breathing-room pass per plan
- [x] Validation: scripts/test.sh + ctest + Xvfb live run; QA evidence

## Decisions
- 2026-09-19, owner: learn from Warp's look (space, contrast, boxes) without copying it; combined plan covers thinking
  presentation and overall terminal presentation.
- 2026-09-19, agent: plan 2fc44c93 supersedes the panel-based task framing above — mechanism is an in-grid streaming
  fold on the existing fold layer (anchor `relay://call/<pane>/<turn>/thinking`, markdown via MarkdownAnsi→FoldLine
  spans, auto-collapse unless user toggled, three-way `agent/thinking_display`); the QFrame bubble is deleted. Tasks
  read with that mechanism.
- 2026-09-19, agent, implementation: landed as `src/Pane.h` `thinkingDelta`…`finishThinkingFold` (anchor row at
  column 0, ~4 Hz coalesced flushes that respect a reader's click-shut, in-place rewrite to `▸ ✦ thought for N s`,
  per-block anchors `thinking`, `thinking-2`, … for interleaved reasoning) on
  `calllines::foldForMarkdown` (`src/CallLines.{h,cpp}`, MarkdownAnsi base SGR 39→97); bubble machinery removed;
  `agent/thinking_display` migrates the old bool in place; Alt+R retargeted to the latest fold with speaking
  refusals; spacing `LineSpacing` 2→5, `TerminalMargin` 12→16 in `data/theme/terminal.conf`.
- 2026-09-19, agent, drive-by fixes the QA run exposed: (1) `backend/relay_core/provider.py::_stream` closed thinking
  state when content began but never reopened it when reasoning resumed, so a GLM-style interleave never sent the
  second block's `thinking_done` — `finish_thinking()` now zeroes `thinking_chars` and a resumed block restarts the
  clock; regression test
  `tests/test_routing_thinking_skills.py::ThinkingStreamTests::test_reasoning_resuming_after_the_answer_gets_its_own_done`.
  (2) `backend/relay_core/forge_github.py::_run` now runs git with `GIT_TERMINAL_PROMPT=0` and
  `GIT_ASKPASS=/bin/false` so a missing credential fails instead of prompting on the tty (root cause of prompt noise
  in test logs), same idiom as `skill_manage._git_env`.

## Implemented

- `src/CallLines.{h,cpp}`: `foldForMarkdown` — MarkdownAnsi feed+finish converted to `FoldLine` spans; unit-tested in
  `tests/calllines_test.cpp`. `src/MarkdownAnsi.h` base SGR 97.
- `src/Pane.h`: the reasoning fold (`thinkingDelta`, `printThinkingAnchor`, `queueThinkingFlush`, `flushThinkingFold`,
  `thinkingFoldLines`, `finishThinkingFold`, `rewriteThinkingAnchor`), `thinkingDisplay()` with in-place migration,
  `toggleThinkingPanel` (Alt+R) with toasts, `thinking.fold` shortcut hint, pane_state feeds the phone the same bytes;
  the thinkingOverlay/thinkingView QFrame and its sizing are deleted.
- `backend/relay_core/provider.py`: second reasoning block of an interleaved stream gets its own `thinking_done`;
  regression test in `tests/test_routing_thinking_skills.py`.
- `backend/relay_core/forge_github.py`: git subprocess env guard (no tty credential prompts).
- `data/theme/terminal.conf`: `LineSpacing 5`, `TerminalMargin 16`.
- Docs: `docs/ENGINE.md` ("The reasoning fold"), `docs/ARCHITECTURE.md` §8 thinking paragraph,
  `docs/SWITCHBOARD-AESTHETIC.md` busy-lamp rationale, `docs/F-KEYS.md` F2 row.
- Tests: `./scripts/test.sh` 2 554 green; `ctest --test-dir build` 55/55; Xvfb live run over collapse / always /
  never / migration / two-block / 5-theme scenes.
- Evidence: `docs/qa_evidence/2026-09-19-thinking-fold/` (stub SSE provider, `drive.sh` scenes, screenshots,
  implementer notes).

## QA checklist

- [ ] Default (`collapse`): the first reasoning delta opens the fold under a `▸ ✦ thinking…` row; the text renders as
      markdown in muted ink and streams; on done the fold collapses and the row is rewritten in place to
      `▸ ✦ thought for N s`; clicking the row (or Alt+R) reopens the finished text.
- [ ] Folding it shut mid-stream (click or Alt+R) keeps it shut for the rest of the block and at done — the stream
      never reopens it over the reader's click.
- [ ] `always`: the fold stays open at done. `never`: no fold at all, only the `✦ thought for N s` line; a settings
      file with `show_thinking=false` migrates to `thinking_display=never` with the old key removed.
- [ ] A model that interleaves (reasoning → answer → reasoning again) gets two anchor rows, each with its own
      finished fold.
- [ ] Alt+R with no reasoning yet, and after the anchor has scrolled out, toasts rather than doing nothing.
- [ ] Every shipped theme: the fold keeps its tint and rule, agent prose reads brighter, and the terminal has the
      new line spacing and margin.
- [ ] The turn pane (Ctrl+click on the summary line) still holds the whole reasoning of every block whatever the
      display mode.
