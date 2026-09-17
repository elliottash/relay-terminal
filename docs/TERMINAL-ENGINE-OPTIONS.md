# Terminal engine options: permissive, cross-platform alternatives to KonsolePart

Date: 2026-09-17. Research only (no code changed). Context: [ENGINE-SPIKE.md](ENGINE-SPIKE.md)
(libvterm + QPainter spike, about 2x slower than Konsole on a 200 MB `cat`) and
[NEXT-STEPS-RESEARCH.md section A](NEXT-STEPS-RESEARCH.md). Relay is GPL-3.0-or-later, C++17,
Qt5 now with Qt6 as the target. The owner prefers permissive licenses (MIT/BSD/Apache/zlib/MPL).

Method: shallow clones of each repository on 2026-09-17 (LICENSE file, headers, CMake/Cargo files,
CI workflows), GitHub API for last push and latest release, crates.io API for published crate
versions. Nothing was built or benchmarked here; performance claims below are the projects' own
unless marked as Relay measurements.

Relay's needs, for the "features" column: **T** screen + scrollback text, **A** alternate-screen
state, **7** OSC 7 cwd, **8** OSC 8 hyperlinks, **P** OSC 133 prompt marks, **I** inject
display-only bytes (host feeds the parser, so it can write output that never reaches the shell),
**S** selection, **K** key/mouse encoding. Click hooks, IME and rendering are the host's job with
every non-widget option.

## Short answer

There is **no permissively licensed, maintained, cross-platform Qt terminal *widget***. Every Qt
widget is GPL (QTermWidget, qmltermwidget, Konsole, Qt Creator's terminal), and Contour's Qt
frontend is a QtQuick app, not a reusable widget. The permissive options are **emulator cores**
(state + parser, bring your own renderer and PTY) or **xterm.js inside QtWebEngine**. So "instead of
building our own" really means "instead of building our own *emulator core*": Relay still owns the
Qt view (the spike's `VTermWidget` code, about 1 400 lines, is mostly engine-independent) and the
PTY layer.

## Comparison table

| Name | License (verified) | Linux / macOS / Windows | Embeddable in Qt? how | Language | Maintained? | Performance evidence | Features vs needs | Effort | Risks |
|---|---|---|---|---|---|---|---|---|---|
| **libghostty-vt** ([ghostty](https://github.com/ghostty-org/ghostty)) | MIT ([LICENSE](https://github.com/ghostty-org/ghostty/blob/main/LICENSE)) | Y / Y / Y (CI jobs `build-libghostty-vt`, `-macos`, `-windows`, `-wasm`, `-android` in [test.yml](https://github.com/ghostty-org/ghostty/blob/main/.github/workflows/test.yml)) | C library; shared/static; [CMakeLists.txt](https://github.com/ghostty-org/ghostty/blob/main/CMakeLists.txt) wraps `zig build -Demit-lib-vt` for `FetchContent`/`find_package`; Qt view is ours | Zig, C API ([vt.h](https://github.com/ghostty-org/ghostty/blob/main/include/ghostty/vt.h)) | Very active (push 2026-09-16); **no libghostty version tag** (only app tags v1.3.1 2026-03-13); header says "API is not yet stable" | Author: vtebench via Ghostling "libghostty is FAST" ([post](https://hachyderm.io/@mitchellh/116267947446799823)); ">2x faster than any other leading fast terminal" on ASCII/Unicode/CSI ([post](https://x.com/mitchellh/status/2074167186785226899)); SIMD UTF-8 decode | T (formatter, grid refs), A (`GHOSTTY_TERMINAL_SCREEN_ALTERNATE`), 7 (`OPT_PWD_CHANGED`), 8 (`ghostty_grid_ref_hyperlink_uri`), P (per-cell `SEMANTIC_PROMPT/INPUT/OUTPUT`), I (`ghostty_terminal_vt_write`), S (`selection.h`), K (key/mouse/focus/paste encoders, kitty keyboard), search, reflow, kitty graphics, render-state dirty API, snapshots. **No PTY, no renderer** | M | API churn until tagged (pin a commit); Zig >= 0.16 toolchain in every build and CI; no Qt embedder precedent (C++ bindings exist: [libghostty-cpp](https://github.com/Uzaaft/libghostty-cpp)) |
| **Contour libs** ([contour](https://github.com/contour-terminal/contour)): vtparser, vtbackend, vtpty, vtrasterizer, crispy | Apache-2.0 ([LICENSE.txt](https://github.com/contour-terminal/contour/blob/master/LICENSE.txt)); deps libunicode, boxed-cpp, reflection-cpp, termbench-pro all Apache-2.0; MS GSL MIT | Y / Y / Y (+FreeBSD, OpenBSD per README) | C++ static CMake targets inside the Contour tree (no install/export of vtbackend); `vtbackend` is Qt-free; GUI is `TerminalDisplay : QQuickItem` + QRhi, so as a widget only via `QQuickWidget`/window container and Contour's config/session model | C++23 (`CMAKE_CXX_STANDARD 23`); frontend Qt 6 only (validated 6.11) | Active (push 2026-09-11; v0.7.0 2026-08-17, v0.6.3 2026-04) | Project claims "actually fast", GPU rendering; ships `bench-headless` and [termbench-pro](https://github.com/contour-terminal/termbench-pro); no neutral numbers found | T, A (`isAlternateScreen`), 7 (`WorkingDirectory`), 8, P (OSC 133 command blocks, folding, `SemanticBlockTracker`), I (`Terminal::writeToScreen`), S, K, vi mode, search, sixel, kitty graphics, ReGIS, reflow, status line, accessibility (`TerminalAccessible`); vtpty has Unix PTY + ConPTY (ships `conpty.dll`/OpenConsole) | M-L | Internal API, not a library contract (large `Terminal` class, parser thread + `_stateMutex`, `Events` callbacks on parser thread); C++23 raises Relay's compiler floor (Relay is C++17); heavy CPM dependency graph; single main maintainer |
| **alacritty_terminal** ([crate](https://crates.io/crates/alacritty_terminal)) | Apache-2.0 ([Cargo.toml](https://github.com/alacritty/alacritty/blob/master/alacritty_terminal/Cargo.toml)); parser `vte` Apache-2.0 OR MIT | Y / Y / Y ([tty/windows/conpty.rs](https://github.com/alacritty/alacritty/tree/master/alacritty_terminal/src/tty/windows)) | Needs a Rust staticlib + C FFI shim (cbindgen) and Cargo in the CMake build | Rust | Active (0.26.0 on crates.io 2026-04-06; push 2026-08-31); Alacritty itself self-described "beta" | Alacritty is a reference fast terminal ([vtebench](https://github.com/alacritty/vtebench) is theirs); Zed ships it ([Cargo.toml](https://github.com/zed-industries/zed/blob/main/Cargo.toml) pins a Zed fork) | T, A, 8 (per-cell `Hyperlink`), I (feed `vte::ansi::Processor`), S, vi mode, regex search, reflow; tty module with event loop and ConPTY. **7 and P not handled** (unknown OSCs go to `unhandled` in [vte ansi.rs](https://github.com/alacritty/vte/blob/master/src/ansi.rs)): host must pre-scan the byte stream. No graphics | L | FFI design is ours; Rust toolchain; Zed needed a fork; minimal-feature philosophy upstream |
| **wezterm-term / termwiz / portable-pty** ([term](https://github.com/wezterm/wezterm/tree/main/term)) | MIT ([LICENSE.md](https://github.com/wezterm/wezterm/blob/main/LICENSE.md), crate manifests) | Y / Y / Y (portable-pty [pseudocon.rs](https://github.com/wezterm/wezterm/blob/main/pty/src/win/pseudocon.rs): ConPTY, prefers sideloaded `conpty.dll`) | Rust + FFI shim, same as Alacritty | Rust | Repo active (push 2026-09-17) but **no WezTerm release since 20240203**; `wezterm-term` 0.1.0 **not published on crates.io**; termwiz 0.23.3 / portable-pty 0.9.0 last published 2025 | None found for the core; WezTerm is not known as a throughput leader | T, A (`is_alt_screen_active`), 7 (`get_current_dir`), 8, P (`SemanticZone`), I (`advance_bytes`), S, K, sixel, iTerm2 + kitty images | L | Git-path dependency on a monorepo, not a versioned library; large dependency tree; cadence risk |
| **xterm.js** ([repo](https://github.com/xtermjs/xterm.js)) in `QWebEngineView` + C++ PTY | MIT; QtWebEngine is LGPL-3/GPL/commercial (Chromium inside) | Y / Y / Y (QtWebEngine on Windows is MSVC-only ([platform notes](https://doc.qt.io/qt-6/qtwebengine-platform-notes.html))) | `QWebEngineView` per pane or one view hosting many; bytes and events over `QWebChannel`; PTY in C++ (Pty-Qt or own) | TypeScript in Chromium | Very active (6.0.0 2025-12-22; push 2026-09-13) | WebGL renderer addon, "up to 900%" frame-time gain vs canvas ([PR 1790](https://github.com/xtermjs/xterm.js/pull/1790)); the JS parser plus a Qt-to-JS IPC hop is the throughput ceiling | T (buffer API), A (`buffer.active.type`), 7 and P via public `parser.registerOscHandler` + markers/decorations (VS Code's shell integration does this), 8 (`OscLinkService`, `linkHandler`), I (`term.write`), S, search, unicode11/graphemes, image (sixel/iTerm), ligatures, serialize addons | M | Adds Chromium (roughly 100+ MB per platform, GPU process, memory per view); async text access; IME/focus/shortcut quirks across the web boundary; composer and panes stay Qt, terminal does not |
| **librio** ([rio/librio](https://github.com/raphamorim/rio/tree/main/librio)) | MIT | Y / Y / Y (teletypewriter PTY has `unix` and `windows`) | C ABI static lib ([librio.h](https://github.com/raphamorim/rio/blob/main/librio/include/librio.h), 430 lines): PTY + VT + render-state pull API, no drawing | Rust | Active (v0.5.27 2026-08-30); librio `publish = false`, packaged as a Swift xcframework | None found | PTY included; OSC coverage not verified | M | Young, shaped for a Swift host; unversioned C ABI |
| **libvterm** (current spike) | MIT | Y / Y / Y (Qt Creator builds it everywhere) | C, vendor ~15 files | C99 | Upstream tarball 0.3.3 ([leonerd](https://www.leonerd.org.uk/code/libvterm/)); **neovim/libvterm archived 2026-06** (Neovim vendored it into `src/nvim/vterm`) | Relay spike: 27 MB/s vs Konsole 54 MB/s on 200 MB `cat` | T, A, I, K; 7/8/P via OSC fallback in host; no graphics, no scrollback reflow | S (done) | Slow scroll path; upstream nearly dormant |
| **libtsm** ([kmscon/libtsm](https://github.com/kmscon/libtsm)) | MIT, except bundled htable LGPL-2.1 ([COPYING](https://github.com/kmscon/libtsm/blob/main/COPYING)) | Plain C, POSIX-oriented; Windows untested | C, meson | C | Active again under kmscon (v4.7.1 2026-08-14) | None | T, A, S, OSC callback (OSC buffer capped at 128 bytes, too short for many OSC 8 URLs) | M | Fewer features than libvterm; not an upgrade |

Non-permissive or single-platform references (not candidates under the owner's constraint):

| Name | License | Why not |
|---|---|---|
| QTermWidget ([lxqt](https://github.com/lxqt/qtermwidget)) | GPL-2.0-or-later (a few LGPL/BSD files) | GPL; Konsole-derived; Unix PTY only (2.4.0, 2026-04) |
| qmltermwidget ([Swordfish90](https://github.com/Swordfish90/qmltermwidget)) | GPL-2.0 | GPL; last release 2018 |
| Qt Creator terminal ([solutions/terminal](https://github.com/qt-creator/qt-creator/tree/master/src/libs/solutions/terminal)) | Qt-Commercial OR GPL-3.0 (on top of MIT libvterm + libptyqt) | Usable by GPL-3 Relay but not permissive; best design reference for a QPainter terminal view (glyph cache, box drawing, sixel) |
| Windows Terminal ([microsoft/terminal](https://github.com/microsoft/terminal)) | MIT | TerminalCore/TerminalControl are C++/WinRT, Windows-only; the useful part for Relay is ConPTY and the redistributable `conpty.dll`/OpenConsole |
| Rio app / sugarloaf | MIT | wgpu renderer and winit window, not Qt-embeddable; librio above is the embeddable part |
| kitty | GPL-3.0 | GPL, own OpenGL app |
| foot | MIT | Wayland-only app |
| st | MIT/X | X11-only app, no library boundary |

PTY libraries (every core option except Contour vtpty and librio needs one):

| Name | License | Platforms | Status | Note |
|---|---|---|---|---|
| Pty-Qt ([kafeg/ptyqt](https://github.com/kafeg/ptyqt)) | MIT | Unix forkpty, Windows ConPTY + winpty | Last commit 2022-04, release 0.6.5 (2021) | Qt API; Qt Creator carries a maintained patched copy ([libptyqt](https://github.com/qt-creator/qt-creator/tree/master/src/libs/3rdparty/libptyqt), MIT) |
| portable-pty | MIT | Unix, ConPTY | 0.9.0 (2025-02); used by Zed | Rust only |
| node-pty | MIT | Unix, ConPTY (winpty removed; Windows 10 1809+) | 1.1.0 (2025-12) | Needs Node/Electron; not usable from Qt without a Node process |
| winpty | MIT | Windows < 1809 | Last release 2017 | Obsolete; ConPTY replaces it |
| Contour vtpty | Apache-2.0 | Unix, ConPTY, SSH | Active | Tied to crispy/Contour build |
| Relay `engine/PtyUnix.cpp` | Relay's own | Linux, macOS | Spike | ConPTY implementation still to write (2-3 weeks in the spike estimate) |

## Top candidates

### 1. libghostty-vt (recommended primary)

Fits Relay's needs most directly through a plain C API: every item in the needs list has a named
entry point (OSC 7 callback, per-cell OSC 133 semantic type, per-cell hyperlink URI, alternate-screen
query, formatter for plain-text extraction, selection and search in scrollback, key/mouse encoders,
render state with row dirty tracking). It does not include a PTY or renderer, which matches the
spike's split (`relay::Pty` + Qt view). Performance is the project's headline claim and it is the
same code Ghostty ships, so the 2x flood gap in the spike is most likely to close here.
Costs: Zig 0.16 in the toolchain (the CMake wrapper hides the invocation but not the dependency),
and an explicitly unstable API with no version tag, so Relay must pin a commit and keep all calls
in one adapter file. Examples to start from: `example/c-vt-render`, `c-vt-stream`,
`cpp-vt-stream`, `c-vt-selection`, and [Ghostling](https://github.com/ghostty-org/ghostling) (MIT).

### 2. Contour vtbackend (strongest C++ alternative)

The most complete permissive engine in C++: OSC 133 command blocks and folding, OSC 7, OSC 8,
sixel/kitty images, vi mode, search, reflow, an accessibility layer, and vtpty with ConPTY. It is
the only candidate in Relay's own language. But the libraries are Contour's internals, not a
product: static targets inside the tree, no installed headers, a threading contract documented in
comments (callbacks on the parser thread under a non-recursive mutex), C++23, and the libunicode /
boxed-cpp / reflection-cpp / GSL stack. The Qt view is a QtQuick item wired to Contour's config
and session model, so Relay would reuse `vtbackend` (and perhaps `vtpty`) and draw with its own
QWidget view, or accept `QQuickWidget` and a larger fork. Treat it as a vendored fork with regular
rebases.

### 3. alacritty_terminal (proven embed, higher glue cost)

Production-proven as an embedded core (Zed), Apache-2.0, fast, with ConPTY in its tty module. It
lacks OSC 7 and OSC 133 (Relay would scan the byte stream before feeding it, which is simple but
duplicates parsing state), has no graphics, and needs a Rust FFI layer we design and maintain.
Choose it only if the Zig toolchain or libghostty API churn turns out to be unacceptable.

### 4. xterm.js in QtWebEngine (fastest route to full features, heaviest runtime)

The most feature-complete and most widely embedded (VS Code, Tabby, Hyper), with every Relay need
reachable through public APIs and addons. The cost is Chromium: install size, memory per pane,
MSVC-only Windows builds, async screen text, and a JS/IPC hop on every output chunk, which works
against "good performance on huge output". It also splits Relay's UI across two toolkits. Worth
keeping as a fallback, not a first choice for a Qt-native terminal.

### Not recommended

wezterm-term: excellent features, but not a published library and WezTerm has had no release since
February 2024. librio: interesting (PTY + VT in one C ABI), too young and Swift-shaped. libtsm:
no gain over libvterm.

## Recommendation

1. **Do not look for a drop-in widget; none is permissive.** Keep Relay's own Qt view and
   `TerminalBackend` interface from the spike, and replace only the emulator core.
2. **Adopt libghostty-vt as the core to evaluate first**, behind the existing `TerminalBackend`,
   pinned to a commit, with all C calls in one adapter. Keep libvterm as the working fallback
   until the benchmark and parity gates pass.
3. **Benchmark first, before any rendering work:** extend the spike's headless `--bench` to feed
   the same 200 MB base64 file (and a no-newline 50 MB file) into libvterm, libghostty-vt and
   Contour `vtbackend` (its `bench-headless`), measuring MB/s and peak RSS; then the GUI `cat`
   test from `scripts/perf.sh` against Konsole (3.7 s) and xterm (3.3 s). Gate: GUI within 1.2x of
   Konsole. Also run vtebench/termbench-pro loads for scrolling and unicode.
4. **Second benchmark: Contour vtbackend.** If libghostty-vt's toolchain or API churn blocks
   Windows/macOS CI, Contour is the C++ alternative with the richest shell-integration model;
   budget for moving the engine adapter to C++23 in its own target.
5. **PTY:** keep `relay::Pty`; write ConPTY using Qt Creator's MIT `libptyqt` and portable-pty as
   references (both can load a sideloaded `conpty.dll`/OpenConsole for current ConPTY behaviour).
   Pty-Qt upstream is unmaintained since 2022, so do not depend on it directly.
6. **Rendering:** reuse the spike's QPainter view; take design cues (glyph cache, box drawing,
   sixel) from Qt Creator's terminal without copying GPL code; move to QRhi only if profiling after
   the engine swap shows painting as the bottleneck.
7. **Keep xterm.js + QtWebEngine and alacritty_terminal as documented fallbacks**, not parallel work.
