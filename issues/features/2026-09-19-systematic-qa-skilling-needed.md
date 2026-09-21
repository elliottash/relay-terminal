---
id: YZ8G
type: work
status: discussing
labels: [feature, switchboard, qa]
waiting_on: owner
priority: 1
rank: zzzzzzzzzzzi
created: '2026-09-19'
links: {plans: [], commits: [], evidence: [], related: [HKAP], github: null}
---
# systematic QA skilling needed

## Issue
relay should help users with human QA. deep research and scoping needed on this.

## Decisions
- **2026-09-20 — what "QA support" means.** "in my mind, QA support is 'human QA', as in a user is put into a test case that simualtes the problem the issue was designed to address"
- **2026-09-20 — an AI plays the scenario first.** "thats good to design for an AI to try simulating first, if we dont have that as well." We did not have it: the LLM QA lane and Verify run a card's checklist; nothing staged a scenario and played it in the app.
- **2026-09-20 — and that is what Verify typically is.** "i think verification should involve that (typically)"
- **2026-09-20 — first worked example: #7BM4.** `docs/qa_evidence/2026-09-20-switchboard-tooling-hub/`: `scenario/stage.py` stages a project in which the card's three problems are happening, `scenario/scenario.json` is the scenario as data (situation, what one did before, steps with an actor of `both` or `human`, an expectation each, one question for the person), `scenario/ai-pass.sh` + `ai-pass.md` are the AI's pass with a screenshot per step, and `HUMAN-QA.md` is the brief generated from the same data.
- **2026-09-20 — first concrete piece: a demo button after Verify.** "a first example of this woudl be a button after "verify", available once you are in needs verification or later. the agent would then run your app in a way that illusrates the feature or fix"
- **2026-09-20 — the QA pane is linked back to the card, like Execute and Verify.** "something else -- we can do the same linking of the QA-support agent pane and the switchbaod card we did with execute and verify: #HKAP" — the demo hand-off writes the pane's `pane_token` into its thread entry, so the entry is a link that reveals that pane (`WindowManager::focusPane`, inert once closed). The mechanism already ships (#HKAP, 56b921f6 + 31468891); this card reuses it rather than adding a second one.
