// SPDX-License-Identifier: AGPL-3.0-or-later
// Focus belongs to the reader, not the card. A card page open in a narrow pane used to call
// focusDocument() on every `board_card` event — and a `board_card` arrives whenever the open
// card's file changes under it (#N5JJ): another session's comment, a status move, any update —
// so the board kept pulling the keyboard focus out of the pane the reader was typing in. The
// page may still take the focus when a *different* card is opened in its own pane; a re-read of
// the card already open, and a thread entry arriving on it, must leave the focus where it was.
#include "BoardPane.h"

#include <QApplication>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QtTest>
#include <QWidget>

namespace {

QJsonObject config()
{
    const auto json = QByteArrayLiteral(R"({
      "columns": ["inbox", "discussing", "ready", "in-progress", "done"],
      "column_statuses": {"inbox": ["inbox"], "discussing": ["discussing"], "ready": ["ready"],
        "in-progress": ["in-progress"], "done": ["done"]},
      "all_statuses": ["inbox", "discussing", "ready", "in-progress", "done"],
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

}  // namespace

class BoardFocusTests : public QObject {
    Q_OBJECT

private slots:
    void anUpdateToTheOpenCardLeavesTheFocusAlone();
};

void BoardFocusTests::anUpdateToTheOpenCardLeavesTheFocusAlone()
{
    // One window, as the Relay window is: the line edit stands for the pane the reader is typing
    // in, the board view a narrow board pane whose card page stacks over its list.
    QWidget window;
    QHBoxLayout layout(&window);
    relay::BoardView view(QStringLiteral("/tmp/relay-card-focus-test"));
    QLineEdit typing(&window);
    layout.addWidget(&typing);
    layout.addWidget(&view);
    view.setMaximumWidth(relay::board::kCardSplitWidth - 40);   // the card page will stack
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    window.resize(1200, 900);
    window.show();
    view.handleEvent(opened({row(QStringLiteral("K7Q2"), QStringLiteral("inbox")),
                              row(QStringLiteral("M3XJ"), QStringLiteral("inbox"))}));
    QCoreApplication::processEvents();
    QVERIFY(view.width() < relay::board::kCardSplitWidth);   // the card page will stack
    const auto answer = [&sent](const QString &id) {
        QJsonObject reply{{"event", "board_card"}, {"card_id", id},
                          {"title", id + QStringLiteral(" card")}, {"status", "inbox"},
                          {"tab", "features"}, {"hash", "h1"},
                          {"path", QStringLiteral("issues/features/") + id + ".md"},
                          {"body", QStringLiteral("# %1 card\n\n## Issue\nthe ask\n").arg(id)},
                          {"issue", QStringLiteral("the ask")}, {"issue_heading", "Issue"},
                          {"thread", QJsonArray{}}, {"thread_total", 0}};
        if (!sent.isEmpty())
            reply.insert(QStringLiteral("id"), sent.last().value(QStringLiteral("id")));
        return reply;
    };

    // Opening a card in this pane still brings the reader to its page — with the focus
    // elsewhere in the window, that is the navigation the page exists for.
    view.openCard(QStringLiteral("K7Q2"));
    view.handleEvent(answer(QStringLiteral("K7Q2")));
    QVERIFY(view.detailOpen());
    QVERIFY(!view.listPaneVisible());
    QTRY_VERIFY(QApplication::focusWidget() != &typing);
    QVERIFY(view.isAncestorOf(QApplication::focusWidget()));

    // The reader goes back to typing.
    window.activateWindow();
    typing.setFocus();
    QTRY_COMPARE(QApplication::focusWidget(), &typing);

    // The card's file changed under the open page (#N5JJ): the pane re-reads it, and the
    // re-read must not touch the focus. Twice — the grab was on *every* update.
    view.handleEvent(answer(QStringLiteral("K7Q2")));
    QCOMPARE(QApplication::focusWidget(), &typing);
    view.handleEvent(answer(QStringLiteral("K7Q2")));
    QCOMPARE(QApplication::focusWidget(), &typing);

    // A thread entry arriving on the open card keeps the reader's focus too.
    view.handleEvent(QJsonObject{{"event", "board_thread_appended"},
                                 {"card_id", QStringLiteral("K7Q2")},
                                 {"entry", QJsonObject{{"kind", "note"},
                                                       {"author", "another pane"},
                                                       {"text", "an update from elsewhere"}}}});
    QCOMPARE(QApplication::focusWidget(), &typing);
}

QTEST_MAIN(BoardFocusTests)
#include "boardfocus_test.moc"
