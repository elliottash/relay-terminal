---
id: 44XA
type: work
status: executing
labels: [bug, remote, phone]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude:ashe-ethz-ch
session: 47171b5e-f82e-4bcd-a055-bf22f59d39e7
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-26'
verify: {artifact: code, primary: script, also: [ai-visual, person], human: optional, criteria: pane_state.composer.suggestion carries the desktop's AI ghost; the phone shows it in an empty box and a tap fills the box; an agent device never gets a command suggestion; a phone prompt clears it., sign_off: none, effort: low, stakes: nuisance, blast: capability}
source: terminal pane 47171b5e, 2026-09-26
links: {plans: [], commits: [], evidence: [], related: [8R3V, 53GR], github: null}
---
# Phone prompt box: the AI suggested prompt / command never reaches the phone

## Issue
After a turn, the desktop's empty prompt box shows an AI suggestion: a next prompt, or a next command in the shell, with Tab to accept (`m_aiGhost`, from the worker's `suggest` side call). `pane_state` never publishes it, so the phone's box shows only the placeholder. There is also no tap equivalent of Tab.

> also it seems like on phones the suggested prompts arnt being included
> — elliott · [session:126f58f09e8e410e9141b406f427a4e8](relay://session/126f58f09e8e410e9141b406f427a4e8) · 2026-09-26

## Done means
When the desktop's empty prompt box shows an AI suggestion, the phone's empty box shows the same one, marked as a suggestion, prompt or command. A tap puts it in the box to edit or send, which is the phone's Tab. Once the text is typed, sent or cleared, or the desktop drops the suggestion, it is gone from the phone too. A device that may only reach the agent is never offered a shell-command suggestion.

Failure is today's: the phone's box shows only "Ask or run…" while the desktop shows a suggested next prompt.
