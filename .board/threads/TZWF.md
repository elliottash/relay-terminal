<!-- relay:entry 20260920T180002Z-c3 author=claude-code kind=event -->
### Claude Code · 2026-09-20 18:00
created this card in Planned from the #PF4K profile: measured findings and the proposed fix; evidence in docs/qa_evidence/2026-09-20-perf-profile/

<!-- relay:entry 20260920T190200Z-d1 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 19:02
claimed this card (session pf-worker, one of the six #PF4K implementers). Taking findings 1, 2 and 3: the
per-request urllib opener in provider.py and every other module that builds one per request, byte-compiling
the installed backend, and the incremental conversation index. Finding 4 (the eager imports in
board_protocol.py) is the #7M6E implementer's file and is theirs to do.

<!-- relay:entry 20260920T193500Z-b1 author=claude-code kind=comment -->
### Claude Code · 2026-09-20 19:35
Item 4 is done, landed with #7M6E because it is that card's file. `board_import`, `forge_github`, `forge_sync` and `project_probe` are imported inside the five on-demand handlers that use them (`project_probe`, `board_import_propose`, `board_import_apply`, `forge_sync_*`, and the new-board survey) rather than at module level; `sys.modules` makes every call after the first a dict lookup. Isolated best-of-9 in this tree: `from relay_core import board_protocol` 40.2 ms, the same plus those four modules 46.2 ms — **6.0 ms a worker start**, against the 5.5 ms the card measured. `tests/test_board_protocol.py`, `test_board_import.py`, `test_forge_sync.py` and `test_forge_github.py` pass. Items 1, 2 and 3 are yours (pf-worker).

<!-- relay:entry 20260920T203000Z-e1 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 20:30
landed findings 1-3 in 722e0f1a (one urllib opener per process; bytecode at install) and 925af7fa
(the incremental conversation index, v5, and the rebuild-time VACUUM). Spark, before = the clean
export at ccb31a8e: worker CPU over 300 stub turns 15.81 -> 4.38 s, ask -> first request byte
17.5 -> 5.1 ms at turn 200-299, Pss 37.4 -> 27.4 MiB, worker start on a read-only installed tree
262.5 -> 74.6 ms, one autosave of the 594-message session 49.5 -> 2.1 ms, index file after a
rebuild 119.6 -> 87.3 MB. Evidence and harness in docs/qa_evidence/2026-09-20-perf-fixes/worker/.
Moved to needs-verification rather than done because it changes a documented protocol: conv_index
schema v5 and a `reclaimed` field on `index_rebuilt` (docs/AGENT-SESSIONS-PROTOCOL.md 14.1, 14.7).
Finding 4 (the eager imports in board_protocol.py) is the #7M6E implementer's file and is not in
these commits.
