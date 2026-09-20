<!-- relay:entry 20260920T180002Z-c3 author=claude-code kind=event -->
### Claude Code · 2026-09-20 18:00
created this card in Planned from the #PF4K profile: measured findings and the proposed fix; evidence in docs/qa_evidence/2026-09-20-perf-profile/

<!-- relay:entry 20260920T190200Z-d1 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 19:02
claimed this card (session pf-worker, one of the six #PF4K implementers). Taking findings 1, 2 and 3: the
per-request urllib opener in provider.py and every other module that builds one per request, byte-compiling
the installed backend, and the incremental conversation index. Finding 4 (the eager imports in
board_protocol.py) is the #7M6E implementer's file and is theirs to do.
