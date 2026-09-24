---
id: WFRA
type: work
status: needs-verification
labels: [feature, switchboard, board, qa]
assignee: agent
component: [worker, gui]
parent: BX7B
rank: zzzzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: owner, Relay conversation, 2026-09-23
implemented_by: anthropic/claude-fable-5-1
verify: {artifact: code, primary: script, also: [ai-text, ai-visual], human: optional, criteria: the claim reminder and a refusal read as one sentence each and the strip reads in one line, sign_off: none, effort: medium}
links: {plans: [], commits: [39eedbfb687440ce50740859a18353641a5d1e5b, d1c9c9af6e608f826c924f0a92ebf9fc7f7d4961], evidence: [docs/qa_evidence/2026-09-23-WFRA-strip/], related: [BX7B, 1QKM, WC3E, JNYN, 74Y5], github: null}
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

## Plan
**Goal.** One `verify` block per work card, validated in one place, visible on the row and the card page, proposed by the agent at step 4.
**Findings.** Front matter is `relay_core.board.parse_yaml` (flow maps round-trip); fields are `WORK_FIELDS`/`ALLOWED_FIELDS`; `board_read` returns `front` whole, so the block reaches the `board_card` event with no new wire field; `Board.check` and `index_markdown` are where a rule and a column go; the policy block is capped at 3 KB (`tests/test_system_prompt.py`), so the rule is one tiered line and the ladder lives in the `deliver` skill and the tool description.
**Steps.** 1. `validate_verify` / `verify_block` / `verify_summary` in board.py, `verify` in the work-card field set. 2. `check`: `bad_verify` error, `missing_verify` warning from executing on. 3. `BOARD.md` Verify column; `board_list` row cell. 4. `board_update_card fields.verify`, `board_read` normalization + `verify_error`, `board_claim` reminder. 5. Policy rule 11, `deliver` step 4 and 5, POLICY.md regenerated, protocol 19.21. 6. Tests. The card-page strip is the GUI session's (`BoardPane.cpp`), from the same `front.verify` object.

## Tasks
- [x] Schema, validation and helpers in `relay_core.board` <!-- t:8f -->
- [x] `check`, `BOARD.md` column, `board_list` cell <!-- t:jx -->
- [x] `board_update_card fields.verify`, `board_read`, `board_claim` reminder <!-- t:vk -->
- [x] Policy, `deliver` skill, POLICY.md, protocol 19.21 <!-- t:8m -->
- [x] Tests in `tests/test_board.py` and `tests/test_board_tools.py` <!-- t:da -->
- [x] Card-page strip (GUI session, `BoardPane.cpp`, d1c9c9af) <!-- t:em -->

## Execution Summary
- Python half (subagent wfra-backend): Python side, commit 39eedbfb (#WFRA). `relay_core.board` gained the ladder's vocabulary (`VERIFY_*`), `validate_verify` (refuses naming the bad key or value; normalizes: `also` a list, `human`/`sign_off` default `none`), `verify_block`, `verify_summary` and `deferred_text`; `verify` is a work-card field and round-trips as a flow map. `Board.check` errors `bad_verify` and warns `missing_verify` from `executing` on (live statuses only; closed cards are history). `BOARD.md` has a Verify column (primary mode + `person` / `person?` flag, or `unverified until …`), and `board_list` rows carry the same `verify` cell. `board_tools`: `board_update_card fields.verify` validates; `board_read` returns the block normalized under `front.verify` (so the `board_card` event carries it) and names an invalid stored one in `verify_error`; `board_claim` returns a one-line `reminder` when the card has none. Policy rule 11 (two lines: the block is capped at 3 KB by `tests/test_system_prompt.py`, so rules 5-8 and 10 lost a few words and the ladder itself lives in the `deliver` skill step 4 and the tool description), `issues/POLICY.md` regenerated, protocol 19.21 documents the wire shape for the GUI. The card-page strip (`BoardPane.cpp`) is the GUI session's and draws from `front.verify`.
- GUI half (subagent wfra-gui): the Verify strip on the card page — `board::VerifyPlan`, `verifyStripText`, `verifyPlanDetail` in `src/BoardModel.*`, `showVerifyPlan` and the `boardVerifyStrip` label under the Try it strip in `src/BoardPane.cpp`; commit d1c9c9af, evidence bf950a9a (`docs/qa_evidence/2026-09-23-WFRA-strip/`).

## Tests
- `PYTHONPATH=backend python3 -m unittest tests.test_board.VerifyBlockTests`
- `PYTHONPATH=backend python3 -m unittest tests.test_board_tools.VerifyFieldTests`
- `PYTHONPATH=backend python3 -m unittest tests.test_board tests.test_board_tools`
- `PYTHONPATH=backend python3 -m unittest tests.test_system_prompt.SizeTests.test_the_board_policy_block_stays_tiered`
- `PYTHONPATH=backend python3 scripts/relay-board.py check` — 0 errors on the live board
- `ctest --test-dir build -R '^board$'` — passed (theVerifyBlockReadsIntoOneStripLine, tests/boardmodel_test.cpp).
- `ctest --test-dir build -R '^boardpane$'` — passed (theVerifyStripReadsTheCardsVerifyBlock, tests/boardpane_test.cpp).
- `ctest --test-dir build -R '^boardsections$'` — passed (unchanged; the strip is not a section-editor case).
- `ctest --test-dir build -R '^board$'` — passed (theVerifyBlockReadsIntoOneStripLine, tests/boardmodel_test.cpp).
- `ctest --test-dir build -R '^boardpane$'` — passed (theVerifyStripReadsTheCardsVerifyBlock, tests/boardpane_test.cpp).
- `ctest --test-dir build -R '^boardsections$'` — passed (unchanged; the strip is not a section-editor case).
