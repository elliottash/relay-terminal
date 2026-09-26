// SPDX-License-Identifier: AGPL-3.0-or-later
// Several cards open at once (#Y2BA). A Board pane pinned to one card (BoardView::pinSolo) shows
// that card and never its list, and every way out of the page closes the *pane*; the list board
// hands a card to a pane of its own on Shift+Enter, on the page's ⤴ button, and when a new card is
// created while another card's page is still open (the owner's "pressing new card again splits").
#include "BoardPane.h"
#include "AgentSplit.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QSplitterHandle>
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

// A drag of a splitter handle with the button held, as the mouse does it (#ZPHJ).
void dragHandle(QSplitterHandle *handle, int dy)
{
    const QPointF from = QRectF(handle->rect()).center();
    const QPointF to = from + QPointF(0, dy);
    auto send = [handle](QEvent::Type type, const QPointF &at, Qt::MouseButton button,
                         Qt::MouseButtons buttons) {
        QMouseEvent event(type, at, QPointF(handle->mapToGlobal(at.toPoint())), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(handle, &event);
    };
    send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseMove, (from + to) / 2, Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
}

}  // namespace

class BoardSoloTests : public QObject {
    Q_OBJECT

private slots:
    void aPinnedPaneShowsOneCardAndClosesAsAPane();
    void aLinkOnAPinnedPageMovesThePane();
    void shiftEnterOpensTheRowInItsOwnPane();
    void shiftEnterOnTheOpenCardTakesItOffThisBoard();
    void thePopOutButtonHandsTheCardOverAndGoesBackToTheList();
    void aNewCardWhileOneIsOpenGoesToItsOwnPane();
    void aPinnedPaneLeavesTheTreeWatchToTheList();
    void theCardPanesAgentDividerDragsAndRestores();
};

// #ZPHJ, the owner's "thats not working in the card pane": a Card pane's console sits under a
// divider that drags both ways, is not capped at 40 % any more, and a second Card pane given the
// saved share opens at it while the first keeps its own.
void BoardSoloTests::theCardPanesAgentDividerDragsAndRestores()
{
    auto openPinned = [](relay::BoardView &view, QList<QJsonObject> &sent, QWidget &consoles) {
        view.onSend = [&sent](const QJsonObject &message) { sent << message; };
        view.onClosePane = [] {};
        view.onCreateConsole = [&consoles](relay::agent::Context *, QWidget *) {
            relay::agent::ConsoleHandle handle;
            auto *transcript = new QPlainTextEdit(&consoles);   // the thread and its chain of thought
            transcript->setPlainText(QStringLiteral("thinking…\n").repeated(200));
            handle.widget = transcript;
            return handle;
        };
        view.resize(900, 1000);
        view.show();
        view.handleEvent(opened({QStringLiteral("K7Q2")}));
        view.pinSolo(QStringLiteral("K7Q2"));
        view.handleEvent(cardEvent(QStringLiteral("K7Q2"), lastId(sent)));
    };
    auto agentHeight = [](const relay::AgentSplit *split) { return split->sizes().value(1); };
    auto total = [](const relay::AgentSplit *split) { return split->sizes().value(0) + split->sizes().value(1); };

    QWidget consoles;
    QList<QJsonObject> sent;
    relay::BoardView view(QStringLiteral("/tmp/relay-card-solo-test"));
    openPinned(view, sent, consoles);
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QVERIFY(view.detailOpen());
    relay::AgentSplit *split = view.cardSplit();
    QVERIFY(split);
    QTRY_VERIFY(!split->agentFolded());   // the console is there, so the handle is live
    QVERIFY(split->handle(1)->isEnabled());
    QTRY_VERIFY(qAbs(agentHeight(split) - qRound(0.4 * total(split))) <= 2);
    int saves = 0;
    split->onUserMoved = [&saves] { ++saves; };

    // Up, past the old 40 % cap: more of the thread in place.
    const int before = agentHeight(split);
    dragHandle(split->handle(1), -250);
    QVERIFY2(agentHeight(split) > before + 200,
             qPrintable(QStringLiteral("%1 -> %2").arg(before).arg(agentHeight(split))));
    QVERIFY(agentHeight(split) > total(split) / 2);
    QVERIFY(saves >= 1);   // once per step of the drag; the window's save is debounced
    // Down: the card gets it back.
    dragHandle(split->handle(1), 400);
    QVERIFY(agentHeight(split) < before);
    const double chosen = split->agentShare();
    QVERIFY(relay::AgentSplit::validShare(chosen));
    // A window resize keeps the share — held up by the console's own minimum when the pane is
    // too short for it, and kept, so a taller pane has it back.
    view.resize(900, 700);
    QTRY_VERIFY2(qAbs(agentHeight(split) - qMax(split->widget(1)->minimumSizeHint().height(),
                                                 qRound(chosen * total(split)))) <= 2,
                 qPrintable(QStringLiteral("agent %1 of %2, share %3, content min %4, agent min %5")
                                .arg(agentHeight(split)).arg(total(split)).arg(chosen)
                                .arg(split->widget(0)->minimumSizeHint().height())
                                .arg(split->widget(1)->minimumSizeHint().height())));
    QCOMPARE(split->agentShare(), chosen);
    view.resize(900, 1000);
    QTRY_VERIFY(qAbs(agentHeight(split) - qRound(chosen * total(split))) <= 2);

    // The restored pane.
    QWidget consoles2;
    QList<QJsonObject> sent2;
    relay::BoardView again(QStringLiteral("/tmp/relay-card-solo-test"));
    again.cardSplit()->setAgentShare(chosen);
    openPinned(again, sent2, consoles2);
    QVERIFY(QTest::qWaitForWindowExposed(&again));
    relay::AgentSplit *second = again.cardSplit();
    QTRY_VERIFY(!second->agentFolded());
    QTRY_VERIFY(qAbs(agentHeight(second) - qRound(chosen * total(second))) <= 2);
    dragHandle(second->handle(1), -150);
    QCOMPARE(split->agentShare(), chosen);   // one pane's drag is its own
}

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
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    QStringList popped;
    view.onOpenInNewPane = [&popped](const QString &id) { popped << id; };
    view.resize(1600, 900);
    view.show();
    view.handleEvent(opened({QStringLiteral("K7Q2"), QStringLiteral("M3XJ")}));
    view.selectCard(QStringLiteral("M3XJ"));
    auto *list = view.findChild<QListWidget *>();
    QVERIFY(list);
    const int before = sent.size();
    // Through the real key path, with the list focused, the way a person presses it.
    list->setFocus();
    QTest::keyClick(list, Qt::Key_Return, Qt::ShiftModifier);
    QCoreApplication::processEvents();
    QCOMPARE(popped, QStringList{QStringLiteral("M3XJ")});
    // …and the list Board did not ask for the card as well: the page would open here too.
    for (int i = before; i < sent.size(); ++i)
        QVERIFY2(sent.at(i).value(QStringLiteral("type")).toString() != QStringLiteral("board_card_get"),
                 "Shift+Enter also opened the card on the list Board");
    // This board stays on its list; nothing opened here.
    QVERIFY(!view.detailOpen());
    QVERIFY(view.listPaneVisible());
    QVERIFY(!view.pinned());
}

// The live pass (#Y2BA, 2026-09-25): a row clicked on a wide board opens its page beside the
// rows, and Shift+Enter then popped the card out while leaving that page open, so the narrowed
// list showed the same card as the new pane and no rows at all.
void BoardSoloTests::shiftEnterOnTheOpenCardTakesItOffThisBoard()
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
    auto *list = view.findChild<QListWidget *>();
    QVERIFY(list);
    list->setFocus();
    QTest::keyClick(list, Qt::Key_Return, Qt::ShiftModifier);
    QCoreApplication::processEvents();
    QCOMPARE(popped, QStringList{QStringLiteral("K7Q2")});
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

void BoardSoloTests::aPinnedPaneLeavesTheTreeWatchToTheList()
{
    // Several card panes in a tab must not each watch the whole board tree: the list Board does,
    // and the tab's worker tells every view what changed. A card pane keeps the folder itself.
    QTemporaryDir project;
    QVERIFY(project.isValid());
    const QString root = project.path() + QStringLiteral("/board");
    QVERIFY(QDir().mkpath(root + QStringLiteral("/features/done")));
    QVERIFY(QDir().mkpath(root + QStringLiteral("/bugs")));
    QFile yaml(root + QStringLiteral("/board.yaml"));
    QVERIFY(yaml.open(QIODevice::WriteOnly));
    yaml.write("columns: [inbox]\n");
    yaml.close();

    relay::BoardView list(project.path());
    auto *listWatcher = list.findChild<QFileSystemWatcher *>();
    QVERIFY(listWatcher);
    QVERIFY(listWatcher->directories().size() >= 4);   // the folder and its three subfolders

    relay::BoardView card(project.path());
    card.onSend = [](const QJsonObject &) {};
    card.onClosePane = [] {};
    card.pinSolo(QStringLiteral("K7Q2"));
    auto *watcher = card.findChild<QFileSystemWatcher *>();
    QVERIFY(watcher);
    QCOMPARE(watcher->directories().size(), 1);
    QVERIFY(QFileInfo(watcher->directories().first()).fileName() == QStringLiteral("board"));
}

QTEST_MAIN(BoardSoloTests)
#include "boardsolo_test.moc"
