<!-- Switchboard agent policy v5 (docs/SWITCHBOARD-DESIGN.md 6.2, owner decisions 12).
     v2, 2026-09-20 (#R9G7): work goes through a card, and a pane claims the card it works
     (`board_claim`, rules 1 and 5); the procedure is the bundled `deliver` skill.
     v3, 2026-09-20 (#R9G7, owner: "for small requests, we are not going to want the full
     workflow"): three tiers — small gets no card, medium a card the agent closes itself, large
     the full workflow.
     v4, 2026-09-20 (#7BM4): rule 6, the `## Tests` section and `tests_check` before
     needs-verification (protocol 31); the old rules 6-12 became 7-13.
     v5, 2026-09-20 (#GMCF, distillation decision 8): tiered the way v3 tiered the workflow.
     v6, 2026-09-20 (#Z4HR): rule 10, one body section per workflow stage; the verdict gate
     takes a verdict only, never a resolution.
     What has to be read *before* a board tool is called stays here; the detail a tool's own
     description or the `deliver` skill already states is read there instead — the landing detail
     of the three sizes and the `## Tests` section in the skill, the stamps and the close-a-QA-card
     rule in `board_move_card`, rewriting the user's text in `board_update_card`, labelling in
     `board_create_card`'s `labels`, the ceilings in the `board_rate_limited` refusal itself.
     Nothing was deleted, and `<board>/POLICY.md` — generated from this file plus that skill plus
     the appendix — still carries every sentence for a guest who has no board tools.
     Versioned here so evals can pin it; loaded into the system prompt by
     relay_core.board_tools.prompt_section when issues/board.yaml exists and autonomy is not off.
     Keep it short: every line costs context on every turn. -->

Switchboard rules (the `board_*` tools write to the repository's `issues/` tracker, in git):

1. **Capture, and work through a card sized to the work.** Every distinct request the user makes
   that you do not finish inside this turn becomes a card, or updates the card that already covers
   it: `board_list` with a query first, one card per request, and never a card for your own working
   steps (that is `update_todos`). *Small* — finished in this turn, verified by you (built, a test
   run, or seen working), no design choice, no question for the user — gets no card; the commit is
   the record. *Medium* (more than one turn or more than two files, no decision needed, a test
   proves it) and *large* (needs a plan, a decision from the user, or changes UI) are started only
   after you have checked the work is not already done and claimed the card that asks for it
   (`board_claim`, on the card you found or the one you just created). Load the **`deliver`** skill
   for the procedure; `/deliver` makes any request large, "just do it" or "no card" makes it small.
2. **The user's words are the record.** `request` is what they wrote, verbatim — do not paraphrase,
   correct or tidy it. The title is yours.
3. **Questions** for the user go on the card as a `question` comment, and the card goes to
   `discussing` with `waiting_on: owner`. Name the card in your reply rather than burying the
   questions in the terminal or the board page's chat — wherever you ask, the card is where
   the question waits.
4. **Decisions** the user makes, in the terminal or on a card, go into a `decision` comment quoting
   their own words in quotation marks, and into the card's `## Decisions` section.
5. **Work.** `board_claim` records the `session` that is this pane, so a card in Executing with
   another `session` is that session's work — comment on it, and claim it only when the user says
   to take it over. It lands in the same commit as the change: a *medium* card you move to `done`
   yourself, a *large* one to `needs-verification` with its evidence path and a `## QA checklist`.
   The skill has the detail.
6. **Unrelated faults** you notice on the way become a new card in the bugs tab with the measured
   evidence — never a silent fix and never a detour.
7. **Other people's cards:** comment, never reassign and never rewrite what they wrote. **Nothing
   is deleted** and there is no delete tool: a card is closed by moving it to `done` or `dropped`
   with a reason, and the thread is append-only. Only the owner can delete a card, from the GUI.
8. **Limits.** A few cards per turn and per hour. When a tool answers `board_rate_limited`, stop
   writing and summarize the rest of the requests in your reply.
9. **Report what you did.** After a card write, name the card as `#ID` in your reply with one line
   about the change, so the user can find it.
10. **A card body is one section per stage**, written as the stage produces it, in this order:
    `## Issue` (the request, verbatim — the owner's words), `## Decisions` (owner decisions,
    quoted, whenever they happen), `## Discussion points` (what the owner is considering),
    `## Planning notes` (decision factors, options not taken, questions asked with their
    options, the owner's answers), `## Plan`, `## Tasks` (the live checklist), `## Execution
    Summary` (what was built, links to the outputs), `## Tests` (what was automated),
    `## QA checklist` (what a verifier must check by hand; the verifier may adjust it),
    `## Verdict` (the verifier: how it was checked, and the result — a `## Resolution` is not
    one), `## Resolution` (when and why the card closed). Write the section your stage
    produces and invent no others; `relay-board.py check` warns on any heading outside the set.
