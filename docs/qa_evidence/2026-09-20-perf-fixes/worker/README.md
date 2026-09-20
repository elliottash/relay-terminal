# Worker performance fixes — #TZWF, measured on spark, 2026-09-20

Findings 1, 2 and 3 of [the profile](../../2026-09-20-perf-profile/worker/FINDINGS.md). Finding 4
(the eager imports in `board_protocol.py`) belongs to #7M6E's implementer and is not in here.

**Before** is the clean export of `main` at `ccb31a8e` (`/tmp/claude-1000/pf4k/src`), the same tree
the profile was measured on. **After** is this checkout with the three fixes in it. Everything ran
against the profile's own harness (`docs/qa_evidence/2026-09-20-perf-profile/worker/`), on spark
(aarch64, 20 cores, CPython 3.12.3), with five other #PF4K agents on the machine — load average
4–7 throughout, which is why the CPU totals rather than the wall times are the numbers to read.
All turns ran against an in-process stub provider on loopback; no live keys, and nothing was run
against the owner's real store (see "What was copied" below).

| | before | after |
| --- | --- | --- |
| worker CPU, 300 stub turns | 15.81 s | **4.38 s** |
| ask → first request byte, turns 1–9 | 14.3 ms | **1.0 ms** |
| ask → first request byte, turns 200–299 | 17.5 ms | **5.1 ms** |
| turn wall (ask → done), turns 200–299 | 21.9 ms | **8.4 ms** |
| Pss after 300 turns | 37.4 MiB | **27.4 MiB** |
| worker start, read-only installed tree | 262.5 ms | **74.6 ms** |
| one autosave, 594-message session | 49.5 ms | **2.1 ms** |
| one autosave, 192-message session | 16.4 ms | **0.9 ms** |
| index file after a rebuild (owner's copy) | 119.6 MB | **87.3 MB** |

Raw output: `turnbench-{before,after}.txt`, `memgrow-{before,after}.txt`,
`startup-install.txt`, `indexbench-{before,after}.txt`, `indexgrow-{before,after}.txt`,
`vacuum.txt`.

## 1. One urllib opener per process

`provider.shared_opener()` builds the opener once behind a lock; every caller that used to build
its own now asks for it (`provider.complete`, the title/summary side calls through it, `voice`,
the custom-provider `/models` probe, the local-model probe with the no-proxy variant, `hosted`,
`forge_github`). The CA store is parsed once per process instead of once per request.

```
python3 turnbench.py <root> 300
```

| turns | before: ask→req | after: ask→req |
| --- | --- | --- |
| 1–9 | 14.3 ms | 1.0 ms |
| 50–99 | 14.9 ms | 2.2 ms |
| 200–299 | 17.5 ms | 5.1 ms |

Worker CPU over the 300 turns: 15.81 s → 4.38 s. Pss at turn 300: 37.4 → 27.4 MiB.
`memgrow.py` (tracemalloc, against the instrumented copy) confirms the live Python heap is
unchanged — 1.23 → 1.21 MiB after 300 turns, ~3 KiB a turn, all of it the conversation itself. The
memory that goes is heap the interpreter allocated for the certificate parse and never returned.

Test: `tests/test_provider.py::SharedOpenerTests` — the openers built and the CA stores parsed
across five completions against a local stub server are `(1, 1)`, and `(0, 0)` for the next five.

## 2. Bytecode in the installed backend

`install(CODE …)` in `CMakeLists.txt` byte-compiles the staged tree
(`--invalidation-mode unchecked-hash`, `-s`/`-p` so each `.pyc` names its installed path). It was
checked on all three install paths: `cmake --install` with `DESTDIR` (the Arch PKGBUILDs), and
`cmake -DCMAKE_INSTALL_PREFIX=<staging>/usr -P cmake_install.cmake` (what CPack's DEB generator
does). 106 `.py`, 106 `.pyc`, flags `1` (hash-based, unchecked), `co_filename`
`/usr/share/relay/backend/relay_core/provider.py`.

```
python3 startup.py <read-only tree> 7      # spawn -> {"event":"ready"}
```

| read-only tree | median ready |
| --- | --- |
| no `__pycache__` (what an installed Relay had) | 262.5 ms |
| byte-compiled at install | **74.6 ms** |

Peak RSS during the start falls with it, 40.1 → 30.9 MiB, because the compiler no longer runs.
A three-tab, six-pane session starts nine workers, so that is ~1.7 s off the session start and the
same off every pane restart.

Tests: `tests/test_install_bytecode.py` pins the rule and then does what it does to a copy of
`backend/`, makes the copy read-only as an installed tree is, and starts the worker from it with
nothing compiled at start. `packaging/smoke-installed.sh` checks the shipped `.pyc`.

## 3. Incremental conversation index

`update_session` (and `update_thread`) compare a rolling digest of the rows already indexed
against the rows the session now holds. A pure extension writes the new turns' rows and the header
rows; anything else — a rewind, a fork, a compaction, a prompt edited where it stands, entries
missing under a row that stayed — falls back to the full rewrite. `session_entries` now returns
rows in conversation order, which is what makes an added turn an append.

```
python3 indexgrow.py <root> <store copy> 12     # append a turn, save, append a turn, save
```

| session | before | after |
| --- | --- | --- |
| 5 KiB, 2 messages | 0.61 ms | 0.33 ms |
| 56 KiB, 35 messages | 3.13 ms | 0.42 ms |
| 182 KiB, 86 messages | 5.77 ms | 0.53 ms |
| 487 KiB, 192 messages | 16.36 ms | 0.93 ms |
| 1574 KiB, 594 messages | 49.53 ms | **2.08 ms** |

`indexbench.py`, which re-saves a session that did not change, reports 45.4 → 2.0 ms on the same
594-message session. The full rebuild of all 667 sessions is unchanged at 1.5–1.6 s.

The freelist the old delete-and-reinsert left behind does not come back on its own, so a rebuild
(`index_rebuild`, asked for by hand and run on a background thread) now ends with a `VACUUM` when
at least a tenth of the file and at least 16 MB of it are free. On a copy of the owner's index:
119.6 MB → 87.3 MB in 254 ms, freelist 31.5 MB → 0, FTS5 `integrity-check` clean.

Tests: `tests/test_conv_index.py::IncrementalIndexTests` — the rows already indexed keep their
ids after a save that added a turn and the save costs a fifth of the rewrite; a rewind, an edited
prompt, a compaction and a wiped `entries` table each end with byte-for-byte what a from-scratch
rebuild writes; FTS5's own `integrity-check` runs after every case; the v4 → v5 migration is in
place and asks for no re-index.

## Live, in a real worker

The 300-turn `turnbench` runs above are real `backend/worker.py` processes with the index on, so
the index each left behind is the fix working end to end rather than a unit test. Both runs wrote
into the same isolated `$XDG_DATA_HOME`, so the after run also migrated a database with the before
run's rows in it, in place:

```
schema 5, FTS5 integrity-check ok
  3ee68650  turns 300  entry_count   0  digest  0 chars  602 rows   <- before (clean export)
  9e41a649  turns 300  entry_count 600  digest 64 chars  602 rows   <- after
```

600 body rows (a prompt and a reply for each of the 300 turns) plus the title and the summary. The
before row keeps `entry_count 0`, which is what a conversation indexed by an older Relay looks like
until its next save.

## What was copied, and what was not

The owner's `~/.local/share/relay` was never opened for writing and never read for content. The
index numbers ran against the copy the profiler already had
(`/tmp/claude-1000/pf4k/worker/store`), duplicated again per run so a before run and an after run
never shared a database. Only sizes and timings are reported here. The owner's own `index.db` was
not vacuumed: the measurement above is on the copy, and the `VACUUM` that ships happens only when
he rebuilds the index himself.
