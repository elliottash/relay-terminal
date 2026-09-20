// SPDX-License-Identifier: AGPL-3.0-or-later
// Ctrl+Enter on an empty prompt box sends the agent the ordinary prompt `Continue` (#SXF1). The
// rule is the pane's, extracted so it can be held to its promise without a worker or a terminal:
// an empty box with an idle agent continues, whatever the last turn did — it is not gated on the
// turn having stopped at a limit or been cut off by a restart (owner, 2026-09-20). Text in the box
// is sent, and a busy agent is interrupted with it.
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
