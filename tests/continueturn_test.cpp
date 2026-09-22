// SPDX-License-Identifier: AGPL-3.0-or-later
// What an empty prompt box's two send keys do. Ctrl+Enter sends the agent the ordinary prompt
// `Continue` (#SXF1); plain Enter resumes a queue a Stop paused (#7JD1). Both rules are the
// pane's, extracted so they can be held to their promise without a worker or a terminal: an empty
// box with an idle agent continues, whatever the last turn did — it is not gated on the turn
// having stopped at a limit or been cut off by a restart (owner, 2026-09-20) — and an empty box
// over a paused queue resumes it. Text in the box is sent, and a busy agent is interrupted with
// it; the queue behind a submitted prompt resumes on the worker, not from here.
#include "ContinueTurn.h"

#include <QTest>

using namespace relay::continueturn;

namespace {
State emptyIdle() {
    State state;
    state.textEmpty = true;
    return state;
}
}   // namespace

class ContinueTurnTest : public QObject {
    Q_OBJECT
private slots:
    void an_empty_box_with_an_idle_agent_continues() {
        // The plain rule: nothing to type, nothing running — the send-now is Continue. The case the
        // owner hit is this one: an ordinary turn had ended, the box was empty, and Ctrl+Enter
        // answered "Type a prompt first." The pane's limit/cut-off marks are not part of State at
        // all, so an empty idle box continues whether or not either is set.
        QCOMPARE(sendNowContinues(emptyIdle()), true);
    }

    void text_in_the_box_never_continues() {
        // What Ctrl+Enter already meant (#N8VK's promise among them) is untouched: text is sent.
        State typed = emptyIdle();
        typed.textEmpty = false;
        QCOMPARE(sendNowContinues(typed), false);
    }

    void an_empty_box_resumes_a_paused_queue() {
        // #7JD1 (owner, 2026-09-21: "why don't we just copy the functionality and have enter
        // resume"). A Stop pauses the queue; Enter on the empty box is the way back, on a pane,
        // on a card's console and on a phone alike. With nothing paused it stays what it was —
        // the pane falls through to the steer/interrupt escalation and then to the router.
        State paused = emptyIdle();
        paused.queuePaused = true;
        QCOMPARE(enterResumes(paused), true);
        QCOMPARE(enterResumes(emptyIdle()), false);
    }

    void text_in_the_box_is_submitted_rather_than_resuming() {
        // Enter with words in it sends that prompt; the queue behind it resumes on the worker
        // (`TurnSupervisor.submit`), with nothing extra on the wire — which is why this returns
        // false rather than the pane sending a resume of its own.
        State typed = emptyIdle();
        typed.queuePaused = true;
        typed.textEmpty = false;
        QCOMPARE(enterResumes(typed), false);
    }

    void a_paused_queue_does_not_change_what_ctrl_enter_means() {
        // #SXF1's rule is kept exactly: an empty Ctrl+Enter still continues, paused or not, and
        // the queue resumes behind that `Continue` like behind any other prompt.
        State paused = emptyIdle();
        paused.queuePaused = true;
        QCOMPARE(sendNowContinues(paused), true);
        State busy = paused;
        busy.agentBusy = true;
        QCOMPARE(sendNowContinues(busy), false);
        // And an empty Enter while a turn runs is still a resume when there is a paused queue to
        // resume: the pane tries the steer and the interrupt first, and this is what is left.
        QCOMPARE(enterResumes(busy), true);
    }

    void a_busy_agent_never_continues() {
        // While the agent works, the empty box has nothing to interrupt with and nothing to send
        // past the running turn: Ctrl+Enter keeps its busy answer.
        State busy = emptyIdle();
        busy.agentBusy = true;
        QCOMPARE(sendNowContinues(busy), false);
        State busyTyped = emptyIdle();
        busyTyped.agentBusy = true;
        busyTyped.textEmpty = false;
        QCOMPARE(sendNowContinues(busyTyped), false);
    }
};

QTEST_GUILESS_MAIN(ContinueTurnTest)
#include "continueturn_test.moc"
