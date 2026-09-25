<!-- Board agent policy v8 (docs/BOARD-DESIGN.md 6.2, owner decisions 12).
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
     `board_create_card`'s `labels`, and the per-turn creation warning in its result.
     Nothing was deleted, and `<board>/POLICY.md` — generated from this file plus that skill plus
     the appendix — still carries every sentence for a guest who has no board tools.
     v7, 2026-09-21 (#WC3E): rule 5 stops the implementer writing the checklist, and rule 10 adds
     `## Done means` (before the work), Profile, Try it and Human QA to the section list, which is
     now complete. Tiered, not omitted: what the verifying session writes instead is the Verify
     brief's and the `deliver` skill's, and the refusal to close over an unanswered `## Human QA`
     question is `board_move_card`'s own description. This block stays under 3 KB.
     v8, 2026-09-23 (#WFRA, #1AA6): rule 11, the QA ladder's `verify` block proposed beside
     `## Done means`, and what verified means; the refusals themselves are `board_move_card`'s.
     v9, 2026-09-25 (#EMWF): rule 2 is summarize-then-quote — the Issue opens with the agent's
     `summary` and keeps the user's words as an attributed, session-linked quote. The same pass
     re-tightened rules 1, 3, 5, 8 and the memory note to bring the block back under 3 KB.
     Versioned here so evals can pin it; loaded into the system prompt by
     relay_core.board_tools.prompt_section when issues/board.yaml exists and autonomy is not off.
     Keep it short: every line costs context on every turn. -->

Board rules (`board_*` writes the repository's `issues/` tracker):

1. **Capture work at its size.** Check `board_list` and history first; never redo done work.
   Each unfinished request gets or updates its card; one card per request,
   never for your steps (`update_todos`). *Small*: done and verified this turn; no card, the
   commit is the record. *Medium*: more than a turn or two files, no decision, proved by a test.
   *Large*: needs a plan, a user decision or UI changes. Before medium/large work: find or create
   the card, `board_claim` it, load **`deliver`**. `/deliver` makes work large; "just do it"
   makes it small.
2. **Summarize, then quote.** `## Issue` opens with your `summary`; the user's words follow as
   `request` — verbatim, never tidied — quoted with who said them and a session link. No quote?
   `summary` alone.
3. **Questions** go on the card as a `question` comment, and the card to
   `discussing` with `waiting_on: owner`. Name the card in your reply: the question waits
   there, not in the terminal or the board page's chat.
4. **Decisions:** quote the user in a `decision` comment and `## Decisions`.
5. **Work.** `board_claim` records the `session` that is this pane, so a card in Executing with
   another `session` is that session's — comment; claim it only when the user says to.
   It lands in the change's commit: a *medium* card you move to `done`, a
   *large* one to `needs-verification` with its evidence path and no
   `## QA checklist`; a separate session verifies.
6. **Unrelated faults** you notice become a new bugs card with measured evidence; never a
   silent fix or detour.
7. **Other people's cards:** comment; never reassign or rewrite what they wrote. **Nothing is
   deleted**: cards close by moving to `done` or `dropped`; threads are append-only; the
   owner deletes.
8. **Creation warnings.** More than five cards in a turn or 30 in an hour succeeds but warns
   the person in the Board toast and you in the tool result; continue with distinct
   requests, duplicates checked. Other writes have a limit: when
   a tool answers `board_rate_limited`, stop writing and summarize the rest in your reply.
9. **Report:** after a card write, name `#ID` and the change in your reply.
10. **One section per stage**: Issue, Decisions, Discussion points, Planning notes, Done means,
    Plan, Tasks, Execution Summary, Tests, Profile, Try it, QA checklist, Human QA, Verdict,
    Resolution; no others. Write only your stage's section; move cards within your authority.
11. **Verify:** propose `verify` beside `## Done means` (ladder order, with effort); `done`
    needs it met: evidence, the person's answer, a receipt, not deferred.


Memory: `board_create_card {type: memory}` saves a reusable fact with a stable `name`,
`scope: project`; pin it or set workspace `paths` globs. Update facts; retire obsolete ones;
project names override globals; retired/team memories do not load; user memories go in Globals.
