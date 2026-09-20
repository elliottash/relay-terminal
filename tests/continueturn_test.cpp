// SPDX-License-Identifier: AGPL-3.0-or-later
// When Ctrl+Enter on an empty prompt box continues the agent's stopped turn (#SXF1). The rule is
// the pane's, extracted so it can be held to its promise without a worker or a terminal: an empty
// send-now may only ever mean "continue" for a turn that stopped at its limit or was cut off by a
// restart — never for a finished conversation, and never in place of the busy-interrupt send.
#include "ContinueTurn.h"

#include <QTest>

using namespace relay::continueturn;

namespace {
State emptyIdle(bool limitReached = false, bool turnCutOff = false) {
    State state;
    state.textEmpty = true;
    state.limitReached = limitReached;
    state.turnCutOff = turnCutOff;
    return state;
}
}   // namespace

class ContinueTurnTest : public QObject {
    Q_OBJECT
private slots:
    void a_limit_stop_and_a_cut_off_restart_continue() {
        // The card's two cases: the turn ran out of steps, or Relay closed mid-turn and the pane
        // was restored from a session whose last turn never ended.
        QCOMPARE(sendNowContinues(emptyIdle(true, false)), true);
        QCOMPARE(sendNowContinues(emptyIdle(false, true)), true);
        QCOMPARE(sendNowContinues(emptyIdle(true, true)), true);
    }

    void a_finished_conversation_does_not() {
        // Nothing stopped early: an empty box has nothing to continue, so it keeps its old answer.
        QCOMPARE(sendNowContinues(emptyIdle()), false);
    }

    void text_in_the_box_or_a_busy_agent_never_continues() {
        // What Ctrl+Enter already meant (#N8VK's promise among them) is untouched: text is sent,
        // and a busy agent is interrupted with it — the continue reading exists only in the
        // empty-idle slot those two leave.
        State typed = emptyIdle(true);
        typed.textEmpty = false;
        QCOMPARE(sendNowContinues(typed), false);
        State busy = emptyIdle(true);
        busy.agentBusy = true;
        QCOMPARE(sendNowContinues(busy), false);
        State busyCutOff = emptyIdle(false, true);
        busyCutOff.agentBusy = true;
        QCOMPARE(sendNowContinues(busyCutOff), false);
    }
};

QTEST_GUILESS_MAIN(ContinueTurnTest)
#include "continueturn_test.moc"
