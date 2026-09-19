---
id: HQ2B
type: work
status: needs-qa-llm
labels: [change]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: claude-opus-4-5
rank: zzzzzy
created: '2026-09-19'
acceptance: the "Relaying …" line above the prompt box is left-aligned with the prompt text and drawn in the normal weight, in every state it can show (agent turn, subagents, terminal program, blocked on a question)
source: 'issues/feature_intake.txt, 2026-09-19: "the \"Relaying -- [action]. . .\" should be at the left and above the prompt box, more like how warp . claude does it. and not in bold."'
links: {commits: [7a5221b], evidence: [docs/qa_evidence/2026-09-19-relaying-line-left-normal/], github: null, plans: [], related: [4E13, V8KT]}
---
# The "Relaying …" line belongs on the left, in the normal weight

## Issue

the "Relaying -- [action]. . ." should be at the left and above the prompt box, more like how warp .
claude does it. and not in bold.

## Reproduced (2026-09-19, live under Xvfb)

A turn against a scripted model that takes 9 s per call. While it runs, the first row of the
composer frame reads

```
                                       Relaying thinking… · 3 s · step 1/256 · Esc stops
```

— hard against the **right** edge of the composer, in **DemiBold**, while the prompt text below it
starts at the left. The terminal-work spelling behaves the same: with a shell command running the
line is `Relaying sleep…`, also right-aligned and bold, in the terminal's blue.

So both halves of the request are live behaviour, not a misremembering: the line is already above
the prompt box (`composerLayout->addWidget(m_busyLine)` is the first row of the composer frame,
`src/Pane.h:3046`), and what is wrong is the alignment and the weight.

**Where it is drawn.** `PaneBusyLine`, `src/Pane.h:245-292`. `paintEvent()` sets
`bold.setWeight(QFont::DemiBold)` (`:280`) and draws with `Qt::AlignRight | Qt::AlignVCenter`
(`:286`); `sizeHint()` measures with the same bold font (`:267-270`). The colour is the state's own
ink and stays — it is what says whose work it is (agent violet, terminal blue, amber when the turn
is blocked on an answer) and nothing in the request touches it.

## Tasks

- [x] paintEvent: Qt::AlignLeft, no DemiBold; state ink and middle elision kept <!-- t:p2 -->
      weight; keep the state ink and the middle elision
- [x] Left edge on the prompt text's edge, not the frame's — inset read live from the editor <!-- t:q9 -->
      box and the box itself share one left margin — that is the "like warp / claude" part of the
      request, and a left-aligned line that starts two pixels further left than the prompt reads as
      a mistake
- [x] sizeHint measures with the font the paint uses <!-- t:r5 -->
      come from a weight that is no longer drawn
- [x] Comments updated: Pane.h (three places) and PaneStatus.h no longer say bold / right-aligned <!-- t:s8 -->
      ("The bold \"Relaying…\" line"), `src/Pane.h:3046-3051` ("right-aligned above the prompt") and
      `src/PaneStatus.h:17` ("The same word sits bold above the prompt box")
- [x] Every state checked under Xvfb: agent turn, subagents, terminal program, amber question, elision <!-- t:t3 -->
      terminal program, and blocked on a question (amber) — and a pane narrow enough to elide

## Decisions

- 2026-09-19, agent: only the line above the prompt box moves. The pane header's live state word
  (`PaneStateWord`, `src/PaneChrome.h:883`) says the same word beside the title and is a different
  surface with its own placement; the request names the one above the prompt box.

## QA checklist
- [x] Every state captured live under Xvfb (relay-dark, isolated HOME, loopback stub model): agent turn (violet "Relaying thinking… · 6 s · step 1/256 · Esc stops"), subagents (violet "Relaying waiting for 1 subagent…"), terminal program (blue "Relaying sleep…"), blocked on a question (amber "Relaying waiting for your answer… · 7 s · Esc stops"), idle (no line at all)
- [x] Left edge on the prompt text's pen origin in every state: the busy row's ink starts at x=38 in all captures, and the prompt's own caret (running scene) stands at x 36–37 — one glyph bearing left of the line's capital R, i.e. the same origin (|Δ ink| ≤ 2 px; measure.py, analysis.txt)
- [x] Normal weight: the painter sets no font weight any more; the captured word "Relaying" has ink ratio 0.205 against 0.243 (fc-matched UI font, normal) and 0.396 (demibold)
- [x] Middle elision kept: in a 420 px window "Relaying running sleep 120 && echo padding…" keeps its head and the "Esc stops" tail and still starts on the prompt's edge
- [x] sizeHint measures with the painted font, plus the left inset
- [x] PaneStateWord (the pane header) untouched — still DemiBold, per this card's decision
- [x] ctest 56/57 on the tree carrying the change; backend-and-bash timed out on the known git-credential prompt hang (environment, not an assertion; retry stopped at the owner's "no tests until all tasks are done"). The commit itself was compiled in an isolated git worktree at the pinned HEAD, where panestate / pulsepaint / editor also passed
- Evidence: docs/qa_evidence/2026-09-19-relaying-line-left-normal/ (README, captures per state, analysis.txt, driver + measurement scripts, per-scene logs)
