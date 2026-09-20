---
id: PBX1
type: work
status: needs-verification
labels: [feature, switchboard, ui]
assignee: claude-code
rank: m
created: '2026-09-20'
source: 'Owner, 2026-09-20, comparing the main pane''s prompt box with the helper panels'
links: {plans: [], commits: [502f2b22, ce29cb8e, 17cfe0b4], evidence: ['docs/qa_evidence/2026-09-20-prompt-boxes-like-the-pane/'], related: [8YQ9, FEJQ, BRD3, VZ69], github: null}
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
- **No help sentence and no reserved space.** The paragraph over an empty conversation is gone;
  the placeholder in the box says what the box is for and names the agent, and the log is hidden
  entirely until there is something in it.

## What landed

- `src/HelperChat.{h,cpp}` — the panel is now a head row (name, Check / Clean up, the fold), the
  findings and survey, the log, the queue, and then `QFrame#boardChatBox`: the busy strip, the
  borderless editor (`setAutoHeight(2, 8)`), and the chip strip with context, model and
  microphone. `boardChatSend` is gone; Esc in the box stops a running turn, as it does in a pane.
- `src/BoardPane.cpp` — `boardCardActions`, a right-aligned row above the reply frame holding
  Plan (p), Execute (x) and Verify (v) with their keys, tooltips and behaviour unchanged;
  `fitButtons()` measures that row and no longer counts the model box, which now sits alone on
  the frame's chip strip.
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
4. Check and Clean up are on the head row above the box.
5. On a card, Plan / Execute / Verify are a row above the reply frame, not in it; `p`, `x` and `v`
   still work and still take whatever is typed in the box as their note.
6. The model box is alone on the reply box's strip, right-aligned.
7. At a ~350 px pane nothing is clipped and the labels keep their keys.
8. Options, Actions and Sessions: Alt+Q expands the helper and its box is the same box, with a
   placeholder naming that helper.

Evidence: `docs/qa_evidence/2026-09-20-prompt-boxes-like-the-pane/` (drive.sh, NOTES.md, six
screenshots, 22 checks passed).
