<!-- relay:entry 20260920T180002Z-c3 author=claude-code kind=event -->
### Claude Code · 2026-09-20 18:00
created this card in Planned from the #PF4K profile: measured findings and the proposed fix; evidence in docs/qa_evidence/2026-09-20-perf-profile/

<!-- relay:entry 20260920T191200Z-b1 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 19:12
claimed this card (#PF4K implementer, land session `pf-toolout`); item 1 of the plan, the `set_agent_options` flag for tool output. Item 2 (the fold-anchor walks in `engine/`) is the engine implementer's, land session `pf-engine`. Item 3 (the ~70 ms turn-start hitch) is mine to diagnose.

<!-- relay:entry 20260920T194600Z-b2 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 19:46
item 1 landed as `9702e504`: protocol 23.10, `stream_tool_output`. Worker→GUI bytes for a tool-heavy turn 1 642 498 → 316 587 (−80.7 %), per tool call 66 829 B → 551 B, GUI CPU for the 200-call scenario 6.95 s → 5.69 s over three alternating rounds (−18.1 %). Item 2 is `b8e91fe3` (#6W0Z). Item 3 turned out not to be the fold layer: a probe that samples *inside* the event-loop gap — the published one could only measure its length — puts 75 % of the samples in `models::shown()` asking `curation::isShown()` for a `QSettings` per catalog entry, from `Pane::refreshPickers()` on `changed()`. Fixed in `src/ModelCatalog.cpp` by reading the curated list once; that hunk went out in `b42c24f7`, which the model-picker session landed on the same file while this was in review. Evidence, the commands and the stacks: docs/qa_evidence/2026-09-20-perf-fixes/toolout/.
