---
id: S5SH
type: work
status: needs-qa-llm
labels: [feature]
component: [gui, shell-integration, agent, router]
milestone: desktop-alpha
workstream: terminal
rank: zzzzzk
assignee: agent
implemented_by: Claude Opus 5 (Claude Code session relay-terminal-be), 2026-09-18
created: '2026-09-18'
acceptance: in a recorded run against a real host, the prompt box types a command into the ssh session, the agent's reply at the remote prompt prints into the terminal, a remote sudo prompt masks the prompt box, and the agent runs a command on the host over the user's connection without a second login
source: owner, in a Claude Code session, 2026-09-18, after the agent's reply went to the side panel while ssh sat at a remote prompt
links: {plans: ['docs/SSH-AND-MOSH.md'], commits: ['6796340', 'd1a3bcb', '3f4614c', 'c2f6aae', '70d4b9a', '0e419fe', 'f4d5f5f', '8bb5b58', '53715ad'], evidence: ['docs/qa_evidence/2026-09-18-ssh-and-mosh-sessions'], related: ['SPBN', 'D8J3', 'C1HH'], github: null}
---
# SSH and mosh sessions: the prompt box, the agent and the reply work on the remote host

## Issue

> i just observed this issue: [screenshot: "Agent · glm-5.3 — output will also print in the
> terminal when ssh exits"] when i pressed enter twice to interrupt with a command, it started
> replying in the thinking bubble, instead of in the terminal

> do research on how warp terminal deals with ssh sessions, including for example looking at their
> github. see if there are other AI harnesses or terminals that could have helpful inputs. then
> help me build a nice set of ssh / mosh features in relay

## Decisions

Owner, 2026-09-18, asked in the session:

- Remote shell integration: "automatic wrapper by default, but you can disable that in the options
  menu (offer per host, or off)".
- The agent running commands on the ssh host over the user's connection: "Yes, always".
- The agent's reply while ssh sits at a remote prompt: into the terminal.

## Tasks

- [x] Wrapper: `ssh()`/`mosh()` add connection sharing in Relay pane shells (`shell/integration.bash`)
- [x] Remote integration script, typed once per login (`shell/remote-integration.sh`)
- [x] Engine keeps OSC 7's host; a remote `cd` no longer moves the local cwd (`TerminalBackend::onCwdHostChanged`, `cursorPosition`)
- [x] Pane: remote session model from `ssh -G`, remote prompt detection (`Pane::beginLogin`, `updateLoginPrompt`; rules in `src/RemoteSession.h`, `tests/remotesession_test.cpp`)
- [x] Prompt box types commands into the remote shell; router `remote` flag (`Pane::typeIntoLogin`)
  - [x] Router half: `route {remote: {host}}` decides shell vs agent by shape, never "not found" locally; `remote_host` on the decision (`backend/relay_core/router.py`, protocol section 24.1)
- [x] Remote password prompts mask the prompt box
- [x] Agent reply prints into the terminal at a remote prompt (`Pane::inlineReady`)
- [x] Agent context `remote_session`; `run_command` `host` over the shared connection (`backend/relay_core/remote_session.py`, protocol section 24.2–24.3; sent by `Pane::loginContext`)
- [x] Connect to host (palette, from `~/.ssh/config`), split on the same host
- [x] Options › Terminal › SSH sessions: auto / ask / off
- [x] Clickable paths in a remote pane do not open local files (a toast names the host; URLs still open)
- [x] A clicked path opens the host's file, editable, and Ctrl+S saves it back over the same
      connection; the engine's link probe answers from the host (`src/RemoteFiles.{h,cpp}`,
      `tests/remotefiles_test.cpp`, `TerminalBackend::setLinkProbe`, docs/SSH-AND-MOSH.md § 9)
- [x] The file tools take `host` too: read, list, write and edit files on the host over the same connection, content on ssh's stdin, temp+`mv`, mode preserved, inside the remote home or the shell's directory (`backend/relay_core/remote_files.py`, `tools.py`; protocol section 24.4; `tests/test_ssh_remote.py`)

Design: [`docs/SSH-AND-MOSH.md`](../../docs/SSH-AND-MOSH.md).

## QA checklist

Against a real host with key login (`ssh localhost` works on the owner's machine), Relay built from
main, Options › Terminal › SSH sessions on "Enhance automatically" unless a step says otherwise.

1. `ssh <host>` from the prompt box. Within a second of the remote prompt: the pane chip names the
   host, a "Logged in to <host>" toast, and no leftover bootstrap line on screen.
2. Type `ls -la` in the prompt box and Enter: it runs on the host (not queued, no "command not
   found" from the local router). A remote command whose name is not installed locally (e.g. one
   only the host has) also runs there.
3. Ask the agent something (Ctrl+Enter) at the remote prompt: the reply prints in the terminal under
   the prompt and the remote prompt comes back after it. No "output will also print when ssh exits"
   panel.
4. Run `sleep 15` on the host, ask the agent something meanwhile: the reply shows in the side panel,
   and prints into the terminal when the remote prompt returns (not only when ssh exits).
5. Ask the agent to check something on the host: it runs `run_command` with `host`, the line says
   "on <host>", and no password is asked.
6. On the host: `read -rsp "Password: " pw; echo ${#pw}` (or a real `sudo` with a password). The prompt
   box becomes the masked "password for ssh" field; the text reaches the host and is in no model
   request or log.
7. `cd /etc` on the host: the pane's local folder chip does not change. A path printed by the host
   that also exists locally opens the **host's** file when clicked (step 12), not this machine's.
8. Options on "Ask for each host": the Enhance banner appears; without it, steps 2–3 still work
   (prompt found from the screen, printed back after the reply). "Off": plain ssh, no banner, the
   agent says it cannot share the connection.
9. `mosh <host>` (if installed on both ends): steps 2, 3 and 5 work; no enhancement is attempted.
10. Actions › Connect to host…: hosts from `~/.ssh/config` and recent ones; choosing one opens a tab
    that logs in. In a logged-in pane, Split on the same host opens a split that logs in without a
    password.
11. At the remote prompt, ask the agent to read and then change a file in the remote home (e.g. add a
    line to `~/notes.md`): the lines read "read notes.md on \<host\>" and "edited notes.md on
    \<host\>", the fold shows a real diff of the remote file, the file on the host changes and keeps
    its mode, and no `.relay-new.*` file is left beside it. Ask it to read `/etc/nginx/nginx.conf`
    (or any file outside the home): it reads it. Ask it to *change* that file, and to read
    `~/.ssh/config`: both are refused in words the model can act on, and nothing is written.
12. Files on the host (§ 9). On the host, make a file of your own (`printf 'one\ntwo\n' >/tmp/t.conf;
    chmod 640 /tmp/t.conf`) and `ls /tmp/t.conf`. Hover the path it printed: it underlines about a
    fifth of a second later (it is a link because the host said so — a path that exists only here is
    not one, and one that exists only there is). Click it: a preview pane opens titled
    `<host>:/tmp/t.conf`, with a chip naming the host, and ↗ is off. Type a line: a ● appears in the
    pane header and the tab. Ctrl+S: "Saved to <host>", the ● goes, and `cat /tmp/t.conf` on the host
    shows the new bytes with the mode still 640 and no `.relay-save.*` left beside it. Change the file
    on the host behind Relay's back (`echo x >>/tmp/t.conf`), edit the pane again and Ctrl+S: it
    refuses with what changed and offers Overwrite anyway / Reload; both keep your text. Then end the
    connection (`exit`, and stop the master with `ssh -O exit <host>`) and Ctrl+S once more: the pane
    keeps the buffer and says the connection has ended. Click a *folder* the host printed: a toast
    says folders on the host are not opened. Evidence:
    `docs/qa_evidence/2026-09-18-ssh-and-mosh-sessions/implementer-remote-file-*`.
13. A `tmux` on the host (`tmux new -s x`): the prompt box still types into the shell inside it, and
    nothing typed there is ever queued for the local shell. Without `set -g allow-passthrough on`
    in the host's tmux, one line says so at login; with it, prompt marks work inside tmux. Open a
    full-screen program on the host instead (`vim`): a typed command is refused and kept in the box,
    naming the program, and Ctrl+H gives it the keyboard.
14. A host whose login shell is zsh: the integration loads and erases itself, and steps 2–3 hold. On
    a host with no `~/.zshrc`, zsh shows its first-run menu instead of a prompt: Relay must wait
    rather than type into it. Afterwards `fc -l` on the host must not show the line Relay typed.
15. The host's Markdown, an image and a PDF: each opens the way a local file of that type does, and
    a Markdown file still edits and saves back. A folder printed by the host opens the explorer pane
    on the host's folder and walks into subfolders.
16. Reading outside the home on the host (`/etc/hostname`) succeeds; writing there is refused with a
    reason, and a symlink (`/etc/os-release`) is refused rather than followed.
17. Break sharing on purpose (make `$XDG_RUNTIME_DIR/relay-ssh` unwritable, or set SSH sessions to
    Off) and log in: typing still works, and the pane and the agent both say the connection cannot
    be shared instead of failing silently.
18. A host in `~/.ssh/config` with `Match exec "…"`: its hook runs no more often than plain ssh
    does, and a hook that hangs does not hang the pane's shell (the wrapper caps it).

