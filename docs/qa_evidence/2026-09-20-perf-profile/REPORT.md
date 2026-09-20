# Relay performance profile — spark and sphinxpad (#PF4K, 2026-09-20)

Five Opus profilers measured a clean export of `main` (ccb31a8e, RelWithDebInfo, frame pointers) on
two machines. They measured only: no product code was changed. Each area has its own `FINDINGS.md`
with the commands that reproduce every number, the harness scripts and the trimmed raw output:
[engine](engine/FINDINGS.md) · [startup and idle](startup/FINDINGS.md) ·
[transcript](transcript/FINDINGS.md) · [Python worker](worker/FINDINGS.md) ·
[Switchboard and side panes](board/FINDINGS.md). This file is the orchestrator's synthesis; the
code fact behind findings 1, 2, 3, 5, 6 and 7 was re-read, and finding 2's cost re-timed, in the
checkout by the orchestrator.

| | spark | sphinxpad |
|---|---|---|
| CPU | aarch64, 20 cores | x86_64 i7-1365U, 12 threads (laptop, on AC, governor `performance`) |
| OS / Python | Ubuntu 24.04 / 3.12 | Ubuntu 26.04 / 3.14 |
| Qt | 5.15.13 | 5.15 (`build`, what CMake AUTO picks) and 6.10.2 (`build-qt6`, what the 26.04 `.deb` ships) |
| Clean build, wall | 1 m 25 s (`-j8`) | 2 m 19 s (`-j10`) |
| `perf` | `perf_event_paranoid=4`; `sudo -n perf` works | the same |

Only the libvterm terminal core builds on either machine (`RELAY_ENGINE_WITH_GHOSTTY=OFF`), so every
terminal number below is libvterm's. The core itself has not regressed since
[ENGINE-PERF.md](../../ENGINE-PERF.md): a Release build gives 46.9 MiB/s headless against the
2026-09-17 baseline's 45.

## How it performs on sphinxpad

It builds from a clean export with no extra packages and runs well. The laptop is slower than spark
on single-thread terminal throughput and equal or faster on most GUI work.

| Scenario | spark Qt5 | sphinxpad Qt5 | sphinxpad Qt6 |
|---|---|---|---|
| Exec → window mapped | 236 ms | 212 ms | **158 ms** |
| `cat` 50 MB, wall / GUI CPU | 1.37 s / 1.01 s | 1.92 s / 0.93 s | 1.79 s / **0.78 s** |
| `seq 1 2000000` | 2.07 s | 3.73 s | 3.74 s |
| Headless libvterm throughput (this build) | 37 MiB/s | 26 MiB/s | — |
| Stream a 20k-token reply, GUI CPU | 1.60 s | 1.84 s | 1.79 s |
| RSS at start | 155 MB | 119 MB | **80 MB** |
| relay Pss, one pane | — | 39.5 MB | **27.3 MB** |
| Idle, 1 pane: CPU / wakeups per s | 0.53 % / 20.9 | 0.48 % / 18.5 | 0.52 % / 20.7 |
| Idle, 4 panes | 1.43 % / 59.8 | 1.37 % / 47.0 | 1.50 % / 48.0 |
| Switchboard open, 337 cards | 457 ms | 508 ms | 632 ms |
| Switchboard filter, per keystroke | 30–50 ms | 20–40 ms | **60–80 ms** |
| Worker spawn → ready (bytecode cached / not) | 71 / 259 ms | 83 / 278 ms | — |
| Worker Pss after 300 turns | 39.9 MiB | **151.8 MiB** | — |
| Shutdown, 1 pane | 63.5 ms | 63.6 ms | 63.9 ms |

**Qt5 or Qt6 on the laptop.** Qt6 is 25 % faster to a window, uses a third less memory, and is equal
on terminal and streaming CPU. It is slower in one place — the Switchboard's filter box, 2.5× per
keystroke (finding 7). A Qt6 build with tests on stopped at `tests/wordwrap_test.cpp`
(`QChar::surrogateToUcs4(uint, QChar)` has no Qt6 overload); that was fixed in d2923c9b and is the
only Qt6 compile failure in the tree.

**Two things are specific to the laptop.** The worker's memory growth (finding 2) is four times worse
on Python 3.14 than on 3.12. And the idle wakeups (finding 3) are what decides battery life; the
numbers above were taken on AC, and minimising the window does not reduce them.

## Findings, ranked by what the user feels

### 1. The Switchboard stops loading at about 1,160 cards, silently — [board 1, 2](board/FINDINGS.md)
`board_open` is one JSON line; `src/BoardWorker.cpp:10` caps the buffer at 8 MiB and `:27` kills the
worker past it; the error goes to a status bar this layout never shows, so the pane says "Loading
the Switchboard…" forever. 7,211 B per card, 92.6 % of it the card's whole body and thread
(`backend/relay_core/board_protocol.py:1082`) shipped for one substring test. This board is at 337
cards (2.4 MB) and grows daily; about 128 long cards would do it too. Reproduced on both machines and
both Qt versions. **Fix:** search in the worker (or ship a digest), and chunk `board_open`.
Also removes finding 7 and takes open from ~457 to ~300 ms.

### 2. The worker reloads the system CA store for every model request — [worker 1](worker/FINDINGS.md)
`backend/relay_core/provider.py:744` calls `urllib.request.build_opener()` per request, which builds
an SSL context and parses every system certificate: **11.8 ms CPU per request** (re-timed by the
orchestrator: 12.0 ms), 52 % of all worker CPU, paid by the model call and both title/summary side
calls, even for `http://127.0.0.1`. With a module-level cached opener (six lines), over 300 stub
turns: worker CPU 16.2 → 5.5 s (spark), 7.7 → 5.1 s (sphinxpad); time to first request byte
17 → 5 ms; worker Pss after 300 turns 39.9 → 27.4 MiB (spark) and **151.8 → 31.2 MiB** (sphinxpad —
109 MiB of unreturned certificate-parse heap per worker on Python 3.14). Caveat: proxy environment
is then read once per process.

### 3. An idle pane wakes 13 times a second and makes ~470 syscalls a second — [startup 1](startup/FINDINGS.md), [engine 5](engine/FINDINGS.md), [transcript 4](transcript/FINDINGS.md)
Linear in visible panes, identical on both architectures and both Qt versions, and **unchanged when
the window is minimised** (44.0 → 44.8 wakeups/s). Causes, each located:
- `Pane::pollGuestEvents()` (`src/Pane.h:1781`, from the 80 ms `pollShell()`) lists a directory that is
  empty unless a guest agent runs: 23.9 % of sampled idle CPU. Mtime-gate it as `pollShell()` already
  gates `state.json` (13.3 µs → 0.45 µs).
- **`QSettings` constructed on hot paths** — one root cause found independently by three profilers.
  Each construction stats 7–8 config paths. `metersEnabled()` (`src/PaneUsage.cpp:457`): 75 statx/s
  for one boolean. `voiceHoldKey()`/`voiceEnabled()` in the app event filter
  (`src/Pane.h:3485-3489`) plus `updateGhost()` (`:497`): **417 statx + 170 faccessat, ~1.2 ms, on
  every key press and release**. `Pane::showToolOutput()` (`src/Pane.h:4407`) and
  `relay::log::level()` (`src/Logging.cpp:76`): 9.6 % of GUI cycles in a tool-heavy turn. Cache them
  behind the existing settings-change notification.
- `tunePoll()` (`src/Pane.h:14893`) tests only `isVisible()`, which stays true when iconified; adding
  `isMinimized()` gives the existing 80 → 400 ms slowdown (~50 → ~14 wakeups/s minimised).
- `RichEditor`'s caret blink (`src/RichEditor.cpp:208-222`) never stops; `TerminalView.cpp:2713` is
  the right pattern.
- `relay::usage::walkTrees` walks `/proc` for every pane in every tab at 2.5 Hz
  (`src/RelayWindow.h:7263-7275`), 3.7 % of idle samples.

### 4. Every painted row resolves the working directory from `/proc` — [engine 2](engine/FINDINGS.md), [transcript 1](transcript/FINDINGS.md)
Found independently by two profilers. `restLinkColumns()` (`engine/view/TerminalView.cpp:1858`, from
`paintRow`) calls `currentDirectory()`, which ends in a `readlink` + `stat` of `/proc/<pid>/cwd` and
takes the core mutex the pty thread is feeding under: 91–94 % of all GUI-thread `statx`, 1,468/s
streaming prose and 9,381/s in a tool-heavy turn, ~4,400 lock acquisitions/s. In the same function
`hyperlinkAt()` (`:1855`) converts every cell of the row to read one link id the caller already
holds. A/B with colour-links off: GUI CPU 1.01 → 0.81 s per 50 MB of `cat`; 1.57 → 1.44 s for a
streamed reply. **Fix:** resolve the directory once per frame; add a `hyperlinkUri(id)` lookup.
Expected 10–20 % of GUI CPU during output. This is what card #9MYY's first item turns out to be: the
per-hover path it named costs 0.14 ms per cell and does not matter; the per-painted-row path does.
#9MYY's other two items measured below noise.

### 5. The terminal repaints at ~150 fps during output — [engine 1](engine/FINDINGS.md)
`TerminalView::scheduleFrame()` (`engine/view/TerminalView.cpp:452-454`) calls it a flood when more
than 512 KiB arrived since the last frame; at a 4 ms interval that needs 128 MiB/s and libvterm does
37. Measured by uprobe: 590 `paintEvent` in 4 s. About 1.0 s of GUI CPU per 50 MB for frames no panel
shows. **Fix:** a 16 ms interval whenever bytes are arriving, keeping 4 ms for the first frame after
idle, so echo latency (p50 10.6 ms key → paint) is untouched. Expected 1.0 → 0.42 s.

### 6. Sessions search and large files stall the GUI thread — [board 4, 5](board/FINDINGS.md)
- Sessions search-as-you-type costs **100–190 ms GUI CPU per key** (300–900 ms wall) on the owner's
  863-conversation store while the pane reports "40 ms": it is not SQL, it is `RowDelegate` building
  a `QTextDocument` from HTML twice per row for ~100 rows on every rebuild
  (`src/Conversations.cpp:448-451`, `:486-488`). Cache the laid-out document: ~4×.
- Opening `src/Pane.h` (15,830 lines) in the file pane freezes the GUI for **1,965 ms cold / 688 ms
  warm**; ~520 ms is KSyntaxHighlighting rehighlighting every block synchronously
  (`src/FilePanes.cpp:1312-1320`). Highlight in chunks after first paint: first paint ~200 ms.

### 7. Switchboard filter and refresh — [board 3, 6](board/FINDINGS.md)
Each filter keystroke scans 2.2 MB on the GUI thread (30–50 ms spark, 60–80 ms sphinxpad Qt6); finding
1's fix removes it. Every `board_refresh` parses the whole board twice whatever changed (179 ms at 337
cards, 1.6 s at 3,000): `_rows()` then `board.check()` again. Reusing the parsed cards is ~40 % off at
once; a stat cache takes it to ~15 ms.

### 8. Costs that grow — [transcript 2, 3](transcript/FINDINGS.md), [engine 3, 4](engine/FINDINGS.md), [worker 2, 3](worker/FINDINGS.md)
- **Tool output is sent, parsed and thrown away.** `tool_output` + `tool_result` are 81 % of
  worker → GUI bytes; in the default configuration the GUI's only use is counting newlines
  (`src/Pane.h:9699`). `QJsonDocument::fromJson` is 7.1 % of GUI cycles. Send a line count unless
  "show tool output" is on. Protocol change, medium risk.
- **Per-turn GUI cost grows with the conversation**: 65 ms/turn at turn 25 → 86 ms at turn 225.
  `resolveFoldAnchors` walks the full scrollback on every block close and on a 250 ms heartbeat;
  `FoldLayer::retainAnchored` is O(folds²).
- **Any fold anchor on screen forces a full-content repaint** (`TerminalView.cpp:493`; collapsed
  anchors count, and agent panes always have them): 4.8 rows per frame instead of 1.0.
- **Scrolled lines are converted and stored at full grid width**
  (`engine/core/LibVtermCore.cpp:305-309`): −30 % throughput at 280 columns, 2.8 kB per scrollback
  line (28 MB per 10k lines). Trim before converting: ~1.7 kB.
- **The installed backend can never write `__pycache__`** (`CMakeLists.txt:507` installs `.py` into a
  root-owned directory): every worker start recompiles 65 modules, 259 ms instead of 71 ms, times nine
  workers in a 3-tab, 6-pane session. Byte-compile at package time.
- **The conversation index rewrites every row of a session on each autosave**
  (`backend/relay_core/conv_index.py:1110/1128`), on the turn thread: 46 ms per save on the owner's
  largest session. His `index.db` is 119.6 MB with a 31.5 MB freelist.

## For the owner to decide
1. **One worker per pane costs ~27 MB each** (73 / 161 / 221 MB tree Pss at 1 / 4 / 6 panes). The
   workers idle at zero CPU, so it is memory only. Sharing a worker trades against the per-pane
   `MemoryMax` scope.
2. **The system prompt is 25 KB on every request, 41 KB with a board.** A product decision.
3. **Session files are rewritten whole on each save** (~10 ms on the largest real session). Changing
   it is a format change.
4. **Qt6 for the 26.04 build** is the better binary on these numbers, once finding 7 is dealt with.
5. `isolation::available()` (`src/Isolation.h:29`) blocks the GUI thread on `systemd-run` for up to
   3 s before the first window. 0–10 ms when healthy; a window-less hang when not. Move it off the
   startup path, or accept it.

## Measured and fine — do not re-profile
Key → paint latency (p50 10.6 ms) and dirty-row repaint (exactly one row per echo); resize with 10k
lines of scrollback (20–30 ms); scrollback paging (3–4 ms); SGR-heavy output (~135 MiB/s); the glyph
cache. Per-delta streaming cost is flat across a 40k-character reply, the event loop stays under 2 ms
at p99 while streaming, and a 300-turn conversation resumes to a window in 511 ms. Shutdown (64 ms at
one pane on all three builds; 164 ms spark / 214 ms sphinxpad at six panes); 20 open/close cycles leak
no fds, children, runtime directories or RSS; a 6-pane, 3-tab layout restores in 317 ms; the 98 MB
binary is 90 MB of unmapped debug info. Idle workers: 0 ms CPU in 60 s, no poll loops; the tool wrapper
adds ~1 ms; a 50 MiB tool output is captured in 32 ms with bounded RSS; no O(n²) history handling and
no leak over 300 turns; a full index rebuild of 667 sessions takes 1.6 s. `relay-board.py check`
0.10 s at 337 cards and linear to 3,000 (there is no PyYAML; the parser is hand-written and 4.8 % of
the profile); board rows are delegate-painted, not per-card widgets; board scrolling, expand and
reorder; the Models pane (flat profile, no fetch).

## Not measured, and why
- Paint counts (fps) on sphinxpad: driving a fresh pane there by `xdotool` was unreliable. A mistyped
  line was handed to the agent, and because Relay Free configures itself on a fresh profile, **one
  agent turn reached the hosted service** before the profiler added a guard and stopped. The laptop's
  GUI CPU figures are measured; only the fps figure is spark-only.
- Interactive Switchboard figures at 3,000 cards: finding 1 stops the pane loading. Worker-side
  scaling at that size is measured.
- The Activity pane re-rendering the whole reasoning block at 4 Hz was read in the code but not
  measured: the pane could not be opened deterministically from the harness.
- GhosttyCore: it does not build on either machine.
- Live-provider latency: stub provider only, by design.
- Battery drain in watts: the laptop stayed on AC. Wakeups and CPU are the proxy.

## Incidental, not performance
The Switchboard's watcher watches directories, so an append to an existing card or thread often does
not reach the pane: about 21 of 60 writes did in the refresh test. Filed separately.
