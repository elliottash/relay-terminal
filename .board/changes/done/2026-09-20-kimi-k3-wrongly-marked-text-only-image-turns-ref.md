---
id: W56B
type: work
status: done
labels: [bug, models]
assignee: agent
implemented_by: kimi/kimi-k3
verified_by: kimi/kimi-k3
rank: zzzzzzzzzzzzzzzz
created: '2026-09-20'
source: pane 80c1c8cd
links: {commits: [a0f5c10cb0bf, a6b63f205031], plans: [], evidence: [], related: [], github: null}
---
# kimi-k3 wrongly marked text-only: image turns refused

## Issue
i just saw this bug, kimi can do images i thought

🖼 No vision model · Options › Models › Vision model
✗ kimi-k3 cannot read images and no vision model is set. Choose one under Options
› Models › Vision model, or switch this pane to a model that reads images. Nothing
was sent to the provider.

## Tests
`PYTHONPATH=$PWD/backend python3 -m unittest tests.test_images tests.test_presets tests.test_roles` — 134 tests, OK. New coverage: `test_kimi_k3_and_the_k25_models_read_images`, `test_an_image_reaches_kimi_k3_directly_with_no_vision_model_set`; MiniMax-M3 replaces kimi-k3 as the text-only fixture in the refusal tests.
