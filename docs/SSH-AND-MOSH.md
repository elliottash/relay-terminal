# SSH and mosh in Relay

Owner decisions, 2026-09-18 (card #S5SH):

- **Relay wraps `ssh` automatically** in its own pane shells, and Options can turn that down to
  "ask per host" or off.
- **The pane's agent may always run commands on the host the pane is ssh'd into**, over the user's
  own authenticated connection (OpenSSH connection sharing). No second password, no second 2FA.
- **While ssh sits at a remote prompt, the agent's reply prints into the terminal**, as it does at a
  local prompt. The side panel ("output will also print in the terminal when ssh exits") is kept
  only for full-screen programs and programs that are mid-output.

Research that shaped this (Warp's documentation and issues, kitty, Ghostty, WezTerm, iTerm2, Wave, VS Code,
Zed, Claude Desktop, Codex) is summarised at the end.

## What was wrong before

While `ssh` owned the terminal:

1. A command typed in the prompt box never reached the remote shell. The router checked it against
   the *local* PATH, then queued it to run *locally* after ssh exited, or sent it to the agent.
2. A remote `sudo` password prompt was not treated as a password prompt (the local tty is raw, so
   the termios check cannot see it). A password typed into the prompt box could be routed to the
   model.
3. The agent's reply went to a side panel until ssh exited, however idle the remote prompt was.
4. OSC 7's hostname was dropped by the engine; a remote `cd` into a path that also exists locally
   moved the pane's local directory, and clickable paths in remote output opened local files.
5. The agent was told only "a program is running". It ran its own `ssh -o BatchMode=yes`, which
   fails for any key with a passphrase, 2FA, or a host that needs the user's agent.

## The pieces

### 1. The wrapper (`shell/integration.bash`)

Relay's pane shell defines `ssh()` and `mosh()` when `RELAY_SSH_WRAP=1` (set by the GUI from
`ssh/enhance` ≠ `off`). They never change what the user asked for; they only add connection
sharing so the agent can reuse the login:

- `ssh`: adds `-o ControlMaster=auto -o ControlPath=$RELAY_SSH_DIR/%C -o ControlPersist=600` —
  unless `ssh -G` shows the user already configured `ControlMaster`/`ControlPath` for that host (then
  their master is used as is), or the arguments ask for something the options would break (`-O`,
  `-S`, `-M`, `-G`, `-V`, `-Q`, `-W`).
- `mosh`: adds `--ssh="ssh -o ControlMaster=auto -o ControlPath=… -o ControlPersist=600"` unless
  `--ssh` is given. mosh's own ssh exits after starting mosh-server; `ControlPersist` keeps the
  master alive so the agent can still reach the host. mosh's default way of learning the server's
  address, `--experimental-remote-ip=proxy`, passes `-S none` to ssh (sharing off) and cannot work
  over an existing master anyway (its proxy never runs), so the wrapper also adds
  `--experimental-remote-ip=remote` (the address from `$SSH_CONNECTION` on the server) when no mode
  is given, and leaves mosh alone when the user asks for `proxy`.
- A destination named after `--` (`ssh -- filly`) is still a destination; a lone `-` is not, and
  mosh's own valued options (`-p`, `--port`, `--client`, …) are read before the destination, so
  `mosh host --ssh=x` — where `--ssh=x` is the remote command — still shares.
- Sockets of masters that were killed outright are swept once per run, before the first pane's
  shell: Relay connects to each one and removes only those nothing answers on
  (`relay::remote::pruneSockets`). Nothing else ever removed them, and under the `/tmp` fallback
  they would outlive the machine's uptime.
- `RELAY_SSH_DIR` is `$XDG_RUNTIME_DIR/relay-ssh` (mode 0700, created by the GUI). `%C` keeps the
  socket path short. The wrappers only use it when it exists, is absolute, is at most 48 bytes (a
  Unix socket path is 108, and ssh adds `/`, 40 for `%C` and a 17-byte temporary suffix) and has no
  character ssh's `%` expansion or mosh's word splitting would read (`[-A-Za-z0-9_.+@/]` only).
- `command ssh` bypasses it, as with any shell function — `\ssh` does not, because a backslash
  suppresses an alias, not a function.
- Asking `ssh -G` what the user configured is not free: it runs their `Match exec` hooks (a VPN
  probe, a token touch) and can resolve names, so it is asked only when their configuration
  mentions `ControlMaster`, `ControlPath`, `ControlPersist` or `Match` at all (`Include`s are
  followed one level, and a `-F` file on the command line is always read),
  under `timeout 5`, and the answer is kept for the rest of that shell. A user with none of those
  keywords — most users — pays nothing.
- mosh's shared connection needs `--experimental-remote-ip=remote`, which reads the server's own
  idea of its address. That address is unreachable for a host behind NAT or a forwarded port, so a
  shared mosh that dies within twenty seconds is run again exactly as the user typed it, with one
  line saying so. An explicit `--ssh` or `--experimental-remote-ip` is always left alone.

### 2. The GUI's picture of a remote session (`src/Pane.h`)

When the foreground program is `ssh`/`mosh`/`mosh-client` (after 300 ms, as before), the pane runs
`ssh -G <the program's own arguments>` once per login (capped at three seconds, since the user's
`Match exec` hooks run inside it), and keeps a
`RemoteLogin` (`Pane::beginLogin`, rules in `src/RemoteSession.h`): the destination as typed,
resolved hostname, user, port, control path, whether that socket exists (`reachable`), the remote
cwd (any OSC 7 while the login runs), whether the remote shell integration is live (OSC 133 marks
seen while the login runs), and whether the remote shell is at its prompt. mosh and mosh-client
are reduced to the ssh arguments mosh gave its own ssh, with Relay's socket directory added when
they name none (the wrapper's).

"At its prompt" is decided by OSC 133 marks when the integration is live (last mark `A`/`B`), and
otherwise by the screen classifier: `ScreenPrompt::ShellPrompt` on the cursor row, the cursor at the
end of it, no alternate screen, twice in a row (about half a second). While Relay's own inline
output is on the cursor row the prompt is taken to still be there, and the screen classifier is not
consulted, so an agent line ending in "password:" can never mask the prompt box.

The engine gained two things for this: `TerminalBackend::onCwdHostChanged(path, host)` (OSC 7's host
was parsed and then dropped) and `TerminalBackend::cursorPosition()`. Outside a login, an OSC 7 whose
host is not this machine no longer moves the pane's directory either (ssh inside a local tmux).

### 3. The remote integration (`shell/remote-integration.sh`)

A small bash/zsh script: OSC 7 with the real hostname, OSC 133 A/B/C/D, and a `Ctrl+X Ctrl+P` no-op
binding so Relay can ask the remote line editor to redraw its prompt. The host's own name is
stripped to a host name's characters before it goes into OSC 7, so a name with an escape in it
cannot close the sequence early and hand the terminal one of the host's choosing (the iTerm2
`it2ssh` class, in the research notes). It installs nothing and edits
no file. Relay types it into the remote shell once per login as one line with a leading space
(kept out of history by `HISTCONTROL=ignorespace`/`HIST_IGNORE_SPACE`, which the script also sets;
bash deletes the entry it already read, and zsh, which cannot, keeps it out of the file on the host
with `HISTORY_IGNORE` instead):

    ␠eval "$(printf %s '<base64 of gzip of RELAY_R=<rows> and the script>' | base64 -d | gzip -dc)"

What is typed is the script without its comments or blank lines (the GUI strips them; the file
keeps them), which is what holds the line under two kilobytes. The row count travels inside the
payload rather than in front of the line: a shell that is neither
bash nor zsh must at least be able to parse what Relay types, or it answers by printing the two
kilobytes back at the user. fish reads `eval "$(…)"` and then fails on the sh inside it, briefly.

The script first erases the rows the typed line and the old prompt took, so the session looks as if
the prompt had simply been redrawn. Nothing happens on shells other than bash and zsh, or in a
non-interactive one. A second eval in the same shell only erases its own line. In bash the typed
line is also taken out of the history if it got in; zsh decides that when the line is read, so the
line stays in a zsh history that did not already have `HIST_IGNORE_SPACE`.

**Inside a remote tmux or screen** none of this would reach Relay: a multiplexer passes on only
what it understands itself. At load the script looks at `$TMUX`, then at `$STY` and a `screen*`
`TERM`, and decides once — a prefix and a suffix, so the prompt pays nothing per redraw — how to
wrap everything it sends:

- tmux: `\033Ptmux;` … `\033\\`, with every ESC of the inner sequence doubled. Everything the
  script sends is one ESC and an OSC ending in BEL, so the doubling is one more ESC in the prefix:
  `ESC P t m u x ; ESC` `ESC ] 1 3 3 ; A BEL` `ESC \`.
- screen: `\033P` … `\033\\`, no doubling. screen's string buffer is finite, so an OSC 7 whose
  body would pass 200 bytes (a deep remote directory) is skipped rather than sent cut in half; the
  next `cd` into a shorter path sends one again.

tmux forwards a wrapped sequence only with **`set -g allow-passthrough on`** in the user's tmux
(3.3 and later; off by default in 3.4). Without it the sequence is dropped, exactly as the
unwrapped one was — nothing is worse, but nothing works — so when `tmux show -gv allow-passthrough`
does not say it is on, the script prints one line at load, under the erase:

    relay: tmux needs "set -g allow-passthrough on" for prompt marks

Once per login, never when the option is on, and never under screen, which needs no option.

OSC 7 still carries the host's own name inside tmux, which is what Relay wants. The erase is
ordinary CSI, which tmux understands and applies to its pane: the typed line disappears there as it
does without tmux, as long as the tmux pane is as wide as Relay's — `RELAY_R` counts rows at
Relay's width, so in a pane narrowed by a tmux split part of the echoed line stays on screen, once,
at the top of the login. A tmux started *after* the login was enhanced runs a shell that never saw
the script and gets nothing from it: the functions and variables are not exported, and a
`PROMPT_COMMAND` the user had exported keeps its value but loses its export, since in that new
shell it would name a function that does not exist. `tmux -CC` control mode is still not handled
(section 9).

For the GUI: the payload must be gzip (RFC 1952), not `qCompress`'s zlib with a length prefix —
`relay::remote::gzip` takes the raw deflate out of `qCompress` and puts gzip's header and CRC-32
around it (`tests/remotesession_test.cpp` decodes it with the real `gzip`) — and base64 on one line
without wrapping; `base64 -d` needs macOS 13
or later there (older macOS spells it `-D`). About 3.5 KB of script becomes about 2 KB typed, in one
write into a pty that holds 4 KB. `tests/test_ssh_shell.py` types exactly this line into bash and
zsh on a pty, into a bash and a zsh that believe they are in tmux or screen, and into a bash inside
a real tmux with `allow-passthrough` both on and off, reading the pane's own bytes back with
`tmux pipe-pane`; it also types it into a real zsh over `ssh -t localhost`, which
`docs/qa_evidence/2026-09-18-ssh-and-mosh-sessions/tmux-zsh-check.py` repeats by hand, with the
screen each case leaves behind.

If the remote shell already binds `Ctrl+X Ctrl+P`, or holds `PROMPT_COMMAND` read-only, the script
says so in one line and installs nothing: half an integration is worse than none, and Relay must
never press a key that runs a command of the user's. Such a login keeps working through the screen
classifier, as an un-enhanced one does.

When: `ssh/enhance` = `auto` → at the first remote prompt of each login, unless the host is in
`ssh/hosts_never`. `ask` → a banner offers "Enhance" for this login (hosts in `ssh/hosts_always` are
enhanced without asking; both lists are edited in Options). `off` → never, and no wrapper. mosh
drops unknown OSC sequences, so a mosh session is never enhanced; it still gets everything the
screen classifier can give (items 4–6).

### 3b. The alternate screen is not the end of a login

mosh-client draws on the alternate screen for its whole life, and a tmux or screen on the host takes
it as soon as it starts. Relay used to read that as "a full-screen program owns the terminal", stop
taking lines, and queue what the user typed **for the local shell** — the original bug, one level
deeper. So while a login runs, the alternate screen decides nothing by itself:

- the login is still detected and still takes lines, but on the alternate screen only when the
  remote shell is at a prompt, so a line can never land in vim;
- what is at a prompt is decided by the cursor's own row (`relay::screen::isShellPrompt`), because
  the bottom row there is tmux's status bar, and because the marks of the shell Relay enhanced
  describe the screen underneath, not the one being drawn — the last of them is the "command
  started" of whatever opened it;
- a command typed while a full-screen program really does hold the terminal is not sent and not
  queued: it stays in the prompt box, and the pane says which program has the keyboard;
- the agent's reply goes to the side panel, never into the alternate screen, because mosh and tmux
  both repaint it from their own copy and would paint over it.

### 4. The prompt box types into the remote shell

At a remote prompt, Enter sends the route request with `remote: {"host": …}`. The router then skips
the local PATH/alias checks (they describe the wrong machine) and decides only between "a shell
command" and "a request for the agent". A shell command is typed into ssh followed by Enter, and the
prompt box keeps it in history as usual. `/agent` and `/shell` prefixes still force the destination.

The same holds while the remote side is busy (no prompt, no full-screen program): the line is typed
ahead into ssh, as it would be at a real keyboard, and a toast says the host was busy. It is never
queued for the local shell. A question read off the remote screen ("Do you want to continue?
[Y/n]") takes the line directly, without the router. A full-screen remote program (vim, htop) keeps
the Take control banner.

For SSH, the first ready remote prompt also hands program input to the agent automatically when
an agent is configured and the pane can show it the screen. The banner then offers Take over. A
password prompt revokes that grant, and taking over is final for that login; later prompts do not
hand it back. A per-program choice to take human control of `ssh` still wins.

### 5. Remote passwords

A masked prompt read off the screen (`Password:`, `[sudo] password for`, `Enter passphrase`) while a
remote program is in the foreground puts the prompt box into the same masked mode as a local
password prompt: what is typed goes to ssh, never to the router or the model, and is wiped.

### 6. The agent's reply at a remote prompt

`printInline` treats "remote shell at its prompt" like "local shell idle at its prompt": it erases
the prompt row, prints, and on close asks the remote shell to redraw (`Ctrl+X Ctrl+P` when the
integration is live), or re-prints the prompt row's text it saved before erasing. Resize is held for
the program while the block is open, as for the local shell.

Without the integration the prompt comes back in its own colours: Relay keeps the bytes the host
wrote since its last newline (the pane's `onOutput`, only while a login runs), and
`relay::remote::promptEcho` strips them to text and SGR — every other escape, OSC and control byte
is dropped, so nothing the host sent can drive the terminal when it is printed back. Those bytes are
used only when the text in them ends exactly where the cursor is; a prompt drawn with cursor moves
(zsh's right-hand prompt) falls back to the screen's own text, padded to the cursor column, which is
plain but always the right width.

Anything printed while the remote side is busy (a command running on the host) still goes to the
side panel and into the terminal at the next remote prompt, not only when ssh exits.

**mosh.** mosh-client draws the remote screen on the alternate screen for its whole life and
repaints it from the server's copy, so two things differ. The alternate screen is not taken as "a
full-screen program" while mosh-client owns it (`Pane::onPrimaryScreen`); the login, the prompt box
and the agent's `host` work as with ssh, the prompt found from the screen. And the agent's reply
stays in the side panel: a line Relay wrote into mosh's screen would be painted over by the next
diff from the server. mosh execs `mosh-client "-# <the original arguments> |" IP PORT` (one
argument, read from `/proc` with mosh 1.4); both `src/RemoteSession.cpp` and
`relay::panestatus::remoteHost` read the destination and the wrapper's `--ssh` options from it.

### 7. The agent works on the host

The ask context gains `remote_session`:

    {"program": "ssh", "host": "filly", "hostname": "65.109.126.152", "user": "elliott", "port": 22,
     "control_path": "/run/user/1000/relay-ssh/956d…", "reachable": true,
     "cwd": "/srv/archive/tracelaw", "shell_integration": true, "at_prompt": true}

`run_command` takes an optional `host`. When it equals `remote_session.host` and the session is
`reachable`, the command runs as

    ssh -S <control_path> -o ControlMaster=no -o BatchMode=yes -o ConnectTimeout=10 -T <host> -- \
        'cd <cwd> || exit 1
    <command>'

reusing the user's login. Any other host is refused with a message the model can act on. The system
prompt tells the model that plain `run_command` runs on the local machine and `host` runs on the
machine the user's terminal is on. `cwd` defaults to the remote cwd when known, else the remote home.

**The file tools take the same `host`** (owner, 2026-09-18: the agent should edit files on the host
as comfortably as locally, with nothing installed there). `read_file`, `list_directory`,
`write_file` and `edit_file` are offered with `host` for the same turns `run_command` is, validate
it the same way, and refuse with the same actionable messages; `path` is then a path on the host.
Each one is a small POSIX script (`backend/relay_core/remote_files.py`) sent through the same argv
builder inside `sh -c`, so a login shell that is bash, zsh, dash or ksh means the same thing by it:

- **read**: `cat -- "$p" | head -c 131073`, then the local tool's own refusals — over 128 KiB, a
  NUL byte, not UTF-8, not a regular file, a symlink. There are no line ranges because the local
  `read_file` has none.
- **list**: a `for` loop over `*` and `.*` printing `type\0name\0`, one entry past the local cap of
  200, so `truncated` means what it does locally. NUL separators: a newline in a name cannot lie.
- **write and edit**: the new content goes over ssh's **stdin** — never in the argv, where its size
  and its quoting would both be a problem — into `"$p".relay-new.$$` beside the target, which takes
  the target's mode (`chmod --reference`, falling back to `cp -p` on a host whose `chmod` is not
  GNU's), and `mv` puts it in place. A half-written file is never visible, and a failed write leaves
  the original untouched. Parent directories are not created, exactly as locally.
- **edit** does its matching here, in Python, with the same `_edited()` the local `edit_file` uses:
  the file is read over the connection, matched, and written back. "old_string must be unique",
  "not found in the file", `replace_all` and every message are therefore identical to the local
  tool's, and the user sees a real diff of the remote file before the write. The write re-reads the
  file and compares its SHA-256 first, so a file that changed under the diff is not overwritten.
  There is no checkpoint/undo for a remote write (Relay's checkpoints are workspace files).

**The rule that replaces the workspace.** Locally the file tools are confined to the workspace and
refuse secret-looking paths. On the host there is no workspace, and a read and a write do not
deserve the same rule (owner, 2026-09-18, after watching `/etc/nginx/nginx.conf` be refused: "i
agree, the agent can read anything"):

- the **same secret-file guard** applies to both (`.ssh`, `.gnupg`, `.git`, `.env`/`.env.*`,
  `id_rsa`, `id_ed25519`, `*.pem`, `*.key`), and `..` is refused, both checked here before any ssh
  runs. That rule is about credentials, not about how far the agent may reach;
- **a read goes anywhere**: `read_file` and `list_directory` with `host` read whatever the user's own
  account can read — `/etc`, `/var/log`, a colleague's checkout. A read is already bounded by the
  remote user's permissions, and `run_command host` with `cat` could fetch the same bytes, so
  fencing `read_file` alone only made the tools inconsistent with each other;
- **a write stays inside the user's home on the host, or under `remote_session.cwd`** (the directory
  their own shell is in — a deploy in `/srv` is what they are logged in to work on). A write is the
  one that can damage a machine nobody asked the agent to touch, and there is no undo for it. `$HOME`
  is only known on the host, so a write's script opens with the path `~`-expanded and made absolute
  against the remote `$PWD`, then a `case` that exits 78 otherwise; the read a write does first, to
  build its diff, is fenced with it, so a refused write reads nothing;
- **symlinks are not followed**, as locally. The write's containment check is textual, so a directory
  symlink inside the home that points elsewhere is not caught: this is a guard against damage nobody
  asked for, like the workspace check, not a sandbox. The user's own permissions still bound
  everything, and nothing runs as root over this connection.

Searching stays with `run_command host` (`grep`, `find`, `ls`): there is no local `glob`/`grep`
tool to give a `host` to, and the context note says so. Every remote file result carries `host`, and
the tool-call line reads "read nginx.conf on filly", "wrote app.conf on filly" (card #TK9C) — and
clicking it folds the detail open rather than opening a local file of the same name.

### 8. Getting there, and elsewhere

- **Actions › Connect to SSH…**: one action opens a searchable modal with concrete `Host` aliases
  from `~/.ssh/config` and its `Include`s, plus recently used hosts. Choose a host or enter a new
  hostname or `user@host`, then Connect opens a new tab running `ssh <target>`. Host rows stay out
  of the Actions list. Typing `ssh <host>` or `user@host` in Actions still offers a direct connection.
- **Split on the same host**: a split that runs the same ssh command line; with connection sharing
  it opens without a login.
- **Options › Terminal › SSH sessions**: `auto` / `ask` / `off`, and the never-enhance host list.
- The pane chrome's remote chip and hatched backdrop (card #SPBN) already mark a remote pane.

### 9. The host's files: clicking one opens it, editing it saves it back

Owner, 2026-09-18: "editing allowed so it's equal to local text editing."

A path printed by the host names one of the host's files. Clicking it opens that file in the same
preview pane a local file opens in, fetched over the connection the user already has, and the pane
can edit it and write it back. `src/RemoteFiles.{h,cpp}` (library `relay-remotefiles`,
`tests/remotefiles_test.cpp`) is the whole of it; `src/FilePanes.cpp` is the pane.

**Which paths are the host's.** The engine only offered a path as a link when it existed *here*
(`links::scan` with `links::systemProbe()`), which under a login is the wrong machine twice over:
most of the host's paths were not links at all, and one that happened to exist here was a link to
the wrong file. `TerminalBackend::setLinkProbe(probe, directory)` lets the pane answer instead.
While a login is reachable, `Pane::remoteLinkProbe` answers from `relay::remote::PathProbe`: a
cache the host fills, one `for p in …; do test -d/-e; done` per two dozen candidates over the same
socket, at most one batch in flight and 150 ms between batches (sshd counts sessions — Warp's
#1957), at most 200 queued and 4000 remembered, all of it dropped when the login ends. The
directory relative paths resolve against becomes the remote cwd (OSC 7), not this process's.

A probe is called from a mouse-move, so it can never wait: a path the host has not answered for
yet is "nothing there", and the underline appears when the batch lands (`linkProbeAnswered()` →
the view re-reads the cell under the pointer). The first Ctrl+Shift+L over brand new remote output
can therefore come up empty; the second press has the answers.

**A folder, an image, a PDF.** A remote file opens the way a local one does: Markdown renders,
an image is shown, a PDF is paged, anything else is text, and a rendered file is still editable —
the bytes decide, not the machine they came from. A clicked folder opens the explorer pane on the
host's folder (`ssh://<host>/<path>/`, the trailing slash decided on the host, since this machine
cannot be asked what a remote path is): it lists, walks into subfolders and opens files from there,
and it does not rename, delete or create.

**Opening.** `Pane::openRemoteOutputPath` turns the clicked path into `ssh://<host>/<path>` and
hands it to `onOpenPath` like any other file. `RelayWindow::openPath` treats it as a file (never a
folder — folders on the host are not browsable yet, and a click on one says so), and
`FilePreview::open()` recognises the URL. The pane is titled `filly:/etc/nginx/nginx.conf`, in the
pane header and in the tab, with a chip naming the host next to the title; ↗ and "Open externally"
are off, because nothing on this machine can open a file that is not on it. The clicked line is
kept until the bytes arrive and the pane then goes to it.

Because the link was only a link at all when the host vouched for the path, a click during a login
is unambiguously the host's file. Files opened before the login keep their panes and stay local.

**Fetching.** One `sh -c` script over
`ssh -S <control path> -o ControlMaster=no -o BatchMode=yes -o ConnectTimeout=10 -T <host>`:
refuse a folder, refuse what cannot be read, `stat -c %s:%Y:%a` (with the BSD `stat -f %z:%m:%Lp`
as a fallback), print that line, refuse anything over 8 MB by its size, then `cat`. Everything
after the first newline is the file. A NUL in the first kilobyte means binary, and Relay says so
rather than opening it as text. Every path is quoted as one POSIX word
(`relay::remote::shellQuote`, tested against spaces, quotes, `$`, backticks, newlines and unicode
— and against a real `sh`).

**Saving.** Ctrl+S or Save, the same as the editable Markdown pane: a `●` marks unsaved edits in
the header and the tab, the header says "Saving to filly…" and then "Saved to filly · 17:47". The
bytes go over ssh's **stdin**, never in an argument where the host's process table would show
them. The script compares the file's current `stat` with the one the fetch saw *on the host*,
between the check and the write, and refuses with `exit 20` if anything else has touched it —
size, mtime or mode. The pane then offers Overwrite anyway / Reload from the host / Cancel, and
the edits survive all three. A save writes `mktemp` beside the file, `chmod --reference`s it and
`mv`s it into place, so the file keeps its mode and is never seen half-written; a folder that
cannot hold the temporary file, a read-only filesystem, a full disk and a permission denied each
come back as the host's own words in the pane.

**When the login ends.** The record of a live login is `relay::remote::announceLogin` /
`forgetLogin`, and the control socket is checked as well as the record. A save with no connection
keeps the buffer and says so: "The connection to filly has ended · your edits are safe in this
pane. Log in to filly again in the terminal pane and press Ctrl+S, or copy the text out." The pane
then watches for the host to come back and says when it has. Nothing is written anywhere else and
nothing is thrown away. (`exit` on its own does not end the connection: `ControlPersist` keeps the
master for ten minutes and a save still works over it.)

The agent reaches the host's files through its own tools; this is the GUI's path and shares no
code with it.

### 10. Not done, on purpose

- No `tmux -CC` control-mode integration and no remote helper binary (Warp's SSH extension, VS Code
  server): heavy, per-architecture, and Warp's own tmux experiment was removed after it broke users'
  tmux. A plain remote tmux or screen is served by the DCS wrapping of section 3 instead, which
  installs nothing and asks the user for one line of tmux configuration.
- No OSC passthrough under mosh: mosh drops unknown sequences upstream.
- No editing of the host's folders: an explorer pane on a remote folder lists and opens, it does
  not rename, delete or create (section 9).

## Research notes

- **Warp** types a bootstrap into the pty after login (detecting the prompt, re-checking after 3 s),
  hex-encodes its hook payloads in DCS/OSC, and now ships a remote server over its own
  ControlMaster. Its longest-running SSH bug (#1957, "channel open failed") came from multiplexing
  background work over the user's connection past sshd's `MaxSessions`; its tmux wrapper was
  removed; mosh is unsupported (#268). Detection by command-text regex misses aliases.
- **kitty** ships integration and terminfo in-band with a one-time password checked before
  accepting the request; **iTerm2**'s `it2ssh` accepted conductor messages from any output and was
  exploitable by `cat readme.txt` — accept nothing that grants power from the output stream.
- **Ghostty** wraps `ssh` in a shell function for terminfo/env, and caches per `user@host`.
- **Claude Desktop, Codex, Zed, VS Code** run a server on the remote; the lighter pattern that fits
  Relay is kitty's and Warp's: reuse the user's authenticated connection with `ssh -S`.
- **mosh** syncs the screen, not the byte stream: OSC 7/133/1337 never arrive; only titles and OSC 52.

## SSH parity and guest tools (#S7GX, #S7KC, #S7CX)

The SSH badge and host/cwd chip describe the connection. The command activity line describes only
an actual remote command; an idle SSH shell has no running-command line. Composer commands enter
the normal queue, not a busy program's stdin. Use Take control for raw program answers. Remote
Bash/zsh hooks preserve status, insert the same prompt/output spacing as the local shell and mark
echoed rows (character-count wrap estimate, as locally). Completion records, failure handling,
watch/fix and terminal handoff finish at each remote command, rather than at disconnect.

Relay confirms its own shell hooks with a per-login nonce, separately from third-party OSC 133
marks. Each command suspends shared-host capabilities until that shell confirms its next prompt.
A nested SSH/tmux shell cannot inherit that confirmation: host tools and remote completion are
unavailable there, rather than accidentally using the outer connection. Take control still works.
This is conservative: tools are also unavailable while the visible remote shell is busy or when
Relay enhancement is off. Returning to the confirmed shell restores them. Context changes reach
an active worker through `remote_session_update`, including revocation on exit.

Tab and @ completion query the confirmed host using bounded asynchronous SSH processes. Replies
are discarded after edits, cd or reconnect; local files and commands never supply fallback
candidates in SSH. @ attachments carry an explicit host, are read on that connection, and identify
their source in the prompt. Remote command suggestions are scoped to host/cwd. The local project
and agent workspace remain local. Remote Bash is required for completion; unsupported hosts
report unavailable/failure instead of silently listing local paths.

The guest MCP bridge exposes run_command, read_file, list_directory, write_file and edit_file with
**required host**, plus command_output, stop_command and the per-turn run_in_terminal handoff.
Discovery is stable even before login; execution validates current capabilities on every call.
Commands return jobs within ten seconds. Native preparation, approvals and cancellation still
apply. A changed SSH context between preparation and execution refuses the call. Both GUI file
operations and backend tools set ProxyCommand=false alongside ControlMaster=no: if the existing
socket dies, OpenSSH cannot fall back to a fresh authenticated network connection.
