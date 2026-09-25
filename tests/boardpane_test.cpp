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
#include <QTreeWidget>
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
    void aCardLinkOpensTheBoardOnTheCardAlone();
    void aCardOpenedInOnePaneDoesNotOpenInTheOther();
    void boardDataStillReachesBothPanes();
    void theCardPagesFlagClicksThroughToBoardPriority();
    void theCardPagesLabelsEditInPlace();
    void metadataPageListsChildrenReverseLinksAndCommits();
    void hygieneChecksBeforeCleanup();
    void emptyAgentTranscriptDoesNotReserveConversationHeight();
    // The three object tabs (#9FX8 step 2).
    void theSkillsTabListsProjectSkillsAndOpensAPage();
    void theMemoriesTabShowsExpiredFirstAndOpensTheCard();
    // The Live strip on the Cards tab (#TBRH).
    void theLiveStripListsThisProjectsPanesAndTheirCards();
    // The reverse side of the Linked panels, from `board_links` (#EE42).
    void theLinkedPanelsDrawTheReverseSideFromBoardLinks();
};

void BoardPaneTests::metadataPageListsChildrenReverseLinksAndCommits()
{
    relay::BoardView view(QStringLiteral("/tmp/relay-board-metadata-test"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row(QStringLiteral("K7Q2"), QStringLiteral("ready")),
                             row(QStringLiteral("M3XJ"), QStringLiteral("ready"))}));
    view.openCard(QStringLiteral("K7Q2"));
    QJsonObject answer = cardArrived(QStringLiteral("K7Q2"));
    answer.insert(QStringLiteral("id"), sent.last().value(QStringLiteral("id")));
    answer.insert(QStringLiteral("children"), QJsonArray{QJsonObject{
        {"id", "M3XJ"}, {"title", "A child"}, {"status", "done"}, {"done", true}}});
    answer.insert(QStringLiteral("reverse"), QJsonObject{{"blocks", QJsonArray{QJsonObject{
        {"id", "M3XJ"}, {"title", "A child"}}}}});
    answer.insert(QStringLiteral("commits"), QJsonArray{QJsonObject{
        {"hash", "abcdef123456"}, {"date", "2026-09-25"},
        {"subject", "A change"}, {"author", "Test"}, {"signature", "openai/gpt-6-sol"}}});
    view.handleEvent(answer);
    auto *meta = view.findChild<QLabel *>(QStringLiteral("boardCardMeta"));
    QVERIFY(meta);
    QVERIFY(meta->text().contains(QStringLiteral("Children 1/1")));
    QVERIFY(meta->text().contains(QStringLiteral("blocks")));
    QVERIFY(meta->text().contains(QStringLiteral("relay-commit:abcdef123456")));
}

void BoardPaneTests::emptyAgentTranscriptDoesNotReserveConversationHeight()
{
    relay::BoardView view(QStringLiteral("/tmp/relay-empty-transcript-test"));
    QWidget *transcript = nullptr;
    QWidget *console = nullptr;
    view.onCreateConsole = [&](relay::agent::Context *, QWidget *parent) {
        relay::agent::ConsoleHandle handle;
        console = new QWidget(parent);
        auto *column = new QVBoxLayout(console);
        transcript = new QWidget(console);
        column->addWidget(transcript, 1);
        auto *queue = new QWidget(console);
        queue->setObjectName(QStringLiteral("queueStrip"));
        queue->hide();
        column->addWidget(queue);
        auto *composer = new QWidget(console);
        composer->setFixedHeight(100);
        column->addWidget(composer);
        handle.widget = console;
        handle.setTranscriptHiddenUntilUsed = [transcript](bool hide) {
            transcript->setVisible(!hide);
        };
        return handle;
    };
    view.resize(995, 1200);
    view.show();
    view.handleEvent(opened({row(QStringLiteral("AB12"), QStringLiteral("inbox"))}));
    QCoreApplication::processEvents();
    QVERIFY(console);
    QVERIFY(transcript->isHidden());
    QCOMPARE(console->minimumHeight(), 0);
    QVERIFY(console->height() < 200);

    transcript->show(); // the first agent output opens the transcript
    QTRY_VERIFY(console->minimumHeight() >= 200);
    QVERIFY(transcript->isVisible());
}

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
    // #1Q5V evidence: the list page top — section checkboxes, engraved header, and no label
    // chips row (RELAY_SHOT_DIR set writes it, the conversations suite's own pattern).
    const QString boardShotDir = qEnvironmentVariable("RELAY_SHOT_DIR");
    if (!boardShotDir.isEmpty())
        QVERIFY(restored.grab().save(boardShotDir + QStringLiteral("/board-top.png")));
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

// #K4SQ: a card link clicked outside the board — a `#ID` in chat, a notification — opens the
// board *on* the card, not the card squeezed beside the list. At 995 the pane is wide
// (kCardSplitWidth is 900), so an ordinary open still splits list and card; the link's reveal
// gives the page the whole pane, an in-board link keeps it (the list does not pop back in beside
// the next card), and closing the page puts the list back for ordinary opens again.
void BoardPaneTests::aCardLinkOpensTheBoardOnTheCardAlone()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.resize(995, 900);
    view.show();
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row(QStringLiteral("K7Q2"), QStringLiteral("inbox")),
                              row(QStringLiteral("M3XJ"), QStringLiteral("ready"))}));
    QCoreApplication::processEvents();
    const auto answer = [&sent](const QString &id) {
        QJsonObject reply = cardArrived(id);
        reply.insert(QStringLiteral("id"), sent.last().value(QStringLiteral("id")));
        return reply;
    };

    // By hand (Enter, a row click) the card sits beside the list, as it always has.
    view.openCard(QStringLiteral("K7Q2"));
    view.handleEvent(answer(QStringLiteral("K7Q2")));
    QVERIFY(view.detailOpen());
    QVERIFY(view.listPaneVisible());
    // #K4SQ evidence: the same width with the list beside the card, against the solo shot below
    // (RELAY_SHOT_DIR set writes it, the navigation test's own pattern).
    const QString shotDir = qEnvironmentVariable("RELAY_SHOT_DIR");
    if (!shotDir.isEmpty())
        QVERIFY(view.grab().save(shotDir + QStringLiteral("/board-card-split.png")));
    view.closeDetail();
    QVERIFY(!view.detailOpen());

    // The link's reveal at the same width: the card has the pane to itself.
    view.openCardSolo(QStringLiteral("M3XJ"));
    view.handleEvent(answer(QStringLiteral("M3XJ")));
    QVERIFY(view.detailOpen());
    QVERIFY(!view.listPaneVisible());
    if (!shotDir.isEmpty())
        QVERIFY(view.grab().save(shotDir + QStringLiteral("/board-card-solo.png")));

    // A `#ID` clicked inside that page keeps the pane to itself — no list beside the next card.
    view.openCard(QStringLiteral("K7Q2"));
    view.handleEvent(answer(QStringLiteral("K7Q2")));
    QVERIFY(view.detailOpen());
    QVERIFY(!view.listPaneVisible());

    // Esc back to the board: the list is what comes back, and ordinary opens split again.
    view.closeDetail();
    QVERIFY(view.listPaneVisible());
    view.openCard(QStringLiteral("M3XJ"));
    view.handleEvent(answer(QStringLiteral("M3XJ")));
    QVERIFY(view.detailOpen());
    QVERIFY(view.listPaneVisible());
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

// The labels editor (#E0Y0): × takes one label off the card, + opens a one-line field that
// saves the whole list on Enter and nothing on Esc; the label word itself still only copies.
void BoardPaneTests::theCardPagesLabelsEditInPlace()
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
    answer.insert(QStringLiteral("front"),
                  QJsonObject{{QStringLiteral("labels"), QJsonArray{QStringLiteral("bug"),
                                                                    QStringLiteral("voice")}}});
    a.handleEvent(answer);
    QVERIFY(a.detailOpen());

    QLabel *meta = a.findChild<QLabel *>(QStringLiteral("boardCardMeta"));
    QVERIFY(meta);
    QVERIFY(meta->text().contains(QStringLiteral("tagx:bug")));
    QVERIFY(meta->text().contains(QStringLiteral("tagadd:")));

    // × writes the list without the clicked label, through the hash-checked board_update
    // the title saves with.
    QMetaObject::invokeMethod(meta, "linkActivated", Q_ARG(QString, QStringLiteral("tagx:bug")));
    QCOMPARE(sent.last().value(QStringLiteral("type")).toString(), QStringLiteral("board_update"));
    QCOMPARE(sent.last().value(QStringLiteral("card")).toString(), QStringLiteral("K7Q2"));
    QCOMPARE(sent.last().value(QStringLiteral("base_hash")).toString(), QStringLiteral("h1"));
    const QJsonArray rest = sent.last().value(QStringLiteral("patch")).toObject()
                                .value(QStringLiteral("fields")).toObject()
                                .value(QStringLiteral("labels")).toArray();
    QCOMPARE(rest.size(), 1);
    QCOMPARE(rest.first().toString(), QStringLiteral("voice"));

    // + opens the field pre-filled with the card's labels; Enter saves the edited list.
    QMetaObject::invokeMethod(meta, "linkActivated", Q_ARG(QString, QStringLiteral("tagadd:")));
    QLineEdit *edit = a.findChild<QLineEdit *>(QStringLiteral("boardCardLabelEdit"));
    QVERIFY(edit);
    QVERIFY(!edit->isHidden());
    QCOMPARE(edit->text(), QStringLiteral("bug, voice"));
    edit->setText(QStringLiteral("voice, remote"));
    QTest::keyClick(edit, Qt::Key_Return);
    const QJsonArray saved = sent.last().value(QStringLiteral("patch")).toObject()
                                 .value(QStringLiteral("fields")).toObject()
                                 .value(QStringLiteral("labels")).toArray();
    QCOMPARE(saved.size(), 2);
    QCOMPARE(saved.first().toString(), QStringLiteral("voice"));
    QCOMPARE(saved.last().toString(), QStringLiteral("remote"));
    QVERIFY(edit->isHidden());

    // Esc closes the field and writes nothing.
    QMetaObject::invokeMethod(meta, "linkActivated", Q_ARG(QString, QStringLiteral("tagadd:")));
    QVERIFY(!edit->isHidden());
    const int before = sent.size();
    QTest::keyClick(edit, Qt::Key_Escape);
    QVERIFY(edit->isHidden());
    QCOMPARE(sent.size(), before);

    // The plain label link is still only the board filter term (#3ZAP), not an edit.
    QMetaObject::invokeMethod(meta, "linkActivated", Q_ARG(QString, QStringLiteral("tag:voice")));
    QCOMPARE(sent.size(), before);
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

// The Skills tab (#9FX8 step 2): Cards opens first (decision 1); the registry's project rows
// list (decision 2), with version, cases, pass rate, last verified and the stale flag; the
// stale reason and the profile strip read the same row the list does; the page's tab choice
// rides navigation state so it persists per board.
void BoardPaneTests::theSkillsTabListsProjectSkillsAndOpensAPage()
{
    relay::BoardView view(QStringLiteral("/tmp/relay-skills-tab-test"));
    // #9FX8 evidence: RELAY_SHOT_DIR set writes the tab and the skill page (the pattern above).
    const QString shotDir = qEnvironmentVariable("RELAY_SHOT_DIR");
    if (!shotDir.isEmpty()) {
        view.resize(1000, 760);
        view.show();
    }
    view.handleEvent(opened({row(QStringLiteral("K7Q2"), QStringLiteral("inbox"))}));
    auto *tabs = view.findChild<QWidget *>(QStringLiteral("boardPageTabs"));
    QVERIFY(tabs);
    QVERIFY(!tabs->isHidden());                       // the row is up while the list is
    auto *cardsButton = view.findChild<QAbstractButton *>(QStringLiteral("boardPageTabCards"));
    auto *skillsButton = view.findChild<QAbstractButton *>(QStringLiteral("boardPageTabSkills"));
    QVERIFY(cardsButton && skillsButton);
    QVERIFY(cardsButton->isChecked());                // Cards is the tab the pane opens on
    QVERIFY(!view.findChild<QWidget *>(QStringLiteral("boardSkillsPage"))->isVisibleTo(&view));
    QVERIFY(view.findChild<QWidget *>(QStringLiteral("boardMemoriesPage"))->isHidden());

    // One project skill, one global one, one stale project one (decision 2 filters here).
    QJsonObject fresh{{"id", "zz-project-skill"}, {"name", "zz-project-skill"},
        {"source", "project-relay"}, {"project", true},
        {"path", QStringLiteral("/home/t/.relay/skills/zz-project-skill/SKILL.md")},
        {"version", "sha256:abc123"}, {"version_short", "abc123"},
        {"description", "a project skill"},
        {"profile", QJsonObject{{"artifact", "decision"}, {"primary", "person"},
                                {"human", "required"}, {"effort", "medium"},
                                {"stakes", "rework"}}},
        {"stats", QJsonObject{{"cases", 2}, {"pass_rate_30", 0.5},
                              {"last_served", "2026-09-20"}, {"stale", false}}},
        {"last_verified", "2026-09-20"}, {"cases", QJsonArray{}},
        {"cards", QJsonArray{QStringLiteral("K7Q2")}}, {"changelog", QJsonArray{}},
        {"profile_warnings", QJsonArray{}}, {"excluded", false}};
    QJsonObject staleSkill = fresh;
    staleSkill["id"] = QStringLiteral("zz-stale-skill");
    staleSkill["name"] = QStringLiteral("zz-stale-skill");
    staleSkill["description"] = QStringLiteral("a stale skill");
    staleSkill["stats"] = QJsonObject{{"cases", 4}, {"pass_rate_30", QJsonValue()},
                                      {"last_served", "2026-08-01"}, {"stale", true}};
    staleSkill["last_verified"] = QStringLiteral("2026-08-01");
    staleSkill["stale_reason"] = QStringLiteral("last passed 2026-08-01");
    QJsonObject globalSkill = fresh;
    globalSkill["id"] = QStringLiteral("zz-global-skill");
    globalSkill["name"] = QStringLiteral("zz-global-skill");
    globalSkill["source"] = QStringLiteral("bundled");
    globalSkill["project"] = false;
    globalSkill["description"] = QStringLiteral("a bundled skill");
    QJsonArray items{fresh, staleSkill, globalSkill};

    skillsButton->click();
    QVERIFY(skillsButton->isChecked());
    QVERIFY(view.findChild<QWidget *>(QStringLiteral("boardSkillsPage"))->isVisibleTo(&view));
    auto *count = view.findChild<QLabel *>(QStringLiteral("boardSkillCount"));
    QCOMPARE(count->text(), QStringLiteral("Loading…"));   // asked, unanswered
    view.handleEvent(QJsonObject{{"event", "skills_registry"}, {"items", items}});
    auto *list = view.findChild<QTreeWidget *>(QStringLiteral("boardSkillList"));
    QVERIFY(list);
    QCOMPARE(count->text(), QStringLiteral("2 skills · 0 excluded"));   // global filtered out
    QCOMPARE(list->topLevelItemCount(), 2);
    QVERIFY(list->topLevelItem(0)->text(0).startsWith(QStringLiteral("zz-project-skill")));
    QVERIFY(list->topLevelItem(0)->text(5).isEmpty());                 // fresh: Stale empty
    int staleRow = -1;
    for (int i = 0; i < list->topLevelItemCount(); ++i)
        if (list->topLevelItem(i)->text(0).startsWith(QStringLiteral("zz-stale-skill")))
            staleRow = i;
    QVERIFY(staleRow >= 0);
    QCOMPARE(list->topLevelItem(staleRow)->text(5), QStringLiteral("stale"));
    QCOMPARE(list->topLevelItem(staleRow)->text(3), QStringLiteral("—"));  // null pass rate

    // The page reads the same row: version, profile strip words, staleness with its reason.
    auto *title = view.findChild<QLabel *>(QStringLiteral("boardSkillTitle"));
    auto *profile = view.findChild<QLabel *>(QStringLiteral("boardSkillProfile"));
    auto *stats = view.findChild<QLabel *>(QStringLiteral("boardSkillStats"));
    QVERIFY(title && profile && stats);
    QVERIFY(title->text().contains(QStringLiteral("zz-project-skill")));
    QVERIFY(title->text().contains(QStringLiteral("abc123")));
    QVERIFY(profile->text().contains(QStringLiteral("artifact decision")));
    QVERIFY(profile->text().contains(QStringLiteral("human required")));
    QVERIFY(stats->text().contains(QStringLiteral("pass 50%")));
    QVERIFY(stats->text().contains(QStringLiteral("last verified 2026-09-20")));
    QVERIFY(!stats->text().contains(QStringLiteral("stale")));
    // The Linked panel (#EA37 (c)): the cards the registry names, as chips.
    auto *linked = view.findChild<QWidget *>(QStringLiteral("boardSkillLinked"));
    QVERIFY(linked);
    QVERIFY(linked->findChild<QPushButton *>() != nullptr);
    if (!shotDir.isEmpty()) {
        QTest::qWait(50);   // the layout settles before the grab
        QVERIFY(view.grab().save(shotDir + QStringLiteral("/board-skills-tab.png")));
    }

    // The stale skill's page says stale and why.
    list->topLevelItem(staleRow)->setSelected(true);
    list->setCurrentItem(list->topLevelItem(staleRow));
    QVERIFY(stats->text().contains(QStringLiteral("<b>stale</b>")));
    QVERIFY(stats->text().contains(QStringLiteral("last passed 2026-08-01")));
    if (!shotDir.isEmpty()) {
        QTest::qWait(50);   // the layout settles before the grab
        QVERIFY(view.grab().save(shotDir + QStringLiteral("/board-skill-page-stale.png")));
    }

    // The choice rides navigation state: a restored pane lands back on Skills.
    QCOMPARE(view.navigationState().value(QStringLiteral("page")).toInt(), 1);
    relay::BoardView restored(QStringLiteral("/tmp/relay-skills-tab-test"));
    restored.handleEvent(opened({row(QStringLiteral("K7Q2"), QStringLiteral("inbox"))}));
    restored.restoreNavigation(view.navigationState());
    QVERIFY(view.findChild<QAbstractButton *>(QStringLiteral("boardPageTabSkills"))->isChecked());
}

// The Memories tab (#9FX8 step 2): Expired first (#EA37 (b)), with the paths-moved date in the
// Reviewed column, then Active; Retired folds; a row opens the ordinary card page.
void BoardPaneTests::theMemoriesTabShowsExpiredFirstAndOpensTheCard()
{
    relay::BoardView view(QStringLiteral("/tmp/relay-memories-tab-test"));
    const QString shotDir = qEnvironmentVariable("RELAY_SHOT_DIR");   // #9FX8 evidence
    if (!shotDir.isEmpty()) {
        view.resize(1000, 600);
        view.show();
    }
    const QJsonObject expiredMemory{{"id", "TM1A"}, {"title", "expired memory"},
        {"type", "memory"}, {"status", "active"}, {"rank", "i"},
        {"path", "issues/memory/tm1a-expired-memory.md"}, {"name", "expired memory"},
        {"reviewed", "2026-08-01"}, {"paths", QJsonArray{QStringLiteral("docs/topic.md")}},
        {"paths_last_commit", "2026-09-20"}, {"expired", true}};
    const QJsonObject freshMemory{{"id", "TM1B"}, {"title", "fresh memory"},
        {"type", "memory"}, {"status", "active"}, {"rank", "i"},
        {"path", "issues/memory/tm1b-fresh-memory.md"}, {"name", "fresh memory"},
        {"reviewed", "2026-09-21"}, {"paths", QJsonArray{QStringLiteral("docs/topic.md")}},
        {"paths_last_commit", "2026-09-20"}, {"expired", false}};
    const QJsonObject retiredMemory{{"id", "TM1C"}, {"title", "retired memory"},
        {"type", "memory"}, {"status", "retired"}, {"rank", "i"},
        {"path", "issues/memory/archive/tm1c-retired-memory.md"}, {"name", "retired memory"},
        {"reviewed", "2026-08-01"}, {"paths", QJsonArray{QStringLiteral("docs/old.md")}},
        {"paths_last_commit", "2026-09-20"}, {"expired", false}};
    view.handleEvent(opened({row(QStringLiteral("K7Q2"), QStringLiteral("inbox")),
                             expiredMemory, freshMemory, retiredMemory}));
    auto *memoriesButton = view.findChild<QAbstractButton *>(QStringLiteral("boardPageTabMemories"));
    QVERIFY(memoriesButton);
    memoriesButton->click();
    QVERIFY(memoriesButton->isChecked());
    QVERIFY(view.findChild<QWidget *>(QStringLiteral("boardMemoriesPage"))->isVisibleTo(&view));
    QVERIFY(view.findChild<QWidget *>(QStringLiteral("boardSkillsPage"))->isHidden());
    auto *list = view.findChild<QTreeWidget *>(QStringLiteral("boardMemoryList"));
    QVERIFY(list);
    QCOMPARE(list->topLevelItemCount(), 3);           // Expired, Active, Retired (no suggestions)
    QCOMPARE(list->topLevelItem(0)->text(0), QStringLiteral("Expired (1)"));
    QCOMPARE(list->topLevelItem(1)->text(0), QStringLiteral("Active (1)"));
    QCOMPARE(list->topLevelItem(2)->text(0), QStringLiteral("Retired (1)"));
    QVERIFY(!list->topLevelItem(2)->isExpanded());    // retired folds
    QCOMPARE(list->topLevelItem(0)->childCount(), 1);
    QVERIFY(list->topLevelItem(0)->child(0)->text(1)
                .contains(QStringLiteral("paths moved 2026-09-20")));
    auto *count = view.findChild<QLabel *>(QStringLiteral("boardMemoryCount"));
    QVERIFY(count->text().contains(QStringLiteral("1 expired")));
    if (!shotDir.isEmpty()) {
        QTest::qWait(50);   // the layout settles before the grab
        QVERIFY(view.grab().save(shotDir + QStringLiteral("/board-memories-tab.png")));
    }

    // Opening a memory goes to the ordinary card page, solo — a memory is a card, `#ID` and all.
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    auto *expiredItem = list->topLevelItem(0)->child(0);
    list->setCurrentItem(expiredItem);
    auto *reverify = view.findChild<QAbstractButton *>(QStringLiteral("boardMemoryReverify"));
    auto *retire = view.findChild<QAbstractButton *>(QStringLiteral("boardMemoryRetire"));
    QVERIFY(reverify && retire);
    QVERIFY(reverify->isEnabled());                    // an expired row re-verifies
    QVERIFY(retire->isEnabled());
    reverify->click();
    QCOMPARE(sent.size(), 0);                          // a draft, never a send
    // Retire goes through the worker as a status move to the archive.
    retire->click();
    QTRY_VERIFY(!sent.isEmpty()
                && sent.last().value(QStringLiteral("type")).toString() == QStringLiteral("board_move_card")
                && sent.last().value(QStringLiteral("status")).toString() == QStringLiteral("retired"));
}

// The Live strip (#TBRH, PROJECT-BOARD-DESIGN §6): computed from the window's panes and each
// card's `session`, one chip per pane and one per open card it holds; hidden with no pane, and
// never on Skills, Memories or a pinned card pane.
void BoardPaneTests::theLiveStripListsThisProjectsPanesAndTheirCards()
{
    relay::BoardView view(QStringLiteral("/tmp/relay-live-strip-test"));
    view.resize(1000, 700);
    view.show();
    const QString busyToken = QStringLiteral("c522363d-fa8e-4db1-afcd-a6e58451cd14");
    const QString idleToken = QStringLiteral("af0737e5-0000-4000-8000-000000000000");
    QJsonObject held = row(QStringLiteral("K7Q2"), QStringLiteral("in-progress"));
    held.insert(QStringLiteral("title"), QStringLiteral("Live strip on Cards"));
    held.insert(QStringLiteral("session"), busyToken);
    QJsonObject closed = row(QStringLiteral("D0N3"), QStringLiteral("done"));
    closed.insert(QStringLiteral("session"), busyToken);      // a closed card is not "held"
    QJsonObject stale = row(QStringLiteral("G0NE"), QStringLiteral("ready"));
    stale.insert(QStringLiteral("session"), QStringLiteral("deadbeef-gone"));   // no such pane

    // No window callback, and then a window with no pane here: no strip.
    view.handleEvent(opened({held, closed, stale}));
    auto *strip = view.findChild<QWidget *>(QStringLiteral("boardLiveStrip"));
    QVERIFY(strip);
    QVERIFY(strip->isHidden());
    QJsonArray panes;
    view.livePanes = [&panes] { return panes; };
    view.handleEvent(opened({held, closed, stale}));
    QVERIFY(strip->isHidden());

    // Two panes: one working on K7Q2, one idle holding nothing.
    panes = QJsonArray{
        QJsonObject{{"token", busyToken}, {"title", "relay-terminal"}, {"model", "claude-opus-5-5"},
                    {"busy", true}},
        QJsonObject{{"token", idleToken}, {"title", "shell"}, {"model", ""}, {"busy", false}}};
    view.handleEvent(opened({held, closed, stale}));
    QVERIFY(strip->isVisibleTo(&view));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);   // the empty draws' leftovers
    const QList<QPushButton *> chips =
        strip->findChildren<QPushButton *>(QStringLiteral("boardLivePaneChip"));
    QCOMPARE(chips.size(), 2);
    QCOMPARE(chips.at(0)->text(), QStringLiteral("⧉ c522363d · claude-opus-5-5 ✦"));
    QCOMPARE(chips.at(1)->text(), QStringLiteral("⧉ af0737e5"));
    const QList<QPushButton *> cardChips =
        strip->findChildren<QPushButton *>(QStringLiteral("boardLiveCardChip"));
    QCOMPARE(cardChips.size(), 1);                       // K7Q2 only: not the done card, not G0NE
    QCOMPARE(cardChips.at(0)->property("card").toString(), QStringLiteral("K7Q2"));
    QCOMPARE(cardChips.at(0)->parentWidget(), chips.at(0)->parentWidget());   // grouped with its pane
    if (qEnvironmentVariableIsSet("RELAY_QA_SCREENSHOT")) {
        QTest::qWait(100);
        QVERIFY(view.grab().save(qEnvironmentVariable("RELAY_QA_SCREENSHOT")));
    }

    // The pane chip reveals the pane; the card chip opens the card through the worker.
    QString focused;
    view.onFocusPane = [&focused](const QString &token) { focused = token; };
    chips.at(0)->click();
    QCOMPARE(focused, busyToken);
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    cardChips.at(0)->click();
    QTRY_VERIFY(std::any_of(sent.cbegin(), sent.cend(), [](const QJsonObject &message) {
        return message.value(QStringLiteral("type")).toString() == QStringLiteral("board_card_get")
               && message.value(QStringLiteral("card")).toString() == QStringLiteral("K7Q2");
    }));

    // Not on Skills or Memories; back on Cards it returns.
    view.findChild<QAbstractButton *>(QStringLiteral("boardPageTabSkills"))->click();
    QVERIFY(strip->isHidden());
    view.findChild<QAbstractButton *>(QStringLiteral("boardPageTabMemories"))->click();
    QVERIFY(strip->isHidden());
    view.findChild<QAbstractButton *>(QStringLiteral("boardPageTabCards"))->click();
    QVERIFY(strip->isVisibleTo(&view));

    // The last pane closes: the strip goes on the next refresh. Nothing was written anywhere.
    panes = QJsonArray{};
    view.handleEvent(opened({held, closed, stale}));
    QVERIFY(strip->isHidden());
    for (const QJsonObject &message : std::as_const(sent))
        QVERIFY(!message.value(QStringLiteral("type")).toString().startsWith(QStringLiteral("board_update")));

    // A pinned card pane (#Y2BA) never shows it.
    panes = QJsonArray{QJsonObject{{"token", busyToken}, {"model", "m"}, {"busy", false}}};
    relay::BoardView pinned(QStringLiteral("/tmp/relay-live-strip-test"));
    pinned.livePanes = [&panes] { return panes; };
    pinned.show();
    pinned.handleEvent(opened({held}));
    pinned.pinSolo(QStringLiteral("K7Q2"));
    pinned.handleEvent(opened({held}));
    QVERIFY(pinned.findChild<QWidget *>(QStringLiteral("boardLiveStrip"))->isHidden());
}

// The card page and the skill page each ask `board_links` about what they show (#EE42) and
// draw its reverse side: a card's "Linked from" block names only what its own front matter and
// `reverse` block did not, and the skill page's chips gain the cards whose `server:` or prose
// names the skill. An answer to a question the page has moved past is dropped.
void BoardPaneTests::theLinkedPanelsDrawTheReverseSideFromBoardLinks()
{
    relay::BoardView view(QStringLiteral("/tmp/relay-board-backlinks-test"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row(QStringLiteral("K7Q2"), QStringLiteral("ready")),
                             row(QStringLiteral("M3XJ"), QStringLiteral("ready")),
                             row(QStringLiteral("P4ZZ"), QStringLiteral("ready"))}));
    const auto lastOfType = [&sent](const char *type) {
        for (auto it = sent.crbegin(); it != sent.crend(); ++it)
            if (it->value(QStringLiteral("type")).toString() == QLatin1String(type))
                return *it;
        return QJsonObject();
    };
    view.openCard(QStringLiteral("K7Q2"));
    QJsonObject answer = cardArrived(QStringLiteral("K7Q2"));
    answer.insert(QStringLiteral("id"), lastOfType("board_card_get").value(QStringLiteral("id")));
    answer.insert(QStringLiteral("reverse"), QJsonObject{{"blocks", QJsonArray{QJsonObject{
        {"id", "M3XJ"}, {"title", "Blocked"}}}}});
    view.handleEvent(answer);
    const QJsonObject ask = lastOfType("board_links");
    QCOMPARE(ask.value(QStringLiteral("address")).toString(), QStringLiteral("#K7Q2"));
    auto *meta = view.findChild<QLabel *>(QStringLiteral("boardCardMeta"));
    QVERIFY(meta);
    QVERIFY(!meta->text().contains(QStringLiteral("Linked from")));   // nothing until it answers

    const QJsonArray reverse{
        QJsonObject{{"name", "mentioned_in"}, {"from", "#P4ZZ"}, {"relation", "mention"}},
        QJsonObject{{"name", "mentioned_in"}, {"from", "#M3XJ"}, {"relation", "mention"}},
        QJsonObject{{"name", "superseded_by"}, {"from", "#Q9RR"}, {"relation", "supersedes"}},
        QJsonObject{{"name", "cases"}, {"from", "case:c-1"}, {"relation", "card"}}};
    // A stale answer (another id) is not believed.
    view.handleEvent(QJsonObject{{"event", "board_links"},
        {"id", ask.value(QStringLiteral("id")).toString() + QStringLiteral("x")},
        {"items", QJsonArray{QJsonObject{{"address", "#K7Q2"}, {"reverse", reverse}}}}});
    QVERIFY(!meta->text().contains(QStringLiteral("Linked from")));
    view.handleEvent(QJsonObject{{"event", "board_links"}, {"id", ask.value(QStringLiteral("id"))},
        {"items", QJsonArray{QJsonObject{{"address", "#K7Q2"}, {"reverse", reverse}}}}});
    const QString text = meta->text();
    QVERIFY(text.contains(QStringLiteral("Linked from")));
    QVERIFY(text.contains(QStringLiteral("mentioned in <a href=\"card:P4ZZ\">#P4ZZ</a>")));
    QVERIFY(text.contains(QStringLiteral("superseded by <a href=\"card:Q9RR\">#Q9RR</a>")));
    QVERIFY(text.contains(QStringLiteral("1 case row")));
    QCOMPARE(text.count(QStringLiteral("\"card:M3XJ\"")), 1);   // blocks already named it

    // The skill page: the registry's ledger card, then the cards `board_links` names.
    view.closeDetail();
    view.findChild<QAbstractButton *>(QStringLiteral("boardPageTabSkills"))->click();
    QJsonObject skill{{"id", "zz-project-skill"}, {"name", "zz-project-skill"},
        {"source", "project-relay"}, {"project", true},
        {"path", QStringLiteral("/home/t/.relay/skills/zz-project-skill/SKILL.md")},
        {"version", "sha256:abc123"}, {"version_short", "abc123"}, {"description", "a skill"},
        {"profile", QJsonObject{}}, {"stats", QJsonObject{{"cases", 1}, {"stale", false}}},
        {"cases", QJsonArray{}}, {"cards", QJsonArray{QStringLiteral("K7Q2")}},
        {"changelog", QJsonArray{}}, {"profile_warnings", QJsonArray{}}, {"excluded", false}};
    view.handleEvent(QJsonObject{{"event", "skills_registry"}, {"items", QJsonArray{skill}}});
    const QJsonObject skillAsk = lastOfType("board_links");
    QCOMPARE(skillAsk.value(QStringLiteral("address")).toString(),
             QStringLiteral("skill:zz-project-skill"));
    auto *linked = view.findChild<QWidget *>(QStringLiteral("boardSkillLinked"));
    QVERIFY(linked);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);   // replaced chip rows
    QCOMPARE(linked->findChildren<QPushButton *>(QStringLiteral("boardLinkedChip")).size(), 1);
    view.handleEvent(QJsonObject{{"event", "board_links"}, {"id", skillAsk.value(QStringLiteral("id"))},
        {"items", QJsonArray{QJsonObject{{"address", "skill:zz-project-skill"}, {"reverse", QJsonArray{
            QJsonObject{{"name", "built_by"}, {"from", "#P4ZZ"}, {"relation", "server"}},
            QJsonObject{{"name", "mentioned_in"}, {"from", "#K7Q2"}, {"relation", "mention"}},
            QJsonObject{{"name", "cases"}, {"from", "case:c-1"}, {"relation", "server"}}}}}}}});
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);   // the old chip row
    QStringList chips;
    for (QPushButton *chip : linked->findChildren<QPushButton *>(QStringLiteral("boardLinkedChip")))
        chips << chip->text();
    QCOMPARE(chips.size(), 2);                                  // K7Q2 once, then P4ZZ
    QVERIFY(chips.at(0).startsWith(QStringLiteral("#K7Q2")));
    QVERIFY(chips.at(1).startsWith(QStringLiteral("#P4ZZ")));
}
