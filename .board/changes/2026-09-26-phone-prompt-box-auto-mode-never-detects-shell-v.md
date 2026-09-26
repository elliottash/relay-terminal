---
id: 53GR
type: work
status: executing
labels: [bug, remote, phone]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude:ashe-ethz-ch
session: 47171b5e-f82e-4bcd-a055-bf22f59d39e7
discovered_from: 8R3V
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-26'
verify: {artifact: code, primary: script, also: [ai-visual, person], human: optional, criteria: 'A full device in auto mode: typing `ls -la` turns the box cyan (TERMINAL), typing `why does the build fail` turns it violet (AGENT), clearing it goes back to neutral; the verdict comes from the pane''s own router with its context; a stale verdict for older text is ignored; an agent-only device never asks.', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: terminal pane 47171b5e, 2026-09-26
links: {plans: [], commits: [], evidence: [], related: [8R3V], github: null}
---
# Phone prompt box: auto mode never detects shell vs agent while typing

## Issue
The desktop's prompt box in auto mode asks the worker's router about the draft as you type (a debounced `route` request carrying the pane's PATH, cwd, known commands and foreground program). It then colours the caret, the text and the mode chip cyan for the terminal or violet for the agent. The phone's box (`app/pane.js`) only shows the pane's fixed mode word from `pane_state`, "auto", and never asks. So nothing changes as you type, and you can't tell where the line will go until it has gone.

> and it seems like the auto detector doesn't work.
> — elliott · [session:126f58f09e8e410e9141b406f427a4e8](relay://session/126f58f09e8e410e9141b406f427a4e8) · 2026-09-26

## Done means
On a phone that may route (`full`, pane mode `auto`), the prompt box reacts as you type the way the desktop's does. A command such as `ls -la` turns the caret and the mode chip the terminal's cyan and the chip reads "terminal". A sentence such as `why does the build fail` turns them the agent's violet and the chip reads "agent". An empty box is neutral. The verdict is the desktop pane's own router's, using that pane's PATH, cwd and known commands. An `agent`-only device never asks, since its box can only reach the agent anyway.

Failure is today's: the chip says "auto" whatever you type and the colour never changes. Also a failure: a verdict for text you have since changed painted over the current one.
