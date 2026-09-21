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

Switchboard rules (`board_*` writes the project tracker in git):

1. **Capture work at its size.** Check code, history and `board_list` first; do not redo done work.
   Each unfinished user request gets a card, or updates its existing card. One card per request,
   never for your steps (`update_todos`). *Small*: finished and verified this turn, no design choice
   or question; no card, the commit is the record. *Medium*: more than one turn or two files,
   no decision needed, proved by a test. *Large*: needs a plan, a user decision or changes UI.
   Before medium/large work, find or create the card and `board_claim` it. Load **`deliver`** for
   the procedure. `/deliver` makes work large; "just do it" or "no card" makes it small.
2. **Verbatim requests.** `request` is the user's words verbatim; do not paraphrase or tidy. Write the title.
3. **Questions** for the user go on the card as a `question` comment, and the card goes to
   `discussing` with `waiting_on: owner`. Name the card in your reply rather than burying the
   questions in the terminal or the board page's chat.
4. **Decisions** the user makes, in the terminal or on a card, go into a `decision` comment quoting
   their own words in quotation marks, and into the card's `## Decisions` section.
5. **Work.** `board_claim` records the `session` that is this pane, so a card in Executing with
   another `session` is that session's work — comment on it, and claim it only when the user says
   to take it over. It lands in the same commit as the change: a *medium* card you move to `done`
   yourself, a *large* one to `needs-verification` with its evidence path and a `## QA checklist`.
6. **Unrelated faults** you notice on the way become a new card in the bugs tab with the measured
   evidence — never a silent fix and never a detour.
7. **Other people's cards:** comment, never reassign and never rewrite what they wrote. **Nothing
   is deleted** and there is no delete tool: a card is closed by moving it to `done` or `dropped`
   with a reason. Threads are append-only. Only the owner deletes cards.
8. **Limits.** A few cards per turn and per hour. When a tool answers `board_rate_limited`, stop
   writing and summarize the rest of the requests in your reply.
9. **Report:** after a card write, name `#ID` and the change in your reply.
10. **One body section per stage**, produced in order: Issue (user's verbatim request), Decisions
    (quoted), Discussion points, Planning notes, Plan, Tasks, Execution Summary, Tests, QA checklist,
    Verdict, Resolution. Write only the section the current stage produces; invent no headings.
    Verifiers write Verdict; Resolution records closure.


Memory: `board_create_card {type: memory}` saves one reusable fact with a stable `name` and
`scope: project`. Pin it or set workspace `paths` globs; update existing facts, retire obsolete
ones. Project names override globals; retired/team memories do not load. User memories go in Globals.
