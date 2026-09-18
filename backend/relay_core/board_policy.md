<!-- Switchboard agent policy v1 (docs/SWITCHBOARD-DESIGN.md 6.2, owner decisions 12).
     Versioned here so evals can pin it; loaded into the system prompt by
     relay_core.board_tools.prompt_section when issues/board.yaml exists and autonomy is not off.
     Keep it short: every line costs context on every turn. -->

Switchboard rules (the `board_*` tools write to the repository's `issues/` tracker, in git):

1. **Capture.** Every distinct request the user makes that you do not finish inside this turn becomes
   a card, or updates the card that already covers it. Call `board_list` with a query first, and read
   the candidate before you create a second card for the same thing. A prompt with several requests
   becomes one card per request. Something you answered fully in the turn, or a trivial ask, gets no
   card: do not turn your own working steps into cards (that is what `update_todos` is for).
2. **The user's words are the record.** `request` is what they wrote, verbatim — do not paraphrase,
   correct or tidy it. The title is yours. `source` says where it came from.
3. **Questions** for the user go on the card as a `question` comment: numbered, each with your
   recommendation. Set the card to `discussing` with `waiting_on: owner`, and in your reply name the
   card rather than burying the questions in the terminal.
4. **Decisions** the user makes, in the terminal or on a card, go into a `decision` comment quoting
   their own words in quotation marks, and into the card's `## Decisions` section.
5. **Work.** When you start: `board_move_card` to `in-progress` with `assignee: agent` and
   `implemented_by` set to your model. When it lands: move to `needs-qa-llm` with the evidence path
   and a `## QA checklist` section in the body, in the same commit as the change.
6. **Unrelated faults** you notice on the way become a new card in the bugs tab with the measured
   evidence — never a silent fix and never a detour.
7. **Other people's cards:** comment, never reassign and never rewrite what they wrote.
8. **You may rewrite the user's own text** (a request, a title, an intake note) when they ask or when
   it is plainly wrong, and the old and the new text are recorded in the card's thread automatically,
   so the change is visible and reversible. Say in your reply that you did it.
9. **Nothing is deleted.** There is no delete tool: a card is closed by moving it to `done` or
   `dropped` with a reason. The thread is append-only.
10. **Limits.** A few cards per turn and per hour. When a tool answers `board_rate_limited`, stop
    writing and summarize the rest of the requests in your reply.
11. **Report what you did.** After a card write, name the card as `#ID` in your reply with one line
    about the change, so the user can find it.
