// SPDX-License-Identifier: GPL-3.0-or-later
// The Switchboard pane's pure logic: which section a card falls into, the filter language, the
// ordering, the row list the one scrolling view draws, and the `#` picker's ranking. No worker,
// no files, no network.
#include "BoardModel.h"
#include "BoardPane.h"

#include <QCheckBox>
#include <QFrame>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTextBrowser>
#include <QToolButton>
#include <QTimeZone>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using relay::board::Card;
using relay::board::Column;
using relay::board::Model;
using relay::board::Row;
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

// The section ids of a model's list, top to bottom.
QStringList sectionIds(const Model &model)
{
    QStringList out;
    const QList<Column> sections = model.sections();
    for (const Column &section : sections)
        out << section.id;
    return out;
}

// What the row list says, as readable lines: "# ready 2" for a header, "K7Q2" for a card.
QStringList sketch(const QList<Row> &rows)
{
    QStringList out;
    for (const Row &row : rows) {
        if (row.kind == Row::Section)
            out << QStringLiteral("# %1 %2%3").arg(row.columnId).arg(row.count)
                       .arg(row.collapsed ? QStringLiteral(" folded") : QString());
        else
            out << row.cardId;
    }
    return out;
}

QJsonObject opened(const QList<QJsonObject> &cards)
{
    return QJsonObject{{"event", "board"}, {"config", config()}, {"cards", rows(cards)},
                       {"problems", QJsonArray{}}};
}

// A `board_card` event: what the worker hands over when a card is opened, including the hash an
// edit is written against and the `## Issue` text an edit starts from (protocol 19.2).
QJsonObject card(const QString &id, const QString &title, const QString &issue, const QString &hash)
{
    return QJsonObject{{"event", "board_card"}, {"card_id", id}, {"title", title},
                       {"status", "inbox"}, {"tab", "features"}, {"hash", hash},
                       {"path", QStringLiteral("issues/features/") + id + ".md"},
                       {"body", QStringLiteral("# %1\n\n## Issue\n%2\n").arg(title, issue)},
                       {"issue", issue}, {"issue_heading", "Issue"},
                       {"thread", QJsonArray{}}, {"thread_total", 0}};
}

// The pane's one list.
QListWidget *listOf(relay::BoardView &view)
{
    return view.findChild<QListWidget *>(QStringLiteral("boardList"));
}

}  // namespace

class BoardModelTests : public QObject {
    Q_OBJECT

private slots:
    void categoryFoldersComeFromTheConfig();
    void sectionsAreTheConfiguredStatusesThenTheRest();
    void cardsLandInTheSectionOfTheirStatus();
    void closedCardsGoToTheDoneSectionAndParkedOnesToTheirOwn();
    void plansAndMemoriesKeepTheirOwnStatuses();
    void rankOrdersASectionAndDoneIsNewestFirst();
    void theFilterLanguageMatchesEveryTerm();
    void theFilterHidesEmptySectionsAndUnfoldsTheRest();
    void searchRanksOpenCardsAndExactIdsFirst();
    void upsertAndRemoveKeepTheBoardInStep();
    void statusTitlesAreHumanReadable();
    void everyStatusHasAMark();
    void theRowListIsHeadersThenCards();
    void badgesSayWhatTheCardCarries();
    void aRowDropsItsLeastImportantBadgesFirst();
    void cardAgesReadShort();
    void theBodyLosesOnlyAHeadingThatRepeatsTheTitle();
    void threadEntriesSayHowLongAgo();
    void placementNamesTheNeighboursOfTheSlot();
    void aDropFindsTheSectionItLandedIn();
    void upAndDownWalkTheCardsAcrossSectionBreaks();
    void theViewRendersOneListFromAnEvent();
    void theViewSendsAMoveWhenACardIsDropped();
    void arrowsFoldASectionAndTheFoldIsSaved();
    void aSectionCheckboxTakesItsSectionOffThePageAndTheCountSaysSo();
    void theListToolsSitOnTheListPageAndTheHeaderIsTheWayBack();
    void aRefusedWriteIsShownAndAnAcceptedOneCanBeUndone();
    void aChangeRefillsTheListInPlace();
    void theOpenCardRefetchesOnlyForItsOwnChanges();
    void aQuestionTheAgentCannotTakeIsReportedOnTheCard();
    void quickAddNamesTheSectionItAddsTo();
    void aCleanupPreviewsFirstAndItsEventsNeverReachACardThread();
    void applyingAPreviewRunsTheCleanupForReal();
    void aCleanupAndACardsAskRefuseEachOther();
    void stoppingACleanupSendsCancelAndTheSummarySaysSo();
    void theProgressLineKeepsOffAnOpenCardsControls();
    void theTitleAndTheIssueAreEditedOnTheCardAndSavedThroughTheWorker();
    void anEditIsKeptWhenTheCardChangedUnderIt();
    void aCardOffersDiscussPlanAndExecuteAndTheThreadNamesTheMode();
    void executeHandsTheCardToAPaneAndMovesItToInProgress();
    void theExecuteTaskCarriesTheBoardsConventions();
    void aRewriteShowsBeforeAboveTheOldTextAndAfterAboveTheNew();
    void aViewIgnoresEventsFromAnotherProjectsBoard();
};

void BoardModelTests::categoryFoldersComeFromTheConfig()
{
    Model model;
    model.setConfig(config());
    QStringList ids;
    for (const Tab &tab : model.tabs())
        ids << tab.id;
    // The pane no longer renders these; they are the folders a card's file can live in.
    QCOMPARE(ids, (QStringList{"features", "bugs", "planning", "deferred", "done", "memory"}));
    QCOMPARE(model.tab(QStringLiteral("planning"))->type, QStringLiteral("plan"));
    QCOMPARE(model.tab(QStringLiteral("planning"))->title, QStringLiteral("Plans"));
    QVERIFY(model.tab(QStringLiteral("deferred"))->isFilter());
    QVERIFY(!model.tab(QStringLiteral("features"))->isFilter());
}

void BoardModelTests::sectionsAreTheConfiguredStatusesThenTheRest()
{
    Model model;
    model.setConfig(config());
    // No cards yet: the configured lanes, with Done last. The configured `done` column is not a
    // lane of its own — Done is always the final section.
    QCOMPARE(sectionIds(model),
             (QStringList{"inbox", "discussing", "ready", "in-progress", "waiting", "needs-qa",
                          "done"}));
    const QList<Column> sections = model.sections();
    QCOMPARE(sections.at(4).statuses, (QStringList{"needs-review", "needs-labels", "needs-ab"}));
    QCOMPARE(sections.last().statuses, (QStringList{"done", "dropped"}));
    QCOMPARE(model.dropStatus(QStringLiteral("needs-qa")), QStringLiteral("needs-qa-llm"));
    QCOMPARE(model.dropStatus(QStringLiteral("nope")), QString());
}

void BoardModelTests::cardsLandInTheSectionOfTheirStatus()
{
    Model model;
    model.setConfig(config());
    model.reset(rows({row("K7Q2", "inbox", "features"), row("M3XJ", "needs-qa-llm", "features"),
                      row("P9AB", "needs-review", "features"), row("ZZ11", "ready", "bugs")}));
    // One list: a bug and a feature sit in the same section when they share a status.
    QCOMPARE(model.cards(QStringLiteral("inbox")).size(), 1);
    QCOMPARE(model.cards(QStringLiteral("needs-qa")).first().id, QStringLiteral("M3XJ"));
    QCOMPARE(model.cards(QStringLiteral("waiting")).first().id, QStringLiteral("P9AB"));
    QCOMPARE(model.cards(QStringLiteral("ready")).first().id, QStringLiteral("ZZ11"));
    QCOMPARE(model.cards(QStringLiteral("discussing")).size(), 0);
    QCOMPARE(model.openCount(), 4);
    QCOMPARE(model.sectionOf(Card::fromJson(row("P9AB", "needs-review", "features"))),
             QStringLiteral("waiting"));
}

void BoardModelTests::closedCardsGoToTheDoneSectionAndParkedOnesToTheirOwn()
{
    Model model;
    model.setConfig(config());
    model.reset(rows({row("K7Q2", "deferred", "features"), row("M3XJ", "done", "bugs"),
                      row("P9AB", "dropped", "features"), row("R4CD", "ready", "features")}));
    // Done is a status, not a place: done and dropped share the last section, and the open count
    // leaves them out.
    QCOMPARE(model.openCount(), 2);
    QCOMPARE(model.cards(relay::board::doneSection()).size(), 2);
    // Deferred is not a configured lane, so it gets a section of its own before Done.
    QCOMPARE(sectionIds(model).mid(6), (QStringList{"deferred", "done"}));
    QCOMPARE(model.cards(QStringLiteral("deferred")).first().id, QStringLiteral("K7Q2"));
    QCOMPARE(model.cards(QStringLiteral("ready")).first().id, QStringLiteral("R4CD"));
    // …and `status:done` in the filter box still finds a closed card.
    model.setFilter(QStringLiteral("status:dropped"));
    QCOMPARE(model.cards(relay::board::doneSection()).size(), 1);
    QCOMPARE(model.cards(relay::board::doneSection()).first().id, QStringLiteral("P9AB"));
}

void BoardModelTests::plansAndMemoriesKeepTheirOwnStatuses()
{
    Model model;
    model.setConfig(config());
    QJsonObject plan = row("PL01", "approved", "planning");
    plan.insert(QStringLiteral("type"), QStringLiteral("plan"));
    QJsonObject memory = row("ME01", "active", "memory");
    memory.insert(QStringLiteral("type"), QStringLiteral("memory"));
    memory.insert(QStringLiteral("topic"), QStringLiteral("conventions"));
    model.reset(rows({row("K7Q2", "ready", "features"), plan, memory}));

    // Statuses no configured lane collects get a section each, so one list really does hold
    // every open card whatever its type.
    QCOMPARE(sectionIds(model).mid(6), (QStringList{"approved", "active", "done"}));
    QCOMPARE(model.cards(QStringLiteral("approved")).first().id, QStringLiteral("PL01"));
    QCOMPARE(model.cards(QStringLiteral("active")).first().id, QStringLiteral("ME01"));
    QCOMPARE(model.openCount(), 3);
}

void BoardModelTests::rankOrdersASectionAndDoneIsNewestFirst()
{
    Model model;
    model.setConfig(config());
    model.reset(rows({row("AAA1", "ready", "features", "z"), row("BBB2", "ready", "features", "a"),
                      row("CCC3", "ready", "features", "m")}));
    QStringList order;
    for (const Card &card : model.cards(QStringLiteral("ready")))
        order << card.id;
    QCOMPARE(order, (QStringList{"BBB2", "CCC3", "AAA1"}));

    QJsonObject older = row("OLD1", "done", "features", "a");
    older.insert(QStringLiteral("created"), QStringLiteral("2026-01-01"));
    QJsonObject newer = row("NEW1", "done", "features", "z");
    newer.insert(QStringLiteral("created"), QStringLiteral("2026-09-17"));
    model.reset(rows({older, newer}));
    QStringList closed;
    for (const Card &card : model.cards(relay::board::doneSection()))
        closed << card.id;
    QCOMPARE(closed, (QStringList{"NEW1", "OLD1"}));
}

void BoardModelTests::theFilterLanguageMatchesEveryTerm()
{
    Card card;
    card.id = QStringLiteral("K7Q2");
    card.title = QStringLiteral("Voice transcription mode");
    card.status = QStringLiteral("ready");
    card.labels = QStringList{QStringLiteral("voice"), QStringLiteral("bug")};
    card.assignee = QStringLiteral("agent");
    card.waitingOn = QStringLiteral("owner");
    card.tab = QStringLiteral("bugs");
    card.path = QStringLiteral("issues/changes/2026-09-17-voice.md");

    QVERIFY(Model::matches(card, QString()));
    QVERIFY(Model::matches(card, QStringLiteral("  ")));
    QVERIFY(Model::matches(card, QStringLiteral("label:voice")));
    QVERIFY(Model::matches(card, QStringLiteral("label:VOICE")));
    // "bug" and "feature" are labels like any other (owner decision, 2026-09-18).
    QVERIFY(Model::matches(card, QStringLiteral("label:bug")));
    QVERIFY(!Model::matches(card, QStringLiteral("label:feature")));
    QVERIFY(Model::matches(card, QStringLiteral("status:ready")));
    QVERIFY(!Model::matches(card, QStringLiteral("status:inbox")));
    // The folder on disk, or the board.yaml id that names it: both reach the card.
    QVERIFY(Model::matches(card, QStringLiteral("folder:changes")));
    QVERIFY(Model::matches(card, QStringLiteral("folder:bugs")));
    QVERIFY(!Model::matches(card, QStringLiteral("folder:marketing")));
    // Either spelling of the board folder is stripped off the path first (#JN7X): a card on a
    // board made from 2026-09-18 on is in `switchboard/`, one filed before that in `issues/`.
    QCOMPARE(card.folder(), QStringLiteral("changes"));
    Card fresh = card;
    fresh.path = QStringLiteral("switchboard/changes/2026-09-18-voice.md");
    QCOMPARE(fresh.folder(), QStringLiteral("changes"));
    QVERIFY(Model::matches(fresh, QStringLiteral("folder:changes")));
    Card privateCard = card;
    privateCard.path = QStringLiteral("switchboard/.private/changes/2026-09-18-voice.md");
    QCOMPARE(privateCard.folder(), QStringLiteral("changes"));
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

void BoardModelTests::theFilterHidesEmptySectionsAndUnfoldsTheRest()
{
    Model model;
    model.setConfig(config());
    QJsonObject tagged = row("K7Q2", "ready", "features");
    tagged.insert(QStringLiteral("labels"), QJsonArray{QStringLiteral("voice")});
    model.reset(rows({tagged, row("M3XJ", "inbox", "features"), row("DN01", "done", "features")}));
    QCOMPARE(model.openCount(), 2);

    // Unfiltered, every section keeps its header — it is a drop target and it says the lane
    // exists — and what the pane folded stays folded.
    const QSet<QString> folded{relay::board::doneSection()};
    QCOMPARE(sketch(model.rows(folded)),
             (QStringList{"# inbox 1", "M3XJ", "# discussing 0", "# ready 1", "K7Q2",
                          "# in-progress 0", "# waiting 0", "# needs-qa 0", "# done 1 folded"}));

    // Filtered, a section with no match gets out of the way, the counts follow, and nothing is
    // folded: a search that hid its own matches would be a search that does nothing.
    model.setFilter(QStringLiteral("label:voice"));
    QCOMPARE(model.openCount(), 1);
    QCOMPARE(sketch(model.rows(folded)), (QStringList{"# ready 1", "K7Q2"}));
    model.setFilter(QStringLiteral("status:done"));
    QCOMPARE(sketch(model.rows(folded)), (QStringList{"# done 1", "DN01"}));
    model.setFilter(QString());
    QCOMPARE(model.openCount(), 2);
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

void BoardModelTests::everyStatusHasAMark()
{
    using relay::board::statusGlyph;
    // One character each, so every row's title starts at the same x.
    for (const char *status : {"inbox", "discussing", "ready", "in-progress", "needs-review",
                               "needs-qa-llm", "needs-qa-human", "deferred", "done", "dropped",
                               "draft", "approved", "executing", "active", "retired"}) {
        const QString mark = statusGlyph(QString::fromLatin1(status));
        QCOMPARE(mark.size(), 1);
        QVERIFY2(mark != QStringLiteral("·"), status);   // not the fallback
    }
    // An unknown status still gets a mark rather than a hole in the row.
    QCOMPARE(statusGlyph(QStringLiteral("invented")), QStringLiteral("·"));
    QCOMPARE(statusGlyph(QStringLiteral("done")), QStringLiteral("✓"));
    QCOMPARE(statusGlyph(QStringLiteral("dropped")), QStringLiteral("✗"));
}

void BoardModelTests::theRowListIsHeadersThenCards()
{
    Model model;
    model.setConfig(config());
    model.reset(rows({row("K7Q2", "needs-qa-llm", "features"),
                      row("M3XJ", "needs-qa-human", "features", "z"),
                      row("P9AB", "ready", "features")}));
    const QList<Row> list = model.rows({});
    QCOMPARE(sketch(list).mid(0, 2), (QStringList{"# inbox 0", "# discussing 0"}));
    const int at = relay::board::rowOfSection(list, QStringLiteral("needs-qa"));
    QVERIFY(at > 0);
    QCOMPARE(list.at(at).count, 2);
    QCOMPARE(list.at(at).title, QStringLiteral("Needs QA"));
    // A section that collects several statuses names each card's exact one; a single-status
    // section has nothing to repeat.
    QVERIFY(list.at(at + 1).showStatus);
    QVERIFY(!list.at(relay::board::rowOfCard(list, QStringLiteral("P9AB"))).showStatus);
    QCOMPARE(relay::board::cardsInSection(list, QStringLiteral("needs-qa")),
             (QStringList{"K7Q2", "M3XJ"}));
    QCOMPARE(relay::board::rowOfCard(list, QStringLiteral("nope")), -1);
    // A folded section keeps its header and its count, and drops its cards.
    const QList<Row> folded = model.rows({QStringLiteral("needs-qa")});
    QCOMPARE(folded.at(relay::board::rowOfSection(folded, QStringLiteral("needs-qa"))).count, 2);
    QVERIFY(folded.at(relay::board::rowOfSection(folded, QStringLiteral("needs-qa"))).collapsed);
    QCOMPARE(relay::board::rowOfCard(folded, QStringLiteral("K7Q2")), -1);
}

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
    // The status says only what the section header does not ("Needs QA" → "human QA").
    QCOMPARE(texts, (QStringList{"human QA", "voice", "✦ agent", "waiting: owner", "☑ 3/3", "✎ 4"}));
    QCOMPARE(all.at(4).kind, relay::board::Badge::TasksDone);
    QCOMPARE(all.at(3).kind, relay::board::Badge::Waiting);
    // In a section of one status, and on a plain card, there is nothing to repeat.
    QVERIFY(relay::board::badges(Card::fromJson(row("M3XJ", "ready", "features")), false).isEmpty());

    // A row adds how old the card is, at the quiet end.
    card.created = QStringLiteral("2026-09-15");
    const QList<relay::board::Badge> onARow =
        relay::board::rowBadges(card, true, QDate(2026, 9, 18));
    QCOMPARE(onARow.size(), all.size() + 1);
    QCOMPARE(onARow.last().kind, relay::board::Badge::Age);
    QCOMPARE(onARow.last().text, QStringLiteral("3 d"));
}

void BoardModelTests::aRowDropsItsLeastImportantBadgesFirst()
{
    using relay::board::Badge;
    using relay::board::fitBadges;
    const QList<QPair<Badge, int>> measured{
        {Badge{Badge::Label, QStringLiteral("voice")}, 40},
        {Badge{Badge::Waiting, QStringLiteral("waiting: owner")}, 80},
        {Badge{Badge::Tasks, QStringLiteral("☑ 1/3")}, 40},
        {Badge{Badge::Age, QStringLiteral("3 d")}, 30}};
    const auto kept = [&](int available) {
        QStringList out;
        for (const Badge &badge : fitBadges(measured, available, 5))
            out << badge.text;
        return out;
    };
    // Everything fits: 40+80+40+30 plus three 5px gaps.
    QCOMPARE(kept(205), (QStringList{"voice", "waiting: owner", "☑ 1/3", "3 d"}));
    // Squeezed, the label goes first, then the age, then the tasks; `waiting:` is the last to go,
    // because it is why the row is being read.
    QCOMPARE(kept(160), (QStringList{"waiting: owner", "☑ 1/3", "3 d"}));
    QCOMPARE(kept(130), (QStringList{"waiting: owner", "☑ 1/3"}));
    QCOMPARE(kept(90), (QStringList{"waiting: owner"}));
    QCOMPARE(kept(10), QStringList());
    QVERIFY(fitBadges({}, 100, 5).isEmpty());
    QVERIFY(relay::board::badgeDropOrder(Badge::Label)
            < relay::board::badgeDropOrder(Badge::Waiting));
}

void BoardModelTests::cardAgesReadShort()
{
    using relay::board::cardAge;
    const QDate today(2026, 9, 18);
    QCOMPARE(cardAge(QStringLiteral("2026-09-18"), today), QStringLiteral("today"));
    QCOMPARE(cardAge(QStringLiteral("2026-09-17"), today), QStringLiteral("1 d"));
    QCOMPARE(cardAge(QStringLiteral("2026-09-05"), today), QStringLiteral("13 d"));
    QCOMPARE(cardAge(QStringLiteral("2026-09-01"), today), QStringLiteral("2 w"));
    QCOMPARE(cardAge(QStringLiteral("2026-05-01"), today), QStringLiteral("4 mo"));
    QCOMPARE(cardAge(QStringLiteral("2023-09-18"), today), QStringLiteral("3 y"));
    // A timestamp is accepted; a card with no `created`, or an unreadable one, shows no age.
    QCOMPARE(cardAge(QStringLiteral("2026-09-17T10:00:00Z"), today), QStringLiteral("1 d"));
    QVERIFY(cardAge(QString(), today).isEmpty());
    QVERIFY(cardAge(QStringLiteral("last tuesday"), today).isEmpty());
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
    const QStringList section{"A", "B", "C"};
    // Moving B to the top: before A, after nothing.
    QCOMPARE(placement(section, "B", 0), qMakePair(QString("A"), QString()));
    // Moving B to the bottom: after C.
    QCOMPARE(placement(section, "B", 2), qMakePair(QString(), QString("C")));
    // A card from another section dropped between A and B.
    QCOMPARE(placement(section, "X", 1), qMakePair(QString("B"), QString("A")));
    // Into an empty section: no neighbours; a slot past the end is the end.
    QCOMPARE(placement({}, "X", 0), qMakePair(QString(), QString()));
    QCOMPARE(placement(section, "X", 99), qMakePair(QString(), QString("C")));
}

void BoardModelTests::aDropFindsTheSectionItLandedIn()
{
    using relay::board::dropTarget;
    // # ready / A / B / # waiting / C / # done (folded)
    QList<Row> list;
    list << Row{Row::Section, "ready", "Ready", {}, 2, false, false};
    list << Row{Row::Card, "ready", {}, "A", 0, false, false};
    list << Row{Row::Card, "ready", {}, "B", 0, false, false};
    list << Row{Row::Section, "waiting", "Waiting", {}, 1, false, false};
    list << Row{Row::Card, "waiting", {}, "C", 0, false, false};
    list << Row{Row::Section, "done", "Done", {}, 7, true, false};

    QCOMPARE(dropTarget(list, 0), qMakePair(QString("ready"), 0));   // above the first header
    QCOMPARE(dropTarget(list, 1), qMakePair(QString("ready"), 0));   // just under it
    QCOMPARE(dropTarget(list, 2), qMakePair(QString("ready"), 1));   // between A and B
    // The line just above the Waiting header is the bottom of Ready, not the top of Waiting.
    QCOMPARE(dropTarget(list, 3), qMakePair(QString("ready"), 2));
    QCOMPARE(dropTarget(list, 4), qMakePair(QString("waiting"), 0));
    QCOMPARE(dropTarget(list, 5), qMakePair(QString("waiting"), 1));
    // Dropped on a header (the view passes its index + 1), even a folded one: into it, at the top.
    QCOMPARE(dropTarget(list, 6), qMakePair(QString("done"), 0));
    QCOMPARE(dropTarget(list, 99), qMakePair(QString("done"), 0));
    QCOMPARE(dropTarget({}, 0), qMakePair(QString(), 0));
}

void BoardModelTests::upAndDownWalkTheCardsAcrossSectionBreaks()
{
    using relay::board::stepRow;
    QList<Row> list;
    list << Row{Row::Section, "ready", "Ready", {}, 1, false, false};
    list << Row{Row::Card, "ready", {}, "A", 0, false, false};
    list << Row{Row::Section, "waiting", "Waiting", {}, 1, false, false};
    list << Row{Row::Card, "waiting", {}, "B", 0, false, false};

    QCOMPARE(stepRow(list, -1, 1), 1);        // from nowhere: the first card
    QCOMPARE(stepRow(list, 1, 1), 3);         // over the Waiting header in one press
    QCOMPARE(stepRow(list, 3, 1), -1);        // the end stays the end
    QCOMPARE(stepRow(list, 3, -1), 1);
    QCOMPARE(stepRow(list, 1, -1), -1);
    QCOMPARE(stepRow(list, int(list.size()), -1), 3);
    QCOMPARE(stepRow(list, 1, 0), 1);
}

// ---- the widget, driven by protocol events alone ------------------------------------

void BoardModelTests::theViewRendersOneListFromAnEvent()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.handleEvent(opened({row("K7Q2", "inbox", "features"), row("M3XJ", "ready", "features"),
                             row("DN01", "done", "features")}));
    QCOMPARE(view.model().total(), 3);
    // The title carries the open count, not every card ever filed: done is a status.
    QCOMPARE(view.title(), QStringLiteral("Switchboard · 2 open"));
    // One list, not seven columns.
    QCOMPARE(view.findChildren<QListWidget *>(QStringLiteral("boardList")).size(), 1);
    QVERIFY(!view.findChild<QListWidget *>(QStringLiteral("boardColumn")));
    // Done folds itself: its header is there with the count, its card is not.
    QCOMPARE(sketch(view.rows()),
             (QStringList{"# inbox 1", "K7Q2", "# discussing 0", "# ready 1", "M3XJ",
                          "# in-progress 0", "# waiting 0", "# needs-qa 0", "# done 1 folded"}));
    QCOMPARE(listOf(view)->count(), view.rows().size());

    view.handleEvent(QJsonObject{{"event", "board_changed"},
                                 {"upserts", rows({row("K7Q2", "ready", "features")})},
                                 {"removed", QJsonArray{QStringLiteral("M3XJ")}}});
    QCOMPARE(view.model().total(), 2);
    QCOMPARE(view.model().card(QStringLiteral("K7Q2"))->status, QStringLiteral("ready"));
    QCOMPARE(relay::board::cardsInSection(view.rows(), QStringLiteral("ready")),
             (QStringList{"K7Q2"}));
}

void BoardModelTests::theViewSendsAMoveWhenACardIsDropped()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    view.selectCard(QStringLiteral("K7Q2"));
    QCOMPARE(view.selectedCard(), QStringLiteral("K7Q2"));

    view.openSelected();
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value(QStringLiteral("type")).toString(), QStringLiteral("board_card_get"));
    QCOMPARE(sent.last().value(QStringLiteral("card")).toString(), QStringLiteral("K7Q2"));

    view.reload();
    QCOMPARE(sent.last().value(QStringLiteral("type")).toString(), QStringLiteral("board_open"));
}

void BoardModelTests::arrowsFoldASectionAndTheFoldIsSaved()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.handleEvent(opened({row("K7Q2", "ready", "features"), row("M3XJ", "inbox", "features")}));
    view.selectCard(QStringLiteral("K7Q2"));
    QListWidget *list = listOf(view);
    QVERIFY(list);

    // Left folds the section the selection is in and stands on the nearest card still on screen.
    QTest::keyClick(list, Qt::Key_Left);
    QCOMPARE(relay::board::rowOfCard(view.rows(), QStringLiteral("K7Q2")), -1);
    QVERIFY(view.rows().at(relay::board::rowOfSection(view.rows(), QStringLiteral("ready"))).collapsed);
    QCOMPARE(view.selectedCard(), QStringLiteral("M3XJ"));
    // Right puts it back and stands on its first card again.
    QTest::keyClick(list, Qt::Key_Right);
    QVERIFY(relay::board::rowOfCard(view.rows(), QStringLiteral("K7Q2")) >= 0);
    QCOMPARE(view.selectedCard(), QStringLiteral("K7Q2"));

    // The folds go into the layout node and come back from it.
    view.toggleSection(QStringLiteral("inbox"));
    QStringList folded;
    for (const QJsonValue &value : view.collapsedSections())
        folded << value.toString();
    QCOMPARE(folded, (QStringList{"deferred", "done", "inbox"}));
    view.setCollapsedSections(QJsonArray{QStringLiteral("ready")});
    QVERIFY(view.rows().at(relay::board::rowOfSection(view.rows(), QStringLiteral("ready"))).collapsed);
    QVERIFY(!view.rows().at(relay::board::rowOfSection(view.rows(), QStringLiteral("inbox"))).collapsed);
    // A restored pane keeps exactly what the window remembered — Done is not re-folded under it.
    QVERIFY(!view.rows().at(relay::board::rowOfSection(view.rows(), QStringLiteral("done"))).collapsed);
}

// A checkbox at the top of the list page per section, all ticked until one is unticked; unticked
// takes the section off the page entirely, composes with the text filter, and is remembered the
// same way the folds are (owner, 2026-09-18).
void BoardModelTests::aSectionCheckboxTakesItsSectionOffThePageAndTheCountSaysSo()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.handleEvent(opened({row("K7Q2", "ready", "features"), row("M3XJ", "inbox", "features"),
                             row("N4YK", "inbox", "features", "j"),
                             row("DN01", "done", "features")}));

    // One box per section the model has, in order, every one ticked.
    const QList<QCheckBox *> boxes = view.findChildren<QCheckBox *>(
        QStringLiteral("boardSectionCheck"));
    QCOMPARE(boxes.size(), sectionIds(view.model()).size());
    for (QCheckBox *box : boxes)
        QVERIFY2(box->isChecked(), qPrintable(box->text()));

    QLabel *count = view.findChild<QLabel *>(QStringLiteral("boardCount"));
    QVERIFY(count);
    QCOMPARE(count->text(), QStringLiteral("3 open"));

    // Unticking Inbox takes its header and both its cards away; the count says how many of the
    // open cards are left, so the number on screen is never a lie.
    QCheckBox *inbox = nullptr;
    for (QCheckBox *box : boxes)
        if (box->text() == QStringLiteral("INBOX"))
            inbox = box;
    QVERIFY(inbox);
    inbox->setChecked(false);
    QCOMPARE(relay::board::rowOfSection(view.rows(), QStringLiteral("inbox")), -1);
    QCOMPARE(relay::board::rowOfCard(view.rows(), QStringLiteral("M3XJ")), -1);
    QVERIFY(relay::board::rowOfCard(view.rows(), QStringLiteral("K7Q2")) >= 0);
    QCOMPARE(count->text(), QStringLiteral("1 of 3 open"));

    // It composes with the text filter rather than replacing it: a search that would match a
    // card in a hidden section still does not put that section back.
    QLineEdit *filter = view.findChild<QLineEdit *>(QStringLiteral("boardFilter"));
    QVERIFY(filter);
    filter->setText(QStringLiteral("card"));
    QCOMPARE(relay::board::rowOfCard(view.rows(), QStringLiteral("M3XJ")), -1);
    QCOMPARE(relay::board::rowOfCard(view.rows(), QStringLiteral("N4YK")), -1);
    QVERIFY(relay::board::rowOfCard(view.rows(), QStringLiteral("K7Q2")) >= 0);
    filter->clear();

    // Unticked is not folded: the model counts them apart, and the layout node carries both.
    QStringList hidden;
    for (const QJsonValue &value : view.hiddenSections())
        hidden << value.toString();
    QCOMPARE(hidden, (QStringList{"inbox"}));
    QCOMPARE(view.model().hiddenCount({QStringLiteral("inbox")}), 2);
    QCOMPARE(view.model().hiddenCount({}), 0);
    QVERIFY(!view.collapsedSections().contains(QJsonValue(QStringLiteral("inbox"))));

    // Restored from the layout node: the boxes follow, and so does the list.
    view.setHiddenSections(QJsonArray{QStringLiteral("ready")});
    QVERIFY(inbox->isChecked());
    QCOMPARE(relay::board::rowOfSection(view.rows(), QStringLiteral("ready")), -1);
    QVERIFY(relay::board::rowOfCard(view.rows(), QStringLiteral("M3XJ")) >= 0);
}

// The filter and "+ New card" are the top of the list page, not of the pane's header; the header
// is the way back and shows only while a card is open (owner, 2026-09-18).
void BoardModelTests::theListToolsSitOnTheListPageAndTheHeaderIsTheWayBack()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.resize(500, 600);          // narrow enough that an open card takes the whole pane
    view.handleEvent(opened({row("K7Q2", "ready", "features")}));

    QWidget *head = view.findChild<QWidget *>(QStringLiteral("boardHead"));
    QWidget *tools = view.findChild<QWidget *>(QStringLiteral("boardListTools"));
    QWidget *listPane = view.findChild<QWidget *>(QStringLiteral("boardListPane"));
    auto *back = view.findChild<QToolButton *>(QStringLiteral("boardBack"));
    QVERIFY(head && tools && listPane && back);
    // The tools belong to the list page, so they travel with it.
    QVERIFY(tools->isAncestorOf(view.findChild<QLineEdit *>(QStringLiteral("boardFilter"))));
    QVERIFY(listPane->isAncestorOf(tools));
    QVERIFY(view.findChild<QToolButton *>(QStringLiteral("boardAddButton")));
    QVERIFY(view.findChild<QToolButton *>(QStringLiteral("boardCleanup")));
    QVERIFY(head->isHidden());

    // With a card open in a narrow pane the list is gone, tools and all, and the header says the
    // one thing there is to say.
    view.handleEvent(QJsonObject{{"event", "board_card"}, {"card_id", "K7Q2"}, {"title", "K7Q2 card"},
                                 {"status", "ready"}, {"tab", "features"}, {"body", "text"},
                                 {"thread", QJsonArray{}}, {"thread_total", 0}});
    QVERIFY(view.detailOpen());
    QVERIFY(!head->isHidden());
    QCOMPARE(back->text(), QStringLiteral("←  Back to board"));
    QVERIFY(listPane->isHidden());

    // Clicking it goes back, the same place Esc goes, and hints that Esc was the fast way.
    QString hinted;
    view.onHint = [&hinted](const QString &, const QString &keys) { hinted = keys; };
    back->click();
    QVERIFY(!view.detailOpen());
    QVERIFY(head->isHidden());
    QVERIFY(!listPane->isHidden());
    QCOMPARE(hinted, QStringLiteral("Esc"));

    // "Clean up" has its room in that row and starts a preview run (19.9); the run itself is
    // walked in aCleanupPreviewsFirstAndItsEventsNeverReachACardThread below.
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.findChild<QToolButton *>(QStringLiteral("boardCleanup"))->click();
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_cleanup"));
    QCOMPARE(sent.last().value("dry_run").toBool(), true);
}

// ---- the view's answers to the worker ------------------------------------------------------

void BoardModelTests::aRefusedWriteIsShownAndAnAcceptedOneCanBeUndone()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "in-progress", "features")}));
    view.selectCard(QStringLiteral("K7Q2"));
    QListWidget *list = listOf(view);
    QVERIFY(list);

    // Alt+Shift+Right: to the next status, which is the section below.
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

void BoardModelTests::aChangeRefillsTheListInPlace()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.handleEvent(opened({row("K7Q2", "inbox", "features"), row("M3XJ", "ready", "features")}));
    QPointer<QListWidget> list = listOf(view);
    QVERIFY(list);
    const int rowsBefore = view.rows().size();

    // A card moving refills the one list; the widget (and so its scroll position, its focus and
    // an open quick-add field) survives.
    view.handleEvent(QJsonObject{{"event", "board_changed"}, {"upserts", rows({row("K7Q2", "ready", "features")})},
                                 {"removed", QJsonArray{}}});
    QVERIFY(list);
    QCOMPARE(listOf(view), list.data());
    QCOMPARE(view.rows().size(), rowsBefore);
    QCOMPARE(relay::board::cardsInSection(view.rows(), QStringLiteral("inbox")), QStringList());
    QCOMPARE(relay::board::cardsInSection(view.rows(), QStringLiteral("ready")),
             (QStringList{"K7Q2", "M3XJ"}));
    QCOMPARE(list->count(), view.rows().size());
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

// ---- the whole-board cleanup (protocol 19.9) -----------------------------------------------

namespace {

// `board_cleanup_started`, as the worker sends it.
QJsonObject cleanupStarted(const QString &id, bool dryRun)
{
    return QJsonObject{{"event", "board_cleanup_started"}, {"id", id}, {"run_id", "c-1a2b3c"},
                       {"dry_run", dryRun}, {"scope", QJsonValue()}, {"cards", 96},
                       {"limits", QJsonObject{{"max_writes_per_turn", 400}}},
                       {"changelog", "docs/qa_evidence/2026-09-18-switchboard-cleanup/run.md"}};
}

// One turn event of the run: `cleanup: true` and a `run_id`, and never a `card_id` (19.9).
QJsonObject cleanupEvent(const QString &type, const QJsonObject &extra = {})
{
    QJsonObject out{{"event", type}, {"cleanup", true}, {"run_id", "c-1a2b3c"}};
    for (auto it = extra.begin(); it != extra.end(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

QJsonObject cleanupSummary(const QString &id, bool dryRun, const QString &outcome,
                           const QJsonArray &changes)
{
    int writes = 0, proposed = 0;
    for (const QJsonValue &value : changes)
        (value.toObject().value("proposed").toBool() ? proposed : writes) += 1;
    return QJsonObject{
        {"event", "board_cleanup_summary"}, {"id", id}, {"run_id", "c-1a2b3c"},
        {"outcome", outcome}, {"dry_run", dryRun}, {"seconds", 412.7},
        {"counts", QJsonObject{{"writes", writes}, {"proposed", proposed},
                               {"cards_touched", changes.size()}, {"merge", 1}, {"move", 1}}},
        {"changes", changes}, {"truncated", false}, {"refusals", QJsonArray{}},
        {"cards_before", 96}, {"cards_after", 95},
        {"changelog", "docs/qa_evidence/2026-09-18-switchboard-cleanup/run.md"},
        {"report", "I merged one pair and moved one card.\n\nI left the QA lane alone."}};
}

QJsonArray twoChanges(bool proposed)
{
    return QJsonArray{
        QJsonObject{{"action", "merge"}, {"card_id", "K7Q2"}, {"summary", "merged 1 card in"},
                    {"path", "issues/features/k7q2.md"}, {"proposed", proposed}},
        QJsonObject{{"action", "move"}, {"card_id", "M3XJ"}, {"summary", "Inbox to Ready"},
                    {"path", "issues/features/m3xj.md"}, {"proposed", proposed}}};
}

QToolButton *cleanupButton(relay::BoardView &view)
{
    return view.findChild<QToolButton *>(QStringLiteral("boardCleanup"));
}

}  // namespace

// The first click is a preview, and every event the run sends is drawn over the board — never in
// the thread of a card that happens to be open (19.9: `cleanup: true`, a run id, no card id).
void BoardModelTests::aCleanupPreviewsFirstAndItsEventsNeverReachACardThread()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features"), row("M3XJ", "inbox", "features")}));
    view.handleEvent(card("K7Q2", "K7Q2 card", "the issue", "h1"));
    QVERIFY(view.detailOpen());
    auto *document = view.findChild<QTextBrowser *>(QStringLiteral("boardCardDocument"));
    QVERIFY(document);
    const QString threadBefore = document->toPlainText();

    QToolButton *button = cleanupButton(view);
    QVERIFY(button);
    QCOMPARE(button->text(), QStringLiteral("Clean up"));
    sent.clear();
    button->click();

    // A preview, not the real thing: a cleanup rewrites the owner's files, so the button never
    // starts one that writes.
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_cleanup"));
    QCOMPARE(sent.last().value("dry_run").toBool(), true);
    const QString runId = sent.last().value("id").toString();
    QVERIFY(!runId.isEmpty());
    QVERIFY(view.cleanupRunning());
    QCOMPARE(button->text(), QStringLiteral("Stop"));

    view.handleEvent(cleanupStarted(runId, true));
    QVERIFY(view.cleanupRunning());
    QVERIFY(view.notice().contains(QStringLiteral("Cleanup preview")));

    // The run's chatter and its writes: progress over the board, nothing on the card.
    view.handleEvent(cleanupEvent(QStringLiteral("delta"), {{"text", "thinking about the board"}}));
    // Protocol § 23: the progress line is the call's concise line, not the tool's name. Without a
    // label it falls back to the tool name, which is exactly what it printed before.
    view.handleEvent(cleanupEvent(QStringLiteral("tool_started"), {{"tool", "board_read"}}));
    QVERIFY(view.notice().contains(QStringLiteral("board read")));
    view.handleEvent(cleanupEvent(QStringLiteral("tool_started"),
                                  {{"tool", "board_list"},
                                   {"label", QJsonObject{{"kind", "board"}, {"running", "listing cards"},
                                                         {"title", "listed cards"}}}}));
    QVERIFY(view.notice().contains(QStringLiteral("listing cards")));
    view.handleEvent(cleanupEvent(QStringLiteral("tool_result"),
                                  {{"tool", "board_list"},
                                   {"label", QJsonObject{{"kind", "board"}, {"running", "listing cards"},
                                                         {"title", "listed cards"},
                                                         {"stats", QJsonArray{QStringLiteral("14 cards")}},
                                                         {"ok", true}}}}));
    QVERIFY(view.notice().contains(QStringLiteral("listed cards · 14 cards")));
    view.handleEvent(cleanupEvent(QStringLiteral("board_activity"),
                                  {{"id", "M3XJ"}, {"action", "move"},
                                   {"summary", "Inbox to Ready"}, {"write_id", "w-1"}}));
    QVERIFY(view.notice().contains(QStringLiteral("#M3XJ")));
    QVERIFY(view.notice().contains(QStringLiteral("1 proposed")));
    QCOMPARE(document->toPlainText(), threadBefore);
    QVERIFY(!document->toPlainText().contains(QStringLiteral("thinking about the board")));

    // The summary ends the run and puts the result in the list page, with Apply offered because
    // this was only a preview.
    view.handleEvent(cleanupEvent(QStringLiteral("done")));
    view.handleEvent(cleanupSummary(runId, true, QStringLiteral("done"), twoChanges(true)));
    QVERIFY(!view.cleanupRunning());
    QCOMPARE(button->text(), QStringLiteral("Clean up"));
    auto *panel = view.findChild<QWidget *>(QStringLiteral("boardCleanupPanel"));
    auto *head = view.findChild<QLabel *>(QStringLiteral("boardCleanupHead"));
    auto *body = view.findChild<QTextBrowser *>(QStringLiteral("boardCleanupBody"));
    QVERIFY(panel && head && body);
    QVERIFY(!panel->isHidden());
    QVERIFY(head->text().contains(QStringLiteral("nothing was written")));
    QVERIFY(head->text().contains(QStringLiteral("2 proposed")));
    QVERIFY(body->toPlainText().contains(QStringLiteral("#K7Q2")));
    QVERIFY(body->toPlainText().contains(QStringLiteral("I left the QA lane alone.")));
    QVERIFY(!view.findChild<QToolButton *>(QStringLiteral("boardAddButton"))->isHidden());
    // It is dismissible, and it never took the keyboard off the list.
    QCOMPARE(body->focusPolicy(), Qt::NoFocus);
    view.findChild<QToolButton *>(QStringLiteral("boardCleanupPanel"));
    for (QToolButton *tool : panel->findChildren<QToolButton *>())
        if (tool->text() == QStringLiteral("Dismiss"))
            tool->click();
    QVERIFY(panel->isHidden());
}

// Apply is the only way to a run that writes, and it says so afterwards.
void BoardModelTests::applyingAPreviewRunsTheCleanupForReal()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    cleanupButton(view)->click();
    const QString previewId = sent.last().value("id").toString();
    view.handleEvent(cleanupStarted(previewId, true));
    view.handleEvent(cleanupSummary(previewId, true, QStringLiteral("done"), twoChanges(true)));

    auto *panel = view.findChild<QWidget *>(QStringLiteral("boardCleanupPanel"));
    QToolButton *apply = nullptr;
    for (QToolButton *tool : panel->findChildren<QToolButton *>())
        if (tool->text() == QStringLiteral("Apply"))
            apply = tool;
    QVERIFY(apply);
    QVERIFY(!apply->isHidden());
    sent.clear();
    apply->click();
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_cleanup"));
    QCOMPARE(sent.last().value("dry_run").toBool(), false);
    const QString runId = sent.last().value("id").toString();
    QVERIFY(panel->isHidden());          // the plan goes when the run it planned starts

    view.handleEvent(cleanupStarted(runId, false));
    QVERIFY(view.notice().startsWith(QStringLiteral("Cleanup ·")));
    view.handleEvent(cleanupSummary(runId, false, QStringLiteral("done"), twoChanges(false)));
    auto *head = view.findChild<QLabel *>(QStringLiteral("boardCleanupHead"));
    QVERIFY(head->text().contains(QStringLiteral("the board was rewritten")));
    QVERIFY(head->text().contains(QStringLiteral("2 written")));
    // Nothing to apply twice: the run already wrote.
    for (QToolButton *tool : panel->findChildren<QToolButton *>())
        if (tool->text() == QStringLiteral("Apply"))
            QVERIFY(tool->isHidden());
}

// One turn at a time (19.9): the second of a cleanup and a card's ask is refused, and the pane
// says which is running rather than showing a bare error.
void BoardModelTests::aCleanupAndACardsAskRefuseEachOther()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    view.handleEvent(card("K7Q2", "K7Q2 card", "the issue", "h1"));
    auto *reply = view.findChild<QPlainTextEdit *>(QStringLiteral("boardReplyEditor"));
    auto *error = view.findChild<QLabel *>(QStringLiteral("boardCardError"));
    QVERIFY(reply && error);

    // A cleanup is running, so the ask is not even sent, and the words stay in the box.
    cleanupButton(view)->click();
    view.handleEvent(cleanupStarted(sent.last().value("id").toString(), true));
    sent.clear();
    reply->setPlainText(QStringLiteral("Which layout?"));
    QTest::keyClick(reply, Qt::Key_Return);
    QVERIFY(sent.isEmpty());
    QVERIFY(!error->isHidden());
    QVERIFY(error->text().contains(QStringLiteral("A cleanup is running")));
    QCOMPARE(reply->toPlainText(), QStringLiteral("Which layout?"));
    // …and it stops saying so when it stops being true.
    view.handleEvent(cleanupSummary(QStringLiteral("x"), true, QStringLiteral("done"), QJsonArray{}));
    QVERIFY(error->isHidden());
    QCOMPARE(reply->toPlainText(), QStringLiteral("Which layout?"));

    // The other way round: an ask is running and the worker refuses the cleanup.
    relay::BoardView second(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> theirs;
    second.onSend = [&theirs](const QJsonObject &message) { theirs << message; };
    second.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    cleanupButton(second)->click();
    const QString refusedId = theirs.last().value("id").toString();
    second.handleEvent(QJsonObject{{"event", "error"}, {"id", refusedId}, {"code", "board_busy"},
                                   {"agent_busy", true}, {"cleanup_running", false},
                                   {"card_id", "K7Q2"},
                                   {"text", "the agent is answering about #K7Q2"}});
    QVERIFY(!second.cleanupRunning());
    QCOMPARE(cleanupButton(second)->text(), QStringLiteral("Clean up"));
    QVERIFY(second.notice().contains(QStringLiteral("answering on #K7Q2")));
    QVERIFY(second.notice().contains(QStringLiteral("did not start")));

    // And an ask refused by a cleanup someone else started: off the card, and back in the box.
    relay::BoardView third(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> mine;
    third.onSend = [&mine](const QJsonObject &message) { mine << message; };
    third.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    third.handleEvent(card("K7Q2", "K7Q2 card", "the issue", "h1"));
    auto *box = third.findChild<QPlainTextEdit *>(QStringLiteral("boardReplyEditor"));
    box->setPlainText(QStringLiteral("Still relevant?"));
    QTest::keyClick(box, Qt::Key_Return);
    QCOMPARE(mine.last().value("type").toString(), QStringLiteral("board_ask"));
    third.handleEvent(QJsonObject{{"event", "error"}, {"id", mine.last().value("id").toString()},
                                  {"code", "board_busy"}, {"agent_busy", true},
                                  {"cleanup_running", true}, {"card_id", QJsonValue()},
                                  {"text", "a cleanup is running"}});
    auto *theirError = third.findChild<QLabel *>(QStringLiteral("boardCardError"));
    QVERIFY(theirError->text().contains(QStringLiteral("A cleanup is running")));
    QVERIFY(theirError->text().contains(QStringLiteral("not in the thread")));
    QCOMPARE(box->toPlainText(), QStringLiteral("Still relevant?"));
}

void BoardModelTests::stoppingACleanupSendsCancelAndTheSummarySaysSo()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    QToolButton *button = cleanupButton(view);
    button->click();
    const QString runId = sent.last().value("id").toString();
    view.handleEvent(cleanupStarted(runId, false));
    QCOMPARE(button->text(), QStringLiteral("Stop"));

    sent.clear();
    button->click();
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("cancel"));
    QVERIFY(view.cleanupRunning());      // still running until the summary says otherwise

    view.handleEvent(cleanupEvent(QStringLiteral("cancelled")));
    view.handleEvent(cleanupSummary(runId, false, QStringLiteral("cancelled"), twoChanges(false)));
    QVERIFY(!view.cleanupRunning());
    QCOMPARE(button->text(), QStringLiteral("Clean up"));
    auto *head = view.findChild<QLabel *>(QStringLiteral("boardCleanupHead"));
    QVERIFY(head->text().contains(QStringLiteral("stopped part way")));
    QVERIFY(view.notice().isEmpty());    // the progress line goes with the run
}

// A move's notice is gone in ten seconds; a cleanup's progress line stays up for minutes, so it
// must not be parked on the reply box and the Ask button of a card that has the pane to itself.
void BoardModelTests::theProgressLineKeepsOffAnOpenCardsControls()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.resize(500, 700);          // narrow: an open card takes the whole pane
    view.show();                    // the placement is geometry, so the layout has to have run
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    cleanupButton(view)->click();
    auto *notice = view.findChild<QFrame *>(QStringLiteral("boardNotice"));
    QVERIFY(notice);
    QVERIFY(!notice->isHidden());
    // Over the list it sits at the bottom, where every other notice has always been.
    QVERIFY2(notice->y() > view.height() / 2, qPrintable(QString::number(notice->y())));

    // With the card on top of it, the line sits clear of the reply box and the Ask button.
    view.handleEvent(card("K7Q2", "K7Q2 card", "the issue", "h1"));
    QVERIFY(view.detailOpen());
    QCoreApplication::processEvents();
    view.rebuild();                 // any pending layout, then the notice is placed again
    QCoreApplication::sendPostedEvents(&view, QEvent::LayoutRequest);
    auto *reply = view.findChild<QFrame *>(QStringLiteral("boardReply"));
    QVERIFY(reply);
    const int replyTop = reply->mapTo(&view, QPoint(0, 0)).y();
    QVERIFY2(notice->y() + notice->height() <= replyTop,
             qPrintable(QStringLiteral("notice %1..%2, reply starts at %3")
                            .arg(notice->y()).arg(notice->y() + notice->height()).arg(replyTop)));

    // …and back to the bottom of the pane once the card is closed.
    view.closeDetail();
    QVERIFY2(notice->y() > view.height() / 2, qPrintable(QString::number(notice->y())));
}

void BoardModelTests::quickAddNamesTheSectionItAddsTo()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "ready", "features")}));
    auto *field = view.findChild<QLineEdit *>(QStringLiteral("boardQuickAdd"));
    auto *strip = view.findChild<QWidget *>(QStringLiteral("boardQuickAddRow"));
    QVERIFY(field);
    QVERIFY(strip);
    QVERIFY(strip->isHidden());

    // `n` with a card selected adds into that card's section, and says so.
    view.selectCard(QStringLiteral("K7Q2"));
    view.quickAdd();
    QVERIFY(!strip->isHidden());
    QCOMPARE(field->placeholderText(), QStringLiteral("New card in Ready — Enter adds, Esc closes"));
    field->setText(QStringLiteral("clickable paths in the output"));
    QTest::keyClick(field, Qt::Key_Return);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_create"));
    QCOMPARE(sent.last().value("status").toString(), QStringLiteral("ready"));
    QCOMPARE(sent.last().value("text").toString(), QStringLiteral("clickable paths in the output"));
    // With no tabs a new card is filed in the board's first category folder; `m` re-files it.
    QCOMPARE(sent.last().value("tab").toString(), QStringLiteral("features"));
    // The field stays open for the next card, and Esc closes it.
    QVERIFY(!strip->isHidden());
    QVERIFY(field->text().isEmpty());
    QTest::keyClick(field, Qt::Key_Escape);
    QVERIFY(strip->isHidden());

    // Nothing is created straight into Done: `n` there falls back to the first section.
    view.quickAddIn(relay::board::doneSection());
    QCOMPARE(field->placeholderText(), QStringLiteral("New card in Inbox — Enter adds, Esc closes"));
}

// Owner, 2026-09-18: "after adding a card, i couldn't edit the title or the task." Both are
// edited on the card itself and written by the worker, never by the pane.
void BoardModelTests::theTitleAndTheIssueAreEditedOnTheCardAndSavedThroughTheWorker()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    view.handleEvent(card("K7Q2", QStringLiteral("K7Q2 card"), QStringLiteral("clicking a path"),
                          QString(64, QLatin1Char('a'))));
    view.selectCard(QStringLiteral("K7Q2"));

    auto *title = view.findChild<QLineEdit *>(QStringLiteral("boardCardTitleEdit"));
    auto *issue = view.findChild<QPlainTextEdit *>(QStringLiteral("boardIssueEditor"));
    QVERIFY(title);
    QVERIFY(issue);
    QVERIFY(title->isHidden());     // a card is read until it is edited

    // `e` turns the title and the card's own words into fields, seeded from the file.
    view.editSelected();
    QVERIFY(!title->isHidden());
    QCOMPARE(title->text(), QStringLiteral("K7Q2 card"));
    QCOMPARE(issue->toPlainText(), QStringLiteral("clicking a path"));

    // Esc leaves the card exactly as it was, and writes nothing.
    sent.clear();
    title->setText(QStringLiteral("typed then dropped"));
    QTest::keyClick(title, Qt::Key_Escape);
    QVERIFY(title->isHidden());
    QVERIFY(sent.isEmpty());

    // Ctrl+Enter in the text saves both in one hash-checked write.
    view.editSelected();
    title->setText(QStringLiteral("Clickable paths in the output"));
    issue->setPlainText(QStringLiteral("clicking a path should open it"));
    QTest::keyClick(issue, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_update"));
    QCOMPARE(sent.last().value("card").toString(), QStringLiteral("K7Q2"));
    QCOMPARE(sent.last().value("base_hash").toString(), QString(64, QLatin1Char('a')));
    const QJsonObject patch = sent.last().value("patch").toObject();
    QCOMPARE(patch.value("title").toString(), QStringLiteral("Clickable paths in the output"));
    // The section the owner's words live in is `## Issue` (it was `## Request` until 2026-09-18).
    QCOMPARE(patch.value("replace_section").toObject().value("heading").toString(),
             QStringLiteral("Issue"));
    QCOMPARE(patch.value("replace_section").toObject().value("text").toString(),
             QStringLiteral("clicking a path should open it"));

    // The card goes back to being read once the worker has written it, and says so.
    view.handleEvent(QJsonObject{{"event", "board_written"}, {"id", sent.last().value("id")},
                                 {"kind", "board_update"}, {"card_id", "K7Q2"},
                                 {"write_id", "w-1"}});
    QVERIFY(title->isHidden());
    QVERIFY(view.notice().contains(QStringLiteral("Saved #K7Q2")));
}

void BoardModelTests::anEditIsKeptWhenTheCardChangedUnderIt()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    view.handleEvent(card("K7Q2", QStringLiteral("K7Q2 card"), QStringLiteral("the ask"),
                          QString(64, QLatin1Char('a'))));
    view.selectCard(QStringLiteral("K7Q2"));
    view.editSelected();
    auto *issue = view.findChild<QPlainTextEdit *>(QStringLiteral("boardIssueEditor"));
    auto *error = view.findChild<QLabel *>(QStringLiteral("boardCardError"));
    QVERIFY(issue);
    QVERIFY(error);
    issue->setPlainText(QStringLiteral("the ask, in better words"));

    // Someone else wrote the file meanwhile: the write is refused, nothing typed is lost, and
    // the card is read again so a second Save goes against the version that is there now.
    sent.clear();
    QTest::keyClick(issue, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_update"));
    const QString requestId = sent.last().value("id").toString();
    sent.clear();
    view.handleEvent(QJsonObject{{"event", "error"}, {"id", requestId}, {"code", "board_conflict"},
                                 {"text", "#K7Q2 changed since you read it"},
                                 {"current_hash", QString(64, QLatin1Char('b'))}});
    QCOMPARE(issue->toPlainText(), QStringLiteral("the ask, in better words"));
    QVERIFY(!error->isHidden());
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_card_get"));

    // The re-read hands over the new hash and the version on disk, and still keeps the text.
    view.handleEvent(card("K7Q2", QStringLiteral("K7Q2 card"), QStringLiteral("someone else's words"),
                          QString(64, QLatin1Char('b'))));
    QCOMPARE(issue->toPlainText(), QStringLiteral("the ask, in better words"));
    QVERIFY(!error->isHidden());
    sent.clear();
    QTest::keyClick(issue, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(sent.last().value("base_hash").toString(), QString(64, QLatin1Char('b')));
}

// ---- Discuss / Plan / Execute (#XS6Q, protocol 19.10) --------------------------------------

namespace {
QPushButton *button(relay::BoardView &view, const QString &text)
{
    const auto buttons = view.findChildren<QPushButton *>();
    for (QPushButton *candidate : buttons)
        if (candidate->text() == text)
            return candidate;
    return nullptr;
}
}  // namespace

void BoardModelTests::aCardOffersDiscussPlanAndExecuteAndTheThreadNamesTheMode()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    view.handleEvent(card("K7Q2", "K7Q2 card", "the issue", "h1"));
    QVERIFY(button(view, QStringLiteral("Discuss")));
    QVERIFY(button(view, QStringLiteral("Plan")));
    QVERIFY(button(view, QStringLiteral("Execute")));
    QVERIFY(!button(view, QStringLiteral("Ask the agent")));

    // Enter in the reply box discusses.
    auto *reply = view.findChild<QPlainTextEdit *>(QStringLiteral("boardReplyEditor"));
    reply->setPlainText(QStringLiteral("Is this still wanted?"));
    QTest::keyClick(reply, Qt::Key_Return);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_ask"));
    QCOMPARE(sent.last().value("mode").toString(), QStringLiteral("discuss"));
    QCOMPARE(sent.last().value("text").toString(), QStringLiteral("Is this still wanted?"));
    // While it runs, Discuss is Stop and the other two wait.
    QVERIFY(button(view, QStringLiteral("Stop")));
    QVERIFY(!button(view, QStringLiteral("Plan"))->isEnabled());
    QVERIFY(!button(view, QStringLiteral("Execute"))->isEnabled());
    view.handleEvent(QJsonObject{{"event", "done"}, {"card_id", "K7Q2"}, {"mode", "discuss"}});
    QVERIFY(button(view, QStringLiteral("Discuss")));
    QVERIFY(button(view, QStringLiteral("Plan"))->isEnabled());

    // `p` plans with an empty box: no text travels, and the Plan button becomes Stop.
    sent.clear();
    view.cardAction(QStringLiteral("plan"));
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value("mode").toString(), QStringLiteral("plan"));
    QVERIFY(!sent.last().contains("text"));
    QVERIFY(!button(view, QStringLiteral("Plan")));
    QVERIFY(!button(view, QStringLiteral("Discuss"))->isEnabled());
    button(view, QStringLiteral("Stop"))->click();
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("cancel"));
    view.handleEvent(QJsonObject{{"event", "cancelled"}, {"card_id", "K7Q2"}});

    // Ctrl+Enter plans with what was typed as the note.
    sent.clear();
    reply->setPlainText(QStringLiteral("keep it to the backend"));
    QTest::keyClick(reply, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(sent.last().value("mode").toString(), QStringLiteral("plan"));
    QCOMPARE(sent.last().value("text").toString(), QStringLiteral("keep it to the backend"));
    view.handleEvent(QJsonObject{{"event", "done"}, {"card_id", "K7Q2"}});

    // The thread says which mode each entry was.
    QJsonObject withThread = card("K7Q2", "K7Q2 card", "the issue", "h2");
    withThread.insert("thread", QJsonArray{
        QJsonObject{{"entry_id", "20260918T100000Z-a1"}, {"author", "owner"}, {"kind", "comment"},
                    {"attrs", QJsonObject{{"mode", "plan"}}}, {"text", "Plan this card."}},
        QJsonObject{{"entry_id", "20260918T100100Z-a2"}, {"author", "agent"}, {"kind", "comment"},
                    {"attrs", QJsonObject{{"mode", "discuss"}, {"model", "glm-5"}}}, {"text", "Retitled it."}}});
    withThread.insert("thread_total", 2);
    view.handleEvent(withThread);
    const QString doc = view.findChild<QTextBrowser *>(QStringLiteral("boardCardDocument"))->toPlainText();
    QVERIFY2(doc.contains(QStringLiteral("owner  Plan")), qPrintable(doc));
    QVERIFY2(doc.contains(QStringLiteral("✦ agent  Discuss · glm-5")), qPrintable(doc));
    QCOMPARE(relay::board::modeTitle(QStringLiteral("plan")), QStringLiteral("Plan"));
    QVERIFY(relay::board::modeTitle(QStringLiteral("comment")).isEmpty());
}

void BoardModelTests::executeHandsTheCardToAPaneAndMovesItToInProgress()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    QString handedCard, handedTask;
    int opened = 0;
    view.onExecuteCard = [&](const QString &id, const QString &task) {
        ++opened;
        handedCard = id;
        handedTask = task;
    };
    view.handleEvent(::opened({row("K7Q2", "ready", "features")}));
    view.handleEvent(card("K7Q2", "Voice mode", "the issue", QString(64, QLatin1Char('a'))));
    auto *error = view.findChild<QLabel *>(QStringLiteral("boardCardError"));

    // No plan and no acceptance: the first press asks, here on the card, and does nothing else.
    sent.clear();
    view.cardAction(QStringLiteral("execute"));
    QCOMPARE(opened, 0);
    QVERIFY(sent.isEmpty());
    QVERIFY(!error->isHidden());
    QVERIFY(error->text().contains(QStringLiteral("no plan and no acceptance")));

    // The second goes ahead: assignee (hash-checked), In progress, a note, then the pane.
    button(view, QStringLiteral("Execute"))->click();
    QCOMPARE(opened, 1);
    QCOMPARE(handedCard, QStringLiteral("K7Q2"));
    QVERIFY(handedTask.startsWith(QStringLiteral("Execute #K7Q2: Voice mode")));
    QStringList types;
    for (const QJsonObject &message : std::as_const(sent))
        types << message.value("type").toString();
    QCOMPARE(types, (QStringList{"board_update", "board_move", "board_comment"}));
    QCOMPARE(sent.at(0).value("base_hash").toString(), QString(64, QLatin1Char('a')));
    QCOMPARE(sent.at(0).value("patch").toObject().value("fields").toObject().value("assignee").toString(),
             QStringLiteral("agent"));
    QCOMPARE(sent.at(1).value("status").toString(), QStringLiteral("in-progress"));
    QVERIFY(sent.at(2).value("text").toString().startsWith(QStringLiteral("Execute ·")));
    QVERIFY(error->isHidden());

    // A card with a plan goes at once, and one already in progress and assigned is not rewritten.
    QJsonObject planned = card("K7Q2", "Voice mode", "the issue", QString(64, QLatin1Char('b')));
    planned.insert("status", "in-progress");
    planned.insert("sections", QJsonArray{"Issue", "Plan"});
    planned.insert("front", QJsonObject{{"assignee", "agent"}});
    view.handleEvent(planned);
    sent.clear();
    view.cardAction(QStringLiteral("execute"));
    QCOMPARE(opened, 2);
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_comment"));
    QVERIFY(handedTask.contains(QStringLiteral("`## Plan`")));
}

void BoardModelTests::theExecuteTaskCarriesTheBoardsConventions()
{
    const QString task = relay::board::executeTask(QStringLiteral("XS6Q"), QStringLiteral("Modes"),
                                                   true, true, QStringLiteral("backend first"));
    QVERIFY(task.startsWith(QStringLiteral("Execute #XS6Q: Modes\n")));
    QVERIFY(task.contains(QStringLiteral("until the acceptance holds")));
    QVERIFY(task.contains(QStringLiteral("implemented_by")));
    QVERIFY(task.contains(QStringLiteral("links.commits")));
    QVERIFY(task.contains(QStringLiteral("Put #XS6Q in the message of every commit")));
    QVERIFY(task.contains(QStringLiteral("needs-qa-llm")));
    QVERIFY(task.endsWith(QStringLiteral("The owner adds, verbatim:\nbackend first")));
    QVERIFY(relay::board::executeTask(QStringLiteral("XS6Q"), QStringLiteral("Modes"), false, false)
                .contains(QStringLiteral("no plan and no acceptance")));
}

// A rewrite entry's before/after, as the thread file writes them for GitHub, reads in order in
// the card detail: the label above its text (QTextDocument drops the <details> tags).
void BoardModelTests::aRewriteShowsBeforeAboveTheOldTextAndAfterAboveTheNew()
{
    const QString entry = QStringLiteral(
        "- ✦ rewrote title\n\n<details><summary>before</summary>\n\n```\nOld\n```\n\n</details>\n\n"
        "<details><summary>after</summary>\n\n```\nNew\n```\n\n</details>");
    const QString md = relay::board::threadMarkdown(entry, QStringLiteral("rewrite"));
    QVERIFY(!md.contains(QStringLiteral("<")));
    QVERIFY(md.startsWith(QStringLiteral("✦ rewrote title")));
    QVERIFY(md.indexOf(QStringLiteral("**before**")) < md.indexOf(QStringLiteral("Old")));
    QVERIFY(md.indexOf(QStringLiteral("Old")) < md.indexOf(QStringLiteral("**after**")));
    QVERIFY(md.indexOf(QStringLiteral("**after**")) < md.indexOf(QStringLiteral("New")));
    QCOMPARE(relay::board::threadMarkdown(QStringLiteral("- a list"), QStringLiteral("comment")),
             QStringLiteral("- a list"));
}

// The Switchboard is per project. Every board event carries the `issues` directory it came from,
// and a view that has learned its own root from the `board` event refuses everything from another
// one: a reset it accepted would silently repoint the view at the other project's cards, and the
// next drag or quick add would be written into that repository (owner report, 2026-09-18).
void BoardModelTests::aViewIgnoresEventsFromAnotherProjectsBoard()
{
    relay::BoardView view(QStringLiteral("/tmp/mine"));
    QJsonObject mine = opened({row("K7Q2", "inbox", "features")});
    mine.insert(QStringLiteral("root"), QStringLiteral("/tmp/mine/issues"));
    view.handleEvent(mine);
    QCOMPARE(view.model().total(), 1);

    // Another project's worker resetting the board: not this view's.
    QJsonObject theirs = opened({row("ZZ11", "inbox", "features"), row("ZZ22", "ready", "features")});
    theirs.insert(QStringLiteral("root"), QStringLiteral("/tmp/theirs/issues"));
    view.handleEvent(theirs);
    QCOMPARE(view.model().total(), 1);
    QVERIFY(view.model().card(QStringLiteral("K7Q2")));
    QVERIFY(!view.model().card(QStringLiteral("ZZ11")));

    // Nor a change from it.
    view.handleEvent(QJsonObject{{"event", "board_changed"},
                                 {"root", "/tmp/theirs/issues"},
                                 {"upserts", rows({row("ZZ22", "ready", "features")})},
                                 {"removed", QJsonArray{QStringLiteral("K7Q2")}}});
    QCOMPARE(view.model().total(), 1);
    QVERIFY(view.model().card(QStringLiteral("K7Q2")));

    // Its own root still gets through, and so does an event from a worker that sends no root.
    view.handleEvent(QJsonObject{{"event", "board_changed"},
                                 {"root", "/tmp/mine/issues"},
                                 {"upserts", rows({row("M3XJ", "ready", "features")})},
                                 {"removed", QJsonArray{}}});
    QCOMPARE(view.model().total(), 2);
    view.handleEvent(QJsonObject{{"event", "board_changed"},
                                 {"upserts", rows({row("DN01", "inbox", "features")})},
                                 {"removed", QJsonArray{}}});
    QCOMPARE(view.model().total(), 3);
}

QTEST_MAIN(BoardModelTests)
#include "boardmodel_test.moc"
