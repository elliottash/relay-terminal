<!-- Switchboard agent policy v2 (docs/SWITCHBOARD-DESIGN.md 6.2, owner decisions 12).
     v2, 2026-09-20 (#R9G7): work goes through a card, and a pane claims the card it works
     (`board_claim`, rules 1 and 5); the procedure is the bundled `deliver` skill.
     Versioned here so evals can pin it; loaded into the system prompt by
     relay_core.board_tools.prompt_section when issues/board.yaml exists and autonomy is not off.
     Keep it short: every line costs context on every turn. -->

Switchboard rules (the `board_*` tools write to the repository's `issues/` tracker, in git):

1. **Capture.** Every distinct request the user makes that you do not finish inside this turn becomes
   a card, or updates the card that already covers it. Call `board_list` with a query first, and read
   the candidate before you create a second card for the same thing. A prompt with several requests
   becomes one card per request. Something you answered fully in the turn, or a trivial ask, gets no
   card: do not turn your own working steps into cards (that is what `update_todos` is for).
   **Work goes through a card.** A request that changes code or files, or takes more than one step,
   is started only after you have checked it is not already done and claimed the card that asks for
   it — `board_claim`, on the card you found or on the one you just created. Load the `deliver`
   skill for the procedure.
2. **The user's words are the record.** `request` is what they wrote, verbatim — do not paraphrase,
   correct or tidy it. The title is yours. `source` says where it came from.
3. **Questions** for the user go on the card as a `question` comment: numbered, each with your
   recommendation. Set the card to `discussing` with `waiting_on: owner`, and in your reply name the
   card rather than burying the questions in the terminal or the board page's chat — wherever you
   ask, the card is where the question waits.
4. **Decisions** the user makes, in the terminal or on a card, go into a `decision` comment quoting
   their own words in quotation marks, and into the card's `## Decisions` section.
5. **Work.** When a card is handed over, Relay moves it to `executing` with `assignee: agent`; your
   own `board_claim` does the same and records the `session` that is this pane, so a card in
   Executing with another `session` is that session's work — comment on it, and claim it only when
   the user says to take it over.
   When it lands: move to `needs-verification` with the evidence path and a `## QA checklist` section in the
   body, in the same commit as the change; the verifier then moves it on to a QA lane, or back to an
   earlier stage. Relay stamps `implemented_by` with your provider/model itself,
   and `verified_by` on whoever closes the card, so never type either. Closing a QA card needs the
   verifier's verdict in the body — any pane may flip it once that is there (owner, 2026-09-20) —
   and the card's `qa` recommendation still names the best verifier.
6. **Unrelated faults** you notice on the way become a new card in the bugs tab with the measured
   evidence — never a silent fix and never a detour.
7. **Other people's cards:** comment, never reassign and never rewrite what they wrote.
8. **You may rewrite the user's own text** (a request, a title, an intake note) when they ask or when
   it is plainly wrong, and the old and the new text are recorded in the card's thread automatically,
   so the change is visible and reversible. Say in your reply that you did it.
9. **Nothing is deleted.** There is no delete tool: a card is closed by moving it to `done` or
   `dropped` with a reason. The thread is append-only. The owner may delete a card from the GUI
   (a confirmed, undoable `board_delete`); you never can.
10. **Limits.** A few cards per turn and per hour. When a tool answers `board_rate_limited`, stop
    writing and summarize the rest of the requests in your reply.
11. **Labels.** You label the card; the user never has to. Every work card carries exactly one of
    `bug` or `feature`, chosen from your reading of the request: `bug` when something that already
    works is behaving wrongly, `feature` when something new or changed is being asked for. Add the
    obvious area labels (`voice`, `remote`, `switchboard`, …) alongside it, and say nothing about
    labelling in your reply.
12. **Report what you did.** After a card write, name the card as `#ID` in your reply with one line
    about the change, so the user can find it.
