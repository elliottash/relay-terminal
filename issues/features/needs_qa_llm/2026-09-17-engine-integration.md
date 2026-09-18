---
id: 9VXF
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: cross-platform
workstream: terminal
assignee: implemented by Claude Opus 5 (Claude Code session), 2026-09-17
rank: hf
created: '2026-09-17'
acceptance: a non-Claude model QA session runs the checklist and records it under `docs/qa_evidence/`
source: '`docs/ENGINE.md` ("Integration plan for `src/main.cpp`"), `issues/features/2026-09-17-portable-terminal-engine.md`'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Relay's own terminal engine, selectable per pane

## Behavior as implemented

**KonsolePart stays the default.** Nothing about an existing pane changed; the app just no
longer talks to KonsolePart directly.

- `relay::TerminalBackend` (`engine/TerminalBackend.h`) is now the only terminal API `Pane`
  uses. Two implementations: `src/KonsoleBackend.{h,cpp}` (KonsolePart: `TerminalInterface`,
  the Session D-Bus object for inline output and `primaryScreenInUse`, the display clipboard
  slots, the hidden scrollbar) and `src/EngineBackend.{h,cpp}` (Relay's engine —
  `relay::VTermBackend` plus Relay's font and colours from `data/theme/konsole` and the
  copy-on-select setting).
- Per-pane selection (`src/TerminalBackends.{h,cpp}`, factory in `src/BackendFactory.cpp`):
  `--engine=konsole|relay` (default `konsole`), `RELAY_ENGINE`, `--engine-core=ghostty|libvterm`
  / `RELAY_ENGINE_CORE`, and the palette actions **"New pane (Relay engine)"** and
  **"New pane (Konsole engine)"**. Both engines run side by side in one window. A restored
  session keeps each pane's engine (`"engine"` in the saved pane state). Aliases: `vterm`,
  `engine`, `own` for relay; `kpart` for konsole. An unknown value prints a line on stderr and
  falls back to KonsolePart.
- `engine/` is now built and linked by default (`RELAY_HAVE_ENGINE`), and `relay-engine-tests`
  runs in the app's `ctest`. `RELAY_BUILD_ENGINE=ON` still adds the manual harness
  (`relay-vterm-spike`) and the benchmark. Without a libghostty-vt prefix only the vendored
  libvterm core is built, so no Zig is needed; that is the core the QA below exercised.
- Pane features wired through the abstraction for both engines: input, resize, scrollback
  paging (composer PageUp/PageDown), selection with copy-on-select, Ctrl+C copy / Ctrl+C
  interrupt / Ctrl+V paste in the terminal, clear, focus, close and restart, alternate-screen
  detection (hides the composer for vim/less/htop/tmux), title and cwd callbacks, and the
  inline agent output path (`printInline`/`closeInline` → `writeToDisplay` + `redrawPrompt`).
  Foreground-process detection, password prompts, waiting-for-input and `readlineReady` still
  use `/proc/<shell pid>/…`, now reached through `TerminalBackend::shellPid()` /
  `foregroundProcessId()`, so they work for both engines unchanged.
- Engine-only palette actions, gated on `capabilities()`: **"Search terminal…"**,
  **"Jump to previous prompt"**, **"Jump to next prompt"**. **"Clear terminal"** works for both.
- New shell integration, **opt-in**: `shell/relay-integration.bash` (and
  `shell/relay-integration.zsh`) emit OSC 7 (working directory, percent-encoded byte-wise) and
  OSC 133 A/B/C/D with the exit code, using `PROMPT_COMMAND`, `PS1` and `PS0` — no DEBUG trap,
  so it composes with Relay's own bridge and with preexec frameworks. Enable it by sourcing it
  from `~/.bashrc`, or with the palette toggle **"Shell integration (OSC 7/133)"**
  (`terminal/shell_integration`), which sets `RELAY_SHELL_INTEGRATION=1` for new panes so
  `shell/integration.bash` sources it last. Engine panes consume it for cwd tracking and the
  prompt jumps; KonsolePart ignores the sequences.

## What the engine cannot do yet (explicit gaps)

Parity table: `docs/ENGINE.md`. On top of the gaps listed there (ligatures, bidi/RTL,
DECDWL/DECDHL, blink attribute, unlimited/disk-backed history, sixel/kitty graphics, profile
and colour-scheme UI, silence/activity monitoring, macOS/Windows), the *integration* has these:

1. **Not the default, by decision.** Engine panes are opt-in per pane; nothing flips
   automatically, and `--engine=relay` is not remembered across app starts (only per pane, in
   the saved session).
2. **No Relay setting for the engine.** Only the command line, the environment and the palette;
   there is no "default engine" entry in settings and no shortcut (so no shortcut hint either).
3. **Relay's keymap is not pushed into the view.** `TerminalView::setShortcutFilter` is unused;
   Relay's shortcuts win because `RelayWindow` filters events on `qApp` before the view sees
   them. The view's own built-ins (Ctrl+Shift+C/V/F/A, zoom, Shift+PageUp, Ctrl+Shift+PageUp)
   stay active and are not listed in the Relay keymap.
4. **Screen text and scrollback text are not fed to the agent yet.** `screenText()` and
   `scrollbackText()` are implemented and reported in `capabilities()`, but nothing in `Pane`
   calls them; agent context is unchanged.
5. **OSC 133 does not drive Relay's state machine.** Prompt marks are only remembered and used
   for the prompt jumps. Waiting-for-input, command boundaries and exit codes still come from
   `shell/integration.bash` + `/proc` polling, for both engines.
6. **Title is not shown.** `onTitleChanged` is wired to the backend but the pane header and tab
   title still use the working directory only.
7. **Search is a dialog, not a find bar.** The palette action asks for text and reports the
   match count in the status bar; next/previous and highlight-all are only reachable through
   the view's own Ctrl+Shift+F bar. KonsolePart panes have no search at all through Relay.
8. **`resizeTerminal(rows, cols)` is a no-op for KonsolePart** (the grid follows the widget)
   and `setTerminalFont` too — Konsole panes take their font from the Relay profile as before.
   `selectedText()` and `scrollbackText()` return nothing for KonsolePart, `find()` returns 0,
   `scrollToPrompt()` returns false.
9. **Engine panes ignore `TerminalInterfaceV2` profile behaviour**: Konsole's
   `UnderlineFilesEnabled`/`TextEditorCmd` path opening is replaced by the engine's own
   `onLinkActivated` (URLs via `QDesktopServices`, existing paths via Relay's file panes);
   `line:column` from a Ctrl+click is passed but not used to position the editor pane.
10. **Not tried:** the ghostty core inside the app (no libghostty-vt prefix on this machine, so
    only the libvterm core ran), IME (fcitx5/ibus), accessibility (Orca), tmux/htop in an
    engine pane inside Relay, session save/restore of an engine pane, per-pane isolation
    (`systemd-run`) with the engine, and the OOM/restart banner path for an engine pane.
11. **Zsh integration file is untested** — no zsh on the implementer's machine. The Bash one is
    verified end to end.
12. **Prompt jump lands near, not on, the prompt line.** With the libvterm core, "Jump to
    previous prompt" scrolled into the scrollback but not to the marked row. The marks
    themselves arrive (verified over a PTY); the row the engine records for a mark while
    Relay stages a command with Ctrl+X Ctrl+R needs a closer look.

## Implementer check (not a QA verdict)

Build: `cmake --build build` clean, no new warnings (Qt5/KF5, `RELAY_QT_MAJOR=AUTO`).
`./scripts/test.sh` OK (274 tests at the time of writing). `ctest --test-dir build` all green,
including the new `backends` test and `relay-engine-tests`. The engine also still builds
standalone (`-DRELAY_BUILD_APP=OFF -DRELAY_BUILD_ENGINE=ON`) and under Qt6
(`-DRELAY_QT_MAJOR=6 -DRELAY_BUILD_APP=OFF`), with no new warnings.

OSC emission over a real PTY through Relay's own rcfile with `RELAY_SHELL_INTEGRATION=1`:
`133;A`, `133;B`, `133;C`, `133;D;0`, `133;D;1`, `7;file://<host>/tmp` — correct, and
`__relay_url_encode "/tmp/a b/ü+x"` → `/tmp/a%20b/%C3%BC%2Bx`.

Live run under Xvfb (`docs/qa_evidence/2026-09-17-engine-integration/drive.sh`, isolated
`XDG_CONFIG_HOME`, no provider keys), screenshots in the same directory:

| Shot | What it shows |
|---|---|
| 01-03 | one Konsole pane, the palette entry, then Konsole left + Relay engine right in one window |
| 04 | engine pane: `ls --color`, CJK, emoji, box drawing, bold, 256-colour |
| 05-06 | `vim` in the engine pane (alternate screen; Relay hid the composer), then back |
| 07 | 40 000-line `cat` and Ctrl+C (`^C`, exit 130) |
| 08 | window resized to 1000x700 with both panes; the engine reflowed |
| 09 | mouse selection in the engine pane + Ctrl+C → "9 characters copied" |
| 10 | inline agent output ("New agent conversation") written into the engine terminal, prompt redrawn |
| 11 | palette "Clear terminal" on the engine pane |
| 12 | palette "Jump to previous prompt" moved the viewport into the scrollback (it reported no failure, so a mark was found; it did not land exactly on the prompt line — see gap 12) |
| 13-14 | the Konsole pane still runs commands normally with the shell integration on |

## QA checklist

1. Default is unchanged: start `relay` with no flags; the pane is KonsolePart, and every
   existing terminal behaviour (staged commands, fix loop, native mode F12, password prompts,
   sudo focus, inline agent output, Ctrl+click on a path) works as before.
2. `relay --engine=relay` and `RELAY_ENGINE=relay relay`: the first pane uses the engine.
   `relay --engine=nonsense` prints one stderr line and falls back to KonsolePart.
3. Palette: "New pane (Relay engine)" and "New pane (Konsole engine)" from a pane of either
   kind; both panes in one window keep working independently (run a command in each).
4. In an engine pane: `ls`, `vim` (composer hides, returns on exit), `less`, `htop`, `tmux`,
   a long `cat` with Ctrl+C, window resize with vim open, `sudo -k true` (password prompt →
   "you're in control"), Ctrl+C with and without a selection, Ctrl+V at a prompt,
   middle-click paste, PageUp/PageDown from the composer, "Clear terminal".
5. Agent turn in an engine pane with a real provider: the reply, tool calls and diffs print
   inline in Relay's colours and the prompt is redrawn afterwards; `Ctrl+Shift+Enter` terminal
   mode and the fix loop still work.
6. Shell integration: turn on "Shell integration (OSC 7/133)", open a new engine pane, run a
   few commands, then "Jump to previous prompt" / "Jump to next prompt"; turn it off, open
   another pane, and confirm the jump reports that no mark was found. Confirm a Konsole pane
   with it enabled shows no stray characters in its prompt. Also check a user who sources
   `shell/relay-integration.bash` from their own `~/.bashrc` (including with a preexec
   framework such as starship or bash-preexec loaded).
7. `cd` in an engine pane updates the pane's directory header (OSC 7 and Relay's own bridge
   both report it).
8. Close a pane, close the tab, restart the shell (kill it from another terminal → restart
   banner) for both engines. Restore a closed engine pane (Ctrl+Shift+W) and confirm it comes
   back as an engine pane.
9. With `libghostty-vt` built (`engine/scripts/build-libghostty-vt.sh`, `-DRELAY_ENGINE_WITH_GHOSTTY=ON`),
   repeat 4 with `--engine-core=ghostty` and compare the 200 MB `cat` against Konsole.
10. Confirm nothing regressed in `./scripts/test.sh` and `ctest --test-dir build`.
