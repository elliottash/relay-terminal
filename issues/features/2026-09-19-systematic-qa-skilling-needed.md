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
- **2026-09-20 — first concrete piece: a demo button after Verify.** "a first example of this woudl be a button after "verify", available once you are in needs verification or later. the agent would then run your app in a way that illusrates the feature or fix"
- **2026-09-20 — the QA pane is linked back to the card, like Execute and Verify.** "something else -- we can do the same linking of the QA-support agent pane and the switchbaod card we did with execute and verify: #HKAP" — the demo hand-off writes the pane's `pane_token` into its thread entry, so the entry is a link that reveals that pane (`WindowManager::focusPane`, inert once closed). The mechanism already ships (#HKAP, 56b921f6 + 31468891); this card reuses it rather than adding a second one.
