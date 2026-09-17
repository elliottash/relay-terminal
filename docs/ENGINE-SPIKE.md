# Engine spike: libvterm + QPainter (2026-09-17)

Time-boxed spike (about 50 minutes of work) for option 2 / hybrid 3a in
[NEXT-STEPS-RESEARCH.md section A](NEXT-STEPS-RESEARCH.md): can a Relay-owned terminal
engine replace KonsolePart later, and give Relay screen text and click control?

Evidence: [qa_evidence/2026-09-17-engine-spike/](qa_evidence/2026-09-17-engine-spike/)
(screenshots, `dump-*.txt` from the debug key, and the xdotool driver scripts in `scripts/`).

## What was built

`engine/` is built only with `-DRELAY_BUILD_ENGINE_SPIKE=ON` (default OFF; the default
configure does not reference libvterm). It is not wired into `src/main.cpp`.

| File | Content |
|---|---|
| `Pty.h`, `PtyUnix.cpp` | `relay::Pty` interface (start, write, resize, childPid, foregroundPid, onOutput/onFinished) and a `forkpty` implementation (macOS `<util.h>` ifdef in place). Non-blocking master fd read with `QSocketNotifier` on the GUI thread, 12 ms read budget then yields one event-loop pass |
| `VTermWidget.h/.cpp` | libvterm 0.3.3 (distro `libvterm-dev`) screen rendered with QPainter: DejaVu Sans Mono, 16/256/truecolor, bold/italic/underline/double underline/strike/reverse/conceal, block/underline/bar cursor, hollow cursor when unfocused, alt screen, row-merged damage coalesced into repaints (8 ms, ~15 fps while output floods), resize to `vterm_set_size` + `TIOCSWINSZ`, scrollback ring (10 000 lines from `sb_pushline`, restored on `sb_popline`), mouse wheel / Shift+PageUp/PageDown/Home/End scrolling, keys via `vterm_keyboard_key/unichar` (arrows, Home/End, PgUp/PgDn, Ins/Del, F1-F35, Ctrl/Alt/Shift combinations), bracketed paste (Ctrl+Shift+V, middle click), drag and double-click selection with copy (PRIMARY on release, Ctrl+Shift+C for CLIPBOARD), mouse reporting forwarded when an app enables it (Shift overrides), focus in/out reports, window title (OSC 0/2), basic IME preedit/commit |
| `VTermWidget` extras | `screenText()`, `scrollbackText(n)`, `altScreen()`, `foregroundProcessId()`, OSC 8 link ranges + `linkActivated`, Ctrl+click token → `pathActivated(absolutePath)` if the file exists relative to the foreground process's `/proc/<pid>/cwd` (strips quotes/brackets and `:line[:col]`), `http(s)://` tokens → `linkActivated` |
| `TerminalBackend.h` | Engine-neutral interface: startProgram(program, args, cwd, env), sendInput, sendText(asPaste), shellPid, foregroundProcessId, capabilities, screenText, scrollbackText, altScreen, rows/columns, resizeTerminal, widget, focusWidget, callbacks onLinkActivated / onPathActivated / onTitleChanged / onFinished. `VTermWidget` implements it. A KonsolePart adapter could implement it with `TerminalInterface` + Session D-Bus, reporting fewer capabilities |
| `main.cpp` | `relay-vterm-spike [--cwd DIR] [--dump FILE] [--size COLSxROWS] [-e PROGRAM ARGS...]`; Ctrl+Shift+D appends `debugDump()` (size, alt screen, cursor, fg pid, links, `scrollbackText(5)`, `screenText()`) to the dump file; `--bench FILE [SCROLLBACK]` parses a file through the widget with no PTY and no painting |

Build (Ubuntu 24.04, Qt 5.15 / KF5 machine):

```sh
sudo apt install libvterm-dev
cmake -S . -B build-spike -DRELAY_QT_MAJOR=5 -DRELAY_BUILD_ENGINE_SPIKE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-spike --target relay-vterm-spike
```

Code: about 1 400 lines in `engine/` plus a 100-line harness. Original code; Qt Creator's
terminal was not copied (the approach is the standard libvterm integration pattern).

## Results (Xvfb 1400x900, xdotool, aarch64 20 cores, Release build)

| Test | Result | Evidence |
|---|---|---|
| bash prompt, `ls --color`, SGR attributes, 256 colour, truecolor fg/bg | Works | `02-bash-colors-unicode-osc8.png` |
| CJK width (漢字, かな, 한글), combining é, box drawing | Correct 2-cell widths and positions; box glyphs join | `02-*.png` |
| Emoji | **Partial.** Cells and widths correct in libvterm and `screenText()` (`🎉👍🏽🇯🇵` round-trips), but Qt 5 draws 🎉 as tofu, 👍 monochrome with the skin-tone modifier as a separate hatched box, and the flag as a boxed "JP". Noto Color Emoji is installed; this is a Qt5/XCB colour-font fallback issue, not an emulator one | `02-*.png` |
| OSC 8 hyperlink | Works: link range recorded, underlined, Ctrl+click → `linkActivated https://example.com/docs` | `02-*.png`, `scripts/t3.sh` |
| Ctrl+click path detection | `src/main.cpp:42:7` → `pathActivated /tmp/.../work/src/main.cpp`; `~/nonexistent` → no callback | `scripts/t3.sh` output |
| Selection + bracketed paste | Drag selects (highlight), middle click pastes; readline shows the paste as bracketed (highlighted) | `03-selection-paste.png` |
| vim: insert text, Tab, Esc, `:wq` | File on disk is exactly `hello from relay-vterm-spike\nsecond line with tab\tend\n` | `04-vim-insert.png`, `07-vim-saved.png` |
| Resize with vim open | 100x30 → 66x16 → 122x33; vim redraws at each size; `screenText()` matches | `05-*`, `06-*`, `dump-vim-less-htop-scrollback.txt` |
| less on a 5 000-line file, `/` search | Works; `altScreen=1` while open, 0 after `q` | `08-less-search.png`, dump |
| htop | Works (meters, colours, function-key bar); F10 quits | `09-htop-filtered.png` |
| tmux: vertical + horizontal split (ls, vim, htop), detach | Works; borders and status line correct; after detach `screenText()` shows `[detached (from session spike)]` and `altScreen=0` | `11-tmux-splits.png`, `12-tmux-detached.png` |
| Keyboard encoding | `cat -v` shows F5 `^[[15~`, F12 `^[[24~`, Up `^[[A`, Home `^[[H`, Shift+Up `^[[1;2A`, Ctrl+Right `^[[1;5C`, Alt+x `^[x`; readline Alt+b and Ctrl+Left edit correctly | `dump-keyboard.txt` |
| Scrollback | `seq 1 300` then Shift+PageUp x2 shows lines 205-237 with offset badge; `scrollbackText(5)` returns 264-268 | `10-*.png`, dump |
| `screenText()` correctness | Matched the visible screen in every dump above (shell, vim, less, htop, tmux) | `dump-*.txt` |

### Throughput: `cat` of a 200 MB file

File: 209 715 200 bytes of base64, 99 columns per line (2.1 M lines). Terminal 100x30,
same Xvfb. Wall = `cat` start to exit as seen by the shell inside the terminal (this includes
back-pressure from a slow terminal). CPU = terminal process user+sys during that window
(+1 s settle). Script: `scripts/perf.sh`.

| Terminal | Wall | Terminal CPU | Peak RSS |
|---|---|---|---|
| relay-vterm-spike (libvterm 0.3.3) | **7.6-7.7 s** (27 MB/s) | 7.3-7.6 s | 158 MB |
| Konsole 23.08.5 (full app, same engine as KonsolePart) | 3.7 s (54 MB/s) | 3.4 s | 143 MB |
| xterm 390 | 3.3 s (61 MB/s) | 3.0 s | 20 MB |

Headless `--bench` (no PTY, no painting): 21-23 MB/s with scrollback 10 000 or 0, and with reflow
disabled; 34 MB/s for 50 MB of the same data with newlines removed (no scrolling). `perf` on a GUI
run (80 MB): 34 % `memcpy` and 9 % `vterm_screen_get_cell` inside the emulator; the top Qt painting
symbol and `QRegion::operator+=` were each under 3 %. So the ceiling is most likely libvterm's
per-line scroll path (buffer moves plus cell reads for `sb_pushline`), not rendering or the
scrollback ring. Not verified further within the time-box. The spike is about **2x slower than
Konsole** on this worst case; interactive use showed no visible lag.

### Interrupt during the flood

Ctrl+C one and a half seconds into the 200 MB `cat` (`scripts/intr.sh`), time until `cat` exits:
spike **32 ms**, Konsole 30 ms, xterm 37 ms. The prompt and later typing are immediate
(`14-interrupt-*.png`).

Bug found and fixed on the way: the first build took **8 s** here. `cat` had SIGINT ignored
because `SIG_IGN` survives `exec` and the spike was launched with `&` from a script. The child
now resets every signal disposition and the signal mask after `forkpty`.
Relay's own launch path should be checked for the same class of bug.

## Gaps vs KonsolePart (honest list)

| Area | Status in spike | Work to close |
|---|---|---|
| Throughput | 2x slower than Konsole on floods; parse on GUI thread | Patch libvterm scrolling (ring-buffered screen) or move to libghostty-vt / alacritty_terminal; optionally parse on a worker thread with a snapshot for painting |
| Emoji / colour fonts | Tofu or monochrome under Qt 5 | Qt 6 colour font support, explicit emoji fallback font, grapheme-cluster drawing |
| IME | Commit + crude preedit overlay; untested (no input method under Xvfb); `inputMethodQuery` not implemented so candidate windows are misplaced | Implement `inputMethodQuery` (cursor rect, surrounding text), preedit styling, test fcitx/ibus, macOS/Windows IME |
| Ligatures | None: ASCII drawn as runs, but no shaping-aware cell grid | Run-level shaping with `QTextLayout`/`QGlyphRun`, cell snapping |
| Sixel / kitty / iTerm images | None (libvterm has no graphics) | Engine change or DCS parsing + own image layer |
| Reflow | libvterm reflows the active screen; our scrollback lines keep their old width (truncated/padded on resize) | Store logical lines with continuation flags and rewrap |
| Selection | Linear drag, double-click word, PRIMARY/CLIPBOARD; no triple-click line, no block selection, no joining of soft-wrapped lines, no auto-scroll timer, selection not cleared when content changes | 1-2 weeks of polish |
| Accessibility | None (no `QAccessibleInterface`) | Text interface over screen/scrollback, cursor and change events; this is where Konsole is also weak, but it matters on macOS/Windows |
| OSC 8 | Ranges from cursor positions at open/close; not invalidated when overwritten, cleared or reflowed; no hover highlight; libvterm 0.3.3 has no per-cell hyperlink attribute | Per-cell link id side-table maintained from damage |
| OSC 7 / 133, search, profiles, colour schemes, cursor blink, bell, URL hover, context menu, font zoom, key bindings UI | Not implemented (OSC 7/133 hook in the same OSC fallback as OSC 8) | Mostly straightforward; together several weeks |
| PTY writes | Blocking retry on `EAGAIN` | Write queue with a write notifier |
| Working directory for path clicks | `/proc/<pid>/cwd` (Linux only) | OSC 7 from Relay's shell integration everywhere; `proc_pidinfo` on macOS |

## Portability notes

- **macOS:** `forkpty` exists (`<util.h>`, already ifdef'd). No `/proc`: use OSC 7 or
  `proc_pidinfo(PROC_PIDVNODEPATHINFO)`; `tcgetpgrp` works. libvterm builds with clang
  (Qt Creator ships it on macOS). Option-as-Meta and Cmd shortcuts need key mapping.
- **Windows:** implement `relay::Pty` with `CreatePseudoConsole` + two pipes +
  `ResizePseudoConsole`; pipe reads need a worker thread (no `QSocketNotifier` for pipes), and
  output must be marshalled to the GUI thread. No foreground-process id: use a job object
  for the tree, OSC 7 for cwd. ConPTY rewrites output (its own screen model), so tmux/vim
  behaviour differs from Linux. Pty-Qt (MIT) or Qt Creator's patched copy is a ready
  reference. libvterm is plain C99 and builds with MSVC (Qt Creator does this).
- **Dependency:** vendor libvterm's ~15 C files (MIT) rather than pkg-config so every
  platform gets the same 0.3.3 behaviour.

## Recommendation

**Conditional go for continuing the owned engine behind `TerminalBackend`; no-go for replacing
KonsolePart on Linux now.**

- Go: in 50 minutes the libvterm engine ran bash, vim, less, htop and tmux correctly, handled
  resize, CJK widths, OSC 8, and exposed exactly what KonsolePart withholds
  (`screenText`, `scrollbackText`, `altScreen`, link and path clicks before anything opens).
  Interactive latency matched Konsole, including Ctrl+C under a flood.
- Not yet: floods are 2x slower than Konsole with libvterm's scroll implementation, colour emoji
  fail on Qt 5, and IME, accessibility, selection polish, reflowed scrollback and the Konsole
  features users see (profiles, search) are missing.
- Path: (1) put the KonsolePart code in `src/main.cpp` behind `TerminalBackend` (1 week), so
  engines can be switched per platform; (2) productionize this engine for macOS/Windows first,
  where there is no KonsolePart; (3) re-run this spike's scripts (they are in the evidence
  folder) as the parity gate before flipping Linux; (4) decide the throughput fix (libvterm
  scroll patch vs. libghostty-vt once it tags a C API release) before investing in rendering
  polish, since the widget code is mostly engine-independent.

## Effort estimate (one experienced engineer)

| Step | Estimate |
|---|---|
| `TerminalBackend` adapter for KonsolePart + switch in `src/main.cpp` | 1 week |
| Engine hardening: write queue, worker-thread parse or libvterm scroll patch, vendored libvterm, tests (vttest subset, scripted xdotool/offscreen checks like this spike) | 2-3 weeks |
| Parity with what Relay uses from Konsole: colours/profiles, search, selection polish, hover links, OSC 7/133, scrollback reflow, font zoom, context menu | 4-6 weeks |
| IME + accessibility | 2-3 weeks |
| Windows ConPTY `Pty` + packaging | 2-3 weeks |
| macOS `Pty` details + packaging | 1-2 weeks |
| **Total to a shippable cross-platform engine** | **12-18 weeks** (the earlier 6-10 week parity figure holds for Linux-only parity without IME/accessibility) |
