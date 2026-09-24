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

<!-- relay:entry 20260920T200244Z-h1 author=claude-code kind=decision -->
### Claude Code · 2026-09-20 20:02
Owner answered all three, 2026-09-20: *"ah yes, QA checklist is needed, its created by execute and
could be adjusted by verify, and the as yet implemented QA support agent."* and *"OK for tasks and
decisions as well."*

All three recommendations taken. The QA checklist gains a detail that was not in the question: it
has **three authors** across two stages — the implementer writes it at executing, and both the
verifier and a **QA support agent that does not exist yet** may adjust it at verify.

Eleven sections, in body order, are in the card's `## Decisions`. Moved to `planned` with the
plan on it. The one judgement call I made rather than asked: **no bulk migration** — 190 cards
carry a what-was-built heading in 40 spellings, and rewriting them all in a checkout with twenty
live sessions is a bigger risk than the inconsistency. The checker warns instead, so the backlog
is visible and countable, and cards convert as they are touched.

<!-- relay:entry 20260920T202933Z-j2 author=claude-code kind=note -->
### Claude Code · 2026-09-20 20:29
**`## Tests` is already owned by #7BM4, which is executing now.** `"tests"` is in
`AGENT_SECTIONS` (`board_tools.py:120`) from its work, and its task `t:q5` builds the section
strip, the Check button, the dated `### Check` blocks and a gate on leaving `needs-verification`.

That definition is more specific than this card's "the tests that were set up, if any", and it
wins. Row 8 of the table stands, but the **format** of the section is #7BM4's, not this card's to
invent: a Markdown list, one line per test, an invocation plus an identifier, with Check appending
dated blocks under it. This card's docs task (`t:s6`) must describe that format and link #7BM4
rather than writing a second one. Commented on #7BM4 so its implementer knows before `t:q5` lands.

<!-- relay:entry 20260920T210744Z-k2 author=claude-code kind=decision -->
### Claude Code · 2026-09-20 21:07
**No profiling section. The set stays at eleven.**

The owner asked whether a card should record that an implementation was profiled. The answer is no,
and the reason applies to this card's whole design: *"many old cards become irrelevant to a
project."* Profiling state belongs on an object whose lifetime matches the code, not on a work item
that ages out. Recorded in full on #7BM4, whose `t:ea` owns profiling.

Worth keeping in mind for the section set generally: a section earns its place when it records what
a **stage produced**, which is a fact that stays true. A section that would have to be kept
*current* against the code does not belong on a card at all.

<!-- relay:entry 20260920T212617Z-26 author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Planned → Executing, session c23ba862

<!-- relay:entry 20260920T212617Z-66 author=owner kind=progress pane=switchboard pane_token=c23ba862-e892-4727-9209-756300e67ff0 -->
Claimed (c23ba862) · working on it from a terminal pane

<!-- relay:entry 20260920T214955Z-5k author=agent kind=event model=kimi-k3 pane=c23ba862 turn=6e530e9ab34243e791c8418791929218/02d27f062bc140f88cf49815c21f08d3 -->
- ✦ agent updated this card · appended to `## Execution Summary`; tasks: 6/6 done

<!-- relay:entry 20260920T215009Z-fb author=agent kind=event model=kimi-k3 pane=c23ba862 turn=6e530e9ab34243e791c8418791929218/02d27f062bc140f88cf49815c21f08d3 -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260920T215026Z-bd author=agent kind=event model=kimi-k3 pane=c23ba862 turn=6e530e9ab34243e791c8418791929218/02d27f062bc140f88cf49815c21f08d3 -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260920T215645Z-rh author=agent kind=event model=kimi-k3 pane=c23ba862 turn=6e530e9ab34243e791c8418791929218/02d27f062bc140f88cf49815c21f08d3 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["VQ8T"], "github": null… → {"plans": [], "commits": ["fdb662d6"], "evidence": ["docs/qa_evidence/2026-09-20…

<!-- relay:entry 20260920T215657Z-yp author=agent kind=event model=kimi-k3 pane=c23ba862 turn=6e530e9ab34243e791c8418791929218/02d27f062bc140f88cf49815c21f08d3 -->
- ✦ agent moved this card · Executing → Needs verification · All six plan tasks landed in fdb662d6: CARD_SECTIONS schema, AGENT_SECTIONS derived from it, the verdict gate takes a verdict only, check warns (never errors) on unknown_section, policy rule 10, deliver skill and docs. 370 board tests pass; live board checks with 0 errors. · evidence docs/qa_evidence/2026-09-20-one-section-per-stage/ · implemented_by kimi/kimi-k3

<!-- relay:entry 20260920T215839Z-ns author=agent kind=progress model=kimi-k3 pane=c23ba862 turn=6e530e9ab34243e791c8418791929218/02d27f062bc140f88cf49815c21f08d3 -->
Landed in `fdb662d6` (card bookkeeping in `a267702c`), moved to needs-verification with evidence `docs/qa_evidence/2026-09-20-one-section-per-stage/`.

All six plan tasks: `CARD_SECTIONS` in `board.py` with the `_check_sections` warning (`unknown_section`, warn-never-error, work cards only); `AGENT_SECTIONS` derived as the schema minus `issue`; the QA-close gate takes `verdict`/`qa verdict`/`qa result` only — `resolution` refused with a message saying why; policy v6 rule 10 with the stage/section table (`issues/POLICY.md` regenerated); the deliver skill names the per-stage sections; docs in SWITCHBOARD-FORMAT §2.7 and protocol §19.20.

Verified: `tests.test_board` + `tests.test_board_tools` 370 tests OK (six new/changed cases); live `relay-board.py check` — 358 cards, **0 errors**, 734 `unknown_section` warnings (the planned no-migration backlog, now countable). `tests_check` clean. The commit raced a busy main (three tips in ten minutes); landed with `--only-hunk` so the two other sessions' uncommitted hunks in `board_tools.py` and the protocol doc stayed out of it.

<!-- relay:entry 20260920T215922Z-c3 author=claude-code kind=comment -->
### Claude Code · 2026-09-20 21:59
fdb662d6 added rule 10's card-body schema to `backend/relay_core/board_policy.md`. Decision 8 of #GMCF (9f650d76) had just tiered that block so the model reads ~740 tokens of policy per board turn, and pinned it with `tests/test_system_prompt.py::test_the_board_policy_block_stays_tiered` (3,072 B budget); it now fails on main at 3,893 B — about 520 tokens back on every board turn. The pattern decision 8 used: a schema belongs in the tool description (`board_update_card`'s `replace_section`, or `board_create_card`) or in `docs/SWITCHBOARD-FORMAT.md`, with one sentence in the policy pointing at it. Yours to place; the test says what fits.
