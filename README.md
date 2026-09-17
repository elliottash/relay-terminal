# Relay 0.1 — native terminal + rich input + BYOK agent

**Status: source-code development preview.**
The native app builds, passes its tests and runs on Ubuntu 24.04 (aarch64) against
Qt5 / KDE Frameworks 5, with the real Konsole terminal embedded. The Qt6 / KF6
build path is kept but only its editor tests have been built here, because Ubuntu
24.04 ships no KF6. The Python backend and Bash/PTY integration pass 79 tests.
Kimi K3, GLM-5.3 (Coding Plan) and DeepSeek V4.1 Flash via OpenRouter were tested
with live requests. See [`docs/VALIDATION.md`](docs/VALIDATION.md).

Relay is a native C++/Qt application (Qt6 + KF6, or Qt5 + KF5) that embeds **KonsolePart**, Konsole's actual
terminal component. A separate first-class text editor handles commands and agent
requests. There is no browser terminal, fake terminal canvas, account system,
Relay cloud service, or telemetry.

## An architectural change from the original plan

This first version is **not a fork of the full Konsole application**. It is a
separate executable using the installed KDE Frameworks 6 Konsole component.
It preserves the native terminal engine without copying or patching its internals,
and does not replace the user's existing Konsole installation. Full Konsole tabs,
splits, settings integration, and a source-level fork remain subsequent work.
The Relay-specific editor and agent modules are separate enough to move into a
fork later. No Warp source code was copied.

## Implemented in this source preview

| Area | Implementation |
|---|---|
| Rich input | A real Qt `QPlainTextEdit`: mouse cursor placement, mouse drag selection, Shift+click, Shift+arrow, word selection/navigation, clipboard, multiline editing, undo/redo, basic shell syntax coloring, and an IME submission guard. Qt behavior is used instead of reimplementing a terminal grid editor. |
| Auto-detection | Local executable/builtin/alias/function recognition, natural-language heuristics, explicit shell syntax and prefixes, a live destination indicator, and an ambiguity dialog. Classification never calls an LLM. |
| Overrides | Auto / Terminal / Agent selector; force-submit shortcuts; `/shell ` and `/agent ` prefixes. |
| Real terminal | KonsolePart, with an explicit native-input toggle. Foreground programs receive normal terminal keystrokes. |
| Bash integration | A separate rcfile loads the user's `.bashrc`, preserves prompt commands, reports cwd/exit status/aliases, checks Readline's tty state, and uses an acknowledged command-loading binding. No user dotfiles are edited. |
| Agent | Streaming chat-completions transport, streamed tool-call assembly, provider reasoning-field preservation, multi-step tool loop, cancellation, output/time/step limits. |
| Tools | `run_command`, `read_file`, `list_directory`, `write_file`, run immediately without per-action confirmation. Each command, path, or write diff prints inline in the terminal as it runs. |
| BYOK | Editable base URL/model/parameters; Kimi K3, GLM-5.3 standard API and Coding Plan presets; custom compatible endpoints. Keys are session-memory-only. |

“Rich input” means rich **editing interactions**, not HTML or bold formatting in
shell commands. Commands remain plain text. Shell highlighting is basic, not a
complete Bash parser/completion engine.

## Build on a Linux/KDE Frameworks 6 system

For **Ubuntu 24.04** (no KF6 packages; builds against Qt5 / KF5):

```bash
sudo apt install build-essential cmake ninja-build python3 libsecret-tools \
  qtbase5-dev libkf5parts-dev libkf5coreaddons-dev konsole-kpart
./scripts/build.sh            # auto-selects Qt6+KF6 when present, else Qt5+KF5
./build/relay --workspace "$HOME/path/to/project"
```

Force a version with `./scripts/build.sh -DRELAY_QT_MAJOR=5` or `=6`.

Reference dependency set for **Debian 13 / trixie**:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build python3 unzip \
  qt6-base-dev libkf6parts-dev libkf6coreaddons-dev konsole

unzip relay-0.1.0-source.zip
cd relay-0.1.0
./scripts/build.sh
./build/relay --workspace "$HOME/path/to/project"
```

The package commands are installation instructions, not something executed on
your machine. This environment could not verify the dependency installation or
native build. A Qt5/KF5 Konsole installation does not satisfy the app's Qt6/KF6
requirements. Other distributions need the corresponding Qt6 Widgets/Test,
KDE Frameworks 6 Parts/CoreAddons, and Konsole packages.

`build.sh` compiles and runs the tests; it does **not** install the app or edit
shell startup files. It uses two parallel compiler jobs by default; change with
`RELAY_JOBS=4 ./scripts/build.sh`. Existing CMake options can be supplied as arguments.

For a conflicting prompt/preexec plugin:

```bash
./build/relay --clean-shell --workspace "$HOME/path/to/project"
```

An existing Bash `DEBUG` trap is not replaced: the app falls back to native input.
`--clean-shell` skips `.bashrc` for that Relay session only. It does not remove your
configuration. If you change the shell to Zsh/Fish, SSH, or tmux, use native mode;
rich integration for those environments is not included in v0.1.

Optional user-local installation after a successful build:

```bash
cmake --install build
# Default prefix from build.sh: ~/.local
# Ensure ~/.local/bin is on PATH; desktop launchers may need a session refresh.
```

## Using the composer

| Input | Destination in Auto mode |
|---|---|
| `git status` | Terminal |
| `find . -type f -size +100M` | Terminal |
| `why is this build failing?` | Agent |
| `find the largest files in this repo` | Agent |
| Anything that is not a runnable command: a syntax error, or any command word in a pipeline or list that does not resolve | Agent, without running anything |

Validity is checked locally and never executes the input: `bash -n`, then every command
word is resolved against builtins, `PATH`, live aliases and functions, and executable paths
relative to the terminal's directory.

**Terminal mode** (Ctrl+Shift+Enter, or the Terminal selector) always targets the terminal:

- If the command is not valid, the agent fixes it and Relay runs the fix in your terminal.
- If a command exits non-zero, the agent investigates, fixes it, and Relay re-runs the fix.
- Up to 3 fix attempts. Ctrl+C (exit 130) stops the loop. Auto-mode commands are never auto-fixed.

The agent cannot read the terminal's scrollback, so it reproduces a failure with its own
`run_command` when it needs the error text. That re-runs the command a second time.

**Agent output is inline.** There is no agent pane. Your prompt, the agent's reply, each
tool call, tool output, and write diffs print in the terminal in distinct colors. They are
written to the terminal display, not typed into the shell: they never enter shell history
and are never executed. Control characters are stripped from model and tool output. Output
that arrives while a program is running waits until the next prompt.

| Shortcut | Action |
|---|---|
| Enter | Submit using the selected/detected destination |
| Shift+Enter | Insert a newline |
| Ctrl+Enter | Always agent |
| Ctrl+Shift+Enter | Always terminal; the agent fixes invalid or failing commands |
| F12 | Toggle native terminal input |
| Escape in the composer | Focus native terminal input |
| Up on the first line / Down on the last line | Composer history, preserving the current draft |
| Ctrl+C / Ctrl+V; Ctrl+Shift+C / Ctrl+Shift+V | Copy / paste in the composer |
| Ctrl+A, Shift+arrows, Ctrl+Shift+arrows | Normal text-editor selection |

### Windows, tabs and panes

| Shortcut | Action |
|---|---|
| Ctrl+N | New window, in the focused pane's directory |
| Alt+Tab / Alt+Shift+Tab | Next / previous Relay window |
| Ctrl+T | New tab, in the focused pane's directory |
| Ctrl+Tab / Ctrl+Shift+Tab | Next / previous tab |
| Ctrl+P | New pane to the right |
| Ctrl+Shift+P | New pane below |
| Alt+Left / Right / Up / Down | Move focus to the neighboring pane |
| Ctrl+W | Close the pane; the tab if it is the last pane; the window, after a warning, if it is the last tab |
| Ctrl+Shift+W | Restore the last closed pane, tab or window |

Every pane has its own shell, composer, agent worker and conversation. The toolbar acts on
the focused pane, which has an accent outline. Typing `exit` closes a pane. Restoring
reopens panes in the same directories and layout with **new shells**: scrollback and
programs that were running are not restored. These shortcuts take priority over the
composer and over Konsole, including in native mode, so Readline's Ctrl+W, Ctrl+P, Ctrl+N
and Ctrl+T are unavailable there. Most desktop window managers reserve Alt+Tab for
themselves, in which case Relay never receives it.

Pasting never submits. In native mode, normal terminal keybindings apply (including
Ctrl+C as interrupt). The toolbar also has **Interrupt shell**. Returning from
native mode at a prompt cancels any partial Readline line before using the composer;
returning while a foreground program runs does not inject Ctrl+C into that program.
F12 is reserved by this preview; customizable shortcuts are future work.

The composer rejects control characters and incomplete Bash syntax before shell
submission. It does not execute a “syntax check” command in your shell: parsing
uses a separate non-executing `bash -n` process. Routing is a convenience, **not a
security classifier**, and can misclassify language or unfamiliar commands.
The visible route and force overrides are intentional safeguards.

## Configure Kimi, GLM, or OpenRouter with your own key

Open **Provider / BYOK…**, select a preset, choose an agent workspace, and confirm
sharing submitted prompts and tool results with that provider. Either
paste an API key or leave the key field empty to use the key stored in the
desktop keyring for that preset. Saving settings makes no network call.

| Preset | Base URL | Model ID |
|---|---|---|
| Kimi | `https://api.moonshot.ai/v1` | `kimi-k3` |
| Z.AI standard API | `https://api.z.ai/api/paas/v4` | `glm-5.3` |
| Z.AI Coding Plan | `https://api.z.ai/api/coding/paas/v4` | `glm-5.3` |
| OpenRouter | `https://openrouter.ai/api/v1` | `deepseek/deepseek-v4.1-flash` |

The preset table lives in `backend/relay_core/presets.py` and is mirrored in the
dialog in `src/main.cpp`.

### Stored keys and importing from Warp

Relay looks up a preset's key in this order:

1. An environment variable such as `RELAY_KIMI_API_KEY`, `RELAY_GLM_CODING_API_KEY`
   or `RELAY_OPENROUTER_API_KEY`.
2. The desktop Secret Service keyring (GNOME Keyring or KWallet), entry
   `service=org.relayterminal.Relay provider=<preset>`, via `secret-tool`.

**Import keys from Warp** in the dialog, or the CLI below, reads Warp's custom
endpoints from `~/.config/warp-terminal/settings.toml`, reads their keys from
Warp's keyring entry, and stores each one under the matching Relay preset.
Keys go to `secret-tool` on stdin, never on a command line, into a file, or
across the GUI pipe. The worker resolves a stored key itself.

```bash
python3 scripts/relay-agent.py --import-warp   # copy Warp keys into the keyring
python3 scripts/relay-agent.py --list          # show presets and stored-key status
python3 scripts/relay-agent.py --provider openrouter --workspace /path/to/project
```

Z.AI's current guide lists the coding endpoint in its protocol table and the
standard endpoint in its example. Both presets are therefore exposed rather than
silently assuming one billing/access path. Availability, quota, plan eligibility,
and charges depend on the provider account. Neither endpoint has been live-tested
here. If switching to a model with different capabilities, edit the request JSON;
use `{}` for a custom provider that does not accept these extra parameters.

Supported extras: `thinking`, `reasoning`, `reasoning_effort`, `temperature`, `top_p`.
OpenRouter's `reasoning` stream field is kept for later tool turns and not displayed.
The OpenRouter preset sends no extras by default; add `{"reasoning":{"effort":"high"}}` if wanted.
HTTP is refused except for a loopback model server. HTTPS certificate checks are
not disabled, and authorization-bearing redirects are refused.

**Do not put API keys in the composer or in a checked-in file.** Provider keys are
not written to QSettings, command arguments, or source files. A key is persisted
only if you import it or tick **Save entered key to the desktop keyring**. This is normal
process memory, not encrypted/locked memory. OS swap, crash dumps, or another
process with your privileges are outside this protection.

## Agent execution and privacy

Agents run commands in a **separate non-interactive Bash process** under the chosen
workspace. They do not inherit the interactive shell's aliases, functions, or
unexported variables; `cd` inside an agent command does not change the terminal's
cwd. The chosen agent workspace does not silently follow terminal directory
changes. Both paths are visible in the app.

**Tools run without asking.** When the model calls a tool, Relay runs it at once
and sends the result to your provider. Each command, file path, or write diff prints
inline in the terminal as it starts. Use **Stop agent** to cancel a turn; it does not undo
actions that already ran. Agent tool commands run in a separate process, not in your
interactive shell; only fixed commands from terminal mode run in your shell. Commands time out (default 30s, maximum 120s) and
have a 32 KiB returned-output cap. A turn is limited to 12 model requests and
24 tool calls. These are limits, not a dollar-denominated spending budget.

**Agent shell commands are not sandboxed.** It runs with your account's normal
permissions, can access files beyond the chosen workspace, and can access the
network. The workspace restriction and basic secret-file guard apply to the file
tools, not arbitrary shell commands. Use a disposable project for initial testing.
Text the agent reads from files or command output can try to steer it into
running other commands, and nothing stops a command before it runs.
Package scripts, build systems, and “read-only-looking” commands may execute code.

Terminal history/output is **not automatically uploaded or available to the
agent** in this version. Copy relevant output into your prompt when needed. The
agent does not automatically index your repository. There is no telemetry.

Composer and agent history are kept in memory. Bash/Konsole keep their normal
history behavior and may persist shell command history. The shell bridge uses a
private temporary directory (0700) containing shell metadata and the most recently
staged command (0600); it is removed on normal application exit. An OS crash may
leave a temporary directory. No provider key is placed there.

Stopping does not undo completed actions. A blocked network operation may take up
to its 30-second I/O timeout to return. Agent output and file content are untrusted
and rendered as plain text, not executable HTML.

## Tests and backend-only diagnostic CLI

```bash
./scripts/test.sh                     # Python + real Bash/PTY tests, no pip install
python3 scripts/relay-agent.py --provider kimi --workspace /path/to/project
python3 scripts/relay-agent.py --provider glm --workspace /path/to/project
```

The diagnostic CLI uses the same actual agent backend. It is
not the rich-input desktop app. It prompts privately for the API key. `/new`
clears conversation context and `/quit` exits. Using a real key incurs whatever
usage charges the configured provider applies.

After Qt is available, `./scripts/build.sh` also runs the Qt interaction tests.
To compile only the editor tests without KDE Parts:

```bash
cmake -S . -B build-editor -DRELAY_BUILD_APP=OFF
cmake --build build-editor
ctest --test-dir build-editor --output-on-failure
```

## Not implemented yet

Full Konsole application fork; Zsh/Fish/remote/tmux rich integration; robust custom
prompt-plugin support; tabs/splits; shell completion in the rich editor; automatic
terminal-output capture; command blocks; automatic repository context; same-session
agent command execution; resumable agent conversations;
OS sandboxing; checkpoint/rollback; multiple agents; MCP; packaged desktop binary.

## Source map

- `src/`: native Qt/Konsole UI and rich editor.
- `shell/`: Bash integration and atomic prompt-state events.
- `backend/relay_core/`: router, provider transport, agent loop, tool execution.
- `backend/worker.py`: private newline-delimited JSON over stdin/stdout.
- `tests/`: backend, PTY, HTTP fixture, agent, queue, and Qt editor tests.
- `data/theme/`: dark theme, Konsole profile and color scheme.
- `scripts/`: build, tests, and a backend diagnostic CLI.
- `docs/`: architecture, source research, validation, and next acceptance gates.

New Relay source files are GPL-3.0-or-later; see `LICENSE`. KDE/Qt components are
external dependencies under their own licenses. No third-party source or fonts are
bundled.
