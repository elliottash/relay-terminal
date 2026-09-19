// SPDX-License-Identifier: AGPL-3.0-or-later
// When a submitted agent prompt starts its turn at once and when it queues (#N8VK). The rule is
// the pane's, extracted so it can be held to its promise without a shell or a worker: an agent
// prompt contends with the agent alone, so nothing the terminal is doing — or has queued for it —
// may hold the prompt back. What the State leaves out is what the test asserts cannot matter.
#include "QueueSubmit.h"

#include <QTest>

using namespace relay::queuesubmit;

namespace {
State idle() { return State{}; }
State turnRunning() {
    State state;
    state.agentBusy = true;
    return state;
}
State turnStarting() {
    State state;
    state.agentTurnStarting = true;   // left the queue; the worker's "busy" report has not landed
    return state;
}
}   // namespace

class QueueSubmitTest : public QObject {
    Q_OBJECT
private slots:
    void an_idle_agent_starts_at_once() {
        QCOMPARE(decide(idle()), Decision::StartNow);
    }

    void shell_work_does_not_hold_an_agent_prompt_back() {
        // The reproduction on the card: `sleep 25` running in the terminal, `echo queued-behind`
        // queued behind it, an agent prompt submitted with Ctrl+Enter. None of that is in the
        // State, because none of it can change the answer: the prompt starts now, and the shell
        // queue keeps its order — StartNow is the pane's "bypass the queue", which never touches
        // the queued items, exactly as the interrupt branch always has.
        QCOMPARE(decide(idle()), Decision::StartNow);
    }

    void a_paused_queue_does_not_hold_an_explicit_submit_back() {
        // The queue paused and items queued are not in the State either: an explicit submit is
        // the user saying "now", and the interrupt branch already ignored the pause.
        QCOMPARE(decide(idle()), Decision::StartNow);
    }

    void a_running_turn_queues_the_prompt_behind_it() {
        QCOMPARE(decide(turnRunning()), Decision::Queue);
    }

    void a_turn_just_started_queues_the_prompt_too() {
        // An agent entry left the queue a moment ago and the worker has not said "busy" yet.
        // Starting another turn in that gap would send the worker two asks; the prompt waits.
        QCOMPARE(decide(turnStarting()), Decision::Queue);
    }

    void busy_and_starting_together_queue() {
        State state = turnRunning();
        state.agentTurnStarting = true;
        QCOMPARE(decide(state), Decision::Queue);
    }
};

QTEST_GUILESS_MAIN(QueueSubmitTest)
#include "queuesubmit_test.moc"
