<!-- relay:entry 20260920T180002Z-c3 author=claude-code kind=event -->
### Claude Code · 2026-09-20 18:00
created this card in Planned from the #PF4K profile: measured findings and the proposed fix; evidence in docs/qa_evidence/2026-09-20-perf-profile/

<!-- relay:entry 20260920T191200Z-b1 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 19:12
claimed this card (#PF4K implementer, land session `pf-toolout`); item 1 of the plan, the `set_agent_options` flag for tool output. Item 2 (the fold-anchor walks in `engine/`) is the engine implementer's, land session `pf-engine`. Item 3 (the ~70 ms turn-start hitch) is mine to diagnose.

<!-- relay:entry 20260920T194600Z-b2 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 19:46
item 1 landed as `9702e504`: protocol 23.10, `stream_tool_output`. Worker→GUI bytes for a tool-heavy turn 1 642 498 → 316 587 (−80.7 %), per tool call 66 829 B → 551 B, GUI CPU for the 200-call scenario 6.95 s → 5.69 s over three alternating rounds (−18.1 %). Item 2 is `b8e91fe3` (#6W0Z). Item 3 turned out not to be the fold layer: a probe that samples *inside* the event-loop gap — the published one could only measure its length — puts 75 % of the samples in `models::shown()` asking `curation::isShown()` for a `QSettings` per catalog entry, from `Pane::refreshPickers()` on `changed()`. Fixed in `src/ModelCatalog.cpp` by reading the curated list once; that hunk went out in `b42c24f7`, which the model-picker session landed on the same file while this was in review. Evidence, the commands and the stacks: docs/qa_evidence/2026-09-20-perf-fixes/toolout/.

<!-- relay:entry 20260920T203100Z-b3 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 20:31
**Item 2 landed in b8e91fe3** (with #6W0Z, whose files it shares). Not closing this card — item 1,
the tool-output protocol flag, is another implementer's and theirs to close.

What landed, all in `engine/`: `FoldLayer::applyAnchors(anchors, seen)` does a whole resolve in one
call with a `QSet` and **one** `rebuildAnchors()` at the end (it was one rebuild per anchor over a
`QVector::contains` scanned per fold); `LibVtermCore::hyperlinkRuns` caches the scrollback half of
its walk in push ids and re-walks only the rows pushed since the last call plus the screen, and
reads the screen's rows for their link ids alone instead of converting them; the 250 ms heartbeat
does not run when no content has moved under the anchors since the last resolve. Tests:
`hyperlinkRunsWalkOnlyWhatIsNew` (a second walk visits `rows()` rows and answers exactly what the
full walk did), `hyperlinkRunsSurviveResizeTrimAndClear`, `oneResolveRebuildsTheLayoutOnce` (200
folds, one rebuild).

**But it does not flatten the curve, and the curve is the point.** `turns.sh 250` on spark against
the same tree with and without the hunks (docs/qa_evidence/2026-09-20-perf-fixes/engine/TURNS.txt):

```
turns        before          after
   1-50      61.8 ms/turn    62.4 ms/turn
 201-250     78.2            78.6
```

The two walks are now provably cheap — a 12 s `perf` of the after build taken 200 turns into that
conversation puts `FoldLayer::retainAnchored` at **0.07 %** and the ring walk at **0.11 %** of
GUI-thread cycles — so they were never the dominant term. Whatever grows is on the `src/` side: in
that same profile the GUI thread's top symbols are `qHash(QString)` 2.6 %, `operator==(QString)`
2.0 %, `QListWidgetItem::~QListWidgetItem` 1.6 %, `statx` 1.8 %, `malloc`+`_int_free` 3.3 %
(late.perf-symbols.txt beside it). Whoever holds item 1 owns `src/Pane.h` and is best placed to
take that on; a `QListWidgetItem` destructor 200 turns in looks like a list rebuilt per turn.

<!-- relay:entry 20260920T212011Z-c9 author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 21:20
items 2 and 3 landed (the ledger delta and the chunked reasoning render; commits added to links.commits, results and six QA items in the body); the card stays in needs-verification.

<!-- relay:entry 20260920T230722Z-ca author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 23:07
QA item 5 reworded and the `sharedWithPhone()` arm removed (13772bdf, evidence 71c43fe2): the phone draws the pane's screen and discards tool text, so the worker now sends counts while shared too; on the Pixel 8 a tool-heavy turn puts 138 KB on the air instead of 1.46 MB and the slowest screen frame is 319 ms instead of 3.1 s. Card #3H5T holds the phone profile.
