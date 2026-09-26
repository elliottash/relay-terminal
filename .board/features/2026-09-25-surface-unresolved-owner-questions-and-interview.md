---
id: MRQT
type: work
status: planned
labels: [feature, switchboard, board]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
source: Relay conversation, 2026-09-25
links: {plans: [], commits: [], evidence: [], related: [JDN4], github: null}
---
# Surface unresolved owner questions and interview from the card

## Issue
Treat requests for an owner decision as structured pending questions, distinct from decisions already made. Show their count and an Answer questions action on Board cards; use the card's Discuss turn and normal question UI to collect answers, then record linked decisions in the thread and ## Decisions. Tighten prompts so unanswered owner questions cannot live only in Decisions, Risks, or discussion prose. Extend Board hygiene to find and reconcile existing cards with buried or stale questions, preserving their original history.

> i agree, put that on a card. 
>
> and i think the hygiene should also fix this in existing cards.
> — elliott · [session:3d9f0c2a853a40a180759676b456c60f](relay://session/3d9f0c2a853a40a180759676b456c60f) · 2026-09-25

## Planning notes
**Question and decision lifecycle.** An owner-facing choice is a pending question with a stable ID, options/recommendation, and a link to its originating thread entry. Answering through the card's Discuss interview records the owner's words as a linked decision and summarizes the settled choice in `## Decisions`. An agent assumption stays in Plan/Risks with its rationale; it is not attributed to the owner. Prompt rules must prohibit unanswered owner requests appearing only in `## Decisions`, Risks, or planning prose. The current Plan brief's “Risks, or question comment” wording must change: a human answer request always creates a pending question, while Risks may explain its impact.

**Board presentation.** Show the pending count on list rows and an Open questions panel plus Answer questions action on the card. The normal question workflow should run on the card's Discuss turn on desktop and phone; answers must be linked and persisted one by one so interruption does not lose earlier answers. `waiting_on` alone remains too generic to identify a particular unanswered question. Moving a card to Planned or Running must not silently settle one.

**Existing-card hygiene.** Audit questions in `## Decisions`, `## Planning notes`, `## Discussion points`, Plan/Risks prose, question thread entries, and `## Human QA`. Reconcile each against later owner answers and plan changes before marking it pending; preserve original text and thread history. Ambiguous or obsolete questions should be flagged for review rather than auto-answered. Clear stale `waiting_on: owner` only when no owner input remains. Include #JDN4 as a regression case: its two questions were followed by an assumed plan and execution while `waiting_on: owner` remained.

**Other structure.** Reuse the question lifecycle for `## Human QA`, which currently has its own numbered-question/indented-`Answer:` syntax and close gate. Keep `## Decisions` as a digest, not a second state store. Tasks, dependencies, due dates, and verification already have structured fields or rules; do not introduce new schemas for ordinary discussion or risks unless a separate actionable state is demonstrated.
