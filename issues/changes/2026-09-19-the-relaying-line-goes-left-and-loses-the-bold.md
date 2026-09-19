---
id: HQ2B
type: work
status: ready
labels: [change]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
rank: zzzzzy
created: '2026-09-19'
acceptance: the "Relaying …" line above the prompt box is left-aligned with the prompt text and drawn in the normal weight, in every state it can show (agent turn, subagents, terminal program, blocked on a question)
source: 'issues/feature_intake.txt, 2026-09-19: "the \"Relaying -- [action]. . .\" should be at the left and above the prompt box, more like how warp . claude does it. and not in bold."'
links: {plans: [], commits: [], evidence: [], related: [4E13, V8KT], github: null}
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

- [ ] `PaneBusyLine::paintEvent()`: draw `Qt::AlignLeft | Qt::AlignVCenter` and drop the DemiBold <!-- t:p2 -->
      weight; keep the state ink and the middle elision
- [ ] Line the left edge up with the prompt text rather than with the frame, so the row above the <!-- t:q9 -->
      box and the box itself share one left margin — that is the "like warp / claude" part of the
      request, and a left-aligned line that starts two pixels further left than the prompt reads as
      a mistake
- [ ] `sizeHint()` measures with the same font the paint uses, so the row's height and width do not <!-- t:r5 -->
      come from a weight that is no longer drawn
- [ ] Update the two comments that describe the line as bold and right-aligned: `src/Pane.h:238-244` <!-- t:s8 -->
      ("The bold \"Relaying…\" line"), `src/Pane.h:3046-3051` ("right-aligned above the prompt") and
      `src/PaneStatus.h:17` ("The same word sits bold above the prompt box")
- [ ] Check every state the line can show under Xvfb — agent turn, `waiting for N subagents…`, a <!-- t:t3 -->
      terminal program, and blocked on a question (amber) — and a pane narrow enough to elide

## Decisions

- 2026-09-19, agent: only the line above the prompt box moves. The pane header's live state word
  (`PaneStateWord`, `src/PaneChrome.h:883`) says the same word beside the title and is a different
  surface with its own placement; the request names the one above the prompt box.
