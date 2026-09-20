---
id: PBX1
type: work
status: needs-verification
labels: [feature, switchboard, ui]
assignee: claude-code
rank: m
created: '2026-09-20'
source: 'Owner, 2026-09-20, comparing the main pane''s prompt box with the helper panels'
links: {plans: [], commits: [502f2b22, ce29cb8e, 17cfe0b4, 69fee7b2], evidence: ['docs/qa_evidence/2026-09-20-prompt-boxes-like-the-pane/', 'docs/qa_evidence/2026-09-20-action-rows-left/'], related: [8YQ9, FEJQ, BRD3, VZ69], github: null}
---
# Every agent prompt box looks and behaves like the main pane's

## Issue

i dont like the helper agent prompt UI … there is the useless help sentence, and then a bunch of
wasted space, and then the tiny text box. and the buttons dont look as good.

the card agent looks a lot better

[remove] the send button on all, make it like the pane agent.

move those buttons out of there (plan / execute / etc), because they actually dont do anything in
the chat box. can we instead put buttons like that in a row above the chat box. they are actions
the agent can take that dont require typing. we put the "clean up" button there for the main
switchboard agent, for example.

## Decisions

- **The main pane's prompt box is the reference** (owner, 2026-09-20). One rounded frame, a
  borderless editor in the prompt font, a strip of chips under it, and no Send — Enter sends.
  Every other prompt box in the app is made to be that control: the helper panel in all four
  panes, and the card page's reply box.
- **Actions that need no typing go in a row above the box.** The box holds the text and the chips
  that qualify it, and nothing else. Clean up and Check were already there on the Switchboard;
  Plan, Execute and Verify move out of the card's reply frame to join the same idiom.
- **An action row is left-aligned buttons and nothing else** (owner, 2026-09-20, of the card
  page's row and the Switchboard panel's): "the plan / execute buttons etc, would those work
  better at the left? (in the switchboard card agent)" — "yes, lets do both left-aligned, drop
  the label". The buttons start at the row's left edge, in the order the work is done (Plan,
  Execute, Verify), and the Switchboard's "Switchboard agent" label is gone from its head row:
  the box's placeholder names the agent and the busy strip names it again while a turn runs.
  A panel that folds has no actions on its head row, so it keeps its name and its fold control.
- **No help sentence and no reserved space.** The paragraph over an empty conversation is gone;
  the placeholder in the box says what the box is for and names the agent, and the log is hidden
  entirely until there is something in it.

## Execution Summary

- `src/HelperChat.{h,cpp}` — the panel is now a head row (name, Check / Clean up, the fold), the
  findings and survey, the log, the queue, and then `QFrame#boardChatBox`: the busy strip, the
  borderless editor (`setAutoHeight(2, 8)`), and the chip strip with context, model and
  microphone. `boardChatSend` is gone; Esc in the box stops a running turn, as it does in a pane.
- `src/BoardPane.cpp` — `boardCardActions`, a row above the reply frame holding
  Plan (p), Execute (x) and Verify (v) with their keys, tooltips and behaviour unchanged;
  `fitButtons()` measures that row and no longer counts the model box, which now sits alone on
  the frame's chip strip.
- The rows went **left** on 2026-09-20 (`69fee7b2`): `boardCardActions` keeps its stretch behind
  Plan / Execute / Verify, the helper panel's head row starts with the tool row (Check, Clean up,
  Tests, Profile) and closes with the stretch, and `boardChatHead` is gone from the Switchboard's
  panel. The turn clock and the `· survey` word moved onto the busy strip with the name
  (`drawBusyLine`, `HelperChatPanel::drawBusy`), which is on screen for exactly as long as there
  is a turn to time.
- `src/Theme.cpp` — `QFrame#boardChatBox` and `QFrame#boardReply` are the same frame as
  `QFrame#composer`, their editors are borderless and in the prompt font, and the chip rules are
  the pane's `stripChip` / `stripChipLabel` rules. Check and Clean up take the card page's
  Plan/Execute face.

## QA checklist

1. Open the Switchboard beside a terminal pane: the panel's box and the pane's box read as one
   control — same frame, same radius, same chip height, no border on either editor.
2. No Send button anywhere. Enter sends; the busy strip's `✕ Stop` and Esc in the box stop a turn.
3. An empty conversation has no log and no help sentence; after a turn the log is no taller than
   what it holds.
4. Check and Clean up are on the head row above the box, **at its left**, with nothing in front
   of them: the "Switchboard agent" label is gone from that row.
5. On a card, Plan / Execute / Verify are a row above the reply frame, not in it, **left-aligned
   and in that order**; `p`, `x` and `v` still work and still take whatever is typed in the box as
   their note.
6. While a turn runs, the busy strip inside the box reads `✦ <helper> · m:ss` — the name, the
   turn clock, and `· survey` on a new board's opening turn. Nothing that was on the head row's
   label was lost with it.
7. The model box is alone on the reply box's strip, right-aligned.
8. At a ~350 px pane nothing is clipped, the labels keep their keys, and the row is still
   left-aligned.
9. Options, Actions and Sessions: Alt+Q expands the helper and its box is the same box, with a
   placeholder naming that helper; their head rows keep the pane name and the fold control,
   because they carry no actions.

Evidence: `docs/qa_evidence/2026-09-20-prompt-boxes-like-the-pane/` (drive.sh, NOTES.md, six
screenshots, 22 checks passed), and `docs/qa_evidence/2026-09-20-action-rows-left/` for the
left-aligned rows (drive.sh, NOTES.md, five screenshots, 22 checks passed — every one of them a
measured x position, because "left-aligned" is a claim about geometry).
