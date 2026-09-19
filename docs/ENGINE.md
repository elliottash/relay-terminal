# Relay terminal engine

Relay's own terminal engine (owner decision: no Konsole fork). Long-term targets: Linux, macOS,
Windows. A reusable library with two emulator cores, a threaded PTY, a QPainter view,
`TerminalBackend` implemented, 97 passing test-case runs in `relay-engine-tests` (both cores),
and GUI checks with vim, less, htop and tmux.

**It is the terminal.** It became every pane's default on 2026-09-17 and the only one on
2026-09-18, when the owner retired KonsolePart ("relay engine is working great, so konsole is no
longer needed"). `--engine-core=ghostty|libvterm` still picks the emulator core. See
[ARCHITECTURE.md](ARCHITECTURE.md) section 16, `src/EngineBackend.*` and the remaining gaps in
`issues/features/needs_qa_llm/2026-09-17-engine-integration.md`.

Performance: [ENGINE-PERF.md](ENGINE-PERF.md). Evidence:
[qa_evidence/2026-09-17-engine-phase1/](qa_evidence/2026-09-17-engine-phase1/) (this phase) and
[qa_evidence/2026-09-17-engine-spike/](qa_evidence/2026-09-17-engine-spike/) (the spike).

## Build

`engine/` is always built and linked into `relay` (which defines `RELAY_HAVE_ENGINE`), and
`relay-engine-tests` runs in the app's `ctest`. `-DRELAY_BUILD_ENGINE=ON` (default **OFF**; the old
`RELAY_BUILD_ENGINE_SPIKE` still works as an alias) adds the manual harness and the benchmark.

```sh
# Optional, recommended core: libghostty-vt (needs git, network, Zig 0.16.x)
engine/scripts/build-libghostty-vt.sh ~/opt/ghostty-vt [path/to/zig]

cmake -S . -B build-engine -G Ninja -DRELAY_QT_MAJOR=5 -DRELAY_BUILD_APP=OFF -DBUILD_TESTING=ON \
  -DRELAY_BUILD_ENGINE=ON \
  -DRELAY_ENGINE_WITH_GHOSTTY=ON -DRELAY_GHOSTTY_VT_PREFIX=$HOME/opt/ghostty-vt \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-engine
ctest --test-dir build-engine -L engine            # relay-engine-tests
build-engine/engine/relay-vterm-spike --core ghostty   # manual harness
```

Without `RELAY_ENGINE_WITH_GHOSTTY` only the libvterm core is built (no Zig needed); every test runs
against whichever cores are available. `RELAY_ENGINE_TEST=SessionTest relay-engine-tests` runs one
test object.

Sanitizers (2026-09-17): the suite is clean under ASan+UBSan (Debug; the libvterm flood test needs
`ASAN_OPTIONS=detect_stack_use_after_return=0` to finish in time). Under TSan
(`setarch -R`, needed on this aarch64 kernel) the only reports were inside uninstrumented Qt
(posted-event queue) and one in test code that was fixed.

| Target | What |
|---|---|
| `relay-vterm-c` | Vendored libvterm 0.3.3 (MIT) with Relay patches, C99 |
| `relay-ghostty-vt` | Imported libghostty-vt static library; on Linux a partial link keeps only `ghostty_*` global (the Zig archive exports `memcpy`, `__chk_fail`, ...) |
| `relay-terminal-engine` | Static library: cores, PTY, session, view, backend (links `relay-outputlinks` for the path rules) |
| `relay-engine-bench` | Headless core throughput (`--core`, `--frames`) |
| `relay-vterm-spike` | Manual/xdotool harness (name kept for the old scripts): `--core`, `--size`, `--dump`, `--font`, `--folds`, `-e` |
| `relay-screen-bridge` | Headless PTY + core, streaming screen state as line JSON for remote access (`docs/REMOTE-PROTOCOL.md`): `--rows`, `--cols`, `--shell`, `--cwd`, `--raw-out` |
| `relay-engine-tests` | ctest: core (x2 cores), pty, session (x2), view (x2) |

## Architecture

```
            host (src/EngineBackend)               engine/
  ┌──────────────────────────────────┐
  │ TerminalBackend (engine-neutral) │  TerminalBackend.h
  └───────────────┬──────────────────┘
                  │ implements
  ┌───────────────▼──────────────────┐
  │ VTermBackend  (QObject)          │  backend/     container widget = view + scrollbar
  └───────┬──────────────────┬───────┘
          │                  │
  ┌───────▼───────┐   ┌──────▼──────────────────────┐
  │ TerminalView  │──▶│ TerminalSession (QObject)    │  session/
  │ (QWidget)     │   │  mutex ─ VtCore ─ Pty        │
  │ paint, input, │   └──────┬─────────────┬─────────┘
  │ IME, a11y     │          │             │
  └───────────────┘   ┌──────▼─────┐ ┌─────▼───────────────┐
    view/             │ VtCore     │ │ Pty (I/O thread)    │  pty/
                      │ ghostty │  │ │ PtyUnix: forkpty    │
                      │ libvterm   │ │ PtyWin: ConPTY stub │
                      └────────────┘ └─────────────────────┘
                        core/
```

### Modules

| Module | Files | Responsibility |
|---|---|---|
| Core interface | `core/VtCore.h`, `core/CellTypes.h` | The swappable emulator boundary: `feed`, `resize`, `updateFrame(ViewportFrame*)` (dirty rows with cells, selection and search decorations, cursor, scrollbar), `screenText`/`historyText`, `historyLines(from, count, out)` (styled scrollback rows in the frame's own `Line`, const: it never moves the viewport, which is what lets a remote client page history while the desktop user reads the screen), alt screen, mouse/paste modes, viewport scrolling pinned to content, `scrollToPrompt`, `hyperlinkAt`, selection (cell/word/line/rectangle), search, key/text/mouse/paste/focus encoding, colours, OSC 52 policy. Events: `reply` (bytes for the pty), title, cwd (OSC 7), bell, altScreenChanged, promptMark (OSC 133 A/B/C/D with row and exit code), clipboardWrite (OSC 52, opt-in), notification (OSC 9/777) |
| libghostty-vt core | `core/GhosttyCore.{h,cpp}`, `core/SequenceScanner.h` | **Default core.** Every libghostty-vt call lives in this one file. Uses the render-state API for frames, native selection/search/reflow/key/mouse/paste/focus encoders, grapheme clustering (mode 2027) on. The scanner splits `feed()` at OSC 133 and alt-screen switches so those events keep stream order |
| libvterm core | `core/LibVtermCore.{h,cpp}`, `third_party/libvterm/` | Fallback core. libvterm only models the screen, so the adapter owns a scrollback ring with reflow (via patched `sb_pushline4`/`sb_popline4`), the viewport, selection, search, OSC 7/8/10/11/52/133/777 parsing. Patches: `third_party/libvterm/README.relay.md` |
| PTY | `pty/Pty.h`, `pty/PtyUnix.cpp`, `pty/PtyWin.cpp` | `forkpty` with a prepared `execve` environment, reset signal dispositions and mask, `close_range`; one I/O thread polling the master and a wake pipe; bounded read bursts; non-blocking queued writes; resize (`TIOCSWINSZ` with pixel size); `tcgetpgrp` foreground group; SIGHUP + background reaper. macOS path uses `select`. Windows: ConPTY stub with the implementation plan |
| Session | `session/TerminalSession.{h,cpp}` | Owns core + pty. Parses on the pty thread under one mutex; the GUI thread gets the lock first (`GuiLock` + yield). Core events are queued and emitted as Qt signals on the GUI thread, coalesced per event-loop pass (one bell, the latest title and cwd per pass; at most 4 096 queued events, so `cat` of a binary cannot stall the GUI). `writeToDisplay()` (inline agent output), `sendInput()`, `withCore(f)` for locked access, `output()` signal (opt-in) |
| View | `view/TerminalView.{h,cpp}`, `view/KeyMapper.*`, `view/BoxDrawing.*`, `view/ColorScheme.h` | Frame snapshot per repaint (4 ms after a change, ~30 fps during floods), glyph runs with `QRawFont`, explicit colour-emoji font, pixel box drawing/blocks, cursor shapes and blink, underline styles, selection gestures (click/double/triple, Alt = rectangle, drag auto-scroll, PRIMARY on select), Ctrl+click and hover for OSC 8/URLs/`path:line:col`, mouse reporting and alternate scroll, IME (preedit, `inputMethodQuery`), search bar, zoom, context menu, `QAccessibleTextInterface`, host shortcut filter |
| Backend | `backend/VTermBackend.{h,cpp}`, `TerminalBackend.h` | The API `Pane` (`src/Pane.h`) uses, through `src/EngineBackend` (see below) |

### Threading

- The pty thread reads up to 1 MiB per burst in 64 KiB reads and calls `core.feed()` under the
  session mutex; replies (DA, DSR, key encodings) go straight to `Pty::write`.
- The GUI thread locks only to encode input, to take a frame (`updateFrame` copies dirty rows) and
  for host queries. Painting never holds the lock.
- Key presses write to the pty from the GUI thread immediately (non-blocking), so Ctrl+C does not
  wait for the parser: 31 ms during a flood, same as Konsole.
- Signals from `TerminalSession` always arrive on the GUI thread.

## Core decision

Benchmarks on the same inputs ([ENGINE-PERF.md](ENGINE-PERF.md)):

| Core | License | 200 MB cat, headless | 50 MB no newlines | Core memory | Build | API fit for Relay |
|---|---|---|---|---|---|---|
| **libghostty-vt** (ghostty `f9a3f24a`, 2026-09-16) | MIT; bundles simdutf (Apache-2.0/MIT), Highway (Apache-2.0/BSD-3), wuffs (Apache-2.0), Zig compiler_rt (MIT) | **~750 MiB/s** | **~900 MiB/s** | 11 MB | Zig 0.16 | Everything needed: render state with dirty rows, scrollback + reflow, alt screen, title/pwd callbacks, OSC 8 per cell, OSC 133 row/cell semantics, selection + formatter, search, key (incl. kitty protocol), mouse, focus, paste encoders, OSC 52 with consent callbacks, DA/size/colour-scheme callbacks. Missing: an OSC 133 event callback (scanner added) |
| Contour v0.7.0 `vtbackend` | Apache-2.0 | 91-118 MiB/s | 155-199 MiB/s | 63 MB | C++23 (one gcc-13 patch), CPM deps (libunicode, boxed-cpp, reflection-cpp, GSL) | Rich (hyperlinks, shell-integration flags, reflow, input generator) but tied to its `Terminal` object model, a `vtpty::Pty` and a non-reentrant mutex; large dependency surface |
| libvterm 0.3.3 + Relay patches | MIT | ~45 MiB/s (unpatched 21.5) | ~60 MiB/s (unpatched 34) | 21 MB | C99, vendored | Screen only: scrollback, reflow of scrollback, selection, search, OSC 7/8/133 had to be written in the adapter (done) |

**Chosen: libghostty-vt**, behind `VtCore`, with libvterm kept as the always-built fallback until
libghostty-vt has passed the GUI harness in Relay itself. It builds, passes the same 21 core test cases as
libvterm, runs vim/less/htop/tmux, and in the GUI the 200 MB `cat` takes 1.2 s against Konsole's
3.7 s (gate was 1.2x Konsole) with 31 ms Ctrl+C.

Risks and mitigations:

- **Unstable C API.** Upstream says breaking changes are expected. The commit is pinned in
  `engine/scripts/build-libghostty-vt.sh`; all calls are in `GhosttyCore.cpp` (about 1 100 lines);
  bumping means re-running `relay-engine-tests` and the GUI scripts. Two API details already bit
  during integration (`DATA_CURSOR_STYLE` is the SGR style, `DATA_MOUSE_TRACKING` is a bool); the
  tests caught both.
- **Zig in the toolchain.** CI and packaging need Zig 0.16 or a cached prebuilt archive per target.
  If unavailable, the build falls back to the libvterm core automatically.
- **Symbol leakage of the static archive** (compiler_rt `memcpy`, `__chk_fail`, ...): localized on
  Linux; macOS and Windows need the equivalent step (TODO in `engine/CMakeLists.txt`).
- **Scrollback limit is a byte budget** (about 10 bytes per cell); the line count is approximate.

Documented fallbacks, not implemented: alacritty_terminal (Rust FFI), xterm.js in QtWebEngine.

## TerminalBackend API

`engine/TerminalBackend.h` (all on the GUI thread):

| Group | Methods / callbacks |
|---|---|
| Process | `startProgram(program, args, cwd, env)`, `sendInput(bytes)`, `sendText(text, asPaste)`, `shellPid()`, `foregroundProcessId()` (`tcgetpgrp` on the master), `isRunning()`, `termiosFlags()` (`{valid, canonical, echo}`: one `tcgetattr` on the master, which on Linux reports the slave's line discipline; needs LineDiscipline, `valid = false` otherwise) |
| Display | `writeToDisplay(bytes)` (inline output, never reaches the program; if the program's output stopped inside an escape sequence or UTF-8 character, the bytes wait until the parser is at ground, at most ~500 ms), `redrawPrompt()` (sends Ctrl+X Ctrl+P by default, Relay's Bash binding), `setRedrawPromptSequence()` |
| Introspection | `capabilities()`, `screenText()`, `scrollbackText(maxLines)`, `altScreen()`, `rows()`, `columns()`, `title()`, `currentDirectory()` (OSC 7, else `/proc/<pid>/cwd`) |
| Geometry | `resizeTerminal(rows, cols)`, `widget()` (view + scrollbar), `focusWidget()`, `setTerminalFont()` |
| Clipboard | `copySelection()`, `paste()`, `selectedText()`, `selectAll()`, `clearScrollback()`, `clear()` |
| Scrolling | `scrollLines(n)`, `scrollPages(n)`, `scrollToBottom()`, `scrollToPrompt(direction)` |
| Search | `find(text, backwards)` — the real rows and the text of every open fold, as one sequence in visual order (see **Folds → Find**) |
| Links | `stepLink(delta, Link*, index*, count*)`, `endLinkWalk()`, `linkWalkActive()`, `setPlainClickOpensLinks(on)` — the keyboard walk over every file, folder and URL in the screen and the scrollback (`Ctrl+Shift+L`) |
| Callbacks | `onLinkActivated(target, line, column)` (OSC 8 URI, URL, or absolute path with `:line:col`), `onTitleChanged`, `onCwdChanged`, `onAltScreenChanged`, `onBell`, `onPromptMark(kind 'A'..'D', exitCode)`, `onOutput(bytes)` (opt-in via `setOutputCallbackEnabled`), `onFinished(exitCode)` |

| Folds | `setFoldPrefix()`, `setFoldContent(uri, lines)`, `setFoldExpanded()`, `foldExpanded()`, `removeFold()`, `clearFolds()`, `expandedFolds()`, `toggleFold(uri)`, callback `onFoldRequested(uri)` — the detail of one agent tool call or one reasoning block, unfolded inside the grid (see **Folds** below) |

Capabilities reported by `VTermBackend`: ScreenText, Scrollback, AltScreenState, LinkClicks,
Osc8Links, PromptMarks, CwdTracking, DisplayInjection, Search, ScrollControl, LinkWalk,
LineDiscipline, Folds.

`termiosFlags()` is what the host polls to tell a Readline prompt (raw, foreground group ==
shell) from a full-screen program or a password prompt (cooked, `ECHO` off), twelve times a
second in every pane; the `/proc/<shell>/fd/0` route it replaces cost an `open`, an `ioctl` and
a `close` each time. See ARCHITECTURE.md §6.

## Folds

Relay prints each agent tool call into the terminal grid as one concise line wrapped in an OSC 8
hyperlink (`relay://call/<pane>/<turn>/<call>`, issue #TK9C). Clicking that line unfolds the call's
detail **in place, underneath it, inside the terminal**, and clicking again folds it away.

### Why it is a view layer

Neither core lets the host insert rows into the scrollback: libghostty-vt owns its page list, and
the libvterm adapter's host ring is rewrapped on every resize. Both cores reflow. So a fold is not
content at all — it is a layer of **virtual rows the view lays between the real rows it paints**,
core-agnostic, and everything about it lives in `view/FoldLayer.{h,cpp}` (no GUI, unit tested in
`tests/FoldLayerTest.cpp`) and `view/TerminalView.cpp`.

### Coordinates

| | |
|---|---|
| real row | absolute scrollback row, 0 = the oldest line — what `scrollViewportToRow()` and `historyRows()` speak. `realRows = historyRows + rows` |
| visual row | real rows with every open fold's rows spliced in after the fold's anchor row. This is what the user scrolls through, what the scroll bar's range counts and what a selection and a copy are ordered by |

`FoldLayer` maps between them: `visualOfReal(r)` adds the heights of the folds anchored above `r`,
and `at(v)` answers whether a visual row is a real row or a fold's row, by binary search over the
open folds (a handful at a time).

### Anchoring

A fold hangs under the **last** real row of the run of cells carrying its anchor URI, so a
soft-wrapped anchor line keeps its block where the eye expects it. The view needs that row for
every open fold — including anchors far outside the viewport, because the total scroll height
depends on them — so `VtCore::hyperlinkRuns(prefix)` walks the scrollback and the screen and
returns every run whose OSC 8 URI starts with the prefix, in absolute rows:

- **libvterm core**: walks its own ring and the screen; the link id is already on every cell, so
  there is no libvterm call per cell (~20 ms for a 100 000-line history).
- **ghostty core**: a hyperlink is answered per grid ref, so a full cell scan would be one FFI call
  per cell. It walks **column 0** of each row and only then right to the end of the run: two calls
  per row. Relay's anchor lines carry their link from the first column (the view overpaints the
  chevron there), so that is where they are found. *An anchor that starts further right is not seen
  on this core.*

It is re-resolved when the grid is resized (both cores reflow), when a fold is added or toggled,
and on a 300 ms heartbeat while any fold is open — which is what notices the scrollback trimming an
anchor away; that fold is then dropped. A fold that has never been anchored is kept, because the
host may set a call's detail before its line is printed. Nothing runs when no fold is open.

### Painting and scrolling

The block is indented (2–4 cells, 3 by default) with a left rule and a tint mixed from the colour
scheme (`ColorScheme::foldBackground` / `foldRule` override it). Spans carry their own foreground,
background, bold/italic/underline/dim and an optional link, so a coloured diff is the host's to
describe; wide characters, grapheme clusters, box drawing and colour emoji go through the same text
path as the real grid. The anchor's first cell is overpainted with `▸` (shut) or `▾` (open); the
host prints a placeholder there.

The view keeps the first *visual* row on screen and drives the core's own viewport to whatever
covers the real rows that window needs — the window can never show more real rows than the core's
viewport is tall, so covering its first real row covers them all. That is why a 500-line fold
scrolls line by line: the wheel, the scroll bar (its range counts the open folds' rows;
`scrollPositionChanged` and `TerminalView::scrollToVisualRow()` are in visual rows), PgUp/PgDn,
scroll to top, bottom and prompt, scroll-on-keystroke and `viewportAtBottom()` all count visual
rows. At the bottom the newest output stays on screen and the older rows are pushed up; toggling a
fold leaves its anchor where it was on screen unless that would push the cursor row off.

### Selection

The cores own the selection over real rows and know nothing about fold rows, so while a fold is on
screen the view owns a selection in **visual** coordinates (its ends stored as content: an absolute
real row, or a fold URI and a row inside it) and hands the real-row part straight back to the core,
which paints it and yields its text exactly as before. A copy walks the visual range, asks the core
for each run of real rows and takes each fold's own text, and joins them in the order the rows are
displayed; a wrapped fold line comes back as its one logical line, without the indent. Double click
takes a word inside a fold, triple click the line, select-all the scrollback and every open fold,
and an Alt-drag rectangle still belongs to the core.

### Find

`find()` covers the real rows **and** the text of every open fold, as one sequence in the order the
rows are painted. The cores search their own rows; `view/FoldSearch.{h,cpp}` (GUI-free, unit tested
in `tests/FoldSearchTest.cpp`) searches the folds and merges the two.

A fold's **logical** line is what is searched, not its wrapped rows, so a match that straddles the
block's wrap is one match — painted on both rows, clipped to each. The matching rule is the cores'
(Qt case-insensitive, non-overlapping), and the count is `searchMatchCount()` + the fold matches.
`searchStep()`'s index keeps its meaning across both kinds: counted from the newest match, 0.

**Why the core is parked, never stepped and undone.** Neither core can enumerate its matches —
libghostty-vt answers *how many* and *which one is selected*, not *all of them* — and walking the
core over every match to find out would be O(matches) FFI calls per needle. So the merge keeps one
match of look-ahead: it steps the core once and, while the fold matches between the previous
position and that core match have not been visited, leaves the core **parked** there and walks the
fold matches on its own; consuming the parked match afterwards is free. That is exactly one
`VtCore::searchStep()` per core match visited — a step is never made and then undone, which no core
promises to be exact — and the core's own cyclic order is the order the merged walk needs in both
directions, wrap included. Reversing direction leaves the parked match on the wrong side, and one
step in the new direction puts it back on the right one. `VtCore::searchCurrentRow()` (both cores)
is what says where the core is, in absolute rows, and it is re-read before a parked match is reused,
so a scrollback that trimmed underneath cannot leave the merge comparing against a row that moved.

The index is arithmetic, not bookkeeping: `searchStep()` says how many core matches are newer than
the one it selected, and the fold matches newer than a given visual row are counted directly, so
both kinds' indices are exact at every step and survive a recompute.

The selected fold match is drawn with `ColorScheme::searchCurrent` and the others with
`searchMatch`, exactly as in the real rows; while the selection is a fold match the view drops the
`current` flag on the core's own parked match, so only one match anywhere is ever the current one.
It is scrolled into view with `scrollToVisualRow()`, half a screen above it, the way a core scrolls
its own match in.

The fold half is recomputed lazily — on a toggle, new content, a rewrap, a resize, a trim — never
per frame, and never over the scrollback: it costs the fold's own text. A shut fold is not searched,
and neither is any fold while a full-screen program owns the grid. With no fold open
(`FoldLayer::active()` is false) `find()` is the core's `searchSet`/`searchStep` and nothing else,
byte for byte the path it was before.

### On the alternate screen

Folds are neither painted, hit-tested nor searched while a full-screen program owns the grid, and
come back when it leaves. Mouse-reporting programs get their clicks as before.

### The reasoning fold (issue T8CN)

Tool calls are not the only thing that folds: reasoning streams into a fold of its own. The first
`thinking_delta` of a block prints an anchor row — `▸ ✦ thinking…`, Note ink, column 0 like every
anchor — hyperlinked to `relay://call/<pane>/<turn>/thinking` (`thinking-2`, `thinking-3`, … for
the later blocks of a model that resumes reasoning after its answer), and the fold opens with its
first content. What it holds is the pane's own buffer rendered as markdown
(`calllines::foldForMarkdown`), the tail while it streams (the last 12 000 characters — the end is
the part being written), with a final row linking to the turn pane, which keeps the whole of every
block. A click on a settled anchor answers from the same buffer: no worker round trip, however old
the turn.

**Its height is capped in rendered rows** (issue K48R; owner's numbers): the **last 6** rows while the block streams, under a muted
`… N earlier lines`, and the **first 18** of a settled fold opened by hand, over a muted
`… N more lines · open in pane`. A grid fold has no scroll view of its own, so the cap is the
height. The count is of the rows the view will paint, not of the lines handed over: the content is
wrapped first, by `relay::wrapFoldLines()` (`engine/TerminalBackend.h` — the same hard wrap at
`columns - kFoldIndent` that `FoldLayer::layout()` does, asserted against the layer in
`engine/tests/FoldLayerTest.cpp`), so one 5 000-character paragraph is 60 rows at 100 columns and
is cut like any other. `FoldOptions::wrapCells` and `FoldOptions::tail` carry the two decisions
into `calllines`.

Updates are coalesced to ~4 Hz (one 250 ms single-shot at a time) — re-rendering the buffer as
markdown per chunk would burn the CPU on a long stream. Because `setFoldContent()` opens what it
sets, a flush first asks `foldExpanded()`: a fold the reader has clicked shut is marked
user-toggled and never pushed content again, which would reopen it over their click. On
`thinking_done` the fold gets its final content and collapses unless the reader toggled it or
`agent/thinking_display` is `always`, and the anchor row is rewritten in place to
`▸ ✦ thought for N s` (`✦ thinking stopped` when the stream died mid-reasoning, `{chars: 0}`);
the rewrite guards on the cursor still sitting on the empty row under the anchor — the shell
redrawing its prompt, or output pushing the anchor into history, means the finished line prints
as a new row instead. `agent/thinking_display` says how much of this runs at all: `collapse` (the
default), `always`, `never` — which leaves the single `✦ thought for N s` Note line of the old
design and no fold; a settings file that still has the `agent/show_thinking` bool is migrated in
place on first read. Alt+R (`agent.thinkingPanel`) toggles the latest fold — live while it
streams, the last turn's afterwards — and every refusal toasts, or the key would read as dead.
All of it is in `src/Pane.h` (`thinkingDelta` … `finishThinkingFold`), on the same fold layer as
the tool calls; evidence in
[qa_evidence/2026-09-19-thinking-fold/](qa_evidence/2026-09-19-thinking-fold/) and, for the caps,
[qa_evidence/2026-09-19-thinking-fold-cap/](qa_evidence/2026-09-19-thinking-fold-cap/).

The `open in pane` row of the fold is `relay://turn/<pane>/<turn>`: the turn pane, which holds the
whole of the block whatever the fold shows. The view keeps that text (`TurnTranscriptView::
setThinking()` holds it and redraws with the log) — before K48R the `turn_transcript` reply that
lands a round trip after the pane opens cleared the log, so the link opened a pane with no
reasoning in it, which is what "the open in pane link doesnt work" was. While the block streams the
pane is refreshed on the fold's own 4 Hz flush, so it follows rather than freezing at what had
arrived when it was opened.

### Limits (2026-09-18)

- On the ghostty core an anchor run must include column 0 (above).
- The find is the *needle*'s: neither core offers a regex or a whole-word mode, and the fold half
  matches what they match, nothing more.
- A selection that reaches above the visible window is read back through the core's own selection,
  so its soft-wrapped rows join as they always did.
- Rectangle (Alt-drag) selection covers real rows only.
- `screenText()`, `historyText()` and `scrollbackText()` are real rows only, by design; the rows on
  screen in visual order are `TerminalView::visibleRowsText()`.

### Seeing it

`relay-vterm-spike --folds` prints three tool-call lines and registers their detail (a short run, a
red and green diff with a link, and a 300-line listing).
`engine/scripts/gui/folds.sh BIN CORE OUTDIR` drives it under Xvfb and captures the states — the
last two are a find whose matches straddle the open folds and one whose only match is *inside* a
fold;
evidence in [qa_evidence/2026-09-18-concise-tool-call-lines/](qa_evidence/2026-09-18-concise-tool-call-lines/).

## Status, with KonsolePart as the comparison it replaced

The KonsolePart column is kept as the bar this engine had to clear; it is history, not a
configuration Relay still ships.

Legend: ✅ done and tested, 🟡 partial, ❌ missing. "Tests" names `relay-engine-tests` cases or the
GUI scenario (`engine/scripts/gui/scenarios.sh`).

| Area | Relay engine (ghostty core) | libvterm core | KonsolePart 23.08 | Evidence |
|---|---|---|---|---|
| vim, less, htop, tmux splits, resize with vim open | ✅ | ✅ | ✅ | scenarios screenshots 02-06 |
| Throughput (200 MB cat) | ✅ 1.2 s | 🟡 4.0 s | 3.7 s | ENGINE-PERF |
| Ctrl+C latency under flood | ✅ 31 ms | ✅ 31 ms | 31 ms | ENGINE-PERF |
| 16/256/truecolor, bold/italic/faint/reverse/strike, underline single/double/curly | ✅ | ✅ | ✅ | CoreTest::sgrAttributesAndColours, screenshot 01 |
| CJK width, combining marks | ✅ | ✅ | ✅ | CoreTest::wideAndCombiningCharacters |
| Emoji: colour glyphs, skin tones, ZWJ, flags as one cell | ✅ | ✅ (patched clustering) | 🟡 | CoreTest::emojiClusters, ViewTest::colorEmoji, screenshot 01 |
| Box drawing / block elements seamless | ✅ (drawn as rectangles) | ✅ | ✅ | ViewTest::boxDrawingJoinsAcrossCells |
| Ligatures, bidi/RTL, DECDWL/DECDHL rendering, blink attribute | ❌ | ❌ | 🟡 | |
| Scrollback with reflow on resize | ✅ | ✅ (host ring + patched libvterm) | ✅ | CoreTest::reflowOnResize |
| Viewport stays on content while output continues | ✅ | ✅ | ✅ | CoreTest::scrollbackAndViewport |
| Unlimited / disk-backed history | ❌ (byte budget in memory) | ❌ | ✅ | |
| Search in scrollback (highlight all, next/previous, find bar) | ✅ | ✅ | ✅ | CoreTest::searchScrollback, screenshot 09 |
| Selection: drag, word, line, rectangle (Alt), auto-scroll, PRIMARY | ✅ | ✅ | ✅ | ViewTest::mouseSelectionAndCopy, CoreTest::selectionText |
| Copy/paste shortcuts, middle-click, bracketed paste, paste sanitizing | ✅ | ✅ | ✅ | CoreTest::pasteAndFocus |
| Mouse reporting (X10/normal/button/any, SGR, SGR-pixels), alternate scroll | ✅ | 🟡 (no SGR-pixels) | ✅ | CoreTest::mouseReporting |
| Keyboard: xterm encoding, app cursor/keypad, modifiers | ✅ | ✅ | ✅ | CoreTest::keyEncoding, screenshot 08 |
| Kitty keyboard protocol | 🟡 (encoder follows program flags; release events only for special keys) | ❌ | ❌ | |
| Focus events, DECSCUSR cursor shapes, cursor blink | ✅ | ✅ | ✅ | CoreTest::cursorShape, pasteAndFocus |
| OSC 8 hyperlinks, URL and `path:line:col` Ctrl+click **to the host** | ✅ | ✅ | ❌ (opens itself) | ViewTest::ctrlClickLinksAndPaths, dump callbacks |
| Clickable paths: hover underline + target, plain click, link context menu, `file:line:col`, quoted names with spaces, compiler/test output (`src/OutputLinks.*`) | ✅ | ✅ | 🟡 (its own filter: text files only, no `:line`, folders to KIO) | ViewTest::plainClickFollowsAPath, `relay-outputlinks-tests` |
| Keyboard walk over the links in screen + scrollback (`Ctrl+Shift+L`) | ✅ | ✅ | ❌ (no screen text) | ViewTest::keyboardLinkWalk |
| OSC 7 cwd, OSC 133 prompt marks + jump to prompt, OSC 9/777 notifications | ✅ | ✅ | 🟡 (cwd via /proc) | CoreTest::osc7*, osc133*, promptJump |
| Alt-screen state + change callback | ✅ | ✅ | ❌ | CoreTest::altScreen, SessionTest |
| Line discipline (`ICANON`/`ECHO`) + foreground group off the pty master | ✅ | ✅ | ❌ (no master fd; the host falls back to `/proc`) | PtyTest::lineDisciplineFromTheMaster |
| Screen text + scrollback text for the agent | ✅ | ✅ | ❌ (KF6 D-Bus: screen only) | SessionTest |
| Write to display (inline agent output) | ✅ | ✅ | 🟡 (private D-Bus slot) | SessionTest::writeToDisplayNeverReachesProgram |
| OSC 52 clipboard | ✅ write opt-in, read never | ✅ | 🟡 | CoreTest::osc52ClipboardIsOptIn |
| IME: commit, preedit at cursor, `inputMethodQuery` | 🟡 (offscreen test only; fcitx/ibus not tried) | 🟡 | ✅ | ViewTest::inputMethod |
| Accessibility | 🟡 (`QAccessibleTextInterface` over the viewport; no Orca test, no detailed change events) | 🟡 | 🟡 | ViewTest::accessibleText |
| Font choice, zoom | ✅ | ✅ | ✅ | |
| Profiles / colour-scheme UI, settings dialogs | ❌ (`ColorScheme` struct only) | ❌ | ✅ | |
| Silence/activity monitoring | ❌ (host can use `onOutput`) | ❌ | ✅ | |
| Sixel / kitty graphics | ❌ (libghostty-vt parses kitty graphics; not rendered) | ❌ | ❌ | |
| Linux | ✅ | ✅ | ✅ | |
| macOS | 🟡 code paths exist (forkpty+select, Cmd shortcuts, Apple Color Emoji), never built | 🟡 | ❌ | |
| Windows | ❌ ConPTY stub | ❌ | ❌ | |

## Remaining work

### Open (the engine is already the Linux default)

1. ~~KonsolePart adapter + `--engine` switch~~ — done, then removed with KonsolePart itself
   (2026-09-18): `src/EngineBackend` and `src/TerminalBackends` are what is left.
2. ~~Shell integration: emit OSC 7 and OSC 133 A/B/C/D~~ — done, opt-in:
   `shell/relay-integration.bash` / `.zsh`. Driving waiting-for-input and command blocks from
   `onPromptMark` (instead of Relay's `/proc` polling and Bash bridge) is still open.
3. IME with fcitx5 and ibus on X11 and Wayland; accessibility with Orca.
4. Theme: map `data/theme` to `ColorScheme`; font from Relay settings.
5. Packaging: CI step for Zig 0.16 + `build-libghostty-vt.sh` (cache by commit), or ship with the
   libvterm core where Zig is unavailable.

### macOS

- Build and run the tests: `PtyUnix.cpp` (`<util.h>`, `select`), `KeyMapper` (Qt swaps Ctrl/Meta;
  Option-as-Alt option), Cmd+C/V/F built-ins, Apple Color Emoji.
- libghostty-vt for `aarch64-macos`/`x86_64-macos`; localize archive symbols for Mach-O
  (`ld -r` + `-exported_symbols_list`).
- `currentDirectory()`: OSC 7, else `proc_pidinfo(PROC_PIDVNODEPATHINFO)`.
- Notarization/codesigning of the bundled app.

### Windows

- `PtyWin.cpp`: ConPTY (`CreatePseudoConsole`, pipes, reader thread, `ResizePseudoConsole`,
  job object for the tree); plan in the file header. Reference Qt Creator's MIT ptyqt copy and
  wezterm's portable-pty; no dependency on upstream Pty-Qt.
- libvterm with MSVC (C99, fine); libghostty-vt for `x86_64-windows-msvc` (check CRT/ABI) or the
  libvterm core first.
- AltGr handling (Ctrl+Alt with printable text, covered by `KeyMapper`), Segoe UI Emoji,
  DirectWrite font fallback, cwd via OSC 7 only.

## Integration into `src/main.cpp` (done 2026-09-17, steps 1-3)

(Written when the pane lived in `src/main.cpp`; since the 2026-09-18 split it is `Pane` in
`src/Pane.h`. KonsolePart, named below, was retired the same day.)

1. **Adapter, no behaviour change.** Add `src/KonsoleBackend.{h,cpp}` implementing
   `relay::TerminalBackend` with today's calls: `TerminalInterface::sendInput`,
   `foregroundProcessId`, `onReceiveBlock` over the Session D-Bus object for `writeToDisplay`, the
   hidden scrollbar for `scrollLines/Pages`, `copyToClipboard`/`pasteFromClipboard` slots, the
   profile keys for fonts/history. `Pane` holds a `std::unique_ptr<TerminalBackend>` and never
   touches KonsolePart directly.
2. **Build switch.** `relay-terminal-engine` is always linked into `relay`, which defines
   `RELAY_HAVE_ENGINE`. `--engine=relay` (or `RELAY_ENGINE=relay`; optional
   `--engine-core=ghostty|libvterm`) creates `EngineBackend` per pane; default stays KonsolePart
   on Linux. `--engine=vterm` is accepted as an alias.
3. **Map Relay features onto the richer API** (only when `capabilities()` has the bit):
   `printInline` -> `writeToDisplay`; `closeInline` -> `redrawPrompt`; hide the composer on
   `onAltScreenChanged(true)` (intake item 9) instead of the full-screen process list;
   waiting-for-input and exit codes from `onPromptMark`; `onLinkActivated` opens files in Relay's
   file panes at `line:column`; `screenText`/`scrollbackText` into agent context;
   composer PageUp/PageDown -> `scrollPages`; Relay's global shortcuts through
   `TerminalView::setShortcutFilter` (replaces `overrideShortcut`).
4. ~~Gate~~ — the engine became the default on 2026-09-17 and the only backend on 2026-09-18;
   `src/KonsoleBackend.*`, `--engine`, `RELAY_ENGINE` and the two palette entries are gone.

## History: the spike (2026-09-17 morning)

A 50-minute spike (libvterm on the GUI thread, one 950-line widget) showed the approach works with
vim, less, htop and tmux and exposes what KonsolePart withholds, but it was 2x slower than Konsole on
a 200 MB `cat` (7.6 s vs 3.7 s), drew emoji as tofu, and had no reflowed scrollback, IME query,
selection polish or tests. This phase replaced it: `VTermWidget` became `TerminalSession` +
`TerminalView` + `VTermBackend`, the emulator moved behind `VtCore`, parsing moved to the pty thread,
libvterm was vendored and patched, libghostty-vt was added and chosen, and every gap in the spike's
table above got an implementation or an explicit status. The spike found one real bug class worth
keeping in mind for Relay's own launcher: `SIG_IGN` dispositions survive `exec`, so the child must
reset signal dispositions and the mask (done in `PtyUnix.cpp`, tested by
`PtyTest::interactiveInputResizeAndForeground`).
