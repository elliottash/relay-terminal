---
id: MDL1
type: work
status: executing
labels: [feature, models]
assignee: claude-code
rank: k
created: '2026-09-21'
source: 'Claude Code in a Relay pane, 2026-09-21'
links: {plans: [docs/MODEL-PICKING-DESIGN.md], commits: [], evidence: [], related: [DC4J], github: null}
---
# Model picking: one name per model, one row per model, one default

## Issue
i need to take a fresh analysis and review of model picking.

we need to organize and reconcile how model names are listed across the app. for example, the picker should say gpt-5.6-sol, not "Codex". model names should always be lowercase, no spaces.

help me think through the edge cases there.

eg, if a model comes from multiple providers (codex, openai api, openrouter), i think its better if the model only shows up once in the small picker. help me design it to work like that.

it seems like which model is selected first, and the defaults, is not clear. when i started a new pane, versus what model was selected with /swap, did not seem consistent.

/swap seems like it doesnt work.

we can use opus subagents for research and implementation

something else i want to fix in this workstream, is that when you use alt+m or alt+e, youu current selection should be highlighted. then you should be able to select with up/down arrows, and also filter with text typing (like warp's model picker). deploy a subagent to fix that

in case you didnt notice yet, i also dont like how it says "switchboard" in the picker in the switchboard agent: [a screenshot of the box reading "kimi-k3 (switchboard)"]

## Decisions
- 2026-09-21, on the three questions below: "i agree with your rec on all 3". So: a guest harness
  at rank 1 of main is what a new pane starts on (the process starts on the first turn); Claude
  Code's `opus` is named `claude-opus-5`; Relay Free's models are `relay-main`, `relay-flash`,
  `relay-lite`.

## Discussion points
Three calls that are the owner's; work proceeds on the recommendation for each until he says
otherwise (`docs/MODEL-PICKING-DESIGN.md`, section 4):

1. A guest harness at rank 1 of the main list: does a new pane start it? Recommended: yes, since he
   ranked it first, with the harness process starting on the first turn.
2. The name of a Claude Code alias: `claude-opus-5` (recommended) or `opus`.
3. `relay-main` rather than `relay main`, which follows from "always lowercase, no spaces".

## Planning notes
Three read-only research passes (names, defaults, `/swap` reproduced live) are written up in
`docs/MODEL-PICKING-DESIGN.md` sections 1.1 to 1.4, with the sixteen edge cases in section 3.
`/swap` does dispatch; it toggles ranks 1 and 2 of the main list whatever the pane was on, so it
never returns to the model the owner was using, and its own sentence is overwritten by
`model_changed`.

## Plan
`docs/MODEL-PICKING-DESIGN.md`, section 5.

## Tasks
- [ ] Alt+M / Alt+E: current row highlighted, arrows, type to filter <!-- t:a1 -->
- [x] catalog: `models::name`, the worker's `name`, `grouped`, the pane's key resolved by name <!-- t:a2 -->
- [ ] defaults: a new pane reads the main list; a pick is per pane; restore the model; re-send tiers <!-- t:a3 -->
- [ ] `/swap`: a toggle with memory, a sentence that survives, a test <!-- t:a4 -->
- [ ] names at every display site, lower-case roles; no protocol role name ("switchboard") in a box <!-- t:a5 -->
- [ ] one row per model in the box and the picker, "via", `/model name@provider` <!-- t:a6 -->
