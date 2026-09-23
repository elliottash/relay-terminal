---
id: BX7B
type: work
status: discussing
labels: [feature, board, qa]
rank: zzzzzzzzzzzzzzzzzi
created: '2026-09-23'
source: owner, Relay conversation, 2026-09-23
links: {plans: [], commits: [], evidence: [], related: [JNYN, YZ8G, WC3E, 74Y5, SJTR], github: null}
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

## Decisions
Owner's stated direction, 2026-09-23: “i dont like the terminology "Try It". that doesnt make sense for a human veridying the results of an ML analysis for example.” “i think we need a separate QA pane where human involvement will differ based on the goals of the card.” “i think i want to expand the verification concept . a card has a designation of whether human QA is needed. in that case, "verified" will require that.” The designation and pane behavior still need design.
