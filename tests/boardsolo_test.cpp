// SPDX-License-Identifier: AGPL-3.0-or-later
// Several cards open at once (#Y2BA). A Board pane pinned to one card (BoardView::pinSolo) shows
// that card and never its list, and every way out of the page closes the *pane*; the list board
// hands a card to a pane of its own on Shift+Enter, on the page's ⤴ button, and when a new card is
// created while another card's page is still open (the owner's "pressing new card again splits").
#include "BoardPane.h"

#include <QApplication>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QToolButton>
#include <QtTest>

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

QJsonObject row(const QString &id)
{
    return QJsonObject{{"id", id}, {"title", id + QStringLiteral(" card")}, {"type", "work"},
                       {"status", "inbox"}, {"tab", "features"}, {"rank", "i"},
                       {"path", QStringLiteral("issues/features/") + id + ".md"}};
}

QJsonObject opened(const QStringList &ids)
{
    QJsonArray items;
    for (const QString &id : ids)
        items << row(id);
    return QJsonObject{{"event", "board"}, {"config", config()}, {"cards", items},
                       {"problems", QJsonArray{}}};
}

QJsonObject cardEvent(const QString &id, const QString &requestId)
{
    QJsonObject reply{{"event", "board_card"}, {"card_id", id},
                      {"title", id + QStringLiteral(" card")}, {"status", "inbox"},
                      {"tab", "features"}, {"hash", "h1"},
                      {"path", QStringLiteral("issues/features/") + id + ".md"},
                      {"body", QStringLiteral("# %1 card\n\n## Issue\nthe ask\n").arg(id)},
                      {"issue", QStringLiteral("the ask")}, {"issue_heading", "Issue"},
                      {"thread", QJsonArray{}}, {"thread_total", 0}};
    if (!requestId.isEmpty())
        reply.insert(QStringLiteral("id"), requestId);
    return reply;
}

QString lastId(const QList<QJsonObject> &sent)
{
    return sent.isEmpty() ? QString() : sent.last().value(QStringLiteral("id")).toString();
}

void pressOn(QWidget *target, int key, Qt::KeyboardModifiers mods = Qt::NoModifier,
             const QString &text = QString())
{
    QKeyEvent press(QEvent::KeyPress, key, mods, text);
    QApplication::sendEvent(target, &press);
}

}  // namespace

class BoardSoloTests : public QObject {
    Q_OBJECT

private slots:
    void aPinnedPaneShowsOneCardAndClosesAsAPane();
    void aLinkOnAPinnedPageMovesThePane();
    void shiftEnterOpensTheRowInItsOwnPane();
    void thePopOutButtonHandsTheCardOverAndGoesBackToTheList();
    void aNewCardWhileOneIsOpenGoesToItsOwnPane();
};

void BoardSoloTests::aPinnedPaneShowsOneCardAndClosesAsAPane()
{
    relay::BoardView view(QStringLiteral("/tmp/relay-card-solo-test"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    int closes = 0, quickAdds = 0;
    view.onClosePane = [&closes] { ++closes; };
    view.onQuickAddElsewhere = [&quickAdds] { ++quickAdds; };
    view.onOpenInNewPane = [](const QString &) { QFAIL("a pinned page does not pop out again"); };
    view.resize(1600, 900);   // wide: an unpinned board would show the list beside the card
    view.show();
    view.handleEvent(opened({QStringLiteral("K7Q2"), QStringLiteral("M3XJ")}));

    view.pinSolo(QStringLiteral("K7Q2"));
    QVERIFY(view.pinned());
    QCOMPARE(view.pinnedCard(), QStringLiteral("K7Q2"));
    // Still loading: the list is not what a pinned pane shows while it waits.
    QVERIFY(!view.listPaneVisible());
    view.handleEvent(cardEvent(QStringLiteral("K7Q2"), lastId(sent)));
    QVERIFY(view.detailOpen());
    QVERIFY(!view.listPaneVisible());
    QCOMPARE(view.title(), QStringLiteral("#K7Q2 K7Q2 card"));
    // The header's button says what it does now.
    auto *back = view.findChild<QToolButton *>(QStringLiteral("boardBack"));
    QVERIFY(back && back->text().contains(QStringLiteral("Close pane")));
    QVERIFY(!view.findChild<QToolButton *>(QStringLiteral("boardCardPopOut"))->isVisibleTo(&view));

    // `/` has no filter to go to, and `n` asks the list board instead.
    pressOn(&view, Qt::Key_Slash, Qt::NoModifier, QStringLiteral("/"));
    QVERIFY(view.detailOpen());
    QCOMPARE(closes, 0);
    pressOn(&view, Qt::Key_N, Qt::NoModifier, QStringLiteral("n"));
    QCOMPARE(quickAdds, 1);

    // Esc closes the pane, not the page: the list never comes back.
    pressOn(&view, Qt::Key_Escape);
    QCOMPARE(closes, 1);
    QVERIFY(view.detailOpen());
    QVERIFY(!view.listPaneVisible());
    // …and so does the header's button.
    back->click();
    QCOMPARE(closes, 2);

    // The card removed under the pane closes it too.
    view.handleEvent(QJsonObject{{"event", "board_changed"},
                                 {"removed", QJsonArray{QStringLiteral("K7Q2")}},
                                 {"changed", QJsonArray{}}});
    QCOMPARE(closes, 3);
}

void BoardSoloTests::aLinkOnAPinnedPageMovesThePane()
{
    relay::BoardView view(QStringLiteral("/tmp/relay-card-solo-test"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.onClosePane = [] {};
    view.resize(1600, 900);
    view.show();
    view.handleEvent(opened({QStringLiteral("K7Q2"), QStringLiteral("M3XJ")}));
    view.pinSolo(QStringLiteral("K7Q2"));
    view.handleEvent(cardEvent(QStringLiteral("K7Q2"), lastId(sent)));
    view.openCard(QStringLiteral("M3XJ"));   // a `#M3XJ` in the card's text
    view.handleEvent(cardEvent(QStringLiteral("M3XJ"), lastId(sent)));
    QCOMPARE(view.pinnedCard(), QStringLiteral("M3XJ"));
    QVERIFY(view.pinned());
    QVERIFY(!view.listPaneVisible());
}

void BoardSoloTests::shiftEnterOpensTheRowInItsOwnPane()
{
    relay::BoardView view(QStringLiteral("/tmp/relay-card-solo-test"));
    view.onSend = [](const QJsonObject &) {};
    QStringList popped;
    view.onOpenInNewPane = [&popped](const QString &id) { popped << id; };
    view.resize(1600, 900);
    view.show();
    view.handleEvent(opened({QStringLiteral("K7Q2"), QStringLiteral("M3XJ")}));
    view.selectCard(QStringLiteral("M3XJ"));
    auto *list = view.findChild<QListWidget *>();
    QVERIFY(list);
    pressOn(list, Qt::Key_Return, Qt::ShiftModifier, QStringLiteral("\r"));
    QCOMPARE(popped, QStringList{QStringLiteral("M3XJ")});
    // This board stays on its list; nothing opened here.
    QVERIFY(!view.detailOpen());
    QVERIFY(view.listPaneVisible());
    QVERIFY(!view.pinned());
}

void BoardSoloTests::thePopOutButtonHandsTheCardOverAndGoesBackToTheList()
{
    relay::BoardView view(QStringLiteral("/tmp/relay-card-solo-test"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    QStringList popped;
    view.onOpenInNewPane = [&popped](const QString &id) { popped << id; };
    view.resize(1600, 900);
    view.show();
    view.handleEvent(opened({QStringLiteral("K7Q2"), QStringLiteral("M3XJ")}));
    view.openCard(QStringLiteral("K7Q2"));
    view.handleEvent(cardEvent(QStringLiteral("K7Q2"), lastId(sent)));
    QVERIFY(view.detailOpen());
    auto *popOut = view.findChild<QToolButton *>(QStringLiteral("boardCardPopOut"));
    QVERIFY(popOut && popOut->isVisibleTo(&view));
    popOut->click();
    QCOMPARE(popped, QStringList{QStringLiteral("K7Q2")});
    QVERIFY(!view.detailOpen());
    QVERIFY(view.listPaneVisible());
}

void BoardSoloTests::aNewCardWhileOneIsOpenGoesToItsOwnPane()
{
    relay::BoardView view(QStringLiteral("/tmp/relay-card-solo-test"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    QStringList popped;
    view.onOpenInNewPane = [&popped](const QString &id) { popped << id; };
    view.resize(1600, 900);
    view.show();
    view.handleEvent(opened({QStringLiteral("K7Q2")}));
    view.openCard(QStringLiteral("K7Q2"));
    view.handleEvent(cardEvent(QStringLiteral("K7Q2"), lastId(sent)));
    QVERIFY(view.detailOpen());
    const int before = sent.size();
    // The worker answers a create this pane asked for.
    const QString mine = QStringLiteral("sb%1-999").arg(quintptr(&view), 0, 36);
    view.handleEvent(QJsonObject{{"event", "board_written"}, {"id", mine}, {"kind", "board_create"},
                                 {"card_id", QStringLiteral("N4WC")}, {"write_id", "w1"}});
    QCOMPARE(popped, QStringList{QStringLiteral("N4WC")});
    // The open card stays where it is, and nothing asked to open the new one here.
    QCOMPARE(view.selectedCard(), QStringLiteral("K7Q2"));
    for (int i = before; i < sent.size(); ++i)
        QVERIFY(sent.at(i).value(QStringLiteral("type")).toString() != QStringLiteral("board_card_get"));

    // With no card open, a new card opens in this pane as it always did.
    relay::BoardView fresh(QStringLiteral("/tmp/relay-card-solo-test"));
    QList<QJsonObject> freshSent;
    fresh.onSend = [&freshSent](const QJsonObject &message) { freshSent << message; };
    fresh.onOpenInNewPane = [&popped](const QString &id) { popped << id; };
    fresh.show();
    fresh.handleEvent(opened({QStringLiteral("K7Q2")}));
    const QString freshMine = QStringLiteral("sb%1-999").arg(quintptr(&fresh), 0, 36);
    fresh.handleEvent(QJsonObject{{"event", "board_written"}, {"id", freshMine}, {"kind", "board_create"},
                                  {"card_id", QStringLiteral("P5RD")}, {"write_id", "w2"}});
    QCOMPARE(popped.size(), 1);
    QVERIFY(!freshSent.isEmpty());
    QCOMPARE(freshSent.last().value(QStringLiteral("type")).toString(), QStringLiteral("board_card_get"));
    QCOMPARE(freshSent.last().value(QStringLiteral("card")).toString(), QStringLiteral("P5RD"));
}

QTEST_MAIN(BoardSoloTests)
#include "boardsolo_test.moc"
