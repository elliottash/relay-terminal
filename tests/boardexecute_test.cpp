// SPDX-License-Identifier: AGPL-3.0-or-later
// The card page's Execute button while a pane is already executing the card (#48S3). The button
// used to stay "Execute (x)" no matter who held the card, so a second press handed it to a second
// pane. Now, while the card carries a session token whose pane is still open and a status that
// still says the pane is building it, the button names the pane — the same eight characters of
// the session token every other surface shows — and a click reveals that pane, the way the claim
// chip (#R9G7) and the thread's hand-off entries (#HKAP) already do.
//
// Its own executable rather than more of boardpane_test.cpp, which several sessions hold at once
// (the same reasoning cardtests_test.cpp records).
#include "BoardPane.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

namespace {

QJsonObject config()
{
    const auto json = QByteArrayLiteral(R"({
      "columns": ["inbox", "discussing", "planning", "executing", "needs-verification", "done"],
      "column_statuses": {"inbox": ["inbox"], "discussing": ["discussing"],
        "planning": ["planning"], "executing": ["executing"],
        "needs-verification": ["needs-verification"], "done": ["done", "dropped"]},
      "all_statuses": ["inbox", "discussing", "planning", "executing",
        "needs-verification", "done", "dropped"],
      "tabs": [{"id": "features", "folder": "features"}],
      "autonomy": "auto"
    })");
    return QJsonDocument::fromJson(json).object();
}

QJsonObject row(const QString &id, const QString &status)
{
    return QJsonObject{{"id", id}, {"title", id + QStringLiteral(" card")}, {"type", "work"},
                       {"status", status}, {"tab", "features"}, {"rank", "i"},
                       {"path", QStringLiteral("issues/features/") + id + ".md"}};
}

QJsonObject opened(const QList<QJsonObject> &cards)
{
    QJsonArray items;
    for (const QJsonObject &item : cards)
        items << item;
    return QJsonObject{{"event", "board"}, {"config", config()}, {"cards", items},
                       {"problems", QJsonArray{}}};
}

// A `board_card` answer (protocol 19.2), with the front matter and status a caller stamps in.
QJsonObject cardArrived(const QString &id, const QString &status, const QString &session)
{
    return QJsonObject{{"event", "board_card"}, {"card_id", id},
                       {"title", id + QStringLiteral(" card")}, {"status", status},
                       {"tab", "features"}, {"hash", "h1"},
                       {"path", QStringLiteral("issues/features/") + id + ".md"},
                       {"body", QStringLiteral("# %1 card\n\n## Issue\nthe ask\n").arg(id)},
                       {"issue", QStringLiteral("the ask")}, {"issue_heading", "Issue"},
                       {"thread", QJsonArray{}}, {"thread_total", 0},
                       {"front", QJsonObject{{QStringLiteral("session"), session}}}};
}

}  // namespace

class BoardExecuteTests : public QObject {
    Q_OBJECT

private slots:
    void theButtonNamesTheExecutingPaneAndRevealsIt();
    void thePlanButtonNamesThePlanningPaneAndRevealsIt();
    void theVerifyButtonNamesTheVerifyingPaneAndRevealsIt();
};

// The open card's action with this key, out of the same list the card page's agent console
// draws its buttons from (#DEH6). Since #BGRN the row is not `QPushButton`s the test can find:
// `CardContext::actions()` answers `relay::agent::Action`s — `BoardView::cardActions()` →
// `CardDetail::cardActions()` — and the console renders each one, with `key` as the button's
// object name, `fullLabel()` ("Run (r)") as its text and `run()` as its click. Testing the list
// tests the buttons, minus the paint.
static relay::agent::Action cardAction(const relay::BoardView &view, const QString &key)
{
    for (const relay::agent::Action &action : view.cardActions())
        if (action.key == key)
            return action;
    return {};
}

// Whether the row carries the key at all — an action the row must not draw (Verify outside a QA
// lane, "Run in pane" while a pane already executes the card) is an action the list must not carry.
static bool hasCardAction(const relay::BoardView &view, const QString &key)
{
    return cardAction(view, key).key == key;
}

void BoardExecuteTests::theButtonNamesTheExecutingPaneAndRevealsIt()
{
    const QString token = QStringLiteral("abcdef1234567890");
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    QString revealed;
    view.onFocusPane = [&revealed](const QString &pane) { revealed = pane; };

    const auto openCard = [&](const QString &id, const QString &status, bool paneOpen) {
        view.handleEvent(opened({row(id, QStringLiteral("inbox"))}));
        view.selectCard(id);
        view.openSelected();
        QJsonObject answer = cardArrived(id, status, token);
        answer.insert(QStringLiteral("id"), sent.last().value(QStringLiteral("id")));
        view.paneExists = [paneOpen](const QString &) { return paneOpen; };
        view.handleEvent(answer);
        return cardAction(view, QStringLiteral("boardExecute"));
    };

    // Executing, pane open: the Run action names the pane (#BGRN renamed Execute to Run) and
    // running it reveals the pane — no second `board_execute` goes out and the focus lands on
    // the pane that already has the card.
    relay::agent::Action action = openCard(QStringLiteral("EX31"), QStringLiteral("executing"), true);
    QCOMPARE(action.key, QStringLiteral("boardExecute"));
    QCOMPARE(action.label, QStringLiteral("Running (abcdef12)"));
    action.run();
    QCOMPARE(revealed, token);
    // While a pane executes the card there is no second way in: no "Run in pane" (#BGRN).
    QVERIFY(!hasCardAction(view, QStringLiteral("boardRunInPane")));

    // A closed pane's claim is only a record (#R9G7): the action is Run again.
    revealed.clear();
    action = openCard(QStringLiteral("EX32"), QStringLiteral("executing"), false);
    QCOMPARE(action.key, QStringLiteral("boardExecute"));
    QCOMPARE(action.fullLabel(), QStringLiteral("Run (r)"));
    action.run();
    QVERIFY(revealed.isEmpty());

    // So is a live claim on a card that has already landed out of Executing.
    action = openCard(QStringLiteral("EX33"), QStringLiteral("needs-verification"), true);
    QCOMPARE(action.key, QStringLiteral("boardExecute"));
    // A landed card is not being run by any pane, so the row carries the second way in.
    QVERIFY(hasCardAction(view, QStringLiteral("boardRunInPane")));
    QCOMPARE(cardAction(view, QStringLiteral("boardRunInPane")).label,
             QStringLiteral("Run in pane"));
    QCOMPARE(action.fullLabel(), QStringLiteral("Run (r)"));
}

// The Plan button in the same states (#48S3): while a pane is planning the card it names the pane
// and reveals it; a closed pane's claim is a record again.
void BoardExecuteTests::thePlanButtonNamesThePlanningPaneAndRevealsIt()
{
    const QString token = QStringLiteral("abcdef1234567890");
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    QString revealed;
    view.onFocusPane = [&revealed](const QString &pane) { revealed = pane; };

    const auto openCard = [&](const QString &id, const QString &status, bool paneOpen) {
        view.handleEvent(opened({row(id, QStringLiteral("inbox"))}));
        view.selectCard(id);
        view.openSelected();
        QJsonObject answer = cardArrived(id, status, token);
        answer.insert(QStringLiteral("id"), sent.last().value(QStringLiteral("id")));
        view.paneExists = [paneOpen](const QString &) { return paneOpen; };
        view.handleEvent(answer);
        return cardAction(view, QStringLiteral("boardReplyButton"));
    };

    relay::agent::Action action = openCard(QStringLiteral("PL41"), QStringLiteral("planning"), true);
    QCOMPARE(action.key, QStringLiteral("boardReplyButton"));
    QCOMPARE(action.label, QStringLiteral("Planning (abcdef12)"));
    action.run();
    QCOMPARE(revealed, token);

    revealed.clear();
    action = openCard(QStringLiteral("PL42"), QStringLiteral("planning"), false);
    QCOMPARE(action.key, QStringLiteral("boardReplyButton"));
    QCOMPARE(action.fullLabel(), QStringLiteral("Plan (p)"));
    action.run();
    QVERIFY(revealed.isEmpty());
}

// The Verify button in the same states (#48S3): a verifier pane claims the card in one of the QA
// lanes, and while its pane is open the button names it and reveals it — with or without a
// recommended verifier, which only matters for opening a new one.
void BoardExecuteTests::theVerifyButtonNamesTheVerifyingPaneAndRevealsIt()
{
    const QString token = QStringLiteral("abcdef1234567890");
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    QString revealed;
    view.onFocusPane = [&revealed](const QString &pane) { revealed = pane; };

    const auto openCard = [&](const QString &id, const QString &status, bool paneOpen) {
        view.handleEvent(opened({row(id, QStringLiteral("inbox"))}));
        view.selectCard(id);
        view.openSelected();
        QJsonObject answer = cardArrived(id, status, token);
        answer.insert(QStringLiteral("id"), sent.last().value(QStringLiteral("id")));
        view.paneExists = [paneOpen](const QString &) { return paneOpen; };
        view.handleEvent(answer);
        return cardAction(view, QStringLiteral("boardVerify"));
    };

    // The action's key is boardVerify since #BGRN, and its presence in the list is the old
    // visibility assertion: CardDetail only carries it in a QA lane ("an action the row must
    // not draw is an action the list must not carry").
    relay::agent::Action action =
        openCard(QStringLiteral("VQ51"), QStringLiteral("needs-qa-llm"), true);
    QCOMPARE(action.key, QStringLiteral("boardVerify"));
    QCOMPARE(action.label, QStringLiteral("Verifying (abcdef12)"));
    action.run();
    QCOMPARE(revealed, token);

    revealed.clear();
    action = openCard(QStringLiteral("VQ52"), QStringLiteral("needs-qa-llm"), false);
    QCOMPARE(action.key, QStringLiteral("boardVerify"));
    QCOMPARE(action.fullLabel(), QStringLiteral("Verify (v)"));
    // The card names no verifier, so the action is disabled — the old click was a no-op because
    // the button was greyed, and `enabled` is what greys it now.
    QVERIFY(!action.enabled);
    QVERIFY(revealed.isEmpty());
}

QTEST_MAIN(BoardExecuteTests)
#include "boardexecute_test.moc"
