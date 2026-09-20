---
id: TZWF
type: work
status: planned
labels: [bug, performance, backend]
assignee: null
rank: m2
created: '2026-09-20'
source: 'Claude Code in the owner''s terminal, 2026-09-20 — found by the #PF4K profilers'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-perf-profile/], related: [PF4K], github: null}
---
# The worker reloads the system CA store for every model request; installed backend never caches bytecode

## Issue
deploy opus subagents to profile and find performance issues and improvements. build it on sphinxpad as well to see how performs there

## Findings
Measured with the stub provider on spark (Python 3.12) and sphinxpad (3.14). Detail: [docs/qa_evidence/2026-09-20-perf-profile/worker/FINDINGS.md](../../docs/qa_evidence/2026-09-20-perf-profile/worker/FINDINGS.md), findings 1, 2, 3 and 5.

1. **`backend/relay_core/provider.py:744`** calls `urllib.request.build_opener(NoRedirect())` per request → `ssl.create_default_context()` → the whole system CA store is parsed: **11.8 ms CPU per request** (12.0 ms re-timed by the orchestrator), 52.5 % of all worker CPU under py-spy, for the model call and both title/summary side calls, even for `http://127.0.0.1`. With a module-level cached opener over 300 stub turns: worker CPU 16.2 → 5.5 s (spark), 7.7 → 5.1 s (sphinxpad); ask → first request byte 17.3 → 4.7 ms; Pss after 300 turns 39.9 → 27.4 MiB (spark), **151.8 → 31.2 MiB** (sphinxpad: 109 MiB of unreturned CA-parse heap per worker on 3.14; `PYTHONMALLOC` and `MALLOC_ARENA_MAX` do nothing).
2. **`CMakeLists.txt:507`** installs the backend as `.py` with `__pycache__` excluded into a root-owned directory, so bytecode can never be written: 259 ms (spark) / 278 ms (sphinxpad) to `ready` instead of 71 / 83 ms, for each of the nine workers of a 3-tab, 6-pane session.
3. **`backend/relay_core/conv_index.py:1110/1128`** deletes and re-inserts every entry row of the session on every autosave, on the turn thread: 0.26 ms at 2 messages, 46 ms on the owner's largest real session (594 messages). His `index.db` is 119.6 MB with a 31.5 MB freelist.
4. **`backend/relay_core/board_protocol.py:24-29`** eagerly imports `project_probe`, `forge_sync`, `forge_github` and `board_import`, reachable only from five on-demand messages: 5.5 ms CPU per worker start.

## Plan
1. Cache the opener at module level (six lines; seven test suites pass against the prototype). Caveat to keep in the comment: proxy environment is then read once per process.
2. Byte-compile the backend at package/install time (`python3 -m compileall`), and stop excluding the result.
3. Make the index update incremental — delete/insert the last turn only — and fall back to the full rewrite on rewind, fork and compaction. Consider a one-off `VACUUM`.
4. Import those four modules where they are used.
