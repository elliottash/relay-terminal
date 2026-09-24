---
id: TJBC
type: work
status: planned
rank: zzzzzzzzzzzzzzr
created: '2026-09-19'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# line spacing adjustment

## Issue
example trace summary from a terminal pane:



▸ ✦ thought for 10 s

▸ ran cat +9 · 7 lines · exit 0

▸ ✦ thought for 22 s
The app exited (my blind click likely closed the last pane). Relaunching and driving precisely with OCR-derived coordinates:

▸ ran relay +6 · 2 lines · exit 0 · 7.6 s
▸ ran xdotool +10 · 4 lines · exit 0 · 8.9 s

▸ ✦ thought for 5 s

▸ ran xdotool +4 · 26 lines · exit 0 · 1.8 s

▸ ✦ thought for 5 s

▸ ran tesseract +2 · 20 lines · exit 0

▸ ✦ thought for 6 s
Rows are exactly as designed. Picking Flash and verifying the write + reconfigure:

▸ ran tesseract +7 · 12 lines · exit 0 · 6.7 s

▸ ✦ thought for 7 s

▸ ran convert +8 · 6 lines · exit 0

this has two things i want to shift:

need an extra line break before agent-to-user messages (in between tools / thoughts and messages)

no extra line break between tools and thoughts (same as you see between two consecutive two calls)

## Plan
**Goal.** In a terminal pane's agent trace, `▸ ✦ thought for N s` rows behave like `▸ ran …` tool rows: no blank line between a thought row and a tool row (either order), and a blank line before the agent's prose messages. Today the thought rows are classified as agent prose, so they take a blank line against tool rows and none against the prose that follows them — the opposite of the card.

**Findings.**
- The blank-line rule is `src/TranscriptGaps.h` (`relay::gaps::Block`, `gapBefore`): a blank line between blocks of different kinds, none within one kind. `Pane::beginBlock` (`src/Pane.h:11543`) prints it and remembers `m_lastBlock`.
- Thinking rows are begun as `Block::Agent` in exactly two places:
  - `src/Pane.h:4373` — the live fold start (`▸ ✦ thinking…`, later rewritten in place to `▸ ✦ thought for N s` by `finishThinkingFold`, ~4486–4503, which begins no new block);
  - `src/Pane.h:4749` — `printAnchoredRow`'s `beginBlock(ink == Ink::Note ? Block::Agent : Block::Call)`; its only caller (4735) passes `Ink::Note` exactly for replayed thinking rows.
- Prose is `Block::Agent` (`printInline`'s ink dispatch, 11423–11426); tool rows are `Block::Call` (9235). Hence today: thought↔tool = blank (Agent↔Call), thought→prose = none (Agent→Agent) — matching the card's trace.
- The gap table itself needs no change: reclassifying thinking rows to `Call` gives exactly the asked-for spacing via the existing rule (`Call`→`Call` none; `Call`→`Agent` blank).
- Legacy fallback: `thinking_done` with no fold prints a bare ✦ Note line (`src/Pane.h:4252`); `Ink::Note` begins no block in `printInline`, so it inherits whatever printed before.

**Steps.**
1. `src/Pane.h:4373`: begin the fold as `beginBlock(relay::gaps::Block::Call);` and update the comment — thought rows sit with the tool rows (#TJBC).
2. `src/Pane.h:4749`: `beginBlock(relay::gaps::Block::Call);` — drop the Note→Agent ternary (Note ink there is only ever thinking rows).
3. `src/Pane.h:~4252`: begin `Block::Call` before the legacy single-✦ `printInline`, so the `never` display mode matches too.
4. Comment touch-ups only: `src/TranscriptGaps.h`'s `Block::Call` line (add the ✦ thought row to its list) and the rule paragraph in `docs/ARCHITECTURE.md` ~1055–1057.

**Risks.**
- Prose → thought row now takes a blank line too (symmetric with prose → tool row): the rule's consequence, matching "in between tools / thoughts and messages".
- A streaming fold is Call-classified from birth; the settle rewrite is in place, so nothing shifts when the row settles. No entanglement with the call-run cursor — the fold row is written directly, not through `m_callCursor`.
- Other panes (AgentInternalsView, SubagentTranscript, BoardChat) space their own traces; out of scope.
- No headless test drives the pane's fold spacing today; verification is visual.

**Verify.**
- `scripts/relay-build`, then `ctest --test-dir build -R transcriptgaps` (table unchanged; must stay green).
- Live under Xvfb with an isolated `XDG_CONFIG_HOME`, reusing the #5AWD harness (`docs/qa_evidence/2026-09-19-helpful-line-breaks/stub-provider.py` + `drive.sh`; folds: `docs/qa_evidence/2026-09-19-thinking-fold/drive.sh`): a stub turn emitting thinking → tools → thinking → prose → tools. Screenshots must show `▸ ✦ thought for N s` tight against `▸ ran …` rows in both orders, and one blank line before the prose message.
- Evidence under `docs/qa_evidence/2026-09-20-<slug>/`; land through `scripts/land.py begin/commit`; card to needs-verification with a QA checklist.
