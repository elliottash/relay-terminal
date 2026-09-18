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
- `RELAY_SSH_DIR` is `$XDG_RUNTIME_DIR/relay-ssh` (mode 0700, created by the GUI). `%C` keeps the
  socket path short. The wrappers only use it when it exists, is absolute, is at most 48 bytes (a
  Unix socket path is 108, and ssh adds `/`, 40 for `%C` and a 17-byte temporary suffix) and has no
  character ssh's `%` expansion or mosh's word splitting would read (`[-A-Za-z0-9_.+@/]` only).
- `command ssh` / `\ssh` bypass it, as with any shell function.

### 2. The GUI's picture of a remote session (`src/Pane.h`)

When the foreground program is `ssh`/`mosh`/`mosh-client` (after 300 ms, as before), the pane runs
`ssh -G <the program's own arguments>` once, locally and without network, and keeps a
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
binding so Relay can ask the remote line editor to redraw its prompt. It installs nothing and edits
no file. Relay types it into the remote shell once per login as one line with a leading space
(kept out of history by `HISTCONTROL=ignorespace`/`HIST_IGNORE_SPACE`, which the script also sets):

    ␠RELAY_R=<rows> eval "$(printf %s '<base64 of gzip of the script>' | base64 -d | gzip -dc)"

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

When: `ssh/enhance` = `auto` → at the first remote prompt of each login, unless the host is in
`ssh/hosts_never`. `ask` → a banner offers "Enhance" for this login (hosts in `ssh/hosts_always` are
enhanced without asking; both lists are edited in Options). `off` → never, and no wrapper. mosh
drops unknown OSC sequences, so a mosh session is never enhanced; it still gets everything the
screen classifier can give (items 4–6).

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
refuse secret-looking paths. On the host there is no workspace, so rather than dropping the
protection:

- the **same secret-file guard** applies (`.ssh`, `.gnupg`, `.git`, `.env`/`.env.*`, `id_rsa`,
  `id_ed25519`, `*.pem`, `*.key`), and `..` is refused, both checked here before any ssh runs;
- the path must be **inside the user's home on the host, or under `remote_session.cwd`** (the
  directory their own shell is in — a deploy in `/srv` is what they are logged in to work on).
  `$HOME` is only known on the host, so this is the opening of every script: the path with `~`
  expanded and made absolute against the remote `$PWD`, then a `case` that exits 78 otherwise;
- **symlinks are not followed**, as locally. The containment check is textual, so a directory
  symlink inside the home that points elsewhere is not caught: this is a guard against accidents,
  like the workspace check, not a sandbox. The user's own permissions still bound everything, and
  nothing runs as root over this connection.

Searching stays with `run_command host` (`grep`, `find`, `ls`): there is no local `glob`/`grep`
tool to give a `host` to, and the context note says so. Every remote file result carries `host`, and
the tool-call line reads "read nginx.conf on filly", "wrote app.conf on filly" (card #TK9C) — and
clicking it folds the detail open rather than opening a local file of the same name.

### 8. Getting there, and elsewhere

- **Connect to a host** (palette, `ssh/` hosts): every concrete `Host` in `~/.ssh/config` and its
  `Include`s, plus recently used hosts. Opens a new tab running `ssh <alias>`.
- **Split on the same host**: a split that runs the same ssh command line; with connection sharing
  it opens without a login.
- **Options › Terminal › SSH sessions**: `auto` / `ask` / `off`, and the never-enhance host list.
- The pane chrome's remote chip and hatched backdrop (card #SPBN) already mark a remote pane.

### 9. Not done, on purpose

- No `tmux -CC` control-mode integration and no remote helper binary (Warp's SSH extension, VS Code
  server): heavy, per-architecture, and Warp's own tmux experiment was removed after it broke users'
  tmux. A plain remote tmux or screen is served by the DCS wrapping of section 3 instead, which
  installs nothing and asks the user for one line of tmux configuration.
- No OSC passthrough under mosh: mosh drops unknown sequences upstream.
- Remote file clicks open nothing (rather than the wrong local file); fetching them over the shared
  connection (kitty's `remote_file`) is a follow-up.

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
