<!-- relay:entry 20260920T191326Z-g1 author=claude-code kind=decision -->
### Claude Code · 2026-09-20 19:13
Owner specified the section set in conversation, 2026-09-20, quoted verbatim in the card's
`## Issue`. The organizing principle is one section per workflow stage, so a card body is the
record of what each stage produced, in the order the stages happen.

Measured against the 338 cards here: **302 distinct headings, 217 used exactly once**. The scheme's
largest single win is `## Execution Summary`, which replaces **190 cards' worth of invented
what-was-built headings in 40 spellings** (`Change` 51, `Behavior as implemented` 34, `Report` 25,
`Implemented` 17, `What landed` 10, `As built` 5, and 34 more).

It also settles a gate bug I found earlier: `board_move_card` accepts `resolution` as a QA verdict
(`board_tools.py:2074`), so a card dropped because the owner changed their mind currently satisfies
the gate that means "a verifier checked this and it passed". The owner's definitions make Verdict
and Resolution different claims at different stages, so the gate takes a verdict only.

<!-- relay:entry 20260920T191326Z-g2 author=claude-code kind=question -->
### Claude Code · 2026-09-20 19:13
Three sections in wide use have no home in the table. Each needs a decision before this is
executable.

**1. `## QA checklist` — 251 cards, the most common heading on the board. Where does it go?**
It is not `Tests` and it is not `Verdict`. Tests is what was automated. Verdict is written
afterwards by the verifier. The checklist is written *before* verification, by the implementer, as
the instructions a human or a different model follows to produce that verdict — "click this, see
that". Policy rule 5 requires it in the same commit as the change. Three authors, three times.
*Recommend: keep it as a third output of executing, beside Execution Summary and Tests.* The
alternative is to make `Tests` hold both, with the checklist as its un-automated half, which is
fewer sections but mixes "what runs by itself" with "what a person must do".

**2. `## Tasks` — 49 cards, and structural. It cannot simply be dropped.**
Its checkbox items carry `<!-- t:xx -->` markers that are parsed, counted into the board row and
drawn in the pane. *Recommend: keep it, and treat it as not a stage output but a stage instrument —*
the working checklist that spans planned through executing, the way the thread spans everything.
It is the one section that is live while work happens rather than a record written at the end.

**3. `## Decisions` — 58 cards, and policy rule 4 requires it.**
`Planning notes` covers the owner's answers *at the planning stage*. A decision made while
discussing, before any plan exists, or one made mid-execution that changes direction, has nowhere
to go under the table. *Recommend: keep `## Decisions` as the one cross-cutting section,* for
owner decisions wherever in the card's life they happen, with Planning notes holding the
planning-specific question-and-answer record. The alternative is to let non-planning decisions live
only in the thread as `kind=decision` comments, which is where they are already logged — but then
the body has no digest of why the card is shaped the way it is.

Status `discussing`, `waiting_on: owner`.
