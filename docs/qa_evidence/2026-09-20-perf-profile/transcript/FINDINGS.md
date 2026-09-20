# #PF4K — the transcript: what the GUI does while an agent turn streams

Area: `src/Pane.h` (the agent side of the pane), `src/CallLines.cpp`, `src/AgentInternalsView.cpp`,
`src/SubagentTranscript.cpp`, `src/RequestLedger.cpp`, `src/SessionInfo.cpp`,
`engine/view/FoldLayer.cpp`, `engine/view/ProseSpans.h`, and the worker↔GUI channel
(`docs/AGENT-SESSIONS-PROTOCOL.md`). Line numbers are from the clean export of `main` at
`ccb31a8e`; paths are relative to the repo. No product code was changed and nothing was committed.

## Machines and state

| | spark | sphinxpad (Qt5) | sphinxpad (Qt6) |
| --- | --- | --- | --- |
| CPU | aarch64, 20 cores | x86_64 i7-1365U, 12 threads | same |
| OS / Qt | Ubuntu 24.04, Qt 5.15.13 | Ubuntu 26.04, Qt 5.15 | Ubuntu 26.04, Qt 6 |
| build | `/tmp/claude-1000/pf4k/build`, RelWithDebInfo + frame pointers | `~/relay-perf/build` | `~/relay-perf/build-qt6` |
| power / governor | mains | AC (`/sys/class/power_supply/AC*/online` = 1), `performance` | same |
| load average during runs | 0.5 – 2.3 (four other #PF4K agents were profiling) | 0.2 – 0.6 | 0.2 – 0.6 |
| RSS, idle Relay, one pane | 152.7 MB | 115.5 MB | **77.8 MB** |
| idle GUI-thread CPU, one pane | 0.44 % of a core | 0.46 % | 0.4 % |

The Ubuntu 26.04 `.deb` ships Qt6, so the third column is what a sphinxpad user gets.
**Qt6 costs the same CPU as Qt5 and 37 MB less memory at rest** (`sphinxpad/summary.txt`; the
configure line is `-- Relay: building against Qt6`).

Every run: Xvfb on a private display, a fresh `HOME` / `XDG_*` / `TMPDIR` under the agent's scratch
dir, `RELAY_KEYRING=off`, `--clean-shell`, window 1400×900 (170 columns × 42 rows), no provider
key. Turns are driven by `stub.py` in this directory — a loopback OpenAI-compatible endpoint that
streams a chosen shape at a chosen rate, keyed on a keyword in the prompt (the worker appends
user-role messages of its own at the end of a turn, so the *last* user message cannot pick the
scene). The pane is put in **agent** input mode by seeding
`$XDG_DATA_HOME/relay/state/windows.json` with `input_mode: agent` (`src/WindowState.h`), so
nothing a run types can be routed to the shell, and the chip is read back from the pixels as a
check. GUI-thread CPU is `utime+stime` from `/proc/<pid>/task/<pid>/stat` (the main thread is the
GUI thread), sampled every 200 ms by `drive.sh` and reduced by `an.py`.

## Headline numbers

GUI-thread CPU seconds for the whole run. Three runs each for the A/B in finding 1; single runs
elsewhere, and repeats of (a) agreed within 6 %.

| scenario | spark | sphinxpad Qt5 | sphinxpad Qt6 |
| --- | --- | --- | --- |
| (a) 40 000-char reply, 20-char deltas at **100/s** (2 000 deltas) | 1.60 s · 7.0 % of a core · **0.72 ms/delta** | 1.84 s · 0.83 ms/delta | 1.79 s |
| (a) the same 2 000 deltas at **1 000/s** | 0.71 s · 19.7 % · 0.30 ms/delta | 0.84 s | 0.86 s |
| (b) 56 000 chars, 40 fenced code blocks + 40 markdown tables, 300/s | 1.78 s, RSS **+11.0 MB** | 2.04 s, +13.0 MB | 2.11 s, +14.7 MB |
| (c) 40 000 chars of reasoning at 200/s, then a 2 000-char reply | 0.85 s | 0.77 s | — |
| (d) 200 `run_command` calls, 2 000-line (156 KB) output each | 6.81 s = **34 ms/call**, RSS +8.4 MB | 8.84 s = 44 ms/call | 9.24 s |
| (e) 300 agent turns built up back to back | 21.9 s = **73 ms/turn**, RSS +5.9 MB | — | — |
| (e) that 300-turn conversation resumed in a fresh process | window in 511 ms, 0.18 s GUI CPU, RSS 147.7 MB | — | — |

Cost per delta does **not** grow with the length of one reply: across the 2 000 deltas of (a) it
stayed between 0.66 and 0.90 ms with no trend (`spark/a100.cpu.txt`). It *does* grow with the
length of the conversation — finding 3. The per-delta figure falls at 1 000 deltas/s because the
view's frame timer coalesces (4 ms, `engine/view/TerminalView.cpp:455-465`): at 100 deltas/s each
delta gets its own frame, at 1 000/s about four deltas share one.

---

## 1. Every painted transcript row resolves the pane's working directory from the filesystem

**What the user feels.** A streaming reply costs about 9 % more CPU than it needs to, and a
tool-heavy turn makes the GUI thread issue about **15 000 path-lookup syscalls a second**. On a
laptop on battery this is the difference between a quiet fan and a noisy one; it is also the single
largest avoidable cost in the paint path.

**Measurement.** `bpftrace` on `sys_enter_statx` filtered to the GUI thread, with user stacks
(`spark/ustack.txt`): **94 % of all `statx` calls** made by the GUI thread during a prose stream
come from one stack —

```
statx  <-  QFileInfo::symLinkTarget()
       <-  relay::TerminalView::currentDirectory()
       <-  relay::TerminalView::restLinkColumns(int, std::vector<char>*)
       <-  relay::TerminalView::paintRow(...)
       <-  relay::TerminalView::paintEvent(...)
```

Raw syscall counts on the GUI thread over a 15 s window (`spark/sysprose.syscalls.txt`,
`spark/systools.syscalls.txt`; aarch64 numbers, 291 = `statx`, 48 = `faccessat`, 78 = `readlinkat`,
79 = `newfstatat`):

| | prose at 200 deltas/s | 60 tool calls, 2 000-line output |
| --- | --- | --- |
| `statx` | 22 022 (1 468/s) | 140 715 (**9 381/s**) |
| `faccessat` | 7 983 | 48 510 |
| `readlinkat` | 3 397 | 36 608 |
| `newfstatat` | 4 620 | 20 782 |

`perf` on the prose run puts `paintRow` at **52.7 %** of GUI-thread cycles and `restLinkColumns` at
**30 %** of them (`spark/a100.perf-flat.txt` and the inclusive report). Turning the feature off
(`[terminal] colour_links=false`) is the A/B: three runs each of scenario (a) —

| | run 1 | run 2 | run 3 | mean |
| --- | --- | --- | --- | --- |
| links at rest **on** | 1.55 s | 1.63 s | 1.54 s | **1.57 s** |
| links at rest **off** | 1.37 s | 1.48 s | 1.46 s | **1.44 s** |

**Reproduce.** `./battery6.sh nolinks2 links2` (and `spark/links*.cpu.txt`,
`spark/nolinks*.cpu.txt`). The stacks: run `KEEP=1 ./drive.sh ust 4 'please stream
zprose400000x20r200 now'`, then
`sudo -n bpftrace -e 'tracepoint:syscalls:sys_enter_statx /tid == <relay pid>/ { @[ustack(perf,16)] = count(); }'`.

**Root cause.** `engine/view/TerminalView.cpp:1858` — `restLinkColumns()` calls
`currentDirectory()`, and `restLinkColumns()` is called once per painted row from
`engine/view/TerminalView.cpp:724`. `currentDirectory()`
(`engine/view/TerminalView.cpp:1624-1647`) either stats the OSC 7 directory
(`QFileInfo(dir).isDir()`) or falls through to
`QFileInfo("/proc/<pid>/cwd").symLinkTarget()` at `engine/view/TerminalView.cpp:1641`. Either way
it is a filesystem round trip, made 42 times per frame at up to 250 frames a second. The result is
then only used to build the cache key at `engine/view/TerminalView.cpp:1861`, so the same string is
recomputed for every row of the same frame.

A second, smaller offender is in the same function: `engine/view/TerminalView.cpp:1854-1855` asks
the core `hyperlinkAt(frameRow, col)` for the row's URI. For a row still on the screen that goes
through `LibVtermCore::Impl::peekLine` → `readScreenRow` (`engine/core/LibVtermCore.cpp:226`),
which converts **every cell of the row** — including two `vterm_state_convert_color_to_rgb` calls
per cell — to read one `link` id. `vterm_state_convert_color_to_rgb` is the **top self symbol of
the whole prose profile at 10.28 %**, and `relay::Line::cellCodepoints` the second at 9.08 %. The
caller already holds that id: `line.cells[col].link` is read three lines above.

**Proposed fix.** Two edits, no behaviour change.
1. Resolve the directory once per paint. Compute `currentDirectory()` at the top of `paintEvent()`
   (or cache it on the view, invalidated by the OSC 7 signal and by `shellPid()` changing) and pass
   it into `restLinkColumns()`. Cache `logical.text.trimmed().isEmpty()` the same way if cheap.
2. Add `VtCore::hyperlinkUri(uint32_t id)` — a direct `d->linkUris[id]` lookup, the same two lines
   `LibVtermCore::hyperlinkAt` ends with — and have `restLinkColumns` pass
   `line.cells[col].link` to it instead of `hyperlinkAt(frameRow, col)`. The three other
   `hyperlinkAt` callers (`:1752`, `:2228`, hover and click) run once per gesture and can stay.

**Expected gain.** The A/B bounds the whole feature at 0.13 s of 1.57 s = **8 % of the GUI CPU of a
streaming reply**, and edit 1 removes essentially all of that (the directory lookup *is* the part
the A/B skips) while keeping links coloured. Edit 2 removes the row conversion that the profile
attributes 10.3 % + part of 9.1 % of cycles to; in a tool-heavy turn, where `paintRow` is 20 % and
the rows are long, the two together should be worth **10–20 % of GUI-thread CPU during a turn**,
and they remove ~15 000 syscalls a second from the GUI thread. Risk: low. Edit 1 is a hoist; the
only visible difference would be a link resolved against a directory that changed mid-frame.
Edit 2 needs the new method on both cores (`GhosttyCore` does not build on this machine — see
"could not be measured").

## 2. The GUI parses ~66 KB of tool output per tool call and throws all of it away

**What the user feels.** A turn that reads or runs anything big is three to five times more
expensive in the GUI than the same turn without tools (34 ms of GUI CPU per call on spark, 44 ms on
sphinxpad), and the worker→GUI pipe carries megabytes that nothing displays.

**Measurement.** The channel was captured by putting a `python3` shim on `PATH` that tees both
directions of `backend/worker.py`'s stdio (`drive.sh IPC=1`; table in
`spark/ipc-per-turn.txt`). One 20 000-char prose turn plus one turn of 20 `run_command` calls whose
output is 2 000 lines (156 KB) each:

| message | n | bytes | avg | share of worker→GUI |
| --- | --- | --- | --- | --- |
| `delta` | 1 009 | 51 768 | 51 B | 3.2 % |
| `tool_output` | 20 | 664 500 | 33 225 B | **40.5 %** |
| `tool_result` | 20 | 672 082 | 33 604 B | **40.9 %** |
| `presets` | 3 | 223 517 | 74 505 B | 13.6 % |
| everything else | 70 | 30 349 | — | 1.8 % |

Nothing resends the conversation: a `delta` is 51 bytes of JSON for 20 characters of text, and no
message carries the history. GUI→worker for the same two turns is 17 messages / 212 KB, all of it
`configure` (73 KB) and `app_catalog` (2 × 64 KB) at startup.

`perf` on scenario (d) puts `QJsonDocument::fromJson` at **7.1 %** of GUI-thread cycles and the
worker-event `handle()` at 16.7 % (`spark/tools.perf-inclusive.txt`).

**Reproduce.** `IPC=1 ./drive.sh ipc 30 'please stream zprose20000x20r200 now' 'please run ztools20x5 now'`,
then the reducer at the head of `spark/ipc-per-turn.txt`.

**Root cause.** Two copies of the same output reach the GUI and neither is kept.
- `src/Pane.h:9691-9707`, the `tool_output` branch: `const QString text = event.value("text").toString();`
  materialises the whole chunk as UTF-16 (66 KB for a 33 KB message), and in the default
  configuration the only thing done with it is `m_toolLines += text.count('\n')` at
  `src/Pane.h:9699`. The text is used only when the Activity pane is open or
  `agent/show_tool_output` is set.
- `src/Pane.h:9754`, the `tool_result` branch: `result` carries the tail of the same output in
  `result.output`; the handler reads only `exit_code` and the `label`/`diff` beside it. The fold
  fetches the real text on demand with `tool_output_get` (`foldRequested`, `src/Pane.h:5282`), so
  the copy in the event is dead weight.

**Proposed fix.** The GUI already tells the worker its preferences over `set_agent_options`
(`src/Pane.h:3135`). Add one option — "stream tool output" — set from `showToolOutput() ||
m_internals`, and when it is false have the worker send `{"event":"tool_output","lines":N}` instead
of the text, and omit `result.output` from `tool_result` (the fold path already round-trips for
it). No GUI surface loses anything: the row's live counter wants the line count, not the lines.

**Expected gain.** 81 % of the worker→GUI bytes of a tool-heavy turn (1.34 MB of 1.64 MB here;
~13 MB over the 200-call scenario), the JSON parse that goes with them (7.1 % of GUI cycles
measured), and the QString materialisation. On scenario (d) that is of the order of **1 s of the
6.8 s** of GUI CPU, plus the allocator churn. Risk: low but it is a protocol change, so it needs
the version note in `docs/AGENT-SESSIONS-PROTOCOL.md` and a worker that keeps sending the text to
an older GUI that did not ask.

## 3. Cost per turn grows with the length of the conversation

**What the user feels.** The 250th turn of a session costs about a third more GUI CPU than the 25th,
for the same reply. It is not yet painful at 300 turns, but it is the shape that becomes painful.

**Measurement.** 300 turns typed back to back, each answered with `ok.`
(`spark/turns-growth.txt`); about 50 turns land in each 10 s window:

| turns so far | GUI CPU | ms per turn |
| --- | --- | --- |
| 1 – 50 | 32.3 % | 65.4 |
| 51 – 100 | 33.9 % | 68.0 |
| 101 – 150 | 36.4 % | 73.4 |
| 151 – 200 | 38.0 % | 77.2 |
| 201 – 250 | 43.0 % | **86.2** |

Total 21.9 s of GUI CPU for 300 turns, RSS 152.3 → 158.2 MB (20 KB a turn — the conversation log on
disk is 5.1 MB). 73 ms of GUI-thread CPU for a three-character reply is itself the headline: the
turn's fixed cost dwarfs its content.

**Reproduce.** `./turns.sh 300`.

**Root cause.** Two accumulating walks, both re-run whenever a transcript block closes.
- `TerminalView::resolveFoldAnchors()` (`engine/view/TerminalView.cpp:2094`) calls
  `VtCore::hyperlinkRuns()` (`engine/core/LibVtermCore.cpp:1073`) once for the `relay://call/`
  prefix and again for `relay://prose/` (`:2113` and `:2117`). `hyperlinkRuns` is a walk over
  **every cell of the whole scrollback plus the screen** — its own comment
  (`engine/core/LibVtermCore.cpp:1069-1072`) says "about 20 ms for a 100 000-line history here,
  which is why callers only run it on resize, trimming and clearing". It is in fact run on every
  `setProseBlock` / `setFoldContent` / `setFoldExpanded` (each ends in `invalidateFoldAnchors()`,
  `engine/view/TerminalView.cpp:2147`) and on a 250 ms heartbeat while any fold is anchored
  (`engine/view/TerminalView.cpp:469`). Relay opens and closes a prose block at every change of
  ink, so a turn with three tool rows closes six of them. In the tool-heavy profile
  `resolveFoldAnchors` is 5.0 % inclusive with `hyperlinkRuns` 2.1 % self.
- `FoldLayer::retainAnchored()` (`engine/view/FoldLayer.cpp:354`) tests membership with
  `seen.contains(f.uri)` on a `QVector<QString>` (`engine/view/FoldLayer.cpp:359`) — O(folds²) in
  string comparisons — and every `setAnchor()` inside the same loop calls `rebuildAnchors()`
  (`engine/view/FoldLayer.cpp:336-344`), which re-sorts the whole anchor list. The fold count grows
  with the conversation: one per prose block, one per tool row, one per reasoning block.

**Proposed fix.**
- Make `retainAnchored` take a `QSet<QString>`, and batch the `setAnchor` loop: set the anchors
  first and call `rebuildAnchors()` once at the end (add a private `setAnchorNoRebuild`). Pure
  bookkeeping; no behaviour change.
- Give `hyperlinkRuns` an incremental path. The ring already knows how many lines it has pushed;
  cache the runs and re-walk only the rows added or trimmed since the last walk
  (`d->pushed` / `d->firstId()` bracket it exactly), rebuilding from scratch only on resize and
  clear — which is what the function's own comment assumes its callers do.
- Stop the 250 ms heartbeat when no fold is *expanded*: at the print width prose folds are stood
  aside (`FoldLayer::takenOver`, `engine/view/FoldLayer.cpp:329`), so nothing needs re-anchoring
  while the pane is not resized.

**Expected gain.** Estimated, not measured, because it needs a code change: the growth in the table
above is 21 ms per turn between turn 25 and turn 225, i.e. about 0.1 ms per turn per turn already
in the transcript, and both walks above are linear in that. Removing them should flatten the curve
to its 65 ms intercept — **a quarter of the GUI cost of a turn by turn 250, and more beyond it**.
Risk: medium. `hyperlinkRuns` incrementality has to get scrollback trimming right; the
`retainAnchored` and heartbeat changes are low risk and can land on their own.

## 4. A per-event `QSettings` on the tool-output path

**What the user feels.** Part of the syscall load in finding 1's table, and about a twentieth of the
GUI CPU of a tool-heavy turn.

**Measurement.** `perf` on scenario (d): `QSettings::QSettings(QSettings::Scope, QObject*)` is
**9.6 % inclusive** of GUI-thread cycles, of which `Pane::showToolOutput` is **5.0 %**
(`spark/tools.perf-inclusive.txt`). Each construction re-stats the whole XDG search path — in the
`strace` of a startup, eight `statx` per construction across
`/etc/xdg/…`, `/etc/xdg/xdg-plasma/…`, `~/.config/kdedefaults/…` and the profile's own
`RelayTerminal/relay.conf` — plus a `QFileInfo::lastModified()` that pulls in
`/etc/localtime` (8 771 of those in a 20 s trace).

**Reproduce.** `SYSCALL_SECS=15 ./drive.sh systools 60 'please run ztools60x10 now'` for the counts;
`RELAY_BIN=./straced-relay ./drive.sh …` for the paths.

**Root cause.** `src/Pane.h:4407` — `static bool showToolOutput() { return QSettings().value("agent/show_tool_output", false).toBool(); }` — is called on every `tool_output` event
(`src/Pane.h:9702`) and again on every `tool_started` (`src/Pane.h:9751`). The same shape is
`src/Logging.cpp:76`, `Level level()`, which constructs a `QSettings` inside every
`relay::log::write()` — including the calls whose level is then dropped, and `Pane::logEvent`
(`src/Pane.h:9354`) builds its message string before that check. `relay::usage::metersEnabled()`
does it from `RelayWindow::refreshPaneStatus()` on a timer (visible in `spark/ustack.txt`).

**Proposed fix.** Cache these three in statics refreshed from
`relay::SettingsWatch::instance()` (`src/SettingsPane.h:152`), which already fires when Options
writes. `showToolOutput()` and `relay::log::level()` are the two on hot paths; `logEvent` should
also check the level before it formats.

**Expected gain.** The 5.0 % that `perf` attributes to `showToolOutput` directly, most of the rest
of the 9.6 %, and a large share of the `statx`/`faccessat`/`readlinkat` counts in finding 1's table.
Risk: low, as long as the watch is wired (a stale cached setting would make an Options toggle take
effect only on the next pane).

## 5. A ~70 ms hitch at the start of every turn

**What the user feels.** One dropped frame when a turn begins. Everything after it is smooth.

**Measurement.** `bpftrace` on the GUI thread's `ppoll` entry/exit — the busy time between two
event-loop iterations is exactly an event-loop stall (`spark/bpf.ppoll.txt`,
`spark/bpftools.ppoll.txt`):

| | prose at 200 deltas/s | 60 tool calls, 2 000-line output |
| --- | --- | --- |
| event-loop iterations in 18 s | 8 258 (459/s) | 16 804 (933/s) |
| median busy time | 16–32 µs | 256–512 µs |
| iterations over 4 ms | 6 | 4 |
| longest | **64.5 ms**, once, at turn start | **81.1 ms**, once, at turn start |

**Reproduce.** `BPF_SECS=18 ./drive.sh bpf 30 'please stream zprose40000x20r200 now'`.

**Root cause.** Not isolated — the probe records the gap, not the stack. It is a single event at
the start of a turn, before the first delta, which is where the pane opens the inline region, holds
the shell resize, sets up the reasoning fold and its anchor, and the view takes its first forced
full frame.

**Proposed fix.** None proposed yet: it needs one more measurement (the same `bpftrace` with a
`ustack` taken when the gap exceeds 40 ms) to say what to move. Everything else on the event loop is
already well inside a frame.

**Expected gain.** One frame per turn. Listed last because it is the only responsiveness defect and
it is small.

---

## Measured and fine — do not re-profile these

- **Typing and scrolling during a stream.** 99.9 % of event-loop iterations finish inside 2 ms in
  both scenarios above; nothing else in 35 000 iterations exceeded 4 ms. The prompt box stays
  responsive.
- **Cost per delta against position in a reply.** Flat: 0.66–0.90 ms across 2 000 deltas of a
  40 000-char reply, no trend (`spark/a100.cpu.txt`). Markdown is rendered incrementally
  (`MarkdownAnsi::feed`), the word wrapper is incremental, and `ProseCollector::feed`
  (`engine/view/ProseSpans.h`) only sees the new chunk — there is no re-render of the whole reply
  per delta, no re-join of the whole string, no scroll-to-bottom per delta.
- **The reasoning fold.** 0.85 s of GUI CPU for 40 000 characters of reasoning at 200 deltas/s.
  The 250 ms coalescing and the `text.right(12000)` cap in `Pane::thinkingFoldLines`
  (`src/Pane.h:4718`) do their job; it costs less than the same volume of prose.
- **Session resume of a 300-turn conversation.** Window in 511 ms (the same as a cold start), 0.18 s
  of GUI CPU in the 40 s after it, RSS 147.7 MB — *lower* than a fresh pane. 1 503 lines of
  scrollback were replayed (cap 5 000, `relay::windowstate::kScrollbackMaxLines`) and the pane came
  back with "Session loaded · 300 turn(s)". Quitting the 300-turn pane on SIGTERM took 104 ms.
- **The delta message itself.** 51 bytes of JSON per 20 characters. No message on the channel
  resends the conversation or the pane state; `pane_state` is coalesced
  (`relay::panestate::Publisher`, `src/PaneState.h:143`) and `thinking_delta` is capped at 200 000
  characters per turn.
- **Synchronous work on the GUI thread during a turn, other than finding 1.** `strace` of the GUI
  thread shows **no `git` invocation, no process spawn** (all 48 `clone3` calls are
  `CLONE_THREAD`, at startup) and only 18 `fdatasync` in 20 s. The conversation log is written by
  the worker, not the GUI.
- **Qt5 vs Qt6 on the same laptop.** Within 5 % on every scenario; Qt6 saves 37 MB of RSS at rest.
- **Idle cost.** 0.44 % of a core with one pane, 18 context switches a second. The 80 ms shell poll
  and the 250 ms program poll are what that is, and `tunePoll()` already backs an unseen pane off
  to 400 ms.

## Could not be measured here, and why

- **`GhosttyCore`.** Only `libvterm` builds on these machines (memory note "Engine cores on this
  machine"), so finding 1's edit 2 and finding 3's incremental `hyperlinkRuns` were profiled and
  are described against `engine/core/LibVtermCore.cpp` only. `GhosttyCore::hyperlinkRuns`
  (`engine/core/GhosttyCore.cpp:1079`) needs the same treatment and someone who can build it.
- **The Activity pane and the subagent panels with a long history.** `AgentInternalsView::setThinking`
  (`src/AgentInternalsView.cpp:283-322`) re-renders the *whole* reasoning block through
  `foldForMarkdown` with `maxLines = 100000` and rewrites it into the `QTextDocument` on every
  flush, throttled to 4 Hz by `Pane::internalsThinking` (`src/Pane.h:4877`). With the
  400 000-character cap that is up to 1.6 MB of markdown rendering a second while a long block
  streams. Driving the pane open needs a keystroke path I could not make deterministic in the time
  available (the pane is opened by a shortcut, and the chip-OCR check I use for input mode does not
  generalise to it); the finding is from reading, not from a number, so it is not ranked above.
  It is the first thing to measure next.
- **The turn-start hitch's stack** (finding 5), for the reason given there.

## Files here

`stub.py` (the provider), `drive.sh` (one run: Xvfb, isolated profile, seeded input mode, prompts,
per-thread sampling, optional `perf`/`bpftrace`/`strace`/IPC probes), `an.py` (the reducer),
`battery*.sh` and `turns.sh` (the scenarios), `straced-relay`. `spark/` and `sphinxpad/` hold the
trimmed outputs each finding cites. `perf.data` files are not kept; the reports made from them are.
