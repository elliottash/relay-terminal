---
id: PBX1
type: work
status: needs-verification
labels: [feature, switchboard, ui]
assignee: claude-code
rank: m
created: '2026-09-20'
source: 'Owner, 2026-09-20, comparing the main pane''s prompt box with the helper panels'
links: {plans: [], commits: [502f2b22, ce29cb8e, 17cfe0b4, 69fee7b2, 3ed92250], evidence: ['docs/qa_evidence/2026-09-20-prompt-boxes-like-the-pane/', 'docs/qa_evidence/2026-09-20-action-rows-left/', 'docs/qa_evidence/2026-09-20-action-row-face/'], related: [8YQ9, FEJQ, BRD3, VZ69], github: null}
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
- **Every button on an action row wears the card page's button face — but not its colours**
  (owner, 2026-09-20): "make the buttons consistent, can you use the styling from the card agent",
  then "(not the colors though)". So the row imposes shape and nothing else: border, radius,
  padding, weight and height. Each button keeps the ground and the ink its own rule gives it, so
  Execute's accent outline still means "this leaves the board" and a plain button stays plain.
  The rule is keyed on a dynamic property, not on object names: a session adding a button gets
  the shape without renaming it, because its own tests find it by that name.
- **Every action on the Switchboard agent's row has a letter** (owner, 2026-09-20): "it should
  have the letter hotkeys for each switchboard action as well", the way the card page's
  "Plan (p)" and "Execute (x)" do. Check is `k` and Clean up is `u`; the letter is a property the
  button's maker sets, so the row answers a button it has never heard of.

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
- **The row's face and its letters** (`3ed92250`, 2026-09-20). `src/Theme.cpp` grew one rule,
  `QPushButton[actionRow="true"], QToolButton[actionRow="true"]`, carrying border, radius,
  padding and weight and **no colour at all**, so it layers under each button's own object-name
  rule (an id selector, which outranks it) and flattens nothing. `HelperChatPanel::addToolWidget`
  stamps that property on every button that joins the head row and `CardDetail` sets it on Plan,
  Execute and Verify — one rule for "a button on an action row" instead of three object names
  that happened to agree. Tests and Profile (#7BM4), which had no rule at all and painted the
  bare Fusion button, joined the plain-ground selector list beside Check and Clean up. Push
  buttons carry three pixels of extra padding: QStyleSheetStyle adds QSize(3, 3) to a styled
  QToolButton and nothing to a QPushButton, so without it the Switchboard's row (tool buttons)
  and the card page's (push buttons) cannot be the same height.
- **The letters** (same commit). A button's maker sets `actionKey` to a free letter; the panel
  appends " (k)" to the label, keeps `fullLabel` for `fitButtons()`, answers the key from the
  list page (`BoardView::handleBoardKey` asks the panel rather than naming buttons) and adds its
  entry to the key line under the list. Check is `k`, Clean up is `u` — free on a page that
  already spends n, e, p, x, v, m, c, y, t, a, o and `/`. `updateCleanupButton()` keeps the
  letter through the Stop state. A mouse click teaches the letter once (`board.action.<name>`,
  the shortcut-hint rule in WARP.md); the key press that just used it says nothing.
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
10. The Switchboard's head row reads as one toolbar: `Check (k) · Clean up (u) · Tests ·
    Profile` are one height, one border, one radius and one type. Tests and Profile in particular
    are no longer flatter or smaller than their neighbours.
11. The colours were **not** made uniform (owner: "not the colors though"). On a card, Execute
    and Verify still wear the agent's violet outline and Plan the plain ground; the card page's
    row looks as it did apart from being three pixels taller, which is what makes it the same
    height as the Switchboard's.
12. Letters: on the list page `k` runs Check and `u` runs Clean up (and stops a running one), the
    line under the list says so, and neither key does anything else on that page. Clicking Check
    with the mouse shows "Next time: k" once; pressing `k` shows nothing. Typing `k` or `u` into
    the filter, the quick-add or the agent's box types the letter.

Evidence: `docs/qa_evidence/2026-09-20-prompt-boxes-like-the-pane/` (drive.sh, NOTES.md, six
screenshots, 22 checks passed), and `docs/qa_evidence/2026-09-20-action-rows-left/` for the
left-aligned rows (drive.sh, NOTES.md, five screenshots, 22 checks passed — every one of them a
measured x position, because "left-aligned" is a claim about geometry).
