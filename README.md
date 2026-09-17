# Relay

A Linux terminal with a rich prompt and bring-your-own-key agents.

Relay embeds Konsole's real terminal (KonsolePart) by default, and ships its own terminal
engine as an option you can pick per pane. Under it sits a normal text editor. Type a shell
command and it runs in the terminal. Type a request in plain language and an agent, using
your own API key, answers inline in the same terminal.

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

Options: `--workspace PATH` sets the first terminal directory and the agent workspace (and, like
`--fresh`, starts one new window instead of reopening the saved layout). `--fresh` ignores the
saved window layout once. `--clean-shell` skips `~/.bashrc` for that session, for prompt plugins
that conflict.

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
  executed to decide. Input that is not a runnable command goes to the agent. Type `!` as the
  first character of an empty prompt box for terminal mode or `*` for agent mode (a chip shows;
  Backspace on the empty box goes back; pasting does not switch). `/shell ` and `/agent ` still
  work. When a command is also an English word used in a sentence ("install ripgrep", "make the
  tests pass"), the label shows the local guess and asks the agent's model in the background
  ("AGENT · guessed: … (82%)"); Enter uses the model's answer if it arrives within 400 ms,
  otherwise the local guess. Typing is never blocked.
- **Two terminal engines, per pane.** KonsolePart is the default. Relay's own engine
  (`docs/ENGINE.md`) is built in: start with `--engine=relay` (or `RELAY_ENGINE=relay`), or
  use the palette's "New pane (Relay engine)" to run both side by side in one window. The
  engine adds screen and scrollback text for the agent, alternate-screen detection, OSC 8
  links, search and prompt marks; `--engine-core=ghostty|libvterm` picks its emulator core.
- **Shell integration (opt-in).** `source /usr/share/relay/shell/relay-integration.bash` in
  `~/.bashrc` (or the palette's "Shell integration (OSC 7/133)") emits OSC 7 and OSC 133, so
  engine panes track the working directory and can jump between prompts. A Zsh version ships
  beside it.
- **Fix loop.** In terminal mode, an invalid command or a non-zero exit asks the agent for a
  fix, which Relay runs in your terminal. Up to 3 attempts; Ctrl+C stops it.
- **Inline agent output.** Prompts, answers, tool calls, command output and diffs print in the
  terminal in distinct colors. They are written to the display, never typed into the shell.
  While a program such as vim runs, output shows in a small panel and prints when it exits.
- **Thinking and tool calls.** Model reasoning streams dimly in a panel over the bottom of the
  terminal (Actions › Agent options › Show thinking); the terminal keeps one line,
  `✦ thought for 7 s`. A turn that used tools ends with `✦ 3 tool calls · 12 s`: Ctrl+click it
  (or Actions › Open last agent turn) for a pane listing each call; Enter on a call opens its
  full output. The link is a `relay://` URL; Relay registers a user-level
  `x-scheme-handler/relay` desktop entry for it on first run.
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
  `/context`, `/rewind` (rewind chat: the conversation only, files untouched; also Esc Esc in
  an empty prompt), `/rewind-code` (restores the files the agent changed, after showing them and
  asking; "Code and chat" does both), `/fork` (continues in a new pane),
  `/resume` (with a recap), `/plan`, `/recap`, `/tasks`, `/continue`, `/agents`, `/skills`, `/instructions`, `/export`
  (Markdown under `.relay/exports`). Coming back to the window after 3 minutes, with a finished
  turn and an empty prompt, prints a short recap (Actions › Agent options turns it off).
- **Model roles.** Actions › Agent options › Model roles picks a model per job: terminal use,
  subagents, the Switchboard agent, the fast agent, chores, vision and route assist. Each role is
  "same as the main agent" until you pick one, and a role whose key is missing quietly falls back to
  the main agent. The fast agent has a default per provider (GLM-5.3 Flash, DeepSeek V4.1 Flash,
  Kimi K2.7 Code HighSpeed). New panes start on the fast agent (the first pane keeps the main agent);
  Alt+F, or Actions › Fast agent for this pane, switches a pane either way without losing the
  conversation.
- **Plan mode.** Shift+Tab in the prompt box (or `/plan`) shows a PLAN chip: the agent
  investigates read-only and writes a plan, which opens in an editable pane (Ctrl+S saves) with
  **Execute**, **Execute in fresh context** and **Keep planning**.
- **Steering.** Enter a prompt while the agent works, then press Enter again on the empty prompt
  box: the prompt joins the running turn at its next tool call instead of waiting in the queue.
- **Tasks.** Everything you ask (typed, queued, steered, re-asked) is tracked per session, and
  the agent splits multi-part asks into tasks. A **Tasks 3/5** chip (in the queue strip while it
  shows, else next to the context indicator) counts completed tasks of the current list; when the
  work is settled it reads **Tasks 5/5** (green) or, amber, **Tasks 3/5 (1 failed, 1 deferred)**
  (failed, deferred, cancelled, unfinished). A new ask after everything settled starts a new
  list; earlier lists stay in the panel under a folded **Earlier** row. Ctrl+Shift+K (Relay
  shortcut preset), a click on the chip, `/tasks` (also `/requests`, `/todos`) or Actions › Tasks…
  open the task list: requests (your words, verbatim) with their tasks under them, marked
  ✓ ◐ ○ ✗ ⏸ ✕; keys: Enter folds, `d` marks done, `x` cancels, `o` reopens, `r` re-asks, Esc
  closes. When a turn stops at its step limit (default 50 model calls, 150 tool calls; Actions ›
  Agent options), the terminal shows **▸ Continue** (Ctrl+click, `/continue` or the palette), and
  turns with more than one task end with a line such as
  `✦ Tasks 3/5 (1 failed, 1 deferred) · T4 “…” failed, T5 “…” deferred`. Resume and recaps list
  unfinished requests. Actions › Agent options › Audit requests after each turn (off by default)
  flags asks that may be unaddressed.
- **Instruction files.** On first launch Relay lists instruction files from other tools
  (CLAUDE.md, AGENTS.md, WARP.md, …) to include, and can combine them into a global
  `~/.config/relay/relay.md`. Change it later in Actions › Agent options › Instructions….
- **AI suggestions (off by default).** Actions › Agent options: a suggested next command after a
  command finishes (→ or Tab accepts) and a suggested next prompt after an agent turn (Tab).
- **Reopen where you left off.** Quit Relay and start it again: your windows come back with their
  tabs, splits, sizes and screens, each pane in the directory it was in, on the same model and
  agent settings, with its conversation reattached (the usual "Session loaded · N turn(s)" line;
  a conversation that is no longer saved starts fresh with a note). Nothing is re-run and terminal
  scrollback is not restored — the shells are new. The layout lives in
  `~/.local/share/relay/state/windows.json` and is saved about a second after each change, so a
  crash loses at most that. A window you close stays closed; quitting keeps what was open. Turn it
  off with Actions › Reopen windows on start, skip it once with `relay --fresh`, or clear it with
  Actions › Start a fresh window set.
- **Windows, tabs and panes.** Each pane has its own shell, prompt, agent and conversation.
  Closed panes, tabs and windows restore in the same directories with new shells. Hovering a
  pane shows a button row: drag grip ⠿, split right, split down, move to a new tab, close. Drag
  the grip onto another pane's edge (a drop zone shows) to move the pane there, or onto a tab bar
  to make it a tab; Ctrl+Alt+Left/Right/Up/Down moves the focused pane (in the Warp preset these
  keys focus panes, so moving is unbound there; GNOME and KDE may take Ctrl+Alt+arrows for
  workspaces). "+" after the tabs opens a tab; hovering a tab shows ⧉ (also in its right-click
  menu) to move it to a new window. Moved panes keep their shell, agent and conversation.
- **File panes.** Folder explorer and file preview (code, Markdown, images, optional PDF).
  Open with `relay open PATH`, a click on the pane's directory line, or Ctrl+click on a text
  file in terminal output.
- **Palette and shortcuts.** One searchable actions palette; it also matches related words
  ("undo" finds Rewind, "reasoning" finds effort, "detach" finds the move actions). Shortcuts
  live in `~/.config/RelayTerminal/relay/keybindings.json`, reload live, and the agent can change
  them.
- **Shortcut hints.** When you click something that has a faster key, a short toast says so
  ("Next time: Ctrl+P · new pane to the right"), at most 3 times per hint and not more than once
  every 20 s. After a finished agent turn, an idle empty prompt box shows a tip. Actions ›
  Agent options › Shortcut hints turns them off; Reset shortcut hints shows them again.
- **Skills.** The agent sees your Warp-style skills (`~/.warp/skills`, `~/.claude/skills`,
  refined copies and imports) and loads one before following it. `/skills` (or Actions ›
  Skills…) lists them: uncheck to exclude, **Refine selected** has the agent write an improved
  copy to `~/.config/relay/skills` and opens it for editing, **Import from repository…** clones a
  git URL, shows the skills and their files for review, and imports the checked ones pinned to
  that commit, and **Check for updates** compares an imported skill with its repository.
- **Pane isolation.** Each pane's shell and agent run in their own systemd user scope with
  memory limits, so a runaway command stops inside its pane. Limits are configurable.

Relay is not a Konsole fork and does not change your Konsole settings or dotfiles.

## Privacy and your keys

- **BYOK.** Presets for Kimi K3, Z.AI GLM-5.3 (standard and Coding Plan) and DeepSeek V4.1
  Flash via OpenRouter, or any OpenAI-compatible endpoint.
- **Where keys live.** The desktop keyring (GNOME Keyring or KWallet) through `secret-tool`,
  or environment variables such as `RELAY_KIMI_API_KEY` (`RELAY_KEYRING=off` skips the keyring). Keys are passed on stdin, never on a
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
