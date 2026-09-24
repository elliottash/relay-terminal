---
id: BX7B
type: work
status: discussing
labels: [feature, board, qa]
rank: zzzzzzzzzzzzzzzzzi
created: '2026-09-23'
source: owner, Relay conversation, 2026-09-23
links: {plans: [], commits: [], evidence: [reports/Knowledge work across projects.md], related: [JNYN, YZ8G, WC3E, 74Y5, SJTR, 1QKM, WFRA, 1AA6, MSJ0, C3Q2, 95VZ], github: null}
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

## Decisions
Owner's stated direction, 2026-09-23: “i dont like the terminology "Try It". that doesnt make sense for a human veridying the results of an ML analysis for example.” “i think we need a separate QA pane where human involvement will differ based on the goals of the card.” “i think i want to expand the verification concept . a card has a designation of whether human QA is needed. in that case, "verified" will require that.” The designation and pane behavior still need design.
- 2026-09-23, owner: "ideally, most of this is just in the agent's work and the user doesn't see it directly." Applied across the build: the `verify` block, the skill profile and the policy floor are agent-facing data and rules; the user meets them only as a one-line review request when a person is needed, an "unverified until …" state, a refusal sentence, and a single Options switch (ask | automatic). No BOARD.md column, no Skills-dialog line, no placeholder strip on cards that need no person.

## Plan
**Goal.** Build the whole QA ladder, phased so each step adds one visible thing and nothing asks the user a question they did not ask for.

**Findings.** The board already has `needs-qa-llm` / `needs-qa-human` statuses (`relay_core/board.py` `_STATUS_ORDER`, `COLUMN_STATUSES`), a `## Human QA` question that blocks close, a `qa` block naming the verifier (protocol 19), Try it (#JNYN, `board_tryit_brief.md`, `BoardPane.cpp` "Try it" strip) and a flat `SKILL.md` frontmatter parser (`relay_core/skills.py`). The ladder is therefore data on cards and skills plus rules in the worker, not a new subsystem.

**Steps (one card each).**
1. **#WFRA** — the `verify` block: schema, validation, `board_update_card fields.verify`, policy and `deliver` text, `relay-board.py check`, the card-page strip. *Visible:* one strip on the card page; one reminder line at claim.
2. **#1AA6** — `verified()` in one place; `human: required`, `sign_off` and `deferred` gate `done`. *Visible:* one refusal message; "unverified until …" on a row.
3. **#MSJ0** — `profile:` on skills; a card claimed under a skill inherits its verify defaults and effort; six example profiles. *Visible:* a second line in the Skills dialog.
4. **#C3Q2** — the policy floor in Options › Agent › QA and `board.yaml qa:`; conservative defaults so nothing changes until the user opens it. *Visible:* one Options heading.
5. **#95VZ** — the case ledger and the Skills list's cases / pass rate / stale column; the third-case suggestion line. *Visible:* numbers beside skills; one sentence in a reply.
6. **This card** — the QA pane: "Review" replaces the Try it label and incorporates it as one action; the pane shows goal, deliverable, checks and evidence, then the human step the `verify` block names; a cross-card human queue ranked by stakes × doubt, not FIFO. Planned after 1–5 land and the strip has been used for a week.
7. Later: a Skills registry surface (versions, evals, rot) — needs 5.

**Risks / owner decisions.** Vocabulary of `primary` and `stakes` is fixed on #WFRA and shared by all five; changing it later touches all. Whether "Review" is the final label (open on this card). Who may set a server's profile (#1QKM question 6).

**Verify.** Each card's own tests; then one end-to-end: claim a card under a profiled skill, see the strip, try to close it with `human: required` unanswered and be refused, answer, close; screenshot the strip and the refusal into `docs/qa_evidence/`.
