# Control handoff and file panes: research

Research date: 2026-09-17. Scope: (A) how Warp and similar tools route input while a program runs,
hand control between a human and an agent, and deal with password prompts; (B) how Relay could open
a file-explorer pane or a preview pane when the user clicks a path, with macOS and Windows support later.
Every claim has a link. "Not documented" means no primary source was found.

Warp's behaviour below was observed in the product and in its public documentation, blog and
issue tracker.

## Part A: running programs, human and agent control, passwords

### A1. Where keystrokes go while a command runs (Warp classic input)
- **Command boundaries come from shell hooks.** precmd/preexec send a DCS containing JSON, and Warp
  starts a new block with its own grid for each command ([How Warp Works](https://www.warp.dev/blog/how-warp-works)).
- **The input editor hides almost at once.** Once a command has run for a fraction of a second the
  block counts as long-running, the editor hides and focus moves to the terminal, "prefer terminal
  when input is hidden" ([issue #14128](https://github.com/warpdotdev/warp/issues/14128)).
  So `sudo apt install`, `ssh`, `python`, and a long `make` all hide the editor and receive keystrokes
  directly. Warp does not tell interactive programs apart from non-interactive ones for this purpose.
- **Keys and paste go to the process.** Page Up/Down/Home/End are forwarded during long-running or
  full-screen commands ([Block basics](https://docs.warp.dev/terminal/blocks/block-basics/)).
- **Alt-screen apps are tracked separately.** vim, htop and less get full-screen rendering,
  mouse reporting (hold Shift to bypass it) and the kitty keyboard protocol
  ([Full-screen apps](https://docs.warp.dev/terminal/more-features/full-screen-apps/)).
  The input stays hidden on the alt screen unless the agent is in control or tagged in.
- **Long-running command UI:** a desktop notification when a command finishes after a threshold
  (30 s by default), sent only when you are in another app
  ([Notifications](https://docs.warp.dev/terminal/more-features/notifications/)).
  Also a "Use Agent" footer button and Cmd/Ctrl+I to bring the agent into the running command
  ([Full Terminal Use](https://docs.warp.dev/agents/capabilities/full-terminal-use/)).

### A2. Agent driving interactive CLIs ("Full Terminal Use")
- The agent attaches to the active PTY, sees the live buffer, and writes to it. It can work in psql,
  vim, python, gdb, top and dev servers. It attaches either because it started the command or because
  you tagged it in with **Use Agent** / **Cmd+I** ([docs](https://docs.warp.dev/agents/capabilities/full-terminal-use/)).
- **Approvals:** every PTY write can require approval, with three modes: "Ask on first write",
  "Always ask" and "Always allow". Per action you can allow once, auto-approve similar actions, or press
  Ctrl+C to refine (same docs).
- **Take over / hand back:** a **Takeover** control, or **Cmd+I** (macOS) / **Ctrl+I** (Linux/Windows),
  "stops the agent from issuing any further PTY writes". Click again and "the agent resumes where you
  left off" (same docs). **Cmd+G** toggles whether agent responses are shown.
- **The agent can hand control back on its own** with a reason, as seen in the product; when it
  chooses to is decided by server-side prompts, so it is not observable.
- **Notifications:** agent "Request" notifications cover "command approval, permission requests, and idle
  prompts where the agent is waiting for you" ([Agent notifications](https://docs.warp.dev/agents/capabilities/agent-notifications/)).
- A known bug: taking manual control to type a sudo password over SSH+tmux can leave the agent stuck
  ([#9086](https://github.com/warpdotdev/Warp/issues/9086)).

### A3. Password and secret prompts
- **Detection appears to use the terminal's echo state, not the output text.** An early user guessed
  the echo-state mechanism in [#3396](https://github.com/warpdotdev/Warp/issues/3396), and the false
  positives reported since fit a periodic poll of the tty attributes: the rule that fits the
  behaviour is `!ECHO && ICANON` (raw-mode programs such as neovim turn both off), checked about once
  a second, one notification per command.
- **What a detection does:** a desktop "needs attention" notification, sent only if you are away from the window,
  plus the SSH drag-and-drop upload flow. It does **not** hand off to the agent automatically. False positives happen:
  [#8112](https://github.com/warpdotdev/Warp/issues/8112) reports "waiting for a password" on commands longer than about 3 s.
  Users have asked for automatic takeover on "Password:" ([#8384](https://github.com/warpdotdev/warp/issues/8384)).
- **When a user types the password,** it goes straight to the PTY, because the input editor is hidden.
- **Secret Redaction** is regex-based and off by default. It keeps matched secrets from reaching Warp servers or LLMs
  ([Secret Redaction](https://docs.warp.dev/support-and-community/privacy-and-security/secret-redaction/)).
  It still applies during Full Terminal Use ([docs](https://docs.warp.dev/agents/capabilities/full-terminal-use/)).
- **"The agent will wait for you" for passwords:** not documented beyond the generic Request notifications.

| Situation | What Warp does |
|---|---|
| User command running | Input editor hidden, keys go to the PTY, focus on terminal |
| Alt-screen app (vim/htop/less) | Full-screen grid, mouse reporting, kitty keyboard protocol; input hidden |
| Long command finishes | Notification after 30 s (configurable), only if Warp is not focused |
| Password prompt | Echo-state check about once a second; notify if away; no auto handoff |
| Agent in a REPL/TUI | Reads buffer, writes PTY with approval modes; Cmd/Ctrl+I take over / hand back |
| Agent needs the human | Hands control back with a reason; "Request" notification |
| Secrets | Optional regex redaction before data leaves the machine |
### A4. Other tools, briefly
- **VS Code + Copilot (1.116):** LLM-based "is it waiting for input" detection was removed. The agent now uses
  `send_to_terminal`. When input is needed ("prompting for a password or ... `npm init`"), a question carousel
  shows a **Focus Terminal** button, and typing in the terminal dismisses it and tells the agent
  ([release notes](https://code.visualstudio.com/updates/v1_116)).
- **Claude Code:** the docs describe `!` shell mode (output added to context, no approval) but say nothing about
  passwords or TTY input ([interactive mode](https://code.claude.com/docs/en/interactive-mode)).
  A report says `! sudo -v` exits silently with no prompt; it was closed as not planned
  ([#83046](https://github.com/anthropics/claude-code/issues/83046)). Community workaround: `SUDO_ASKPASS` GUI helper
  ([claude-sudo-askpass](https://github.com/dgutson/claude-sudo-askpass)).
  No official "run `! cmd` for passwords" guidance was found.
- **Cursor:** since 1.6, agent terminals failed with "sudo: a terminal is required to read the password". Staff
  pointed to a setting to restore the old behaviour, with mixed results
  ([forum](https://forum.cursor.com/t/regression-agent-terminals-no-longer-support-sudo-or-interactive-input/136719)).
  Official docs: not documented.
- **Zed:** "Terminal Threads" run an agent CLI/TUI in a real terminal that stays an interactive shell
  ([docs](https://zed.dev/docs/ai/terminal-threads)). How the agent terminal tool handles passwords is not documented
  ([tools](https://zed.dev/docs/ai/tools)).

### A5. Linux signals a host can read without being the controlling process
Checked on this machine (kernel 7.0.0, Python `pty.fork` + `bash -i`):

| Signal | How to read it | Works for Relay? |
|---|---|---|
| Foreground pgrp differs from the shell | `/proc/<shell>/stat` field 8 `tpgid` ([proc_pid_stat(5)](https://man7.org/linux/man-pages/man5/proc_pid_stat.5.html)) | Yes (Relay did this in `src/main.cpp`; since 2026-09-18 it asks the pty master first, with this as the fallback, in `src/Pane.h`) |
| Same, via `tcgetpgrp` on the slave (`/proc/<pid>/fd/0`) | `TIOCGPGRP` | **No: ENOTTY.** The kernel rejects it on a non-master tty unless it is the caller's controlling tty ([tty_jobctrl.c `tiocgpgrp`](https://raw.githubusercontent.com/torvalds/linux/master/drivers/tty/tty_jobctrl.c)). This is a long-standing rule, not new. |
| Same, via `tcgetpgrp` on the **master** fd | `TIOCGPGRP` | Yes, but KonsolePart owns the master (Konsole's `Pty::foregroundProcessGroup` uses it, [API](https://api.kde.org/4.14-api/applications-apidocs/konsole/html/classKonsole_1_1Pty.html)); Relay gets `foregroundProcessId()` through TerminalInterface instead |
| Noncanonical / raw mode | `tcgetattr` on `/proc/<pid>/fd/0` (`!ICANON`) | Yes. Note that the **idle readline prompt is also `!ECHO && !ICANON`** |
| Password prompt | `tcgetattr`: `!ECHO && ICANON` (the rule Warp's behaviour fits) | Yes. `read -s` showed ECHO=0 ICANON=1; `sleep` showed ECHO=1 ICANON=1 |
| Alt screen (DECSET 1049/47) | Must parse the output stream | **No** with KonsolePart 23.08: no screen/mode API ([Part.h](https://invent.kde.org/utilities/konsole/-/raw/v23.08.5/src/Part.h)) |

Gaps: termios is per tty, not per process, so a background job can change it. Polling adds latency
(Warp appears to use about 1 s). Programs like `ssh` and `gpg` may read from `/dev/tty` with their own termios, which is still
visible on the same tty. Alt-screen state and OSC 8 links need the byte stream. One option: `Part::openTeletype(ptyMasterFd, runShell)`
lets the host create the PTY ([Part.h](https://invent.kde.org/utilities/konsole/-/raw/v23.08.5/src/Part.h)).
Relay could then own the master and proxy bytes through its own VT tracker, at the cost of extra latency and complexity.

## Part B: directory and preview panes

### B1. KDE parts
Installed here as KF5/23.08 plugins: `dolphinpart`, `katepart`, `gvpart` (Gwenview), `okularpart`, `konsolepart`
(`/usr/lib/*/qt5/plugins/kf5/parts/`, observed locally).

| Part | Windows | macOS | License |
|---|---|---|---|
| Okular | Microsoft Store since 2019; nightlies ([apps.kde.org](https://apps.kde.org/okular/), [Kate blog](https://kate-editor.org/post/2025/2025-06-03-kate-and-co-in-the-microsoft-store/)) | Nightly ARM/Intel only (apps.kde.org) | GPL-2.0+ |
| KTextEditor/katepart | Kate in Store; nightlies ([get it](https://kate-editor.org/get-it/)) | Nightly DMGs only (same) | LGPLv2+ ([README](https://github.com/KDE/ktexteditor/blob/master/README.md)) |
| Dolphin | Installer and nightly; described as experimental, file deletion broken ([apps.kde.org](https://apps.kde.org/dolphin/), [How-To Geek](https://www.howtogeek.com/kde-dolphin-file-manager-on-windows/)) | Not listed | GPL-2.0+ |
| Gwenview | Not listed | Not listed ([apps.kde.org](https://apps.kde.org/gwenview/)) | GPL-2.0+ |
| Konsole | Nightly installers listed ([apps.kde.org](https://apps.kde.org/konsole/)) | Nightlies; buildable with Craft ([blog](https://clehaxze.tw/gemlog/2023/03-30-install-konsole-on-macos.gmi)) | GPL-2.0+ |

Craft builds these and packages APPX/DMG ([Craft wiki](https://community.kde.org/Craft)). Homebrew casks:
not documented by KDE. Licensing: Relay is AGPL-3.0, so GPL-2.0+ and LGPL parts are compatible. Relay's
bundles must then ship source for everything. Distributing plugins "as parts" on macOS/Windows means shipping
KF runtimes, KIO and the parts themselves in-bundle. How to embed each part on those platforms: not documented.

### B2. Plain Qt building blocks
- `QFileSystemModel` + `QTreeView`/`QListView`: Qt core widgets on every desktop platform ([docs](https://doc.qt.io/qt-6/qfilesystemmodel.html)).
- Markdown: `QTextDocument::setMarkdown`, GitHub dialect by default; "Markdown formatting inside HTML blocks is not supported" ([docs](https://doc.qt.io/qt-6/qtextdocument.html)).
- Code: **KSyntaxHighlighting** is MIT since KF 5.50 (definition files may have other licenses) and has been used by Qt Creator since 4.9
  ([Kate blog](https://kate-editor.org/2018/10/21/mit-licensed-ksyntaxhighlighting-usage/), [Qt Creator attribution](https://doc.qt.io/qtcreator/qtcreator-attribution-ksyntaxhighlighting.html)). Depends only on Qt, so it ports.
- PDF: **QtPdf/QPdfView** wraps PDFium, runs on all Qt WebEngine platforms plus iOS, and is LGPLv3/GPLv2 ([index](https://doc.qt.io/qt-6/qtpdf-index.html), [licensing](https://doc.qt.io/qt-6/qtpdf-licensing.html)). Ubuntu 24.04 has `libqt5pdf5` and `libqt6pdfwidgets6` (apt, local).
- Images: `QImageReader` plus format plugins ([docs](https://doc.qt.io/qt-6/qimagereader.html)).
- Media: **QtMultimedia** uses an FFmpeg backend by default on desktop; apps must bundle FFmpeg or depend on the OS copy; codec patents are a separate issue ([docs](https://doc.qt.io/qt-6/qtmultimedia-index.html)).
- HTML: QtWebEngine is Chromium-based and heavy ([overview](https://doc.qt.io/qt-6/qtwebengine-overview.html)). Avoid it for previews.

### B3. Detecting clickable paths in the terminal
- **KonsolePart hotspots:** the FileFilter matches quoted and unquoted paths with optional `:line:col`.
  Relative names are matched against a listing of the session cwd; absolute paths are taken as given
  ([FileFilter.cpp](https://invent.kde.org/utilities/konsole/-/raw/v23.08.5/src/filterHotSpots/FileFilter.cpp)).
  On click, **text/plain** files open with the profile `TextEditorCmd`, where PATH/LINE/COLUMN are substituted.
  Anything else, including directories and images, goes to `KIO::OpenUrlJob`, the system default app
  ([FileFilterHotspot.cpp](https://invent.kde.org/utilities/konsole/-/raw/v23.08.5/src/filterHotSpots/FileFilterHotspot.cpp)).
  This requires the "Underline files" and "Open files/links by direct click" settings
  ([Profile.h](https://invent.kde.org/utilities/konsole/-/raw/v23.08.5/src/profile/Profile.h), [example](https://gibsonic.org/blog/2024/01/21/opening_files_from_konsole/)).
  **The Part has no click signal and no screen-text API.** Its only signals are overrideShortcut, silence,
  activity and `currentDirectoryChanged` ([Part.h](https://invent.kde.org/utilities/konsole/-/raw/v23.08.5/src/Part.h)).
  A custom editor command such as `relay-open PATH LINE` could forward text files to Relay over IPC,
  but directories would still escape to KIO.
- **OSC 8:** `ls --hyperlink` (coreutils 8.28+) emits `file://host/path`. Konsole has supported OSC 8 since
  July 2020, off by default ([OSC8-Adoption](https://github.com/Alhadis/OSC8-Adoption)), behind `AllowEscapedLinks`
  plus `EscapedLinksSchema` (Profile.h). A click still goes through Konsole, not the host.
- **Shell integration:** Relay's `shell/integration.bash` already reports `$PWD` and prompt/exec events, so Relay
  knows the cwd and could resolve paths that appear in agent output, the composer, or a `relay open <path>` command,
  all without reading the screen.
- **Owning the emulator:** Qt Creator 11+ ships a cross-platform terminal built on **libvterm + ptyqt/ConPTY**
  ([libvterm attribution](https://doc.qt.io/qtcreator/qtcreator-attribution-libvterm.html),
  [ConPTY attribution](https://doc.qt.io/qtcreator/qtcreator-attribution-ptyqt-conpty.html)).
  **libghostty-vt** has a C API covering VT parsing and terminal/render state, but is at an early reference stage
  ([heise, 2026-03](https://www.heise.de/en/news/Ghostling-makes-terminal-emulation-a-C-library-11222728.html),
  [announcement](https://mitchellh.com/writing/libghostty-is-coming)).
  **qtermwidget** supports Linux, BSD and macOS but not Windows ([repo](https://github.com/lxqt/qtermwidget)).
  **ConPTY** (Windows 10 fall 2018 release) hands the host a UTF-8/VT stream ([MS blog](https://devblogs.microsoft.com/commandline/windows-command-line-introducing-the-windows-pseudo-console-conpty/)).
  If Relay owns the screen model, it gets OSC 8, alt-screen state, and its own path hotspots with click routing
  on every OS. With KonsolePart, Linux only, and clicks are routed by Konsole.

### Options

| Approach | Linux now | macOS/Windows later | Effort | Notes |
|---|---|---|---|---|
| 1. Embed KParts (dolphinpart, okularpart, katepart, gvpart) | Works today (installed) | Poor: Dolphin/Gwenview unsupported on macOS, Dolphin experimental on Windows | Low now, high later | Richest viewers; KIO/KF runtime baggage; GPL fine |
| 2. Plain Qt panes: QFileSystemModel tree + preview stack (QPlainTextEdit+KSyntaxHighlighting, QTextBrowser markdown, QImageReader, QPdfView) | Works (needs `libqt5pdf5`/Qt6 pdf) | Good: all pieces are portable Qt/MIT | Medium | One code path; PDF/media optional modules; no editing |
| 3. Hybrid: option 2 interface, with KTextEditor (LGPL) as the text viewer where available | Works | KTextEditor ships with Kate on Win/mac, but only as nightlies on mac | Medium+ | Nice editing, but more bundling |
| 4. Path detection via KonsolePart `TextEditorCmd` → `relay-open` IPC | Text files only; dirs/images go to KIO | Tied to Konsole | Low | Quick win, incomplete |
| 5. Path detection via Relay-owned sources (shell cwd + agent output + composer + `relay open`) | Works | Portable | Low-Med | No terminal-screen clicks |
| 6. Own the emulator (libvterm/ConPTY like Qt Creator, or libghostty-vt later) | Replaces KonsolePart | The real cross-platform path | High | Full OSC 8, hotspots, alt-screen, echo state |

### Recommended path
1. Build **option 2** now behind a small `PreviewProvider` interface (directory, text/code, markdown, image,
   PDF; binary shows file info). Use QtPdf and QtMultimedia as optional CMake components.
2. For click sources, start with **option 5**: paths in agent output and the composer, plus `relay open`.
   Add **option 4** as a best-effort terminal hook for text files. Do not rely on it for directories.
3. For "needs the human", keep `tpgid` and add Warp's termios rule (`!ECHO && ICANON`, polled only while a
   foreground job runs). Use it to pause the agent and focus the terminal, and never route composer text into
   that prompt.
4. Treat **option 6** as the macOS/Windows milestone. It also gives Relay alt-screen detection and OSC 8, which
   KonsolePart cannot expose.
