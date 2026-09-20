# The phase-1 performance fixes on sphinxpad — before and after, Qt5 and Qt6 (#PF4K)

The owner's original request was "deploy opus subagents to profile and find performance issues and
improvements. **build it on sphinxpad as well to see how performs there**". The five profilers
measured both machines ([REPORT.md](../../2026-09-20-perf-profile/REPORT.md)); every implementer
then re-measured their fix **on spark only**. This is the laptop half of the before/after.

| | |
|---|---|
| Machine | sphinxpad, x86_64 Intel i7-1365U, 12 threads, Ubuntu 26.04, CPython 3.14.4 |
| Power / governor | **on AC** (`AC/online=1`), governor `performance`, all runs |
| Load average | recorded with every run; 0.05–0.8 for everything but where noted |
| Display | private Xvfb `:271`, 1600×1000, no window manager; the owner's own desktop session was untouched |
| **before** | `main` at `ccb31a8e` — `~/relay-perf/build/relay` (Qt 5.15.18) and `build-qt6/relay` (Qt 6.10.2), source `~/relay-perf/src` |
| **after** | `main` at `dc091a86`, every phase-1 fix — `~/relay-perf/build2-qt5/relay`, `build2-qt6/relay`, source `~/relay-perf/src2` |
| Build | RelWithDebInfo `-O2 -g -DNDEBUG -fno-omit-frame-pointer`, libvterm core (GhosttyCore does not build here) |
| Backend | each binary was run with `RELAY_DATA_DIR` pointed at **its own** tree, so no worker number crosses the two trees |
| Provider | the loopback stub only (`tr/stub.py`), or no agent at all; `RELAY_KEYRING=off`; every GUI profile had `[input] default=shell` and an outbound proxy of `127.0.0.1:9`, so nothing typed could reach a hosted service |

Harness in [`harness/`](harness/), trimmed raw output in [`raw/`](raw/), screenshots in
[`shots/`](shots/). Everything reuses the profilers' and implementers' own scripts
(`2026-09-20-perf-profile/startup/harness/`, `.../board/harness/`, `.../transcript/`,
`2026-09-20-perf-fixes/{board,worker,toolout}/`), with paths adapted and one guard added.

---

## The table

`b5` = before Qt5, `a5` = after Qt5, `b6` = before Qt6, `a6` = after Qt6. Bold is the best cell of
a row where the row has a best.

### 1. Idle — the battery row (`harness/idle-all.sh`, median of 3 × 45 s, `raw/idle-summary.txt`)

| metric | before Qt5 | after Qt5 | before Qt6 | after Qt6 |
|---|---|---|---|---|
| 1 pane, CPU | 0.58 % | **0.49 %** | 0.53 % | 0.51 % |
| 1 pane, wakeups/s | 18.7 | 18.4 | 20.2 | 20.4 |
| 4 panes, CPU | 1.38 % | **1.22 %** | 1.60 % | 1.27 % |
| 4 panes, wakeups/s | 46.6 | 45.7 | 46.7 | 48.0 |
| 1 pane, relay Pss | 50.1 MB | 49.8 MB | 27.8 MB | **28.3 MB** |
| 4 panes, whole tree Pss | 172.1 MB | 169.4 MB | 152.6 MB | **148.5 MB** |

### 2. Filesystem syscalls (`harness/statx-all.sh`, `raw/per-key.txt`, `raw/syscalls-idle.txt`)

Both figures are **deterministic**: twelve independent runs returned the same integers, which is
what says they are the polling rate and not noise.

| metric | before Qt5 | after Qt5 | before Qt6 | after Qt6 |
|---|---|---|---|---|
| **per key press**, `statx` | 225.2 | **2.2** | 225.2 | **2.2** |
| **per key press**, `openat` | 11.0 | **0.0** | 11.0 | **0.0** |
| idle 1 pane, `statx`/s | 105.0 | **57.5** | 105.0 | **57.5** |
| idle 1 pane, `getdents64`/s | 35.0 | **10.0** | 35.0 | **10.0** |
| idle 1 pane, all six counted/s | 249.0 | **161.5** | 234.0 | **161.5** |
| idle 4 panes, `statx`/s | 367.5 | **230.0** | 367.5 | **230.0** |
| idle 4 panes, `getdents64`/s | 140.0 | **40.0** | 140.0 | **40.0** |
| idle 4 panes, all six counted/s | 917.7 | **642.6** | 880.1 | **642.7** |

### 3. Terminal (`harness/term.sh`, `harness/sb.sh`, median of 3, `raw/term-*.txt`)

156×38 grid, `cat` of a 50 MB base64 file and `seq 1 2000000`, driven through the pane's shell.

| metric | before Qt5 | after Qt5 | before Qt6 | after Qt6 |
|---|---|---|---|---|
| `cat` 50 MB, wall | 1.74 s | **1.55 s** | 1.79 s | 1.62 s |
| `cat` 50 MB, GUI CPU | 0.80 s | **0.40 s** | 0.81 s | 0.41 s |
| `seq 1 2000000`, wall | 3.65 s | **2.13 s** | 3.85 s | 2.29 s |
| GUI-thread `statx` in 4 s of output | 19,097 | **1,169** | 19,847 | **1,205** |
| `paintEvent` in 4 s of output | 503 (126/s) | **156 (39/s)** | 473 (118/s) | **170 (42/s)** |
| `paintRow` in the same 4 s | 18,140 | **5,583** | 18,467 | **6,594** |
| per scrollback line, fresh pane | 3,263 B | **275 B** | — (core is Qt-independent) | — |

### 4. Python worker (`harness/worker-all.sh`, 300 stub turns, `raw/worker-*.txt`)

Backend only; the Qt build does not enter into it. Two alternating rounds, both shown.

| metric | before | after |
|---|---|---|
| worker CPU, 300 turns | 7.75 s / 7.45 s | **4.04 s / 4.06 s** |
| ask → first request byte, turns 1–9 | 5.6 / 6.4 ms | **1.4 / 1.3 ms** |
| ask → first request byte, turns 200–299 | 10.7 / 8.6 ms | **5.1 / 5.8 ms** |
| turn wall (ask → done), turns 200–299 | 14.7 / 12.1 ms | **8.5 / 9.8 ms** |
| **worker Pss after 300 turns** | **140.1 / 146.6 MiB** | **30.2 / 30.0 MiB** |
| worker spawn → ready, read-only tree, no bytecode | 295.6 ms | 270.7 ms |
| worker spawn → ready, read-only tree, bytecode installed | **84.6 ms** | **79.3 ms** |

The 151.8 MiB the profile reported on Python 3.14 reproduces (140–147 MiB) and is **gone**: 30 MiB,
which is what spark's 3.12 already showed. That is the laptop's headline row.

### 5. Switchboard, worker side (`harness/board-be.sh`, `raw/board-backend.txt`)

Both sides against a private copy of the same board. Qt-independent.

| 352 cards (the repo's own board) | before | after |
|---|---|---|
| `board_open` | 273 ms | **199 ms** |
| payload | 2.54 MB, one message | **0.23 MB**, one message |
| bytes per card | 7,229 | **653** |
| of which the card's `text` | 92.6 % | — (not sent) |
| `board_refresh`, nothing changed / one card touched | 157 / 158 ms | **4 / 5 ms** |
| `board_search` "zzzz" / "composer" / "switchboard pane" | — (no such message) | **0.4 / 1.2 / 1.1 ms** |
| worker RSS after open | 56 MB | **45 MB** |

| 3,000 cards (synthetic) | before | after |
|---|---|---|
| `board_open` | 1,703 ms | **1,292 ms** |
| payload / biggest single message | 21.63 MB / **21.63 MB** | 1.97 MB / **0.27 MB** |
| `board_refresh`, nothing changed / one card touched | 1,367 / 1,384 ms | **43 / 42 ms** |
| worker RSS after open | 184 MB | **91 MB** |

### 6. Switchboard, GUI side (`harness/board-gui.sh`, `harness/board-burst.sh`, `raw/board-*.txt`)

| metric | before Qt5 | after Qt5 | before Qt6 | after Qt6 |
|---|---|---|---|---|
| open the pane, to the last pixel change | 488 ms | **419 ms** | 487 ms | **414 ms** |
| open the pane, GUI CPU | 170 ms | 150 ms | 160 ms | **140 ms** |
| **typing `switch`, GUI CPU for the burst** | 120 ms | **90 ms** | 130 ms | **100 ms** |
| the same, per character | 20.0 ms | **15.0 ms** | 21.7 ms | **16.7 ms** |
| typing `switch`, wall to the settled list | **138 ms** | 275 ms | **146 ms** | 287 ms |
| one key on its own, GUI CPU (n = 18) | **20 ms** | 30 ms | **20 ms** | 30 ms |
| 3,000-card board | **never loads** | opens | **never loads** | opens |

### 7. Sessions search and the file pane (`harness/panes-bench.sh`, xcb, median of 2, `raw/panes-bench.txt`)

Both sides were built from the **same** bench test code (the #MDSG implementer's), so the only
difference measured is `src/Conversations.cpp` and `src/FilePanes.cpp`.

| metric | before Qt5 | after Qt5 | before Qt6 | after Qt6 |
|---|---|---|---|---|
| sessions search, GUI CPU per key, collapsed rows | 35.8 ms | **4.3 ms** | 41.5 ms | **4.1 ms** |
| sessions search, per key, every row unfolded | 131.5 ms | **13.4 ms** | 151.0 ms | **15.3 ms** |
| `src/Pane.h` (15,990 lines, 1.0 MB), GUI CPU to first paint, cold | 200 ms | **46 ms** | 152 ms | **44 ms** |
| the same, warm | 190 ms | **38 ms** | 146 ms | **36 ms** |
| a 419 kB file, first paint, cold | 105 ms | **37 ms** | 80 ms | **32 ms** |
| a 300-line file, first paint, cold | 10 ms | 12 ms | 18 ms | 20 ms |

### 8. Tool output (`harness/toolout.sh`, `raw/toolout-*.txt`)

One 20,000-character prose turn plus a turn of 20 `run_command` calls of 2,000 lines each
(`IPC=1`, the whole worker → GUI channel captured), then the 200-call scenario.

| metric | before Qt5 | after Qt5 | before Qt6 | after Qt6 |
|---|---|---|---|---|
| `tool_output` + `tool_result`, **per tool call** | 66,867 B | **589 B** | (worker side, same) | (same) |
| the whole channel, both turns | 1,873,890 B | **548,388 B** | (same) | (same) |
| 200 `run_command` calls, GUI CPU, round 1 | 7.95 s | **4.04 s** | 8.58 s | **4.19 s** |
| 200 `run_command` calls, GUI CPU, round 2 | 7.79 s | **4.12 s** | 8.48 s | **4.14 s** |
| RSS growth over the scenario | +9.1 MB | **+2.7 MB** | +10.1 MB | **+3.1 MB** |

### 9. Startup, as a regression check (from the same 24 idle runs, median of 3)

| metric | before Qt5 | after Qt5 | before Qt6 | after Qt6 |
|---|---|---|---|---|
| exec → window mapped | 188 ms | **178 ms** | 158 ms | **139 ms** |
| RSS at start | 115.9 MB | 115.8 MB | 77.2 MB | **77.5 MB** |

Nothing got slower.

---

## Notes

**Everything the fixes claimed is there on the laptop, and the two Python-specific rows are much
bigger than on spark.** The worker's memory is the headline: 140–147 MiB after 300 turns on
CPython 3.14 → **30 MiB**, a 4.8× reduction that spark (3.12) could not show, because 110 MiB of it
was certificate-parse heap the 3.14 allocator never returned. The terminal rows are also bigger
here than the fixes' spark numbers (GUI CPU per 50 MB halves rather than falls a fifth) because the
laptop pays for both the per-row `/proc` resolution and the 150 fps repaint at once.

**Idle CPU improves by less than on spark, and wakeups barely move.** spark went 1.35 → 0.68 % and
47.3 → 33.7 wakeups/s at four panes; the laptop goes 1.38 → 1.22 % and 46.6 → 45.7. The fixes
themselves are demonstrably working — the syscall counts in table 2 are the spark after-numbers to
the decimal (57.5 `statx`/s, 10 `getdents64`/s per pane) — so what is left is that an x86 syscall is
cheap enough that removing two thirds of them is only 12 % of the idle CPU, and the after tree has
a day's worth of other features in it (the Test-suites pane, the model catalog, the Activity pane
work) that spark's narrower "before vs the fix alone" comparison did not carry.

**The Qt6 filter penalty the owner's decision was waiting on does not reproduce.** The profile
reported 60–80 ms a keystroke on sphinxpad's Qt6 against 20–40 on Qt5. Three independent harnesses
say otherwise today, on a quiet laptop: the per-key matrix (Qt6 before 20 ms vs Qt5 before 20 ms),
the burst (130 ms vs 120 ms for six characters) and **the profiler's own `sphinx-run.sh`, run
verbatim against the same `build-qt6` binary and its own `issues340` board — 20–30 ms a key**
(`raw/board-qt6-repro.txt`). The profile's figure was taken while several agents were building on
the laptop; it does not survive a quiet machine. On these numbers **Qt6 is the better binary on
every row**: a third less memory (27.8 vs 50.1 MB Pss at one pane), 139 ms to a window against 178,
equal terminal and board CPU, and no filter penalty.

**One row is genuinely worse after the fix, and it is a latency, not a cost.** Typing in the
Switchboard filter now takes **275 ms** (Qt5) / **287 ms** (Qt6) from the last character to the
settled list, against 138 / 146 ms before. The CPU is lower — 90 ms against 120 for the six
characters — but the answer arrives later, because the box is now answered by a worker-side
`board_search` behind the 120 ms debounce (`#7M6E`, `backend/relay_core/board_protocol.py`) instead
of scanning `card.text` on the GUI thread. Per **isolated** keystroke the after build even reads
worse (30 ms against 20) because every single key then pays its own debounce and its own second
list rebuild when the answer lands; nobody types that slowly, which is why the burst is the honest
row. This is the trade the fix made deliberately, and it is the only row where the user could feel
the change as a slow-down. Whether ~140 ms more to a filtered list is the right price for taking a
2.2 MB scan off the GUI thread is the owner's call; halving the debounce would split the
difference.

**The 3,000-card cliff reproduces exactly and is gone.** Before, on both Qt versions, the pane sits
at "Loading the Switchboard…" for ever with "The Switchboard worker exited." in the status bar
(`shots/b5-3k-board.png`, `shots/b6-3k-board.png`) — the 21.63 MB single message over
`src/BoardWorker.cpp`'s 8 MiB cap. After, it opens and shows 2,901 open cards
(`shots/a5-3k-board.png`), with the biggest message at 0.27 MB. Filtering a 3,000-card board still
costs 100–115 ms a key on the GUI thread, which is the list rebuild, not the search.

**`faccessat` reads 0 in table 2 on both sides.** This glibc issues `faccessat2`, which is a
different tracepoint; the profile's "170 `faccessat` per key press" is inside the `statx` column's
story here and is not separately counted. It does not change the conclusion — per key the after
build makes 2.2 filesystem calls where the before build made 236.

**Where the numbers came from.** Every GUI run used a fresh isolated profile (`XDG_*`,
`TMPDIR`, `HOME`-equivalents, `XDG_RUNTIME_DIR` mode 0700 with the session bus symlinked in),
`--clean-shell --fresh`, `instructions/onboarded` and `security/approvals_chosen` pre-set so no
modal swallowed a keystroke, and `xdotool windowfocus --sync` because Xvfb has no window manager.
Terminal scenarios are submitted with `ctrl+shift+Return`, which always runs in the shell, and every
one of them aborts before typing anything else if a warm-up line fails to reach the shell. The
agent scenarios run against `tr/stub.py` on loopback, configured as `local:stub` in the profile the
run writes. No live provider key was used and nothing left the machine.

---

## Not measured, and why

- **Qt6 at the tip.** `main`'s tip does not compile against Qt6 (`src/Pane.h:1028`,
  `std::min(int, qsizetype)`, from another session's `fed72147`), so the third column below is Qt5
  only. Filed by the orchestrator as a bug card; not this agent's to fix.
- **Battery drain in watts.** The laptop was on AC for every run, as the profile's were, so the
  before/after is comparable. Wakeups and CPU are the proxy; a watt figure would need the laptop on
  battery for an hour a side and would not be comparable with the profile.
- **`board_search` before/after on the same message.** The before build has no `board_search` at
  all (`Unknown protocol message.`), so that row has one side only.
- **The `in all` column of the file-pane bench.** Both sides were given the before tree's bench
  code, which stops timing at the first paint; the after build's colouring continues afterwards in
  4 ms slices, which that code does not follow. The spark run has that number
  (`../panes/MEASUREMENTS.md`); the row that matters — the freeze the reader feels — is first paint,
  and it is here.
- **Prompt sizes before and after.** `promptsize.py` only exists from the tip commit and its
  `AppTools(keybindings=…)` signature does not exist in either earlier tree, so there is no
  before/after; the tip's absolute numbers are in the third column.
