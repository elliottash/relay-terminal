<!-- relay:entry 20260923T050000Z-c1 author=claude-code kind=decision -->
### Claude Code · 2026-09-23 01:00
Owner: "when changing between models, i would say, compact if there are more than 128K tokens in the context". Implemented as a preference on top of the existing fit rule: it compacts, but never blocks the switch when the compaction cannot run.
