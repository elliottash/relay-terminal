// SPDX-License-Identifier: AGPL-3.0-or-later
// Two Switchboard panes on one workspace (#TTYB). The window hands every board event to every
// board pane of the workspace, so what keeps the panes independent is the request id: the worker
// echoes on a `board_card` the id of the `board_card_get` that asked for it, and a pane shows the
// card only when that id is its own. Opening a card in one tab must not open it in another — while
// the data broadcasts (`board`, `board_changed`) keep reaching every pane, so both lists move
// together. The one set of files is still one board; only each pane's own navigation is its own.
#include "BoardPane.h"
#include "CleanupTranscript.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QToolButton>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QDir>
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

// A `board_card` answer (protocol 19.2) minus the request id — whose id it carries is exactly
// what is under test, so the test stamps it by hand.
QJsonObject cardArrived(const QString &id)
{
    return QJsonObject{{"event", "board_card"}, {"card_id", id},
                       {"title", id + QStringLiteral(" card")}, {"status", "inbox"},
                       {"tab", "features"}, {"hash", "h1"},
                       {"path", QStringLiteral("issues/features/") + id + ".md"},
                       {"body", QStringLiteral("# %1 card\n\n## Issue\nthe ask\n").arg(id)},
                       {"issue", QStringLiteral("the ask")}, {"issue_heading", "Issue"},
                       {"thread", QJsonArray{}}, {"thread_total", 0}};
}

}  // namespace

class BoardPaneTests : public QObject {
    Q_OBJECT

private slots:
    void cleanupOperationsHaveTranscriptNotes();
    void tryItOpenKeepsWindowsPathsWithSpaces();
    void doneButtonAndKeyOfferUndo();
    void navigationSurvivesReload();
    void aCardOpenedInOnePaneDoesNotOpenInTheOther();
    void boardDataStillReachesBothPanes();
    void theCardPagesFlagClicksThroughToBoardPriority();
};

void BoardPaneTests::navigationSurvivesReload()
{
    const auto board = opened({row(QStringLiteral("K7Q2"), QStringLiteral("inbox"))});
    relay::BoardView original(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    original.onSend = [&sent](const QJsonObject &message) { sent << message; };
    original.handleEvent(board);
    original.openCard(QStringLiteral("K7Q2"));
    auto reply = cardArrived(QStringLiteral("K7Q2"));
    reply.insert(QStringLiteral("id"), sent.last().value(QStringLiteral("id")));
    original.handleEvent(reply);
    QVERIFY(original.detailOpen());
    auto saved = original.navigationState();
    saved.insert(QStringLiteral("filter"), QStringLiteral("label:bug"));

    relay::BoardView restored(QStringLiteral("/tmp/workspace"));
    sent.clear();
    restored.onSend = [&sent](const QJsonObject &message) { sent << message; };
    restored.restoreNavigation(saved);
    QCOMPARE(restored.navigationState(), saved); // also survives a save before loading finishes
    QVERIFY(sent.isEmpty());
    restored.handleEvent(board);
    QCOMPARE(sent.last().value(QStringLiteral("type")).toString(), QStringLiteral("board_card_get"));
    QCOMPARE(sent.last().value(QStringLiteral("card")).toString(), QStringLiteral("K7Q2"));
    reply.insert(QStringLiteral("id"), sent.last().value(QStringLiteral("id")));
    restored.handleEvent(reply);
    QVERIFY(restored.detailOpen());
    QCOMPARE(restored.navigationState(), saved);
    restored.closeDetail();
    QVERIFY(restored.navigationState().value(QStringLiteral("card")).toString().isEmpty());
    sent.clear();
    restored.handleEvent(board);
    for (const auto &message : sent)
        QVERIFY(message.value(QStringLiteral("type")) != QStringLiteral("board_card_get"));

    relay::BoardView missing(QStringLiteral("/tmp/workspace"));
    missing.onSend = [&sent](const QJsonObject &message) { sent << message; };
    missing.restoreNavigation(saved);
    sent.clear();
    missing.handleEvent(opened({}));
    QVERIFY(!missing.detailOpen());
    for (const auto &message : sent)
        QVERIFY(message.value(QStringLiteral("type")) != QStringLiteral("board_card_get"));
}

// The report behind the card: two tabs, one Switchboard each, the same project. A card opened in
// tab A opened in tab B too, because B handled the answer to A's own `board_card_get`. The fix is
// that the answer carries A's request id and B's pane leaves it alone — B keeps its list, its own
// selection, and its folds.
void BoardPaneTests::aCardOpenedInOnePaneDoesNotOpenInTheOther()
{
    relay::BoardView a(QStringLiteral("/tmp/workspace"));
    relay::BoardView b(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    a.onSend = [&sent](const QJsonObject &message) { sent << message; };
    const QJsonObject board = opened({row(QStringLiteral("K7Q2"), QStringLiteral("inbox")),
                                       row(QStringLiteral("M3XJ"), QStringLiteral("ready"))});
    a.handleEvent(board);
    b.handleEvent(board);
    a.setCollapsedSections(QJsonArray{});
    b.setCollapsedSections(QJsonArray{});
    b.selectCard(QStringLiteral("M3XJ"));       // B is reading somewhere else entirely

    // A opens a card, and the worker's answer — under A's request id — is handed by the window's
    // fan-out to both panes, because it asks neither pane where it lives.
    a.selectCard(QStringLiteral("K7Q2"));
    a.openSelected();
    QCOMPARE(sent.last().value(QStringLiteral("type")).toString(), QStringLiteral("board_card_get"));
    QJsonObject answer = cardArrived(QStringLiteral("K7Q2"));
    answer.insert(QStringLiteral("id"), sent.last().value(QStringLiteral("id")));
    const QString aId = answer.value(QStringLiteral("id")).toString();
    QVERIFY(!aId.isEmpty());
    a.handleEvent(answer);
    b.handleEvent(answer);

    QVERIFY(a.detailOpen());
    QVERIFY(!b.detailOpen());
    QCOMPARE(b.selectedCard(), QStringLiteral("M3XJ"));
    QCOMPARE(a.selectedCard(), QStringLiteral("K7Q2"));

    // An answer with no id at all opens nothing either: a pane shows only what it asked for.
    b.handleEvent(cardArrived(QStringLiteral("K7Q2")));
    QVERIFY(!b.detailOpen());
    QCOMPARE(b.selectedCard(), QStringLiteral("M3XJ"));
}

// The other half of the contract: scoping the answers must not scope the data. A `board_changed`
// broadcast reaches both panes, both models take the upsert, and B — mid-list — stays exactly
// where it was while A's open card re-reads under A's own id.
void BoardPaneTests::boardDataStillReachesBothPanes()
{
    relay::BoardView a(QStringLiteral("/tmp/workspace"));
    relay::BoardView b(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    a.onSend = [&sent](const QJsonObject &message) { sent << message; };
    const QJsonObject board = opened({row(QStringLiteral("K7Q2"), QStringLiteral("inbox")),
                                       row(QStringLiteral("M3XJ"), QStringLiteral("ready"))});
    a.handleEvent(board);
    b.handleEvent(board);
    a.setCollapsedSections(QJsonArray{});
    b.setCollapsedSections(QJsonArray{});
    b.selectCard(QStringLiteral("M3XJ"));

    a.selectCard(QStringLiteral("K7Q2"));
    a.openSelected();
    QJsonObject answer = cardArrived(QStringLiteral("K7Q2"));
    answer.insert(QStringLiteral("id"), sent.last().value(QStringLiteral("id")));
    a.handleEvent(answer);
    b.handleEvent(answer);
    QVERIFY(a.detailOpen());
    QVERIFY(!b.detailOpen());

    // Someone moves the card: the broadcast reaches both panes and both rows move — the board is
    // still one set of files.
    QJsonObject moved = row(QStringLiteral("K7Q2"), QStringLiteral("ready"));
    moved.insert(QStringLiteral("title"), QStringLiteral("K7Q2 card, retitled"));
    const QJsonObject change = QJsonObject{{"event", "board_changed"},
                                           {"upserts", QJsonArray{moved}},
                                           {"removed", QJsonArray{}}};
    a.handleEvent(change);
    b.handleEvent(change);
    QCOMPARE(a.model().card(QStringLiteral("K7Q2"))->status, QStringLiteral("ready"));
    QCOMPARE(b.model().card(QStringLiteral("K7Q2"))->status, QStringLiteral("ready"));
    QCOMPARE(b.model().card(QStringLiteral("K7Q2"))->title, QStringLiteral("K7Q2 card, retitled"));
    QCOMPARE(b.model().total(), 2);
    QVERIFY(!b.detailOpen());
    QCOMPARE(b.selectedCard(), QStringLiteral("M3XJ"));

    // The change touched A's open card, so A re-reads it — under A's own id, which B ignores.
    QCOMPARE(sent.last().value(QStringLiteral("type")).toString(), QStringLiteral("board_card_get"));
    QJsonObject reRead = cardArrived(QStringLiteral("K7Q2"));
    reRead.insert(QStringLiteral("title"), QStringLiteral("K7Q2 card, retitled"));
    reRead.insert(QStringLiteral("id"), sent.last().value(QStringLiteral("id")));
    a.handleEvent(reRead);
    b.handleEvent(reRead);
    QVERIFY(a.detailOpen());
    QVERIFY(!b.detailOpen());
    QCOMPARE(b.selectedCard(), QStringLiteral("M3XJ"));
}

// The card page's flag (#DPJB). The detail header draws the row's own flag and a click on it
// writes through the row's own path — one `board_priority`, clamped at −1…+3 — so a card can be
// flagged from the page it is read on and not only from its row. The pane is never shown in this
// test, so the press goes to the widget itself rather than through a window.
void BoardPaneTests::theCardPagesFlagClicksThroughToBoardPriority()
{
    relay::BoardView a(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    a.onSend = [&sent](const QJsonObject &message) { sent << message; };
    a.handleEvent(opened({row(QStringLiteral("K7Q2"), QStringLiteral("inbox"))}));
    a.setCollapsedSections(QJsonArray{});
    a.selectCard(QStringLiteral("K7Q2"));
    a.openSelected();
    QJsonObject answer = cardArrived(QStringLiteral("K7Q2"));
    answer.insert(QStringLiteral("id"), sent.last().value(QStringLiteral("id")).toString());
    a.handleEvent(answer);
    QVERIFY(a.detailOpen());

    QWidget *flag = a.findChild<QWidget *>(QStringLiteral("boardCardFlag"));
    QVERIFY(flag);
    const auto press = [flag](Qt::MouseButton button) {
        const QPointF at(flag->rect().center());
        QMouseEvent event(QEvent::MouseButtonPress, at, flag->mapToGlobal(at.toPoint()),
                          button, button, Qt::NoModifier);
        QCoreApplication::sendEvent(flag, &event);
    };
    const auto prioritySent = [&sent] {
        return sent.last().value(QStringLiteral("priority")).toInt();
    };

    press(Qt::LeftButton);
    QCOMPARE(sent.last().value(QStringLiteral("type")).toString(), QStringLiteral("board_priority"));
    QCOMPARE(sent.last().value(QStringLiteral("card")).toString(), QStringLiteral("K7Q2"));
    QCOMPARE(prioritySent(), 1);
    // The page shows the click at once, before the worker's `board_changed` settles it.
    QVERIFY(flag->toolTip().contains(QStringLiteral("+1")));
    // Right-click lowers it, and the ends clamp exactly as the row's click does.
    press(Qt::RightButton);
    QCOMPARE(prioritySent(), 0);
    QVERIFY(flag->toolTip().contains(QStringLiteral("Priority 0")));
    press(Qt::RightButton);
    QCOMPARE(prioritySent(), -1);
    press(Qt::RightButton);
    QCOMPARE(prioritySent(), -1);
}

void BoardPaneTests::doneButtonAndKeyOfferUndo()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.resize(1100, 760);
    view.show();
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    QString hint;
    view.onHint = [&hint](const QString &id, const QString &) { hint = id; };
    view.handleEvent(opened({row(QStringLiteral("K7Q2"), QStringLiteral("inbox"))}));
    view.selectCard(QStringLiteral("K7Q2"));
    QTest::keyClick(&view, Qt::Key_D);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_move"));
    QCOMPARE(sent.last().value("status").toString(), QStringLiteral("done"));
    QVERIFY(sent.last().contains("section"));
    QCOMPARE(sent.last().value("section").toString(), QString());

    view.openSelected();
    auto answer = cardArrived(QStringLiteral("K7Q2"));
    answer.insert("id", sent.last().value("id"));
    view.handleEvent(answer);
    auto *button = view.findChild<QToolButton *>(QStringLiteral("boardCardDone"));
    QVERIFY(button);
    QCOMPARE(button->text(), QStringLiteral("Done (d)"));
    QTest::mouseClick(button, Qt::LeftButton);
    QCOMPARE(hint, QStringLiteral("board.done"));
    QCOMPARE(sent.last().value("card").toString(), QStringLiteral("K7Q2"));
    QCOMPARE(sent.last().value("status").toString(), QStringLiteral("done"));
    view.handleEvent(QJsonObject{{"event", "board_written"}, {"id", sent.last().value("id")},
                                {"kind", "board_move"}, {"card_id", "K7Q2"}, {"write_id", "done-write"}});
    auto *notice = view.findChild<QWidget *>(QStringLiteral("boardNotice"));
    QVERIFY(notice && notice->isVisible());
    QToolButton *undo = nullptr;
    for (auto *candidate : notice->findChildren<QToolButton *>())
        if (candidate->text() == QStringLiteral("Undo (Ctrl+Z)"))
            undo = candidate;
    QVERIFY(undo && undo->isVisible());
    QVERIFY(notice->findChild<QLabel *>(QStringLiteral("boardNoticeText"))->text().contains("Marked #K7Q2 done"));
    QCoreApplication::processEvents();
    const QString capture = qEnvironmentVariable("RELAY_DONE_SCREENSHOT");
    if (!capture.isEmpty())
        QVERIFY(view.grab().save(capture));
    QTest::mouseClick(undo, Qt::LeftButton);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_undo"));
    QCOMPARE(sent.last().value("write_id").toString(), QStringLiteral("done-write"));

    auto *document = view.findChild<QWidget *>(QStringLiteral("boardCardDocument"));
    QVERIFY(document);
    QTest::keyClick(document, Qt::Key_D);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_move"));
    auto *filter = view.findChild<QLineEdit *>(QStringLiteral("boardFilter"));
    QVERIFY(filter);
    const auto writes = [&sent] {
        int count = 0;
        for (const auto &message : sent)
            count += message.value("type").toString() == QStringLiteral("board_move");
        return count;
    };
    const int before = writes();
    QTest::keyClick(filter, Qt::Key_D);
    QCOMPARE(filter->text(), QStringLiteral("d"));
    QCOMPARE(writes(), before);

    view.handleEvent(opened({row(QStringLiteral("K7Q2"), QStringLiteral("done"))}));
    QTest::keyClick(&view, Qt::Key_D);
    QCOMPARE(writes(), before);
}

void BoardPaneTests::tryItOpenKeepsWindowsPathsWithSpaces()
{
    const QStringList targets = {QStringLiteral("C:\\Users\\Some Person\\result.txt"),
                                 QStringLiteral("\\\\server\\shared folder\\result.txt"),
                                 QStringLiteral("pwsh -NoProfile -File 'C:\\Some Person\\stage.ps1'")};
    for (const QString &target : targets) {
        relay::BoardView view(QStringLiteral("/tmp/workspace"));
        QList<QJsonObject> sent;
        QString file, command;
        view.onSend = [&sent](const QJsonObject &message) { sent << message; };
        view.onOpenFile = [&file](const QString &path) { file = path; };
        view.onRunCommand = [&command](const QString &text) { command = text; };
        view.handleEvent(opened({row(QStringLiteral("K7Q2"), QStringLiteral("inbox"))}));
        view.openCard(QStringLiteral("K7Q2"));
        auto reply = cardArrived(QStringLiteral("K7Q2"));
        reply.insert(QStringLiteral("id"), sent.last().value(QStringLiteral("id")));
        reply.insert(QStringLiteral("body"), QStringLiteral("# Card\n\n## Try it\nOpen: `%1`\nDoes it work?\nExpected: expected.md\n").arg(target));
        view.handleEvent(reply);
        auto *button = view.findChild<QPushButton *>(QStringLiteral("boardTryOpen"));
        QVERIFY(button);
        button->click();
        if (target.startsWith(QStringLiteral("pwsh "))) {
            QCOMPARE(command, target);
            QVERIFY(file.isEmpty());
        } else {
            QCOMPARE(file, QDir(QStringLiteral("/tmp/workspace")).absoluteFilePath(target));
            QVERIFY(command.isEmpty());
        }
    }
}

void BoardPaneTests::cleanupOperationsHaveTranscriptNotes()
{
    using relay::board::cleanupTranscriptNote;
    QVERIFY(cleanupTranscriptNote({{"event", "tool_started"}}).isEmpty());
    QVERIFY(cleanupTranscriptNote({{"event", "delta"}, {"text", "already streamed"}}).isEmpty());
    QVERIFY(cleanupTranscriptNote({{"event", "board_activity"}, {"summary", "ordinary write"}}).isEmpty());
    const auto preview = cleanupTranscriptNote({{"event", "board_cleanup_started"}, {"dry_run", true}, {"cards", 3}});
    QVERIFY(preview.contains("nothing will be written"));
    QVERIFY(preview.contains("reading 3 cards"));
    const auto applying = cleanupTranscriptNote({{"event", "board_cleanup_started"}, {"dry_run", false}});
    QVERIFY(applying.contains("applying changes"));
    QCOMPARE(cleanupTranscriptNote({{"event", "board_activity"}, {"cleanup", true},
                                   {"id", "K7Q2"}, {"summary", "moved to ready"}}),
             QStringLiteral("◆ #K7Q2 · moved to ready\n"));
    QVERIFY(cleanupTranscriptNote({{"event", "board_activity"}, {"cleanup", true},
                                   {"summary", "reordered sections"}}).contains("board · reordered sections"));
    QJsonObject summary{{"event", "board_cleanup_summary"}, {"outcome", "done"}, {"dry_run", true},
                        {"counts", QJsonObject{{"proposed", 2}, {"writes", 1}}},
                        {"changelog", "docs/cleanup.md"}, {"report", "already streamed"},
                        {"refusals", QJsonArray{QJsonObject{{"tool", "board_move"}, {"error", "requires a verdict"}}}}};
    auto note = cleanupTranscriptNote(summary);
    QVERIFY(note.contains("finished · 2 proposed · nothing was written"));
    QVERIFY(note.contains("Refused board_move: requires a verdict"));
    QVERIFY(note.contains("Changelog: docs/cleanup.md"));
    QVERIFY(!note.contains("already streamed"));
    summary.insert("dry_run", false);
    summary.insert("outcome", "cancelled");
    QVERIFY(cleanupTranscriptNote(summary).contains("stopped · 1 written"));
    summary.insert("outcome", "error");
    QVERIFY(cleanupTranscriptNote(summary).contains("failed · 1 written"));
}

QTEST_MAIN(BoardPaneTests)
#include "boardpane_test.moc"
