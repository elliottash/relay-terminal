# Next steps research: terminal engine, pane isolation, terminal-only Relay

Date: 2026-09-17. Inputs for the owner discussion. Related issues:
`issues/features/2026-09-17-portable-terminal-engine.md`, `2026-09-17-clickable-paths.md`,
`2026-09-17-keyboard-jump-to-output-links.md`, `2026-09-17-pane-process-isolation.md`,
`2026-09-17-terminal-only-tui-relay.md`. Background: `docs/CONTROL-AND-FILE-PANES-RESEARCH.md`.

Relay today: loads `kf6/parts/konsolepart`, falling back to KF5 (`src/main.cpp`); this machine has
Konsole 23.08.5 (KF5). Relay is AGPL-3.0, which is compatible with everything below.

## A. Konsole fork vs Relay-owned terminal engine

### What KonsolePart gives and withholds

- Public `Part.h` (master) has only `createSession`, `openTeletype`, profile dialogs, monitor toggles,
  and the signals `overrideShortcut`, `silenceDetected`, `activityDetected`, `currentDirectoryChanged`.
  No screen text, no click signal, no mode state
  ([Part.h](https://invent.kde.org/utilities/konsole/-/raw/master/src/Part.h)).
- **New finding:** Konsole **24.02+** exports screen text on each session's D-Bus object:
  `getAllDisplayedText(bool removeTrailingEmptyLines)`, `getAllDisplayedTextList`,
  `getDisplayedText(int startLineOffset, int endLineOffset)` and `getDisplayedTextList`
  ([Session.h master](https://invent.kde.org/utilities/konsole/-/raw/master/src/session/Session.h)).
  They are absent in 23.08.5 and present in every tag from v24.02.0 through v26.04.0 (checked).
  They read the *view's* `ScreenWindow`, so they return displayed lines, not the full
  scrollback, and give no colours, alt-screen flag or hyperlinks. Sessions register
  `/Sessions/<id>` on the session bus even inside a Part (Yakuake scripts use the same paths
  ([KDE blog](https://blogs.kde.org/2025/04/02/konsole-layout-automation-part-2/))), so a KF6 Relay can call
  them on its own bus name with no fork. The same object already offers `sendText`, `runCommand`,
  `sendMouseEvent`, `foregroundProcessId`, `setHistorySize`, including in 23.08. Still to verify:
  the `HAVE_DBUS` build flag (Session.cpp registers `/Sessions/<id>` only when set), and which `/Sessions/<id>` belongs to which pane.
- Click handling stays inside Konsole: `FileFilterHotSpot::activate` runs `TextEditorCmd` or
  `KIO::OpenUrlJob` ([FileFilterHotspot.cpp](https://invent.kde.org/utilities/konsole/-/raw/master/src/filterHotSpots/FileFilterHotspot.cpp)).

### Option 1: fork or patch KonsolePart

Patches needed (all small in lines, but they touch private classes):

1. `FileFilterHotSpot::activate` / `UrlFilterHotSpot` (including OSC 8 `EscapedUrlFilter`) emit a
   host signal before opening anything, so every click goes to Relay.
2. Part API: current session's screen text with scrollback range, selection text, cursor position,
   alt-screen state (`Emulation` / `Screen` already track it), and a "highlight range" call for
   keyboard link stepping.
3. Optional: a prompt/OSC 133 mark API for jumping between command blocks.

| Aspect | Evidence |
|---|---|
| Build deps (master) | Qt 6.5+, KF 6.6+: Bookmarks, Config, ConfigWidgets, CoreAddons, Crash, GuiAddons, I18n, IconThemes, KIO, NewStuff, Notifications, NotifyConfig, Parts, Service, TextWidgets, WidgetsAddons, WindowSystem, XmlGui; Pty on Unix; DBusAddons, GlobalAccel with D-Bus ([CMakeLists.txt](https://invent.kde.org/utilities/konsole/-/raw/master/CMakeLists.txt)) |
| License | GPL-2.0-or-later (SPDX headers in `src/`); combined work under Relay's AGPL-3.0 is fine |
| Cadence | Ships with KDE Gear three times a year (April, August, December) plus bugfix point releases ([schedules](https://community.kde.org/Schedules)); a fork rebases 3x/year |
| KF5 to KF6 | Konsole moved to Qt6/KF6 at 24.02; distros on 23.08 (Ubuntu 24.04, this machine) have only KF5; a fork must pick KF6 and bundle it (Flatpak KDE runtime) |
| Platforms | Linux (and BSD). macOS/Windows builds are experimental Craft nightlies; KPty is Unix-only |
| Precedent | Embedders use the stock Part, not forks: Yakuake ([terminal.cpp](https://github.com/KDE/yakuake/blob/master/app/terminal.cpp)), Kate ([addons/konsole](https://github.com/KDE/kate/tree/master/addons/konsole)), Dolphin ([terminalpanel.cpp](https://github.com/KDE/dolphin/blob/master/src/panels/terminal/terminalpanel.cpp)), KDevelop ([plugins/konsole](https://github.com/KDE/kdevelop/tree/master/plugins/konsole)). Kate/Dolphin need the same things Relay wants and get them via `TerminalInterface` and D-Bus, so upstreaming a Part API is plausible |

Effort: patch + private build 1-2 weeks; packaging a bundled Konsole (Flatpak or AppImage) 1-2
weeks; rebase 1-3 days per Gear release. Better variant: **upstream** the Part API (merge request to
invent.kde.org/utilities/konsole) and carry the patch only until a release has it.
Risks: ties Relay to KF6 and Linux; private-class patches break on refactors; users on KF5 distros
need a bundled runtime; nothing gained for macOS/Windows.

### Option 2: Relay-owned engine

| Candidate | Status and fit |
|---|---|
| **libvterm** (C, MIT, [home](https://www.leonerd.org.uk/code/libvterm/)) | Mature; used by Neovim ([fork](https://github.com/neovim/libvterm)), Emacs vterm ([emacs-libvterm](https://github.com/akermu/emacs-libvterm)), Vim. 0.3 added reflow for the active screen only, not scrollback (host must reflow its own scrollback) ([vterm.h](https://github.com/neovim/libvterm/blob/nvim/include/vterm.h)). No sixel/kitty graphics, no OSC 8 built in (host parses via OSC callbacks) |
| **Qt Creator 11+ terminal** | libvterm 0.3.3 + Pty-Qt, both MIT ([libvterm](https://doc.qt.io/qtcreator/qtcreator-attribution-libvterm.html), [Pty-Qt](https://doc.qt.io/qtcreator/qtcreator-attribution-ptyqt.html), [ConPTY](https://doc.qt.io/qtcreator/qtcreator-attribution-ptyqt-conpty.html)). Widget is reusable in `src/libs/solutions/terminal` (`TerminalView`, `TerminalSurface`, glyph cache, box drawing); QPainter + `QRawFont`/`QGlyphRun`/`QTextLayout`; master handles alt screen, OSC 8, selection callbacks and sixel ([terminalview.cpp](https://github.com/qt-creator/qt-creator/blob/master/src/libs/solutions/terminal/terminalview.cpp), [terminalsurface.cpp](https://github.com/qt-creator/qt-creator/blob/master/src/libs/solutions/terminal/terminalsurface.cpp)). License of that code: `LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0`, usable in GPL-3.0 Relay |
| **Pty-Qt** ([kafeg/ptyqt](https://github.com/kafeg/ptyqt), MIT) | forkpty on Unix, ConPTY and WinPTY on Windows, Qt API. Qt Creator carries a patched copy |
| **libghostty-vt** (Zig, C API, MIT) | Fast, modern (kitty keyboard, graphics, reflow), Linux/macOS/Windows/WASM. Public alpha since Sept 2025; C API for parser/terminal still being added; no tagged release as of the 1.3.0 notes ([announcement](https://mitchellh.com/writing/libghostty-is-coming), [C API discussion](https://github.com/ghostty-org/ghostty/discussions/11348), [vt.h](https://github.com/ghostty-org/ghostty/blob/main/include/ghostty/vt.h), [1.3.0 notes](https://ghostty.org/docs/install/release-notes/1-3-0)). Good second-generation target; Zig toolchain in the build |
| **alacritty_terminal** (Rust, Apache-2.0) | "Library for writing terminal emulators" ([crates.io](https://crates.io/crates/alacritty_terminal)); production embedder: Zed ([crates/terminal](https://github.com/zed-industries/zed/tree/main/crates/terminal)). Needs a C FFI shim and Cargo in the build |
| **wezterm-term** (Rust, MIT) ([term](https://github.com/wezterm/wezterm/tree/main/term)) | Full featured (sixel, kitty/iTerm images), not versioned as a stable library; same FFI cost |

Work the host owns regardless of engine: Qt rendering (QPainter with a glyph cache is enough for
typical 60 Hz terminals, Qt Creator proves it; QRhi/OpenGL only if profiling says so), font
fallback and ligatures (HarfBuzz via `QTextLayout`/`QGlyphRun`; ligatures need run-level shaping),
IME (`inputMethodEvent`, preedit drawing), selection and word/line semantics, scrollback storage and
reflow, mouse reporting modes, bracketed paste, focus events, OSC 7/8/52/133, search, URL/path
detection, accessibility, key encoding (kitty protocol), and high-throughput output (`cat` of a 1 GB
file must not freeze the UI: parse off the GUI thread, coalesce repaints).

Effort estimate (one experienced engineer): spike with Qt Creator's `solutions/terminal` + Pty-Qt in
one pane: 1-2 weeks. Parity with what Relay uses from Konsole (profiles/colours, search, hotspots,
copy-on-select, scrollback, shell integration hooks): 6-10 weeks. Windows ConPTY + macOS packaging
and polish: +4-6 weeks. Swapping libvterm for libghostty-vt later: 2-4 weeks if the view talks to
an engine interface from day one.
Risks: a long tail of emulator bugs (vttest, tmux, vim, htop, CJK/emoji widths, RTL); perf
regressions on huge output; losing Konsole features users notice (profiles UI, KIO, split view
niceties); maintenance moves in-house.

### Option 3: hybrid

- **3a. Keep KonsolePart on Linux, build the engine behind a `TerminalBackend` interface.** Ship
  Konsole while the engine reaches parity, per-platform switch, then flip Linux when tests pass.
- **3b. Screen text now without a fork:** on KF6 use `getDisplayedText` over D-Bus for visible
  lines. Alternative for KF5 and scrollback: Relay creates the PTY, runs the shell on the slave,
  passes a proxy PTY to `Part::openTeletype(fd)`, and tees the byte stream into a libvterm
  shadow screen. That also yields alt-screen state, OSC 8 links and OSC 133 marks. Cost 2-3
  weeks. Risks: two emulators disagreeing about width/wrap; resize must reach both; latency
  of the extra copy (small).
- Clicks still need a Konsole patch under 3b; keyboard link stepping can be done entirely by
  Relay (overlay highlight drawn from shadow-screen coordinates, approximate while scrolled).

### Pros and cons summary

| | Fork/patch KonsolePart | Own engine (libvterm now, libghostty-vt later) |
|---|---|---|
| Time to clicks + screen text | 2-4 weeks | 6-10 weeks to parity |
| macOS / Windows | No | Yes (Pty-Qt ConPTY/forkpty) |
| Emulator quality | Mature, 20+ years | Libvterm is mature; the widget is new code (Qt Creator's code lowers that) |
| Maintenance | Rebase 3x/year; KF6 + bundled runtime | Own every rendering/input bug |
| Dependencies | ~18 KDE Frameworks | libvterm + Pty-Qt (MIT), Qt only |
| Control (clicks, alt screen, OSC 8/133, overlays, inline blocks) | Only what the patch exposes | Complete |
| Upstream path | Part API MR could land in Konsole | n/a |

**Recommendation:** hybrid 3a. Do not fork Konsole. Now: use the KF6 D-Bus text calls where
available, and send a small upstream Part MR (click signal + text API). In parallel, spike Qt
Creator's `solutions/terminal` + Pty-Qt behind a backend interface; if the spike handles vim,
tmux, htop and a 1 GB `cat` in a 2-week time-box, make it the cross-platform engine and retire
KonsolePart when parity tests pass. Revisit libghostty-vt when it tags a release.

## B. Pane-specific processes

What can take down all of Relay today:

- **Kernel OOM killer** picks the highest `oom_score` (roughly RSS share, plus `oom_score_adj`)
  ([proc_pid_oom_score_adj(5)](https://man7.org/linux/man-pages/man5/proc_pid_oom_score_adj.5.html)).
  A runaway command usually dies alone, but the GUI (all panes' screens, scrollback, Qt) can be the
  biggest process when the runaway is spread over many children (`make -j`, browsers, test farms).
- **systemd-oomd** (default on Ubuntu 22.04+, Fedora) kills a *whole cgroup* under memory pressure
  or swap exhaustion ([systemd-oomd](https://www.freedesktop.org/software/systemd/man/latest/systemd-oomd.service.html)).
  Desktop launchers put an app and all its children in one `app-*.scope`, so one pane's build
  gets the Relay window and every pane killed together. This is the most likely "whole terminal
  crashed" case.
- **GUI memory:** Relay's profile sets `HistoryMode=2` (unlimited). Konsole keeps unlimited
  history in a file under `QDir::tempPath()` by default ([HistoryFile.cpp](https://invent.kde.org/utilities/konsole/-/raw/master/src/history/HistoryFile.cpp)), which lives in RAM if the temp dir is tmpfs; huge output also costs
  GUI CPU/RAM while it renders.
- **In-process crashes:** a KonsolePart or Qt bug segfaults everything. Python workers and
  shells are already separate processes, so an agent crash only affects its pane.

Options:

1. **Per-pane transient scopes.** Start each pane's shell and worker with
   `systemd-run --user --scope --unit=relay-pane-<id> -p MemoryMax=<n> -p MemoryHigh=<n*0.8> --`
   ([systemd-run](https://www.freedesktop.org/software/systemd/man/latest/systemd-run.html),
   [resource control](https://www.freedesktop.org/software/systemd/man/latest/systemd.resource-control.html)).
   `--scope` execs in place, so KonsolePart's `startProgram` PTY semantics are unchanged. The
   limit kills only that pane's cgroup; oomd also acts per pane. Fall back to a plain spawn when
   no user manager exists (containers, WSL, non-systemd distros).
2. **`oom_score_adj`**: an unprivileged process may raise its children's score; set +300..+500 on
   pane shells/workers so the kernel never picks the GUI first.
3. **Bounded scrollback:** default to a large fixed limit (e.g. 100k lines) instead of unlimited;
   history file in `$XDG_CACHE_HOME` rather than tmpfs.
4. **Watchdog + UX:** watch `memory.events` (`oom_kill`) in each scope; show "pane killed: out of
   memory (limit N GB)" with restart; optional soft warning at `MemoryHigh`.
5. **Multi-process UI (browser-style).** Each pane's renderer in its own process: on X11 via
   XEmbed ([spec](https://specifications.freedesktop.org/xembed-spec/latest/)) / `QWindow::fromWinId`
   ([QWindow](https://doc.qt.io/qt-6/qwindow.html)); Wayland has no cross-client embedding (it would need
   a nested compositor or shared-memory frame transport). Large effort, only sensible with the
   owned engine (renderer process draws into shared memory, GUI composites).

**Recommendation:** 1 + 2 + 3 + 4 (about 1 week total, Linux), which covers the realistic failure
(one pane's workload exhausting memory). Leave 5 until the owned engine exists; that engine should
parse PTY output off the GUI thread and cap per-pane memory itself.

## C. "croft" and a terminal-only Relay

**What "croft" is:** most likely **croft**, a VS Code-style IDE that runs entirely in the terminal:
explorer sidebar, editor, bottom panel with PROBLEMS/OUTPUT/TERMINAL/CAPTURES/PORTS tabs, LSP,
tree-sitter, git hunks, a real terminal with shell integration, debugging, shared sessions
(`croft attach`) and AI pairing via MCP or `croft pair`. Rust, single static binary, MIT
([docs](https://docs.croft.software/), [GitHub](https://github.com/vitali87/croft),
[Terminal Trove](https://terminaltrove.com/croft/)). Other TUI-first agent tools the owner may be
comparing with: Crush by Charm ([charmbracelet/crush](https://github.com/charmbracelet/crush), Go/Bubble
Tea), opencode ([opencode.ai](https://opencode.ai/)), Claude Code, Codex CLI, aider. Warp is a GUI app.

**What a pure-TUI Relay would be:** Relay's value in a terminal: one prompt that routes to shell or
agent, inline agent blocks, delegate/take-over of running programs, file explorer/preview panes, all
usable over SSH and in any emulator. Shape:

- **Reuse:** `backend/worker.py` and `relay_core` (router, agent loop, providers, skills) unchanged;
  the TUI talks the same JSON protocol the Qt app uses.
- **Terminal panes inside a TUI** need their own VT emulator (a TUI is itself drawn on a terminal):
  `pyte` in Python, `vt100`/`alacritty_terminal` in Rust, or run inside tmux and drive panes with
  `tmux` commands (cheapest, like many agent orchestrators).
- **Framework:** Python **Textual** ([textual.textualize.io](https://textual.textualize.io/)) keeps one
  language with the backend and has widgets, layouts, mouse, and a terminal-widget ecosystem; Rust
  **Ratatui** ([ratatui.rs](https://ratatui.rs/)) or Go **Bubble Tea**
  ([charmbracelet/bubbletea](https://github.com/charmbracelet/bubbletea)) give a single fast binary
  but a second language next to Python.
- **Lighter alternative:** no full-screen TUI, a shell integration mode: a `relay` readline/
  prompt wrapper in Bash/Zsh that routes lines starting with a marker (or classified by the router)
  to the worker and prints agent blocks inline. Works in any terminal including Konsole, tmux, SSH.

Relation to the Qt app: same backend, different frontends. The work that makes it cheap is keeping
UI logic out of `src/main.cpp` and the worker protocol stable and documented.

Effort: prompt-wrapper mode 1-2 weeks; Textual app with shell pane (pyte or tmux) + agent blocks +
file explorer 4-6 weeks; croft-level IDE features are out of scope.
