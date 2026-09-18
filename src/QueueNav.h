// SPDX-License-Identifier: GPL-3.0-or-later
// relay::queuenav: what a key does while the prompt box is arrowing through the queue.
//
// Pressing Up on an empty prompt box steps into the queue; from there the selected item's text sits
// in the prompt box and is edited there, so the same Up and Down keys have to serve two masters —
// moving between queued items, and moving the cursor inside a multi-line item. The rule is the one
// every shell history uses: the queue moves only when the cursor is already on the first line (Up)
// or the last line (Down); anywhere else the keys belong to the text.
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
    Enter,          // step into the queue from the prompt box, selecting the item nearest it
    Up,             // select the item above, keeping any edit to the one being left
    Down,           // select the item below, keeping any edit
    LeaveToHistory, // past the top: leave the queue empty-handed and let prompt history take the key
    LeaveToPrompt,  // past the bottom: leave the queue, back to an empty prompt box
    Save,           // keep the edit and leave the queue
    Cancel,         // drop the edit and leave the queue
    Remove,         // drop the selected item from the queue
    MoveUp,         // reorder the selected item one place towards the front
    MoveDown,       // reorder it one place towards the back
};

struct State {
    int selected = -1;        // the selected queue index, -1 when the prompt box owns the keys
    int count = 0;            // queued items
    bool promptEmpty = true;  // the prompt box holds no text
    int cursorLine = 0;       // the cursor's line in the prompt box, 0-based
    int lineCount = 1;        // lines in the prompt box
};

Action decide(const State &state, int key, Qt::KeyboardModifiers mods);

}   // namespace relay::queuenav
