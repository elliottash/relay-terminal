---
id: WFRA
type: work
status: planned
labels: [feature, switchboard, board, qa]
component: [worker, gui]
parent: BX7B
rank: zzzzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: owner, Relay conversation, 2026-09-23
links: {plans: [], commits: [], evidence: [], related: [BX7B, 1QKM, WC3E, JNYN, 74Y5], github: null}
---
# The QA ladder: a `verify` block on every card, proposed by the agent, shown on the card page

## Issue
thinking about QA, help me come up with a checklist like the following:
-- can a test script confirm?
-- can AI confirm based on text output?
-- can AI confirm based on visual / multimodal output?
-- is there a verifiable metric? or perhaps, are ther levels. like for art/design, that is the lowest level.
-- should the AI do pairwise comparisons rather than pointwise evals?

agent should make this suggestion, and also the effort level, per task.

[...] yes, document it, and lets build all the functionality, and we can experiment with how to phase in complexity without overwhelming the user

## Done means
- A work card can carry a `verify:` block in its front matter with exactly these keys and vocabularies, validated on read and on `board_update_card` (`fields.verify`), refused with a message naming the bad key or value:
  `artifact` (code|text|number|visual|audio|system|physical|decision), `primary` (script|probe|metric|ai-text|ai-visual|level|pairwise|person|world), `also` (list of the same), `deferred` (free text "until …", optional), `human` (none|optional|required), `criteria` (one line, required when human ≠ none), `sample` (optional, e.g. "1/10 after 30"), `sign_off` (none|money|publish|send|delete|legal|clinical), `effort` (low|medium|high), `stakes` (nuisance|rework|money|reputation|harm), `blast` (case|capability).
- `board_read` returns it; `relay-board.py check` warns on a card in `executing` or later with no `verify` block and errors on an invalid one; `BOARD.md` rows show the primary mode and the human flag in one short column.
- The Board policy (`board_policy.md`, so `POLICY.md` and the system prompt) and the `deliver` skill tell the agent to propose the block at step 4 beside `## Done means`, in the ladder order, and to state the effort; a card claimed without one gets a one-line reminder in the claim result.
- The card page shows the block as one strip under the action row ("Verify: probe · also ai-visual, pairwise · person required: <criteria> · effort medium"), drawn from the same section machinery as the `## Try it` strip; a missing block shows "No verify plan yet".
- Tests: `tests/test_board.py` (parse, validate, refuse, BOARD.md column), `tests/test_board_tools.py` (update via fields, claim reminder), a `boardsections_test.cpp` or `boardpane_test.cpp` case for the strip. Failure shows as: an invalid block accepted silently, or a card page with a block and no strip.
