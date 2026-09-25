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
State waitingOnBackground() {
    // The running turn is parked inside a wait — agent_wait, or command_output on a
    // still-running job — with no step boundary of its own: the worker ends that wait for a
    // steered message (Queue.peek_steer), so the submit can steer straight in (#T4VK).
    State state;
    state.agentBusy = true;
    state.agentWaitingBackground = true;
    return state;
}
}   // namespace

class QueueSubmitTest : public QObject {
    Q_OBJECT
private slots:
    void tui_delivery_waits_through_polling_and_stale_idle_until_progress() {
        GuestDelivery delivery;
        QVERIFY(delivery.available(false));
        delivery.sent();
        for (int poll = 0; poll < 25; ++poll) {
            delivery.observe(false); // delayed idle snapshot, not an acknowledgement
            QVERIFY(!delivery.available(false));
        }
        delivery.observe(true);
        QVERIFY(!delivery.available(true));
        delivery.observe(false);
        QVERIFY(delivery.available(false));
        delivery.sent();
        QVERIFY(!delivery.available(false)); // the next item gets its own reservation
    }

    void duplicate_completion_cannot_release_the_next_prompt() {
        GuestDelivery delivery;
        delivery.sent();
        delivery.observe(true);
        delivery.observe(false);
        QVERIFY(delivery.available(false));
        delivery.sent();
        delivery.observe(false); // codex notify after rollout completion of previous turn
        QVERIFY(!delivery.available(false));
    }

    void tui_departure_and_failed_send_do_not_leave_a_reservation() {
        GuestDelivery delivery;
        // A rejected write must not call sent(); the same entry can be retried.
        QVERIFY(delivery.available(false));
        delivery.sent();
        delivery.reset(); // foreground guest changed or exited
        QVERIFY(delivery.available(false));
        delivery.sent();
        QVERIFY(!delivery.available(false));
    }

    void queued_guest_launch_does_not_own_the_guest_prompt_slot() {
        GuestDelivery delivery;
        QVERIFY(queueResourceAvailable(true, true)); // launch still active
        QVERIFY(delivery.available(false));
        delivery.sent();
        QVERIFY(queueResourceAvailable(true, true));
        QVERIFY(!delivery.available(false)); // still serialize guest prompts
        QVERIFY(!queueResourceAvailable(false, true)); // shell/agent FIFO unchanged
        QVERIFY(queueResourceAvailable(false, false));
    }

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

    void a_turn_waiting_for_background_agents_takes_the_prompt_as_a_steer() {
        // The reported bug (#T4VK): a steered prompt had nowhere to land while the turn sat in
        // agent_wait, so the only way through was the double-Enter interrupt. Now the submit
        // itself steers in — one Enter, as in Claude Code — because the worker ends the wait
        // for it and the message joins right after the wait's tool result.
        QCOMPARE(decide(waitingOnBackground()), Decision::Steer);
    }

    void an_agent_prompt_queued_ahead_keeps_its_place_before_a_new_steer() {
        // Only the lane's head steers: with an older prompt waiting, the submit queues behind it
        // (the empty-Enter steer delivers the first of them instead) — no queue jumping.
        State state = waitingOnBackground();
        state.agentQueueEmpty = false;
        QCOMPARE(decide(state), Decision::Queue);
    }

    void a_turn_still_starting_never_steers_even_if_a_wait_was_reported() {
        // agentTurnStarting and a live wait call cannot both be real (a wait arrives mid-turn),
        // but the rule holds either way: a turn whose busy report has not landed cannot take a
        // steer, or two asks could race.
        State state = waitingOnBackground();
        state.agentTurnStarting = true;
        QCOMPARE(decide(state), Decision::Queue);
    }
};

QTEST_GUILESS_MAIN(QueueSubmitTest)
#include "queuesubmit_test.moc"
