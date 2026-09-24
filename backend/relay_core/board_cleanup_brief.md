<!-- Whole-board cleanup brief v2 (docs/AGENT-SESSIONS-PROTOCOL.md 19.9).
     Versioned here, beside board_policy.md, so evals can pin it and the owner can edit what
     the cleanup does without touching code.  It is sent as the *prompt* of the cleanup turn
     (relay_core.board_protocol.cleanup_prompt), not as part of the system prompt, so an
     ordinary card chat never carries it.  Keep it concrete: it is the whole instruction.

     v2 (card #VKFV): step 5 now annotates labels — bug/feature plus the board's own area
     labels — and *suggests* tags outside the vocabulary instead of writing them, because the
     labels became a filter the pane draws. -->

You are tidying the whole board in one pass, because the user pressed **Clean up the board**.

These are the user's own issue files, in git, and other people and agents read them. Improve the
board; do not rewrite it into your own idea of a tracker. When you are unsure whether two cards are
the same thing, or whether a card's status is wrong, **leave it alone and say so in your reply**.
Doing less than asked and explaining why is a good outcome; a confident wrong merge is not.

## Work in this order

1. **Read before you write.** `board_list` for the roster (paginate with `status` and `tab`), then
   `board_read` every card you intend to touch. You may not merge, split, move or re-label a card
   you have not read in this turn.
2. **Redundant cards → one card.** Two or more cards asking for the same thing become one with
   `board_merge_cards`: the survivor is the one with the most work on it (evidence, thread, a
   landed change), and `reason` says in one line why they are the same request. Cards that merely
   touch the same area are *not* duplicates. Do not merge a card that has evidence into one that
   does not without saying so.
3. **Cards that mix unrelated work → one card each.** `board_split_card` with one part per piece;
   each part's `request` is the user's own words for that piece, quoted verbatim from the original
   card — never your paraphrase. Pass `close: true` only when every piece moved out and nothing is
   left on the original.
4. **Status against reality.** For each card ask what is actually true: is the change on main
   (`git log`, the linked commits), does the evidence folder it names exist, is its file in the
   folder its status says? A card whose work has landed with evidence belongs in `needs-qa-llm`; a
   card whose work is done and QA'd belongs in `done` with a verdict; a card nobody is working on
   is not `in-progress`. Move it with `board_move_card` and put the evidence in `reason`. If you
   cannot tell, leave the status and note it in your reply.
5. **Labels and ranks.** Labels are a filter now: the pane draws one chip per label the board
   carries, so a card the labels cannot find is a card the pane cannot show. Every *work* card
   carries exactly one of `bug` or `feature`, and the area labels the board already uses
   (`gui`, `voice`, `remote`, `switchboard`, … — the labels you see on other cards, never
   invented words). Read the card, decide from what it asks for, and fix a plainly wrong or
   missing label with `board_update_card`. A word that would be a good label but is not already
   in the board's vocabulary is **suggested in your report, never written**: one "Suggested
   tags" line, `#ID: word — why`, and the owner decides. Fix a rank only when it is missing or
   when a card is plainly in the wrong place in its section.
6. **Sections last, and rarely.** `board_sections` changes `issues/board.yaml` — the structure
   everyone sees. Merge two sections (drop a column) only when one of them has been empty for a
   while and its work has moved; split one (add a column) only when a section is so large that it
   has stopped meaning anything. Changing nothing here is the usual right answer.

## Rules

- **Nothing is deleted.** There is no delete tool. A merged card keeps its file, its id and its
  text, and is closed as `dropped` pointing at the survivor; a closed card is `done` or `dropped`
  with a reason. Never make a `#ID` someone wrote down stop resolving.
- **The board's `bug_intake.txt` and `feature_intake.txt` are the user's inboxes.** Never read
  them into cards, never edit them, never mention tidying them.
- **The user's words are the record.** A `## Issue` section is quoted, not improved. If you must
  rewrite one, the old text is kept in the thread automatically — say in your reply that you did it.
- **Do not touch the repository outside `issues/`** and **never run git commands that write**
  (no `commit`, `add`, `stash`, `checkout`, `reset`). Reading history with `git log` is expected.
- **Do not open the cards' code.** This is a tracker pass, not an implementation turn: you are not
  fixing bugs, writing files under `src/`, or running the test suite.
- Work steadily and stop when the board is tidy. If a tool answers `board_rate_limited`, stop
  writing and put the rest in your reply.

## Finish with a report

Your last message is what the user reads in the Board pane, and it is copied into the cleanup's
changelog. Make it a short list, no preamble:

- **Merged**: `#A + #B → #A` and the one-line reason, per merge.
- **Split**: `#C → #D, #E`, per split.
- **Status**: `#F in-progress → needs-qa-llm (evidence docs/qa_evidence/…)`, per move.
- **Labels / ranks**: how many cards you annotated (and what you took off), then one
  **Suggested tags** line per proposed new label — `#ID: word — why` — or "none".
- **Sections**: what changed in `board.yaml`, or "unchanged".
- **Left alone**: the cards you were unsure about and why — this part matters most.
