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
`RemoteSession`: alias, resolved hostname, user, port, control path, whether the master answers
`ssh -O check`, the remote cwd (from OSC 7 with a non-local host), whether the remote shell
integration is live (OSC 133 marks seen), and whether the remote shell is at its prompt.

"At its prompt" is decided by OSC 133 marks when the integration is live (last mark `A`/`B`), and
otherwise by the screen classifier (`ScreenPrompt::ShellPrompt`, cursor on that row, no alternate
screen, output quiet).

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

For the GUI: the payload must be gzip (RFC 1952, e.g. zlib with `windowBits` 31 — not `qCompress`,
which is zlib with a length prefix), base64 on one line without wrapping; `base64 -d` needs macOS 13
or later there (older macOS spells it `-D`). About 2.5 KB of script becomes about 1.5 KB typed.
`tests/test_ssh_shell.py` types exactly this line into bash and zsh on a pty.

When: `ssh/enhance` = `auto` → at the first remote prompt of each login, unless the host is in
`ssh/hosts_never`. `ask` → the program bar offers "Enhance" / "Always on this host" / "Never on this
host". `off` → never, and no wrapper. mosh drops unknown OSC sequences, so a mosh session is never
enhanced; it still gets everything the screen classifier can give (items 4–6).

### 4. The prompt box types into the remote shell

At a remote prompt, Enter sends the route request with `remote: {"host": …}`. The router then skips
the local PATH/alias checks (they describe the wrong machine) and decides only between "a shell
command" and "a request for the agent". A shell command is typed into ssh followed by Enter, and the
prompt box keeps it in history as usual. `/agent` and `/shell` prefixes still force the destination.

### 5. Remote passwords

A masked prompt read off the screen (`Password:`, `[sudo] password for`, `Enter passphrase`) while a
remote program is in the foreground puts the prompt box into the same masked mode as a local
password prompt: what is typed goes to ssh, never to the router or the model, and is wiped.

### 6. The agent's reply at a remote prompt

`printInline` treats "remote shell at its prompt" like "local shell idle at its prompt": it erases
the prompt row, prints, and on close asks the remote shell to redraw (`Ctrl+X Ctrl+P` when the
integration is live), or re-prints the prompt row's text it saved before erasing. Resize is held for
the program while the block is open, as for the local shell.

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

### 8. Getting there, and elsewhere

- **Connect to a host** (palette, `ssh/` hosts): every concrete `Host` in `~/.ssh/config` and its
  `Include`s, plus recently used hosts. Opens a new tab running `ssh <alias>`.
- **Split on the same host**: a split that runs the same ssh command line; with connection sharing
  it opens without a login.
- **Options › Terminal › SSH sessions**: `auto` / `ask` / `off`, and the never-enhance host list.
- The pane chrome's remote chip and hatched backdrop (card #SPBN) already mark a remote pane.

### 9. Not done, on purpose

- No `tmux -CC` integration and no remote helper binary (Warp's SSH extension, VS Code server):
  heavy, per-architecture, and Warp's own tmux experiment was removed after it broke users' tmux.
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
