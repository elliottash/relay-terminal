---
id: X59Q
type: work
status: needs-qa-llm
labels: [bug]
assignee: agent
rank: zzzz101
created: '2026-09-18'
acceptance: typing `continue` alone in auto mode sends it to the agent instead of the shell
source: 'issues/bug_intake.txt, 2026-09-18: ""continue" on its own should read as an agent prompt"'
links: {plans: [], commits: [79f1b6b], evidence: [], related: [], github: null}
---
# A bare "continue" should reach the agent, not the shell

## Request
"continue" on its own should read as an agent prompt

## Resolution (found already fixed by the 2026-09-19 board sweep)

`79f1b6b`, "A lone `continue` is a prompt, not a loop keyword". `continue` is a bash builtin, so the
router sent it to the shell, where outside a loop it is an error. It is now in `LOOP_ONLY`, and
classified live it reads:

```
'continue'          -> agent   "continue" means nothing outside a loop; sent to the agent
'continue the build'-> agent   "continue" is a command and an English word; reads like a sentence
'ls'                -> shell   Runnable shell command.
```

Nothing was needed here; the card had simply never been moved out of the inbox.

## QA checklist
- [ ] In auto mode, `continue` alone goes to the agent and continues the turn.
- [ ] `continue` inside a real loop body still reaches the shell (a pasted `for` loop is not broken).
- [ ] The sibling loop keywords (`break`) behave the same way.
