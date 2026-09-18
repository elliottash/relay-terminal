// SPDX-License-Identifier: GPL-3.0-or-later
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
        // Reordering never falls off either end: at the front Ctrl+Up does nothing rather than
        // quietly dropping the selection.
        if (key == Qt::Key_Up) return state.selected > 0 ? Action::MoveUp : Action::None;
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
