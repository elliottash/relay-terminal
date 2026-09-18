---
id: D8J3
type: work
status: in-progress
labels: [feature]
component: [agent, gui, shell-integration]
milestone: desktop-alpha
workstream: agent
rank: zzzzzj
assignee: agent
implemented_by: Claude Fable 5.1 (Claude Code session), 2026-09-18
created: '2026-09-18'
acceptance: a recorded run where the agent runs `ssh -t` in the visible pane, the user answers it, and the agent continues from the exit status and output without being told; and one where the command lands in the prompt box instead
source: owner, in a Claude Code session, 2026-09-18, with a screenshot of the agent printing an `ssh -t` one-liner in a `relay-run` fence that did nothing
links: {plans: [], commits: [], evidence: [], related: ['VH4B', 'C1HH', 'QRNJ'], github: null}
---
# The agent hands a command to the user's terminal: run it, or put it in the prompt box

## Issue

> check out this interaction with relay: [screenshot: the agent's reply ends with a `relay-run`
> block holding `ssh -t filly '~/bin/codex login && rm /srv/archive/tracelaw/worker/pause && echo
> "filly unpaused"'`, then "Tell me when the SSO is done and I'll verify…"]
>
> i think it should either:
>
> 1) run ssh for me and do it.
>
> 2) pre-fill the prompt box with the reocmmended command.
>
> research how we can implement these options and allow the relay agent to do that

Asked which should be the default:

> the agent should have both options. warp does both, i guess the agent sometimes can tell what
> will be better, depending on the confidence in the command, whether the user might want to edit
> it, and whether the SSH / remote session should be interactive (with potential agent control)

Asked whether Relay should start a follow-up agent turn with the exit status and output when the
command exits: "Yes, auto-continue."

## What was wrong

`run_command` is a separate Bash with no terminal, no stdin and no ssh agent, so `ssh -t`, `sudo`
and logins cannot work there. The agent had seen one terminal fix request (#VH4B), which asks for a
`relay-run` fence, and reused the fence in an ordinary reply, where nothing reads it. The owner
copied the command by hand.

## Tasks

- [x] Worker tool `run_in_terminal {command, mode, intent, report_back}`, offered by `context.terminal_handoff` <!-- t:a1 -->
- [x] Protocol section 22: `terminal_command` / `terminal_command_result` <!-- t:a2 -->
- [ ] Pane: run through `runInTerminal`, prefill through `setComposerText`, never over a draft <!-- t:a3 s=in-progress -->
- [ ] Follow-up turn with exit status and output when the command exits; Ctrl+C sends nothing <!-- t:a4 s=in-progress -->
- [ ] Setting `agent/terminal_handoff` (agent decides / always prefill / off) in the settings pane <!-- t:a5 -->
- [ ] Offer "Let the agent drive" when a run leaves a program in the foreground <!-- t:a6 -->
- [ ] Live run under Xvfb with a stub provider, evidence, then one pass with a real model <!-- t:a7 -->

## Decisions

- **One tool, the agent picks the mode per call** (owner). The user's setting is a ceiling on it.
- **Not an approval.** "No per-action tool approvals" stands: `prefill` is the agent handing the
  user a command to finish, not a gate Relay puts in front of `run`.
- **The `relay-run` fence stays a fix-loop detail** and is not honoured in ordinary replies: free
  text is the wrong channel into a tty. The system prompt now says where it belongs.
- **A prompt from a paired device never gets the tool**: a view-or-agent phone must not reach the
  shell through the agent either.
- **A handed-over command is not also fix-looped**: its exit goes to the agent that asked for it.
