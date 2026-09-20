---
id: TZWF
type: work
status: needs-verification
labels: [bug, performance, backend]
assignee: claude-code
rank: m2
created: '2026-09-20'
source: 'Claude Code in the owner''s terminal, 2026-09-20 — found by the #PF4K profilers'
links: {plans: [], commits: [722e0f1aece09fce219b39d343941aac98dac8ac, 925af7fa689b3a9b4ec018f14efeef7c48e6f7e1], evidence: [docs/qa_evidence/2026-09-20-perf-profile/, docs/qa_evidence/2026-09-20-perf-fixes/worker/], related: [PF4K], github: null}
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

## Outcome
Findings 1, 2 and 3 landed (`722e0f1a`, `925af7fa`). Finding 4 is `board_protocol.py`, which the
#7M6E implementer owns; it is theirs to do and is not in these commits.

1. `provider.shared_opener()` builds the opener once per process behind a lock. Every caller that
   built its own asks for it now: `ChatProvider.complete` (and the title/summary side calls through
   it), `voice`, the custom-provider `/models` probe, the local-model probe (the no-proxy variant),
   Relay Free and the GitHub forge. The one behaviour difference, said in the comment: the proxy
   environment is read once per process instead of once per request.
2. `install(CODE …)` byte-compiles the staged backend, `remote/` and `rendezvous/` —
   `--invalidation-mode unchecked-hash` so no mtime decides whether the cache is used and the
   package stays deterministic, `-s`/`-p` so each `.pyc` names its installed path. Checked on
   `cmake --install`, on `DESTDIR` (both Arch PKGBUILDs) and on CPack's staging prefix (the .deb).
   The checkout's own `__pycache__` is still excluded from the copy.
3. conv_index v5 keeps `entry_count` and `entry_digest` on the conversation row; a save whose
   rolling digest still matches its first `entry_count` rows writes only the turns that were added.
   A rewind, a fork, a compaction, a prompt edited where it stands or a missing `entries` table
   falls back to the full rewrite and ends with what a from-scratch rebuild writes. `update_thread`
   does the same. `session_entries` now returns rows in conversation order, which is what makes an
   added turn an append — and a body row's `time` is now the save that indexed it rather than the
   session's latest save. A rebuild ends with a `VACUUM` when at least a tenth of the file and at
   least 16 MB of it are free; that is the only place Relay vacuums, and `index_rebuilt` reports
   `reclaimed`.

Measured on spark, before = the clean export at `ccb31a8e`, after = these commits
(docs/qa_evidence/2026-09-20-perf-fixes/worker/):

| | before | after |
| --- | --- | --- |
| worker CPU, 300 stub turns | 15.81 s | 4.38 s |
| ask → first request byte, turns 200–299 | 17.5 ms | 5.1 ms |
| Pss after 300 turns | 37.4 MiB | 27.4 MiB |
| worker start, read-only installed tree | 262.5 ms | 74.6 ms |
| one autosave, 594-message session | 49.5 ms | 2.1 ms |
| index file after a rebuild (copy of the owner's) | 119.6 MB | 87.3 MB |

Tests: `tests/test_provider.py::SharedOpenerTests`, `tests/test_install_bytecode.py`,
`tests/test_conv_index.py::IncrementalIndexTests` and the v4 → v5 migration case.
`packaging/smoke-installed.sh` checks the shipped `.pyc` on an installed package.

## QA checklist
- [ ] A conversation indexed by an older Relay still searches: open the session manager on the real
      store, search a word from an old conversation, and check the hit and its line are right. The
      database migrates v4 → v5 in place on first open; nothing should be re-read. <!-- t:q1 -->
- [ ] Keep talking in a long pane and search for something said in the newest turn: it is found at
      once, and so is something from the first turn. <!-- t:q2 -->
- [ ] Rewind a conversation (or fork it), then search for a turn that was undone: it must be gone
      from the results. <!-- t:q3 -->
- [ ] Options › … › rebuild the conversation index on the real store, then search again; the status
      line reports the rebuild and the file should be smaller afterwards (a one-off `VACUUM`).
      <!-- t:q4 -->
- [ ] A model turn still works against a live provider (a real key, one turn, one tool call), and a
      local model server is still reached with no proxy in the way. <!-- t:q5 -->
- [ ] On a packaged install (`.deb` or `makepkg`), `/usr/share/relay/backend/relay_core/__pycache__`
      exists and panes come up noticeably faster than before; `packaging/smoke-installed.sh` passes.
      <!-- t:q6 -->
