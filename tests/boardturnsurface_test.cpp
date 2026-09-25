// SPDX-License-Identifier: AGPL-3.0-or-later
// The busy line follows the surface the console is drawing (card #6YS5).
//
// One console serves every card of a board's card page (card #CTRN): `clearTranscript` repoints
// it at whatever card is open. A turn on card A finishes with an `agent_finished` that the window
// routes to `card:A`'s surface, and the console no longer holds that surface — so nothing would
// ever clear the busy line, and the console sat on "Relaying · thinking…" over a card whose agent
// is idle while the real turn ran on another card.
//
// What must be true instead is small: a surfaced turn's busy line is dropped when the console
// moves off its surface; the host's `turnRunning` restores one for a card that was already
// working when the console arrived; and a tab turn with no surface — the board conversation,
// which prints on every console — keeps its line across a switch.
//
// Same shape as tests/consolemode_test.cpp (its own executable for the same reasons: `Pane`
// lives only in the `relay` target's translation unit, and no `Q_OBJECT`/moc near src/Keymap.h).
#include "Pane.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>

namespace {
int failures = 0;
}
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            ++failures;                                                                            \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                          \
    } while (false)

namespace {

// A no-shell context whose surface the test moves the way the card page does, by pointing it at
// whatever card is open (src/BoardPane.cpp, CardContext::spec()).
class CardContext final : public relay::agent::Context {
public:
    relay::agent::ContextSpec spec() const override
    {
        relay::agent::ContextSpec spec;
        spec.name = QStringLiteral("card");
        spec.surface = surface;
        spec.agentRole = QStringLiteral("helper");
        spec.workspace = workspace;
        spec.scope = QStringLiteral("console");
        spec.persistScope = QStringLiteral("helper");
        spec.persistKey = QStringLiteral("tab-1");
        spec.shell = false;
        spec.routing = QStringLiteral("agent");
        return spec;
    }
    QString placeholder() const override { return QStringLiteral("Reply to this card…"); }

    QString workspace;
    QString surface = QStringLiteral("card:A1");
};

}  // namespace

// The reported bug: a card turn starts while the console holds that card, the user opens another
// card, and the "Relaying · thinking…" line stays — its agent_finished is routed to the departed
// surface and cannot reach this console any more.
void aSurfacedTurnsBusyLineDropsWhenTheConsoleMovesOffItsSurface()
{
    QTemporaryDir scratch;
    CardContext context;
    context.workspace = scratch.path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    console.onWorkerLine = [](const QJsonObject &) {};
    console.deliverWorkerEvent(QJsonObject{{QStringLiteral("event"), QStringLiteral("agent_started")},
                                           {QStringLiteral("id"), QStringLiteral("t1")},
                                           {QStringLiteral("surface"), QStringLiteral("card:A1")}});
    CHECK(console.agentActive());
    // Open card B1 mid-turn: the transcript is banked for A1, and the busy line — which belongs
    // to A1's turn the way the transcript does — must go with it.
    context.surface = QStringLiteral("card:B1");
    console.clearTranscript(QStringLiteral("card:B1"));
    CHECK(!console.agentActive());
    // And A1's agent_finished, arriving for the surface it names while the console holds B1,
    // neither wedges nor re-busies anything (it is routed away in the window; the pane tolerates
    // it the same way).
    console.deliverWorkerEvent(QJsonObject{{QStringLiteral("event"), QStringLiteral("agent_finished")},
                                           {QStringLiteral("id"), QStringLiteral("t1")},
                                           {QStringLiteral("outcome"), QStringLiteral("done")},
                                           {QStringLiteral("surface"), QStringLiteral("card:A1")}});
    CHECK(!console.agentActive());
    }

// The other half: arriving at a card that was already working. The turn's agent_started was
// routed to a surface this console held only if it was on that card when the turn began — the
// host knows from the board's per-card facts and says so.
void arrivingAtARunningCardRestoresTheBusyLine()
{
    QTemporaryDir scratch;
    CardContext context;
    context.workspace = scratch.path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    console.onWorkerLine = [](const QJsonObject &) {};
    console.deliverWorkerEvent(QJsonObject{{QStringLiteral("event"), QStringLiteral("agent_started")},
                                           {QStringLiteral("id"), QStringLiteral("t2")},
                                           {QStringLiteral("surface"), QStringLiteral("card:A1")}});
    CHECK(console.agentActive());
    context.surface = QStringLiteral("card:B1");
    console.clearTranscript(QStringLiteral("card:B1"));
    CHECK(!console.agentActive());
    // B1 was already working when the console arrived: the host's `turnRunning`.
    console.consoleTurnRunning();
    CHECK(console.agentActive());
    // B1's turn finishes while the console holds B1: the routed agent_finished clears it.
    console.deliverWorkerEvent(QJsonObject{{QStringLiteral("event"), QStringLiteral("agent_finished")},
                                           {QStringLiteral("id"), QStringLiteral("t2")},
                                           {QStringLiteral("outcome"), QStringLiteral("done")},
                                           {QStringLiteral("surface"), QStringLiteral("card:B1")}});
    CHECK(!console.agentActive());
    // turnRunning on an idle surface says nothing (a card with no turn).
    console.consoleTurnRunning();
    CHECK(console.agentActive());   // restored again: the host said a turn runs on B1
    console.deliverWorkerEvent(QJsonObject{{QStringLiteral("event"), QStringLiteral("queue_changed")},
                                           {QStringLiteral("running"), QString()},
                                           {QStringLiteral("paused"), false},
                                           {QStringLiteral("items"), QJsonArray{}},
                                           {QStringLiteral("steering"), QJsonArray{}}});
    CHECK(!console.agentActive());
    }

// A turn with no surface is the board's own conversation, which prints on every console of the
// tab; its busy line is every console's and must survive a card switch.
void aTabTurnKeepsItsBusyLineAcrossTheSwitch()
{
    QTemporaryDir scratch;
    CardContext context;
    context.workspace = scratch.path();
    Pane console(context.workspace, context.workspace, false, relay::defaultEngineCore(), &context);
    console.onWorkerLine = [](const QJsonObject &) {};
    console.deliverWorkerEvent(QJsonObject{{QStringLiteral("event"), QStringLiteral("agent_started")},
                                           {QStringLiteral("id"), QStringLiteral("t3")}});
    CHECK(console.agentActive());
    context.surface = QStringLiteral("card:B1");
    console.clearTranscript(QStringLiteral("card:B1"));
    CHECK(console.agentActive());   // the tab's turn still prints here
    console.deliverWorkerEvent(QJsonObject{{QStringLiteral("event"), QStringLiteral("agent_finished")},
                                           {QStringLiteral("id"), QStringLiteral("t3")},
                                           {QStringLiteral("outcome"), QStringLiteral("done")}});
    CHECK(!console.agentActive());
    }

int main(int argc, char **argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminal"));
    QCoreApplication::setApplicationName(QStringLiteral("relay"));
    QApplication app(argc, argv);
    aSurfacedTurnsBusyLineDropsWhenTheConsoleMovesOffItsSurface();
    arrivingAtARunningCardRestoresTheBusyLine();
    aTabTurnKeepsItsBusyLineAcrossTheSwitch();
    if (!failures) std::fprintf(stdout, "boardturnsurface: busy line follows the surface\n");
    return failures ? 1 : 0;
}
