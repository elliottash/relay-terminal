# The launch flags, against the real CLIs (GT7X, §26.9)

`backend/relay_core/guest_launch.py` replaced per-project setup with per-launch command lines, and
its docstring claims them as "verified against the installed CLIs on 2026-09-19". This is that
verification, done rather than asserted: `launch-flags-verify.py` beside this file starts the real
`claude` and the real `codex` on the owner's own logins, drives the two TUIs on a pty, and reads
the pane's guest spool back to see which hooks actually fired.

    $ claude --version  ->  2.1.278 (Claude Code)
    $ codex --version   ->  codex-cli 0.155.1

**Every step is VERIFIED.** Three things came out of it that the design does not currently account
for, and they are collected under [What contradicts the design](#what-contradicts-the-design) at
the end; the rest of this file is one section per step.

## Running it again

    python3 docs/qa_evidence/2026-09-19-claude-codex-guest-integration/launch-flags-verify.py
    python3 …/launch-flags-verify.py --only A,C,F
    python3 …/launch-flags-verify.py --fresh-project      # brings Claude's trust dialog back

A whole run is **92.8 s of wall time and 5 model calls** (A, C and E's two runs each send the one
line `Reply with the single word ok.`; the fifth is Claude's own answer to step B's `!` command,
which is not asked for — see B). The scratch root is `/tmp/claude-1000/launchv`, kept short because
a pty and the spool live under it; `HOME` stays the real home, because both CLIs need their real
login, and nothing here writes into `~/.claude` or `~/.codex` itself.

Two details of the harness are worth knowing before reading the results:

* **`CLAUDE_*` is stripped from the child's environment.** Run from inside a Claude Code session,
  the guest inherits `CLAUDE_CODE_CHILD_SESSION` and `CLAUDE_EFFORT`; it then prints
  *"⚠ Transcript saving is off — inherited CLAUDE_CODE_CHILD_SESSION marker"* and runs at the
  parent's effort. A Relay pane's shell has none of that, so neither does the guest here.
* **The pty is read through a small screen model, not an escape-stripper.** Both TUIs place words
  by jumping the cursor to a column (`ESC [ n G`) instead of emitting spaces, and both redraw a
  line in place many times a second. Stripping escapes yields `Yes,Itrustthisfolder` and
  `echorrelay-bang-okg`; the `Screen` class in the driver keeps a grid and a cursor, so the quoted
  screens below are what a person would have seen.

---

## A — `claude --settings <file>` with hooks · VERIFIED

The settings file is written by calling the real function, not a copy of it:
`guest_launch.write_claude_settings(<rt>, cwd=<temp project>, home=<real home>)`. Then:

    $ RELAY_RUNTIME_DIR=<rt> RELAY_SESSION_TOKEN=test RELAY_GUEST_EVENT=<rt>/guest-events \
      RELAY_BACKEND_DIR=$PWD/backend RELAY_PYTHON=$(which python3) \
      claude -p 'Reply with the single word ok.' \
        --settings <rt>/guest/claude-settings.json --dangerously-skip-permissions

What happened:

    settings keys: ['hooks']  hooks: ['Notification', 'PermissionRequest', 'Stop', 'UserPromptSubmit']
    statusLine present: False
    exit=0  stdout='ok'
    spool: 2 file(s)
    01789832568267254062-154525-1.json  event='hook' guest='claude' data.name='UserPromptSubmit'
        payload={"session_id": "2b327629-…", "transcript_path": "/home/elliott/.claude/projects/
        -tmp-claude-1000-launchv-proj/2b327629-….jsonl", "cwd": "/tmp/claude-1000/launchv/proj",
        "prompt_id": "32477…", "permission_mode": "bypassPermissions",
        "hook_event_name": "UserPromptSubmit", "prompt": "Reply with the single word ok."}
    01789832569912030491-154883-1.json  event='hook' guest='claude' data.name='Stop'
        payload={… "hook_event_name": "Stop", "stop_hook_active": false,
        "last_assistant_message": "ok", "background_tasks": [], "session_crons": []}
    lines mentioning trust/accept in the output: none

So: claude ran, answered `ok`, and **both bracket hooks fired from a command-line settings file**,
with no trust prompt and nothing installed anywhere. `permission_mode` reads `bypassPermissions`,
which is `--dangerously-skip-permissions` arriving as far as the hook payload. The envelopes are
the shim's own (`guest`, `token`, `sequence`, `event`, `data.name`, `data.payload`), so §26.3 and
§26.4 are unchanged by the move to a launch file — as `guest_launch` intends, since both the file
and the retired installer are built from the one `guest_install.relay_entries()`.

`statusLine present: False` is not a failure: the owner has a statusline of their own in
`~/.claude/settings.json`, so `write_claude_settings` drops Relay's rather than replace it
(`user_statusline_present()`), exactly as documented. The guest chip then has nothing to show for
*this* user — worth knowing, but it is the designed behaviour and not a finding.

**`--settings` also takes a JSON string.** `claude --help` documents the argument as
`<file-or-json>`: *"Path to a settings JSON file or a JSON string to load additional settings
from"*. Relay uses the file, which is right — the command line would otherwise carry the hook
commands into every `ps` listing on the machine — and the string form is recorded only because it
exists.

---

## B — the Claude TUI, `!` shell mode and `/exit` · VERIFIED

Same command line without `-p`, on a 120×40 pty.

    $ claude --settings <rt>/guest/claude-settings.json --dangerously-skip-permissions

    after '!' on an empty composer, the prompt becomes '!': True
    lines carrying 'relay-bang-ok':
        ['! echo relay-bang-ok',
         '  ⎿ relay-bang-ok',
         "● The command ran and printed relay-bang-ok, so the ! relay is working. …"]
    the output is shown on its own '⎿' line: True
    spool after the bang command: ['Stop']
    /exit -> exit code 0

`!` on an empty composer switches it to shell mode (the `❯` prompt becomes `!`), the command runs
locally, and its output is shown under the `⎿` gutter. `/exit` quits with status 0.

**But the command is not only local.** Claude answers it with a model turn — the `●` line above —
and a `Stop` envelope lands on the spool. There is no `UserPromptSubmit`, so the typed line is not
submitted *as a prompt*; the turn is Claude's reaction to the command and its output. This was
checked twice, including once with `CLAUDE_*` stripped from the environment, and is the one model
call in a run that asks for none. See [What contradicts the design](#what-contradicts-the-design).

### No acceptance dialog for `--dangerously-skip-permissions`; a workspace trust dialog instead

There is **no** first-run acceptance dialog for `--dangerously-skip-permissions` in 2.1.278 — not
in `-p` and not in the TUI. The footer simply reads `⏵⏵ bypass permissions on (shift+tab to
cycle)`.

There *is* a different one-time dialog, and Relay has to know about it: the **workspace trust**
question, shown once per directory Claude Code has not seen before. On the very first TUI launch
in the temp project it came up verbatim as

    Accessing workspace:

    /tmp/claude-1000/launchv/proj

    Quick safety check: Is this a project you created or one you trust? (Like your own code, a
    well-known open source project, or work from your team). If not, take a moment to review
    what's in this folder first.

    Claude Code'll be able to read, edit, and execute files here.

    Security guide

    ❯ No, exit
      Yes, I trust this folder

    Enter to confirm · Esc to cancel

**The default is `No, exit`** — a bare Enter quits Claude. It is answered with **Down, then
Enter**, and the answer is remembered as `projects["<dir>"].hasTrustDialogAccepted` in the user's
`~/.claude.json`, so it appears once per project directory and never again. That is why the run
quoted above shows no dialog: the temp project was trusted on the first run. `--fresh-project`
brings it back on demand.

`-p` never shows it. `claude --help` says so under `-p, --print`: *"The workspace trust dialog is
skipped when Claude is run in non-interactive mode (via -p, or when stdout is not a TTY, e.g.
piped or redirected output). Only use this in directories you trust. Settings files that fail
validation are silently ignored in this mode (no error dialog is shown)."* The last sentence is
worth a second read — see the contradictions section.

---

## C — Codex `-c` overrides and the `notify` hook · VERIFIED

The two overrides are taken from `guest_launch.codex_overrides(python)` itself:

    ['-c', 'notify=["/usr/bin/python3", "-S",
            "/home/elliott/repos/relay-terminal/backend/relay_core/guest_codex.py", "notify"]',
     '-c', 'tui.notification_condition="always"']

    $ codex exec -c notify=[…] -c tui.notification_condition="always" \
        --dangerously-bypass-approvals-and-sandbox --skip-git-repo-check \
        'Reply with the single word ok.'

What happened — codex ran (`approval: never`, `sandbox: danger-full-access`, answer `ok`,
exit 0) and **the notify hook fired in `exec` mode**, one file on the spool:

    01789832611135603887-160274-1.json  event='hook' guest='codex' data.name='notify'
        payload={"type": "agent-turn-complete", "thread-id": "01a0ba56-35ab-…",
        "turn-id": "01a0ba56-35d6-…", "cwd": "/tmp/claude-1000/launchv/proj",
        "client": "codex_exec", "input-messages": ["Reply with the single word ok."],
        "last-assistant-message": "ok"}

`notify` is not a hook in Codex's `hooks` sense — it is the top-level program Codex execs at the
end of a turn — so it is not gated by hook trust (E) and needs no extra flag. The TUI variant was
therefore not needed and not run.

One operational note: with stdin left open and not a tty, `codex exec` prints
`Reading additional input from stdin...` and waits on it. The driver runs it with stdin on
`/dev/null`. A Relay pane gives it a tty, so this does not affect the product.

---

## D — the Codex TUI, `!` shell mode and `/quit` · VERIFIED

    $ codex -c notify=[…] -c tui.notification_condition="always" \
        --dangerously-bypass-approvals-and-sandbox        # on a 120x40 pty

Codex's shell mode is exactly what §26.9 assumes, and cleaner than Claude's:

    after '!' on an empty composer: the status bar says 'Shell mode' and the prompt becomes '!': True
    lines carrying 'relay-bang-ok': ['• You ran echo relay-bang-ok', '  └ relay-bang-ok']
    shown as a local run ('You ran …'): True;  its output under the '└' gutter: True
    spool after the bang command: empty
    /quit -> exit code 0

`!` at the start of an empty composer switches to shell mode — the status bar's right edge reads
`Shell mode` and the prompt becomes `!`. Enter runs the command locally, and the transcript shows
it as `• You ran echo relay-bang-ok` with `└ relay-bang-ok` under it. **Nothing is sent to the
model**: the spool stays empty, and an empty spool here is proof, because `notify` fires at the end
of every turn. `/quit` exits with status 0.

### Codex gates hooks at startup, before the composer

Before any of that, the TUI opened on a full-screen question — with no hooks of ours in the
command line:

    Hooks need review
    5 hooks are new or changed.
    Hooks can run outside the sandbox after you trust them.

    › 1. Review hooks
      2. Trust all and continue
      3. Continue without trusting (hooks won't run)

    Press enter to confirm or esc to go back

The five are the owner's own `codex-warp` plugin's
(`~/.codex/plugins/cache/codex-warp/warp/0.4.0/hooks/hooks.json`: `SessionStart`, `Stop`,
`PermissionRequest`, `UserPromptSubmit`, `PostToolUse` — one entry each). The driver answers **`3`, Enter** —
"Continue without trusting" — deliberately: options 1 and 2 persist a `trusted_hash` into the
user's config, and nothing here is allowed to do that. The composer is unreachable until this is
answered, and the default selection is *Review hooks*, so a bare Enter does not dismiss it.

---

## E — Codex hooks injected with `-c hooks.<Event>=[…]` · VERIFIED

`codex features list` shows `hooks  stable  true`. The schema matches Claude Code's
(`hooks.<Event> = [{matcher, hooks:[{type, command, timeout}]}]`; the events are `PreToolUse`,
`PermissionRequest`, `PostToolUse`, `PreCompact`, `PostCompact`, `SessionStart`, `SessionEnd`,
`UserPromptSubmit`, `SubagentStart`, `SubagentStop`, `Stop`, `Interrupt`). The question is whether
a hook can be *injected at launch*, which is what Relay would need. The answer is yes, with one
flag, and the pair of runs is the evidence.

**Without `--dangerously-bypass-hook-trust`:**

    $ codex exec -c 'hooks.Stop=[{hooks=[{type="command",command="touch <marker>"}]}]' \
        --dangerously-bypass-approvals-and-sandbox --skip-git-repo-check \
        'Reply with the single word ok.'
    exit=0
    hook/trust lines in the output: none
    marker created: False

The run succeeds, answers `ok`, and **says nothing at all**. The hook did not run; Codex printed no
warning, no note, nothing. An untrusted hook is skipped silently in `exec`.

**With `--dangerously-bypass-hook-trust`:**

    exit=0
    hook/trust lines in the output:
        warning: `--dangerously-bypass-hook-trust` is enabled. Enabled hooks may run without
                 review for this invocation.        (printed twice)
        hook: SessionStart          hook: SessionStart Completed       (each twice)
        hook: UserPromptSubmit      hook: UserPromptSubmit Completed
        hook: Stop                  hook: Stop Completed               (each twice)
    marker created: True

The marker file exists: the injected hook ran. The doubling is the plugin's hook and ours for the
same event; `UserPromptSubmit` appears once because only the plugin has one.

Trust is stored as `hooks.state."<identity>".trusted_hash` (and an `enabled` flag beside it) in the
user's config, keyed by a hash of the hook, which is why *"new or changed"* is the wording in the
TUI gate. Nothing was persisted by this run: `exec` never asks, and step D declined the TUI's
question.

Two consequences for Relay, both of them design questions rather than defects:

* A Relay-injected Codex hook needs `--dangerously-bypass-hook-trust` on the command line, and
  that flag is per-invocation, so it has to be on *every* launch. `guest_launch.codex_argv()` does
  not add it today, which is correct as long as Relay ships no Codex hooks — `notify` (C) is not
  one. If §26.9 ever grows a Codex hook, the flag comes with it.
* That flag is not narrow. It un-gates **every** enabled hook for that invocation, including the
  user's plugins'. Adding it to buy Relay a hook would silently trust theirs too.

---

## F — the `--help` lines, verbatim · VERIFIED

`claude --help` (2.1.278):

      --settings <file-or-json>             Path to a settings JSON file or a JSON
                                            string to load additional settings from
      --ide                                 Automatically connect to IDE on startup
                                            if exactly one valid IDE is available
      --dangerously-skip-permissions        Bypass all permission checks.
                                            Recommended only for sandboxes with no
                                            internet access.
      -r, --resume [value]                  Resume a conversation by session ID, or
                                            open interactive picker with optional
                                            search term
      --fork-session                        When resuming, create a new session ID
                                            instead of reusing the original (use
                                            with --resume or --continue)
      --session-id <uuid>                   Use a specific session ID for the
                                            conversation (must be a valid UUID)

`codex --help` (0.155.1):

      -c, --config <key=value>
              Override a configuration value that would otherwise be loaded from `~/.codex/config.toml`.
              Use a dotted path (`foo.bar.baz`) to override nested values. The `value` portion is parsed
              as TOML. If it fails to parse as TOML, the raw string is used as a literal.

              Examples: - `-c model="o3"` - `-c 'sandbox_permissions=["disk-full-read-access"]'` - `-c
              shell_environment_policy.inherit=all`
          --dangerously-bypass-approvals-and-sandbox
              Skip all confirmation prompts and execute commands without sandboxing. EXTREMELY
              DANGEROUS. Intended solely for running in environments that are externally sandboxed

          --dangerously-bypass-hook-trust
              Run enabled hooks without requiring persisted hook trust for this invocation. DANGEROUS.
              Intended only for automation that already vets hook sources

      -C, --cd <DIR>
              Tell the agent to use the specified directory as its working root

`codex resume --help` and `codex fork --help` both carry `-c, --config <key=value>` and
`--dangerously-bypass-approvals-and-sandbox` (and `--dangerously-bypass-hook-trust`) with the same
text, so `guest_launch.codex_argv()`'s subcommand-first ordering — `codex resume -c … --dangerously-…
<id>` — is accepted. Their usage lines are `codex resume [OPTIONS] [SESSION_ID] [PROMPT]` and
`codex fork [OPTIONS] [SESSION_ID] [PROMPT]`, which is the ordering `CODEX_SUBCOMMANDS` assumes.

---

## What contradicts the design

Three things, in the order they matter. None of them is a bug in `guest_launch`; all three are
behaviour of the CLIs that §26.9 currently does not mention, and the first two need the owner's
call about what a pane should do.

1. **A `!` command in the Claude guest costs a model turn.** §26.9 and the task that commissioned
   this check both assume `!` is a local shell escape that the model never sees. In 2.1.278 the
   command runs locally *and* Claude answers it — `● The command ran and printed relay-bang-ok …` —
   and a `Stop` hook fires. Codex does not do this (step D: `!` runs the command and the spool
   stays empty). So the two guests differ in exactly the place a user is most likely to treat them
   as the same, and a pane that shows `!` as "just the shell" would be lying about Claude. There is
   no `UserPromptSubmit`, so a pane can still tell a bang turn from a typed prompt if it needs to.

2. **Both guests can open on a full-screen question the pane has to get through.**
   * Claude: the workspace trust dialog, once per project directory, whose **default is
     `No, exit`** — an accidental Enter kills the guest. `-p` skips it; the TUI does not, and
     `--dangerously-skip-permissions` does not cover it.
   * Codex: `Hooks need review`, whenever any enabled hook is new or changed — including hooks
     Relay had nothing to do with, like the owner's `codex-warp` plugin's five. The composer is
     unreachable until it is answered.

   Neither is `--dangerously-…` asking to be accepted: there is **no** first-run acceptance for
   `--dangerously-skip-permissions` or for `--dangerously-bypass-approvals-and-sandbox`. But a pane
   that launches a guest and immediately shows a chip saying "ready" will be wrong on the first
   launch in a project.

3. **A `--settings` file that fails validation is ignored in `-p` mode with no error.** From
   `claude --help`: *"Settings files that fail validation are silently ignored in this mode (no
   error dialog is shown)."* `write_claude_settings` writes atomically into a 0700 directory, so a
   half-written file is not the risk; a future change to `relay_entries()` that Claude rejects is,
   and it would show up as "the hooks just stopped firing" with nothing in the pane to say why.
   Step A is the check that would catch it, which is why it asserts on the spool and not on the
   exit code.

Two smaller notes, already handled in the driver and needing nothing from anyone:

* A guest started from inside a Claude Code session inherits `CLAUDE_CODE_CHILD_SESSION` (transcript
  saving off) and `CLAUDE_EFFORT`. A pane's shell does not have these; the driver strips them.
* `codex exec` with stdin open and not a tty blocks on `Reading additional input from stdin...`.
