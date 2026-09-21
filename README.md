# Relay

A Linux terminal with a rich prompt and bring-your-own-key agents.

Relay has its own terminal engine, and under it sits a normal text editor. Type a shell
command and it runs in the terminal. Type a request in plain language and an agent, using
your own API key, answers inline in the same terminal.

**Status: Linux beta in preparation.** The app runs on Ubuntu 24.04 (Qt5). It has not had
independent QA yet. See [docs/VALIDATION.md](docs/VALIDATION.md).

<!-- Screenshot placeholder: composer, an inline agent answer with a tool call, a split pane. -->

## Install

### Packages (beta)

Every release publishes `.deb`s for Ubuntu 24.04 (Qt 5), Ubuntu 26.04 and Debian 13 (Qt 6), amd64
and arm64, with `SHA256SUMS`, on [GitHub Releases](https://github.com/elliottash/relay-terminal/releases/latest):

```bash
sudo apt install ./relay_*_ubuntu24.04_amd64.deb    # or ubuntu26.04 / debian13, amd64 / arm64
relay --workspace ~/project
```

To upgrade a package install, type `/update` in any prompt box: Relay downloads the newest
release's `.deb` for its distribution and architecture, checks it against the release's
`SHA256SUMS`, installs it (pkexec asks for the password) and restarts itself. `relay-update.py
check` does the same discovery without installing.

**Update channel** (Options › General › Updates, or `relay-update.py --channel all|stable`):
`all` is the default and offers every published release, betas included; `stable` offers only the
releases that are not marked as a prerelease. Either way the *highest* version in the channel is
the one offered — GitHub lists releases by creation date, so a patch cut for an older tag is not
the newest version.

A fresh install runs on Relay Free with no key (see "Privacy and your keys"). The AUR packages
`relay-terminal` and `relay-terminal-git` are prepared but not yet published; see
[docs/RELEASING.md](docs/RELEASING.md).

### From source

Ubuntu 24.04:

```bash
sudo apt install build-essential cmake ninja-build python3 python3-cryptography libsecret-tools \
  qtbase5-dev libkf5syntaxhighlighting-dev qtpdf5-dev
./scripts/build.sh                 # configures, builds, runs all tests
./build/relay --workspace ~/project
```

Relay builds against Qt 5 or Qt 6 (`qt6-base-dev`, `libkf6syntaxhighlighting-dev` on Ubuntu 26.04
and Debian 13); KDE Frameworks is no longer required (KonsolePart was retired on 2026-09-18).
Force one with `./scripts/build.sh -DRELAY_QT_MAJOR=5` (or `6`). KSyntaxHighlighting and Qt PDF
are optional.
`cmake --install build` installs to `~/.local` by default.

Options: `--workspace PATH` sets the first terminal directory and the agent workspace (and, like
`--fresh`, starts one new window instead of reopening the saved layout). `--fresh` ignores the
saved window layout once. `--clean-shell` skips `~/.bashrc` for that session, for prompt plugins
that conflict.

## Quick start

1. Start Relay and open **Options › Models** (`/models`, Ctrl+Shift+O, or ⚙ customize… at the
   bottom of the model box). The page reads top to bottom the way it is set up: **providers** —
   add key… for one of them (or Actions › API keys… to **Import from Warp** or **from Claude Code /
   Codex**; **test** checks the key reaches the provider) — then **models in the picker**, a
   checklist of each provider's models, then five ordered lists — **main, high, flash, lite and
   local models**. Each row is a model and the reasoning level it runs at there, in the provider's
   own words (xhigh on a GPT row, max on a GLM row). The first of a list is what that tier runs
   on: main for new panes, high for plan mode, flash for terminal driving and quick side calls,
   lite for chores, local for `/local`. The rest are that tier's fallbacks, in order, and a model
   in no list is only used when you pick it by hand. "fill the lists" applies Relay's defaults,
   or the recommended ones with the cheaper OpenRouter twins after your own models. A **profile**
   names the five lists as a set — "AI work", "admin work" — and switching one in, there or with
   `/profile`, swaps every list at once for every pane; a profile exports to a JSON file you can
   mail, commit or carry to another machine, and imports back. Claude Code
   and Codex, when installed, are providers there like any other.
2. Type `git status` and press Enter. It runs in the terminal.
3. Type `why is this build failing?` and press Enter. The agent answers inline.

| Key | Action |
|---|---|
| Enter | Submit: commands to the terminal, everything else to the agent |
| Ctrl+Enter | Always the agent; while the agent is busy, interrupt it and send now (Ctrl+Alt+Enter also works) |
| Ctrl+Shift+Enter | Always the terminal; the agent fixes an invalid or failing command. A line that reads like a request runs nothing and suggests Ctrl+I instead |
| Esc (prompt box) | Stop the agent while it is busy |
| @ | Pick a file: `@name` alone opens it in a preview pane; inside an agent prompt it attaches the file |
| → or Ctrl+F / Alt+→ | Accept the dim history suggestion / one word of it |
| Shift+Enter | New line |
| Ctrl+I | Toggle terminal / agent input (from the prompt box; at a password prompt it switches to the agent) |
| Ctrl+H / Ctrl+Shift+H | Take control of the terminal (the only way keys reach it) / back to the prompt box |
| Ctrl+Shift+A | Actions: everything you can do now, with its keys, in a list you can filter (again to close) |
| Ctrl+Shift+O | Options: what persists, a tab per section (again to close; also Ctrl+, and the gear) |
| Ctrl+Shift+S | Switchboard: this repository's cards, threads and plans (again to close it) |
| Alt+M, Ctrl+Alt+M | Models: drop the pane's model box open / the dialog that picks *and* prioritizes them (`/model`) |
| Alt+E | Reasoning: drop the pane's level box open (also Alt+. / Alt+, and `/effort`) |
| Ctrl+Shift+M | Model options: Options › Models (`/models`) |
| Ctrl+Shift+Y | Sessions: resume a saved session (`/resume`) |
| Alt+A, Ctrl+Shift+X | Subagents: this pane's subagent tabs / stop all running subagents |
| Ctrl+T, Ctrl+N | New tab, new window |
| Ctrl+E, Ctrl+Shift+E | New pane to the right (then ← ↑ ↓ places it) |
| Ctrl+Shift+J | Delegate: the agent drives the program in this pane; ask from the prompt box |
| Ctrl+Shift+L | Step through files, folders and links in the output (Enter opens, Esc leaves) |
| Alt+Arrows | Move between panes |
| Ctrl+W, Ctrl+Shift+W | Close pane (then tab, then window) |
| Ctrl+Shift+Z | Reopen the last closed pane, tab or window, with its text and conversation |
| F12 | Toggle native terminal input (same hand-over as Ctrl+H) |
| Ctrl+Shift+R | Restart a pane's shell or agent after it was stopped |
| Ctrl+Tab, Ctrl+Shift+Tab | Next, previous tab |
| Up (empty prompt box, items queued) | Open the queued items in the prompt box, one at a time: Up/Down move between them, Ctrl+Up/Down reorder, Enter saves, Esc cancels, Shift+Delete removes |
| Up / Down (first / last line) | Prompt history |
| PageUp / PageDown (prompt box) | Scroll the terminal |
| Ctrl+C in the terminal | Copy the selection, or interrupt when nothing is selected |
| Ctrl+C in the prompt box (nothing selected, program running) | Interrupt the program |

Shortcut presets: **Relay** (Chrome-style, default), **Warp**, **VS Code**, **Konsole**
(Options › Keyboard). Every shortcut can be changed in
`~/.config/RelayTerminal/relay/keybindings.json` (Options › Keyboard › Edit keyboard shortcuts),
which reloads live. Copy on select is off by default (Options › Terminal).

**Actions and Options** are two panes that open beside the one you are in, one at a time.
**Actions** (Ctrl+Shift+A) is everything you can do *now*, to this pane, conversation or window —
resume a session, open the Switchboard, pick the model, a new pane, rewind, open Options — in one
list you can filter, recent first, each with its keys. **Options** (Ctrl+Shift+O, the gear at the
top right, or Ctrl+,) is what *persists*: a tab per section — General, Appearance, Models, Terminal,
Agent, Voice, Privacy, Keyboard — with every setting as a real control. The two open side by side:
each key opens, focuses or closes its own pane and leaves the other where it is, so a setting can be
read next to the action that uses it. The title bar has a button for each tool pane, left of the gear: Actions,
Sessions and the Switchboard, each with the glyph its pane wears. Either search box reaches both: in Actions, an option shows as an
"Options › …" row that takes you to it. Type, ↑ ↓, Enter runs the action or changes the row; Esc
closes and puts focus back where it was, so nothing there needs the mouse.

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
- **Relay's own terminal engine** (`docs/ENGINE.md`) runs every pane. It gives the agent the
  screen and the scrollback, detects the alternate screen, and handles OSC 8 links, search and
  prompt marks; `--engine-core=ghostty|libvterm` picks its emulator core.
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
  terminal (Options › General › Show thinking); the terminal keeps one line,
  `✦ thought for 7 s`. A turn that used tools ends with `✦ 3 tool calls · 12 s`: Ctrl+click it
  (or Actions › Open last agent turn) for a pane listing each call; Enter on a call opens its
  full output. The link is a `relay://` URL; Relay registers a user-level
  `x-scheme-handler/relay` desktop entry for it on first run.
- **One queue for commands and prompts.** Terminal commands entered while the shell is busy and
  agent prompts entered while the agent is busy wait in one queue per pane and run in the order
  entered: an agent prompt queued after a command waits for that command, and vice versa. The
  strip over the terminal shows commands in amber (`$`) and agent prompts in cyan (`✦`).
  **Up on an empty prompt box opens the queued items for editing**, starting with the one queued
  last: the highlighted item's text sits in the prompt box and is edited there like anything else.
  Up and Down move between items — inside a multi-line item they move the cursor first, and only
  step to the next item from its first or last line — Enter saves the edit and leaves, Esc drops it,
  Ctrl+Up/Down reorder, Shift+Delete removes. While the **top** item is highlighted the queue holds,
  so the item being edited cannot run out from under the edit; leaving it releases the queue. Rows
  can also be dragged to reorder and have an × to remove. A failing command or a stopped agent
  pauses the queue until **Resume**.
- **The prompt box is the only keyboard input.** Clicking the terminal selects text, scrolls and
  follows links, but never takes the keyboard: typed keys always reach the prompt box. **Ctrl+H**
  (or F12) is the one deliberate exception — it hides the prompt box and types straight into the
  terminal; Ctrl+Shift+H comes back.
  - **Full-screen programs** (vim, less, htop, tmux, and `ssh`/`mosh`/`telnet` sessions) no longer
    take the keyboard by themselves. A small **Take control (Ctrl+H)** button appears over the
    terminal and the prompt box keeps the focus until you press it. Leaving the program returns to
    the prompt box. Actions › "Control when a full-screen program starts" (or the per-program entry
    below it) switches back to the old automatic hand-over.
  - **Password prompts** (echo off with line input on: `sudo`, `su`, `ssh`, `git`) turn the prompt
    box into a masked field with a `password for sudo` chip. Enter writes the line to the running
    program's stdin, not to the shell. The text never enters prompt history, the queue, the request
    ledger, the session file, logs, route assist, suggestions or any model prompt; it is wiped from
    the editor and from memory as soon as it is written, and Relay prints nothing about it in the
    terminal. Masked input ends when the program turns echo back on. **Ctrl+I** switches to the
    agent instead ("paste the password from my clipboard") — that path is not masked and follows
    the usual control rules; Esc leaves masked input without answering.
  - **Ordinary programs reading a line** (`apt`'s `[Y/n]`, a Python REPL, `read` in a script):
    what you submit is sent to that program with a short "Sent to apt", instead of being queued.
    When nothing is reading, submissions keep the existing behaviour — run now, or queue until the
    terminal is free. While `sudo`, `doas`, `pkexec`, `su` or a program running as another user is
    in the foreground, Relay cannot see whether it is reading, so anything but a password prompt
    queues; the prompt row says so.
- **@ files.** Typing `@` lists files in the repository (git-tracked and untracked, respecting
  `.gitignore`) or a bounded walk of the directory, previewable files first, filtered as you type.
- **History suggestions.** A dim completion from commands you ran in this directory, the prompt
  history and your shell history file; Actions › Command suggestions from history turns it off.
- **Agent sessions.** Per pane: switch model without losing the conversation, reasoning effort
  (picker, Alt+. / Alt+,), and a context indicator (`ctx 142k · 14%`, amber near the auto-compact
  limit). Type `/` in the prompt box for commands: `/new`, `/model` (alone: the picker, also
  Ctrl+Alt+M), `/models` (Options › Models, also Ctrl+Shift+M), `/profile [name]` (switch the model profile), `/swap`, `/effort` (`/reasoning`), `/compact [focus]`,
  `/context`, `/rewind` (rewind chat: the conversation only, files untouched; also Esc Esc in
  an empty prompt), `/rewind-code` (restores the files the agent changed, after showing them and
  asking; "Code and chat" does both), `/fork` (continues in a new pane),
  `/resume` (with a recap; `/sessions` is the same), `/conversations`, `/find`, `/plan`, `/recap`, `/tasks`, `/continue`, `/agents`, `/skills`, `/instructions`, `/export`
  (Markdown under `.relay/exports`), `/main` and `/flash` (this pane's model tier), `/glm` and `/kimi`
  (switch provider), `/help` (the popup `?` shows). A `/command` Relay does not have is answered by
  Relay, not by the shell: it names the closest real commands and points at `/` and `/help`. Coming back to the window after 3 minutes, with a finished
  turn and an empty prompt, prints a short recap (Options › General turns it off).
- **Sessions: list and full-text search.** Ctrl+Shift+Y (also `/resume`, `/sessions`, `/conversations`, Actions ›
  Sessions… and the list button in the title bar) opens every saved conversation, grouped by project and newest first, with a
  search field that filters as you type. The search covers **both** agent threads (your prompts,
  the agent's replies, its tool calls and their output) and **Relay's terminal history** (the
  commands Relay ran, their exit status and, on the Relay engine, their output); every hit says
  which kind it is and which turn it came from. Filters: this project or all projects, agent
  threads or terminal history, model, open tasks, date. The right pane previews the conversation
  with the match highlighted; Enter resumes it in this pane, Shift+Enter opens it in a new one,
  and there are Rename, Pin and Delete. Each row shows its tags — pinned, unfinished,
  edits · N files, the branch — and the agent's summary once it has one; → unfolds a quick look
  (summary, first prompt, the last turns, files, open todos), and **Continue** heads the list
  with this project's pinned, unfinished and recently closed sessions. Words are matched inside
  one message or command; use
  `"quotes"` for a phrase.
  **Ctrl+F** searches the current pane instead: the terminal scrollback (Relay engine) and this
  pane's conversation, with next/previous, wrap, a match count and Esc to close.
  The index is an SQLite FTS5 file next to the sessions (`~/.local/share/relay/index.db`, 0600),
  updated on every autosave and rebuildable from the session files at any time (Actions › Rebuild
  the conversation index). It holds message text, so it stays on this machine and is deleted with
  the conversation; `RELAY_INDEX=off` turns indexing off entirely.
- **The model dialog: pick *and* prioritize.** Ctrl+Alt+M, `/model`, or **more models…** at the
  bottom of the pane's model box. A tab per list — **high · main · flash · lite · local** — and an
  **all** tab; it opens on the tab of the mode the pane is in, and ←/→ (or Ctrl+Tab) walk them. A
  tier tab *is* that list: numbered, in order, one row per model in lower case with the provider in
  a **via** column, rank 1 of main marked "new panes start here", and a rank whose provider has no
  key or is spent greyed in place with the reason rather than dropped. **Enter** uses the row in
  this pane, **Alt+↑/Alt+↓** or a drag moves it, **Delete** takes it out, the level list on the
  right sets the level that model runs at *in that list*, and **Ctrl+Z** undoes any of it — the
  same storage Options › Models writes, so the two can never disagree. **Typing** searches every
  model: this list's matches first, then "not in this list", where **Ctrl+Enter** adds one. The
  **all** tab is the flat picker — a **sort** menu (priority, a to z, intelligence, speed, most
  used, subscription left), ★ favorites and the ten most recent above the rest, subscription
  windows ("5h 62% left, resets 14:30") under the list — with one row per model whatever serves it
  and **→** opening that row's providers to choose one. The profile is named in the header and
  switched there. `/model <name>` switches without the dialog; the box lists the same rows.
- **Model roles: main, high, flash, lite, local.** The tiers are five ordered lists on Options ›
  Models: **main** for agent turns and subagents, **high** for plan mode (main at max reasoning
  unless you list a model), **flash** for driving programs and quick side calls, **lite** for
  titles, labels and duplicate checks, **local** for a model served on this machine. Each entry is a
  model plus a reasoning level; the first one is what the tier runs on, the rest are its fallbacks
  in order, and the first entry of the main list is your default provider.
  **per-job models (advanced)**, on the same page, opens one row per job — agent turns, plan mode,
  subagents, terminal use, new panes, suggestions, summaries, Switchboard threads, chores, the
  request audit — plus the vision model. Each row shows the model it resolves to ("flash ·
  glm-5.3-flash") and follows its tier until you choose "its own provider…"; then you pick the
  provider, one of its models from a list, and a reasoning level in that provider's own words
  (xhigh on OpenAI). Providers are named as companies — Kimi, Z.AI (GLM), OpenRouter, OpenAI
  (ChatGPT), Anthropic (Claude), Google (Gemini) — and only the ones you hold a key for are offered.
  Command routing is pinned to `google/gemini-3.5-flash-lite` on purpose: routing has a sub-second
  budget and that model measures 0.5–0.6 s against 2.3–4.9 s for Gemini 3.8 Flash.
  A tier whose provider has no key steps down to the next one and says so inline; nothing ever fails
  because a key is missing. Every pane starts on the Main agent unless Options › Agent › "New panes
  use the Flash agent" is on, and then every pane after a window's first one starts on the Flash agent.
  **`/flash`**, Alt+F, the **(flash)** row in the model box, or Actions › Flash agent for this
  pane moves a pane to the Flash model, and **`/main`** or the **(main)** row moves it back,
  both without losing the conversation. **`/glm`** and **`/kimi`** switch
  the pane to that provider's Coding Plan (its pay-as-you-go preset when no Coding Plan key is stored).
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
  closes. When a turn stops at its step limit (default 256 model calls, 150 tool calls; Actions ›
  Options › Agent), the terminal shows **▸ Continue** (Ctrl+click, `/continue` or the palette), and
  turns with more than one task end with a line such as
  `✦ Tasks 3/5 (1 failed, 1 deferred) · T4 “…” failed, T5 “…” deferred`. Resume and recaps list
  unfinished requests. Options › Agent › Audit requests after each turn (off by default)
  flags asks that may be unaddressed.
- **When a model goes quiet.** A turn whose model sends nothing usable for 60 s (SSE keepalives do
  not count) ends instead of hanging: the connection is closed, the turn is retried once
  automatically when nothing of the answer had arrived, and only then does it fail with "the model
  sent nothing for 60 s", keeping your request open. While a turn runs the status line counts up —
  `thinking · 48 s · Esc stops`. Change the limit in Actions › Diagnostics › Stop a silent model
  after…, or set `RELAY_PROVIDER_TIMEOUT` (seconds).
- **Logs.** Relay writes `~/.local/share/relay/logs/relay.log` and `worker.log` (5 MB × 3, mode
  0600): timestamps, pane and session ids, model and provider host, turn start/end and outcome,
  tool names and durations, event types, errors and retries. **No prompts, answers, tool output,
  file contents or keys.** Actions › Diagnostics › Open log folder, and Log detail to change the
  level — its opt-in "Verbose" level does add your prompt text to the file.
- **Instruction files.** On first launch Relay lists instruction files from other tools
  (CLAUDE.md, AGENTS.md, WARP.md, …) to include, and can combine them into a global
  `~/.config/relay/relay.md`. Change it later in Options › Agent › Instructions….
- **AI suggestions (off by default).** Options › Privacy: a suggested next command after a
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
  Closed panes, tabs and windows restore in the same directories with new shells; Ctrl+Shift+Z
  brings the last one back, and Actions › Recently closed keeps the last 25 — a list that
  survives a restart. Hovering a
  pane shows a button row: drag grip ⠿, split right, split down, move to a new tab, close. Drag
  the grip onto another pane's edge (a drop zone shows) to move the pane there, or onto a tab bar
  to make it a tab; Ctrl+Alt+Left/Right/Up/Down moves the focused pane, and Ctrl+Alt+Left or
  Ctrl+Alt+Right followed by Ctrl+Alt+Down within two seconds docks the pane beneath the neighbor
  it moved toward (in the Warp preset these
  keys focus panes, so moving is unbound there; GNOME and KDE may take Ctrl+Alt+arrows for
  workspaces). "+" after the tabs opens a tab; hovering a tab shows ⧉ (also in its right-click
  menu) to move it to a new window. Moved panes keep their shell, agent and conversation.
- **File panes.** Folder explorer and file preview (code, Markdown, images, optional PDF).
  Open with `relay open PATH`, a click on the pane's directory line, or Ctrl+click on a text
  file in terminal output.
- **SSH hosts.** Actions › **Connect to host…** lists your recent hosts and every `Host` in
  `~/.ssh/config` (with the `user@host` its block writes); typing `user@host` or `ssh host` in
  the prompt box offers that host too. Choosing one opens a new tab that logs in, and Split on
  the same host opens another pane on it. The pane knows the host it is on: remote shell
  integration is typed into the remote bash or zsh (tmux included, or it says what it needs) so
  prompt marks and the working directory keep working, and the agent keeps its local powers — it
  runs commands on the host, and its file tools take a `host`, so it reads and edits remote
  files and shows the diff before writing (`docs/SSH-AND-MOSH.md`).
- **Share a pane with a phone — and with other people.** The share button in the pane's
  top-right corner pairs a phone: a link and QR, a five-digit code both ends derive, and a deliberate choice
  between viewing and typing. The phone sees the pane's screen, gets one prompt box routed
  exactly like Relay's own (a command runs, anything else goes to the agent), pages through the
  scrollback, and can send a voice clip that is transcribed on the desktop, so the API key never
  leaves it. For other people there is the **Sharing pane** — invite with a role (Viewer or
  Editor) and an expiry — which opens by itself when somebody knocks: who is here, what is
  waiting for you, Refuse first. While a guest holds the keyboard the title row says so, and
  your typing takes control straight back (`docs/REMOTE-PROTOCOL.md`).
- **Subagents.** A task can go to a subagent that runs in its own tab with its own conversation;
  the task list shows which task each one works on. Alt+A opens this pane's subagent tabs (and
  goes back to the main agent), Ctrl+Shift+X stops all running subagents, and `/agents` lists
  agent definitions and the running ones.
- **Local models.** Options › **Local models** finds, adds, tests and removes a model server on
  this machine, and the agent can set one up from the local-model-setup skill. `/local`
  switches this pane to the Local agent and back.
- **Voice.** Press the microphone beside the prompt box, or hold the voice key (Right Alt by
  default), and the transcript is inserted at the cursor; transcription goes through the
  provider you already use. The hold key is a setting under Options › Voice.
- **Actions and shortcuts.** The Actions pane (Ctrl+Shift+A, Ctrl+?) lists every action with its
  keys, and its search finds actions and options together; it also matches related words
  ("undo" finds Rewind, "reasoning" finds effort, "detach" finds the move actions). Shortcuts
  live in `~/.config/RelayTerminal/relay/keybindings.json`, reload live, and the agent can change
  them.
- **Shortcut hints.** When you click something that has a faster key, a short toast says so
  ("Next time: Ctrl+E · new pane"), at most 3 times per hint and not more than once
  every 20 s. After a finished agent turn, an idle empty prompt box shows a tip. Options ›
  General › Shortcut hints turns them off; Actions › Reset shortcut hints shows them again.
- **Skills.** The agent sees your Warp-style skills (`~/.warp/skills`, `~/.claude/skills`,
  refined copies and imports) and loads one before following it. `/skills` (or Actions ›
  Skills…) lists them: uncheck to exclude, **Refine selected** has the agent write an improved
  copy to `~/.config/relay/skills` and opens it for editing, **Import from repository…** clones a
  git URL, shows the skills and their files for review, and imports the checked ones pinned to
  that commit, and **Check for updates** compares an imported skill with its repository.
- **Pane isolation.** Each pane's shell and agent run in their own systemd user scope with
  memory limits, so a runaway command stops inside its pane. Limits are configurable.

Relay is not a Konsole fork and does not read or change your Konsole settings or dotfiles.

### Switchboard (Ctrl+Shift+S)

The repository's `issues/` tracker as a board: one Markdown card per issue, plan or memory, in
git, readable on GitHub and usable without Relay. Tabs are categories (Features, Bugs, Design,
Marketing, Plans, Memory, Deferred, Done), columns are status, and dragging a card between them
moves the file and records the move. Each card has a thread: the discussion and the audit trail,
append-only, one entry per write.

- **Ctrl+Shift+S** opens it beside the pane you were in (again to close it); `n` adds a card, `/` filters
  (`label:voice`, `status:ready`, `@agent`, `waiting:me`), Enter opens one, `m` moves it,
  `t` sends `#ID` to the composer, `Del` (or the card's Delete button) deletes one after a
  confirm — undoable for 30 seconds, and agents have no delete at all.
- **In the terminal**: `#` and a few characters picks a card, `/card <text>` captures one without
  leaving the prompt, `/switchboard` opens the pane. A `#K7Q2` in a prompt hands the agent the
  card, its open tasks and its recent thread.
- **The agent keeps it** (`board.yaml`'s `agent.autonomy`): every request you make that it does
  not finish becomes a card or updates one, questions go on the card with a recommendation,
  decisions quote you, and landed work moves to a QA lane with its evidence. There is no delete
  tool, every write is recorded in the thread with the model and turn that made it, rewrites of
  your own text keep the old text, and each write can be undone for 30 seconds.
- **Card threads run on their own agent** (the `switchboard` model role, defaulting to your main
  model), so talking about a card never disturbs a pane's conversation.

Cards are plain files: `python3 scripts/relay-board.py check` verifies the format and `index`
regenerates `issues/BOARD.md`. A repository without `issues/board.yaml` never sees any of this.

## Privacy and your keys

- **BYOK.** Subscriptions (GLM Coding Plan, Kimi Code, MiniMax Coding/Token Plan), the OpenRouter
  aggregator, and pay-as-you-go OpenAI, Anthropic and Google Gemini — all through their
  OpenAI-compatible endpoints — plus Kimi's and Z.AI's standard APIs and any other
  OpenAI-compatible endpoint.
- **Where keys live.** The desktop keyring (GNOME Keyring or KWallet) through `secret-tool`,
  or environment variables such as `RELAY_KIMI_API_KEY` (`RELAY_KEYRING=off` skips the keyring). Keys are passed on stdin, never on a
  command line, in settings files or logs. Options › Models shows, per provider,
  whether the key is in the keyring, comes from `RELAY_*_API_KEY` or is missing; **Test** makes one
  two-word call and reports ok or the HTTP status without ever printing the key. **Import from Warp**
  copies Warp's custom-endpoint keys; **Import from Claude Code / Codex** copies an API key out of
  `~/.claude/settings.json` or `~/.codex/auth.json` — an OAuth login is not an API key and is never
  imported.
- **Relay Free.** A fresh install with no key uses Relay Free, an included daily allowance served
  by a Relay-operated gateway that holds the provider keys (Main, Flash and Lite are GLM,
  DeepSeek and Gemini through OpenRouter to start). It is one more provider row: pick any other
  provider and it is out of the path. Prompts, the conversation and tool results go to the gateway
  and on to the provider; the gateway keeps request metadata only. `docs/RELAY-FREE.md` has the
  details and the quotas.
- **No telemetry.** No analytics, crash reports or account. On your own key Relay connects only to
  the provider you configure, when you use the agent; on Relay Free, only to Relay's gateway.
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

AGPL-3.0-or-later. See `LICENSE`. The Affero clause is there for the hosted parts: anyone who runs
a modified Relay gateway or rendezvous as a service for others must publish their changes. Qt and,
when present, KSyntaxHighlighting are external dependencies under their own licenses.
