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
#include <QPushButton>
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
};

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
        QList<QPushButton *> buttons =
            view.findChildren<QPushButton *>(QStringLiteral("boardExecute"));
        for (QPushButton *button : buttons)
            if (button->text() == QStringLiteral("Execute (x)"))
                return button;
        return buttons.value(0);
    };

    // Executing, pane open: the button names the pane and the click reveals it — no second
    // `board_execute` goes out and the focus lands on the pane that already has the card.
    QPushButton *button = openCard(QStringLiteral("EX31"), QStringLiteral("executing"), true);
    QVERIFY(button);
    QCOMPARE(button->text(), QStringLiteral("Executing (abcdef12)"));
    button->click();
    QCOMPARE(revealed, token);

    // A closed pane's claim is only a record (#R9G7): the button is Execute again.
    revealed.clear();
    button = openCard(QStringLiteral("EX32"), QStringLiteral("executing"), false);
    QVERIFY(button);
    QCOMPARE(button->text(), QStringLiteral("Execute (x)"));
    button->click();
    QVERIFY(revealed.isEmpty());

    // So is a live claim on a card that has already landed out of Executing.
    button = openCard(QStringLiteral("EX33"), QStringLiteral("needs-verification"), true);
    QVERIFY(button);
    QCOMPARE(button->text(), QStringLiteral("Execute (x)"));
}

QTEST_MAIN(BoardExecuteTests)
#include "boardexecute_test.moc"
