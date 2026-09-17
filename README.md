# Relay

A Linux terminal with a rich prompt and bring-your-own-key agents.

Relay embeds Konsole's real terminal (KonsolePart). Under it sits a normal text editor.
Type a shell command and it runs in the terminal. Type a request in plain language and an
agent, using your own API key, answers inline in the same terminal.

**Status: Linux beta in preparation.** The app runs on Ubuntu 24.04 (Qt5/KF5) and builds for
Qt6/KF6. It has not had independent QA yet. See [docs/VALIDATION.md](docs/VALIDATION.md).

<!-- Screenshot placeholder: composer, an inline agent answer with a tool call, a split pane. -->

## Install

### From source

Ubuntu 24.04 (Qt5 / KF5):

```bash
sudo apt install build-essential cmake ninja-build python3 libsecret-tools \
  qtbase5-dev libkf5parts-dev libkf5coreaddons-dev libkf5syntaxhighlighting-dev \
  qtpdf5-dev konsole-kpart
./scripts/build.sh                 # configures, builds, runs all tests
./build/relay --workspace ~/project
```

Debian 13 or Ubuntu 26.04 (Qt6 / KF6):

```bash
sudo apt install build-essential cmake ninja-build python3 libsecret-tools \
  qt6-base-dev libkf6parts-dev libkf6coreaddons-dev libkf6syntaxhighlighting-dev konsole-kpart
./scripts/build.sh
```

`build.sh` picks Qt6+KF6 when both are installed, else Qt5+KF5. Force one with
`./scripts/build.sh -DRELAY_QT_MAJOR=5` (or `6`). KSyntaxHighlighting and Qt PDF are optional.
`cmake --install build` installs to `~/.local` by default.

Options: `--workspace PATH` sets the first terminal directory and the agent workspace.
`--clean-shell` skips `~/.bashrc` for that session, for prompt plugins that conflict.

### Packages (coming)

Beta `.deb`s for Ubuntu 24.04, Debian 13 and Ubuntu 26.04 (amd64, arm64) and the AUR packages
`relay-terminal` and `relay-terminal-git` are prepared but not published.
See [docs/RELEASING.md](docs/RELEASING.md).

## Quick start

1. Start Relay. If you use Warp, open **Provider / BYOK…** and click **Import keys from Warp**.
   Otherwise pick a preset, paste a key, and tick **Save entered key to the desktop keyring**.
2. Type `git status` and press Enter. It runs in the terminal.
3. Type `why is this build failing?` and press Enter. The agent answers inline.

| Key | Action |
|---|---|
| Enter | Submit: commands to the terminal, everything else to the agent |
| Ctrl+Enter | Always the agent; while the agent is busy, interrupt it and send now (Ctrl+Alt+Enter also works) |
| Ctrl+Shift+Enter | Always the terminal; the agent fixes an invalid or failing command |
| Esc (prompt box) | Stop the agent while it is busy |
| @ | Pick a file: `@name` alone opens it in a preview pane; inside an agent prompt it attaches the file |
| → or Ctrl+F / Alt+→ | Accept the dim history suggestion / one word of it |
| Shift+Enter | New line |
| Ctrl+I | Toggle terminal / agent input (from the prompt box) |
| Ctrl+H / Ctrl+Shift+H | Take control of the terminal / back to the prompt |
| Ctrl+Shift+A | Actions palette |
| Ctrl+T, Ctrl+N | New tab, new window |
| Ctrl+P, Ctrl+Shift+P | Split right, split down |
| Alt+Arrows | Move between panes |
| Ctrl+W, Ctrl+Shift+W | Close pane (then tab, then window); restore |
| F12 | Toggle native terminal input |
| Ctrl+Shift+R | Restart a pane's shell or agent after it was stopped |
| Ctrl+Tab, Ctrl+Shift+Tab | Next, previous tab |
| Up (empty prompt box, items queued) | Select queued items; Ctrl+Up/Down move, Enter edits, Delete removes, Esc leaves |
| Up / Down (first / last line) | Prompt history |
| PageUp / PageDown (prompt box) | Scroll the terminal |
| Ctrl+C in the terminal | Copy the selection, or interrupt when nothing is selected |
| Ctrl+C in the prompt box (nothing selected, program running) | Interrupt the program |

Shortcut presets: **Relay** (Chrome-style, default), **Warp**, **VS Code**, **Konsole**
(Actions › Shortcut preset). Every shortcut can be changed in
`~/.config/RelayTerminal/relay/keybindings.json` (Actions › Edit keyboard shortcuts), which
reloads live. Copy on select is off by default (Actions › Copy on select).

## Feature tour

- **Composer routing.** A local check (`bash -n`, then every command word resolved against
  builtins, `PATH`, your live aliases and functions) decides terminal or agent. Nothing is
  executed to decide. Input that is not a runnable command goes to the agent. `/shell ` and
  `/agent ` prefixes force a destination.
- **Fix loop.** In terminal mode, an invalid command or a non-zero exit asks the agent for a
  fix, which Relay runs in your terminal. Up to 3 attempts; Ctrl+C stops it.
- **Inline agent output.** Prompts, answers, tool calls, command output and diffs print in the
  terminal in distinct colors. They are written to the display, never typed into the shell.
  While a program such as vim runs, output shows in a small panel and prints when it exits.
- **One queue for commands and prompts.** Terminal commands entered while the shell is busy and
  agent prompts entered while the agent is busy wait in one queue per pane and run in the order
  entered: an agent prompt queued after a command waits for that command, and vice versa. The
  strip over the terminal shows commands in amber (`$`) and agent prompts in cyan (`✦`). Drag
  rows or use Ctrl+Up/Down to reorder, × or Delete to remove, Enter to edit (editing the next
  item holds the queue until you resubmit). A failing command or a stopped agent pauses the
  queue until **Resume**.
- **The prompt box stays up.** While ordinary programs run (`sudo apt upgrade`, `make`,
  `sleep`), the prompt box stays visible so you can queue more. It hides for full-screen programs
  (Konsole reports the alternate screen: vim, less, htop, tmux), for password prompts (echo off
  with line input on), and for `ssh`/`mosh`/`telnet` sessions. When a program is blocked reading
  the terminal (for example `read -p "continue? [Y/n]"` or a Python REPL), Relay shows "Waiting
  for input" and moves the focus to the terminal; focus returns to the prompt box when it stops
  waiting. While `sudo`, `doas`, `pkexec`, `su` or a program running as another user is in the
  foreground (Relay cannot see whether it is reading), the prompt box stays but the keys go to the
  terminal, with a hint in the prompt row; Ctrl+Shift+H moves to the prompt box to queue items and
  Ctrl+H or a click returns. Ctrl+H / Ctrl+Shift+H still hand control back and forth, and the
  per-program policy in the palette decides who is in control of full-screen programs.
- **@ files.** Typing `@` lists files in the repository (git-tracked and untracked, respecting
  `.gitignore`) or a bounded walk of the directory, previewable files first, filtered as you type.
- **History suggestions.** A dim completion from commands you ran in this directory, the prompt
  history and your shell history file; Actions › Command suggestions from history turns it off.
- **Agent sessions.** Per pane: switch model without losing the conversation, reasoning effort
  (picker, Alt+. / Alt+,), and a context indicator (`ctx 142k · 14%`, amber near the auto-compact
  limit). Type `/` in the prompt box for commands: `/new`, `/model`, `/effort`, `/compact [focus]`,
  `/context`, `/rewind` (also Esc Esc in an empty prompt), `/fork` (continues in a new pane),
  `/resume` (with a recap), `/plan`, `/recap`, `/agents`, `/skills`, `/instructions`, `/export`
  (Markdown under `.relay/exports`). Coming back to the window after 3 minutes, with a finished
  turn and an empty prompt, prints a short recap (Actions › Agent options turns it off).
- **Plan mode.** Shift+Tab in the prompt box (or `/plan`) shows a PLAN chip: the agent
  investigates read-only and writes a plan, which opens in an editable pane (Ctrl+S saves) with
  **Execute**, **Execute in fresh context** and **Keep planning**.
- **Steering.** Enter a prompt while the agent works, then press Enter again on the empty prompt
  box: the prompt joins the running turn at its next tool call instead of waiting in the queue.
- **Instruction files.** On first launch Relay lists instruction files from other tools
  (CLAUDE.md, AGENTS.md, WARP.md, …) to include, and can combine them into a global
  `~/.config/relay/relay.md`. Change it later in Actions › Agent options › Instructions….
- **AI suggestions (off by default).** Actions › Agent options: a suggested next command after a
  command finishes (→ or Tab accepts) and a suggested next prompt after an agent turn (Tab).
- **Windows, tabs and panes.** Each pane has its own shell, prompt, agent and conversation.
  Closed panes, tabs and windows restore in the same directories with new shells.
- **File panes.** Folder explorer and file preview (code, Markdown, images, optional PDF).
  Open with `relay open PATH`, a click on the pane's directory line, or Ctrl+click on a text
  file in terminal output.
- **Palette and shortcuts.** One searchable actions palette. Shortcuts live in
  `~/.config/RelayTerminal/relay/keybindings.json`, reload live, and the agent can change them.
- **Skills.** The agent sees your Warp-style skills in `~/.warp/skills` and loads one before
  following it.
- **Pane isolation.** Each pane's shell and agent run in their own systemd user scope with
  memory limits, so a runaway command stops inside its pane. Limits are configurable.

Relay is not a Konsole fork and does not change your Konsole settings or dotfiles.

## Privacy and your keys

- **BYOK.** Presets for Kimi K3, Z.AI GLM-5.3 (standard and Coding Plan) and DeepSeek V4.1
  Flash via OpenRouter, or any OpenAI-compatible endpoint.
- **Where keys live.** The desktop keyring (GNOME Keyring or KWallet) through `secret-tool`,
  or environment variables such as `RELAY_KIMI_API_KEY`. Keys are passed on stdin, never on a
  command line, in settings files or logs. **Import keys from Warp** copies Warp's
  custom-endpoint keys into the keyring.
- **No telemetry.** No analytics, crash reports, account or Relay server. Relay connects only to
  the provider you configure, when you use the agent.
- **What goes to your provider.** Your agent prompts, the conversation, and tool results
  (command output, file contents the agent reads). Terminal output is not sent automatically.
- **Tools run without asking.** The agent runs commands and writes files as your user as soon
  as it decides to. Every action prints in the terminal first, and Stop agent cancels, but
  nothing is rolled back. Shell commands are **not sandboxed**; file tools are limited to the
  workspace. Start with a disposable project.

## Documentation

[docs/README.md](docs/README.md) lists every document: architecture, roadmap, validation,
releasing, and research notes.

## Contributing and QA

- Tests: `./scripts/test.sh` (Python backend and real Bash/PTY), `./scripts/build.sh` (all).
- Issues are Markdown files in [`issues/`](issues/README.md). Implemented work waits in
  `needs_qa_llm/` until a QA session by a non-Claude model runs its checklist and records
  evidence in `docs/qa_evidence/`.
- Backend diagnostic CLI: `python3 scripts/relay-agent.py --list`, `--import-warp`, or
  `--provider openrouter --workspace PATH`.

## License

GPL-3.0-or-later. See `LICENSE`. Qt, KDE Frameworks and Konsole are external dependencies
under their own licenses. No Warp source code is included.
