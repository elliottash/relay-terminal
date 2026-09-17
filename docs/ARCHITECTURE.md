# Architecture and development notes

## Product requirements preserved

A first-class rich editor with normal mouse/Shift selection is the central UI,
not a future polish item. Users enter shell commands and agent requests in that
same editor. Auto-routing must be visible and overridable. Normal terminal
programs must retain raw/native interaction. The agent is BYOK and provider-agnostic.

The implemented milestone is Linux, Bash, one embedded Konsole terminal, one
composer, one conversation, explicit per-action approvals, and editable model
configuration. A source-level Konsole fork is not included in this milestone.

## Separation of concerns

```text
Qt rich editor
    |
    +-- local Python router ----- visible decision / ambiguity choice
    |                                |
    |                                +-- shell destination
    |                                |     syntax-check without execution
    |                                |     private input file
    |                                |     Readline binding + hash acknowledgement
    |                                |     Enter -> real Bash -> KonsolePart
    |                                |
    |                                +-- agent destination
    |                                      provider HTTP stream
    |                                      validated tool call
    |                                      review / approve / deny
    |                                      separate tool process or file operation
    |                                      result -> provider -> final answer
    |
    +-- explicit native toggle --- Konsole receives normal keys
```

The Python worker has no listening TCP port. Its frontend protocol is JSON lines
on a private child-process stdin/stdout pipe. API keys travel over that pipe, not
command-line arguments, URLs, files, or logs. Frontend output is plain text.

## Shell state machine and the bug found during testing

`PROMPT_COMMAND` runs **before** Bash puts the terminal in Readline's input mode.
A prompt event alone is therefore not sufficient to dispatch a control-key
binding. An early test observed the kernel interpreting Ctrl+R as terminal reprint
because the terminal was still canonical. Tests were corrected to require
noncanonical tty mode; the frontend now also checks tty mode and foreground process
group via the shell's `/proc/<pid>/fd/0` before dispatch.

The intended sequence is:

1. Bash prompt hooks publish authenticated private state: cwd, original exit status,
   aliases/functions, PATH, and a new sequence value.
2. Frontend also verifies Readline tty readiness and shell foreground ownership.
3. User submits. Router validates the input and performs non-executing `bash -n`.
4. Frontend writes the exact UTF-8 command to a private atomic file. The PTY only
   receives the reserved Readline key sequence, not arbitrary input bytes.
5. Readline loads the command; the shell bridge acknowledges its SHA-256 hash.
6. Only after matching acknowledgement does the frontend send Enter. No ack means
   no Enter. The editor draft remains recoverable.
7. Terminal takes focus during execution. Bash DEBUG/prompt hooks report running
   and ready states. A foreground TUI/REPL receives ordinary terminal input.
8. On a new ready prompt, the composer can regain focus unless native mode was
   explicitly selected.

The event file is not a trust boundary against hostile same-user processes.
Permissions and the token avoid accidental cross-session events and terminal
output spoofing; they are not a substitute for an OS sandbox.

The temporary rcfile sources the user's `.bashrc`, preserves scalar/array prompt
commands, and refuses to replace a pre-existing DEBUG trap. The latter intentionally
fails into native mode. It does not claim compatibility with all prompt frameworks.

## Routing

No remote classifier is used. The router considers explicit prefixes/modes,
known shell builtins, executable resolution, live aliases/functions, natural-language
patterns, and explicit shell constructs. Unknown input produces `ambiguous`, not
an automatic speculative shell execution or hidden API request.

Syntax validity does not imply safety or even command intent. Natural-language
strings can be valid Bash. Routing therefore does not use parsing alone.
Examples and overrides are in README.md. This classifier is deliberately modest;
measure real misroutes before adding a model classifier or a learned component.

## Provider transport

Use standard-library HTTP/JSON, not one vendor's agent SDK. Providers can differ
in reasoning parameters and tool-stream fields even when “OpenAI-compatible.”
The parser assembles partial tool arguments by index, retains provider reasoning
fields for later tool turns, enforces stream limits, handles JSON fallback, and
refuses truncated/incomplete tool-call execution. The request adapter exposes a
small allowlist of extra request parameters, editable in the UI.

BYOK does not imply account access. Configuration validates syntax and policy;
only a real API request can validate a user's model entitlement. Live provider
calls remain untested in this environment.

## Agent tools and approvals

Tools are prepared first, reviewed second, and executed only after an approval
with the matching one-shot ID. Writes are previewed locally and re-check the
original file before replacement. File tools reject parent traversal, absolute
paths, symlinks, common secret-file locations, nonregular files, and oversized
content. These checks reduce mistakes, but are not a hardened filesystem sandbox
against concurrent hostile processes.

Shell commands are separate Bash processes. They do not mutate the live terminal's
shell state. API-key-like environment names, shell-init hooks, and authentication
agent variables are removed from their inherited environment. This reduces
accidental leakage but cannot revoke filesystem or network permissions.

All tool actions require approval in this preview. Do not add a “safe command”
allowlist based only on the first command word: shell substitutions, redirects,
build scripts, aliases, and interpreters make such a policy unreliable.

## Next acceptance gates (not completed work)

The first gate is a real Qt6/KF6 build followed by mouse-drag, Shift+click,
Ctrl+Shift+arrow, undo, multiline, paste, IME, and accessibility checks on a KDE
desktop. No backend test substitutes for these user-critical GUI checks.

Next test native terminal compatibility with vim/neovim, less, fzf, interactive
Python, Ctrl+C/Ctrl+D, resize, alternate screen, Unicode, SSH, and tmux. Remote and
multiplexer sessions should remain native until their integrations are explicit.

Then validate a real Kimi key and GLM-5.3 key with streaming and a harmless approved
tool call. Follow that with user-requested workspace/file edits, denied tool calls,
network failures, token exhaustion, and cancellation during each tool state.

Only after those gates should the milestone be called a usable desktop alpha.
The next product work is completion, command/output capture, tabs/splits, explicit
context attachment, KWallet storage, Zsh/Fish bridges, and OS sandboxing. A decision
on moving these modules into a full Konsole fork remains open.

## Theme

Relay uses a Warp-inspired dark theme defined in `src/Theme.h`/`src/Theme.cpp`:
Fusion style, a dark `QPalette`, and one stylesheet built from color tokens
(background, surface, border, muted text, and a single cyan accent). Widgets that
`buildUi()` creates without names are tagged by `relay::theme::polishWindow()`.

The embedded terminal uses `data/theme/konsole/RelayDark.colorscheme` through the
`Relay.profile` profile. KF5 KonsolePart has no API to select a profile, so before
`QApplication` is created Relay prepends `data/theme` to `XDG_CONFIG_DIRS` and
`XDG_DATA_DIRS`. KonsolePart's `ProfileManager` then reads `relayrc`
(`DefaultProfile=Relay.profile`) and finds the profile and color scheme there.
Relay restores both variables before starting Bash, so the user's shell and the
programs it launches see their original XDG paths. No file in `~/.config` or
`~/.local/share/konsole` is written. Theme data is installed to
`share/relay/theme` and found from the source tree during development.
