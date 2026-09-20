---
id: Z4HR
type: work
status: planned
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

## Decisions (owner, 2026-09-20)

All three open questions answered; all three recommendations taken.

- **`## QA checklist` stays.** Owner: *"QA checklist is needed, its created by execute and could be
  adjusted by verify, and the as yet implemented QA support agent."* So it has **three authors**:
  written at executing by the implementer, and adjustable at verify by the verifier and by a QA
  support agent that does not exist yet. It is not `Tests` (what was automated) and not `Verdict`
  (written afterwards, by the verifier).
- **`## Tasks` stays**, as a stage *instrument* rather than a stage output: the live working
  checklist spanning planned through executing. Its `<!-- t:xx -->` markers are parsed and counted
  into the board row, so it is structural regardless.
- **`## Decisions` stays**, as the one cross-cutting section, for owner decisions wherever in the
  card's life they happen. `Planning notes` holds the planning-stage question-and-answer record.

### The set, in body order

| # | Section | Stage | Written by | Holds |
|---|---|---|---|---|
| 1 | `## Issue` | inbox | the owner, or an agent quoting them | the request, verbatim |
| 2 | `## Decisions` | any | an agent, quoting the owner | owner decisions, wherever they happen |
| 3 | `## Discussion points` | discussing | the owner | what they are thinking about or considering |
| 4 | `## Planning notes` | planning | the planner | decision factors, options not taken, the agent's questions with their options, and the owner's answers |
| 5 | `## Plan` | planned | the planner | how it will be done |
| 6 | `## Tasks` | planned → executing | the implementer | the live checklist; markers counted into the board row |
| 7 | `## Execution Summary` | executing | the implementer | what was built, and links to the outputs |
| 8 | `## Tests` | executing | the implementer | the tests that were set up, if any |
| 9 | `## QA checklist` | executing, adjusted at verify | implementer, then verifier, then the QA support agent | what a verifier must check by hand |
| 10 | `## Verdict` | needs-qa-* | the verifier | how it was checked, and the result |
| 11 | `## Resolution` | done / dropped | whoever closes it | a time-stamped record of how the card was removed |

`## Merged in` and `## Split` are written by the merge and split tools. The **thread** is not a body
section: it is the append-only file under `issues/threads/`, and it holds the history that these
sections digest.

`## Decisions` is placed second because it is cross-cutting and a reader needs it before the plan,
not because it is written then.

## Plan

1. **`board_policy.md`** carries the table. This is the piece that actually changes behaviour: it is
   in every agent's system prompt, and today it says nothing about sections at all.
2. **`AGENT_SECTIONS`** (`board_tools.py:115`) becomes this set, so it is a schema rather than a
   13-name rewrite allowlist.
3. **The verdict gate** (`board_tools.py:2074`) stops accepting `resolution` as a verdict. Passing
   QA and closing for another reason become different claims.
4. **`relay-board.py check`** warns on a heading outside the set, the way it already warns on
   missing task markers. **Warn, not error**: 338 cards carry 302 distinct headings and an error
   would invalidate the board on day one.
5. **The `deliver` skill** names the section each stage writes, so the procedure and the schema agree.
6. **Docs**: `docs/SWITCHBOARD-FORMAT.md`, and the section list in `docs/AGENT-SESSIONS-PROTOCOL.md`
   §19.
7. **No bulk migration.** Existing cards are left alone and converted when a card is next touched.
   190 cards carry a what-was-built heading in 40 spellings; rewriting them all at once in a
   checkout with twenty live sessions is a far bigger risk than the inconsistency it removes. The
   checker's warning makes the backlog visible and countable.

## Tasks
- [ ] `board_policy.md`: the stage/section table <!-- t:s1 -->
- [ ] `AGENT_SECTIONS` becomes the schema <!-- t:s2 -->
- [ ] the verdict gate stops taking `resolution` <!-- t:s3 -->
- [ ] `relay-board.py check` warns on an unknown heading <!-- t:s4 -->
- [ ] the `deliver` skill names the per-stage section <!-- t:s5 -->
- [ ] docs: SWITCHBOARD-FORMAT and protocol §19 <!-- t:s6 -->
