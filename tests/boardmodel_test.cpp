// SPDX-License-Identifier: GPL-3.0-or-later
// The Switchboard pane's pure logic: which tab and column a card falls into, the filter
// language, the ordering and the `#` picker's ranking. No worker, no files, no network.
#include "BoardModel.h"
#include "BoardPane.h"

#include <QJsonArray>
#include <QListWidget>
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

QTEST_MAIN(BoardModelTests)
#include "boardmodel_test.moc"
