// SPDX-License-Identifier: AGPL-3.0-or-later
// The Switchboard pane's pure logic: which section a card falls into, the filter language, the
// ordering, the row list the one scrolling view draws, and the `#` picker's ranking. No worker,
// no files, no network.
#include "BoardModel.h"

#include "Projects.h"
#include "BoardPane.h"
#include "BoardChat.h"   // the page agent's panel (#8YQ9, protocol 19.18)

#include <QApplication>
#include <QCheckBox>
#include <QFocusEvent>
#include <QFrame>
#include <QKeyEvent>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
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
    void timeSortsOrderEverySectionAlikeAndTheIdsRoundTrip();
    void theFilterLanguageMatchesEveryTerm();
    void theFilterHidesEmptySectionsAndUnfoldsTheRest();
    void searchRanksOpenCardsAndExactIdsFirst();
    void upsertAndRemoveKeepTheBoardInStep();
    void statusTitlesAreHumanReadable();
    void everySectionSaysWhatItIsFor();
    void theRowListIsHeadersThenCards();
    void badgesSayWhatTheCardCarries();
    void aRowDropsItsLeastImportantBadgesFirst();
    void theBodyLosesOnlyAHeadingThatRepeatsTheTitle();
    void threadEntriesSayHowLongAgo();
    void placementNamesTheNeighboursOfTheSlot();
    void aDropFindsTheSectionItLandedIn();
    void upAndDownWalkTheCardsAcrossSectionBreaks();
    void theViewRendersOneListFromAnEvent();
    void theViewSendsAMoveWhenACardIsDropped();
    void theColumnHeaderSortsTheListWithinASection();
    void aRowCarriesItsPriorityFlag();
    void theFlagHeaderSortsByPriority();
    void aFlagClickWritesBoardPriority();
    void labelChipsKeepOnlyTheCardsThatCarryThem();
    void theColumnsNameTheOrdersAClickGoesThrough();
    void arrowsFoldASectionAndTheFoldIsSaved();
    void aSectionCheckboxTakesItsSectionOffThePageAndTheCountSaysSo();
    void theListToolsSitOnTheListPageAndTheHeaderIsTheWayBack();
    void escOnTheMainPageGoesToTheFilterBar();
    void aRefusedWriteIsShownAndAnAcceptedOneCanBeUndone();
    void aChangeRefillsTheListInPlace();
    void theOpenCardRefetchesOnlyForItsOwnChanges();
    void aQuestionTheAgentCannotTakeIsReportedOnTheCard();
    void theThinkingTraceRunsInTheCardsThread();
    void quickAddNamesTheSectionItAddsTo();
    void aCleanupPreviewsFirstAndItsEventsNeverReachACardThread();
    void applyingAPreviewRunsTheCleanupForReal();
    void aCleanupAndACardsAskRefuseEachOther();
    void stoppingACleanupSendsCancelAndTheSummarySaysSo();
    void theProgressLineKeepsOffAnOpenCardsControls();
    void theTitleAndTheIssueAreEditedOnTheCardAndSavedThroughTheWorker();
    void anEditIsKeptWhenTheCardChangedUnderIt();
    void theBoxDiscussesAndTheRowPlansOrLeavesTheBoard();
    void aCardKeepsItsOwnTurnWhileAnotherCardIsOnScreen();
    void executeHandsTheCardToAPaneAndMovesItToInProgress();
    void theExecuteTaskCarriesTheBoardsConventions();
    void aRewriteShowsBeforeAboveTheOldTextAndAfterAboveTheNew();
    void aViewIgnoresEventsFromAnotherProjectsBoard();
    // Cross-provider QA (#T71W)
    void theVerifyLineNamesTheRecommendedVerifierAndWhatItSkipped();
    void theExecuteTaskAsksForTheImplementedByTrailer();
    void theVerifyTaskIsTheQaChecklistAndAsksForTheVerifiedByTrailer();
    void aQaLaneCardOffersVerifyOnTheRecommendedRunner();
    void aDoneCardWithASignatureSitsInVerifiedAndTheRestStayInDone();
    void aSignatureReadsAsItsModelAndItsHarness();
    void nothingIsMovedIntoVerifiedAndMovingOutIsOrdinary();
    void theBriefsAskForTheExactModelAndTheGuestHarness();
    void relayFreeSaysWhyItCannotVerifyAndAWeakPickWarns();
    // The Switchboard page agent's panel (#8YQ9, protocol 19.18)
    void thePanelSeedsItselfFromTheBoardEventsChatBlock();
    void aPromptSendsBoardChatAndASecondOneQueuesInsteadOfBeingRefused();
    void theQueueRowsRemoveAndReorderThroughTheWorker();
    void cleanUpMovedIntoThePageAgentsRowBesideCheck();
    void checkIsUnscopedAndASectionsTriageNamesItsSection();
    void aProblemDraftsAFixInTheComposerWithoutSendingIt();
    void theSurveysImportButtonSendsBoardImportApply();
    void theSurveyOffersToLookOnGithubAndThatLookWritesNothing();
    void theContextChipFollowsTheConversation();
    void thePageAgentsEventsNeverReachACardThread();
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
    // Verified is the one section no status names: a `done` card the worker signed `verified_by`
    // (#T71W). It is always there, between Needs QA and Done, whatever board.yaml says.
    QCOMPARE(sectionIds(model),
             (QStringList{"inbox", "discussing", "ready", "in-progress", "waiting", "needs-qa",
                          "verified", "done"}));
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
    QCOMPARE(sectionIds(model).mid(6), (QStringList{"deferred", "verified", "done"}));
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
    QCOMPARE(sectionIds(model).mid(6), (QStringList{"approved", "active", "verified", "done"}));
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

void BoardModelTests::timeSortsOrderEverySectionAlikeAndTheIdsRoundTrip()
{
    Model model;
    model.setConfig(config());
    QJsonObject a = row("AAA1", "ready", "features", "a"), b = row("BBB2", "ready", "features", "b"),
                c = row("CCC3", "ready", "features", "c");
    a.insert(QStringLiteral("created"), QStringLiteral("2026-09-01"));
    b.insert(QStringLiteral("created"), QStringLiteral("2026-09-10"));
    c.insert(QStringLiteral("created"), QStringLiteral("2026-09-05"));
    // `updated` is the file's mtime; an older worker sends none, and that card falls back to its
    // `created` (which sorts before that day's timestamps, so the two spellings mix).
    b.insert(QStringLiteral("updated"), QStringLiteral("2026-09-12T08:00:00Z"));
    c.insert(QStringLiteral("updated"), QStringLiteral("2026-09-11T08:00:00Z"));
    model.reset(rows({a, b, c}));

    const auto ids = [&model](relay::board::Sort sort) {
        model.setSort(sort);
        QStringList out;
        for (const Card &card : model.cards(QStringLiteral("ready")))
            out << card.id;
        return out;
    };
    QCOMPARE(ids(relay::board::Sort::Manual), (QStringList{"AAA1", "BBB2", "CCC3"}));
    QCOMPARE(ids(relay::board::Sort::NewestFirst), (QStringList{"BBB2", "CCC3", "AAA1"}));
    QCOMPARE(ids(relay::board::Sort::OldestFirst), (QStringList{"AAA1", "CCC3", "BBB2"}));
    QCOMPARE(ids(relay::board::Sort::RecentlyUpdated), (QStringList{"BBB2", "CCC3", "AAA1"}));
    QCOMPARE(ids(relay::board::Sort::OldestUpdated), (QStringList{"AAA1", "CCC3", "BBB2"}));
    // The card's own column, which is the title, and both ways round.
    QCOMPARE(ids(relay::board::Sort::TitleAsc), (QStringList{"AAA1", "BBB2", "CCC3"}));
    QCOMPARE(ids(relay::board::Sort::TitleDesc), (QStringList{"CCC3", "BBB2", "AAA1"}));

    // Done is newest first under Manual, as it always was, and follows the chosen sort otherwise.
    QJsonObject older = row("OLD1", "done", "features", "a"), newer = row("NEW1", "done", "features", "z");
    older.insert(QStringLiteral("created"), QStringLiteral("2026-01-01"));
    newer.insert(QStringLiteral("created"), QStringLiteral("2026-09-17"));
    Model closed;
    closed.setConfig(config());
    closed.reset(rows({older, newer}));
    const auto closedIds = [&closed](relay::board::Sort sort) {
        closed.setSort(sort);
        QStringList out;
        for (const Card &card : closed.cards(relay::board::doneSection()))
            out << card.id;
        return out;
    };
    QCOMPARE(closedIds(relay::board::Sort::Manual), (QStringList{"NEW1", "OLD1"}));
    QCOMPARE(closedIds(relay::board::Sort::OldestFirst), (QStringList{"OLD1", "NEW1"}));

    // The layout node's ids round-trip, and anything unknown reads as Manual.
    const QList<relay::board::Sort> sorts{relay::board::Sort::Manual, relay::board::Sort::NewestFirst,
                                          relay::board::Sort::OldestFirst,
                                          relay::board::Sort::RecentlyUpdated,
                                          relay::board::Sort::OldestUpdated,
                                          relay::board::Sort::TitleAsc,
                                          relay::board::Sort::TitleDesc};
    for (relay::board::Sort sort : sorts)
        QCOMPARE(relay::board::sortFromId(relay::board::sortId(sort)), sort);
    QCOMPARE(relay::board::sortFromId(QStringLiteral("nonsense")), relay::board::Sort::Manual);
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
    // Every spelling of the board folder is stripped off the path first (#JN7X, and the hidden
    // folder of 2026-09-19): `.switchboard/` on a board made now, `switchboard/` on one from
    // between the two decisions, `issues/` on one filed before either.
    QCOMPARE(card.folder(), QStringLiteral("changes"));
    for (const QString &folder : relay::projects::boardFolders()) {
        Card moved = card;
        moved.path = folder + QStringLiteral("/changes/2026-09-18-voice.md");
        QCOMPARE(moved.folder(), QStringLiteral("changes"));
        QVERIFY(Model::matches(moved, QStringLiteral("folder:changes")));
        Card privateCard = card;
        privateCard.path = folder + QStringLiteral("/.private/changes/2026-09-18-voice.md");
        QCOMPARE(privateCard.folder(), QStringLiteral("changes"));
        QVERIFY(Model::matches(privateCard, QStringLiteral("folder:changes")));
    }
    QVERIFY(Model::matches(card, QStringLiteral("@agent")));
    QVERIFY(!Model::matches(card, QStringLiteral("@dana")));
    QVERIFY(Model::matches(card, QStringLiteral("waiting:me")));
    QVERIFY(Model::matches(card, QStringLiteral("waiting:owner")));
    QVERIFY(!Model::matches(card, QStringLiteral("waiting:dana")));
    QVERIFY(Model::matches(card, QStringLiteral("#K7Q2")));
    QVERIFY(Model::matches(card, QStringLiteral("transcription")));
    QVERIFY(!Model::matches(card, QStringLiteral("clickable")));
    // Plain words are full-text search (owner, 2026-09-19: "switchboard filter bar should be
    // full text search"): the worker sends the card's body and thread as one `text` on the row,
    // and a word matches it as readily as the title.
    QVERIFY(!Model::matches(card, QStringLiteral("hotline")));
    card.text = QStringLiteral("## Issue\nadd voice transcribe mode\n- dana asked for a hotline");
    QVERIFY(Model::matches(card, QStringLiteral("hotline")));
    QVERIFY(Model::matches(card, QStringLiteral("HOTLINE")));
    QVERIFY(Model::matches(card, QStringLiteral("transcribe")));
    QVERIFY(!Model::matches(card, QStringLiteral("supercalifragilistic")));
    // Every term has to match, not just one.
    QVERIFY(Model::matches(card, QStringLiteral("label:voice @agent transcription")));
    QVERIFY(!Model::matches(card, QStringLiteral("label:voice @dana")));
    QVERIFY(Model::matches(card, QStringLiteral("label:voice hotline")));
    QVERIFY(!Model::matches(card, QStringLiteral("label:voice clickable")));
}

void BoardModelTests::theFilterHidesEmptySectionsAndUnfoldsTheRest()
{
    Model model;
    model.setConfig(config());
    QJsonObject tagged = row("K7Q2", "ready", "features");
    tagged.insert(QStringLiteral("labels"), QJsonArray{QStringLiteral("voice")});
    // The row's `text` is the card's whole body and thread as the worker sends it (19.2); a
    // plain word in it filters the list exactly like one in the title.
    tagged.insert(QStringLiteral("text"), QStringLiteral("## Issue\nthe composer eats bullets"));
    model.reset(rows({tagged, row("M3XJ", "inbox", "features"), row("DN01", "done", "features")}));
    QCOMPARE(model.openCount(), 2);

    // Unfiltered, every section keeps its header — it is a drop target and it says the lane
    // exists — and what the pane folded stays folded.
    const QSet<QString> folded{relay::board::doneSection()};
    QCOMPARE(sketch(model.rows(folded)),
             (QStringList{"# inbox 1", "M3XJ", "# discussing 0", "# ready 1", "K7Q2",
                          "# in-progress 0", "# waiting 0", "# needs-qa 0", "# verified 0",
                          "# done 1 folded"}));

    // Filtered, a section with no match gets out of the way, the counts follow, and nothing is
    // folded: a search that hid its own matches would be a search that does nothing.
    model.setFilter(QStringLiteral("label:voice"));
    QCOMPARE(model.openCount(), 1);
    QCOMPARE(sketch(model.rows(folded)), (QStringList{"# ready 1", "K7Q2"}));
    model.setFilter(QStringLiteral("composer"));
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
    // Not "Ready": on its own it was read as "ready to ship" (owner, 2026-09-19). The id is
    // still `ready`, so nothing on disk moved.
    QCOMPARE(relay::board::statusTitle(QStringLiteral("ready")), QStringLiteral("Ready to start"));
    QCOMPARE(relay::board::statusTitle(QStringLiteral("needs-qa-llm")), QStringLiteral("Needs QA (LLM)"));
    QCOMPARE(relay::board::statusTitle(QStringLiteral("brand-new")), QStringLiteral("Brand new"));
    QCOMPARE(relay::board::tabTitle(QStringLiteral("planning")), QStringLiteral("Plans"));
    QCOMPARE(relay::board::tabTitle(QStringLiteral("features")), QStringLiteral("Features"));
}

void BoardModelTests::everySectionSaysWhatItIsFor()
{
    using relay::board::sectionMeaning;
    // The question this answers, asked of the board itself (owner, 2026-09-19: "what does ready
    // mean? done or inbox?"): every section the pane can draw explains itself, so no header is
    // a word you have to guess at. A section with no meaning would be a silent hole in the
    // tooltip, which is exactly the state that prompted the question.
    Model model;
    model.setConfig(config());
    model.reset(rows({row("K7Q2", "needs-review", "features"), row("M3XJ", "deferred", "bugs"),
                      row("P9AB", "draft", "planning"), row("R4CD", "active", "memory")}));
    const QStringList drawn = sectionIds(model);
    QVERIFY(drawn.contains(QStringLiteral("ready")));
    QVERIFY(drawn.contains(QStringLiteral("verified")));
    for (const QString &id : drawn)
        QVERIFY2(!sectionMeaning(id).isEmpty(), qPrintable(id));
    // The meaning is a clause, not a sentence: it goes on one tooltip line under the header.
    QCOMPARE(sectionMeaning(QStringLiteral("ready")),
             QStringLiteral("agreed and not started — anyone may pick it up"));
    QVERIFY(!sectionMeaning(QStringLiteral("inbox")).isEmpty());
    for (const QString &id : drawn)
        QVERIFY2(!sectionMeaning(id).endsWith(QLatin1Char('.')), qPrintable(id));
    // A column a board invented gets no invented explanation.
    QVERIFY(sectionMeaning(QStringLiteral("triage-later")).isEmpty());
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
}

void BoardModelTests::aRowDropsItsLeastImportantBadgesFirst()
{
    using relay::board::Badge;
    using relay::board::fitBadges;
    const QList<QPair<Badge, int>> measured{
        {Badge{Badge::Label, QStringLiteral("voice")}, 40},
        {Badge{Badge::Waiting, QStringLiteral("waiting: owner")}, 80},
        {Badge{Badge::Tasks, QStringLiteral("☑ 1/3")}, 40},
        {Badge{Badge::Thread, QStringLiteral("✎ 4")}, 30}};
    const auto kept = [&](int available) {
        QStringList out;
        for (const Badge &badge : fitBadges(measured, available, 5))
            out << badge.text;
        return out;
    };
    // Everything fits: 40+80+40+30 plus three 5px gaps.
    QCOMPARE(kept(205), (QStringList{"voice", "waiting: owner", "☑ 1/3", "✎ 4"}));
    // Squeezed, the label goes first, then the thread count, then the tasks; `waiting:` is the
    // last to go, because it is why the row is being read.
    QCOMPARE(kept(160), (QStringList{"waiting: owner", "☑ 1/3", "✎ 4"}));
    QCOMPARE(kept(130), (QStringList{"waiting: owner", "☑ 1/3"}));
    QCOMPARE(kept(90), (QStringList{"waiting: owner"}));
    QCOMPARE(kept(10), QStringList());
    QVERIFY(fitBadges({}, 100, 5).isEmpty());
    QVERIFY(relay::board::badgeDropOrder(Badge::Label)
            < relay::board::badgeDropOrder(Badge::Waiting));
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
    // A new Switchboard is a compact overview: every header is present and every card starts
    // folded away.
    QCOMPARE(sketch(view.rows()),
             (QStringList{"# inbox 1 folded", "# discussing 0 folded", "# ready 1 folded",
                          "# in-progress 0 folded", "# waiting 0 folded", "# needs-qa 0 folded",
                          "# verified 0 folded",
                          "# done 1 folded"}));
    QCOMPARE(listOf(view)->count(), view.rows().size());

    view.handleEvent(QJsonObject{{"event", "board_changed"},
                                 {"upserts", rows({row("K7Q2", "ready", "features")})},
                                 {"removed", QJsonArray{QStringLiteral("M3XJ")}}});
    QCOMPARE(view.model().total(), 2);
    QCOMPARE(view.model().card(QStringLiteral("K7Q2"))->status, QStringLiteral("ready"));
    QVERIFY(view.rows().at(relay::board::rowOfSection(view.rows(), QStringLiteral("ready"))).collapsed);
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

// The list's column header is the sort (owner, 2026-09-19: "change switchboard sorting from a sort
// button to adding header columns that you click on ... and sorting is within section"): a click on
// a cell orders the cards inside every section by that column, a second click turns it round, a
// third gives the board its own drag order back.
void BoardModelTests::theColumnHeaderSortsTheListWithinASection()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    QJsonObject a = row("AAA1", "ready", "features", "a"), z = row("ZZZ9", "ready", "features", "z");
    a.insert(QStringLiteral("created"), QStringLiteral("2026-09-01"));
    z.insert(QStringLiteral("created"), QStringLiteral("2026-09-10"));
    // A second section, so what is tested is the order *within* a section, not the whole list.
    QJsonObject b = row("BBB2", "inbox", "features", "i");
    b.insert(QStringLiteral("created"), QStringLiteral("2026-09-20"));
    view.handleEvent(opened({a, z, b}));
    view.setCollapsedSections(QJsonArray{});   // unfold: the cards themselves are on the list

    QVERIFY(view.findChild<QWidget *>(QStringLiteral("boardColumnHeader")));
    auto *card = view.findChild<QToolButton *>(QStringLiteral("boardHeaderCard"));
    auto *created = view.findChild<QToolButton *>(QStringLiteral("boardHeaderCreated"));
    auto *updated = view.findChild<QToolButton *>(QStringLiteral("boardHeaderUpdated"));
    QVERIFY(card && created && updated);
    // The button that used to be the sort is gone, and with nothing sorted there is no arrow.
    QVERIFY(!view.findChild<QToolButton *>(QStringLiteral("boardSort")));
    QCOMPARE(view.sortOrder(), QStringLiteral("manual"));
    QVERIFY(!created->property("active").toBool());
    QCOMPARE(relay::board::cardsInSection(view.rows(), QStringLiteral("ready")),
             (QStringList{"AAA1", "ZZZ9"}));
    QVERIFY(created->toolTip().contains(QStringLiteral("newest first")));

    // Newest first, then oldest first, then back to the board's own order.
    created->click();
    QCOMPARE(view.sortOrder(), QStringLiteral("newest"));
    QCOMPARE(relay::board::cardsInSection(view.rows(), QStringLiteral("ready")),
             (QStringList{"ZZZ9", "AAA1"}));
    QVERIFY(created->property("active").toBool());
    QVERIFY(created->text().contains(QStringLiteral("▼")));
    created->click();
    QCOMPARE(view.sortOrder(), QStringLiteral("oldest"));
    QCOMPARE(relay::board::cardsInSection(view.rows(), QStringLiteral("ready")),
             (QStringList{"AAA1", "ZZZ9"}));
    QVERIFY(created->text().contains(QStringLiteral("▲")));
    created->click();
    QCOMPARE(view.sortOrder(), QStringLiteral("manual"));
    QVERIFY(!created->property("active").toBool());

    // A click on another column starts that column's own cycle rather than turning this one round,
    // and the card's own column sorts by title.
    created->click();                          // newest first again
    card->click();
    QCOMPARE(view.sortOrder(), QStringLiteral("title"));
    QVERIFY(card->text().contains(QStringLiteral("▲")));     // A→Z points up
    card->click();
    QCOMPARE(view.sortOrder(), QStringLiteral("title-desc"));
    QCOMPARE(relay::board::cardsInSection(view.rows(), QStringLiteral("ready")),
             (QStringList{"ZZZ9", "AAA1"}));
    updated->click();
    QCOMPARE(view.sortOrder(), QStringLiteral("updated"));
    // Neither card has an `updated`, so both fall back to their `created` and the order is the same
    // as Newest first's.
    QCOMPARE(relay::board::cardsInSection(view.rows(), QStringLiteral("ready")),
             (QStringList{"ZZZ9", "AAA1"}));

    // A column sort takes the manual reorder off: Alt+Shift+↑ is refused with a notice and nothing
    // is sent (a rank nobody can see is a rank nobody can write).
    view.selectCard(QStringLiteral("AAA1"));
    QTest::keyPress(listOf(view), Qt::Key_Up, Qt::AltModifier | Qt::ShiftModifier);
    QCOMPARE(sent.size(), 0);
    QVERIFY(view.notice().contains(QStringLiteral("sorted")));

    // Back on Manual the same key writes the rank again.
    updated->click();                          // updated -> updated-oldest
    updated->click();                          // updated-oldest -> manual
    QCOMPARE(view.sortOrder(), QStringLiteral("manual"));
    QTest::keyPress(listOf(view), Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier);
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value(QStringLiteral("type")).toString(), QStringLiteral("board_move"));
    QCOMPARE(sent.last().value(QStringLiteral("card")).toString(), QStringLiteral("AAA1"));
}

// The row's flag (#VKFV): −1…+3, 0 the default, and a value outside the range clamped on the way
// in — the worker clamps what it sends, and this side clamps again so a stale row cannot paint a
// colour that does not exist.
void BoardModelTests::aRowCarriesItsPriorityFlag()
{
    using relay::board::Card;
    QCOMPARE(Card::fromJson(row("K7Q2", "ready", "features")).priority, 0);
    QJsonObject flagged = row("K7Q2", "ready", "features");
    flagged.insert(QStringLiteral("priority"), -1);
    QCOMPARE(Card::fromJson(flagged).priority, -1);
    flagged.insert(QStringLiteral("priority"), 3);
    QCOMPARE(Card::fromJson(flagged).priority, 3);
    flagged.insert(QStringLiteral("priority"), 9);
    QCOMPARE(Card::fromJson(flagged).priority, 3);
    flagged.insert(QStringLiteral("priority"), -9);
    QCOMPARE(Card::fromJson(flagged).priority, -1);
}

// The ⚑ cell over the flag column orders the flags high first, then low first, then hands the
// list back to the board's own order — the same cycle every other column walks.
void BoardModelTests::theFlagHeaderSortsByPriority()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.onSend = [](const QJsonObject &) {};
    QJsonObject none = row("AAA1", "ready", "features", "i"), low = row("BBB2", "ready", "features", "j"),
                high = row("CCC3", "ready", "features", "k"), mid = row("DDD4", "ready", "features", "l");
    low.insert(QStringLiteral("priority"), -1);
    high.insert(QStringLiteral("priority"), 3);
    mid.insert(QStringLiteral("priority"), 1);
    view.handleEvent(opened({none, low, high, mid}));
    view.setCollapsedSections(QJsonArray{});

    auto *flag = view.findChild<QToolButton *>(QStringLiteral("boardHeaderPriority"));
    QVERIFY(flag);
    QCOMPARE(view.sortOrder(), QStringLiteral("manual"));
    QCOMPARE(relay::board::cardsInSection(view.rows(), QStringLiteral("ready")),
             (QStringList{"AAA1", "BBB2", "CCC3", "DDD4"}));
    // One glyph, no arrow: the accent alone says the priority sort is on.
    QVERIFY(!flag->property("active").toBool());

    flag->click();
    QCOMPARE(view.sortOrder(), QStringLiteral("priority"));
    QCOMPARE(relay::board::cardsInSection(view.rows(), QStringLiteral("ready")),
             (QStringList{"CCC3", "DDD4", "AAA1", "BBB2"}));   // +3, +1, 0, −1
    QVERIFY(flag->property("active").toBool());
    flag->click();
    QCOMPARE(view.sortOrder(), QStringLiteral("priority-low"));
    QCOMPARE(relay::board::cardsInSection(view.rows(), QStringLiteral("ready")),
             (QStringList{"BBB2", "AAA1", "DDD4", "CCC3"}));   // −1, 0, +1, +3
    flag->click();
    QCOMPARE(view.sortOrder(), QStringLiteral("manual"));
}

// A click on the flag writes `board_priority` (#VKFV): no base_hash — the whole patch is one
// clamped integer — and the row moves at once, before the worker answers.
void BoardModelTests::aFlagClickWritesBoardPriority()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("AAA1", "ready", "features")}));
    view.setCollapsedSections(QJsonArray{});

    view.setCardPriority(QStringLiteral("AAA1"), +1);
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value(QStringLiteral("type")).toString(), QStringLiteral("board_priority"));
    QCOMPARE(sent.last().value(QStringLiteral("card")).toString(), QStringLiteral("AAA1"));
    QCOMPARE(sent.last().value(QStringLiteral("priority")).toInt(), 1);
    QCOMPARE(view.model().card(QStringLiteral("AAA1"))->priority, 1);   // optimistic

    // Two more raises clamp at +3, and lowering walks back down to −1 and stops there.
    view.setCardPriority(QStringLiteral("AAA1"), +1);
    view.setCardPriority(QStringLiteral("AAA1"), +1);
    view.setCardPriority(QStringLiteral("AAA1"), +1);
    QCOMPARE(sent.last().value(QStringLiteral("priority")).toInt(), 3);
    for (int i = 0; i < 6; ++i)
        view.setCardPriority(QStringLiteral("AAA1"), -1);
    QCOMPARE(sent.last().value(QStringLiteral("priority")).toInt(), -1);
    QCOMPARE(view.model().card(QStringLiteral("AAA1"))->priority, -1);
}

// The label chips beside the section checkboxes (#VKFV): a card is on the page only when it
// carries every ticked label, composing with the text filter the way `label:` terms do.
void BoardModelTests::labelChipsKeepOnlyTheCardsThatCarryThem()
{
    Model model;
    model.setConfig(config());
    QJsonObject voice = row("K7Q2", "ready", "features");
    voice.insert(QStringLiteral("labels"), QJsonArray{QStringLiteral("voice"), QStringLiteral("gui")});
    QJsonObject gui = row("M3XJ", "inbox", "features");
    gui.insert(QStringLiteral("labels"), QJsonArray{QStringLiteral("gui")});
    model.reset(rows({voice, gui, row("DN01", "inbox", "features")}));
    QCOMPARE(model.openCount(), 3);

    const QSet<QString> folded;
    model.setLabelFilter({QStringLiteral("gui")});
    QCOMPARE(model.openCount(), 2);
    QCOMPARE(relay::board::cardsInSection(model.rows(folded), QStringLiteral("inbox")),
             (QStringList{"M3XJ"}));
    // Ticking a second label is an AND, like a second `label:` term.
    model.setLabelFilter({QStringLiteral("gui"), QStringLiteral("voice")});
    QCOMPARE(model.openCount(), 1);
    QCOMPARE(relay::board::cardsInSection(model.rows(folded), QStringLiteral("ready")),
             (QStringList{"K7Q2"}));
    // And it composes with the words: the filter box narrows inside what the chips kept.
    model.setFilter(QStringLiteral("inbox"));
    QCOMPARE(model.openCount(), 0);
    QVERIFY(model.rows(folded).isEmpty());
    model.setFilter(QString());
    // A label nothing carries filters everything out, and unticking everything brings it back.
    model.setLabelFilter({QStringLiteral("remote")});
    QCOMPARE(model.openCount(), 0);
    model.setLabelFilter({});
    QCOMPARE(model.openCount(), 3);
}

// What each column's header is and what a click on it goes through: the pure half of the header, so
// the cycle back to the board's own drag order is pinned down without a widget.
void BoardModelTests::theColumnsNameTheOrdersAClickGoesThrough()
{
    using namespace relay::board;
    QCOMPARE(columnTitle(SortColumn::Priority), QStringLiteral("⚑"));
    QCOMPARE(columnTitle(SortColumn::Card), QStringLiteral("Card"));
    QCOMPARE(columnTitle(SortColumn::Created), QStringLiteral("Created"));
    QCOMPARE(columnTitle(SortColumn::Updated), QStringLiteral("Updated"));

    QCOMPARE(nextColumnSort(SortColumn::Created, Sort::Manual), Sort::NewestFirst);
    QCOMPARE(nextColumnSort(SortColumn::Created, Sort::NewestFirst), Sort::OldestFirst);
    QCOMPARE(nextColumnSort(SortColumn::Created, Sort::OldestFirst), Sort::Manual);
    QCOMPARE(nextColumnSort(SortColumn::Updated, Sort::Manual), Sort::RecentlyUpdated);
    QCOMPARE(nextColumnSort(SortColumn::Updated, Sort::RecentlyUpdated), Sort::OldestUpdated);
    QCOMPARE(nextColumnSort(SortColumn::Updated, Sort::OldestUpdated), Sort::Manual);
    QCOMPARE(nextColumnSort(SortColumn::Card, Sort::Manual), Sort::TitleAsc);
    QCOMPARE(nextColumnSort(SortColumn::Card, Sort::TitleAsc), Sort::TitleDesc);
    QCOMPARE(nextColumnSort(SortColumn::Card, Sort::TitleDesc), Sort::Manual);
    // The flag's own cycle (#VKFV): high first, then low first, then the board's own order.
    QCOMPARE(nextColumnSort(SortColumn::Priority, Sort::Manual), Sort::PriorityHigh);
    QCOMPARE(nextColumnSort(SortColumn::Priority, Sort::PriorityHigh), Sort::PriorityLow);
    QCOMPARE(nextColumnSort(SortColumn::Priority, Sort::PriorityLow), Sort::Manual);
    QCOMPARE(sortId(Sort::PriorityHigh), QStringLiteral("priority"));
    QCOMPARE(sortId(Sort::PriorityLow), QStringLiteral("priority-low"));
    QCOMPARE(sortFromId(QStringLiteral("priority")), Sort::PriorityHigh);
    QCOMPARE(sortFromId(QStringLiteral("priority-low")), Sort::PriorityLow);
    // A click on a column that is not the one sorting starts that column's own cycle.
    QCOMPARE(nextColumnSort(SortColumn::Card, Sort::RecentlyUpdated), Sort::TitleAsc);
    QCOMPARE(nextColumnSort(SortColumn::Created, Sort::TitleDesc), Sort::NewestFirst);

    // The flag column sits left of Card, so every other column's index moved one to the right.
    QCOMPARE(sortColumnIndex(Sort::Manual), -1);
    QCOMPARE(sortColumnIndex(Sort::PriorityHigh), 0);
    QCOMPARE(sortColumnIndex(Sort::PriorityLow), 0);
    QCOMPARE(sortColumnIndex(Sort::TitleDesc), 1);
    QCOMPARE(sortColumnIndex(Sort::OldestFirst), 2);
    QCOMPARE(sortColumnIndex(Sort::OldestUpdated), 3);
    QVERIFY(sortAscending(Sort::OldestFirst));
    QVERIFY(sortAscending(Sort::OldestUpdated));
    QVERIFY(sortAscending(Sort::TitleAsc));
    QVERIFY(sortAscending(Sort::PriorityLow));
    QVERIFY(!sortAscending(Sort::NewestFirst));
    QVERIFY(!sortAscending(Sort::RecentlyUpdated));
    QVERIFY(!sortAscending(Sort::PriorityHigh));
    QVERIFY(!sortAscending(Sort::Manual));

    // A date column shows the date part of either spelling the worker sends, and nothing at all for
    // a field that holds nothing date-like.
    QCOMPARE(dateCell(QStringLiteral("2026-09-19")), QStringLiteral("2026-09-19"));
    QCOMPARE(dateCell(QStringLiteral("2026-09-19T21:13:58Z")), QStringLiteral("2026-09-19"));
    QVERIFY(dateCell(QString()).isEmpty());
    QVERIFY(dateCell(QStringLiteral("someday")).isEmpty());
    QVERIFY(dateCell(QStringLiteral("2026-9-19")).isEmpty());
    // The date part of a longer stamp, which is what an ISO timestamp is.
    QCOMPARE(dateCell(QStringLiteral("2026-09-19-ish")), QStringLiteral("2026-09-19"));
}

void BoardModelTests::arrowsFoldASectionAndTheFoldIsSaved()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    // This test exercises folding from an expanded restored layout; brand-new panes start with
    // every section folded.
    view.setCollapsedSections(QJsonArray{});
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
    QCOMPARE(folded, (QStringList{"inbox"}));
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
    view.setCollapsedSections(QJsonArray{});
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
    QCOMPARE(back->text(), QStringLiteral("←  Back to board (Esc)"));
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

// Esc is the way to the filter bar from anywhere on the main page (#K9X6). An active filter
// comes off first — the same first Esc as inside the box — and once there is nothing to undo
// the bar itself takes the focus, so the key always does one visible thing. A card still goes
// back first, and inside the box Esc still hands the keyboard to the list.
void BoardModelTests::escOnTheMainPageGoesToTheFilterBar()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.setCollapsedSections(QJsonArray{});
    view.handleEvent(opened({row("K7Q2", "ready", "features")}));

    QListWidget *list = listOf(view);
    QLineEdit *filter = view.findChild<QLineEdit *>(QStringLiteral("boardFilter"));
    QVERIFY(list);
    QVERIFY(filter);

    // From the list, and from the view itself (where an empty board leaves the keys), Esc
    // lands in the filter bar: the rule is the page's, not the list's.
    QTest::keyClick(list, Qt::Key_Escape);
    QCOMPARE(view.focusWidget(), static_cast<QWidget *>(filter));
    list->setFocus();
    QTest::keyClick(&view, Qt::Key_Escape);
    QCOMPARE(view.focusWidget(), static_cast<QWidget *>(filter));

    // An active filter comes off first — its matches come back — and the keyboard stays
    // where it was, ready to walk the unfiltered list.
    filter->setText(QStringLiteral("zzz"));
    QCOMPARE(relay::board::rowOfCard(view.rows(), QStringLiteral("K7Q2")), -1);
    list->setFocus();
    QTest::keyClick(list, Qt::Key_Escape);
    QCOMPARE(filter->text(), QString());
    QVERIFY(relay::board::rowOfCard(view.rows(), QStringLiteral("K7Q2")) >= 0);

    // Inside the box Esc keeps its two old steps: the text off, then the keyboard to the list.
    filter->setText(QStringLiteral("zzz"));
    QTest::keyClick(filter, Qt::Key_Escape);
    QCOMPARE(filter->text(), QString());
    QTest::keyClick(filter, Qt::Key_Escape);
    QCOMPARE(view.focusWidget(), static_cast<QWidget *>(list));

    // A card that has the pane goes back first; the next Esc is the filter bar.
    view.handleEvent(QJsonObject{{"event", "board_card"}, {"card_id", "K7Q2"},
                                 {"title", "K7Q2 card"}, {"status", "ready"}, {"tab", "features"},
                                 {"body", "text"}, {"thread", QJsonArray{}}, {"thread_total", 0}});
    QVERIFY(view.detailOpen());
    QTest::keyClick(&view, Qt::Key_Escape);
    QVERIFY(!view.detailOpen());
    QTest::keyClick(&view, Qt::Key_Escape);
    QCOMPARE(view.focusWidget(), static_cast<QWidget *>(filter));

    // Clicking into the box is the mouse way there, so that is what the hint names — and a
    // focus the keyboard brought (`/`, Tab) is already the fast path and says nothing.
    QString hinted;
    view.onHint = [&hinted](const QString &id, const QString &keys) {
        hinted = id + QLatin1Char(':') + keys;
    };
    QApplication::sendEvent(filter, new QFocusEvent(QEvent::FocusIn, Qt::MouseFocusReason));
    QCOMPARE(hinted, QStringLiteral("board.filter:Esc"));
    QApplication::sendEvent(filter, new QFocusEvent(QEvent::FocusIn, Qt::OtherFocusReason));
    QCOMPARE(hinted, QStringLiteral("board.filter:Esc"));
}

// ---- the view's answers to the worker ------------------------------------------------------

void BoardModelTests::aRefusedWriteIsShownAndAnAcceptedOneCanBeUndone()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.setCollapsedSections(QJsonArray{});
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
    view.setCollapsedSections(QJsonArray{});
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

// #9K5H: a Discuss or Plan turn's reasoning streams in the card's thread — the same words the
// terminal's fold uses — and is sealed where a thread entry lands under it, so a question the
// agent asks mid-turn reads after the thinking it came from. It is a live view: never written
// to the card file, gone when the card is left and back when it is reopened while the turn runs.
void BoardModelTests::theThinkingTraceRunsInTheCardsThread()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    const auto cardArrived = [](const QString &id) {
        return QJsonObject{{"event", "board_card"}, {"card_id", id}, {"title", id + QStringLiteral(" card")},
                           {"status", "inbox"}, {"tab", "features"}, {"body", "text"},
                           {"thread", QJsonArray{}}, {"thread_total", 0}};
    };
    view.handleEvent(cardArrived(QStringLiteral("K7Q2")));
    auto *doc = view.findChild<QTextBrowser *>(QStringLiteral("boardCardDocument"));
    QVERIFY(doc);
    const auto text = [doc] { return doc->toPlainText(); };

    auto *reply = view.findChild<QPlainTextEdit *>(QStringLiteral("boardReplyEditor"));
    QVERIFY(reply);
    reply->setPlainText(QStringLiteral("Plan the trace."));
    QTest::keyClick(reply, Qt::Key_Return);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_ask"));

    // The trace streams under the turn's own header, before any answer.
    view.handleEvent(QJsonObject{{"event", "thinking_delta"}, {"card_id", "K7Q2"}, {"turn_id", "t-1"},
                                 {"text", QStringLiteral("The card asks for the trace in the thread.\n")}});
    QTest::qWait(120);   // the render is coalesced on the 40 ms timer
    QVERIFY(text().contains(QStringLiteral("\u2726 thinking\u2026")));
    QVERIFY(text().contains(QStringLiteral("The card asks for the trace in the thread.")));

    // The block settles to the fold's own words, with its seconds.
    view.handleEvent(QJsonObject{{"event", "thinking_done"}, {"card_id", "K7Q2"}, {"turn_id", "t-1"},
                                 {"elapsed_ms", 4200}});
    QTest::qWait(120);
    QVERIFY(text().contains(QStringLiteral("\u2726 thought for 4 s")));
    QVERIFY(!text().contains(QStringLiteral("thinking\u2026")));

    // A question the agent asks mid-turn lands *after* the thinking it came from, and the trace
    // stays sealed above it rather than following the turn to the bottom of the thread.
    view.handleEvent(QJsonObject{{"event", "board_thread_appended"}, {"card_id", "K7Q2"},
                                 {"entry_id", "20260919T210001Z-aa"}, {"author", "agent"},
                                 {"kind", "question"},
                                 {"text", QStringLiteral("1. Should the trace reach the file?")}});
    QVERIFY(text().contains(QStringLiteral("Should the trace reach the file?")));
    QVERIFY(text().indexOf(QStringLiteral("\u2726 thought for 4 s"))
            < text().indexOf(QStringLiteral("Should the trace reach the file?")));

    // The answer streams under the trace, and leaving the card and coming back finds both.
    view.handleEvent(QJsonObject{{"event", "delta"}, {"card_id", "K7Q2"}, {"turn_id", "t-1"},
                                 {"text", QStringLiteral("The plan: render it in the thread.")}});
    QTest::qWait(120);
    view.handleEvent(cardArrived(QStringLiteral("M3XJ")));   // another card, no turn on it
    QVERIFY(!text().contains(QStringLiteral("\u2726 thought for 4 s")));
    QVERIFY(!text().contains(QStringLiteral("The plan: render it in the thread.")));
    auto *strip = view.findChild<QWidget *>(QStringLiteral("boardBusyStrip"));
    QVERIFY(strip && strip->isHidden());
    view.handleEvent(cardArrived(QStringLiteral("K7Q2")));
    QVERIFY(strip && !strip->isHidden());
    QVERIFY(text().contains(QStringLiteral("\u2726 thought for 4 s")));
    QVERIFY(text().contains(QStringLiteral("The plan: render it in the thread.")));

    // The answer lands as the thread's own entry and the turn ends; the sealed trace stays.
    view.handleEvent(QJsonObject{{"event", "board_thread_appended"}, {"card_id", "K7Q2"},
                                 {"entry_id", "20260919T210002Z-bb"}, {"author", "agent"},
                                 {"kind", "comment"},
                                 {"text", QStringLiteral("The plan: render it in the thread.")}});
    view.handleEvent(QJsonObject{{"event", "done"}, {"card_id", "K7Q2"}, {"turn_id", "t-1"}});
    QVERIFY(strip && strip->isHidden());
    QVERIFY(text().contains(QStringLiteral("\u2726 thought for 4 s")));
    QVERIFY(text().indexOf(QStringLiteral("\u2726 thought for 4 s"))
            < text().lastIndexOf(QStringLiteral("The plan: render it in the thread.")));
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
    view.setCollapsedSections(QJsonArray{});
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "ready", "features")}));
    auto *field = view.findChild<QLineEdit *>(QStringLiteral("boardQuickAdd"));
    auto *strip = view.findChild<QWidget *>(QStringLiteral("boardQuickAddRow"));
    QVERIFY(field);
    QVERIFY(strip);
    QVERIFY(strip->isHidden());

    // `n` with a card selected still adds in Inbox (owner, #5G43): the selection must not pick
    // the section, or a card selected in In progress made every new card start there.
    view.selectCard(QStringLiteral("K7Q2"));
    view.quickAdd();
    QVERIFY(!strip->isHidden());
    QCOMPARE(field->placeholderText(),
             QStringLiteral("Title of a new card in Inbox — Enter opens it, Esc closes"));
    field->setText(QStringLiteral("clickable paths in the output"));
    QTest::keyClick(field, Qt::Key_Return);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_create"));
    QCOMPARE(sent.last().value("status").toString(), QStringLiteral("inbox"));
    QCOMPARE(sent.last().value("text").toString(), QStringLiteral("clickable paths in the output"));
    // With no tabs a new card is filed in the board's first category folder; `m` re-files it.
    QCOMPARE(sent.last().value("tab").toString(), QStringLiteral("features"));

    // Owner, #VZ69: "when you first press enter to add a new card, it should open the edit box,
    // the editable issue part. the first thing you enter in teh top row thing makes the title,
    // not the issue content." So the write comes back, the field closes, and the card is asked
    // for; when it arrives it is already being edited, with the cursor in the issue box.
    const QString createId = sent.last().value("id").toString();
    view.handleEvent(QJsonObject{{"event", "board_written"}, {"id", createId},
                                 {"kind", "board_create"}, {"card_id", "N3W1"},
                                 {"write_id", "w1"}});
    QVERIFY(strip->isHidden());
    QCOMPARE(view.selectedCard(), QStringLiteral("N3W1"));
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_card_get"));
    QCOMPARE(sent.last().value("card").toString(), QStringLiteral("N3W1"));
    // The worker seeds the new card's `## Issue` with the line that was typed, because that line
    // is the owner's own words and the card format keeps them verbatim.
    view.handleEvent(card("N3W1", "clickable paths in the output",
                          "clickable paths in the output", "h1"));
    auto *issue = view.findChild<QPlainTextEdit *>(QStringLiteral("boardIssueEditor"));
    auto *titleField = view.findChild<QLineEdit *>(QStringLiteral("boardCardTitleEdit"));
    QVERIFY(issue);
    QVERIFY(!issue->isHidden());
    // The cursor is in the issue box, not the title: the title is already typed. (`hasFocus()`
    // asks the window system, which an offscreen test has none of; `focusWidget()` is the same
    // question asked of the widget tree.)
    QCOMPARE(view.focusWidget(), static_cast<QWidget *>(issue));
    QCOMPARE(titleField->text(), QStringLiteral("clickable paths in the output"));
    // And that line is offered *selected*: it is the title, not the issue (owner, #VZ69), so the
    // first thing typed replaces it — while Esc or an empty save leaves the card as the field
    // made it, rather than blanking the only words the card has.
    QCOMPARE(issue->toPlainText(), QStringLiteral("clickable paths in the output"));
    QCOMPARE(issue->textCursor().selectedText(), QStringLiteral("clickable paths in the output"));
    // The reply box stands down while the card is being written.
    QVERIFY(view.findChild<QFrame *>(QStringLiteral("boardReply"))->isHidden());

    // A section's own + is the one path that names a section: it adds straight into that one.
    view.closeDetail();
    view.quickAddIn(QStringLiteral("ready"));
    QCOMPARE(field->placeholderText(),
             QStringLiteral("Title of a new card in Ready to start — Enter opens it, Esc closes"));
    // Nothing is created straight into Done: `n` there falls back to the first section.
    QTest::keyClick(field, Qt::Key_Escape);
    view.quickAddIn(relay::board::doneSection());
    QCOMPARE(field->placeholderText(),
             QStringLiteral("Title of a new card in Inbox — Enter opens it, Esc closes"));
    QTest::keyClick(field, Qt::Key_Escape);
    QVERIFY(strip->isHidden());
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

    // The way in by mouse is a pencil on the title itself, not a text button among the card's
    // tools (owner, #VZ69). It carries its key like every other button here (#QG60), and it goes
    // quiet while the editor it opened is up.
    auto *pencil = view.findChild<QToolButton *>(QStringLiteral("boardEditPencil"));
    QVERIFY(pencil);
    QCOMPARE(pencil->text(), QStringLiteral("✎ Edit (e)"));
    QVERIFY(pencil->isEnabled());
    pencil->click();
    QVERIFY(!title->isHidden());
    QVERIFY(!pencil->isEnabled());
    QTest::keyClick(issue, Qt::Key_Escape);
    QVERIFY(title->isHidden());
    QVERIFY(pencil->isEnabled());

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
        // Buttons carry their shortcut in parentheses (#QG60): "Execute (x)". A query matches
        // a bare label ("Stop", while a run is going) or the suffixed one.
        if (candidate->text() == text || candidate->text().startsWith(text + QStringLiteral(" (")))
            return candidate;
    return nullptr;
}
}  // namespace

// Owner, #VZ69: "remove comment / discuss buttons. i would say you just press enter in the prompt
// box to discuss / comment" and "'stop' button isnt intuitive, it should be stop planning i guess,
// or there should be an X next to 'agent planning'".
void BoardModelTests::theBoxDiscussesAndTheRowPlansOrLeavesTheBoard()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    view.handleEvent(card("K7Q2", "K7Q2 card", "the issue", "h1"));
    // Discuss and Comment are what the box does, so they have no buttons; what is left on the row
    // is Plan and the two that leave the board.
    QVERIFY(!button(view, QStringLiteral("Discuss")));
    QVERIFY(!button(view, QStringLiteral("Comment")));
    QVERIFY(button(view, QStringLiteral("Plan")));
    QVERIFY(button(view, QStringLiteral("Execute")));
    QVERIFY(!button(view, QStringLiteral("Ask the agent")));
    // Every button that has a key shows it in parentheses (#QG60).
    QCOMPARE(button(view, QStringLiteral("Plan"))->text(), QStringLiteral("Plan (p)"));
    QCOMPARE(button(view, QStringLiteral("Execute"))->text(), QStringLiteral("Execute (x)"));
    auto *strip = view.findChild<QWidget *>(QStringLiteral("boardBusyStrip"));
    auto *busy = view.findChild<QLabel *>(QStringLiteral("boardBusyLabel"));
    auto *stop = view.findChild<QToolButton *>(QStringLiteral("boardStop"));
    QVERIFY(strip);
    QVERIFY(strip->isHidden());

    // Enter in the reply box discusses.
    auto *reply = view.findChild<QPlainTextEdit *>(QStringLiteral("boardReplyEditor"));
    reply->setPlainText(QStringLiteral("Is this still wanted?"));
    QTest::keyClick(reply, Qt::Key_Return);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_ask"));
    QCOMPARE(sent.last().value("mode").toString(), QStringLiteral("discuss"));
    QCOMPARE(sent.last().value("text").toString(), QStringLiteral("Is this still wanted?"));
    // While it runs, the strip over the box says which turn it is and carries the one control
    // that ends it; the buttons that would start another turn wait.
    QVERIFY(!strip->isHidden());
    QCOMPARE(busy->text(), QStringLiteral("✦ Switchboarding · discussing…"));
    QCOMPARE(stop->text(), QStringLiteral("✕ Stop discussing"));
    QVERIFY(!button(view, QStringLiteral("Plan"))->isEnabled());
    QVERIFY(!button(view, QStringLiteral("Execute"))->isEnabled());
    view.handleEvent(QJsonObject{{"event", "done"}, {"card_id", "K7Q2"}, {"mode", "discuss"}});
    QVERIFY(strip->isHidden());
    QVERIFY(button(view, QStringLiteral("Plan"))->isEnabled());

    // `p` plans with an empty box: no text travels, the strip names the plan, and its ✕ cancels.
    sent.clear();
    view.cardAction(QStringLiteral("plan"));
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value("mode").toString(), QStringLiteral("plan"));
    QVERIFY(!sent.last().contains("text"));
    QVERIFY(!strip->isHidden());
    QCOMPARE(busy->text(), QStringLiteral("✦ Switchboarding · planning…"));
    QCOMPARE(stop->text(), QStringLiteral("✕ Stop planning"));
    QVERIFY(!button(view, QStringLiteral("Plan"))->isEnabled());
    stop->click();
    // It stops *this card's* turn by name (protocol 19.16): the worker-wide `cancel` would stop
    // whichever turn the worker's own agent is running, which is a cleanup, and would leave the
    // other cards' turns going.
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_cancel"));
    QCOMPARE(sent.last().value("card").toString(), QStringLiteral("K7Q2"));
    view.handleEvent(QJsonObject{{"event", "cancelled"}, {"card_id", "K7Q2"}});
    QVERIFY(strip->isHidden());

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
    auto *browser = view.findChild<QTextBrowser *>(QStringLiteral("boardCardDocument"));
    const QString doc = browser->toPlainText();
    QVERIFY2(doc.contains(QStringLiteral("owner  Plan")), qPrintable(doc));
    QVERIFY2(doc.contains(QStringLiteral("✦ agent  Discuss · glm-5")), qPrintable(doc));

    // The card's own words and the conversation about them are two surfaces, not one column of
    // text with a louder line in it (owner, #VZ69: "there should be a clearer dematcation between
    // the issue and the convo thread"): the thread opens on a rule the width of the document, and
    // its heading has a ground of its own.
    QTextBlock band, rule;
    for (QTextBlock block = browser->document()->begin(); block.isValid(); block = block.next())
        if (block.text().startsWith(QStringLiteral("THREAD"))) {
            band = block;
            rule = block.previous();
        }
    QVERIFY(band.isValid());
    QVERIFY(band.blockFormat().background().style() != Qt::NoBrush);
    QVERIFY(rule.isValid());
    QVERIFY(rule.text().isEmpty());
    QVERIFY(rule.blockFormat().background().style() != Qt::NoBrush);
    QCOMPARE(rule.blockFormat().lineHeightType(), int(QTextBlockFormat::FixedHeight));
    QCOMPARE(relay::board::modeTitle(QStringLiteral("plan")), QStringLiteral("Plan"));
    QVERIFY(relay::board::modeTitle(QStringLiteral("comment")).isEmpty());
}


// Protocol 19.16: turns run per card, so the board can be showing #A while #B is planning. Each
// card keeps its own strip, its own answer so far and its own progress line; a `done` for one
// card does not end the other's turn, and the list marks the cards that are working.
void BoardModelTests::aCardKeepsItsOwnTurnWhileAnotherCardIsOnScreen()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features"), row("M3XJ", "inbox", "features")}));
    auto *strip = view.findChild<QWidget *>(QStringLiteral("boardBusyStrip"));
    auto *busy = view.findChild<QLabel *>(QStringLiteral("boardBusyLabel"));
    auto *what = view.findChild<QLabel *>(QStringLiteral("boardBusyWhat"));
    QVERIFY(strip && busy && what);

    // Plan #K7Q2, and watch it read the repository: the strip says what it is doing, which is
    // what a Plan does for minutes before it says a word.
    view.handleEvent(card("K7Q2", "K7Q2 card", "the issue", "h1"));
    view.cardAction(QStringLiteral("plan"));
    QCOMPARE(sent.last().value("mode").toString(), QStringLiteral("plan"));
    view.handleEvent(QJsonObject{{"event", "status"}, {"card_id", "K7Q2"}, {"mode", "plan"},
                                 {"text", "Requesting model · step 4/256"}});
    QVERIFY(!strip->isHidden());
    QCOMPARE(what->toolTip(), QStringLiteral("Requesting model · step 4/256"));
    view.handleEvent(QJsonObject{{"event", "delta"}, {"card_id", "K7Q2"}, {"mode", "plan"},
                                 {"text", "Reading the completion code."}});

    // Open #M3XJ while that one runs: this card is idle, and its own Plan is offered.
    view.handleEvent(card("M3XJ", "M3XJ card", "another issue", "h2"));
    QVERIFY(strip->isHidden());
    QVERIFY(button(view, QStringLiteral("Plan"))->isEnabled());
    view.cardAction(QStringLiteral("plan"));
    QCOMPARE(sent.last().value("card").toString(), QStringLiteral("M3XJ"));
    QCOMPARE(busy->text(), QStringLiteral("✦ Switchboarding · planning…"));

    // #K7Q2 finishing does not end the turn on the card being shown.
    view.handleEvent(QJsonObject{{"event", "done"}, {"card_id", "K7Q2"}, {"mode", "plan"}});
    QVERIFY(!strip->isHidden());

    // Back to #K7Q2: it finished while it was off screen, so it is idle again — and its answer
    // is in the thread, which the worker appended.
    view.handleEvent(card("K7Q2", "K7Q2 card", "the issue", "h1"));
    QVERIFY(strip->isHidden());

    // Back to #M3XJ: still planning, and the strip has its answer so far and its step back.
    view.handleEvent(QJsonObject{{"event", "status"}, {"card_id", "M3XJ"}, {"mode", "plan"},
                                 {"text", "Requesting model · step 2/256"}});
    view.handleEvent(QJsonObject{{"event", "delta"}, {"card_id", "M3XJ"}, {"mode", "plan"},
                                 {"text", "Looking at the header."}});
    view.handleEvent(card("M3XJ", "M3XJ card", "another issue", "h2"));
    QVERIFY(!strip->isHidden());
    QCOMPARE(busy->text(), QStringLiteral("✦ Switchboarding · planning…"));
    QCOMPARE(what->toolTip(), QStringLiteral("Requesting model · step 2/256"));

    // A fourth card refused while three run says which cards are working, and keeps the text.
    auto *reply = view.findChild<QPlainTextEdit *>(QStringLiteral("boardReplyEditor"));
    reply->setPlainText(QStringLiteral("what about this one?"));
    QTest::keyClick(reply, Qt::Key_Return);
    view.handleEvent(QJsonObject{{"event", "error"}, {"code", "board_busy"},
                                 {"cleanup_running", false}, {"card_id", "K7Q2"},
                                 {"cards", QJsonArray{"K7Q2", "R4TT"}},
                                 {"text", "The Switchboard agent is busy with turns on #K7Q2 and #R4TT."}});
    QCOMPARE(reply->toPlainText(), QStringLiteral("what about this one?"));
    QVERIFY(strip->isHidden());
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

// ---- cross-provider QA (#T71W) ---------------------------------------------------------------
//
// The worker puts a `qa` object on a work card's `board_card_get` answer: who implemented it, the
// verifier it recommends, the alternates, and why every other family was skipped or is not here.
// These four slots are the GUI's whole half of that contract — the line the card shows, the two
// briefs, and the button and key that open a pane on the recommended runner. The fixture below is
// the JSON shape the card's "Where it shows" section specifies, verbatim.

namespace {

QJsonObject qaBlock()
{
    const auto json = QByteArrayLiteral(R"({
      "implemented_by": "anthropic/claude-opus-5", "implementer_family": "anthropic",
      "recommended": {"family": "openai", "label": "Codex", "runner": "guest:codex",
                      "model": "codex",
                      "why": "first in the ranking that is not the implementer and is installed"},
      "alternates": [{"family": "glm", "label": "GLM-5.3", "runner": "preset:glm-coding",
                      "model": "glm-5.3"}],
      "skipped": [{"family": "anthropic", "why": "implemented this card"}],
      "unavailable": [{"family": "kimi", "why": "no key"}],
      "commits": [{"hash": "1a2b3c4", "trailer": "anthropic/claude-opus-5", "agrees": true}]
    })");
    return QJsonDocument::fromJson(json).object();
}

// A card the worker hands over in the LLM QA lane, with its `qa` block.
QJsonObject qaCard(const QJsonObject &qa)
{
    QJsonObject out = card(QStringLiteral("K7Q2"), QStringLiteral("Voice mode"),
                           QStringLiteral("the issue"), QString(64, QLatin1Char('a')));
    out.insert(QStringLiteral("status"), QStringLiteral("needs-qa-llm"));
    out.insert(QStringLiteral("sections"), QJsonArray{"Issue", "Plan", "QA checklist"});
    out.insert(QStringLiteral("front"),
               QJsonObject{{"assignee", "agent"}, {"implemented_by", "anthropic/claude-opus-5"}});
    if (!qa.isEmpty())
        out.insert(QStringLiteral("qa"), qa);
    return out;
}

}  // namespace

void BoardModelTests::theVerifyLineNamesTheRecommendedVerifierAndWhatItSkipped()
{
    const QJsonObject qa = qaBlock();
    QCOMPARE(relay::board::verifyRunner(qa), QStringLiteral("guest:codex"));
    QCOMPARE(relay::board::verifyLabel(qa), QStringLiteral("Codex"));
    QCOMPARE(relay::board::verifyLine(qa),
             QStringLiteral("Verify with Codex (installed) · then GLM-5.3 · "
                            "Claude skipped: implemented this card"));

    // A preset runner is reachable because a key is stored, not because a CLI is installed.
    QJsonObject keyed = qa;
    keyed.insert(QStringLiteral("recommended"),
                 QJsonObject{{"family", "glm"}, {"label", "GLM-5.3"}, {"runner", "preset:glm-coding"}});
    keyed.remove(QStringLiteral("alternates"));
    keyed.remove(QStringLiteral("skipped"));
    QCOMPARE(relay::board::verifyLine(keyed), QStringLiteral("Verify with GLM-5.3 (key)"));
    // The worker's own word wins over the runner's prefix: a local model has no key.
    const QJsonObject local{{"recommended", QJsonObject{{"family", "local"}, {"label", "Bonsai 2 27B"},
                                                        {"runner", "preset:local:bonsai"},
                                                        {"available", "on this machine"}}}};
    QCOMPARE(relay::board::verifyLine(local), QStringLiteral("Verify with Bonsai 2 27B (on this machine)"));
    QCOMPARE(relay::board::verifyRunner(local), QStringLiteral("preset:local:bonsai"));

    // Nothing available: the line says why, family by family, so the reader knows what to install.
    QJsonObject none{{"implemented_by", "anthropic/claude-opus-5"},
                     {"skipped", QJsonArray{QJsonObject{{"family", "anthropic"}, {"why", "implemented this card"}}}},
                     {"unavailable", QJsonArray{QJsonObject{{"family", "openai"}, {"why", "not installed"}},
                                                QJsonObject{{"family", "kimi"}, {"why", "no key"}}}}};
    QCOMPARE(relay::board::verifyRunner(none), QString());
    QCOMPARE(relay::board::verifyLine(none),
             QStringLiteral("No verifier available: Claude skipped: implemented this card · "
                            "OpenAI: not installed · Kimi: no key"));

    // A worker that sends no `qa` at all says nothing on the card.
    QVERIFY(relay::board::verifyLine(QJsonObject()).isEmpty());
    // An entry with no label of its own is named by its family.
    QCOMPARE(relay::board::familyLabel(QStringLiteral("anthropic")), QStringLiteral("Claude"));
    QCOMPARE(relay::board::familyLabel(QStringLiteral("relay-free")), QStringLiteral("Relay Free"));
    QCOMPARE(relay::board::familyLabel(QStringLiteral("nebula")), QStringLiteral("Nebula"));
}

void BoardModelTests::theExecuteTaskAsksForTheImplementedByTrailer()
{
    const QString task = relay::board::executeTask(QStringLiteral("T71W"), QStringLiteral("Signatures"),
                                                   true, true);
    QVERIFY2(task.contains(QStringLiteral("Implemented-By: <vendor>/<your exact model id>")), qPrintable(task));
    QVERIFY(task.contains(QStringLiteral("anthropic/claude-opus-5")));
    // The worker stamps the card's own field, so the agent is not asked to type it as well.
    QVERIFY(task.contains(QStringLiteral("`implemented_by` is stamped")));
}

void BoardModelTests::theVerifyTaskIsTheQaChecklistAndAsksForTheVerifiedByTrailer()
{
    const QString task = relay::board::verifyTask(QStringLiteral("T71W"), QStringLiteral("Signatures"),
                                                  QStringLiteral("Codex"),
                                                  QStringLiteral("anthropic/claude-opus-5"),
                                                  QStringLiteral("check the Xvfb run too"));
    QVERIFY(task.startsWith(QStringLiteral("Verify #T71W: Signatures\n")));
    QVERIFY(task.contains(QStringLiteral("you are its verifier (Codex)")));
    QVERIFY(task.contains(QStringLiteral("anthropic/claude-opus-5 implemented it")));
    QVERIFY(task.contains(QStringLiteral("`## QA checklist`")));
    QVERIFY(task.contains(QStringLiteral("docs/qa_evidence/")));
    QVERIFY(task.contains(QStringLiteral("`qa-`")));
    QVERIFY(task.contains(QStringLiteral("`## Verdict`")));
    QVERIFY(task.contains(QStringLiteral("board_update_card")));
    QVERIFY(task.contains(QStringLiteral("board_move_card")));
    QVERIFY(task.contains(QStringLiteral("board_comment")));
    QVERIFY(task.contains(QStringLiteral("Verified-By: <vendor>/<your exact model id>")));
    QVERIFY(task.contains(QStringLiteral("Never fix the code yourself")));
    // A guest CLI gets this text and nothing else, so it also says where the card lives.
    QVERIFY(task.contains(QStringLiteral("issues/threads/T71W.md")));
    QVERIFY(task.endsWith(QStringLiteral("The owner adds, verbatim:\ncheck the Xvfb run too")));
}

void BoardModelTests::aQaLaneCardOffersVerifyOnTheRecommendedRunner()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    QString handedCard, handedRunner, handedTask;
    int opened = 0;
    view.onVerifyCard = [&](const QString &id, const QString &runner, const QString &task) {
        ++opened;
        handedCard = id;
        handedRunner = runner;
        handedTask = task;
    };
    QString hintId, hintKeys;
    view.onHint = [&](const QString &id, const QString &keys) { hintId = id; hintKeys = keys; };
    view.handleEvent(::opened({row("K7Q2", "needs-qa-llm", "features")}));
    view.handleEvent(qaCard(qaBlock()));

    // The line sits under the fields, in the muted ink, and says the whole recommendation.
    auto *line = view.findChild<QLabel *>(QStringLiteral("boardCardVerifyLine"));
    QVERIFY(line);
    QVERIFY(!line->isHidden());
    QVERIFY2(line->text().contains(QStringLiteral("Verify with Codex (installed) · then GLM-5.3 · "
                                                  "Claude skipped: implemented this card")),
             qPrintable(line->text()));

    // The button carries its key like the others (#QG60), and is live because a runner exists.
    QPushButton *verify = button(view, QStringLiteral("Verify"));
    QVERIFY(verify);
    QCOMPARE(verify->text(), QStringLiteral("Verify (v)"));
    QVERIFY(!verify->isHidden());
    QVERIFY(verify->isEnabled());

    // The click: one progress note on the thread, no move — the card stays in its QA lane — and
    // the pane is opened on the recommended runner with the QA brief.
    sent.clear();
    verify->click();
    QCOMPARE(opened, 1);
    QCOMPARE(handedCard, QStringLiteral("K7Q2"));
    QCOMPARE(handedRunner, QStringLiteral("guest:codex"));
    QVERIFY(handedTask.startsWith(QStringLiteral("Verify #K7Q2: Voice mode")));
    QVERIFY(handedTask.contains(QStringLiteral("Verified-By:")));
    QVERIFY(handedTask.contains(QStringLiteral("#K7Q2")));
    QVERIFY(handedTask.contains(QStringLiteral("anthropic/claude-opus-5 implemented it")));
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.at(0).value("type").toString(), QStringLiteral("board_comment"));
    QCOMPARE(sent.at(0).value("kind").toString(), QStringLiteral("progress"));
    QVERIFY2(sent.at(0).value("text").toString().startsWith(
                 QStringLiteral("Verify · handed to a new terminal pane on Codex · first in the ranking")),
             qPrintable(sent.at(0).value("text").toString()));
    // A click is the slow path, so it says its key once (WARP.md hint rule).
    QCOMPARE(hintId, QStringLiteral("board.verify"));
    QCOMPARE(hintKeys, QStringLiteral("v"));

    // `v` on the card's document does the same, and the reply box goes with it as the owner's note.
    auto *doc = view.findChild<QTextBrowser *>(QStringLiteral("boardCardDocument"));
    QVERIFY(doc);
    auto *reply = view.findChild<QPlainTextEdit *>(QStringLiteral("boardReplyEditor"));
    QVERIFY(reply);
    reply->setPlainText(QStringLiteral("watch the Xvfb run"));
    sent.clear();
    QTest::keyClick(doc, Qt::Key_V);
    QCOMPARE(opened, 2);
    QCOMPARE(handedRunner, QStringLiteral("guest:codex"));
    QVERIFY(handedTask.endsWith(QStringLiteral("The owner adds, verbatim:\nwatch the Xvfb run")));
    QCOMPARE(sent.size(), 1);
    QVERIFY(sent.at(0).value("text").toString().endsWith(QStringLiteral("\n\nwatch the Xvfb run")));

    // `v` from the list opens the selected card and verifies it, as `x` executes it.
    view.selectCard(QStringLiteral("K7Q2"));
    view.cardAction(QStringLiteral("verify"));
    QCOMPARE(opened, 3);

    // A card the worker sent no `qa` for: no line, and nothing to press.
    view.handleEvent(qaCard(QJsonObject()));
    QVERIFY(line->isHidden());
    QVERIFY(!verify->isEnabled());
    sent.clear();
    opened = 0;
    verify->click();
    QCOMPARE(opened, 0);
    QVERIFY(sent.isEmpty());

    // And a card that is not in a QA lane at all does not offer it: there is nothing to verify yet.
    view.handleEvent(card("K7Q2", "Voice mode", "the issue", QString(64, QLatin1Char('a'))));
    QVERIFY(line->isHidden());
    QVERIFY(verify->isHidden());
    QVERIFY(!verify->isEnabled());
}


// ---- the Verified section, the signature, and the notes (#T71W, 2026-09-19) -------------------
//
// Owner: "so we need a Verified section in the switchboard?" — yes, and it is derived rather
// than a status: `done` plus the `verified_by` the worker stamps when a card is closed out of a
// QA lane by a different model. Done keeps the rest.

void BoardModelTests::aDoneCardWithASignatureSitsInVerifiedAndTheRestStayInDone()
{
    Model model;
    model.setConfig(config());
    QJsonObject verified = row("K7Q2", "done", "features");
    verified.insert("verified_by", "openai/codex");
    QJsonObject closed = row("M3XJ", "done", "features");
    QJsonObject dropped = row("DN01", "dropped", "features");
    // A dropped card is closed, never verified, even if something wrote a signature on it.
    dropped.insert("verified_by", "openai/codex");
    model.reset(rows({verified, closed, dropped, row("T71W", "needs-qa-llm", "features")}));

    QCOMPARE(model.sectionOf(*model.card("K7Q2")), QStringLiteral("verified"));
    QCOMPARE(model.sectionOf(*model.card("M3XJ")), QStringLiteral("done"));
    QCOMPARE(model.sectionOf(*model.card("DN01")), QStringLiteral("done"));
    QCOMPARE(relay::board::verifiedSection(), QStringLiteral("verified"));

    // Between Needs QA and Done, whether or not board.yaml has ever heard of it.
    const QStringList ids = sectionIds(model);
    QCOMPARE(ids.mid(ids.size() - 3), (QStringList{"needs-qa", "verified", "done"}));
    QCOMPARE(model.cards(QStringLiteral("verified")).size(), 1);
    QCOMPARE(model.cards(QStringLiteral("done")).size(), 2);
    // It collects no status of its own, so nothing can be dropped or quick-added into it.
    QVERIFY(model.dropStatus(QStringLiteral("verified")).isEmpty());
    QCOMPARE(model.dropStatus(QStringLiteral("done")), QStringLiteral("done"));

    // The row list shows it like any other section, and it folds like any other.
    const QStringList shown = sketch(model.rows({}));
    QCOMPARE(shown.mid(shown.indexOf("# verified 1")),
             (QStringList{"# verified 1", "K7Q2", "# done 2", "DN01", "M3XJ"}));
    QVERIFY(sketch(model.rows({QStringLiteral("verified")})).contains("# verified 1 folded"));

    // The row wears its verifier, and only a verified row does.
    const auto names = [](const QList<relay::board::Badge> &list) {
        QStringList out;
        for (const relay::board::Badge &badge : list)
            out << badge.text;
        return out;
    };
    QVERIFY(names(relay::board::badges(*model.card("K7Q2"), false)).contains(QStringLiteral("\u2713 Codex")));
    QVERIFY(!names(relay::board::badges(*model.card("M3XJ"), false)).contains(QStringLiteral("\u2713 Codex")));
}

void BoardModelTests::aSignatureReadsAsItsModelAndItsHarness()
{
    const auto label = [](const char *text) {
        return relay::board::signatureLabel(QString::fromUtf8(text));
    };
    QCOMPARE(label("openai/codex"), QStringLiteral("Codex"));
    QCOMPARE(label("anthropic/claude-code"), QStringLiteral("Claude Code"));
    QCOMPARE(label("glm/glm-5.3"), QStringLiteral("GLM-5.3"));
    QCOMPARE(label("openai/gpt-6-astra"), QStringLiteral("GPT-6 Astra"));
    QCOMPARE(label("anthropic/claude-opus-5"), QStringLiteral("Claude Opus 5"));
    QCOMPARE(label("deepseek/deepseek-v4.1-flash"), QStringLiteral("DeepSeek V4.1 Flash"));
    QCOMPARE(label("kimi/kimi-k3"), QStringLiteral("Kimi K3"));
    // The owner, 2026-09-19: "lets try to record the model used" — so a guest names the model
    // it ran *and* the harness that ran it, and both are readable.
    QCOMPARE(label("anthropic/claude-opus-5 via claude-code"),
             QStringLiteral("Claude Opus 5 \u00b7 Claude Code"));
    QCOMPARE(label("openai/gpt-5.6-codex via codex"), QStringLiteral("GPT-5.6 Codex \u00b7 Codex"));
    // The harness alone, when the model could not be seen, does not say itself twice.
    QCOMPARE(label("openai/codex via codex"), QStringLiteral("Codex"));
    // Free text after the slug is allowed and ignored, as the worker's own reader ignores it.
    QCOMPARE(label("anthropic/claude-opus-5 (pane 2)"), QStringLiteral("Claude Opus 5"));
    QVERIFY(label("").isEmpty());
}

void BoardModelTests::nothingIsMovedIntoVerifiedAndMovingOutIsOrdinary()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.setCollapsedSections(QJsonArray{});
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    QJsonObject verified = row("K7Q2", "done", "features");
    verified.insert("verified_by", "openai/codex");
    view.handleEvent(opened({verified, row("T71W", "needs-qa-llm", "features")}));
    QListWidget *list = listOf(view);
    QVERIFY(list);

    // Alt+Shift+Right from Needs QA aims at Verified: refused here, with the one way in.
    view.selectCard(QStringLiteral("T71W"));
    sent.clear();
    QTest::keyClick(list, Qt::Key_Right, Qt::AltModifier | Qt::ShiftModifier);
    QVERIFY(sent.isEmpty());
    QCOMPARE(view.notice(),
             QStringLiteral("A card is verified by closing it from a QA lane with a different model."));

    // Out of Verified is an ordinary move, exactly as out of Done.
    view.selectCard(QStringLiteral("K7Q2"));
    sent.clear();
    QTest::keyClick(list, Qt::Key_Left, Qt::AltModifier | Qt::ShiftModifier);
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_move"));
    QCOMPARE(sent.last().value("status").toString(), QStringLiteral("needs-qa-llm"));
}

void BoardModelTests::theBriefsAskForTheExactModelAndTheGuestHarness()
{
    const QString execute = relay::board::executeTask(QStringLiteral("T71W"), QStringLiteral("Signatures"),
                                                      true, true);
    QVERIFY(execute.contains(QStringLiteral("Implemented-By: <vendor>/<your exact model id>")));
    QVERIFY(execute.contains(QStringLiteral("openai/gpt-6-astra")));
    QVERIFY(execute.contains(QStringLiteral("` via claude-code` or ` via codex`")));
    QVERIFY(execute.contains(QStringLiteral("anthropic/claude-opus-5 via claude-code")));

    const QString verify = relay::board::verifyTask(QStringLiteral("T71W"), QStringLiteral("Signatures"),
                                                    QStringLiteral("Codex"),
                                                    QStringLiteral("anthropic/claude-opus-5"));
    QVERIFY(verify.contains(QStringLiteral("Verified-By: <vendor>/<your exact model id>")));
    QVERIFY(verify.contains(QStringLiteral("` via claude-code` or ` via codex`")));
    // A guest has no board tools, so nothing stamps `verified_by` for it: the brief says to write
    // it into the card's front matter itself, or the board cannot say who checked the card.
    QVERIFY(verify.contains(QStringLiteral("verified_by: <that same signature>")));
    QVERIFY(verify.contains(QStringLiteral("front matter")));
}

void BoardModelTests::relayFreeSaysWhyItCannotVerifyAndAWeakPickWarns()
{
    // The worker's own sentence for the Relay Free case (qa_verifiers.NO_VERIFIER_NOTE): there is
    // no verifier at all, and "no key, no key" would not tell the reader why.
    const QString free = QStringLiteral("No verifier available. Verifying is not available on Relay "
                                        "Free: add a provider key, or install Codex or Claude Code.");
    QJsonObject none{{"implemented_by", "relay-free/relay-main"},
                     {"recommended", QJsonValue::Null},
                     {"unavailable", QJsonArray{QJsonObject{{"family", "openai"}, {"why", "not installed"}}}},
                     {"note", free}};
    QCOMPARE(relay::board::verifyNote(none), free);
    QCOMPARE(relay::board::verifyLine(none), free);        // the whole line, not a list of reasons

    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.handleEvent(::opened({row("K7Q2", "needs-qa-llm", "features")}));
    QJsonObject card = qaCard(none);
    view.handleEvent(card);
    auto *line = view.findChild<QLabel *>(QStringLiteral("boardCardVerifyLine"));
    QVERIFY(line);
    QVERIFY(!line->isHidden());
    QVERIFY2(line->text().contains(free), qPrintable(line->text()));
    QPushButton *verify = button(view, QStringLiteral("Verify"));
    QVERIFY(verify);
    QVERIFY(!verify->isEnabled());
    QVERIFY2(verify->toolTip().contains(free), qPrintable(verify->toolTip()));

    // A warning beside a recommendation: the line stands and the note follows it, and Verify works.
    const QString weak = QStringLiteral("The recommended verifier shares the implementer's lineage "
                                        "(cn-open): it shares training data, so it is a weaker check.");
    QJsonObject warned = qaBlock();
    warned.insert(QStringLiteral("note"), weak);
    view.handleEvent(qaCard(warned));
    QVERIFY(line->text().contains(QStringLiteral("Verify with Codex (installed)")));
    QVERIFY2(line->text().contains(weak.toHtmlEscaped()), qPrintable(line->text()));
    QVERIFY(button(view, QStringLiteral("Verify"))->isEnabled());
}

// ------------------------------------------- the page agent's panel (#8YQ9, protocol 19.18)

namespace {

// The `chat` block of a `board` event or a `board_chat_*` answer: what the worker's PageAgent
// reports as its state (board_chat.py, `PageAgent.state()`).
QJsonObject chatState(bool running, const QJsonArray &queue = {}, const QJsonArray &history = {})
{
    return QJsonObject{{"running", running}, {"turn_id", running ? "chat-9f1c2a" : QJsonValue()},
                       {"model", QJsonValue()}, {"survey", false}, {"seconds", 0.0},
                       {"queue", queue}, {"history", history}};
}

// A `board` event that also carries the conversation, the way the worker sends it (19.18).
QJsonObject openedWithChat(const QList<QJsonObject> &cards, const QJsonObject &chat)
{
    QJsonObject event = opened(cards);
    event.insert(QStringLiteral("chat"), chat);
    return event;
}

// One turn event of the page agent: `chat: true` and a `turn_id`, never a `card_id` (19.18).
QJsonObject chatEvent(const QString &type, const QJsonObject &extra = {})
{
    QJsonObject out{{"event", type}, {"chat", true}, {"turn_id", "chat-9f1c2a"}};
    for (auto it = extra.begin(); it != extra.end(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

QWidget *chatPanel(relay::BoardView &view)
{
    return view.findChild<QWidget *>(QStringLiteral("boardChatPanel"));
}

QPlainTextEdit *composerOf(relay::BoardView &view)
{
    return view.findChild<QPlainTextEdit *>(QStringLiteral("boardChatComposer"));
}

QTextBrowser *chatLog(relay::BoardView &view)
{
    return view.findChild<QTextBrowser *>(QStringLiteral("boardChatLog"));
}

QList<QWidget *> queueRows(relay::BoardView &view)
{
    return view.findChildren<QWidget *>(QStringLiteral("boardChatQueueRow"));
}

// Type into the composer and press Enter, the way the owner asks a question.
void ask(relay::BoardView &view, const QString &text)
{
    QPlainTextEdit *box = composerOf(view);
    QVERIFY(box);
    box->setPlainText(text);
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(box, &enter);
}

QJsonObject problem(const QString &path, const QString &message)
{
    return QJsonObject{{"code", "bad_front_matter"}, {"path", path}, {"message", message},
                       {"severity", "error"}};
}

}  // namespace

// A pane opened while the agent is half way through an answer draws the whole panel from the one
// `board` event: the conversation so far, the queue behind it, and the fact that it is running.
void BoardModelTests::thePanelSeedsItselfFromTheBoardEventsChatBlock()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QVERIFY(chatPanel(view));
    const QJsonArray history{QJsonObject{{"role", "owner"}, {"text", "merge the two voice cards"}},
                             QJsonObject{{"role", "agent"}, {"text", "They are #K7Q2 and #M3XJ."}}};
    const QJsonArray queue{QJsonObject{{"id", "c1"}, {"text", "then sort Inbox"}}};
    view.handleEvent(openedWithChat({row("K7Q2", "inbox", "features")},
                                    chatState(true, queue, history)));
    QVERIFY(view.chatRunning());
    QTextBrowser *log = chatLog(view);
    QVERIFY(log);
    QVERIFY(log->toPlainText().contains(QStringLiteral("merge the two voice cards")));
    QVERIFY(log->toPlainText().contains(QStringLiteral("They are #K7Q2 and #M3XJ.")));
    QCOMPARE(queueRows(view).size(), 1);
}

// #N8VK's rule, which the page agent takes from the terminal panes: a second prompt typed while a
// turn runs is *queued*, never refused. The panel sends it exactly like the first — the worker
// owns the queue — and the row appears from the event the worker sends back.
void BoardModelTests::aPromptSendsBoardChatAndASecondOneQueuesInsteadOfBeingRefused()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    sent.clear();

    ask(view, QStringLiteral("what is in Inbox?"));
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_chat"));
    QCOMPARE(sent.last().value("text").toString(), QStringLiteral("what is in Inbox?"));
    QVERIFY(!sent.last().value("id").toString().isEmpty());
    QVERIFY(composerOf(view)->toPlainText().isEmpty());     // the box is cleared on send

    view.handleEvent(QJsonObject{{"event", "board_chat_started"}, {"turn_id", "chat-9f1c2a"},
                                 {"model", "switchboard"}, {"chat", chatState(true)}});
    QVERIFY(view.chatRunning());

    // The answer streams in. The log re-renders on a timer — at most 25 times a second, so a
    // fast stream does not repaint per token — so this waits for it rather than for one event.
    view.handleEvent(chatEvent(QStringLiteral("delta"), {{"text", "Two cards."}}));
    QTRY_VERIFY(chatLog(view)->toPlainText().contains(QStringLiteral("Two cards.")));

    // A second prompt while it runs: still one `board_chat`, no refusal anywhere in the panel.
    sent.clear();
    ask(view, QStringLiteral("then sort it"));
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_chat"));
    QCOMPARE(queueRows(view).size(), 0);                    // nothing is queued until the worker says so
    view.handleEvent(QJsonObject{{"event", "board_chat_queued"}, {"id", "c1"}, {"position", 1},
                                 {"text", "then sort it"}, {"chat", true}});
    QCOMPARE(queueRows(view).size(), 1);

    // While a turn runs the send button is Stop, and it stops that turn and not the queue.
    auto *send = view.findChild<QToolButton *>(QStringLiteral("boardChatSend"));
    QVERIFY(send);
    QCOMPARE(send->text(), QStringLiteral("Stop"));
    sent.clear();
    send->click();
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_chat_cancel"));

    view.handleEvent(QJsonObject{{"event", "board_chat_cancelled"}, {"stopped", true},
                                 {"chat", chatState(false, QJsonArray{
                                     QJsonObject{{"id", "c1"}, {"text", "then sort it"}}})}});
    QVERIFY(!view.chatRunning());
    QCOMPARE(send->text(), QStringLiteral("Send"));
    QCOMPARE(queueRows(view).size(), 1);                    // the queue survived the stop
}

// The worker's queue is authoritative (19.18): a click sends the op and the redraw comes back as
// `board_chat_state`. The panel never reorders its own rows optimistically.
void BoardModelTests::theQueueRowsRemoveAndReorderThroughTheWorker()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    const QJsonArray queue{QJsonObject{{"id", "c1"}, {"text", "first"}},
                           QJsonObject{{"id", "c2"}, {"text", "second"}}};
    view.handleEvent(openedWithChat({row("K7Q2", "inbox", "features")}, chatState(true, queue)));
    QCOMPARE(queueRows(view).size(), 2);

    sent.clear();
    QList<QToolButton *> removes;
    for (QWidget *rowWidget : queueRows(view))
        for (QToolButton *button : rowWidget->findChildren<QToolButton *>())
            if (button->text() == QStringLiteral("×"))
                removes << button;
    QCOMPARE(removes.size(), 2);
    removes.first()->click();
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_chat_queue_remove"));
    QCOMPARE(sent.last().value("item").toString(), QStringLiteral("c1"));
    QCOMPARE(queueRows(view).size(), 2);                    // not until the worker answers

    view.handleEvent(QJsonObject{{"event", "board_chat_state"},
                                 {"chat", chatState(true, QJsonArray{queue.at(1)})}});
    QCOMPARE(queueRows(view).size(), 1);
}

// Owner, 2026-09-19: "put the clean up button down there" — beside the page agent, with the other
// board-wide button the survey of that row turned up. It keeps all of its own behaviour.
void BoardModelTests::cleanUpMovedIntoThePageAgentsRowBesideCheck()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    QWidget *panel = chatPanel(view);
    QToolButton *cleanup = cleanupButton(view);
    auto *check = view.findChild<QToolButton *>(QStringLiteral("boardChatCheck"));
    QVERIFY(panel && cleanup && check);
    QVERIFY(panel->isAncestorOf(cleanup));
    QVERIFY(panel->isAncestorOf(check));
    // It is out of the filter row for good, not shown in both places.
    QWidget *tools = view.findChild<QWidget *>(QStringLiteral("boardListTools"));
    QVERIFY(tools && !tools->isAncestorOf(cleanup));
    // And it still runs a preview first (19.9).
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    cleanup->click();
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_cleanup"));
    QCOMPARE(sent.last().value("dry_run").toBool(), true);
}

// Check is the whole board; a section's ⚠ is that section alone (`board_check {section}`). A
// scoped answer is drawn as that section's triage and is never allowed to rewrite the banner over
// the list, which counts the board-level problems a scoped check leaves out.
void BoardModelTests::checkIsUnscopedAndASectionsTriageNamesItsSection()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features"), row("M3XJ", "ready", "features")}));

    sent.clear();
    view.findChild<QToolButton *>(QStringLiteral("boardChatCheck"))->click();
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_check"));
    QVERIFY(!sent.last().contains(QStringLiteral("section")));
    const QString unscoped = sent.last().value("id").toString();

    view.handleEvent(QJsonObject{{"event", "board_problems"}, {"id", unscoped},
                                 {"items", QJsonArray{problem("issues/features/a.md", "no id")}},
                                 {"section", QJsonValue()}});
    auto *findings = view.findChild<QWidget *>(QStringLiteral("boardChatFindings"));
    QVERIFY(findings && !findings->isHidden());

    // The ⚠ on the Ready header. The rect it is clicked in is the delegate's; what it does is this.
    sent.clear();
    view.requestCheck(QStringLiteral("ready"));
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_check"));
    QCOMPARE(sent.last().value("section").toString(), QStringLiteral("ready"));
    const QString scoped = sent.last().value("id").toString();

    auto *banner = view.findChild<QLabel *>(QStringLiteral("boardProblems"));
    QVERIFY(banner);
    const QString bannerBefore = banner->text();
    view.handleEvent(QJsonObject{{"event", "board_problems"}, {"id", scoped},
                                 {"items", QJsonArray{}}, {"section", "ready"}});
    QCOMPARE(banner->text(), bannerBefore);       // a scoped check never rewrites the banner
    // "nothing to fix" is still an answer: a click that says nothing looks broken.
    QVERIFY(findings->findChildren<QLabel *>().size() > 0);
}

// Owner, 2026-09-19: "if there are problems, when you click on them, it sends a fix note to the
// switchboard agent" — and "draft you confirm". So a click fills the composer and stops there.
void BoardModelTests::aProblemDraftsAFixInTheComposerWithoutSendingIt()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    QJsonObject event = opened({row("K7Q2", "inbox", "features")});
    event.insert(QStringLiteral("problems"),
                 QJsonArray{problem("issues/features/2026-09-19-broken.md", "front matter has no id")});
    view.handleEvent(event);

    auto *banner = view.findChild<QLabel *>(QStringLiteral("boardProblems"));
    QVERIFY(banner && !banner->isHidden());
    sent.clear();
    banner->linkActivated(QStringLiteral("issues/features/2026-09-19-broken.md"));

    QPlainTextEdit *box = composerOf(view);
    QVERIFY(box);
    QVERIFY(box->toPlainText().contains(QStringLiteral("2026-09-19-broken.md")));
    QVERIFY(box->toPlainText().contains(QStringLiteral("front matter has no id")));
    QVERIFY(sent.isEmpty());                      // a draft, never a send
}

// The survey is the page agent's opening turn on a board that was just created (19.18). Its
// proposals are an offer: nothing is written until the owner ticks and presses the button, which
// goes through the same never-twice import path as everything else (19.13).
void BoardModelTests::theSurveysImportButtonSendsBoardImportApply()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({}));
    sent.clear();

    view.handleEvent(QJsonObject{
        {"event", "board_survey"}, {"root", "/tmp/workspace/issues"}, {"project", "/tmp/workspace"},
        {"hints", QJsonArray{QJsonObject{{"kind", "vendor"}, {"message", "node_modules/ is left alone"}}}},
        {"counts", QJsonObject{{"items", 2}}},
        {"proposals", QJsonArray{
            QJsonObject{{"title", "Fix the flaky test"},
                        {"source", QJsonObject{{"kind", "todo-md"}, {"path", "TODO.md"},
                                               {"key", "todo-md:TODO.md#0"}}}},
            QJsonObject{{"title", "Ship the board"},
                        {"source", QJsonObject{{"kind", "todo-md"}, {"path", "TODO.md"},
                                               {"key", "todo-md:TODO.md#1"}}}}}},
        {"git", QJsonObject{{"is_repo", true}, {"primary", "origin"}, {"forge", "github"},
                            {"owner", "relay"}, {"repo", "relay-terminal"},
                            {"url", "git@github.com:relay/relay-terminal.git"}}}});

    auto *survey = view.findChild<QWidget *>(QStringLiteral("boardChatSurvey"));
    QVERIFY(survey && !survey->isHidden());
    QVERIFY(sent.isEmpty());                      // the survey writes nothing by itself

    const QList<QCheckBox *> proposals = survey->findChildren<QCheckBox *>();
    QCOMPARE(proposals.size(), 2);
    for (QCheckBox *box : proposals)
        QVERIFY(box->isChecked());                // ticked by default; untick to leave one out
    proposals.at(1)->setChecked(false);

    auto *import = view.findChild<QToolButton *>(QStringLiteral("boardChatImport"));
    QVERIFY(import);
    import->click();
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_import_apply"));
    const QJsonArray keys = sent.last().value("keys").toArray();
    QCOMPARE(keys.size(), 1);
    QCOMPARE(keys.first().toString(), QStringLiteral("todo-md:TODO.md#0"));
    QVERIFY(survey->isHidden());
}

// Owner, 2026-09-19: "if its .git, it should offer to look on github.com for an issues corpus to
// sync". Looking is `forge_sync_plan` (19.14, landed by #GDQN), which writes to neither side; the
// sync itself is #ZKR0's surface, so the panel never sends `forge_sync_run`.
void BoardModelTests::theSurveyOffersToLookOnGithubAndThatLookWritesNothing()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({}));
    view.handleEvent(QJsonObject{
        {"event", "board_survey"}, {"root", "/tmp/workspace/issues"}, {"project", "/tmp/workspace"},
        {"hints", QJsonArray{}}, {"counts", QJsonObject{{"items", 0}}}, {"proposals", QJsonArray{}},
        {"git", QJsonObject{{"is_repo", true}, {"primary", "origin"}, {"forge", "github"},
                            {"owner", "relay"}, {"repo", "relay-terminal"},
                            {"url", "git@github.com:relay/relay-terminal.git"}}}});

    auto *look = view.findChild<QToolButton *>(QStringLiteral("boardChatForgeLook"));
    QVERIFY(look);
    sent.clear();
    look->click();
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("forge_sync_plan"));
    // The repo comes from the probe's own `git` block, so a board with no `github:` in its
    // board.yaml — which a board created a minute ago will not have — can still be asked.
    QCOMPARE(sent.last().value("repo").toString(), QStringLiteral("relay/relay-terminal"));
    const QString request = sent.last().value("id").toString();
    QVERIFY(!request.isEmpty());
    QVERIFY(!look->isEnabled());                 // one look at a time

    view.handleEvent(QJsonObject{{"event", "forge_sync_planned"}, {"id", request},
                                 {"root", "/tmp/workspace/issues"}, {"repo", "relay/relay-terminal"},
                                 {"dry_run", true}, {"cards", 3}, {"creates", 2}, {"pushed", 0},
                                 {"pulled", 5}, {"conflicts", 0}, {"needs_confirm", false},
                                 {"cap", 50}, {"idle", 0}, {"errors", QJsonArray{}}});
    QVERIFY(look->isEnabled());
    bool reported = false;
    for (QLabel *line : view.findChild<QWidget *>(QStringLiteral("boardChatSurvey"))->findChildren<QLabel *>())
        if (line->text().contains(QStringLiteral("5 issues would become cards"))
            && line->text().contains(QStringLiteral("Nothing was written")))
            reported = true;
    QVERIFY(reported);

    // Looking is the whole offer: the sync that would write is a different card's surface.
    for (const QJsonObject &message : sent)
        QVERIFY(message.value("type").toString() != QStringLiteral("forge_sync_run"));

    // A forge that cannot be read says so on the same line, and the button comes back.
    sent.clear();
    look->click();
    const QString second = sent.last().value("id").toString();
    view.handleEvent(QJsonObject{{"event", "error"}, {"id", second}, {"root", "/tmp/workspace/issues"},
                                 {"code", "forge_auth"}, {"text", "No GitHub credential is stored."}});
    QVERIFY(look->isEnabled());
    bool explained = false;
    for (QLabel *line : view.findChild<QWidget *>(QStringLiteral("boardChatSurvey"))->findChildren<QLabel *>())
        if (line->text().contains(QStringLiteral("no credential")))
            explained = true;
    QVERIFY(explained);
}

// `context` rides along tagged `chat: true` (19.18) so the page's chip follows the conversation
// exactly as a terminal pane's follows its own.
void BoardModelTests::theContextChipFollowsTheConversation()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    auto *chip = view.findChild<QLabel *>(QStringLiteral("boardChatContext"));
    QVERIFY(chip);
    view.handleEvent(chatEvent(QStringLiteral("context"),
                               {{"used", 48000}, {"window", 200000}, {"limit", 180000},
                                {"percent", 24.0}}));
    QVERIFY(!chip->isHidden());
    QVERIFY(chip->text().contains(QStringLiteral("76")));
    QVERIFY(chip->text().contains(QStringLiteral("left")));
}

// The same rule a cleanup's events keep (19.9), for the same reason: the page agent can write any
// card, and a card that happens to be open must never see its chatter in its thread.
void BoardModelTests::thePageAgentsEventsNeverReachACardThread()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    view.handleEvent(card("K7Q2", "K7Q2 card", "the issue", "h1"));
    QVERIFY(view.detailOpen());
    auto *document = view.findChild<QTextBrowser *>(QStringLiteral("boardCardDocument"));
    QVERIFY(document);
    const QString before = document->toPlainText();

    view.handleEvent(chatEvent(QStringLiteral("delta"), {{"text", "reorganising the board"}}));
    view.handleEvent(chatEvent(QStringLiteral("tool_started"), {{"tool", "board_read"}}));
    view.handleEvent(chatEvent(QStringLiteral("done")));

    QCOMPARE(document->toPlainText(), before);
    QVERIFY(!document->toPlainText().contains(QStringLiteral("reorganising the board")));
    QTRY_VERIFY(chatLog(view)->toPlainText().contains(QStringLiteral("reorganising the board")));
}

QTEST_MAIN(BoardModelTests)
#include "boardmodel_test.moc"
