// SPDX-License-Identifier: AGPL-3.0-or-later
// Two Switchboard panes on one workspace (#TTYB). The window hands every board event to every
// board pane of the workspace, so what keeps the panes independent is the request id: the worker
// echoes on a `board_card` the id of the `board_card_get` that asked for it, and a pane shows the
// card only when that id is its own. Opening a card in one tab must not open it in another — while
// the data broadcasts (`board`, `board_changed`) keep reaching every pane, so both lists move
// together. The one set of files is still one board; only each pane's own navigation is its own.
#include "BoardPane.h"
#include "CleanupTranscript.h"
#include "Theme.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QToolButton>
#include <QLineEdit>
#include <QLabel>
#include <QBoxLayout>
#include <optional>
#include <QPushButton>
#include <QDir>
#include <QScrollArea>
#include <QScrollBar>
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
    void longFindingsRemainReadableAndScrollable();
    void namedDriverUsesControlsAndRefusesUnavailableTargets();
    void cleanupOperationsHaveTranscriptNotes();
    void unsurfacedEventsNameTheirCard();
    void tryItOpenKeepsWindowsPathsWithSpaces();
    void theVerifyStripReadsTheCardsVerifyBlock();
    void doneButtonAndKeyOfferUndo();
    void navigationSurvivesReload();
    void aCardOpenedInOnePaneDoesNotOpenInTheOther();
    void boardDataStillReachesBothPanes();
    void theCardPagesFlagClicksThroughToBoardPriority();
    void hygieneChecksBeforeCleanup();
};

void BoardPaneTests::longFindingsRemainReadableAndScrollable()
{
    relay::BoardView view(QStringLiteral("/tmp/relay-findings-test"));
    QList<QJsonObject> sent;
    view.onSend = [&](const QJsonObject &message) { sent << message; };
    view.resize(900, 650);
    view.show();
    view.handleEvent(opened({row(QStringLiteral("K7Q2"), QStringLiteral("inbox"))}));
    view.openCard(QStringLiteral("K7Q2"));
    auto reply = cardArrived(QStringLiteral("K7Q2"));
    reply.insert("id", sent.last().value("id"));
    reply.insert("body", reply.value("body").toString() + "\n## Tests\n`ctest -R board`\n");
    reply.insert("sections", QJsonArray{"Issue", "Tests"});
    view.handleEvent(reply);
    QJsonArray findings;
    for (int i = 0; i < 30; ++i)
        findings.append(QJsonObject{{"test", QStringLiteral("ctest:case%1").arg(i)},
                                    {"severity", "warning"},
                                    {"message", "This test has no recorded evidence for the revision."}});
    view.handleEvent(QJsonObject{{"event", "tests_check"}, {"card", "K7Q2"},
                                  {"findings", findings}});
    auto *scroll = view.findChild<QScrollArea *>(QStringLiteral("boardTestsFindings"));
    QVERIFY(scroll);
    QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
    const auto rows = scroll->findChildren<QLabel *>(QStringLiteral("boardTestsFinding"));
    QCOMPARE(rows.size(), 31); // the summary plus thirty findings
    for (const auto *label : rows)
        QVERIFY(label->height() >= label->fontMetrics().height());
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    QVERIFY(scroll->viewport()->rect().intersects(
            QRect(rows.last()->mapTo(scroll->viewport(), QPoint()), rows.last()->size())));
}

void BoardPaneTests::hygieneChecksBeforeCleanup()
{
    relay::BoardView view(QStringLiteral("/tmp/relay-hygiene-test"));
    QList<QJsonObject> sent;
    relay::agent::Context *context = nullptr;
    view.onSend = [&](const QJsonObject &message) { sent << message; };
    view.onCreateConsole = [&](relay::agent::Context *value, QWidget *parent) {
        context = value;
        relay::agent::ConsoleHandle handle;
        handle.widget = new QWidget(parent);
        return handle;
    };
    view.resize(900, 700);
    view.show();
    view.handleEvent(opened({row("AB12", "inbox")}));
    QCoreApplication::processEvents();
    QVERIFY(context);
    const auto actions = context->actions();
    QCOMPARE(actions.size(), 4);
    QCOMPARE(actions[0].label, QStringLiteral("Hygiene"));
    QCOMPARE(actions[1].label, QStringLiteral("Tests"));
    QCOMPARE(actions[2].label, QStringLiteral("Review"));
    QCOMPARE(actions[3].label, QStringLiteral("Performance"));
    QCOMPARE(actions[1].key, QStringLiteral("boardTests"));
    QCOMPARE(actions[2].key, QStringLiteral("boardReview"));
    QCOMPARE(actions[3].key, QStringLiteral("boardProfile"));
    bool reviewOpened = false;
    view.onOpenReview = [&] { reviewOpened = true; };
    actions[2].run();
    QVERIFY(reviewOpened);
    sent.clear();
    actions[0].run();
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_check"));
    QVERIFY(!view.findChild<QToolButton *>("boardCleanup"));
    view.handleEvent({{"event", "board_problems"}, {"id", sent.last().value("id")},
                      {"items", QJsonArray{}}});
    auto *cleanup = view.findChild<QToolButton *>("boardCleanup");
    QVERIFY(cleanup && cleanup->isVisible());
    QVERIFY(view.findChild<QLabel *>("boardChatFindingsHead")->text().contains("Hygiene"));
    cleanup->click();
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_cleanup"));
    QVERIFY(sent.last().value("dry_run").toBool());
    QCOMPARE(cleanup->text(), QStringLiteral("Stop"));
    cleanup->click();
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("cancel"));
    view.handleEvent({{"event", "board_cleanup_summary"}, {"dry_run", true}, {"outcome", "done"},
                      {"changes", QJsonArray{QJsonObject{{"action", "update"}, {"id", "AB12"}}}}});
    auto *panel = view.findChild<QWidget *>("boardCleanupPanel");
    QVERIFY(panel && panel->isVisible());
    QCOMPARE(panel->parentWidget(), view.findChild<QWidget *>("boardChatArea"));
    QToolButton *apply = nullptr;
    for (auto *button : panel->findChildren<QToolButton *>())
        if (button->text() == QStringLiteral("Apply")) apply = button;
    QVERIFY(apply && apply->isVisible());
    apply->click();
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_cleanup"));
    QVERIFY(!sent.last().value("dry_run").toBool());
}

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

// The Verify strip (#WFRA): what of the card's `verify` block is the person's business, one
// line under the Try it strip (owner steer 2026-09-23: shown only when a person is needed). A
// plan that asks for a review, a sign-off or a deferral draws it; a card with no block and a
// fully machine-verified card draw nothing at all, and the same pane re-reading a card whose
// block changed goes quiet or speaks again — the strip is read off the event, never kept.
// Failure shows as the agent's work put in front of the person, or their part of it hidden.
void BoardPaneTests::theVerifyStripReadsTheCardsVerifyBlock()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row(QStringLiteral("K7Q2"), QStringLiteral("in-progress")),
                             row(QStringLiteral("SSRQ"), QStringLiteral("inbox"))}));
    auto arrive = [&view, &sent](const QString &id, std::optional<QJsonObject> verify) {
        auto reply = cardArrived(id);
        reply.insert(QStringLiteral("id"), sent.last().value(QStringLiteral("id")));
        if (verify)
            reply.insert(QStringLiteral("verify"), *verify);
        view.handleEvent(reply);
    };
    view.openCard(QStringLiteral("K7Q2"));
    arrive(QStringLiteral("K7Q2"),
           QJsonObject{{"artifact", "visual"}, {"primary", "probe"},
                       {"also", QJsonArray{"ai-visual", "pairwise"}},
                       {"human", "required"}, {"criteria", "the strip reads as one line"},
                       {"sign_off", "none"}, {"effort", "medium"}, {"stakes", "rework"},
                       {"blast", "capability"}});
    auto *strip = view.findChild<QLabel *>(QStringLiteral("boardVerifyStrip"));
    QVERIFY(strip);
    QCOMPARE(strip->text(),
             QStringLiteral("<span style=\"color:%1\">Your review: the strip reads as one line · "
                            "effort medium</span>")
                 .arg(relay::theme::TextMuted.name()));
    QVERIFY(strip->isVisibleTo(&view));
    QVERIFY(strip->toolTip().contains(QStringLiteral("artifact: visual")));
    // Under the Try it strip and above the body, where the card's machine-read strips live.
    auto *document = view.findChild<QWidget *>(QStringLiteral("boardCardDocument"));
    auto *tryStrip = view.findChild<QWidget *>(QStringLiteral("boardTryStrip"));
    QVERIFY(document && tryStrip);
    auto *column = qobject_cast<QBoxLayout *>(document->parentWidget()->layout());
    QVERIFY(column);
    QVERIFY(column->indexOf(tryStrip) < column->indexOf(strip));
    QVERIFY(column->indexOf(strip) < column->indexOf(document));
    // Evidence for the card (docs/qa_evidence/2026-09-23-WFRA-strip): the directory this names
    // gets a grab of the page with something to review and one with nothing to see.
    const QString captureDir = qEnvironmentVariable("RELAY_VERIFY_STRIP_SCREENSHOTS");
    if (!captureDir.isEmpty()) {
        view.resize(1100, 800);
        view.show();
        QVERIFY(view.grab().save(QDir(captureDir).filePath(QStringLiteral("card-asking-for-review.png"))));
    }

    // The next card has no block at all: no strip, not a placeholder. The page is the card.
    view.openCard(QStringLiteral("SSRQ"));
    arrive(QStringLiteral("SSRQ"), std::nullopt);
    QVERIFY(strip->text().isEmpty());
    QVERIFY(strip->toolTip().isEmpty());
    QVERIFY(!strip->isVisibleTo(&view));
    if (!captureDir.isEmpty())
        QVERIFY(view.grab().save(QDir(captureDir).filePath(QStringLiteral("card-with-nothing-to-verify.png"))));

    // A deferral the person should know about, with no review asked: one muted line for it.
    view.openCard(QStringLiteral("K7Q2"));
    arrive(QStringLiteral("K7Q2"), QJsonObject{{"primary", "script"},
                                               {"deferred", "the fixture lands"},
                                               {"human", "none"}, {"effort", "low"}});
    QCOMPARE(strip->text(), QStringLiteral("<span style=\"color:%1\">Unverified until the "
                                           "fixture lands</span>")
                                .arg(relay::theme::TextMuted.name()));

    // A sign-off beyond `none`: theirs to give.
    view.openCard(QStringLiteral("K7Q2"));
    arrive(QStringLiteral("K7Q2"), QJsonObject{{"primary", "script"}, {"human", "none"},
                                               {"sign_off", "publish"}, {"effort", "low"}});
    QCOMPARE(strip->text(), QStringLiteral("<span style=\"color:%1\">Needs your sign-off: "
                                           "publish</span>")
                                .arg(relay::theme::TextMuted.name()));

    // A fully machine-verified plan: the same quiet page as a card with no block.
    view.openCard(QStringLiteral("K7Q2"));
    arrive(QStringLiteral("K7Q2"), QJsonObject{{"primary", "script"}, {"human", "none"},
                                               {"sign_off", "none"}, {"effort", "low"}});
    QVERIFY(strip->text().isEmpty());
    QVERIFY(!strip->isVisibleTo(&view));
}

// What `RelayWindow::deliverToConsoles` asks of an unsurfaced event before showing it to a card
// console (#KSKH): the card it names, or nothing when it is the tab's own news. A card turn's
// own events never reach this helper — they carry `surface: "card:<ID>"` and are routed by it —
// these are the board's own (`board_activity`, `board_thread_appended`), which used to broadcast
// to every console of the tab and printed one card's write line inside another card's console.
void BoardPaneTests::unsurfacedEventsNameTheirCard()
{
    using relay::board::namedCardOf;
    QCOMPARE(namedCardOf({{"event", "board_activity"}, {"id", "K7Q2"}, {"summary", "replaced `## Plan`"}}),
             QStringLiteral("K7Q2"));
    QCOMPARE(namedCardOf({{"event", "board_thread_appended"}, {"card_id", "SSRQ"}, {"author", "agent"}}),
             QStringLiteral("SSRQ"));
    // The tab's own news names no card and reaches every console as before.
    QVERIFY(namedCardOf({{"event", "board_activity"}, {"summary", "reordered sections"}}).isEmpty());
    QVERIFY(namedCardOf({{"event", "configured"}, {"model", "kimi-k3"}}).isEmpty());
    QVERIFY(namedCardOf({{"event", "queue_changed"}}).isEmpty());
    // A card turn's own event is surface-routed already; the card it would name is its own.
    QCOMPARE(namedCardOf({{"event", "delta"}, {"card_id", "K7Q2"}, {"surface", "card:K7Q2"}}),
             QStringLiteral("K7Q2"));
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


void BoardPaneTests::namedDriverUsesControlsAndRefusesUnavailableTargets()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.resize(1200, 900); view.show();
    view.handleEvent(opened({row(QStringLiteral("K7Q2"), QStringLiteral("inbox"))}));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    QVERIFY(view.drive({{"op", "type"}, {"name", "boardFilter"}, {"text", "K7Q2"}}).value("ok").toBool());
    QCOMPARE(view.drive({{"op", "read"}, {"name", "boardFilter"}}).value("text").toString(), QStringLiteral("K7Q2"));
    QVERIFY(!view.drive({{"op", "type"}, {"name", "composer"}, {"text", "echo bad"}}).value("ok").toBool());
    QVERIFY(!view.drive({{"op", "press"}, {"name", "missing"}}).value("ok").toBool());
    QVERIFY(!view.drive({{"op", "open"}, {"card", "NONE"}}).value("ok").toBool());
    QVERIFY(view.drive({{"op", "open"}, {"card", "K7Q2"}}).value("ok").toBool());
    auto arrived = cardArrived(QStringLiteral("K7Q2"));
    arrived.insert(QStringLiteral("id"), sent.last().value(QStringLiteral("id")));
    view.handleEvent(arrived);
    QVERIFY(view.drive({{"op", "read"}, {"name", "sections"}}).value("text").toString().contains("the ask"));
    auto *done = view.findChild<QAbstractButton *>(QStringLiteral("boardCardDone"));
    QVERIFY(done); done->setEnabled(false);
    QCOMPARE(view.drive({{"op", "press"}, {"name", "boardCardDone"}}).value("error").toString(), QStringLiteral("control_disabled"));
    done->setEnabled(true);
    QVERIFY(view.drive({{"op", "press"}, {"name", "boardCardDone"}}).value("ok").toBool());
    QTRY_VERIFY(!sent.isEmpty() && sent.last().value("type").toString() == QStringLiteral("board_move"));
}
