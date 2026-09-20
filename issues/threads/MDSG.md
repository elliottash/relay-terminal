<!-- relay:entry 20260920T180002Z-c3 author=claude-code kind=event -->
### Claude Code · 2026-09-20 18:00
created this card in Planned from the #PF4K profile: measured findings and the proposed fix; evidence in docs/qa_evidence/2026-09-20-perf-profile/

<!-- relay:entry 20260920T190501Z-a1 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 19:05
claimed this card (#PF4K implementer, land session `pf-panes`); starting on finding 4 in src/Conversations.cpp (the row delegate's per-row QTextDocument) and finding 5 in src/FilePanes.cpp (the synchronous KSyntaxHighlighting rehighlight). No other file in src/ is mine.

<!-- relay:entry 20260920T222000Z-b2 author=claude-code kind=evidence -->
### Claude Code · 2026-09-20 22:20
landed both fixes and moved the card to needs-verification. Sessions, one keystroke: 392 → 62 ms GUI CPU
with the rows unfolded, 37 → 4.4 ms collapsed (Xvfb/xcb; offscreen 127 → 12 and 36 → 4.1), and the same
page rendered by the before and after builds is byte-identical. File panes, a 1 MB src/Pane.h: 203 → 51 ms
before the text is on screen; a 300-line file is unchanged. The delegate's QTextDocument was only a third
of the sessions cost — the rest was Qt re-measuring the three ResizeToContents columns over every row, once
per setFirstColumnSpanned() and once per setExpanded(). Numbers, commands and the harness in
docs/qa_evidence/2026-09-20-perf-fixes/panes/MEASUREMENTS.md; new tests in tests/conversations_test.cpp and
tests/filepanes_test.cpp, and two benches that are skipped unless RELAY_PERF_BENCH=1.

<!-- relay:entry 20260920T223200Z-c1 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 22:32
landed as 93599e123c237cb479aaf2810cf0ccc42af59ddb (12 files, land session `pf-panes`); recorded on the card.
