---
id: EM1E
type: work
status: ready
component: [gui, worker]
milestone: desktop-alpha
workstream: agent
rank: '42'
created: '2026-09-17'
acceptance: an image pasted or dropped into the composer reaches the model; on GLM 5.3 the turn is sent to GLM 5.3 Flash automatically; presets without vision say so instead of failing
source: '`issues/feature_intake.txt`, 2026-09-17: "we need image context (glm 5.3 swaps to glm 5.3 flash for that)"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Image context in agent prompts

## Open questions
1. Inputs: paste, drag-and-drop, file path, and a "screenshot this pane" action?
2. GLM: swap to GLM 5.3 Flash only for turns that carry an image (recommended), then back?
3. Presets without vision: route that turn to a configured vision model, or refuse with a message?
4. Keep images in session history (context cost) or replace with a text description after the turn?

## Decisions (owner, 2026-09-17)
All recommendations accepted: paste, drag-and-drop, file paths and a "screenshot this pane" action; GLM swaps to GLM 5.3
Flash only for turns with an image and says so; presets without vision use the configured vision model, else refuse
with a message; images stay for their turn and are then replaced by a short description plus the file path. The user
can choose a **vision model** separate from the main model in Agent options.
