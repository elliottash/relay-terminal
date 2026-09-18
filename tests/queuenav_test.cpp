// SPDX-License-Identifier: GPL-3.0-or-later
// Arrowing through the queue while its items are edited in the prompt box: when Up and Down move
// between queued items and when they belong to the text, and which keys save, cancel, reorder
// and remove.
#include "QueueNav.h"

#include <QTest>

using namespace relay::queuenav;

namespace {
// Three queued items, the middle one selected, a one-line item in the prompt box.
State selected(int index, int count = 3) {
    State state;
    state.selected = index;
    state.count = count;
    state.promptEmpty = false;
    return state;
}
State idlePrompt(int count = 3) {
    State state;
    state.count = count;
    return state;
}
Action key(const State &state, int k, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    return decide(state, k, mods);
}
}   // namespace

class QueueNavTest : public QObject {
    Q_OBJECT
private slots:
    // ----- stepping in ---------------------------------------------------------------------
    void up_on_an_empty_prompt_box_steps_into_the_queue() {
        QCOMPARE(key(idlePrompt(), Qt::Key_Up), Action::Enter);
    }
    void a_draft_in_the_prompt_box_keeps_up_for_history() {
        State state = idlePrompt();
        state.promptEmpty = false;
        QCOMPARE(key(state, Qt::Key_Up), Action::None);
    }
    void an_empty_queue_has_nothing_to_step_into() {
        QCOMPARE(key(idlePrompt(0), Qt::Key_Up), Action::None);
    }
    void down_never_steps_into_the_queue() {
        QCOMPARE(key(idlePrompt(), Qt::Key_Down), Action::None);
    }

    // ----- moving between items ------------------------------------------------------------
    void up_and_down_move_the_selection() {
        QCOMPARE(key(selected(1), Qt::Key_Up), Action::Up);
        QCOMPARE(key(selected(1), Qt::Key_Down), Action::Down);
    }
    void up_past_the_front_hands_the_key_to_prompt_history() {
        QCOMPARE(key(selected(0), Qt::Key_Up), Action::LeaveToHistory);
    }
    void down_past_the_back_returns_to_an_empty_prompt_box() {
        QCOMPARE(key(selected(2), Qt::Key_Down), Action::LeaveToPrompt);
    }

    // ----- the multi-line rule -------------------------------------------------------------
    // The whole point of the rule: a three-line queued prompt is edited like text, and only the
    // first and last lines reach past it to the queue.
    void inside_a_multi_line_item_the_arrows_are_the_texts() {
        State state = selected(1);
        state.lineCount = 3;
        state.cursorLine = 1;
        QCOMPARE(key(state, Qt::Key_Up), Action::None);
        QCOMPARE(key(state, Qt::Key_Down), Action::None);
    }
    void the_first_line_reaches_the_queue_with_up_only() {
        State state = selected(1);
        state.lineCount = 3;
        state.cursorLine = 0;
        QCOMPARE(key(state, Qt::Key_Up), Action::Up);
        QCOMPARE(key(state, Qt::Key_Down), Action::None);
    }
    void the_last_line_reaches_the_queue_with_down_only() {
        State state = selected(1);
        state.lineCount = 3;
        state.cursorLine = 2;
        QCOMPARE(key(state, Qt::Key_Down), Action::Down);
        QCOMPARE(key(state, Qt::Key_Up), Action::None);
    }
    void a_one_line_item_is_both_ends_at_once() {
        QCOMPARE(key(selected(1), Qt::Key_Up), Action::Up);
        QCOMPARE(key(selected(1), Qt::Key_Down), Action::Down);
    }

    // ----- leaving -------------------------------------------------------------------------
    void enter_saves_and_escape_cancels() {
        QCOMPARE(key(selected(1), Qt::Key_Return), Action::Save);
        QCOMPARE(key(selected(1), Qt::Key_Enter), Action::Save);
        QCOMPARE(key(selected(1), Qt::Key_Escape), Action::Cancel);
    }
    void escape_cancels_whatever_modifiers_are_held() {
        QCOMPARE(key(selected(1), Qt::Key_Escape, Qt::ShiftModifier), Action::Cancel);
    }

    // ----- reordering and removing ---------------------------------------------------------
    void ctrl_arrows_reorder_within_the_queue() {
        QCOMPARE(key(selected(1), Qt::Key_Up, Qt::ControlModifier), Action::MoveUp);
        QCOMPARE(key(selected(1), Qt::Key_Down, Qt::ControlModifier), Action::MoveDown);
    }
    void reordering_stops_at_both_ends() {
        QCOMPARE(key(selected(0), Qt::Key_Up, Qt::ControlModifier), Action::None);
        QCOMPARE(key(selected(2), Qt::Key_Down, Qt::ControlModifier), Action::None);
    }
    void shift_delete_removes_the_selected_item() {
        QCOMPARE(key(selected(1), Qt::Key_Delete, Qt::ShiftModifier), Action::Remove);
    }
    // Delete and Backspace edit the text now that the item is in the prompt box, so neither may
    // reach the queue: losing a queued item to a stray Backspace is the bug this guards.
    void plain_delete_and_backspace_belong_to_the_text() {
        QCOMPARE(key(selected(1), Qt::Key_Delete), Action::None);
        QCOMPARE(key(selected(1), Qt::Key_Backspace), Action::None);
    }
    void ordinary_typing_is_never_ours() {
        QCOMPARE(key(selected(1), Qt::Key_A), Action::None);
        QCOMPARE(key(selected(1), Qt::Key_Space), Action::None);
    }

    // ----- a stale selection ---------------------------------------------------------------
    // The queue shrinks under the selection when an item runs; an index past the end must read as
    // "not in the queue" rather than steering a key at an item that is gone.
    void a_selection_past_the_end_is_out_of_the_queue() {
        State state = selected(5);
        state.promptEmpty = true;
        QCOMPARE(key(state, Qt::Key_Up), Action::Enter);
        QCOMPARE(key(state, Qt::Key_Escape), Action::None);
    }
};

QTEST_MAIN(QueueNavTest)
#include "queuenav_test.moc"
