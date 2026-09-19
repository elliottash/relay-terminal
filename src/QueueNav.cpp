// SPDX-License-Identifier: AGPL-3.0-or-later
#include "QueueNav.h"

namespace relay::queuenav {

Action decide(const State &state, int key, Qt::KeyboardModifiers mods) {
    const bool plain = mods == Qt::NoModifier;
    const bool enter = key == Qt::Key_Return || key == Qt::Key_Enter;

    // Outside the queue, one key leads in: Up on an empty prompt box with something queued. The
    // prompt box must be empty because a draft in it is the user's, not the queue's.
    if (state.selected < 0 || state.selected >= state.count)
        return (plain && key == Qt::Key_Up && state.promptEmpty && state.count > 0) ? Action::Enter : Action::None;

    if (plain && key == Qt::Key_Up) {
        if (state.cursorLine > 0) return Action::None;          // inside the text: move the cursor
        return state.selected > 0 ? Action::Up : Action::LeaveToHistory;
    }
    if (plain && key == Qt::Key_Down) {
        if (state.cursorLine < state.lineCount - 1) return Action::None;
        return state.selected + 1 < state.count ? Action::Down : Action::LeaveToPrompt;
    }
    if (mods == Qt::ControlModifier && (key == Qt::Key_Up || key == Qt::Key_Down)) {
        // A steer's place is "the next tool call", not a position among the others: Ctrl+Down
        // takes it out of the turn to the head of the queue, and Ctrl+Up has nowhere to go.
        if (state.selected < state.steers) return key == Qt::Key_Down ? Action::Unsteer : Action::None;
        // The head of the queue goes one further up only by becoming a steer. Reordering never
        // falls off either end: otherwise Ctrl+Up at the front does nothing rather than quietly
        // dropping the selection.
        if (key == Qt::Key_Up) {
            if (state.selected > state.steers) return Action::MoveUp;
            return state.headSteerable ? Action::Steer : Action::None;
        }
        return state.selected + 1 < state.count ? Action::MoveDown : Action::None;
    }
    if (plain && enter) return Action::Save;
    if (key == Qt::Key_Escape) return Action::Cancel;
    // Delete and Backspace are the text's now that the item is being edited in the prompt box, so
    // removing an item takes a modifier. Not Ctrl+D: that is end-of-input for a running program.
    if (mods == Qt::ShiftModifier && key == Qt::Key_Delete) return Action::Remove;
    return Action::None;
}

}   // namespace relay::queuenav
