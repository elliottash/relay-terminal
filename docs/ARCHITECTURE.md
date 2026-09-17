# Relay architecture

Current as of 2026-09-17 (commit `10819d9`). Every
statement points at code; paths are relative to the repository root. Planned work is in
[ROADMAP.md](ROADMAP.md), test status in [VALIDATION.md](VALIDATION.md).

Contents:

1. [Overview](#1-overview)
2. [Process model](#2-process-model)
3. [Windows, tabs and panes](#3-windows-tabs-and-panes)
4. [Keyboard: Keymap, presets, palette](#4-keyboard-keymap-presets-palette)
5. [Composer and routing](#5-composer-and-routing)
6. [Shell bridge: staging commands safely](#6-shell-bridge-staging-commands-safely)
7. [Terminal-mode fix loop](#7-terminal-mode-fix-loop)
8. [Inline agent output](#8-inline-agent-output)
9. [Human and agent control](#9-human-and-agent-control)
10. [File panes and `relay open`](#10-file-panes-and-relay-open)
11. [Agent backend](#11-agent-backend)
12. [Keys, keyring and Warp import](#12-keys-keyring-and-warp-import)
13. [Per-pane isolation](#13-per-pane-isolation)
14. [Theme](#14-theme)
15. [Packaging layout](#15-packaging-layout)
16. [Engine spike and `TerminalBackend`](#16-engine-spike-and-terminalbackend)
17. [Fragile dependencies and limits](#17-fragile-dependencies-and-limits)
18. [Source map](#18-source-map)

## 1. Overview

Relay is a native C++/Qt application for Linux. It embeds **KonsolePart**, Konsole's
terminal component, loaded at runtime as a KPart. Under each terminal sits a real text
editor (the composer). Text typed there goes to the shell or to a bring-your-own-key agent.
The agent runs in a separate Python process per pane and talks to any OpenAI-compatible
chat-completions endpoint.

It builds against Qt6 + KF6 or Qt5 + KF5 (`CMakeLists.txt`, `RELAY_QT_MAJOR=AUTO|6|5`).
It is not a Konsole fork and does not patch Konsole.

## 2. Process model

```text
relay  (GUI process: Qt, all KonsolePart instances, all screens and scrollback)
 |
 |-- WindowManager: private QLocalServer  $TMPDIR/relay-open-XXXXXX/open.sock   <-- scripts/relay-open
 |
 +-- per terminal pane (Pane) -------------------------------------------------------------
 |    runtime dir  $TMPDIR/relay-XXXXXX (0700): state.json, input.txt (0600)
 |
 |    systemd scope relay-pane-<id8>-shell-<n>          systemd scope relay-pane-<id8>-agent-<n>
 |      bash --noprofile --rcfile shell/integration.bash -i    python3 -S -u backend/worker.py
 |        |-- shell/event.py  (writes state.json)                |-- bash -n  (router syntax check)
 |        +-- user commands, vim, builds ...                     +-- bash --norc -c  (run_command)
 |               ^                                                      ^
 |               | PTY (KonsolePart startProgram / sendInput)           | NDJSON over stdin/stdout
 +---------------+------------------------------------------------------+
```

| Process | Started by | Talks to the GUI through |
|---|---|---|
| `relay` | user or desktop file | n/a |
| Pane shell (Bash) | KonsolePart `startProgram`, wrapped in `systemd-run --user --scope` when available | PTY bytes; atomic `state.json` events; `input.txt` for staged commands |
| `shell/event.py` | Bash prompt and DEBUG hooks | writes `state.json` (token, sequence, event, status, cwd, shell PID, aliases/functions, PATH) |
| Agent worker `backend/worker.py` | `QProcess`, wrapped in `systemd-run` when available | newline-delimited JSON on private stdin/stdout. No TCP port. |
| Agent tool commands | worker, `/bin/bash --noprofile --norc -c` in a new session | results go back through the worker |
| `scripts/relay-open` | `relay open` in a pane shell, or Konsole's file-link editor command | Unix socket `RELAY_OPEN_SOCKET`, one JSON line |

Startup (`main()` in `src/main.cpp`):

1. `relay::theme::exposeKonsoleProfile()` prepends `data/theme` to `XDG_CONFIG_DIRS` and
   `XDG_DATA_DIRS`, before `QApplication` exists.
2. The data root is the first of `$RELAY_DATA_DIR`, `<exe>/../share/relay`, the compiled
   `RELAY_DATA_DIR`, or the source tree that contains `backend/worker.py`.
3. `<data>/scripts` is prepended to `PATH`; `RELAY_OPEN_HELPER` points at `relay-open`.
4. `WindowManager` opens the socket, then `newWindowAt(--workspace)` creates the first window.

Options: `--workspace/-w PATH` (initial terminal directory and agent workspace) and
`--clean-shell` (skip `~/.bashrc`).

## 3. Windows, tabs and panes

| Class | Role |
|---|---|
| `WindowManager` | Window list, a stack of up to 25 closed items (pane, tab or window), the `relay open` socket |
| `RelayWindow` | `QMainWindow`: toolbar (Actions, New chat, Stop agent, Provider / BYOK…), a `QTabWidget`, the actions palette overlay, an application event filter for shortcuts |
| Tab page | One root widget: a leaf or a tree of `QSplitter`s |
| `Pane` (leaf) | Terminal pane: KonsolePart, Bash bridge, composer, its own worker and conversation |
| `ToolPane` (leaf) | Folder explorer or file preview (section 10) |

Pane anatomy, top to bottom: directory line (click opens the explorer), an optional
banner (memory kill, restart), the terminal, the transcript panel (section 8), the composer
frame (route label, input-mode picker, model picker, interrupt-shell button, Submit, editor,
key hints). Overlays float over the terminal without resizing it (a resize makes the idle shell
redraw its prompt in the middle of inline output): the agent queue strip, the thinking panel,
toasts and the pane button row.

Layout rules:

- Splitting reuses the anchor's splitter if the orientation matches, otherwise wraps the
  anchor in a new splitter. Closing a leaf collapses a splitter left with one child.
- Focus movement is geometric: the nearest leaf on the requested side, then the best aligned.
- The focused leaf gets the `relayActive` property (accent outline). Agent and terminal
  actions use the last focused terminal `Pane` in that tab, even when a tool pane has focus.
- Closed items are stored as JSON layout nodes:
  `{"pane":{"cwd","workspace"}}`, `{"explorer":{"path"}}`, `{"preview":{"path"}}`,
  `{"split":"h"|"v","sizes":[…],"children":[…]}`. Restoring starts **new shells** in the saved
  directories (`RELAY_START_DIR`, applied by `shell/integration.bash` after `.bashrc`).
  Scrollback and running programs are not restored.
- Closing a window asks for confirmation when it has more than one pane or anything is busy.
- **Pane button row** (`PaneChrome`, a child of each leaf created in `syncChrome()`): shown for
  the leaf under the mouse (application event filter, Enter/MouseMove). Buttons run the same
  actions as the keys (`pane.splitRight`, `pane.splitDown`, `pane.moveToNewTab`, `pane.close`).
  `PaneChrome` has no `Q_OBJECT`, so it is found with `dynamic_cast` (`chromeOf`), never
  `findChild<PaneChrome*>` (that matches any `QFrame`, such as the transcript panel).
- **Moving without destroying.** `takeLeaf()` detaches a leaf (collapsing a one-child splitter,
  removing an emptied tab, closing an emptied window) and leaves it parentless;
  `insertBeside()` / `adoptLeafAsTab()` / `adoptPage()` put it back. Shells, workers and
  conversations keep running. Pane callbacks resolve their window at call time
  (`windowOf(pane)`), so nothing has to be rebound when a pane or tab changes window.
- **Keyboard moves** (`pane.moveLeft/Right/Up/Down`, default Ctrl+Alt+arrows, unbound in the
  Warp preset where those keys focus panes): the neighbor is found like focus movement; adjacent
  siblings in a splitter of that orientation swap, otherwise the pane docks on the neighbor's
  near side, so repeating keeps moving it.
- **Drag:** the grip tracks the mouse itself (no `QDrag`, because KonsolePart accepts text
  drops). `dropTarget()` picks the nearest edge of the leaf under the cursor or a `QTabBar`; a
  translucent `dropZone` frame shows the half that will be taken. Esc cancels.
- **Tabs:** a "+" button placed after the last tab, a ⧉ left-side tab button shown on hover and a
  context menu run `tab.new` / `tab.moveToNewWindow`; the tab page moves to
  `WindowManager::newEmptyWindow()`.
- Typing `exit` closes the pane. A shell stopped for memory keeps the pane open (section 13).

## 4. Keyboard: Keymap, presets, palette

### Shortcut hints

`src/Hints.*` (`relay::ShortcutHints`) decides whether a hint may show: on by default
(`hints/enabled`, toggles in Agent options and the Shortcuts section), at most `limit` (3) times
per id, a per-id cooldown (600 s) and a global gap of 20 s, counts in QSettings `hints/`.
`Pane::hint()` and `RelayWindow::hint()` show a 5 s toast; `nextTime(shortcut, what)` builds the
text from the live Keymap, so rebinding changes the hint and unbound actions get none. Current
triggers: toolbar and palette activations of actions with shortcuts, pane buttons, the tab "+",
tab close and ⧉ buttons, clicking into another pane, mouse model/effort/mode pickers, clicking
the directory line (`@`), the queue ×, `/shell ` and `/agent ` (`!`, `*`), palette rewinds, pane
drags, the first `relay://` link, the Tasks chip and `/tasks`, `/requests`, `/todos` (→ `agent.requests`, Ctrl+Shift+K), Continue
from the link or palette (→ `/continue` or `agent.continue`), and rotating idle tips 4 s after a finished agent turn with an
empty prompt box. **Every new feature with a shortcut should add a hint on its slow path** (rule
in `WARP.md`); tests in `tests/hints_test.cpp`.

Palette items also match hidden alias words (`paletteAliases()`, keyed by label/key/section
substrings, half weight), e.g. "undo" → Rewind, "reasoning" → effort, "detach" → move actions.

`Keymap` (`src/main.cpp`) is a process-wide registry of named actions. Each action has an id,
a category, a description and default keys.

| Source, lowest to highest priority | Where |
|---|---|
| Relay defaults | `Keymap::Keymap()` `add(...)` calls |
| Preset table (`relay`, `warp`, `vscode`, `konsole`) | `Keymap::presetJson()`, researched in [KEYBINDING-PRESETS.md](KEYBINDING-PRESETS.md). Actions missing from a table keep the Relay default. |
| User overrides | `~/.config/RelayTerminal/relay/keybindings.json` `bindings` |

The file also holds `preset` and `program_keys` (`shift-only` default, `all`, `none`).
A `QFileSystemWatcher` watches the file and its directory, because atomic replacement
drops a plain file watch. Unknown keys and conflicts are reported in the status bar.
Symbol keys match with or without Shift, because shifted punctuation differs by layout.

Dispatch: `RelayWindow::eventFilter` handles `ShortcutOverride` and `KeyPress` for widgets in
its window and runs `runAction(id)`, which the toolbar and palette also use.

- `control.human` (Ctrl+H), `input.toggle` (Ctrl+I) and `agent.interrupt` (Ctrl+Alt+Enter)
  act only from the composer. In the terminal those keys stay Backspace, Tab and Enter.
- While a foreground program owns the focused terminal, only keys allowed by
  `program_keys` act. The default lets Ctrl+Shift combinations and F-keys through to Relay.
- Terminal clipboard: Ctrl+C invokes the display's `copyToClipboard` slot and treats a
  clipboard change as proof of a selection (KonsolePart has no selection query); otherwise
  the key reaches the shell. Ctrl+V pastes at a prompt and passes through inside programs.
  Optional copy-on-select (`terminal/copy_on_select`).

Default window shortcuts:

| Action | Key | Action | Key |
|---|---|---|---|
| New window | Ctrl+N | Close pane → tab → window | Ctrl+W |
| Next / previous window | Alt+Tab / Alt+Shift+Tab | Restore closed | Ctrl+Shift+W |
| New tab | Ctrl+T | Actions palette | Ctrl+Shift+A |
| Next / previous tab | Ctrl+Tab / Ctrl+Shift+Tab | Take control (from composer) | Ctrl+H |
| Split right / down | Ctrl+P / Ctrl+Shift+P | Back to the prompt | Ctrl+Shift+H |
| Focus neighbor pane | Alt+Arrows | Native input toggle | F12 |
| Toggle terminal/agent input | Ctrl+I | Restart stopped shell/agent | Ctrl+Shift+R |
| Interrupt agent with prompt | Ctrl+Alt+Enter | | |

Unbound by default: `files.explorer`, `files.open`, `terminal.interrupt`, `agent.newChat`,
`agent.stop`, `agent.clearQueue`, `agent.resumeQueue`, `agent.provider`, `input.mode*`,
`keybindings.edit`, `keybindings.reload`.

**Actions palette** (Ctrl+Shift+A). One overlay child of the central widget, so opening it
never resizes a terminal. Sections: Recent (up to 4, from `palette/recent`), then Agent and
Terminal (ordered by where focus was), Panes and tabs, Shortcuts. Items have a stable key and
either a run function or a submenu (Model, Input mode, Control when a program starts,
Shortcut preset, Shortcuts inside programs). Typing searches everything, including submenu
entries. Toggles stay open and re-render. Closing returns focus to the widget that had it.

**Agent-editable shortcuts.** Each worker receives the action catalog at configure time and
after every reload. The `set_keybinding` tool (`backend/relay_core/keybindings.py`) validates
the action id and key strings, then rewrites only that binding atomically. The watcher reloads it.

## 5. Composer and routing

`RichEditor` (`src/RichEditor.cpp`) is a `QPlainTextEdit`: native mouse and keyboard selection,
undo, multiline, basic shell coloring, draft-preserving history (Up on the first line, Down on
the last), an IME guard (Enter during preedit never submits) and a 128 KiB paste cap.
Pasting never submits.

| Key in the composer | Destination sent to the router |
|---|---|
| Enter | selected input mode (`auto`, `shell`, `agent`) |
| Ctrl+Enter | `agent` |
| Ctrl+Shift+Enter | `shell` (terminal mode) |
| Ctrl+Alt+Enter | agent, `when: "interrupt"` (section 11) |
| Shift+Enter | newline |
| Esc | native terminal input |
| PageUp / PageDown | scroll the terminal scrollback one page |

Text changes trigger a debounced (150 ms) preview route; the route label shows the decision.

**Prefixes.** `!` or `*` typed (not pasted) as the first character of an empty editor is consumed
and switches the input mode to terminal or agent for one submission (`setPrefixMode`, chip
`prefixChip`); Backspace on the empty editor restores the previous mode. `/shell ` and `/agent `
still work and trigger a shortcut hint.

**Routing assist (protocol 11).** When a preview `route` has `needs_assist` and the mode is auto,
the label shows the local guess, then "AUTO · checking…" after 150 ms; 300 ms after typing stops
the pane sends `route_assist {id: "assist-N", text, cwd, timeout_ms: 4000}` for the current text
only. `route_assisted` updates the label ("AGENT · guessed: reason (82%)") and is cached per
text. On submit, a cached answer replaces the route; otherwise the decision is held for at most
400 ms (`m_assistHold`) and then dispatched with the local guess. A failed or timed-out assist
leaves the local guess ("model check unavailable"). Prefixes, Ctrl+Enter and Ctrl+Shift+Enter
send a non-auto mode and never ask.
Submission sends `route` to the worker with the text, mode, live alias/function names, the
shell's `PATH` and cwd. The worker's `router.classify` (`backend/relay_core/router.py`) never
executes input:

1. Control characters (other than newline and tab) are rejected. Limit 128 KiB.
2. A leading `/shell ` or `/agent ` forces a destination.
3. Agent mode returns `agent`. Terminal mode returns `shell` plus a validity check.
4. Auto mode: text matching the natural-language pattern (`why`, `how`, `please`,
   `explain`, `find the`, …) goes to the agent, unless the first word is a live alias or
   function and the text is runnable.
5. Otherwise `check_runnable`: `bash --noprofile --norc -n` in a clean environment (2 s
   timeout), then every command word in pipelines, lists, subshells and command substitutions
   must resolve to a builtin, keyword, live alias/function, `PATH` executable, or executable
   path relative to the terminal cwd. Heredocs, `case`, arithmetic and arrays fall back to
   checking the first word only.
6. Runnable text returns `shell`. Anything else returns `agent` with `invalid_reason`
   (for example `command not found: foo`), which the GUI prints as the reason.

The router no longer produces `ambiguous`. `Pane::dispatch` still treats a legacy
`ambiguous` decision like `agent`.

GUI dispatch (`Pane::dispatch`):

| Decision and mode | Action |
|---|---|
| `shell`, a program owns the terminal, not terminal mode | send to the agent with a note |
| `shell`, terminal mode, invalid | start the fix loop (section 7) without running |
| `shell`, terminal mode, valid | run in the terminal and watch the exit status |
| `shell`, auto, invalid | send to the agent with the reason |
| `shell`, auto, valid | run in the terminal |
| `agent` | `submitAgent` |

Routing is a convenience, not a security classifier. Natural language can be valid Bash.

## 6. Shell bridge: staging commands safely

`shell/integration.bash` is passed as `--rcfile`; user dotfiles are never edited. It sources
`~/.bashrc` (unless `--clean-shell`), raises its own `oom_score_adj` to 300, changes to
`RELAY_START_DIR`, defines `relay open`, and installs hooks:

- `PROMPT_COMMAND` becomes `(__relay_prompt_begin, <user entries>, __relay_prompt_end)`,
  keeping scalar or array forms and the original exit status. `prompt_end` emits `ready` with
  alias and function names.
- A DEBUG trap emits `running` for the first command after a prompt. If a DEBUG trap already
  exists, the script emits `unsupported` and stops; Relay falls back to native input.
- `bind -x` in emacs, vi-insert and vi-move keymaps: Ctrl+X Ctrl+R runs `__relay_load`,
  Ctrl+X Ctrl+P runs the no-op `__relay_redraw`.

The GUI polls `state.json` every 80 ms and accepts only events with its session token and a
new sequence value. If no event arrives within 5 s, the pane switches to native input.

Sending a command (`Pane::runInTerminal`):

1. Readiness: a `ready` event was seen, nothing is loading, and `/proc/<shell>/fd/0` is in
   noncanonical mode (Readline active; `PROMPT_COMMAND` runs before that), and the shell owns
   the foreground group (`/proc/<shell>/stat` field `tpgid`; `tcgetpgrp()` fails with
   `ENOTTY` because the PTY is not Relay's controlling terminal).
2. The UTF-8 text is written atomically to `input.txt` (0600).
3. Only Ctrl+X Ctrl+R is sent to the PTY. `__relay_load` reads the file into `READLINE_LINE`
   and emits `loaded` with the file's SHA-256.
4. Enter is sent only if the hash matches. No acknowledgement within 2.5 s means no Enter:
   the pane switches to native input and says so.

The event file protects against accidental cross-session events and output spoofing, not
against hostile processes running as the same user.

## 7. Terminal-mode fix loop

Applies only to terminal mode (Ctrl+Shift+Enter, `/shell `, or the Terminal picker).

- An invalid command is not run. The agent is asked to fix it (attempt 1).
- A valid command runs. At the next `ready` event: exit 0 ends the loop, exit 130 (Ctrl+C)
  ends it silently, any other status starts a fix turn.
- The fix prompt carries the command, terminal cwd and problem, and tells the agent it cannot
  see terminal output. The reply must end with a fenced `relay-run` block.
- Fix prompts are submitted through the agent queue, so a busy agent queues them.
- When the fix turn ends with `done`, Relay takes the last `relay-run` block and stages it
  through section 6, 150 ms later. At most 3 attempts (`kMaxFixAttempts`).
- Auto-mode commands are never auto-fixed.

## 8. Inline agent output

There is no agent pane. KonsolePart has no API to write to the display, but each Konsole
`Session` registers on D-Bus at `/Sessions/N`. In-process,
`QDBusConnection::objectRegisteredAt()` returns that `QObject`. Relay finds the session whose
child reports the shell PID through `processId()`, then invokes its
`onReceiveBlock(const char*, int)` slot. Bytes go to the emulator like program output. They
never reach the shell, its history or its input.

`Pane::printInline`:

- Text is sanitized: C0 and C1 controls other than newline and tab are dropped.
- On the first block, the idle prompt line is erased (`\r\x1b[2K`). Each kind of text has its
  own 24-bit color (`Ink`: user prompt, agent text, tool line, tool output, diff add/remove,
  error, note).
- `closeInline` sends Ctrl+X Ctrl+P, so Readline redraws the prompt. While more queued turns
  are pending, the prompt is not redrawn between turns.
- `tool_started` previews are compacted: `⚙ $ command`, `⚙ read path`, `⚙ list path`,
  `⚙ write path` plus a colored diff. `tool_result` prints `exit N`, `✓ tool` or `✗ error`.
- If the D-Bus session is not found, output goes to stderr.

**While a program owns the terminal** (vim, a build, a REPL), printing would corrupt its
screen. Output is buffered, and also shown live in the **transcript panel** above the composer
(`Pane::appendTranscript`): header "Agent · model — output will also print in the terminal
when <program> exits", at most about 40% of the pane height, × hides it until the program
exits. On the next `ready` event the buffer prints into the terminal and the panel resets.

**Thinking and turn summaries (protocol 11).** `thinking_delta` text streams into a floating
`thinkingOverlay` over the bottom of the terminal (not the transcript panel: that is in the
layout, and resizing the terminal makes Readline redraw its prompt mid-output) when
`agent/show_thinking` is on (default). `thinking_done` hides it and prints one Note line,
`✦ thought for N s` (skipped for `chars: 0`). `turn_summary` (sent just before `done`) is stored
per pane (last 50) and, when the turn used tools, prints `✦ N tool calls · T s` wrapped in an
OSC 8 hyperlink to `relay://turn/<pane token>/<turn id>`. The live `tool_output {text}` stream and
the stored reply `tool_output {stored: true, …}` share a name; the GUI branches on `stored`.

**`relay://` links.** Konsole 23.08 opens OSC 8 links only when the profile has
`AllowEscapedLinks=true` and the scheme is in `EscapedLinksSchema` (`data/theme/konsole/Relay.profile`),
and KonsolePart applies its profile before the view exists, which leaves the URL extractor off;
`Pane` re-applies the profile (`TerminalInterfaceV2::setCurrentProfile`) after starting the shell.
Clicks go through `KIO::OpenUrlJob`, so `registerUrlHandler()` (1.5 s after start, idempotent,
`RELAY_NO_URL_HANDLER=1` skips it) writes
`$XDG_DATA_HOME/applications/org.relayterminal.Relay.url-handler.desktop` (`Exec=python3
relay-open %u`, template in `data/`), runs `xdg-mime default … x-scheme-handler/relay`,
`update-desktop-database` and `kbuildsycoca5`, and says so once in the status bar. `relay-open`
forwards `relay://` URLs as `{url}` over the open socket; a helper launched by the desktop has no
`RELAY_OPEN_SOCKET`, so `WindowManager` also writes the address to
`$XDG_RUNTIME_DIR/relay/open-socket` (0600). `handleOpen` finds the pane by token and
`openTurnPane()` inserts a `ToolPane(TurnTranscriptView)` (`src/TurnTranscript.*`): tool rows
from the summary, the transcript from `turn_transcript_get`; Enter on a row sends
`tool_output_get` and the result is written to a 0600 temp file (`.diff` or `.log`) and opened in
a preview pane.

**Rewind.** `/rewind` and Esc Esc open *Rewind chat* (`rewind {restore: "conversation"}`, files
never touched; "Fork from here"). `/rewind-code` (palette "Rewind code…") opens *Rewind code*
with "Rewind code…" (`files`) and "Code and chat…" (`both`); both first list the files changed by
that turn and later ones in a confirmation where Enter restores. Conflicts are only known after
the restore (the backend skips files changed since and reports them in `rewound`).

## 9. Human and agent control

| Situation | Behavior (`Pane::pollShell`, `takeControl`, `showPrompt`) |
|---|---|
| A command is still running 150 ms after `running` | Read the foreground program's basename from `/proc/<tpgid>/cmdline`. Policy `human` (default): hide the composer, focus the terminal. Policy `agent`: keep the composer and show "Agent in control of <name>". |
| Program exits (`ready`) | Automatic human control ends; the composer returns |
| Ctrl+H from the composer | Human control: composer hidden, keys go to the terminal |
| Ctrl+Shift+H | Composer back. While a program runs, submissions go to the agent |
| Typing directly into the terminal at a prompt | Switches to native input, so a later composer submission cannot overwrite Readline's line |
| F12 | Toggles native input |

Policy lives in QSettings: `control/default` (`human` or `agent`) and `control/programs`
(basename → `human` or `agent`), set from the palette.

**Password prompts.** Once a second while a command runs, `checkPasswordPrompt` opens
`/proc/<shell>/fd/0` and checks termios: `ICANON` on and `ECHO` off (full-screen programs
and Readline turn `ICANON` off, so they do not match). A match forces human control, shows
"Password prompt · you're in control", and, if the window is inactive, flashes the taskbar
and runs `notify-send`. Commands that finish after more than 30 s notify the same way.
Composer text never goes to a running program.

**Program context for the agent.** A prompt submitted while a program runs carries
`"context": {"foreground_program", "terminal_cwd"}`. The worker validates it
(`agent.validate_context`) and prepends a labelled note saying the agent cannot see or type
into that program and that `run_command` uses a separate shell.

The agent cannot type into running programs. That is planned
(`issues/features/2026-09-17-agent-delegate-and-take-over.md`).

## 10. File panes and `relay open`

`src/FilePanes.{h,cpp}` builds the static library `relay-filepanes`: plain Qt widgets with no
KDE requirement.

- `relay::FileExplorer`: one folder through `QFileSystemModel`; folders first, hidden-file
  toggle, type-to-filter. Enter or double-click opens (folders navigate, files call
  `onOpenFile`); Backspace or Alt+Up goes up.
- `relay::FilePreview::open(path)` picks a viewer by MIME type: text and code in a read-only
  `QPlainTextEdit` (KSyntaxHighlighting "Breeze Dark" when built in), Markdown rendered or
  source, images (fit or 100%), PDF when Qt PDF is built in, otherwise a file-info panel with
  Open externally. Text is capped at 2 MiB with a notice; images over 64 MiB are refused.
  `goToLine` scrolls and highlights.

Optional dependencies are detected at configure time (`RELAY_HAVE_SYNTAX_HIGHLIGHTING`,
`RELAY_HAVE_QTPDF`). The Qt6 `.deb` and AUR builds leave PDF off
(`packaging/deb/build-deb.sh`, `packaging/arch/*/PKGBUILD`).

`RelayWindow::openPath` reuses an existing explorer or preview in the tab. A new preview
opens beside an explorer if there is one, otherwise beside the anchor. Tool panes split,
close, restore and navigate like terminal panes.

Ways to open a path:

| Source | Path |
|---|---|
| Click the pane's directory line | `Pane::onOpenPath` → explorer |
| Palette: Open folder in explorer / Open file… | `files.explorer`, `files.open` |
| `relay open PATH` in a pane shell | shell function → `scripts/relay-open` → socket request `{path, line, token}`; the token selects the pane |
| Ctrl+click a text file in terminal output | Relay's Konsole profile sets `UnderlineFilesEnabled=true` and `TextEditorCmdCustom=relay-open PATH:LINE:COLUMN` |

KonsolePart sends folders, images and PDFs to KIO (the desktop default app), not to the
editor command, so those clicks do not reach Relay
(`issues/features/2026-09-17-clickable-paths.md`). `relay-open` falls back to `xdg-open` when
Relay is not reachable.

## 11. Agent backend

### Worker protocol

`backend/worker.py`: one JSON object per line, 2 MiB maximum per message. The GUI kills a
worker whose unread output exceeds 8 MiB and ignores its stderr. The worker raises its own
`oom_score_adj` to 500.

| Request `type` | Purpose |
|---|---|
| `route` | classify composer text (section 5) |
| `configure` | provider, key or `use_stored_key`, workspace, extras, `max_tokens`, `keybindings` catalog, optional `skills`. Refused while a turn runs. |
| `keybindings` | replace the action catalog, keeping the conversation |
| `presets` | list presets with `has_stored_key` and Warp's default preset |
| `store_key`, `import_warp` | keyring operations (section 12) |
| `ask` | `text`, `when` (`now`/`queue`/`interrupt`), optional `context` |
| `cancel`, `resume_queue`, `queue_remove`, `queue_clear` | queue control |
| `reset` | new conversation; refused while a turn runs |
| `shutdown` | exit |

Events: `ready`, `route`, `configured`, `presets`, `key_stored`, `warp_imported`,
`keybindings_updated`, `queued`, `queue_changed`, `interrupting`, `agent_started`,
`agent_finished`, `status`, `delta`, `usage`, `tool_started`, `tool_output`, `tool_result`,
`done`, `cancelled`, `error`, `reset`. Errors carry `agent_busy`.

### Provider transport

`backend/relay_core/provider.py`, standard library only.

- `POST <base_url>/chat/completions` with `stream: true`; JSON (non-stream) responses are also accepted.
- HTTPS required, except plain HTTP to `localhost`, `127.0.0.1` or `::1`. No credentials,
  query or fragment in the URL. Redirects are refused.
- Extra request keys are limited to `thinking`, `reasoning`, `reasoning_effort`,
  `temperature`, `top_p`. `max_tokens` 256–32768.
- Limits: 8 MiB request and response, 2 MiB per SSE event, 16 tool calls per response,
  30 s socket timeout. Cancel closes the response from another thread.
- Tool-call fragments are assembled by index. `reasoning_content` and OpenRouter's
  `reasoning` are kept in history for later tool turns, not displayed.
- A stream without `[DONE]` or a `stop`/`tool_calls` finish, or with `length` or
  `content_filter`, is an error; partial tool calls never run. HTTP error bodies are not echoed.

Presets (`backend/relay_core/presets.py`, mirrored by hand in the dialog in `src/main.cpp`):

| Id | Base URL | Model | Extras |
|---|---|---|---|
| `kimi` | `https://api.moonshot.ai/v1` | `kimi-k3` | `reasoning_effort: high` |
| `kimi-code` | `https://api.kimi.ai/coding/v1` | `k3` (also `k3-256k`, `kimi-for-coding` = K2.8 Preview, `kimi-for-coding-highspeed`) | `reasoning_effort: high`; key from the Kimi Code Console |
| `glm` | `https://api.z.ai/api/paas/v4` | `glm-5.3` | thinking enabled, `reasoning_effort: high` |
| `glm-coding` | `https://api.z.ai/api/coding/paas/v4` | `glm-5.3` | same |
| `openrouter` | `https://openrouter.ai/api/v1` | `deepseek/deepseek-v4.1-flash` | none |

The Provider / BYOK dialog also accepts custom base URL, model and extras, requires an
existing workspace and a consent checkbox for sending prompts and tool results to the
provider. Saving makes no network call. Switching model starts a new conversation.

### Agent loop

`backend/relay_core/agent.py`. One conversation per worker. The system prompt tells the model
that tools run without confirmation, that tool output is untrusted, and that `run_command` is a
separate non-interactive shell. Per turn: at most 50 model requests and 150 tool calls by default
(`max_steps`/`max_tool_calls`, configurable); hitting a limit ends the turn with
`done {stop_reason: "limit"}`, which does not pause the queue. On cancel or error the user's prompt
and delivered steers stay in history; only a half-finished tool-call group is completed with
"not completed" results, and a note says the request is unfinished and state must be reinspected.
Context accounting and compaction: `context.py` (protocol section 4 and 12.7).

**Requests and todos** (`requests.py`, `todos.py`; protocol section 12). Every prompt becomes a
ledger entry `R<n>` when it is submitted (queue, steer, interrupt, requeue, Relay-origin), saved in
the session and never summarized. The model keeps a todo list with `update_todos` (linked to
request ids); linked todos set request statuses. Steers are framed with their id and keep their
attachments. When the model stops with open todos for the turn, the worker re-prompts at most twice
(`completion_check`), then `done {open_items}`. After 8 steps without a todo update, a short
reminder is added. Compaction inserts a deterministic carried block after the summary (requests
verbatim, todos, plan, files, subagents, recent user messages up to ~20K tokens), and only
`relay_kind: "prompt"` messages count as turn starts. An optional audit side call
(`audit_requests`, route-assist model) only flags possibly unaddressed asks. Subagents have none of
this (no ledger or todos).

**Tasks UI** (request ledger UI: `src/RequestLedger.*`, `src/RequestsPanel.*`, library
`relay-requests`, tests `tests/requests_test.cpp`). `RequestLedgerModel` holds the latest
`requests`/`todos` lists (verbatim text from `request_get`, and settled todos the model later
dropped from its list) and derives **tasks**, all in the GUI (no protocol change): each todo is a
task; a user request with no linked todos (or open again while all its todos are settled) counts as
one task itself; Relay-origin requests do not count. Outcomes: todo `completed` → completed;
`blocked` → failed; `deferred` → deferred; `cancelled` → cancelled; request `done` → completed,
`cancelled`/`cancelled_by_user` → cancelled, `blocked` → failed, `deferred` → deferred. Open todos
and open requests are *active* while any request is `in_progress` or they wait in the queue (not
delivered, or re-asked/requeued: a new `queue_item` since last delivery), otherwise *unfinished*
(the turn ended by error, cancel, the step limit or an exhausted completion check); open todos of a
request the user marked done or cancelled follow it. **Batches:** a new request starts a new task
list when the current list has tasks, none active, no request in progress and it did not arrive in
the same turn; unfinished (and re-asked) tasks of earlier lists move into the current one. After a
restart or `/resume` the same walk runs over the loaded ledger; a ledger whose highest id went down
or whose ids changed text (new chat, other session, rewind) resets the batches. The `requestsChip`
shows `Tasks c/t` (property `state`: `running`, `done` green, `attention` amber with the suffix
"(1 failed, 1 deferred, 1 cancelled, 1 unfinished)" once settled), hosted in the queue strip header
while the strip is visible and in the composer row otherwise; `turnEndLine()` gives the `✦ Tasks …`
line printed on `done`/`cancelled`/`error` (skipped for a single completed task). `RequestsPanel`
floats over the right of the terminal: current requests (unfolded, with `c/t` and their todos),
"Other tasks", then a folded "Earlier · c/t (…)" row; d/x/o/r send `request_set` and
`request_reask`. Toggle: `agent.requests` (Ctrl+Shift+K in the Relay preset; unbound in the Warp,
VS Code and Konsole presets, where the key clears blocks, deletes a line, or clears scrollback),
`/tasks`, `/requests`, `/todos`, the chip. `done {stop_reason: "limit"}` prints a
`relay://continue/<pane>` link handled by
`WindowManager::handleOpen`; Continue sends an ordinary ask. `max_steps`, `max_tool_calls` and
`audit_requests` live in QSettings `agent/*`, go into `configure` and are sent with
`set_agent_options` when changed.

### Tools

`backend/relay_core/tools.py`. **There is no approval step.** Each call is validated and
prepared, `tool_started` carries a preview, and it executes immediately.

| Tool | Behavior |
|---|---|
| `run_command` | `/bin/bash --noprofile --norc -c` in the workspace (or a workspace-relative `cwd`), new session, stdin `/dev/null`. Timeout 1–120 s, default 30. Output streamed as `tool_output`, 32 KiB returned. The process group gets SIGTERM then SIGKILL. Environment scrubbed: names containing KEY/TOKEN/SECRET/PASSWORD/CREDENTIAL/COOKIE, `RELAY_*`, `BASH_ENV`, `ENV`, `PYTHONPATH`, `LD_PRELOAD`, `LD_LIBRARY_PATH`, `SSH_AUTH_SOCK`, `BASH_FUNC_*`. Sets `TERM=dumb`, `PAGER=cat`, `GIT_TERMINAL_PROMPT=0`. |
| `read_file` | UTF-8 regular file, 128 KiB, no NUL bytes |
| `list_directory` | at most 200 entries |
| `write_file` | parent must exist; unified diff in the preview; the file's SHA-256 is rechecked before an atomic replace that keeps its mode |
| `set_keybinding` | offered when the GUI sent a catalog (section 4) |
| `load_skill`, `read_skill_file` | offered when at least one skill is indexed |

File tools reject absolute paths, `..`, symlinks anywhere on the path, paths outside the
workspace, and `.ssh`, `.gnupg`, `.git`, `.env*`, `id_rsa`, `id_ed25519`, `*.pem`, `*.key`.
These checks reduce mistakes; they are not a sandbox. `run_command` has the user's full
filesystem and network permissions.

Remaining controls without approvals: the system prompt, file-tool guards, environment
scrubbing, timeouts, output caps, step and tool limits, the Stop agent action, and the inline
preview of every action. Stop does not undo completed actions.

### Skills

`backend/relay_core/skills.py`. At configure time the worker indexes
`<dir>/<name>/SKILL.md` files with `name`/`description` frontmatter. Default directory:
`~/.warp/skills`; `configure.skills` can set `enabled`, up to 8 absolute `dirs`, and
`project: true` for `<workspace>/.warp/skills`. The GUI does not send `skills` today, so the
default applies. A list of skill ids and descriptions (6 KiB cap) is appended to the system
prompt as lower-priority guidance. `load_skill` returns up to 64 KiB of `SKILL.md` plus the
folder's file list; `read_skill_file` reads a text file inside the folder. Symlinks, `..` and
binary files are refused. Skipped folders are reported in `configured.skills_skipped`.

GUI: `src/SkillsDialog.*` (non-modal, from `/skills`, the palette or Agent options) lists
`skills_list` items with a checkbox per skill (unchecked names go to QSettings `skills/exclude`,
sent in `configure.skills.exclude` for new sessions), "overridden" for `shadowed_by`. Refine sends
`refine_skills` and opens the first refined `SKILL.md` in an editable pane. Import sends
`import_skills_preview`, shows a modal review (checkbox per skill, files as children) and sends
`import_skills_confirm` with the checked names. Check for updates reads the repository URL from
the imported skill's `../.relay-import.json` and sends `skills_check_updates`. Dialog requests
use ids `skills-N` so worker `error` events route to the dialog's status line.

### Queue and interrupt

`backend/relay_core/queue.py` (`TurnSupervisor`) runs every turn on one dispatcher thread.

- `queue` appends; `now` is refused while busy; `interrupt` goes ahead of ordinary queued
  prompts (FIFO among interrupts) and stops the running turn.
- `cancel` stops the turn, drops waiting interrupts and **pauses** the queue. A failed turn also
  pauses it. `resume_queue` continues. `now` and `interrupt` still run while paused.
- At most 32 queued prompts. `configure` and `reset` clear the queue when idle.
- Each accepted prompt is recorded in the agent's request ledger before it is queued
  (`queued.ledger_id`); removing or clearing queued prompts marks them `cancelled_by_user`.

GUI (`Pane::submitAgent`, `rebuildQueueStrip`): every agent prompt is sent with `when: "queue"`
(or `now` while paused). The prompt is echoed when its turn starts (`agent_started`), not when
queued. Busy state follows `agent_started`/`agent_finished`. The queue strip floats over the
bottom of the terminal: running prompt, numbered queued prompts with ×, Clear, and
"PAUSED · Resume". The palette offers Clear agent queue and Resume agent queue when relevant.
Full protocol: [QUEUE-INTERRUPT.md](QUEUE-INTERRUPT.md).

## 12. Keys, keyring and Warp import

`backend/relay_core/keystore.py`.

- Lookup order for a preset: environment variable `RELAY_<PRESET>_API_KEY` (for example
  `RELAY_GLM_CODING_API_KEY`), then the Secret Service keyring through `secret-tool`
  (`service=org.relayterminal.Relay provider=<preset>`), which works with GNOME Keyring and
  KWallet's Secret Service provider.
- Keys go to `secret-tool` on stdin, never in argv, files, QSettings or logs.
- At startup each pane asks for `presets`. If any preset has a stored key, it configures one
  without the key crossing the GUI pipe (`use_stored_key`): the saved preset, else Warp's
  default agent model, else the first stored key. A "custom" configuration whose base URL
  matches a preset also uses that preset's stored key.
- A key typed into the dialog is sent over the private pipe and kept in worker memory. It is
  saved to the keyring only if "Save entered key to the desktop keyring" is ticked.
- **Warp import** reads `agents.custom_endpoints` from `~/.config/warp-terminal/settings.toml`
  (TOML 1.1 inline tables are normalized for Python's TOML 1.0 parser), reads keys from Warp's
  keyring entry (`service=dev.warp.Warp key=AiCustomEndpointKeys`), matches endpoints to presets
  by base URL, and stores each key under the Relay preset. It never returns key material.
- CLI: `scripts/relay-agent.py --import-warp`, `--list`, or an interactive session on the same
  backend.

Non-secret provider settings live in QSettings (`provider/preset`, `base`, `model`, `extra`,
`max_tokens`) in `~/.config/RelayTerminal/relay.conf`.

## 13. Per-pane isolation

`namespace isolation` in `src/main.cpp`, probed once with `systemd-run --user --scope -- true`.

| Unit | Properties (defaults) |
|---|---|
| `relay-pane-<token8>-shell-<n>.scope` | `MemoryMax=8G`, `MemoryHigh=6G`, `MemorySwapMax=2G`, `KillSignal=SIGHUP`, `TimeoutStopSec=5`, `OOMPolicy=continue` |
| `relay-pane-<token8>-agent-<n>.scope` | `MemoryMax=2G`, `MemorySwapMax=512M`, `TimeoutStopSec=5`, `OOMPolicy=stop` |

`--scope` execs in place, so the PIDs Relay tracks are Bash's and Python's own. Settings in
`relay.conf` `[isolation]`: `enabled`, `shell_memory_max`, `shell_memory_high`, `shell_swap_max`,
`shell_oom_policy` (`continue` or `stop`), `agent_memory_max`, `agent_swap_max`. Invalid sizes
fall back to defaults. Without a systemd user manager, panes start unisolated and the status
bar says so once.

Detection, once a second: an increase in the shell scope's `memory.events` `oom_kill` shows
"A command in this pane was stopped because it ran out of memory"; a dead shell PID or
`Result=oom-kill` shows a banner with Restart shell (Ctrl+Shift+R), which replaces the
KonsolePart in the same pane. A stopped worker shows Restart agent. Failed scopes are
`reset-failed`. Scrollback is capped at 20,000 lines by the profile.

## 14. Theme

`src/Theme.{h,cpp}`: Fusion style, a dark `QPalette`, and one stylesheet built from color
tokens. `polishWindow()` tags unnamed widgets.

The terminal uses `data/theme/konsole/Relay.profile` and `RelayDark.colorscheme`. KonsolePart
has no API to select a profile, so Relay prepends `data/theme` to `XDG_CONFIG_DIRS` and
`XDG_DATA_DIRS` before `QApplication` starts; `relayrc` sets `DefaultProfile=Relay.profile`.
Both variables are restored before each shell starts, so user programs see their original
paths. Nothing is written to `~/.config` or `~/.local/share/konsole`.

## 15. Packaging layout

Installed tree (`CMakeLists.txt` `install()`):

| Path | Content |
|---|---|
| `bin/relay` | the application |
| `share/relay/backend/`, `share/relay/shell/` | worker, `relay_core`, Bash integration |
| `share/relay/scripts/` | `relay-open`, `relay-agent.py` |
| `share/relay/theme/` | `relayrc`, Konsole profile and color scheme, icons used by the stylesheet |
| `share/applications/org.relayterminal.Relay.desktop` | desktop entry |
| `share/metainfo/org.relayterminal.Relay.metainfo.xml` | AppStream metadata |
| `share/icons/hicolor/…` | PNG and SVG icons |
| `share/doc/relay/` | `README.md`, `copyright` |

| Piece | File |
|---|---|
| `.deb` (CPack) | `packaging/cpack.cmake`; runtime deps `konsole-kpart` pinned below or above 4:24.02 to match KF5 or KF6, `python3 (>= 3.10)`, `bash`; recommends `libsecret-tools`, `xdg-utils` |
| Per-distro build in a container | `packaging/deb/build-deb.sh` (Ubuntu 24.04 Qt5; Debian 13, Ubuntu 25.10/26.04 Qt6) |
| Install + smoke test | `packaging/deb/smoke-test.sh`, `packaging/smoke-installed.sh` (installed files, `--version`, worker `ready`, KonsolePart plugin, GUI start under Xvfb offscreen and xcb) |
| Local matrix | `packaging/deb/docker-build-all.sh` |
| Arch | `packaging/arch/relay-terminal/PKGBUILD` (release tarball), `relay-terminal-git` |
| CI | `.github/workflows/ci.yml`: Ubuntu 24.04 Qt5 build, ctest, install layout, desktop/AppStream validation; Debian 13 Qt6 build, tests and `.deb` |
| Release | `.github/workflows/release.yml` on `v*` tags: source tarball, 6 `.deb` jobs (3 distros × amd64/arm64) with smoke tests, `SHA256SUMS`, GitHub pre-release; AUR job present but disabled |
| Website | `site/` static page; `.github/workflows/pages.yml` runs only when `RELAY_PAGES_ENABLED=true` |

Version: `project(Relay VERSION …)` in `CMakeLists.txt` is passed to the app as `RELAY_VERSION`;
the worker reports `relay_core.__version__`, which `tests/test_version.py` checks against CMake.
Procedure: [RELEASING.md](RELEASING.md).

## 16. Engine spike and `TerminalBackend`

`engine/` is built only with `-DRELAY_BUILD_ENGINE_SPIKE=ON` (default OFF) and is **not
used by the app**.

- `engine/Pty.h`, `PtyUnix.cpp`: `relay::Pty` over `forkpty`, non-blocking reads on the GUI
  thread; the child resets signal dispositions and mask.
- `engine/VTermWidget.{h,cpp}`: libvterm screen painted with `QPainter`, scrollback ring,
  selection, bracketed paste, mouse reporting, OSC 8 links, Ctrl+click path detection.
- `engine/TerminalBackend.h`: the engine-neutral interface. `VTermWidget` implements it.
  Process (`startProgram`, `sendInput`, `sendText`, `shellPid`, `foregroundProcessId`),
  introspection (`capabilities`, `screenText`, `scrollbackText`, `altScreen`, rows/columns),
  geometry and focus, callbacks (`onLinkActivated`, `onPathActivated`, `onTitleChanged`,
  `onFinished`). Capability flags: `ScreenText`, `Scrollback`, `AltScreenState`, `LinkClicks`,
  `Osc8Links`.
- `engine/main.cpp`: `relay-vterm-spike` test harness with a debug dump and `--bench`.

No KonsolePart adapter exists yet; `src/main.cpp` still calls KonsolePart's `TerminalInterface`
and the Session D-Bus object directly. Results and gaps: [ENGINE-SPIKE.md](ENGINE-SPIKE.md).
Plan: [ROADMAP.md](ROADMAP.md).

## 17. Fragile dependencies and limits

Relay relies on KonsolePart internals that are not a public API. All were exercised on
Konsole 23.08 / KF5 only:

| Dependency | Used for |
|---|---|
| `QDBusConnection::objectRegisteredAt("/Sessions/N")`, a child with `processId()`, slot `onReceiveBlock(const char*, int)` | inline agent output |
| Display slots `copyToClipboard()`, `pasteFromClipboard()` | terminal Ctrl+C / Ctrl+V |
| The terminal's hidden vertical `QScrollBar` | PageUp/PageDown from the composer |
| Profile keys `TextEditorCmdCustom`, `UnderlineFilesEnabled`, `HistoryMode` via `XDG_*` paths | Ctrl+click text files, scrollback cap, theme |

Other limits:

- Linux only: `/proc/<pid>/fd/0`, `/proc/<pid>/stat`, `/proc/<pid>/cmdline`, cgroup files,
  `systemd-run`. CMake refuses to build the app on other systems.
- Rich integration is Bash only. Zsh, Fish, SSH and tmux sessions work through native input.
- KonsolePart exposes no screen text, alternate-screen state or click signal, so the agent
  cannot read the terminal and folder/image clicks go to the desktop.
- The GUI process holds every pane's screen and scrollback; a KonsolePart crash takes down
  all panes.

## 18. Source map

| Path | Content |
|---|---|
| `src/main.cpp` | `Keymap`, `isolation`, `Pane`, `ToolPane`, `WindowManager`, `RelayWindow`, palette, `main()` |
| `src/RichEditor.*` | composer editor |
| `src/FilePanes.*` | explorer and preview widgets |
| `src/Theme.*` | palette, stylesheet, Konsole profile exposure |
| `src/Hints.*` | shortcut hint limits and idle tips |
| `src/TurnTranscript.*` | turn details pane (tool calls, transcript) |
| `src/SkillsDialog.*` | skills list, exclude, refine, import, updates |
| `src/AgentUi.*` | pickers and instructions dialog |
| `shell/integration.bash`, `shell/event.py` | Bash bridge |
| `backend/worker.py` | worker protocol loop |
| `backend/relay_core/` | `router`, `provider`, `presets`, `agent`, `tools`, `queue`, `requests` (ledger, audit), `todos`, `context` (compaction), `keystore`, `keybindings`, `skills` |
| `scripts/` | `build.sh`, `test.sh`, `relay-open`, `relay-agent.py` |
| `engine/` | libvterm spike, `TerminalBackend.h` |
| `data/` | theme, Konsole profile, icons |
| `packaging/`, `.github/workflows/`, `site/` | packages, CI, release, website |
| `tests/` | Python backend and PTY tests, Qt editor and file pane tests |
| `issues/` | file-based tracker |
