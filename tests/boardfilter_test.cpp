// SPDX-License-Identifier: AGPL-3.0-or-later
// The Switchboard pane's filter bar, its chunked open, and what it shows when its worker dies
// (#7M6E). All three are the same change: each row used to carry its card's whole body and
// thread so that the filter's plain words could be matched here. That was 92.6 % of the `board`
// event's bytes; past about 1,160 cards the event overflowed the worker pipe's 8 MiB read buffer
// and the GUI killed the worker, with the only explanation going to a status bar this layout
// does not show — so the pane said "Loading the Switchboard…" for ever. The words are asked of
// the worker now (`board_search`), the rows travel in batches, and a worker failure is in the
// pane with a Retry.
//
// Separate from tests/boardpane_test.cpp, which is about how two panes on one board keep their
// navigation apart, and which another session was mid-edit in when this landed.
#include "BoardPane.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
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

class BoardFilterTests : public QObject {
    Q_OBJECT

private slots:
    void theFilterAsksTheWorkerAboutItsPlainWords();
    void theRestOfAChunkedBoardOpenPatchesTheRowsIn();
    void aWorkerFailureReplacesTheLoadingLine();
};

// The filter bar (#7M6E). Each card's whole body and thread used to ride on every row for one
// substring test here; it was 92.6 % of the `board` event and 30–80 ms of this thread per
// keystroke. The plain words go to the worker now (`board_search`), debounced, and only the
// newest answer is believed. The scoped terms never leave this side, so they need no worker.
void BoardFilterTests::theFilterAsksTheWorkerAboutItsPlainWords()
{
    relay::BoardView a(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    a.onSend = [&sent](const QJsonObject &message) { sent << message; };
    a.handleEvent(opened({row(QStringLiteral("K7Q2"), QStringLiteral("inbox")),
                          row(QStringLiteral("M3XJ"), QStringLiteral("ready"))}));
    a.setCollapsedSections(QJsonArray{});
    auto *filter = a.findChild<QLineEdit *>(QStringLiteral("boardFilter"));
    QVERIFY(filter);
    const auto searches = [&sent] {
        QList<QJsonObject> out;
        for (const QJsonObject &message : sent)
            if (message.value(QStringLiteral("type")).toString() == QStringLiteral("board_search"))
                out << message;
        return out;
    };

    // A scoped term is answered from the rows: nothing goes down the pipe at all.
    sent.clear();
    filter->setText(QStringLiteral("status:ready"));
    QTest::qWait(250);
    QCOMPARE(searches().size(), 0);
    QCOMPARE(a.model().openCount(), 1);

    // Plain words are asked of the worker — once for the burst, not once per key — and the list
    // shows what the rows alone can say in the meantime.
    sent.clear();
    filter->setText(QStringLiteral("hot"));
    filter->setText(QStringLiteral("hotl"));
    filter->setText(QStringLiteral("hotline"));
    QCOMPARE(a.model().openCount(), 0);          // no row field holds it, and no answer yet
    QTest::qWait(250);
    QCOMPARE(searches().size(), 1);
    QCOMPARE(searches().last().value(QStringLiteral("query")).toString(), QStringLiteral("hotline"));

    // The answer arrives under that request's id and the list settles on it.
    QJsonObject answer{{"event", "board_search"}, {"query", "hotline"},
                       {"ids", QJsonArray{QStringLiteral("M3XJ")}}};
    answer.insert(QStringLiteral("id"), searches().last().value(QStringLiteral("id")));
    a.handleEvent(answer);
    QCOMPARE(a.model().openCount(), 1);
    QCOMPARE(a.model().cards(QStringLiteral("ready")).size(), 1);

    // A superseded answer — one the box has already typed past — is dropped, not applied.
    QJsonObject stale = answer;
    stale.insert(QStringLiteral("id"), QStringLiteral("not-the-one"));
    stale.insert(QStringLiteral("ids"), QJsonArray{QStringLiteral("K7Q2"), QStringLiteral("M3XJ")});
    a.handleEvent(stale);
    QCOMPARE(a.model().openCount(), 1);

    // A card changing under a live filter asks the same words again: the body it matched on is
    // only the worker's to see.
    sent.clear();
    a.handleEvent(QJsonObject{{"event", "board_changed"},
                              {"upserts", QJsonArray{row(QStringLiteral("K7Q2"),
                                                         QStringLiteral("ready"))}},
                              {"removed", QJsonArray{}}});
    QTest::qWait(250);
    QCOMPARE(searches().size(), 1);
    QCOMPARE(searches().last().value(QStringLiteral("query")).toString(), QStringLiteral("hotline"));

    // A worker too old to know the message refuses it. That is not news — the filter goes on
    // matching the row's own fields, which is all it could do before `board_search` existed — so
    // the pane says nothing and stops asking, rather than collecting a notice per burst of typing.
    QJsonObject refusal{{"event", "error"}, {"text", "Unknown protocol message."}};
    refusal.insert(QStringLiteral("id"), searches().last().value(QStringLiteral("id")));
    a.handleEvent(refusal);
    QVERIFY(a.notice().isEmpty());
    sent.clear();
    filter->setText(QStringLiteral("hotline again"));
    QTest::qWait(250);
    QCOMPARE(searches().size(), 0);
    QCOMPARE(a.model().openCount(), 0);   // and the fields still decide, as they always did
}

// `board_open` answers in batches so that no board size can overflow the worker pipe's read
// buffer (#7M6E): the pane draws the first batch and patches the rest in as they arrive.
void BoardFilterTests::theRestOfAChunkedBoardOpenPatchesTheRowsIn()
{
    relay::BoardView a(QStringLiteral("/tmp/workspace"));
    QJsonObject board = opened({row(QStringLiteral("K7Q2"), QStringLiteral("inbox"))});
    board.insert(QStringLiteral("cards_total"), 3);
    board.insert(QStringLiteral("more"), true);
    a.handleEvent(board);
    a.setCollapsedSections(QJsonArray{});
    QCOMPARE(a.model().total(), 1);

    a.handleEvent(QJsonObject{{"event", "board_cards"}, {"more", true},
                              {"cards", QJsonArray{row(QStringLiteral("M3XJ"),
                                                       QStringLiteral("ready"))}}});
    QCOMPARE(a.model().total(), 2);
    a.handleEvent(QJsonObject{{"event", "board_cards"}, {"more", false},
                              {"cards", QJsonArray{row(QStringLiteral("P8T4"),
                                                       QStringLiteral("done"))}}});
    QCOMPARE(a.model().total(), 3);
    QCOMPARE(a.model().card(QStringLiteral("P8T4"))->status, QStringLiteral("done"));
    // The last batch redraws the list, so every card that arrived is on the page.
    QCOMPARE(relay::board::rowOfCard(a.rows(), QStringLiteral("P8T4")) >= 0, true);
}

// A worker that dies says so in the pane, not only in a status bar this layout never shows
// (#7M6E): the board that never loaded used to sit on "Loading the Switchboard…" for ever, which
// is what a board of more than about 1,160 cards did to it.
void BoardFilterTests::aWorkerFailureReplacesTheLoadingLine()
{
    relay::BoardView a(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    a.onSend = [&sent](const QJsonObject &message) { sent << message; };
    auto *empty = a.findChild<QLabel *>(QStringLiteral("boardEmpty"));
    auto *retryRow = a.findChild<QWidget *>(QStringLiteral("boardEmptyRetry"));
    QVERIFY(empty);
    QVERIFY(retryRow);
    QVERIFY(empty->text().contains(QStringLiteral("Loading")));

    a.handleEvent(QJsonObject{{"event", "board_worker_status"},
                              {"text", "Switchboard worker protocol overflow; stopped."}});
    QVERIFY(!empty->text().contains(QStringLiteral("Loading")));
    QVERIFY(empty->text().contains(QStringLiteral("protocol overflow")));

    // And the Retry is the `board_open` the pane opens with; the window starts a fresh worker.
    auto *retry = retryRow->findChild<QToolButton *>();
    QVERIFY(retry);
    sent.clear();
    retry->click();
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value(QStringLiteral("type")).toString(), QStringLiteral("board_open"));
    QVERIFY(empty->text().contains(QStringLiteral("Loading")));

    // A board that is already on screen keeps its cards: the failure is a notice, not a wipe.
    a.handleEvent(opened({row(QStringLiteral("K7Q2"), QStringLiteral("inbox"))}));
    a.handleEvent(QJsonObject{{"event", "board_worker_status"},
                              {"text", "The Switchboard worker exited."}});
    QCOMPARE(a.model().total(), 1);
    QVERIFY(a.notice().contains(QStringLiteral("exited")));
}

QTEST_MAIN(BoardFilterTests)
#include "boardfilter_test.moc"
