# Worker (Python backend) — performance profile, #PF4K, 2026-09-20

What was measured: `backend/worker.py` and `backend/relay_core/**` — the process the GUI spawns per
pane (`src/Pane.h:8854`) and per tab for the Switchboard (`src/BoardWorker.cpp:111`). Both run the
same `python3 -S -u <data>/backend/worker.py`. Rendering belongs to another profile; so does the
Switchboard pane and `scripts/relay-board.py`.

## Machines and state

| | spark | sphinxpad |
| --- | --- | --- |
| CPU | aarch64, 20 cores | 13th Gen i7-1365U, 12 threads |
| OS / Python | Ubuntu 24.04, CPython 3.12.3 | Ubuntu 26.04, CPython 3.14.4 |
| load average while measuring | 1.3–3.4 (other #PF4K agents were running) | 0.1–1.1 |
| power / governor | mains | on AC (`AC/online=1`), governor `performance` |
| Qt the C++ build picked | — | Qt5 (`~/relay-perf/configure.log`: "Relay: building against Qt5") |

Source under test: the clean export of `main` at `ccb31a8e` (`/tmp/claude-1000/pf4k/src` on spark,
`~/relay-perf/src` on sphinxpad). Line numbers below are from that export. No product code was
changed: the two prototypes measured below were applied to private copies of the tree under my
scratch directory and nothing was committed.

Every number is the median of at least three runs; where wall time was noisy under the other
agents' load, CPU time and syscall counts are quoted instead. All agent turns ran against an
in-process stub provider on loopback — no live keys.

Harness in this directory: `turnbench.py` (per-turn), `streambench.py` (streaming), `startup.py`,
`idle.py`, `toolbench.py`, `indexbench.py`, `memgrow.py`, `importcost.py`. Raw output in the
`raw-*.txt` files.

---

## 1. Every model request rebuilds a urllib opener, and with it the whole system CA store

**What the user feels.** Dead time before each model call, paid again at every step of a turn: a
turn with 20 tool steps loses ~0.3 s on spark before a single token is asked for. On Ubuntu 26.04
it is also where the worker's memory goes — a pane that has held a 300-turn conversation sits at
146 MiB instead of 37 MiB, so a 3-tab/6-pane session costs about a gigabyte more than it needs to.

**Measurement.** `python3 turnbench.py <root> 300` — 300 stub turns, no tools; the stub records
when the first request byte arrives.

| | spark, clean export | spark, fixed | sphinxpad, clean export | sphinxpad, fixed |
| --- | --- | --- | --- | --- |
| ask → first request byte, turn 1–9 | 13.7 ms | **1.1 ms** | 4.1 ms | **1.4 ms** |
| ask → first request byte, turn 200–299 | 17.3 ms | **4.7 ms** | 10.3 ms | **5.1 ms** |
| worker CPU over 300 turns | 16.2 s | **5.5 s** | 7.7 s | **5.1 s** |
| Pss after 300 turns | 39.9 MiB | **27.4 MiB** | 151.8 MiB | **31.2 MiB** |

Isolated cost of the call itself, on spark:

```
build_opener(NoRedirect()):   11.77 ms CPU
ssl.create_default_context(): 11.65 ms CPU
set_default_verify_paths():   11.67 ms CPU
```

A 30 s `py-spy record --rate 300` of a stub conversation on the clean export puts
`ssl.load_default_certs` at **52.5 % of all worker CPU** across three stacks (the model call, the
title side call and the summary side call) — see `raw-pyspy-spark.txt`. The stub conversation makes
1.33 provider requests per turn (400 requests over 300 turns: one model call, plus a title and a
summary call every five turns via `titles.REFRESH_TURNS = 5` / `SUMMARY_REFRESH_TURNS = 5`).

**Root cause.** `backend/relay_core/provider.py:744`

```python
opener = urllib.request.build_opener(NoRedirect())
```

`build_opener` constructs an `HTTPSHandler` whatever the scheme, `HTTPSHandler.__init__` calls
`ssl.create_default_context()`, and that calls `SSLContext.set_default_verify_paths()`, which parses
the machine's whole CA bundle. It happens on every request, including requests to
`http://127.0.0.1` where no TLS is used at all. `sidecall.call` (`sidecall.py:37`) reaches the same
line for the title and summary calls.

The memory follows from the same line. `memgrow.py` (tracemalloc inside the worker) shows the live
Python heap at **1.4–1.8 MiB after 300 turns on both machines** — there is no leak in Relay's own
objects. What grows is heap the interpreter allocated for the certificate parse and never gave
back: on Python 3.14 that is 109 MiB by turn 300 (146.4 → 37.2 MiB with the fix), on 3.12 about
12 MiB. `PYTHONMALLOC=pymalloc|malloc|mimalloc` and `MALLOC_ARENA_MAX=2` were each tried on
sphinxpad and changed nothing; only not making the allocation does.

**Fix.** Build the opener once per process and reuse it. Prototype (measured above):

```python
_OPENER = None
_OPENER_LOCK = threading.Lock()

def _shared_opener():
    global _OPENER
    if _OPENER is None:
        with _OPENER_LOCK:
            if _OPENER is None:
                _OPENER = urllib.request.build_opener(NoRedirect())
    return _OPENER
```

and `opener = _shared_opener()` at line 744.

**Expected gain.** Measured, not estimated: −66 % worker CPU per conversation on spark, −34 % on
sphinxpad; −12.6 ms of pre-request latency per model call on spark and −5 ms on sphinxpad; −109 MiB
of resident memory per busy pane on Ubuntu 26.04.

**Risk.** Low but not zero. `build_opener` reads proxy settings from the environment when it is
built, so caching freezes them for the process — the worker's environment is fixed at spawn, so
nothing observable changes, but it is the one behaviour difference. The handlers hold no
per-request state and the `HTTPSHandler`'s `SSLContext` is safe to share across threads.
`tests/test_provider.py`, `test_provider_local.py`, `test_failover.py`, `test_agent.py`,
`test_sessions.py`, `test_conv_index.py` and `test_session_protocol.py` all pass against the
prototype. A narrower variant — cache only the `SSLContext` and pass it to `HTTPSHandler(context=…)`
— gets the same saving with no proxy caveat.

---

## 2. An installed Relay can never write `__pycache__`, so every worker start recompiles 65 modules

**What the user feels.** Opening a window with three tabs and six panes starts nine workers; on a
packaged install each takes ~0.26 s (spark) or ~0.28 s (sphinxpad) instead of ~0.07–0.08 s, so the
panes take about 1.7 s longer to become usable, and every pane restart pays it again. Nothing in
the app reports this, and it does not happen in a git checkout, where the first run writes the
cache — which is why it would not be noticed in development.

**Measurement.** `startup.py` spawns the worker exactly as the GUI does and times spawn →
`{"event":"ready"}`.

| spark | median ready |
| --- | --- |
| warm `__pycache__` (a checkout) | 71 ms |
| source tree read-only, no `__pycache__` | **259 ms** |
| same tree after `python3 -m compileall -q backend` | 71 ms |

| sphinxpad | median ready |
| --- | --- |
| warm `__pycache__` | 83 ms |
| source tree read-only, no `__pycache__` | **278 ms** |
| after `compileall` | 83 ms |

Peak RSS during the start is 40 MiB uncached against 31 MiB cached on spark (46 vs 37 on
sphinxpad), because the compiler runs too.

Reproduce:

```
cp -r <export>/backend /tmp/ro/backend && find /tmp/ro -name __pycache__ -exec rm -rf {} +
chmod -R a-w /tmp/ro/backend/relay_core /tmp/ro/backend
python3 startup.py /tmp/ro 5
```

**Root cause.** `CMakeLists.txt:507` installs the backend as source:

```cmake
install(DIRECTORY backend shell remote rendezvous app DESTINATION ${CMAKE_INSTALL_DATADIR}/relay
```

`${CMAKE_INSTALL_DATADIR}/relay/backend/relay_core` is root-owned, so the user's worker cannot write
`__pycache__` next to the `.py` files and CPython silently falls back to compiling every module on
every start. There is no `compileall` step anywhere in the install or packaging.

**Fix.** Byte-compile at install time — an `install(CODE …)` that runs
`"${Python3_EXECUTABLE}" -m compileall -q "<prefix>/<datadir>/relay/backend"` after the
`install(DIRECTORY …)`, and the equivalent in the `.deb` postinst / any other packaging. Nothing
else changes: the `.pyc` files sit beside the sources and are used read-only.

**Expected gain.** 188 ms per worker start on spark, 195 ms on sphinxpad; ~1.7 s off a nine-worker
session start on a packaged install, and the same off every pane restart. Measured directly above.

**Risk.** None to the running app. A stale `.pyc` cannot occur: CPython validates the source
mtime/size, and `compileall` runs on the installed copy. Packagers who want reproducible builds can
add `--invalidation-mode checked-hash`.

---

## 3. Every autosave deletes and re-inserts the whole conversation's search index

**What the user feels.** Turns get slower the longer the conversation runs, on the turn's own
thread. On the owner's largest real session (1.6 MiB, 594 messages) one autosave spends 46 ms
re-indexing; the same session at two messages spends 0.26 ms. It is also why the index file is
a third larger than the data in it (119.6 MB, of which 31.5 MB is freelist).

**Measurement.** `indexbench.py`, against a **read-only copy** of `~/.local/share/relay` (sqlite
`backup()` of `index.db` plus `cp -r` of `sessions/`; the originals were never opened for writing —
sizes and timings only, no content is reported).

```
update_session (one autosave) by session-file size:
     5 KiB    2 messages  ->   0.26 ms
    56 KiB   35 messages  ->   2.57 ms
   182 KiB   86 messages  ->   5.95 ms
   487 KiB  192 messages  ->  16.77 ms
  1574 KiB  594 messages  ->  46.14 ms
```

Whole-worker effect, from `RELAY_INDEX=off` on the same 300-turn run: spark 16.2 s → 14.1 s of CPU
(**11 ms per turn, 23 % of the clean export's CPU once the opener fix is in**); sphinxpad 7.7 s →
5.0 s (**9 ms per turn**).

Space, on the copy of the live index: `page_count` 29 198, `freelist_count` 7 689 — 31.5 MB of the
119.6 MB file is free pages from the delete/re-insert churn. `VACUUM` takes it to 87.3 MB in 0.3 s.

**Root cause.** `backend/relay_core/conv_index.py:1110` and `:1128`, inside `update_session`:

```python
db.execute("DELETE FROM entries WHERE session_id=?", (session_id,))
...
db.executemany("INSERT INTO entries(session_id, turn, seq, kind, time, text) VALUES(?,?,?,?,?,?)", …)
```

Every row of the conversation — and its FTS shadow rows — is deleted and written again on each
save, although only the last turn changed. `update_session` is called from `sessions.py:222`
(`SessionStore.save`), which is called from `agent.py:3084` (`autosave`) on the turn thread inside
`Agent.ask`, so the turn waits for it.

**Fix.** Make it incremental: keep the indexed turn high-water mark on the `conversations` row
(there is already an `indexed_version` column to put a companion beside) and, when the incoming
`data` extends what is indexed rather than rewriting it, `DELETE FROM entries WHERE session_id=? AND
turn>=?` for the last turn only and insert the rows from there. Fall back to the full rewrite when
the session was rewound, forked, compacted or its epoch changed — the cases that already invalidate
the context estimate (`context.invalidate()`), so the signal exists. A whole-session rebuild then
costs what a two-message session costs today, ~0.3 ms.

**Expected gain.** ~11 ms per turn on spark and ~9 ms on sphinxpad for a mid-sized conversation,
rising to ~46 ms per autosave on the owner's largest session — replaced by a constant ~0.3 ms.
Estimated from the size/cost table above: the work is proportional to rows rewritten, and the
incremental form rewrites one turn's rows instead of all of them. It also stops the freelist churn,
so the index stops growing to 1.4× its content.

**Risk.** Medium — it is real logic, and getting the invalidation wrong means stale search results.
It needs `tests/test_conv_index.py` extended with rewind/fork/compaction cases. A zero-risk partial
step that can land first: run `PRAGMA incremental_vacuum` (or a `VACUUM` behind the existing
`index_rebuild` path) so the 31.5 MB of freelist is returned.

---

## 4. The whole session file is re-serialised and rewritten on every save

**What the user feels.** A fixed cost at the end of every turn that grows with the conversation:
~10 ms on the owner's largest session, and about 60 MB written to disk over the life of a 300-turn
conversation that only ever holds 390 KB.

**Measurement.** On the owner's four largest real sessions (read-only copies):

```
 1120 KiB  391 msgs  json.dump(fp)= 6.45 ms   write(dumps)= 6.57 ms
 1142 KiB  452 msgs  json.dump(fp)= 7.17 ms   write(dumps)= 7.63 ms
 1385 KiB  401 msgs  json.dump(fp)= 8.47 ms   write(dumps)= 6.82 ms
 1574 KiB  594 msgs  json.dump(fp)= 9.75 ms   write(dumps)= 9.53 ms
```

On the shape the 300-turn stub run produces (389 KB, many short messages) the same file costs
5.48 ms with `json.dump(obj, fp)` and **1.39 ms** with `fp.write(json.dumps(obj))`, because
`json.dump` streams through `TextIOWrapper.write` one fragment at a time — the profile shows
1 038 449 `write` calls over 100 turns. `strace` of 20 turns counts **5.8 session-file opens per
turn** (session + meta, temp-and-rename, and the meta read-back).

Composition of the 300-turn session file: messages 190 KB, requests ledger 133 KB, checkpoints
65 KB.

**Root cause.** `backend/relay_core/sessions.py:138` inside `_atomic_json`, and
`agent.py:3065` — `session_data()` copies `self.messages[1:]` and the full request ledger and
checkpoint list into a new dict, which `_atomic_json` then streams to a temp file and renames.

**Fix (small, safe).** `out.write(json.dumps(data, ensure_ascii=False))` in `_atomic_json`. Measured
alone over 300 stub turns: 16.2 s → 14.7 s of worker CPU on spark (−7 %); on the owner's real
sessions it is a wash, because their strings are longer and there are fewer of them. Worth taking
because it is one line and never slower, but it is not the headline.

**Fix (structural, needs a decision).** The autosave is whole-file because the session format is one
JSON document. An append-only turn log with a periodic compaction would make it O(1) per turn, but
that is a format change that touches `sessions.py`, `conv_index.py`, rewind/fork and the session
manager — the owner's call, not something to fold into a perf pass.

**Risk.** The one-line change: none (`tests/test_sessions.py` passes). The format change: large.

---

## 5. Four modules that are only reachable on demand are imported at every worker start

**What the user feels.** ~6 ms of the ~75 ms warm start, per worker, nine times.

**Measurement.** `python3 -X importtime -c "import worker"` (`raw-importtime-spark.txt`) and, since
importtime inflates per-import costs, a direct CPU comparison over 15 starts:

```
import worker:  77.4 ms child CPU (median of 15)  -- clean export
import worker:  71.9 ms child CPU (median of 15)  -- with the four made lazy
```

**Root cause.** `backend/relay_core/board_protocol.py:24,26,27,29`

```python
from . import board_import as I
from . import forge_github as GH
from . import forge_sync as F
from . import project_probe as PP
```

`project_probe` (1 745 lines), `forge_sync` (1 713), `board_import` and `forge_github` are reached
only by the `project_probe`, `board_import_propose`, `board_import_apply`, `forge_sync_plan` and
`forge_sync_run` messages — none of which a pane ever sends unless the user opens a project survey
or a GitHub sync. Nothing else in `relay_core` imports them.

**Fix.** Import them on first use (a small lazy proxy object per alias, or `importlib.import_module`
inside the five handlers). The prototype used a proxy and passes the board tests.

**Expected gain.** 5.5 ms CPU per worker start, ~50 ms across a nine-worker session. Measured.
Small — worth doing only alongside finding 2, which is where the startup time actually is.

**Risk.** Low, but it is easy to miss a reference: anything doing `isinstance` against a class from
those modules, or a `from … import name`, has to be checked. `tests/test_board_protocol.py`,
`test_board_import.py` and `test_forge_sync.py` cover the handlers.

---

## 6. The system prompt is 25 KB, or 41 KB with a Switchboard attached, on every request

Not a CPU finding — re-serialising it costs 0.45 ms at 300 turns — but it is paid in input tokens on
every model call of every step. `relay_core.agent.SYSTEM` is 5 009 bytes; the assembled prompt at
`agent.py:738` reaches 25 KB once the 60 bundled skills, todo rules and app rules are appended, and
41 KB with `board_tools.prompt_section` (`board_policy.md`, 5 376 bytes, plus the header). Measured
as the first request's payload with two messages: 24 984 B with no board, 40 959 B with one.

Whether that is the right trade against prompt caching is a product decision, so it stays listed
rather than fixed. Worth knowing when reading a bill: at four characters per token it is ~6 000
input tokens before the conversation starts, ~10 000 with a board.

`policy_text()` (`board_tools.py:3052`) re-reads `board_policy.md` from disk and re-runs a regex
over it on every call, with no cache — but `system_prompt()` is only called when the prompt changes
(`configure`, `set_mode`, board attach, session load), not per turn, so it costs nothing today. It
is one `functools.cache` away from being safe if that ever changes.

---

## Measured and fine — do not re-profile these

- **An idle worker is genuinely idle.** 0 ms of CPU over 60 s (spark) and 30 s (sphinxpad), zero
  context switches in the window, 2 threads. `strace -c` of the whole lifetime shows no polling:
  every wait in `queue.py`, `subagents.py` and `jobs.py` is a condition variable or an
  `Event.wait`, and the only periodic timer is the provider stall watchdog at 2 Hz
  (`provider.py:57`, `WATCHDOG_TICK = 0.5`), which runs only while a request is in flight.
- **Idle memory per worker: 21.1 MiB Pss on spark, 24.8 MiB on sphinxpad.** Nine workers (six panes
  + three tab board workers) total 193.1 MiB Pss / 286 MiB Rss on spark, essentially unshared
  (190.6 MiB `Private_Dirty`) — that is Python heap, not something page sharing can help. The fix in
  finding 1 is what keeps it from growing five-fold under load.
- **The tool-execution wrapper.** `run_command "true"` is 0.96 ms wall / 0.45 ms worker CPU on
  spark and 1.05 / 0.32 on sphinxpad, of which the `bash --noprofile --norc -c` spawn is ~1.0 ms and
  ~0.7 ms — the wrapper adds almost nothing. A command producing **50 MiB** costs 32 ms wall / 28 ms
  worker CPU and moves peak RSS by 2 MiB, because `jobs.py` keeps a bounded ring buffer and
  `take_output` returns the last 32 KiB. `read_file` on a 116 KiB source file is 0.26 ms;
  `list_directory` is 0.08 ms.
- **There is no `search_files` tool and no Python directory walk in the agent's tool set.** The
  tools are `run_command, read_file, list_directory, write_file, edit_file` (+ job and skill tools),
  5 116 bytes of schema. Searching is whatever the model types: `rg -n … -g '!build'` across this
  repo is 9.6 ms on spark and 7.6 ms on sphinxpad, `grep -rn` with `--exclude-dir` is 2 529 ms on
  spark. `.gitignore` handling and skipping `build/` are therefore the model's business, steered by
  the prompt, not the worker's.
- **Streaming.** 20 µs of worker CPU per delta back to back on spark (45 µs on sphinxpad); 85 µs at
  1 000 deltas/s and 185 µs at 100 deltas/s, the difference being the scheduler wakeup rather than
  work. One `delta` event and 44 bytes on stdout per provider delta, flushed per event — a 1:1
  pass-through with no coalescing. At 1 000 deltas/s that is 8.5 % of a core on spark; at the
  50–100 deltas/s a real provider produces it is under 2 %. Coalescing would only be worth it if the
  GUI side turns out to be the bottleneck — that is the rendering profile's call, not this one.
- **Full index rebuild.** 667 sessions (54.5 MB) in 1.6 s, 2 ms each, peak RSS 42 MiB. Fine.
- **`project_probe` and `forge_sync` do no per-turn work.** They run only on their own protocol
  messages; nothing calls them from `configure` or from a turn. Their cost is the import in
  finding 5.
- **No git subprocess per turn.** `Agent.refresh_branch` (`agent.py:2814`) reads `.git/HEAD`
  directly; `strace` of 20 turns shows no `execve`.
- **Token counting is not O(n²).** `Context.used` (`context.py:151`) estimates only the messages
  added since the last usage report when the provider reports usage. `record_usage`
  (`context.py:139`) does re-estimate the whole conversation once per usage report, but
  `estimate_tokens` on a 300-turn list is 0.77 ms — visible in a profile, not in a turn.
- **Request payload growth is linear and cheap.** 25 KB at turn 1, 173 KB at turn 300;
  `json.dumps` of the largest is 0.45 ms.
- **Turn wall time grows sub-linearly.** With the opener fix, ask → done is 2.0 ms at turn 1, 3.6 ms
  at turn 50–99 and 7.8 ms at turn 200–299 on spark (2.6 / 4.0 / 8.8 ms on sphinxpad) — the growth is
  the session write and the index write of findings 3 and 4, not the conversation handling.
- **No unbounded memory growth in Relay's own objects.** tracemalloc across 300 turns: 1.23 MiB live
  on spark, 1.45 MiB on sphinxpad, ~3 KiB per turn, all of it the conversation itself
  (`requests.py:64`, `checkpoints.py:50`, `provider.py:1224`).

## Spark vs sphinxpad, side by side

| scenario | spark (aarch64, py3.12) | sphinxpad (x86_64, py3.14) |
| --- | --- | --- |
| worker start, warm cache | 71 ms | 83 ms |
| worker start, packaged install (no writable cache) | 259 ms | 278 ms |
| `configure` after `ready` (repo as workspace) | 45 ms | — |
| worker CPU, 300 stub turns | 16.2 s → 5.5 s fixed | 7.7 s → 5.1 s fixed |
| ask → first request byte (turn 200–299) | 17.3 ms → 4.7 ms fixed | 10.3 ms → 5.1 ms fixed |
| Pss after 300 turns | 39.9 MiB → 27.4 MiB fixed | 151.8 MiB → 31.2 MiB fixed |
| idle worker Pss | 21.1 MiB | 24.8 MiB |
| streaming CPU per delta (1 000/s) | 85 µs | 75 µs |
| `run_command "true"` | 0.96 ms | 1.05 ms |
| 50 MiB command output | 32 ms wall / 28 ms CPU | 41 ms wall / 31 ms CPU |
| conv_index cost per turn (index on vs off) | 11 ms | 9 ms |

sphinxpad's per-core speed is roughly twice spark's on this workload, so its raw turn times are
lower; its Python 3.14 is what makes finding 1 a memory problem there and only a CPU problem on
spark. Anyone reading spark's memory numbers as the product's memory footprint will be wrong about
the distributions Relay will actually ship on.

## Not measured, and why

- **The owner's live workers.** Seven `backend/worker.py` processes from his checkout were running
  throughout; they were left alone, as the brief requires. Everything here is from workers I started
  under isolated `XDG_*` directories with `RELAY_KEYRING=off`.
- **Turns against a real provider.** No live keys were used. The stub makes the worker's own CPU and
  latency visible, which is the point, but it cannot show how provider-side behaviour (retries,
  failover, `stall_timeout`) interacts with any of this.
- **`conv_index` search latency as the user feels it.** A one-word query over the copy of the
  owner's index takes 76 ms (`relay`) and 59 ms (`board_tools`), against 1.3 ms for a listing with
  no query. That is the session manager's search box, which belongs to another agent's area — it is
  recorded here because the index is the worker's, and 76 ms per keystroke is worth their attention.
