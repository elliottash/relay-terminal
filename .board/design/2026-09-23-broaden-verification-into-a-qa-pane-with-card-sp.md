---
id: BX7B
type: work
status: needs-verification
labels: [feature, board, qa]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: c74ef5a7-e3b2-4741-b4d4-251871e1398e
rank: zzzzzzzzzzzzzzzzzi
created: '2026-09-23'
verify: {artifact: visual, primary: script, also: [probe, ai-visual, person], human: required, criteria: Review view shows the right cards and a clear one-step judgement on a live card, sign_off: none, effort: high, stakes: rework, blast: capability}
source: owner, Relay conversation, 2026-09-23
links: {plans: [], commits: [25deb67f, 35bfbcb2, 31e11b6c, fbf43d9c, 9b7ac698, 1bb32fe8, ec074443], evidence: [reports/Knowledge work across projects.md, docs/qa_evidence/2026-09-24-BX7B-review/], related: [JNYN, YZ8G, WC3E, 74Y5, SJTR, 1QKM, WFRA, 1AA6, MSJ0, C3Q2, 95VZ], github: null}
---
# Broaden verification into a QA pane with card-specific human review

## Issue
write a discussion card summarizing this conversation thread, with links to a more detailed report if needed

## Discussion points
The owner finds **“Try it”** too app-specific: it does not fit a person verifying the results of an ML analysis. They propose a separate **QA pane** in which the person's role depends on the card's goal. A card should designate whether human QA is needed; when it is, “verified” requires that human step. This is a product direction to design, not a gate implemented today.

The existing pieces are narrower. #JNYN supplies a staged interaction, one task, one question, an answer and a reveal on the card page; it remains in needs-verification pending a combined current-build recheck. #YZ8G sets the broader approach: intended outcome, artifact, evidence, AI verification and only the remaining human judgement, across apps, systems, analysis and research. #WC3E covers independent verification; #74Y5 distinguishes check, agent and person steps in the user path. #SJTR discusses a broader agent-led QA system from the Tests entry point. There is no dedicated QA pane yet.

The pane could show the goal, current deliverable, checks and evidence, then present the review appropriate to that card: a staged app interaction, a backend or CLI result, changed rows and reconciled totals, or an ML analysis with examples and claims to judge. Some cards may need no human step. Keep mechanical work with agents, use human time for an explicit judgement, and record the answer against the revision reviewed.

Open design choices: who sets and may change the human-QA designation; how the gate distinguishes required, optional and not applicable review; what “verified” means relative to existing QA lanes and `done`; how the pane draws artifacts and evidence without imposing one review shape; and whether “Review” replaces the “Try it” label or incorporates it as one action. These are questions for the design pass, not decisions already made.

Detailed background: [QA across fields](../../docs/QA-ACROSS-FIELDS-RESEARCH.md), [#YZ8G](../features/2026-09-19-systematic-qa-skilling-needed.md), [#JNYN](../features/2026-09-21-try-it-stage-play-and-hand-over-one-question.md), and [app-driving guidance](../../docs/DRIVING-APPS.md).
### The QA ladder (owner + agent, 2026-09-23)

Asked once per task, cheapest and strongest oracle first. The agent stops at the first yes for the **primary** (gating) mode and keeps going for **supplementary** ones, then proposes the result on the card as a `verify` block (#WFRA). The user corrects it; corrections are ledger data (#95VZ).

- **A. What is the artifact?** text / number / code / visual / audio / running system / physical / decision. Sets which rungs are possible.
- **B. Can the agent run it at all?** If not (experiment, rehearsal, device playtest, client), verification is *deferred*: the card says "unverified until X" with X named and a person owning it; no model estimate stands in (#1AA6).
1. **Can a test script confirm it?** build, unit test, diff, schema, reconciling total, hash → hard gate, every time, no person.
2. **Can a probe confirm it on the live artifact?** HTTP codes, page grep, origin hash, API readback, `systemctl` → hard gate on the deployed thing, not the recipe.
3. **Is there a metric with a threshold?** recall on a ground-truth set, a ceiling, a CI, κ → gate only if calibrated: reported with its denominator, threshold set before the run; otherwise advisory.
4. **Can AI confirm from text output?** logs, diffs, tables, paper vs results, citation vs source → advisory until this reviewer has a measured false-positive rate on this task; then it may gate at a sampled rate; different model family from the author.
5. **Can AI confirm from visual / multimodal output?** rendered page, screenshot, plot, contact sheet, audio → advisory for quality, may gate for nameable defects (overflow, stand-in asset, missing label, wrong orientation); the evidence is the image, never the words "visually checked".
6. **Are there levels rather than a metric?** recommendation scale, rubric, severity ladder → criteria written before the judgement; the verdict records the level and who gave it.
7. **Pairwise rather than pointwise?** yes for perceptual artifacts, batches (editor, PC, hiring) and "better than last version": old-vs-new side by side, blind where possible, several judges compared (`blinded-model-review`).
8. **Must a person look, listen, play or read?** then two more answers: the pass criteria, written now, and the device / entry point. This is the queue the QA pane rations.
9. **Is sign-off required by rule regardless?** money, publish, send, delete, legal, clinical, anything irreversible → click-to-confirm with a receipt, never sampled.
10. **Blast radius if wrong?** one case or every case → if the capability, a dry run, a before-image and a rollback path before the gate passes.

**Effort** follows stakes × novelty: routine and low stakes → low; novel or high stakes → high; a rule-based sign-off means the person's time is the effort.

**Automatic or not, three layers** (#C3Q2): a global floor in Options (which stakes classes always need a person, which rungs may gate alone, default sampling); per-project overrides in `board.yaml`; and the server's own declared profile (#MSJ0) as the proposal. The user never fills in the ladder; they see the proposal and can say "always ask me" or "stop asking" at any layer.

**Verified** then means: the primary mode passed with evidence, *and* the human step is answered when `verify.human` is `required`, *and* the sign-off receipt exists when one is required. A deferred card is not verified and says until when.
### Status, 2026-09-23 (end of day)

Landed on `main`: #WFRA (verify block, agent-facing; strip shown only for a review, sign-off or deferral, evidence `docs/qa_evidence/2026-09-23-WFRA-strip/`), #1AA6 (`verified()`, the done / needs-qa gates), #MSJ0 (skill `profile:` and the claim-time default from the loaded skill), #C3Q2 (`qa_policy`, the one Options row, `board.yaml qa:`). #95VZ (case ledger) in progress. All four are in `needs-verification` for a verifier outside the Anthropic lineage.

**One decision made in passing, for the owner to confirm or reverse:** under `Verification: ask`, a card with `verify.human: optional` counts as "needs no person" for the ask gate (the verifier may not close it in `ask` mode either way; in `automatic` mode it closes). `required` stays the hard gate. If `optional` should also hold the card for the person in `automatic` mode, say so on #C3Q2.

## Decisions
Owner's stated direction, 2026-09-23: “i dont like the terminology "Try It". that doesnt make sense for a human veridying the results of an ML analysis for example.” “i think we need a separate QA pane where human involvement will differ based on the goals of the card.” “i think i want to expand the verification concept . a card has a designation of whether human QA is needed. in that case, "verified" will require that.” The designation and pane behavior still need design.
- 2026-09-23, owner: "ideally, most of this is just in the agent's work and the user doesn't see it directly." Applied across the build: the `verify` block, the skill profile and the policy floor are agent-facing data and rules; the user meets them only as a one-line review request when a person is needed, an "unverified until …" state, a refusal sentence, and a single Options switch (ask | automatic). No BOARD.md column, no Skills-dialog line, no placeholder strip on cards that need no person.

## Plan
**Goal.** Give human review a focused pane; keep the QA ladder and machine checks in the agent workflow.

**Findings.** #WFRA, #1AA6, #MSJ0, #C3Q2 and #95VZ are landed. `board_open` sends small card rows, while `board_card_get` provides the card's full verify block and sections. `BoardView` already handles staged Try it answers. The separate Test suites pane provides the host/worker wiring pattern.

**Steps.** (1) Verify the five landed pieces and repair regressions found. (2) Add a small review queue derived from cards with a pending human step, ordered by stakes then uncertainty. (3) Add a separate Review pane that opens the selected card's goal, deliverable and evidence and focuses the one human question or sign-off. Keep Try it staging as an action for cards that need it. (4) Wire the pane to the board worker, restore it with the layout, and rename the user action Review. (5) Capture a real screenshot and run targeted backend and GUI tests.

**Risks.** An answer must be tied to the revision read, and stale answers must be refused. Existing board rows omit verify details by design; the queue gets only the minimum human-review flags, while the full plan stays in board_card. The UI must not turn routine machine QA into a user task.

**Verify.** Targeted board and GUI tests on the exact landed tree, plus a live Review pane showing a required judgement and a machine-only card absent from the queue.

## Done means
The Board has one Review view that lists cards needing a person's judgement, ordered by stakes and uncertainty. Opening a row shows the goal, deliverable and evidence, with only the person's question or sign-off as an action. Existing staged Try it cases remain usable through Review. A recorded answer is tied to the card revision; cards without a human step stay out of the queue.

## Execution Summary
Built a separate Review pane beside the Board. Its queue contains only built cards with an unanswered required human question or missing sign-off; stakes and uncertainty order the rows. Selecting a card shows its goal, pass criterion, result and evidence, then one answer field or explicit sign-off confirmation. The answer writes `## Human QA` against the card hash and names the revision reviewed; a stale write keeps the person's draft and reloads the card. Internal QA plans stay off this view. The existing Try it protocol remains available through the card, with its visible action renamed Stage review. Repaired the two malformed QA thread IDs and landed the generated policy that #95VZ had left pending. Fixed the multi-question gate so every numbered human question must be answered.

Evidence: `docs/qa_evidence/2026-09-24-BX7B-review/01-review-queue.png` and its README. The exact landed C++ tree built through `land.py`; focused Python and GUI tests pass.

## Tests
- `PYTHONPATH=backend python3 -m unittest tests.test_board.VerifiedTests tests.test_board_tools.VerifyGateTests tests.test_cases tests.test_qa_policy`
- `ctest --test-dir build -R '^(boardpane|reviewpane)$' --output-on-failure`
- manual: `docs/qa_evidence/2026-09-24-BX7B-review/01-review-queue.png`

## Human QA
1. Does the Review pane show the right cards and leave you one clear judgement, with internal QA details out of sight?
