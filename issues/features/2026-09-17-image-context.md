# Image context in agent prompts

- **Status**: open
- **Component**: gui, worker
- **Milestone**: desktop-alpha
- **Workstream**: agent
- **Acceptance evidence**: an image pasted or dropped into the composer reaches the model; on GLM 5.3 the turn is sent to GLM 5.3 Flash automatically; presets without vision say so instead of failing
- **Assignee**: unassigned
- **Source**: `issues/feature_intake.txt`, 2026-09-17: "we need image context (glm 5.3 swaps to glm 5.3 flash for that)"

## Open questions
1. Inputs: paste, drag-and-drop, file path, and a "screenshot this pane" action?
2. GLM: swap to GLM 5.3 Flash only for turns that carry an image (recommended), then back?
3. Presets without vision: route that turn to a configured vision model, or refuse with a message?
4. Keep images in session history (context cost) or replace with a text description after the turn?
