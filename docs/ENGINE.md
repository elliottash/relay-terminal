# Relay terminal engine

Relay's own terminal engine (owner decision: no Konsole fork; KonsolePart stays the Linux default
until parity). Long-term targets: Linux, macOS, Windows. Status on 2026-09-17: a reusable library
with two emulator cores, a threaded PTY, a QPainter view, `TerminalBackend` implemented, 97 passing
test-case runs in `relay-engine-tests` (both cores), and GUI checks with vim, less, htop and tmux.
**Wired into the app behind a per-pane flag**: `--engine=relay`, `RELAY_ENGINE=relay` or the palette's
"New pane (Relay engine)"; KonsolePart remains the default. See
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
| `relay-terminal-engine` | Static library: cores, PTY, session, view, backend |
| `relay-engine-bench` | Headless core throughput (`--core`, `--frames`) |
| `relay-vterm-spike` | Manual/xdotool harness (name kept for the old scripts): `--core`, `--size`, `--dump`, `--font`, `-e` |
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
| Core interface | `core/VtCore.h`, `core/CellTypes.h` | The swappable emulator boundary: `feed`, `resize`, `updateFrame(ViewportFrame*)` (dirty rows with cells, selection and search decorations, cursor, scrollbar), `screenText`/`historyText`, alt screen, mouse/paste modes, viewport scrolling pinned to content, `scrollToPrompt`, `hyperlinkAt`, selection (cell/word/line/rectangle), search, key/text/mouse/paste/focus encoding, colours, OSC 52 policy. Events: `reply` (bytes for the pty), title, cwd (OSC 7), bell, altScreenChanged, promptMark (OSC 133 A/B/C/D with row and exit code), clipboardWrite (OSC 52, opt-in), notification (OSC 9/777) |
| libghostty-vt core | `core/GhosttyCore.{h,cpp}`, `core/SequenceScanner.h` | **Default core.** Every libghostty-vt call lives in this one file. Uses the render-state API for frames, native selection/search/reflow/key/mouse/paste/focus encoders, grapheme clustering (mode 2027) on. The scanner splits `feed()` at OSC 133 and alt-screen switches so those events keep stream order |
| libvterm core | `core/LibVtermCore.{h,cpp}`, `third_party/libvterm/` | Fallback core. libvterm only models the screen, so the adapter owns a scrollback ring with reflow (via patched `sb_pushline4`/`sb_popline4`), the viewport, selection, search, OSC 7/8/10/11/52/133/777 parsing. Patches: `third_party/libvterm/README.relay.md` |
| PTY | `pty/Pty.h`, `pty/PtyUnix.cpp`, `pty/PtyWin.cpp` | `forkpty` with a prepared `execve` environment, reset signal dispositions and mask, `close_range`; one I/O thread polling the master and a wake pipe; bounded read bursts; non-blocking queued writes; resize (`TIOCSWINSZ` with pixel size); `tcgetpgrp` foreground group; SIGHUP + background reaper. macOS path uses `select`. Windows: ConPTY stub with the implementation plan |
| Session | `session/TerminalSession.{h,cpp}` | Owns core + pty. Parses on the pty thread under one mutex; the GUI thread gets the lock first (`GuiLock` + yield). Core events are queued and emitted as Qt signals on the GUI thread, coalesced per event-loop pass (one bell, the latest title and cwd per pass; at most 4 096 queued events, so `cat` of a binary cannot stall the GUI). `writeToDisplay()` (inline agent output), `sendInput()`, `withCore(f)` for locked access, `output()` signal (opt-in) |
| View | `view/TerminalView.{h,cpp}`, `view/KeyMapper.*`, `view/BoxDrawing.*`, `view/ColorScheme.h` | Frame snapshot per repaint (4 ms after a change, ~30 fps during floods), glyph runs with `QRawFont`, explicit colour-emoji font, pixel box drawing/blocks, cursor shapes and blink, underline styles, selection gestures (click/double/triple, Alt = rectangle, drag auto-scroll, PRIMARY on select), Ctrl+click and hover for OSC 8/URLs/`path:line:col`, mouse reporting and alternate scroll, IME (preedit, `inputMethodQuery`), search bar, zoom, context menu, `QAccessibleTextInterface`, host shortcut filter |
| Backend | `backend/VTermBackend.{h,cpp}`, `TerminalBackend.h` | The API `src/main.cpp` uses, through `src/EngineBackend` (see below) |

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
| Process | `startProgram(program, args, cwd, env)`, `sendInput(bytes)`, `sendText(text, asPaste)`, `shellPid()`, `foregroundProcessId()` (`tcgetpgrp` on the master), `isRunning()` |
| Display | `writeToDisplay(bytes)` (inline output, never reaches the program; if the program's output stopped inside an escape sequence or UTF-8 character, the bytes wait until the parser is at ground, at most ~500 ms), `redrawPrompt()` (sends Ctrl+X Ctrl+P by default, Relay's Bash binding), `setRedrawPromptSequence()` |
| Introspection | `capabilities()`, `screenText()`, `scrollbackText(maxLines)`, `altScreen()`, `rows()`, `columns()`, `title()`, `currentDirectory()` (OSC 7, else `/proc/<pid>/cwd`) |
| Geometry | `resizeTerminal(rows, cols)`, `widget()` (view + scrollbar), `focusWidget()`, `setTerminalFont()` |
| Clipboard | `copySelection()`, `paste()`, `selectedText()`, `selectAll()`, `clearScrollback()`, `clear()` |
| Scrolling | `scrollLines(n)`, `scrollPages(n)`, `scrollToBottom()`, `scrollToPrompt(direction)` |
| Search | `find(text, backwards)` |
| Callbacks | `onLinkActivated(target, line, column)` (OSC 8 URI, URL, or absolute path with `:line:col`), `onTitleChanged`, `onCwdChanged`, `onAltScreenChanged`, `onBell`, `onPromptMark(kind 'A'..'D', exitCode)`, `onOutput(bytes)` (opt-in via `setOutputCallbackEnabled`), `onFinished(exitCode)` |

Capabilities reported by `VTermBackend`: ScreenText, Scrollback, AltScreenState, LinkClicks,
Osc8Links, PromptMarks, CwdTracking, DisplayInjection, Search, ScrollControl. A KonsolePart adapter
would report DisplayInjection (private D-Bus slot) and ScrollControl (hidden scrollbar), ScreenText
only on KF6 (D-Bus `getDisplayedText`), and none of the others.

## Status vs KonsolePart (as Relay uses it)

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
| OSC 7 cwd, OSC 133 prompt marks + jump to prompt, OSC 9/777 notifications | ✅ | ✅ | 🟡 (cwd via /proc) | CoreTest::osc7*, osc133*, promptJump |
| Alt-screen state + change callback | ✅ | ✅ | ❌ | CoreTest::altScreen, SessionTest |
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

### Before making the engine the Linux default

1. ~~KonsolePart adapter + `--engine` switch~~ — done: `src/KonsoleBackend`, `src/EngineBackend`,
   `src/TerminalBackends` (per-pane, default KonsolePart).
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
4. **Gate (open).** `relay-engine-tests`, `engine/scripts/gui/scenarios.sh` and the
   perf/interrupt scripts with `--engine=relay` inside Relay, plus Relay's own backend-and-bash
   tests. Then make the engine the default where there is no KonsolePart (macOS, Windows builds)
   and flip Linux when the "Before making the engine the Linux default" list is done.

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
