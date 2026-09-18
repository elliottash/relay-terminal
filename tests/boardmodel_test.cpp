// SPDX-License-Identifier: GPL-3.0-or-later
// The Switchboard pane's pure logic: which tab and column a card falls into, the filter
// language, the ordering and the `#` picker's ranking. No worker, no files, no network.
#include "BoardModel.h"
#include "BoardPane.h"

#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTimeZone>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using relay::board::Card;
using relay::board::Column;
using relay::board::Model;
using relay::board::Tab;

namespace {

QJsonObject config()
{
    const auto json = QByteArrayLiteral(R"({
      "columns": ["inbox", "discussing", "ready", "in-progress", "waiting", "needs-qa", "done"],
      "column_statuses": {"inbox": ["inbox"], "discussing": ["discussing"], "ready": ["ready"],
        "in-progress": ["in-progress"],
        "waiting": ["needs-review", "needs-labels", "needs-ab"],
        "needs-qa": ["needs-qa-llm", "needs-qa-human"], "done": ["done", "dropped"]},
      "tabs": [{"id": "features", "folder": "features"}, {"id": "bugs", "folder": "changes"},
               {"id": "planning", "folder": "planning"},
               {"id": "deferred", "filter": "status:deferred"},
               {"id": "done", "filter": "status:done,dropped"}],
      "autonomy": "auto"
    })");
    return QJsonDocument::fromJson(json).object();
}

QJsonObject row(const QString &id, const QString &status, const QString &tab,
                const QString &rank = QStringLiteral("i"))
{
    return QJsonObject{{"id", id}, {"title", id + QStringLiteral(" card")}, {"type", "work"},
                       {"status", status}, {"tab", tab}, {"rank", rank},
                       {"path", QStringLiteral("issues/") + tab + QLatin1Char('/') + id + ".md"}};
}

QJsonArray rows(const QList<QJsonObject> &items)
{
    QJsonArray out;
    for (const QJsonObject &item : items)
        out << item;
    return out;
}

}  // namespace

class BoardModelTests : public QObject {
    Q_OBJECT

private slots:
    void tabsComeFromTheConfigAndMemoryIsAlwaysThere();
    void columnsFollowTheConfiguredStatuses();
    void cardsLandInTheColumnOfTheirStatus();
    void deferredAndDoneHaveTheirOwnTabs();
    void plansAndMemoriesAreNotWorkCards();
    void memoryIsGroupedByTopic();
    void rankOrdersAColumnAndDoneIsNewestFirst();
    void theFilterLanguageMatchesEveryTerm();
    void theFilterHidesCardsAndTheCountsFollow();
    void searchRanksOpenCardsAndExactIdsFirst();
    void upsertAndRemoveKeepTheBoardInStep();
    void statusTitlesAreHumanReadable();
    void theViewRendersTabsAndColumnsFromAnEvent();
    void theViewSendsAMoveWhenACardIsDropped();
    void badgesSayWhatTheCardCarries();
    void theBodyLosesOnlyAHeadingThatRepeatsTheTitle();
    void threadEntriesSayHowLongAgo();
    void placementNamesTheNeighboursOfTheSlot();
    void aRefusedWriteIsShownAndAnAcceptedOneCanBeUndone();
    void aChangeRefillsTheColumnsInPlace();
    void theOpenCardRefetchesOnlyForItsOwnChanges();
    void aQuestionTheAgentCannotTakeIsReportedOnTheCard();
};

void BoardModelTests::tabsComeFromTheConfigAndMemoryIsAlwaysThere()
{
    Model model;
    model.setConfig(config());
    QStringList ids;
    for (const Tab &tab : model.tabs())
        ids << tab.id;
    QCOMPARE(ids, (QStringList{"features", "bugs", "planning", "deferred", "done", "memory"}));
    QCOMPARE(model.tab(QStringLiteral("planning"))->type, QStringLiteral("plan"));
    QCOMPARE(model.tab(QStringLiteral("planning"))->title, QStringLiteral("Plans"));
    QCOMPARE(model.tab(QStringLiteral("memory"))->type, QStringLiteral("memory"));
    QVERIFY(model.tab(QStringLiteral("deferred"))->isFilter());
    QVERIFY(!model.tab(QStringLiteral("features"))->isFilter());
}

void BoardModelTests::columnsFollowTheConfiguredStatuses()
{
    Model model;
    model.setConfig(config());
    const QList<Column> columns = model.columnsFor(QStringLiteral("features"));
    QCOMPARE(columns.size(), 7);
    QCOMPARE(columns.first().id, QStringLiteral("inbox"));
    QCOMPARE(columns.at(4).statuses,
             (QStringList{"needs-review", "needs-labels", "needs-ab"}));
    QCOMPARE(model.dropStatus(QStringLiteral("features"), QStringLiteral("needs-qa")),
             QStringLiteral("needs-qa-llm"));
    QCOMPARE(model.dropStatus(QStringLiteral("features"), QStringLiteral("nope")), QString());
}

void BoardModelTests::cardsLandInTheColumnOfTheirStatus()
{
    Model model;
    model.setConfig(config());
    model.reset(rows({row("K7Q2", "inbox", "features"), row("M3XJ", "needs-qa-llm", "features"),
                      row("P9AB", "needs-review", "features"), row("ZZ11", "ready", "bugs")}));
    QCOMPARE(model.cards(QStringLiteral("features"), QStringLiteral("inbox")).size(), 1);
    QCOMPARE(model.cards(QStringLiteral("features"), QStringLiteral("needs-qa")).first().id,
             QStringLiteral("M3XJ"));
    QCOMPARE(model.cards(QStringLiteral("features"), QStringLiteral("waiting")).first().id,
             QStringLiteral("P9AB"));
    QCOMPARE(model.cards(QStringLiteral("bugs"), QStringLiteral("ready")).size(), 1);
    QCOMPARE(model.cards(QStringLiteral("features"), QStringLiteral("ready")).size(), 0);
    QCOMPARE(model.count(QStringLiteral("features")), 3);
    QCOMPARE(model.count(QStringLiteral("bugs")), 1);
}

void BoardModelTests::deferredAndDoneHaveTheirOwnTabs()
{
    Model model;
    model.setConfig(config());
    model.reset(rows({row("K7Q2", "deferred", "features"), row("M3XJ", "done", "bugs"),
                      row("P9AB", "dropped", "features"), row("R4CD", "ready", "features")}));
    // A deferred or closed card leaves its category tab.
    QCOMPARE(model.count(QStringLiteral("features")), 1);
    QCOMPARE(model.cards(QStringLiteral("features"), QStringLiteral("ready")).first().id,
             QStringLiteral("R4CD"));
    // Deferred groups by category; Done is one column.
    QCOMPARE(model.count(QStringLiteral("deferred")), 1);
    QCOMPARE(model.cards(QStringLiteral("deferred"), QStringLiteral("features")).first().id,
             QStringLiteral("K7Q2"));
    QCOMPARE(model.columnsFor(QStringLiteral("done")).size(), 1);
    QCOMPARE(model.cards(QStringLiteral("done"), QStringLiteral("done")).size(), 2);
}

void BoardModelTests::plansAndMemoriesAreNotWorkCards()
{
    Model model;
    model.setConfig(config());
    QJsonObject plan = row("PL01", "approved", "planning");
    plan.insert(QStringLiteral("type"), QStringLiteral("plan"));
    QJsonObject memory = row("ME01", "active", "memory");
    memory.insert(QStringLiteral("type"), QStringLiteral("memory"));
    memory.insert(QStringLiteral("topic"), QStringLiteral("conventions"));
    model.reset(rows({row("K7Q2", "ready", "features"), plan, memory}));

    // A plan never appears on a work board, and a work card never appears among the plans.
    QCOMPARE(model.count(QStringLiteral("features")), 1);
    QCOMPARE(model.count(QStringLiteral("planning")), 1);
    QStringList planColumns;
    for (const Column &column : model.columnsFor(QStringLiteral("planning")))
        planColumns << column.id;
    QCOMPARE(planColumns, (QStringList{"draft", "approved", "executing", "done"}));
    QCOMPARE(model.cards(QStringLiteral("planning"), QStringLiteral("approved")).first().id,
             QStringLiteral("PL01"));
    QCOMPARE(model.cards(QStringLiteral("planning"), QStringLiteral("draft")).size(), 0);
    QCOMPARE(model.count(QStringLiteral("memory")), 1);
    QCOMPARE(model.columnOf(QStringLiteral("features"), Card::fromJson(plan)), QString());
}

void BoardModelTests::memoryIsGroupedByTopic()
{
    Model model;
    model.setConfig(config());
    QJsonObject one = row("ME01", "active", "memory");
    one.insert(QStringLiteral("type"), QStringLiteral("memory"));
    one.insert(QStringLiteral("topic"), QStringLiteral("conventions"));
    QJsonObject two = row("ME02", "active", "memory");
    two.insert(QStringLiteral("type"), QStringLiteral("memory"));
    two.insert(QStringLiteral("topic"), QStringLiteral("environment"));
    QJsonObject old = row("ME03", "retired", "memory");
    old.insert(QStringLiteral("type"), QStringLiteral("memory"));
    model.reset(rows({one, two, old}));
    QStringList columns;
    for (const Column &column : model.columnsFor(QStringLiteral("memory")))
        columns << column.id;
    QCOMPARE(columns, (QStringList{"topic:conventions", "topic:environment", "retired"}));
    QCOMPARE(model.cards(QStringLiteral("memory"), QStringLiteral("topic:environment")).first().id,
             QStringLiteral("ME02"));
    QCOMPARE(model.cards(QStringLiteral("memory"), QStringLiteral("retired")).first().id,
             QStringLiteral("ME03"));
}

void BoardModelTests::rankOrdersAColumnAndDoneIsNewestFirst()
{
    Model model;
    model.setConfig(config());
    model.reset(rows({row("AAA1", "ready", "features", "z"), row("BBB2", "ready", "features", "a"),
                      row("CCC3", "ready", "features", "m")}));
    QStringList order;
    for (const Card &card : model.cards(QStringLiteral("features"), QStringLiteral("ready")))
        order << card.id;
    QCOMPARE(order, (QStringList{"BBB2", "CCC3", "AAA1"}));

    QJsonObject older = row("OLD1", "done", "features", "a");
    older.insert(QStringLiteral("created"), QStringLiteral("2026-01-01"));
    QJsonObject newer = row("NEW1", "done", "features", "z");
    newer.insert(QStringLiteral("created"), QStringLiteral("2026-09-17"));
    model.reset(rows({older, newer}));
    QStringList closed;
    for (const Card &card : model.cards(QStringLiteral("done"), QStringLiteral("done")))
        closed << card.id;
    QCOMPARE(closed, (QStringList{"NEW1", "OLD1"}));
}

void BoardModelTests::theFilterLanguageMatchesEveryTerm()
{
    Card card;
    card.id = QStringLiteral("K7Q2");
    card.title = QStringLiteral("Voice transcription mode");
    card.status = QStringLiteral("ready");
    card.labels = QStringList{QStringLiteral("voice"), QStringLiteral("mvp")};
    card.assignee = QStringLiteral("agent");
    card.waitingOn = QStringLiteral("owner");

    QVERIFY(Model::matches(card, QString()));
    QVERIFY(Model::matches(card, QStringLiteral("  ")));
    QVERIFY(Model::matches(card, QStringLiteral("label:voice")));
    QVERIFY(Model::matches(card, QStringLiteral("label:VOICE")));
    QVERIFY(!Model::matches(card, QStringLiteral("label:audio")));
    QVERIFY(Model::matches(card, QStringLiteral("status:ready")));
    QVERIFY(!Model::matches(card, QStringLiteral("status:inbox")));
    QVERIFY(Model::matches(card, QStringLiteral("@agent")));
    QVERIFY(!Model::matches(card, QStringLiteral("@dana")));
    QVERIFY(Model::matches(card, QStringLiteral("waiting:me")));
    QVERIFY(Model::matches(card, QStringLiteral("waiting:owner")));
    QVERIFY(!Model::matches(card, QStringLiteral("waiting:dana")));
    QVERIFY(Model::matches(card, QStringLiteral("#K7Q2")));
    QVERIFY(Model::matches(card, QStringLiteral("transcription")));
    QVERIFY(!Model::matches(card, QStringLiteral("clickable")));
    // Every term has to match, not just one.
    QVERIFY(Model::matches(card, QStringLiteral("label:voice @agent transcription")));
    QVERIFY(!Model::matches(card, QStringLiteral("label:voice @dana")));
}

void BoardModelTests::theFilterHidesCardsAndTheCountsFollow()
{
    Model model;
    model.setConfig(config());
    QJsonObject tagged = row("K7Q2", "ready", "features");
    tagged.insert(QStringLiteral("labels"), QJsonArray{QStringLiteral("voice")});
    model.reset(rows({tagged, row("M3XJ", "ready", "features")}));
    QCOMPARE(model.count(QStringLiteral("features")), 2);
    model.setFilter(QStringLiteral("label:voice"));
    QCOMPARE(model.count(QStringLiteral("features")), 1);
    QCOMPARE(model.cards(QStringLiteral("features"), QStringLiteral("ready")).size(), 1);
    model.setFilter(QString());
    QCOMPARE(model.count(QStringLiteral("features")), 2);
}

void BoardModelTests::searchRanksOpenCardsAndExactIdsFirst()
{
    Model model;
    model.setConfig(config());
    QJsonObject voice = row("K7Q2", "ready", "features");
    voice.insert(QStringLiteral("title"), QStringLiteral("Voice transcription mode"));
    QJsonObject closed = row("M3XJ", "done", "features");
    closed.insert(QStringLiteral("title"), QStringLiteral("Voice mode groundwork"));
    QJsonObject other = row("P9AB", "ready", "features");
    other.insert(QStringLiteral("title"), QStringLiteral("Clickable paths"));
    model.reset(rows({voice, closed, other}));

    QStringList found;
    for (const Card &card : model.search(QStringLiteral("voice")))
        found << card.id;
    QCOMPARE(found, (QStringList{"K7Q2", "M3XJ"}));       // open first, unrelated card dropped
    QCOMPARE(model.search(QStringLiteral("M3XJ")).first().id, QStringLiteral("M3XJ"));
    QCOMPARE(model.search(QString()).size(), 3);          // no query: everything, open first
    QCOMPARE(model.search(QStringLiteral("voice"), 1).size(), 1);
    QVERIFY(model.search(QStringLiteral("zzzzz")).isEmpty());
}

void BoardModelTests::upsertAndRemoveKeepTheBoardInStep()
{
    Model model;
    model.setConfig(config());
    model.reset(rows({row("K7Q2", "inbox", "features")}));
    QCOMPARE(model.total(), 1);
    model.upsert(rows({row("K7Q2", "ready", "features"), row("M3XJ", "inbox", "features")}));
    QCOMPARE(model.total(), 2);
    QCOMPARE(model.card(QStringLiteral("K7Q2"))->status, QStringLiteral("ready"));
    QCOMPARE(model.card(QStringLiteral("k7q2"))->id, QStringLiteral("K7Q2"));
    model.remove({QStringLiteral("K7Q2")});
    QCOMPARE(model.total(), 1);
    QVERIFY(!model.card(QStringLiteral("K7Q2")));
    model.clear();
    QCOMPARE(model.total(), 0);
}

void BoardModelTests::statusTitlesAreHumanReadable()
{
    QCOMPARE(relay::board::statusTitle(QStringLiteral("in-progress")), QStringLiteral("In progress"));
    QCOMPARE(relay::board::statusTitle(QStringLiteral("needs-qa-llm")), QStringLiteral("Needs QA (LLM)"));
    QCOMPARE(relay::board::statusTitle(QStringLiteral("brand-new")), QStringLiteral("Brand new"));
    QCOMPARE(relay::board::tabTitle(QStringLiteral("planning")), QStringLiteral("Plans"));
    QCOMPARE(relay::board::tabTitle(QStringLiteral("features")), QStringLiteral("Features"));
}

// ---- the widget, driven by protocol events alone ------------------------------------

void BoardModelTests::theViewRendersTabsAndColumnsFromAnEvent()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QJsonObject opened{{"event", "board"}, {"rev", 1}, {"config", config()},
                       {"cards", rows({row("K7Q2", "inbox", "features"),
                                       row("M3XJ", "ready", "features")})},
                       {"problems", QJsonArray{}}};
    view.handleEvent(opened);
    QCOMPARE(view.currentTab(), QStringLiteral("features"));
    QCOMPARE(view.model().total(), 2);
    QCOMPARE(view.title(), QStringLiteral("Switchboard · 2"));
    QCOMPARE(view.findChildren<QListWidget *>(QStringLiteral("boardColumn")).size(), 7);

    view.handleEvent(QJsonObject{{"event", "board_changed"},
                                 {"upserts", rows({row("K7Q2", "ready", "features")})},
                                 {"removed", QJsonArray{QStringLiteral("M3XJ")}}});
    QCOMPARE(view.model().total(), 1);
    QCOMPARE(view.model().card(QStringLiteral("K7Q2"))->status, QStringLiteral("ready"));
}

void BoardModelTests::theViewSendsAMoveWhenACardIsDropped()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(QJsonObject{{"event", "board"}, {"config", config()},
                                 {"cards", rows({row("K7Q2", "inbox", "features")})}});
    view.selectCard(QStringLiteral("K7Q2"));
    QCOMPARE(view.selectedCard(), QStringLiteral("K7Q2"));

    view.openSelected();
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value(QStringLiteral("type")).toString(), QStringLiteral("board_card_get"));
    QCOMPARE(sent.last().value(QStringLiteral("card")).toString(), QStringLiteral("K7Q2"));

    view.reload();
    QCOMPARE(sent.last().value(QStringLiteral("type")).toString(), QStringLiteral("board_open"));

    view.setCurrentTab(QStringLiteral("bugs"));
    QCOMPARE(view.currentTab(), QStringLiteral("bugs"));
}

// ---- the card face and the detail view's helpers ----------------------------------------

void BoardModelTests::badgesSayWhatTheCardCarries()
{
    Card card = Card::fromJson(row("K7Q2", "needs-qa-human", "features"));
    card.labels = QStringList{QStringLiteral("voice")};
    card.assignee = QStringLiteral("agent");
    card.waitingOn = QStringLiteral("owner");
    card.tasksDone = 3;
    card.tasksTotal = 3;
    card.threadEntries = 4;
    const QList<relay::board::Badge> all = relay::board::badges(card, true);
    QStringList texts;
    for (const auto &badge : all)
        texts << badge.text;
    // The status says only what the column header does not ("Needs QA" → "human QA").
    QCOMPARE(texts, (QStringList{"human QA", "voice", "✦ agent", "waiting: owner", "☑ 3/3", "✎ 4"}));
    QCOMPARE(all.at(4).kind, relay::board::Badge::TasksDone);
    QCOMPARE(all.at(3).kind, relay::board::Badge::Waiting);
    // In a column of one status, and on a plain card, there is nothing to repeat.
    QVERIFY(relay::board::badges(Card::fromJson(row("M3XJ", "ready", "features")), false).isEmpty());
}

void BoardModelTests::theBodyLosesOnlyAHeadingThatRepeatsTheTitle()
{
    using relay::board::bodyWithoutTitle;
    QCOMPARE(bodyWithoutTitle(QStringLiteral("# Voice mode\n\n## Request\nhi\n"), QStringLiteral("Voice mode")),
             QStringLiteral("## Request\nhi\n"));
    QCOMPARE(bodyWithoutTitle(QStringLiteral("\n# voice MODE\ntext"), QStringLiteral("Voice mode")),
             QStringLiteral("text"));
    // A different heading, or text before it, is the author's and stays.
    const QString other = QStringLiteral("# Something else\ntext");
    QCOMPARE(bodyWithoutTitle(other, QStringLiteral("Voice mode")), other);
    const QString later = QStringLiteral("Intro\n# Voice mode\n");
    QCOMPARE(bodyWithoutTitle(later, QStringLiteral("Voice mode")), later);
}

void BoardModelTests::threadEntriesSayHowLongAgo()
{
    using relay::board::entryAge;
    const QDateTime now(QDate(2026, 9, 18), QTime(12, 0, 0), QTimeZone::utc());
    QCOMPARE(entryAge(QStringLiteral("20260918T115950Z-ab"), now), QStringLiteral("just now"));
    QCOMPARE(entryAge(QStringLiteral("20260918T114000Z-ab"), now), QStringLiteral("20 min ago"));
    QVERIFY(entryAge(QStringLiteral("20260916T080000Z-ab"), now).startsWith(QStringLiteral("Sep 1")));
    QVERIFY(entryAge(QStringLiteral("20250101T080000Z-ab"), now).endsWith(QStringLiteral("2025")));
    QVERIFY(entryAge(QStringLiteral("not-a-time"), now).isEmpty());
}

void BoardModelTests::placementNamesTheNeighboursOfTheSlot()
{
    using relay::board::placement;
    const QStringList column{"A", "B", "C"};
    // Moving B to the top: before A, after nothing.
    QCOMPARE(placement(column, "B", 0), qMakePair(QString("A"), QString()));
    // Moving B to the bottom: after C.
    QCOMPARE(placement(column, "B", 2), qMakePair(QString(), QString("C")));
    // A card from another column dropped between A and B.
    QCOMPARE(placement(column, "X", 1), qMakePair(QString("B"), QString("A")));
    // Into an empty column: no neighbours; a slot past the end is the end.
    QCOMPARE(placement({}, "X", 0), qMakePair(QString(), QString()));
    QCOMPARE(placement(column, "X", 99), qMakePair(QString(), QString("C")));
}

// ---- the view's answers to the worker ------------------------------------------------------

namespace {
QJsonObject opened(const QList<QJsonObject> &cards)
{
    return QJsonObject{{"event", "board"}, {"config", config()}, {"cards", rows(cards)},
                       {"problems", QJsonArray{}}};
}
}  // namespace

void BoardModelTests::aRefusedWriteIsShownAndAnAcceptedOneCanBeUndone()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "in-progress", "features")}));
    view.selectCard(QStringLiteral("K7Q2"));
    QListWidget *list = nullptr;
    for (QListWidget *candidate : view.findChildren<QListWidget *>(QStringLiteral("boardColumn")))
        if (candidate->count() == 1)
            list = candidate;
    QVERIFY(list);

    // Alt+Shift+Right: to the next column. The request carries an id this view recognises.
    QTest::keyClick(list, Qt::Key_Right, Qt::AltModifier | Qt::ShiftModifier);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_move"));
    QCOMPARE(sent.last().value("status").toString(), QStringLiteral("needs-review"));
    const QString moveId = sent.last().value("id").toString();
    QVERIFY(!moveId.isEmpty());

    // Refused: the reason is shown instead of nothing happening.
    view.handleEvent(QJsonObject{{"event", "error"}, {"id", moveId}, {"text", "needs evidence"}});
    QCOMPARE(view.notice(), QStringLiteral("needs evidence"));
    // Another pane's error is not this view's business.
    view.handleEvent(QJsonObject{{"event", "error"}, {"id", "elsewhere-1"}, {"text", "other"}});
    QCOMPARE(view.notice(), QStringLiteral("needs evidence"));

    // Accepted: a notice that names the move, and Undo sends board_undo for that write.
    QTest::keyClick(list, Qt::Key_Right, Qt::AltModifier | Qt::ShiftModifier);
    const QString secondId = sent.last().value("id").toString();
    view.handleEvent(QJsonObject{{"event", "board_written"}, {"id", secondId}, {"kind", "board_move"},
                                 {"card_id", "K7Q2"}, {"write_id", "w-42"}});
    QCOMPARE(view.notice(), QStringLiteral("Moved #K7Q2 to Waiting"));
    view.undoLast();
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_undo"));
    QCOMPARE(sent.last().value("write_id").toString(), QStringLiteral("w-42"));
}

void BoardModelTests::aChangeRefillsTheColumnsInPlace()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.handleEvent(opened({row("K7Q2", "inbox", "features"), row("M3XJ", "ready", "features")}));
    const QList<QListWidget *> before = view.findChildren<QListWidget *>(QStringLiteral("boardColumn"));
    QCOMPARE(before.size(), 7);
    QPointer<QListWidget> inbox = before.first();

    // A card moving inside the same tab refills the lists; the widgets (and so their scroll
    // positions, focus and an open quick-add field) survive.
    view.handleEvent(QJsonObject{{"event", "board_changed"}, {"upserts", rows({row("K7Q2", "ready", "features")})},
                                 {"removed", QJsonArray{}}});
    QVERIFY(inbox);
    QCOMPARE(view.findChildren<QListWidget *>(QStringLiteral("boardColumn")).size(), 7);
    int cards = 0;
    for (QListWidget *list : view.findChildren<QListWidget *>(QStringLiteral("boardColumn")))
        cards += list->count();
    QCOMPARE(cards, 2);
    QCOMPARE(inbox->count(), 0);
}

void BoardModelTests::theOpenCardRefetchesOnlyForItsOwnChanges()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features"), row("M3XJ", "ready", "features")}));
    view.handleEvent(QJsonObject{{"event", "board_card"}, {"card_id", "K7Q2"}, {"title", "K7Q2 card"},
                                 {"status", "inbox"}, {"tab", "features"}, {"body", "# K7Q2 card\ntext"},
                                 {"thread", QJsonArray{}}, {"thread_total", 0}});
    QVERIFY(view.detailOpen());
    sent.clear();

    // An agent writing to another card leaves the open one alone (it used to re-render and
    // jump back to the top on every write anywhere on the board).
    view.handleEvent(QJsonObject{{"event", "board_changed"}, {"upserts", rows({row("M3XJ", "discussing", "features")})},
                                 {"removed", QJsonArray{}}});
    QVERIFY(sent.isEmpty());
    view.handleEvent(QJsonObject{{"event", "board_changed"}, {"upserts", rows({row("K7Q2", "ready", "features")})},
                                 {"removed", QJsonArray{}}});
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_card_get"));
    QCOMPARE(sent.last().value("card").toString(), QStringLiteral("K7Q2"));

    // The watcher's refresh that finds nothing new sends nothing and changes nothing.
    sent.clear();
    view.handleEvent(QJsonObject{{"event", "board_changed"}, {"upserts", QJsonArray{}}, {"removed", QJsonArray{}}});
    QVERIFY(sent.isEmpty());

    view.closeDetail();
    QVERIFY(!view.detailOpen());
}

void BoardModelTests::aQuestionTheAgentCannotTakeIsReportedOnTheCard()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    view.handleEvent(QJsonObject{{"event", "board_card"}, {"card_id", "K7Q2"}, {"title", "K7Q2 card"},
                                 {"status", "inbox"}, {"tab", "features"}, {"body", "text"},
                                 {"thread", QJsonArray{}}, {"thread_total", 0}});
    auto *reply = view.findChild<QPlainTextEdit *>(QStringLiteral("boardReplyEditor"));
    QVERIFY(reply);
    reply->setPlainText(QStringLiteral("Which layout?"));
    QTest::keyClick(reply, Qt::Key_Return);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_ask"));
    const QString askId = sent.last().value("id").toString();

    view.handleEvent(QJsonObject{{"event", "error"}, {"id", askId}, {"text", "Configure a provider first."}});
    auto *error = view.findChild<QLabel *>(QStringLiteral("boardCardError"));
    QVERIFY(error);
    QVERIFY(!error->isHidden());
    QVERIFY(error->text().contains(QStringLiteral("Configure a provider first.")));
    QVERIFY(view.notice().isEmpty());   // on the card, not over the board
}

QTEST_MAIN(BoardModelTests)
#include "boardmodel_test.moc"
