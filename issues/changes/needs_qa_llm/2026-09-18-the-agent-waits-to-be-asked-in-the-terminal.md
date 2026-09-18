---
id: TN4P
type: work
status: needs-qa-llm
labels: [change, bug, prompt]
component: [agent, worker]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'No prompt string gates an action on the user having asked for it (regression test in tests/test_terminal_handoff.py); the password, destructive-action and untrusted-screen rules are unchanged; a pane whose ceiling is agent or prefill says so in the Relay context; `./scripts/test.sh` passes'
source: 'owner, 2026-09-18: the agent''s thinking said "im not supposed to enter commands for the user without being asked", "im not sure i want that", and "the agent can run tool calls on its own. so why should this be stricter."'
links: {plans: [], commits: [], evidence: [], related: [XM0T], github: null}
---
# The agent waited to be asked before using the user's terminal

## Report

The owner saw the agent reason that it was not supposed to enter commands for the user without
being asked, and questioned it: `run_command` already runs arbitrary Bash and `edit_file` already
rewrites files, both with no per-action approval — a decision the project has already taken
(`WARP.md`, "no per-action tool approvals"). `run_in_terminal` is the *more* supervised path, since
the pane prints the command with its intent line before it acts, the user can stop it, the prompt
box and a busy shell both refuse it, and the chain breaker ends a run of them.

Nothing in the code required the agent to be asked. Four prompt strings did:

| Where | Text |
|---|---|
| `terminal_handoff.py` | mode "run" … "and **the user asked for the outcome**" |
| `agent.py` `SYSTEM` | "call a tool **only when it is needed for the request**" |
| `agent.py` `SYSTEM` | "never send a keystroke **the user's request does not call for**" |
| `agent.py` grant note | "**Only do what they asked for**" |

Three outside models (GLM 5.3, DeepSeek Pro, Kimi K2.8) reviewed every prompt string in the
product independently and each traced the behaviour to these, with no prompting toward that
answer beyond a description of what the owner saw.

## Change

The consent gates are gone; the safety lines are untouched. `SYSTEM` now says tools run without
confirmation and the agent is expected to act, including in the user's terminal, and keeps "Never
take destructive or irreversible action the user did not ask for" as its own sentence.
`run_in_terminal`'s description says a command may be handed over whenever it is clearly the next
step, and reserves `prefill` for what is destructive, hard to undo, or worth editing first. The
grant note tells the agent to drive a handed-over program to where the user wants it rather than
handing each prompt back.

Four things found alongside it, all of them prompts contradicting the code:

- `SYSTEM` banned "privileged commands or tools that require a password" while the same prompt
  told the agent to use `run_in_terminal` for sudo and logins. The restriction belongs to
  `run_command`, which has no tty and no stdin, and now says so there.
- The terminal-directory note promised that `run_command` defaults to the pane's directory.
  `set_default_cwd` silently falls back to the workspace root for anything outside it, so the note
  was wrong exactly when the user was somewhere else. It now states both cases.
- `run_command`'s "Ask for the time a long build or test suite needs" reads as *ask the user*;
  it means *pass a bigger `timeout_seconds`*. Reworded, and its first line now admits that `host`
  runs the command on the ssh host rather than in the workspace.
- The `chain` refusal said the agent had run commands "without the user typing anything", which
  is a rate limit phrased as a moral rule — and is roughly the sentence the owner saw quoted back.
  It now says the agent has reached the run of commands allowed before the user takes a turn.
- The `explore` subagent's read-only rule listed six allowed commands, which it read as a
  whitelist and used to refuse harmless reads. The list is now an illustration.

`SYSTEM` is also one sentence per line instead of a single 4,700-character paragraph. All three
reviewers raised it unprompted: the hard rules sat mid-paragraph beside the Markdown advice, which
is how a clause about tool discipline came to be read as a general rule about permission.

Last, a pane whose `terminal_handoff` ceiling is `agent` or `prefill` now says so in the Relay
context. Before, the tool appeared in the list with nothing said about it, and an unexplained tool
is one a careful model talks itself out of using; the `prefill` note also warns that a `run` comes
back placed in the prompt box.

## Not done here

Whether the ceiling's default should stay `agent` is the owner's call and was not touched:
`agent/terminal_handoff` still defaults to `agent`, and `off` still removes the tool.
