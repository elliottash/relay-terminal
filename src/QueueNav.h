// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::queuenav: what a key does while the prompt box is arrowing through the queue.
//
// Up on an empty prompt recalls the head as an unsent draft. Rows that cannot be recalled
// (Relay-authored items or worker previews without full text) retain the selection controls.
// Within that selection, Up/Down move between rows only at the first/last line of the text.
//
// The Pane owns the state (the queue, the selection, the prompt box); these functions turn a key
// into a decision, so the rules can be tested without a widget. See
// issues/changes/needs_qa_llm/2026-09-18-queue-items-edit-in-the-prompt-box.md.
#pragma once

#include <Qt>

namespace relay::queuenav {

// What the pane should do with this key.
enum class Action {
    None,           // not ours: let the rest of the composer have the key
    Enter,          // recall the top row into an unsent draft (first steer, else queue head);
                    // noneditable rows retain selection for move/remove actions
    Up,             // select the item above, keeping any edit to the one being left
    Down,           // select the item below, keeping any edit
    LeaveToHistory, // past the top: leave the queue empty-handed and let prompt history take the key
    LeaveToPrompt,  // past the bottom: leave the queue, back to an empty prompt box
    Save,           // keep the edit and leave the queue
    Cancel,         // drop the edit and leave the queue
    Remove,         // drop the selected item from the queue
    MoveUp,         // reorder the selected item one place towards the front
    MoveDown,       // reorder it one place towards the back
    Steer,          // Ctrl+Up on the head queued agent prompt while the agent works: deliver it at
                    // the next tool call instead of after the turn
    Unsteer,        // Ctrl+Down on a steer: take it back out of the turn, to the head of the queue
};

// The rows, top to bottom in delivery order: steers waiting for the running turn's next tool call
// (rows 0 .. steers-1), then the queued items. The "running" line above them is not a row.
struct State {
    int selected = -1;        // the selected row, -1 when the prompt box owns the keys
    int count = 0;            // rows: steers plus queued items
    int steers = 0;           // how many of the rows are steers
    bool headSteerable = false; // the head queued item is a user's agent prompt and the agent works
    bool promptEmpty = true;  // the prompt box holds no text
    int cursorLine = 0;       // the cursor's line in the prompt box, 0-based
    int lineCount = 1;        // lines in the prompt box
};

Action decide(const State &state, int key, Qt::KeyboardModifiers mods);

}   // namespace relay::queuenav
