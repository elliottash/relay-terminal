---
id: Z4HR
type: work
status: discussing
waiting_on: owner
labels: [feature, switchboard, docs]
assignee: ''
rank: m
created: '2026-09-20'
source: 'conversation, 2026-09-20'
links: {plans: [], commits: [], evidence: [], related: [VQ8T], github: null}
---
# One section per stage: a card's body follows the workflow

## Issue
Owner, 2026-09-20, asked what the segments on a card are and then specified the set. Their words:

> inbox -> issue i agree.
>
> discussing -> Discussion points
> that the user is thinking about or considering
>
> planning -> planning notes
>  this records decision factors, options that weren't taken in the plan, the agent questions asked (with options), and the user's answers.
>
> planned -> plan
>
> execute ->
> Execution Summary
> -- summary of what was built and links to the outputs.
>
> Tests
> -- the tests that were set up, if any
>
> verify->
> Verdict
> -- how it was checked and the result
>
> done ->
> Resolution
> -- time-stamped record of how card was removed.

## Decisions

**The organizing principle: one section per workflow stage.** A card's body is the record of what
each stage produced, in the order the stages happen.

| Stage | Section | What it holds |
|---|---|---|
| inbox | `## Issue` | the request, the owner's words verbatim |
| discussing | `## Discussion points` | what the owner is thinking about or considering |
| planning | `## Planning notes` | decision factors, options not taken, the agent's questions with their options, and the owner's answers |
| planned | `## Plan` | how it will be done |
| executing | `## Execution Summary` | what was built, and links to the outputs |
| executing | `## Tests` | the tests that were set up, if any |
| needs-qa-* | `## Verdict` | how it was checked, and the result |
| done / dropped | `## Resolution` | a time-stamped record of how the card was removed |

Plus `## Merged in` and `## Split`, which the merge and split tools write.

**`Verdict` and `Resolution` stop being synonyms.** `board_move_card` currently accepts `verdict`,
`qa verdict`, `qa result` **or `resolution`** as the QA gate (`board_tools.py:2074`), so a card
dropped because the owner changed their mind satisfies the gate that is supposed to mean "a
verifier checked this and it passed". Under the table above they are different claims at different
stages, and the gate takes a verdict only.

**`## Implementer check (not a QA verdict)` goes away.** 54 cards carry a heading with a disclaimer
baked into it, because implementers kept writing things that read as verdicts. With
`Execution Summary` and `Tests` named for the executing stage, there is nothing left for it to say.

### What this consolidates

Measured across the 338 cards on this board:

| Today | Cards | Becomes |
|---|---|---|
| 40 spellings of what-was-built (`Change` 51, `Behavior as implemented` 34, `Report` 25, `Implemented` 17, `What landed` 10, `As built` 5, …) | 190 | `## Execution Summary` |
| `QA checklist` | 251 | see question 1 |
| `Implementer check` and its variants | 64 | `## Tests` |
| `Decisions` | 58 | see question 3 |
| `Tasks` | 49 | see question 2 |

The body currently has **302 distinct headings across 338 cards, 217 of them used exactly once**.

`## Discussion points` and `## Planning notes` are genuinely new: discussing and planning produce
almost nothing durable in a body today (7 cards have a `## Why`, 2 have `## Options`). Planning
notes in particular is where the agent's questions and the owner's answers land, which is the one
stage whose whole output currently lives only in the append-only thread.

## Questions
Three sections in wide use have no home in the table above. On the card, numbered, with a
recommendation each — see the thread.
