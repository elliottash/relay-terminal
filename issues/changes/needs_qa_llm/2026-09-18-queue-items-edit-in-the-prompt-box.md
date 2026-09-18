---
id: 9V1F
type: work
status: needs-qa-llm
labels: [change]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: c
created: '2026-09-18'
acceptance: 'Up on an empty prompt box opens each queued item in the prompt box for editing; the top item highlighted holds the queue; `tests/queuenav_test.cpp` (21 tests) plus `ctest` (27) and `./scripts/test.sh` (871) pass'
source: 'owner in chat, 2026-09-18: "pressing up into the queue and selecting items should put the command in the prompt and make it editable (closer to the claude code functionality). if the top (first-queued) item is highlighted, the queue is paused and wont run."'
links: {plans: [], commits: [], evidence: [], related: [4C94], github: null}
---
# Queued items are edited in the prompt box, and the highlighted top item holds the queue

## Request

Up into the queue should put the selected item's text in the prompt box and let it be edited there,
the way Claude Code recalls a prompt. While the first-queued item is highlighted the queue must not
run.

## Behavior

Selecting a queued item now **puts its text in the prompt box**, where it is edited like anything
else. It stays in the queue while this happens — the highlighted row keeps the stored text until the
edit is saved, so the row and the box differ only while an edit is in flight.

Keys, while an item is selected:

| Key | What it does |
|---|---|
| Up / Down | Move between queued items — but inside a multi-line item they move the cursor, and only reach the queue from its first (Up) or last (Down) line |
| Up past the front | Leaves the queue and walks on into prompt history |
| Down past the back | Leaves the queue, prompt box empty |
| Enter | Saves the edit in place and leaves; the item keeps its position |
| Esc | Drops the edit and leaves |
| Ctrl+Up / Ctrl+Down | Reorder, stopping at both ends |
| Shift+Delete | Remove the item, staying on the one that moves up into the gap |

**The hold.** `queueHeldBySelection()` is true while the top item is highlighted, and `pumpQueue` /
`moreTurnsPending` go through `queueBlocked()` (a real pause, or this hold). It is derived from the
selection rather than stored, so leaving the selection releases the queue with no second piece of
state to keep in step — and it covers the case the owner's rule implies but does not spell out: an
item further down that drifts to the front while it is being edited is held the moment it arrives
there. The strip title reads `QUEUE · PAUSED` with a tooltip saying why.

**Following the item.** Anything that changes the queue under a selection — the head starting, a
removal, a drag-reorder — passes the selected item's id through `keepSelectionOn`, which finds that
item's new index or, if it has gone, hands the prompt box back empty. Without this the selection
drifted onto a different item while the box still showed the old one.

**The rules are extracted.** `src/QueueNav.{h,cpp}` (`relay::queuenav::decide`) turns a key and the
state into one of eleven actions, following the `InputPolicy` pattern: the Pane owns the state, the
module owns the rules, and `tests/queuenav_test.cpp` covers them without a widget (21 tests).

### Decisions worth knowing

- **Enter saves in place** rather than pulling the item out to run now. Running it now would
  duplicate an item that is already queued, and the owner's pause rule only makes sense if the item
  stays queued while highlighted.
- **Shift+Delete, not Delete**, because Delete and Backspace now edit the text in the box. Not
  Ctrl+D: that is deliberately left as end-of-input for a running program (`src/main.cpp`, the
  Ctrl+E note).
- **An emptied box is "no change"**, not a blank queued item; removing is Shift+Delete.
- The slash popup is suppressed while an item is selected, so a queued `/command` cannot open it and
  take the arrow keys.

### Removed

The old Enter behaviour pulled the item out of the queue and resubmitted it at the head, which is
what `m_resubmitAtFront` and `m_editKind` existed for. Nothing sets them now, so both are gone, along
with the two `!m_resubmitAtFront &&` guards in `submitAgent` / `submitTerminal` and the prepend branch
in `enqueue` (everything queued now joins the back).

## QA checklist

1. **Round trip.** Queue three agent prompts while a turn runs. Up → the third is highlighted and its
   text is in the prompt box. Up, Up → the first. Edit it, press Enter → the row shows the new text,
   the toast says "Saved · it runs next", the prompt box is empty and the queue runs on.
2. **The hold.** With the top item highlighted, the strip says `QUEUE · PAUSED` and nothing starts,
   even after the running turn finishes. Esc → it starts. Repeat with Enter instead of Esc.
3. **Drift.** Queue three, highlight the *third*, then let the first two run. The highlight must stay
   on the same item (its text stays in the box), and the moment it reaches the top the queue holds.
4. **Multi-line.** Queue a prompt with three lines (Shift+Enter). Select it, put the cursor on the
   middle line: Up and Down move the cursor. From the first line Up goes to the item above; from the
   last line Down goes below.
5. **Past the ends.** From the top item, Up goes into prompt history with an empty box (not the
   item's text). From the bottom item, Down leaves with an empty box.
6. **Esc.** Edit an item heavily, press Esc: the row is unchanged and the box is empty.
7. **Removing.** Shift+Delete removes the highlighted item and stays on the one below it; plain
   Delete and Backspace edit the text instead. Remove the last item: the strip disappears and the
   box is empty.
8. **Drag.** With an item highlighted and edited, drag another row past it: the highlight stays on
   the same item and the edit is not lost.
9. **Clear.** With an item highlighted, click Clear: the strip goes and the prompt box is empty (it
   must not keep the removed item's text).
10. **A slash item.** Queue `/context`, select it: no slash popup opens and Up/Down still move
    between items.
11. **Mixed queue.** A shell command and an agent prompt queued together: editing the shell one keeps
    it a shell command (amber `$`) and it still runs in the terminal.

## Known gaps

- Clicking a queue row still only removes it through the ×; a click does not select it for editing.
- The queue holds only on the *top* item, per the owner's rule. An item being edited further down is
  protected because the hold applies once it reaches the front, not before.
