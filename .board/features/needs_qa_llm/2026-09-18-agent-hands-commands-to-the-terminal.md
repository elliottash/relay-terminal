---
id: D8J3
type: work
status: needs-qa-llm
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
links: {plans: [], commits: ['322c64d'], evidence: ['docs/qa_evidence/2026-09-18-agent-terminal-handoff'], related: ['VH4B', 'C1HH', 'QRNJ'], github: null}
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
- [x] Pane: run through `runInTerminal`, prefill through `setComposerText`, never over a draft <!-- t:a3 -->
- [x] Follow-up turn with exit status and output when the command exits; Ctrl+C sends nothing <!-- t:a4 -->
- [x] Setting `agent/terminal_handoff` (agent decides / always prefill / off) in the settings pane <!-- t:a5 -->
- [x] Offer "Let the agent drive" when a run leaves a program in the foreground: the existing banner (#C1HH) already appears, no new code <!-- t:a6 -->
- [x] Live run under Xvfb with a stub provider, with evidence <!-- t:a7 -->
- [x] One pass with a real model, to see which mode it picks <!-- t:a8 -->
- [ ] The phone: show a `terminal_command` in `app/app.js` (it is forwarded, nothing renders it yet) <!-- t:a9 -->

## Decisions

- **One tool, the agent picks the mode per call** (owner). The user's setting is a ceiling on it.
- **Not an approval.** "No per-action tool approvals" stands: `prefill` is the agent handing the
  user a command to finish, not a gate Relay puts in front of `run`.
- **The `relay-run` fence stays a fix-loop detail** and is not honoured in ordinary replies: free
  text is the wrong channel into a tty. The system prompt now says where it belongs.
- **A prompt from a paired device never gets the tool**: a view-or-agent phone must not reach the
  shell through the agent either.
- **A handed-over command is not also fix-looped**: its exit goes to the agent that asked for it.

## What was built

- Worker: `backend/relay_core/terminal_handoff.py`, registered in `tools.py`, offered through
  `context.terminal_handoff`; `terminal_command_result` routed in `worker.py`. Protocol section 22.
- Pane (`src/Pane.h`): `handleTerminalCommand`, `answerTerminalCommand`, `finishHandoff`; the rule
  is `relay::input::handoffAction` and the follow-up text `relay::input::handoffReport`
  (`src/InputPolicy`). ARCHITECTURE section 7.1.
- Setting: Settings pane, Agent, "Commands the agent hands to your terminal".
- Tests: `tests/test_terminal_handoff.py` (28), three cases in `tests/inputpolicy_test.cpp`.

## Known limits

- The capture keeps the first 64 KiB a command prints, so the tail sent to the agent is the end of
  that. A login that prints megabytes first would lose its last lines.
- One real model was tried (GLM-5.3, three prompts, headless; see the evidence NOTES). Whether a
  model that has seen a fix request in the same conversation still writes a `relay-run` fence in
  an ordinary reply has not been tried.
- A stale `relay-run` fence in an ordinary reply is still inert text. The system prompt now tells
  the model not to write one; nothing rewrites it if it does.

## QA checklist

1. Ask the agent for something that needs a login on another host (`ssh -t <host> '<cmd>'`). It
   calls `run_in_terminal`, the command appears and runs in the pane, prompts work, and when it
   exits the agent continues by itself with the exit status and output.
2. Ask for a destructive command on purpose ("give me the command to wipe build/, I'll run it").
   It lands in the prompt box under `! terminal`; editing it and pressing Enter runs the edited
   text, and the agent hears about the edited command.
3. Wipe a prefilled command: the `! terminal` chip goes, and the next prompt routes normally.
4. Type a draft while the agent works: the draft survives, the pane prints "not run", and the
   reply shows the command in a code block instead.
5. Interrupt a handed-over command (Esc): no follow-up turn.
6. Settings, Agent, "Commands the agent hands to your terminal": "Always in the prompt box" turns
   every run into a prefill; "Off" removes the tool (the agent says it cannot and shows a block).
7. A terminal-mode command that fails still starts the fix loop, and a handed-over one does not.
8. A prompt sent from a paired phone never gets the tool.
