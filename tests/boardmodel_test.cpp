// SPDX-License-Identifier: AGPL-3.0-or-later
// The Switchboard pane's pure logic: which section a card falls into, the filter language, the
// ordering, the row list the one scrolling view draws, and the `#` picker's ranking. No worker,
// no files, no network.
#include "BoardModel.h"

#include "Projects.h"
#include "BoardPane.h"
#include "AgentContext.h"   // what the agent on each of the two pages is about (#AGNT step 6)
#include "RichEditor.h"     // the console's composer, which is the board's prompt box
#include "Theme.h"      // the real stylesheet, for the action-row face (#PBX1)

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QClipboard>
#include <QComboBox>
#include <QFocusEvent>
#include <QFrame>
#include <QKeyEvent>
#include <QSplitter>
#include <QJsonArray>
#include <QLabel>
#include <QLayout>
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
#include <QMessageBox>
#include <QTimer>
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
        else if (row.kind == Row::Fold)
            // The self-closed fold row (#93WR): "~ done 3" open, "~ done 3 folded" put away.
            out << QStringLiteral("~ %1 %2%3").arg(row.columnId).arg(row.count)
                       .arg(row.collapsed ? QStringLiteral(" folded") : QString());
        else
            out << row.cardId;
    }
    return out;
}

// The delete confirm (#CYM9) is modal, so the answer is armed before the click, the way
// modelsettings_test.cpp answers the Model… prompt. `accept` clicks the Delete button;
// otherwise the dialog is rejected (Cancel). The try count is only a backstop: an unanswered
// dialog would hang the suite.
void answerDeleteConfirm(bool accept)
{
    auto *timer = new QTimer;
    auto *tries = new int(0);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, tries, accept] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        // A real window of time, not a tick budget: on a busy machine 500 zero-ms ticks can
        // pass before the dialog's exec loop even starts, and an unanswered dialog then hangs
        // the suite until QtTest's own timeout.
        if (!box && ++*tries < 200)
            return;
        if (box) {
            if (accept) {
                for (QAbstractButton *button : box->buttons())
                    if (button->text() == QStringLiteral("Delete")) {
                        button->click();
                        break;
                    }
            } else {
                box->reject();
            }
        }
        timer->stop();
        timer->deleteLater();
        delete tries;
    });
    timer->start(50);
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

// #TTYB: the worker answers a `board_card_get` by echoing that request's id, and the window hands
// every event to every board pane of the workspace, so a pane shows the card only when the id is
// its own. `openCard` is that handshake as one call: the pane selects and asks, and the answer is
// delivered under the id it coined. A card whose detail is already open is not asked for again
// (`BoardView::openSelected`), so the id echoed is the pane's latest coin — the re-read a change
// asks for reuses that same id, which is exactly what the worker echoes back.
void openCard(relay::BoardView &view, QList<QJsonObject> &sent, QJsonObject event)
{
    view.selectCard(event.value(QStringLiteral("card_id")).toString());
    view.openSelected();
    if (sent.isEmpty()) {
        // The list was cleared since the ask that opened this card; ask again so the answer can
        // carry a request id this pane coined.
        view.closeDetail();
        view.openSelected();
    }
    QVERIFY(!sent.isEmpty());
    event.insert(QStringLiteral("id"), sent.constLast().value(QStringLiteral("id")));
    view.handleEvent(event);
}

// The pane's one list.
QListWidget *listOf(relay::BoardView &view)
{
    return view.findChild<QListWidget *>(QStringLiteral("boardList"));
}

// ---- the agent console the window makes (card #AGNT steps 5 and 6) --------------------------
//
// `BoardView` does not build its own chat widget any more. It asks the window for a console —
// a no-shell `Pane`, which lives only in the app's translation unit and cannot be constructed
// from `relay-board` — and is handed back a `QWidget *` and a `relay::agent::ConsoleHandle`.
//
// This is that console, minus the pane: the two things the board actually drives are the
// composer, which is the prompt box, and the action row, which the console builds from
// `Context::actions()` and rebuilds whenever the context says something moved. Everything else
// on the handle records what it was asked to do, so a test can say what the board asked for
// rather than what a widget looks like.
class FakeConsole : public QWidget {
public:
    FakeConsole(relay::agent::Context *context, QWidget *parent)
        : QWidget(parent), m_context(context), m_spec(context->spec())
    {
        setObjectName(QStringLiteral("agentConsole"));
        auto *column = new QVBoxLayout(this);
        column->setContentsMargins(0, 0, 0, 0);
        m_row = new QWidget(this);
        m_row->setObjectName(QStringLiteral("agentActionRow"));
        m_rowLayout = new QHBoxLayout(m_row);
        m_rowLayout->setContentsMargins(0, 0, 0, 0);
        column->addWidget(m_row);
        m_editor = new RichEditor(this);
        column->addWidget(m_editor);
        // The pane owns this callback while it holds the context (src/Pane.h, setContext).
        context->onChanged = [this] { rebuild(); };
        // And the pane offers a submitted line to its context before it routes it (src/Pane.h,
        // `requestRoute`), with the **raw** route — "auto" for Enter, "agent" for Ctrl+Enter,
        // "shell" for Ctrl+Shift+Enter — so a card's three chords can be told apart. The card
        // page reads them in `CardContext::submit`; before card #AGNT step 5 it took this
        // editor's `onSubmit` for itself, which took the box away from everything else that
        // speaks through it.
        // The box is the context's while it handles the line — it clears what it took and
        // leaves what it refused — so nothing is cleared here either.
        m_editor->onSubmit = [this](const QString &route) {
            const QString typed = m_editor->toPlainText();
            if (typed.trimmed().isEmpty())
                return;
            m_context->submit(route, typed);
        };
        rebuild();
    }

    // The row, exactly as `Pane::rebuildActionRow` builds it: the action's `key` is the button's
    // object name, its `fullLabel()` is the text and what a narrow row shortens from, and
    // `leaves` rides as a property.
    void rebuild()
    {
        qDeleteAll(m_row->findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly));
        m_actions = relay::agent::withUniqueLetters(m_context->actions());
        for (const relay::agent::Action &action : std::as_const(m_actions)) {
            auto *button = new QToolButton(m_row);
            button->setObjectName(action.key);
            button->setText(action.fullLabel());
            button->setProperty("fullLabel", action.fullLabel());
            button->setProperty("leaves", action.leaves);
            button->setToolTip(action.tooltip);
            button->setEnabled(action.enabled);
            if (action.run)
                connect(button, &QToolButton::clicked, this, action.run);
            m_rowLayout->addWidget(button);
        }
        m_editor->setPlaceholders({m_context->placeholder(), QStringLiteral("…")});
        ++m_rebuilds;
    }

    relay::agent::Context *context() const { return m_context; }
    // The spec as it was when the window made this console: what would have gone out in
    // `configure`'s `context` block.
    relay::agent::ContextSpec spec() const { return m_spec; }
    relay::agent::ContextSpec liveSpec() const { return m_context->spec(); }
    RichEditor *editor() const { return m_editor; }
    QList<relay::agent::Action> actions() const { return m_actions; }
    int rebuilds() const { return m_rebuilds; }
    QStringList drafts;          // every `draftInComposer`, in order
    int focusCalls = 0;
    // `setTranscriptHiddenUntilUsed`, which the card page called until card #CTRN: the transcript
    // is where a card turn is drawn now, so it must never be hidden until a byte arrives.
    int hideCalls = 0;
    bool hidden = false;
    // `clearTranscript`, in order: the surface the console was told it is drawing (card #CTRN).
    QStringList transcriptOf;

    relay::agent::ConsoleHandle handle()
    {
        relay::agent::ConsoleHandle out;
        out.widget = this;
        out.focusComposer = [this] { ++focusCalls; m_editor->setFocus(); };
        out.draftInComposer = [this](const QString &text) {
            drafts << text;
            m_editor->setPlainText(text);
        };
        out.composerText = [this] { return m_editor->toPlainText(); };
        out.setTranscriptHiddenUntilUsed = [this](bool hide) { ++hideCalls; hidden = hide; };
        out.clearTranscript = [this](const QString &surface) { transcriptOf << surface; };
        out.setCollapsed = [this](bool collapse) { setVisible(!collapse); };
        out.collapsed = [this] { return isHidden(); };
        out.runActionLetter = [this](const QString &letter) {
            const int at = relay::agent::actionForLetter(m_actions, letter);
            if (at < 0 || !m_actions.at(at).run)
                return false;
            m_actions.at(at).run();
            return true;
        };
        return out;
    }

private:
    relay::agent::Context *m_context;
    relay::agent::ContextSpec m_spec;
    QWidget *m_row = nullptr;
    QHBoxLayout *m_rowLayout = nullptr;
    RichEditor *m_editor = nullptr;
    QList<relay::agent::Action> m_actions;
    int m_rebuilds = 0;
};

// Give a view the consoles the window would make, and keep what it asked for. On the stack
// beside the view: the widgets belong to the view, this only holds pointers at them.
struct Consoles {
    explicit Consoles(relay::BoardView &view)
    {
        view.onCreateConsole = [this](relay::agent::Context *context, QWidget *parent) {
            auto *console = new FakeConsole(context, parent);
            made << console;
            return console->handle();
        };
    }
    QList<FakeConsole *> made;
    // The console of a given context name — "switchboard" for the list page, "card" for an
    // open card. Null before the board has asked for one.
    FakeConsole *of(const QString &name) const
    {
        for (FakeConsole *console : made)
            if (console->spec().name == name)
                return console;
        return nullptr;
    }
    FakeConsole *board() const { return of(QStringLiteral("switchboard")); }
    FakeConsole *card() const { return of(QStringLiteral("card")); }
};

// The card page's reply box: the composer inside the console the card page embedded.
QPlainTextEdit *replyBox(relay::BoardView &view)
{
    QWidget *console = view.cardConsole();
    return console ? console->findChild<QPlainTextEdit *>(QStringLiteral("composerEditor")) : nullptr;
}

}  // namespace

class BoardModelTests : public QObject {
    Q_OBJECT

private slots:
    void categoryFoldersComeFromTheConfig();
    void sectionsAreTheConfiguredStatusesThenTheRest();
    void cardsLandInTheSectionOfTheirStatus();
    void closedCardsGoToTheDoneSectionAndParkedOnesToTheirOwn();
    void memoriesKeepTheirOwnStatuses();
    void rankOrdersASectionAndDoneIsNewestFirst();
    void timeSortsOrderEverySectionAlikeAndTheIdsRoundTrip();
    void theFilterLanguageMatchesEveryTerm();
    void theWorkerAnswersThePlainWordsOfTheFilter();
    void theFilterHidesEmptySectionsAndUnfoldsTheRest();
    void searchRanksOpenCardsAndExactIdsFirst();
    void upsertAndRemoveKeepTheBoardInStep();
    void statusTitlesAreHumanReadable();
    void everySectionSaysWhatItIsFor();
    void theRowListIsHeadersThenCards();
    void badgesSayWhatTheCardCarries();
    void aClaimedCardCarriesThePanesSession();
    void aClaimedRowSaysWhichPaneHoldsItAndWhetherItIsStillOpen();
    void theCardPageLinksTheClaimToThePaneAndMutesAClosedOne();
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
    void theDeleteKeyAndButtonDeleteTheCardAndTheUndoToastSurvives();
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
    void theThinkingTraceRunsInTheCardsConsole();
    void labelClicksCopyFiltersAndCardRefsZoom();
    void theRefCopyButtonCopiesTheIdInTheRowsAndTheHeader();
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
    void executeHandsTheCardToAPaneAndMovesItToExecuting();
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
    // The cards the agent closed itself, folded into one row of the done list (#93WR)
    void aSelfClosedCardIsDoneAndStampedByWhoeverImplementedIt();
    void theSelfClosedCardsOfASectionFoldIntoOneRow();
    void theFoldRowTogglesOnClickEnterAndTheArrowsAndRidesTheLayout();
    void aSelfClosedCardReachedByIdUnfoldsItsGroup();
    void theBriefsAskForTheExactModelAndTheGuestHarness();
    void relayFreeSaysWhyItCannotVerifyAndAWeakPickWarns();
    // The board and the card as contexts (card #AGNT step 6, protocol 33)
    void theListPageAsksForASwitchboardConsoleKeyedByTheTab();
    void theBoardsRowIsCheckCleanUpTestsAndProfileWithTheirLetters();
    void checkIsUnscopedAndASectionsTriageNamesItsSection();
    void aProblemDraftsAFixInTheConsoleWithoutSendingIt();
    void theSurveysImportButtonSendsBoardImportApply();
    void theSurveyOffersToLookOnGithubAndThatLookWritesNothing();
    void anEmptyBoardStillShowsTheAgentBecauseThatIsWhereTheSurveyRuns();
    void theAskKeyFocusesTheConsoleAndACardGoesBackFirst();
    void theCardPageAsksForACardConsoleAndItsActionsFollowTheCard();
    void theOneConsoleIsToldWhichCardsTranscriptItIsDrawing();
    void theCardsRowCarriesVerifyOnlyInAQaLane();
    void anAnswersCardOptionAndSessionLinksResolveThroughTheContext();
    void aBoardWithNoConsoleFactoryStillWorks();
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
    // #X7NB dropped the plan card type: `planning` is an ordinary folder of work cards, and
    // `memory` is the one tab that is a card type of its own.
    QCOMPARE(model.tab(QStringLiteral("planning"))->type, QStringLiteral("work"));
    QCOMPARE(model.tab(QStringLiteral("memory"))->type, QStringLiteral("memory"));
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

void BoardModelTests::memoriesKeepTheirOwnStatuses()
{
    Model model;
    model.setConfig(config());
    QJsonObject memory = row("ME01", "active", "memory");
    memory.insert(QStringLiteral("type"), QStringLiteral("memory"));
    memory.insert(QStringLiteral("topic"), QStringLiteral("conventions"));
    QJsonObject planning = row("PL01", "planned", "planning");
    model.reset(rows({row("K7Q2", "ready", "features"), planning, memory}));

    // Statuses no configured lane collects get a section each, so one list really does hold
    // every open card whatever its type. A card in the `planning` folder is an ordinary work
    // card (#X7NB): it sits in the section its work status names.
    QCOMPARE(sectionIds(model).mid(6), (QStringList{"active", "planned", "verified", "done"}));
    QCOMPARE(model.cards(QStringLiteral("planned")).first().id, QStringLiteral("PL01"));
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

void BoardModelTests::theWorkerAnswersThePlainWordsOfTheFilter()
{
    // #7M6E: the card's text stopped riding on every row, so the plain words are asked of the
    // worker (`board_search`) and the scoped terms stay here. Same language, same answers.
    using relay::board::Model;
    QCOMPARE(Model::plainTerms(QString()), QStringList{});
    QCOMPARE(Model::plainTerms(QStringLiteral("label:voice status:ready @agent #K7Q2 waiting:me "
                                              "folder:changes")),
             QStringList{});
    QCOMPARE(Model::plainTerms(QStringLiteral("label:voice hotline  dana")),
             (QStringList{QStringLiteral("hotline"), QStringLiteral("dana")}));

    Card card;
    card.id = QStringLiteral("K7Q2");
    card.title = QStringLiteral("Voice transcription mode");
    card.status = QStringLiteral("ready");
    card.labels = QStringList{QStringLiteral("voice")};
    // Nothing on the row says "hotline": the word is in the card's body, which only the worker
    // has now, and `card.text` is empty from a current worker.
    const QSet<QString> matched{QStringLiteral("K7Q2")};
    const QSet<QString> none;
    QVERIFY(Model::matches(card, QStringLiteral("hotline"), &matched));
    QVERIFY(!Model::matches(card, QStringLiteral("hotline"), &none));
    // The scoped terms are still decided here, and they compose with the worker's answer.
    QVERIFY(Model::matches(card, QStringLiteral("status:ready hotline"), &matched));
    QVERIFY(!Model::matches(card, QStringLiteral("status:inbox hotline"), &matched));
    QVERIFY(!Model::matches(card, QStringLiteral("status:ready hotline"), &none));
    // An all-scoped filter needs no answer at all: an empty id set must not hide it.
    QVERIFY(Model::matches(card, QStringLiteral("status:ready"), &none));
    QVERIFY(Model::matches(card, QString(), &none));

    // The model holds one answer and uses it only for the words it answers. Until the answer for
    // what is in the box arrives, the row's own fields decide — so a title match is instant.
    Model model;
    model.setConfig(config());
    QJsonObject object = row("K7Q2", "ready", "features");
    object.insert(QStringLiteral("title"), QStringLiteral("Voice transcription mode"));
    model.reset(QJsonArray{object});
    model.setFilter(QStringLiteral("hotline"));
    QCOMPARE(model.openCount(), 0);                 // no field holds it, no answer yet
    model.setSearchResult(QStringLiteral("hotline"), matched);
    QCOMPARE(model.openCount(), 1);
    model.setFilter(QStringLiteral("hotline t"));   // another keystroke: the answer is stale
    QCOMPARE(model.openCount(), 0);
    model.setFilter(QStringLiteral("transcription"));
    QCOMPARE(model.openCount(), 1);                 // the title, without waiting for the worker
    model.setSearchResult(QStringLiteral("transcription"), none);
    QCOMPARE(model.openCount(), 0);                 // and the worker's word is final
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

// A pane claims a card (#R9G7): `board_claim` writes its session token into the front matter and
// the row carries it, so the list says who is already on the card before a second agent starts on
// it. The chip is the token's first eight characters behind the session glyph, and it says
// `closed` once the pane that claimed it has gone — the claim is still the record of who took it.
void BoardModelTests::aClaimedCardCarriesThePanesSession()
{
    QJsonObject json = row("K7Q2", "executing", "features");
    json.insert(QStringLiteral("session"), QStringLiteral("9f3a7c21d4e5b6a7"));
    const Card card = Card::fromJson(json);
    QCOMPARE(card.session, QStringLiteral("9f3a7c21d4e5b6a7"));
    // A card nobody claimed has no session and no chip.
    QVERIFY(Card::fromJson(row("M3XJ", "ready", "features")).session.isEmpty());
    QVERIFY(relay::board::sessionChip(QString(), true).isEmpty());

    QCOMPARE(relay::board::sessionChip(card.session, true), QStringLiteral("\u29C9 9f3a7c21"));
    QCOMPARE(relay::board::sessionChip(card.session, false),
             QStringLiteral("\u29C9 9f3a7c21 closed"));

    // Last in reading order, after the facts about the card itself.
    const QList<relay::board::Badge> live = relay::board::badges(card, false);
    QCOMPARE(live.size(), 1);
    QCOMPARE(live.last().kind, relay::board::Badge::Session);
    QCOMPARE(live.last().text, QStringLiteral("\u29C9 9f3a7c21"));
    const QList<relay::board::Badge> gone = relay::board::badges(card, false, false);
    QCOMPARE(gone.last().kind, relay::board::Badge::SessionClosed);
    QCOMPARE(gone.last().text, QStringLiteral("\u29C9 9f3a7c21 closed"));

    // The live claim is the last badge a narrow row gives up; the closed one goes early, with
    // the labels, because it is history rather than news.
    QVERIFY(relay::board::badgeDropOrder(relay::board::Badge::Session)
            > relay::board::badgeDropOrder(relay::board::Badge::Status));
    QVERIFY(relay::board::badgeDropOrder(relay::board::Badge::SessionClosed)
            < relay::board::badgeDropOrder(relay::board::Badge::Assignee));
}

// The claim on a row (#R9G7). The chip is a glyph and eight characters, which says nothing on its
// own — the row's tooltip is where it is spelled out, and where the row says whether that pane is
// still open. Liveness is the view's `paneExists`, which the window answers with its pane-by-token
// lookup; unset — this test's first half, and any window that cannot look — reads every claim as
// live, so a board with no window behind it never draws a card as abandoned.
void BoardModelTests::aClaimedRowSaysWhichPaneHoldsItAndWhetherItIsStillOpen()
{
    const QString token = QStringLiteral("9f3a7c21d4e5b6a7");
    QJsonObject claimed = row("K7Q2", "in-progress", "features");
    claimed.insert(QStringLiteral("session"), token);
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.handleEvent(::opened({claimed, row("M3XJ", "inbox", "features")}));
    view.setCollapsedSections(QJsonArray{});

    QListWidget *list = listOf(view);
    QVERIFY(list);
    const auto tipOf = [list](const QString &cardId) {
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->data(Qt::UserRole).toString() == cardId)
                return list->item(i)->toolTip();
        return QString();
    };
    QVERIFY(tipOf(QStringLiteral("K7Q2"))
                .contains(QStringLiteral("Claimed by the pane \u29C9 9f3a7c21")));
    QVERIFY(!tipOf(QStringLiteral("K7Q2")).contains(QStringLiteral("closed")));
    QVERIFY(!tipOf(QStringLiteral("M3XJ")).contains(QStringLiteral("Claimed by")));   // nobody's

    // The window is asked again on every refill rather than once when the board loads, so the pane
    // that took the card closing while the board is up shows up at the list's next redraw.
    QStringList asked;
    view.paneExists = [&asked](const QString &t) {
        asked << t;
        return false;
    };
    view.rebuild();
    QVERIFY(asked.contains(token));
    QVERIFY(!asked.contains(QString()));         // an unclaimed row asks about nothing
    QVERIFY(tipOf(QStringLiteral("K7Q2"))
                .contains(QStringLiteral("Claimed by the pane \u29C9 9f3a7c21, which has closed")));
}

// The same chip on the card page (#R9G7), beside who the card is assigned to. While that pane is
// open it is the `relay-pane:` anchor the thread's "Executing (xxxxxxxx)" entry already uses, so
// one click reveals the pane through the same handler. Once the pane has gone the chip stays — the
// claim is the record of who took the card — but says `closed`, is muted, and is not a link.
void BoardModelTests::theCardPageLinksTheClaimToThePaneAndMutesAClosedOne()
{
    const QString token = QStringLiteral("9f3a7c21d4e5b6a7");
    QJsonObject claimed = row("K7Q2", "in-progress", "features");
    claimed.insert(QStringLiteral("session"), token);
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(::opened({claimed}));
    view.setCollapsedSections(QJsonArray{});

    QJsonObject page = card("K7Q2", "Voice mode", "the issue", QString(64, QLatin1Char('a')));
    page.insert(QStringLiteral("front"), QJsonObject{{QStringLiteral("assignee"), QStringLiteral("agent")},
                                                     {QStringLiteral("session"), token}});
    openCard(view, sent, page);
    QVERIFY(view.detailOpen());
    auto *meta = view.findChild<QLabel *>(QStringLiteral("boardCardMeta"));
    QVERIFY(meta);
    QVERIFY(meta->text().contains(QStringLiteral("session")));
    QVERIFY(meta->text().contains(QStringLiteral("href=\"relay-pane:9f3a7c21d4e5b6a7\"")));
    QVERIFY(meta->text().contains(QStringLiteral("\u29C9 9f3a7c21")));

    // The anchor is the thread's, so it reaches the thread's handler: reveal that pane.
    QStringList revealed;
    view.onFocusPane = [&revealed](const QString &t) { revealed << t; };
    QVERIFY(QMetaObject::invokeMethod(meta, "linkActivated",
                                      Q_ARG(QString, QStringLiteral("relay-pane:") + token)));
    QCOMPARE(revealed, QStringList{token});

    // The pane has gone: the token stays, with `closed`, and there is nothing left to click.
    view.paneExists = [](const QString &) { return false; };
    view.closeDetail();
    sent.clear();
    openCard(view, sent, page);
    QVERIFY(meta->text().contains(QStringLiteral("\u29C9 9f3a7c21 closed")));
    QVERIFY(!meta->text().contains(QStringLiteral("relay-pane:")));
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
// #CYM9: the owner deletes a card from the detail's button or the Del key on the selection,
// after one confirm; the write's notice carries Undo and the removal events that follow do not
// talk over it. Cancel changes nothing.
void BoardModelTests::theDeleteKeyAndButtonDeleteTheCardAndTheUndoToastSurvives()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.setCollapsedSections(QJsonArray{});
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features"), row("M3XJ", "ready", "features")}));
    view.selectCard(QStringLiteral("K7Q2"));
    QListWidget *list = listOf(view);
    QVERIFY(list);

    // The Del key on the selected card, confirmed: one board_delete naming that card.
    answerDeleteConfirm(true);
    QTest::keyClick(list, Qt::Key_Delete);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_delete"));
    QCOMPARE(sent.last().value("card").toString(), QStringLiteral("K7Q2"));
    const QString requestId = sent.last().value("id").toString();
    QVERIFY(!requestId.isEmpty());

    // The write lands, then the removal events follow: the notice names the delete and keeps
    // its Undo, instead of being replaced by "#K7Q2 was removed from the board.".
    view.handleEvent(QJsonObject{{"event", "board_written"}, {"id", requestId},
                                 {"kind", "board_delete"}, {"card_id", "K7Q2"},
                                 {"write_id", "w-77"}, {"removed", true}});
    view.handleEvent(QJsonObject{{"event", "board_changed"}, {"upserts", QJsonArray{}},
                                 {"removed", QJsonArray{"K7Q2"}}});
    QCOMPARE(view.notice(), QStringLiteral("Deleted #K7Q2"));
    view.undoLast();
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_undo"));
    QCOMPARE(sent.last().value("write_id").toString(), QStringLiteral("w-77"));

    // Another pane's delete of the card this pane has open is still news, not an echo.
    view.handleEvent(opened({row("M3XJ", "ready", "features")}));
    openCard(view, sent, QJsonObject{{"event", "board_card"}, {"card_id", "M3XJ"},
                                    {"title", "M3XJ card"}, {"status", "ready"}, {"tab", "features"},
                                    {"body", "text"}, {"thread", QJsonArray{}}, {"thread_total", 0}});
    QVERIFY(view.detailOpen());
    sent.clear();
    view.handleEvent(QJsonObject{{"event", "board_changed"}, {"upserts", QJsonArray{}},
                                 {"removed", QJsonArray{"M3XJ"}}});
    QCOMPARE(view.notice(), QStringLiteral("#M3XJ was removed from the board."));
    QVERIFY(!view.detailOpen());

    // The detail's own button asks the same question; Cancel sends nothing.
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    openCard(view, sent, QJsonObject{{"event", "board_card"}, {"card_id", "K7Q2"},
                                    {"title", "K7Q2 card"}, {"status", "inbox"}, {"tab", "features"},
                                    {"body", "text"}, {"thread", QJsonArray{}}, {"thread_total", 0}});
    QVERIFY(view.detailOpen());
    auto *remove = view.findChild<QToolButton *>(QStringLiteral("boardCardDelete"));
    QVERIFY(remove);
    QVERIFY(!remove->isHidden());
    sent.clear();
    answerDeleteConfirm(false);
    remove->click();
    QVERIFY(sent.isEmpty());

    // Confirmed, it deletes the open card.
    answerDeleteConfirm(true);
    remove->click();
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_delete"));
    QCOMPARE(sent.last().value("card").toString(), QStringLiteral("K7Q2"));
}

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
    Consoles consoles(view);   // Clean up and Check are the console's row now (#AGNT)
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
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
    openCard(view, sent, QJsonObject{{"event", "board_card"}, {"card_id", "K7Q2"}, {"title", "K7Q2 card"},
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
    sent.clear();
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
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
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
    openCard(view, sent, QJsonObject{{"event", "board_card"}, {"card_id", "K7Q2"},
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
    openCard(view, sent, QJsonObject{{"event", "board_card"}, {"card_id", "K7Q2"}, {"title", "K7Q2 card"},
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
    Consoles consoles(view);   // the console the window would make (#AGNT)
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    openCard(view, sent, QJsonObject{{"event", "board_card"}, {"card_id", "K7Q2"}, {"title", "K7Q2 card"},
                                 {"status", "inbox"}, {"tab", "features"}, {"body", "text"},
                                 {"thread", QJsonArray{}}, {"thread_total", 0}});
    QPlainTextEdit *reply = replyBox(view);
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

// Card #CTRN, owner decision 4: a Discuss or Plan on a card is an ordinary console turn, so the
// turn is drawn by the card's **console** — the pane's own thinking fold, its tool rows, its
// answer as it streams — and the thread view shows settled entries only. Until this card the same
// bytes were drawn twice: `deliverToConsoles` fanned them to every console of the tab and this
// page rendered them again as a live tail, where every tool call collapsed into one elided
// progress line. #9K5H's trace-in-the-thread is what that live tail was, and it is the console's
// fold now.
void BoardModelTests::theThinkingTraceRunsInTheCardsConsole()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    Consoles consoles(view);   // the console the window would make (#AGNT)
    view.setTabId(QStringLiteral("tab-7"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features"), row("M3XJ", "inbox", "features")}));
    const auto cardArrived = [](const QString &id) {
        return QJsonObject{{"event", "board_card"}, {"card_id", id}, {"title", id + QStringLiteral(" card")},
                           {"status", "inbox"}, {"tab", "features"}, {"body", "text"},
                           {"thread", QJsonArray{}}, {"thread_total", 0}};
    };
    openCard(view, sent, cardArrived(QStringLiteral("K7Q2")));
    auto *doc = view.findChild<QTextBrowser *>(QStringLiteral("boardCardDocument"));
    QVERIFY(doc);
    const auto text = [doc] { return doc->toPlainText(); };

    // The console the turn is drawn in, and the conversation behind it: one per card, persisted
    // per (tab, card) — owner decision 1 on #CTRN, which is what lets two cards run at once.
    FakeConsole *console = consoles.card();
    QVERIFY(console);
    QCOMPARE(console->liveSpec().surface, QStringLiteral("card:K7Q2"));
    QCOMPARE(console->liveSpec().persistScope, QStringLiteral("helper"));
    QCOMPARE(console->liveSpec().persistKey, QStringLiteral("tab-7/card:K7Q2"));
    // The transcript *is* the live view of the turn, so it is never hidden until a byte arrives.
    QCOMPARE(console->hideCalls, 0);

    QPlainTextEdit *reply = replyBox(view);
    QVERIFY(reply);
    reply->setPlainText(QStringLiteral("Plan the trace."));
    QTest::keyClick(reply, Qt::Key_Return);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_ask"));
    QCOMPARE(sent.last().value("mode").toString(), QStringLiteral("discuss"));
    // The surface the turn's events come back tagged with, on the message that starts it.
    QCOMPARE(sent.last().value("surface").toString(), QStringLiteral("card:K7Q2"));

    // The strip says a turn is running and carries the ✕ that stops it, and that is all it is:
    // the progress line the tool rows replace is gone, widget and all.
    auto *strip = view.findChild<QWidget *>(QStringLiteral("boardBusyStrip"));
    QVERIFY(strip && !strip->isHidden());
    QVERIFY2(!view.findChild<QLabel *>(QStringLiteral("boardBusyWhat")), "the progress line is back");

    // Every event of a running card turn, and not one of them draws anything here.
    const QString running = text();
    for (const QJsonObject &event :
         {QJsonObject{{"event", "thinking_delta"}, {"card_id", "K7Q2"}, {"turn_id", "t-1"},
                      {"text", QStringLiteral("The card asks for the trace in the thread.\n")}},
          QJsonObject{{"event", "thinking_done"}, {"card_id", "K7Q2"}, {"turn_id", "t-1"},
                      {"elapsed_ms", 4200}},
          QJsonObject{{"event", "tool_started"}, {"card_id", "K7Q2"}, {"turn_id", "t-1"},
                      {"tool", "board_read"}},
          QJsonObject{{"event", "tool_result"}, {"card_id", "K7Q2"}, {"turn_id", "t-1"},
                      {"tool", "board_read"}},
          QJsonObject{{"event", "status"}, {"card_id", "K7Q2"},
                      {"text", "Requesting model · step 4/256"}},
          QJsonObject{{"event", "delta"}, {"card_id", "K7Q2"}, {"turn_id", "t-1"},
                      {"text", QStringLiteral("The plan: draw it in the console.")}}})
        view.handleEvent(event);
    QTest::qWait(120);   // longer than the 40 ms timer the live tail used to coalesce on
    QCOMPARE(text(), running);
    QVERIFY(!text().contains(QStringLiteral("thinking")));
    QVERIFY(!text().contains(QStringLiteral("thought for")));
    QVERIFY(!text().contains(QStringLiteral("The plan: draw it in the console.")));
    QVERIFY(!text().contains(QStringLiteral("board_read")));

    // Another card while that one runs: this page is idle and shows nothing of the other's turn.
    openCard(view, sent, cardArrived(QStringLiteral("M3XJ")));
    QVERIFY(strip->isHidden());
    QVERIFY(!text().contains(QStringLiteral("The plan: draw it in the console.")));
    openCard(view, sent, cardArrived(QStringLiteral("K7Q2")));
    QVERIFY(!strip->isHidden());   // still working, and the strip comes back with the card

    // What lands in the thread is the entry the worker writes, with its provenance (decision 2).
    view.handleEvent(QJsonObject{{"event", "board_thread_appended"}, {"card_id", "K7Q2"},
                                 {"entry_id", "20260921T210002Z-bb"}, {"author", "agent"},
                                 {"kind", "comment"},
                                 {"attrs", QJsonObject{{"mode", "discuss"}, {"model", "glm-5"}}},
                                 {"text", QStringLiteral("The plan: draw it in the console.")}});
    QVERIFY2(text().contains(QStringLiteral("✦ agent  Discuss · glm-5")), qPrintable(text()));
    QVERIFY(text().contains(QStringLiteral("The plan: draw it in the console.")));
    view.handleEvent(QJsonObject{{"event", "done"}, {"card_id", "K7Q2"}, {"turn_id", "t-1"}});
    QVERIFY(strip->isHidden());
    QVERIFY(text().contains(QStringLiteral("The plan: draw it in the console.")));   // the record stays
}

// #S53Z: bare labels copy filter terms; only known #ID references become links.
// What is already a link or code keeps its meaning.
void BoardModelTests::labelClicksCopyFiltersAndCardRefsZoom()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    QJsonObject withLabel = row("M3XJ", "inbox", "features");
    withLabel.insert(QStringLiteral("labels"), QJsonArray{QStringLiteral("bug")});
    view.handleEvent(opened({row("K7Q2", "inbox", "features"), withLabel}));
    view.setCollapsedSections(QJsonArray{});

    const QString body = QStringLiteral(
        "# M3XJ card\n\n## Issue\nA bug #bug and a ref #K7Q2; a missing #ZZZZ stays plain "
        "text.\n\nSee [#bug](http://example.com/x) and:\n\n```cpp\n#include <vector>\n```\n");
    const QJsonArray thread{QJsonObject{{"entry_id", "20260920T120000Z-aa"},
                                       {"author", "owner"}, {"kind", "comment"},
                                       {"text", QStringLiteral("Also #bug here, ref #K7Q2.")}}};
    openCard(view, sent, QJsonObject{{"event", "board_card"},
                                     {"card_id", "M3XJ"},
                                     {"title", "M3XJ card"},
                                     {"status", "inbox"},
                                     {"tab", "features"},
                                     {"path", "issues/features/M3XJ.md"},
                                     {"front", QJsonObject{{"labels",
                                                           QJsonArray{QStringLiteral("bug")}}}},
                                     {"body", body},
                                     {"thread", thread},
                                     {"thread_total", 1}});

    // What the document carries: only the meta label has a tag: anchor; the
    // reference a card: one, the Markdown link's own href untouched, the fenced code bare.
    auto *doc = view.findChild<QTextBrowser *>(QStringLiteral("boardCardDocument"));
    QVERIFY(doc);
    view.resize(1100, 700);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QStringList hrefs;
    QString linked;
    for (QTextBlock block = doc->document()->begin(); block.isValid(); block = block.next())
        for (QTextBlock::iterator piece = block.begin(); !piece.atEnd(); ++piece) {
            const QTextFragment fragment = piece.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isAnchor())
                continue;
            const QString href = fragment.charFormat().anchorHref();
            hrefs << href;
            if (href == QStringLiteral("http://example.com/x"))
                linked += fragment.text();
        }
    QCOMPARE(hrefs.count(QStringLiteral("tag:bug")), 0);    // body and thread are plain
    QVERIFY(!hrefs.contains(QStringLiteral("tag:ZZZZ")));    // unknown: plain text
    QVERIFY(hrefs.contains(QStringLiteral("card:K7Q2")));   // a card on the board
    QVERIFY(!hrefs.contains(QStringLiteral("tag:include")));   // the fence keeps its meaning
    QCOMPARE(linked, QStringLiteral("#bug"));   // the Markdown link kept its own href

    // A tag copies with a notice; a card reference zooms without touching the clipboard.
    QApplication::clipboard()->setText(QString());
    // Click the rendered document anchor, including under Xvfb with the xcb platform.
    const auto anchorPoint = [doc](const QString &href) {
        for (int y = 0; y < doc->viewport()->height(); ++y)
            for (int x = 0; x < doc->viewport()->width(); ++x)
                if (doc->anchorAt(QPoint(x, y)) == href)
                    return QPoint(x, y);
        return QPoint();
    };
    auto *meta = view.findChild<QLabel *>(QStringLiteral("boardCardMeta"));
    QVERIFY(meta);
    QVERIFY(meta->isVisible());
    QVERIFY(meta->text().contains(QStringLiteral(">bug</a>")));
    QVERIFY(!meta->text().contains(QStringLiteral(">#bug</a>")));
    for (int y = 2; y < meta->height() && QApplication::clipboard()->text().isEmpty(); y += 4)
        for (int x = 2; x < meta->width() && QApplication::clipboard()->text().isEmpty(); x += 4)
            QTest::mouseClick(meta, Qt::LeftButton, {}, QPoint(x, y));
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("label:bug"));
    QCOMPARE(view.notice(), QStringLiteral("Copied label:bug"));
    // And the same copy toasts "label:bug copied" (#Y2F4) — the copy-on-highlight popup, so the copy
    // is said where the eye is and not only in the notice line over the list.
    auto *toast = view.findChild<QLabel *>(QStringLiteral("toast"));
    QVERIFY(toast);
    QVERIFY(!toast->isHidden());
    QCOMPARE(toast->text(), QStringLiteral("label:bug copied"));
    const QString evidence = qEnvironmentVariable("RELAY_LABEL_TEST_SCREENSHOT");
    if (!evidence.isEmpty())
        QVERIFY(view.grab().save(evidence));
    QApplication::clipboard()->setText(QStringLiteral("untouched"));
    const QPoint refPoint = anchorPoint(QStringLiteral("card:K7Q2"));
    QVERIFY(!refPoint.isNull());
    QTest::mouseClick(doc->viewport(), Qt::LeftButton, {}, refPoint);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("untouched"));
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_card_get"));
    QCOMPARE(sent.last().value("card").toString(), QStringLiteral("K7Q2"));

    // The meta link handler carries the same copy.
    QVERIFY(meta->text().contains(QStringLiteral("href=\"tag:bug\"")));
    QVERIFY(!meta->text().contains(QStringLiteral(">#bug</a>")));
    Q_EMIT meta->linkActivated(QStringLiteral("tag:bug"));
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("label:bug"));
    QCOMPARE(view.notice(), QStringLiteral("Copied label:bug"));

    // A row's label badge copies without moving the selection; the rest of the row selects.
    QListWidget *list = listOf(view);
    QVERIFY(list);
    view.resize(1100, 700);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    view.selectCard(QStringLiteral("M3XJ"));
    const QRect rowRect = list->visualItemRect(list->currentItem());
    QVERIFY(rowRect.isValid());
    const int y = rowRect.center().y();
    QApplication::clipboard()->setText(QString());
    int hit = -1;
    // The badges sit at the row's right end, left of the date columns; a sweep finds the one
    // label badge wherever this platform's metrics put it, and no other badge intercepts.
    for (int x = rowRect.right() - 6; x > rowRect.right() - 320 && x > rowRect.left() + 90;
         x -= 6) {
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, QPoint(x, y));
        if (QApplication::clipboard()->text() == QStringLiteral("label:bug")) {
            hit = x;
            break;
        }
    }
    QVERIFY(hit > 0);
    QCOMPARE(view.notice(), QStringLiteral("Copied label:bug"));
    QCOMPARE(toast->text(), QStringLiteral("label:bug copied"));   // the badge toasts too (#Y2F4)
    // At the pane's bottom-right, where a pane's own toast sits: placing it before showing it
    // leaves a never-shown widget at the top-left instead (#Y2F4).
    QVERIFY(toast->geometry().right() > view.width() - 60);
    QVERIFY(toast->geometry().bottom() > view.height() - 120);
    view.selectCard(QStringLiteral("K7Q2"));
    QApplication::clipboard()->setText(QString());
    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, QPoint(hit, y));
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("label:bug"));
    QCOMPARE(view.selectedCard(), QStringLiteral("K7Q2"));   // the badge never selects
    QApplication::clipboard()->setText(QStringLiteral("keep"));
    // On the title, past the id column and its ⧉ (#FT77): the row still selects as before.
    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, QPoint(rowRect.left() + 130, y));
    QCOMPARE(view.selectedCard(), QStringLiteral("M3XJ"));
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("keep"));

    // Pasting the copied badge term into the real filter keeps only labelled cards.
    QApplication::clipboard()->setText(QStringLiteral("label:bug"));
    auto *filter = view.findChild<QLineEdit *>(QStringLiteral("boardFilter"));
    QVERIFY(filter);
    filter->paste();
    QVERIFY(relay::board::rowOfCard(view.rows(), QStringLiteral("M3XJ")) >= 0);
    QCOMPARE(relay::board::rowOfCard(view.rows(), QStringLiteral("K7Q2")), -1);
}

// The ⧉ beside a card's `#ID` (#FT77): on the card page's header and beside every row's id — one
// click and the reference is on the clipboard, with the notice and the toast, and the row is
// never selected by it.
void BoardModelTests::theRefCopyButtonCopiesTheIdInTheRowsAndTheHeader()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    Consoles consoles(view);   // Clean up and Check are the console's row now (#AGNT)
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    QStringList hinted;
    view.onHint = [&hinted](const QString &id, const QString &keys) { hinted << id << keys; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features"), row("M3XJ", "inbox", "features")}));
    view.setCollapsedSections(QJsonArray{});
    view.resize(1100, 700);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    // The card page's ⧉: one click and the reference is on the clipboard, with the notice, the
    // toast, and the one-off `y` hint (the click is the slow path).
    openCard(view, sent, QJsonObject{{"event", "board_card"}, {"card_id", "M3XJ"},
                                     {"title", "M3XJ card"}, {"status", "inbox"},
                                     {"tab", "features"}, {"path", "issues/features/M3XJ.md"},
                                     {"front", QJsonObject{}}, {"body", "a plain body"},
                                     {"thread", QJsonArray{}}, {"thread_total", 0}});
    QApplication::clipboard()->setText(QString());
    auto *refCopy = view.findChild<QToolButton *>(QStringLiteral("boardCardRefCopy"));
    QVERIFY(refCopy);
    QTest::mouseClick(refCopy, Qt::LeftButton);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("#M3XJ"));
    QCOMPARE(view.notice(), QStringLiteral("Copied #M3XJ"));
    auto *toast = view.findChild<QLabel *>(QStringLiteral("toast"));
    QVERIFY(toast);
    QVERIFY(!toast->isHidden());
    QCOMPARE(toast->text(), QStringLiteral("#M3XJ copied"));
    QVERIFY(hinted.contains(QStringLiteral("copyId")));
    hinted.clear();

    // The row's ⧉: the id column is a fixed strip at the row's left; a sweep over it finds the
    // glyph wherever this platform's metrics put it.
    QListWidget *list = listOf(view);
    QVERIFY(list);
    view.selectCard(QStringLiteral("M3XJ"));
    const QRect rowRect = list->visualItemRect(list->currentItem());
    QVERIFY(rowRect.isValid());
    const int y = rowRect.center().y();
    QApplication::clipboard()->setText(QString());
    int hit = -1;
    for (int x = rowRect.left() + 30; x < rowRect.left() + 110; x += 4) {
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, QPoint(x, y));
        if (QApplication::clipboard()->text() == QStringLiteral("#M3XJ")) {
            hit = x;
            break;
        }
    }
    QVERIFY(hit > 0);
    QCOMPARE(view.notice(), QStringLiteral("Copied #M3XJ"));
    QCOMPARE(toast->text(), QStringLiteral("#M3XJ copied"));
    QVERIFY(hinted.contains(QStringLiteral("copyId")));

    // It is a copy, not a selection: stand the keyboard on K7Q2 and click the ⧉ again — the
    // sweep's own clicks on the id text did select the row, as any row click does.
    view.selectCard(QStringLiteral("K7Q2"));
    QApplication::clipboard()->setText(QStringLiteral("keep"));
    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, QPoint(hit, y));
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("#M3XJ"));
    QCOMPARE(view.selectedCard(), QStringLiteral("K7Q2"));
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
    Consoles consoles(view);   // Clean up and Check are the console's row now (#AGNT)
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features"), row("M3XJ", "inbox", "features")}));
    openCard(view, sent, card("K7Q2", "K7Q2 card", "the issue", "h1"));
    QVERIFY(view.detailOpen());
    auto *document = view.findChild<QTextBrowser *>(QStringLiteral("boardCardDocument"));
    QVERIFY(document);
    const QString threadBefore = document->toPlainText();

    QVERIFY(cleanupButton(view));
    // The label carries the letter that clicks it from the list (#PBX1, owner: "it should
    // have the letter hotkeys for each switchboard action as well").
    QCOMPARE(cleanupButton(view)->text(), QStringLiteral("Clean up (u)"));
    sent.clear();
    // The button is looked up again after every click: the row is rebuilt from
    // `BoardContext::actions()` whenever the run's state moves, so the widget is a new one.
    cleanupButton(view)->click();

    // A preview, not the real thing: a cleanup rewrites the owner's files, so the button never
    // starts one that writes.
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_cleanup"));
    QCOMPARE(sent.last().value("dry_run").toBool(), true);
    const QString runId = sent.last().value("id").toString();
    QVERIFY(!runId.isEmpty());
    QVERIFY(view.cleanupRunning());
    QTRY_COMPARE(cleanupButton(view)->text(), QStringLiteral("Stop (u)"));   // the word changes, the letter does not

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
    QTRY_COMPARE(cleanupButton(view)->text(), QStringLiteral("Clean up (u)"));
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
    Consoles consoles(view);   // Clean up and Check are the console's row now (#AGNT)
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
    Consoles consoles(view);   // the console the window would make (#AGNT)
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    openCard(view, sent, card("K7Q2", "K7Q2 card", "the issue", "h1"));
    QPlainTextEdit *reply = replyBox(view);
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
    Consoles secondConsoles(second);
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
    QTRY_COMPARE(cleanupButton(second)->text(), QStringLiteral("Clean up (u)"));
    QVERIFY(second.notice().contains(QStringLiteral("answering on #K7Q2")));
    QVERIFY(second.notice().contains(QStringLiteral("did not start")));

    // And an ask refused by a cleanup someone else started: off the card, and back in the box.
    relay::BoardView third(QStringLiteral("/tmp/workspace"));
    Consoles thirdConsoles(third);
    QList<QJsonObject> mine;
    third.onSend = [&mine](const QJsonObject &message) { mine << message; };
    third.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    openCard(third, mine, card("K7Q2", "K7Q2 card", "the issue", "h1"));
    QPlainTextEdit *box = replyBox(third);
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
    Consoles consoles(view);   // Clean up and Check are the console's row now (#AGNT)
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    cleanupButton(view)->click();
    const QString runId = sent.last().value("id").toString();
    view.handleEvent(cleanupStarted(runId, false));
    // The row is rebuilt from the context whenever the run's state moves, so the button is a new
    // widget each time: it is looked up again rather than held.
    QTRY_COMPARE(cleanupButton(view)->text(), QStringLiteral("Stop (u)"));   // the word changes, the letter does not

    sent.clear();
    cleanupButton(view)->click();
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("cancel"));
    QVERIFY(view.cleanupRunning());      // still running until the summary says otherwise

    view.handleEvent(cleanupEvent(QStringLiteral("cancelled")));
    view.handleEvent(cleanupSummary(runId, false, QStringLiteral("cancelled"), twoChanges(false)));
    QVERIFY(!view.cleanupRunning());
    QTRY_COMPARE(cleanupButton(view)->text(), QStringLiteral("Clean up (u)"));
    auto *head = view.findChild<QLabel *>(QStringLiteral("boardCleanupHead"));
    QVERIFY(head->text().contains(QStringLiteral("stopped part way")));
    QVERIFY(view.notice().isEmpty());    // the progress line goes with the run
}

// A move's notice is gone in ten seconds; a cleanup's progress line stays up for minutes, so it
// must not be parked on the reply box and the Ask button of a card that has the pane to itself.
void BoardModelTests::theProgressLineKeepsOffAnOpenCardsControls()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    Consoles consoles(view);   // Clean up is the console's row now (#AGNT)
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
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
    openCard(view, sent, card("K7Q2", "K7Q2 card", "the issue", "h1"));
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
    // The notice's `#ID` is a `card:` link: clicking it zooms to the card it names.
    auto *noticeText = view.findChild<QLabel *>(QStringLiteral("boardNoticeText"));
    QVERIFY(noticeText);
    QVERIFY(noticeText->text().contains(QStringLiteral("href=\"card:N3W1\"")));
    view.selectCard(QStringLiteral("K7Q2"));
    emit noticeText->linkActivated(QStringLiteral("card:N3W1"));
    QCOMPARE(view.selectedCard(), QStringLiteral("N3W1"));
    // The worker seeds the new card's `## Issue` with the line that was typed, because that line
    // is the owner's own words and the card format keeps them verbatim.
    openCard(view, sent, card("N3W1", "clickable paths in the output",
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
    openCard(view, sent, card("K7Q2", QStringLiteral("K7Q2 card"), QStringLiteral("clicking a path"),
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
    openCard(view, sent, card("K7Q2", QStringLiteral("K7Q2 card"), QStringLiteral("the ask"),
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
    openCard(view, sent, card("K7Q2", QStringLiteral("K7Q2 card"), QStringLiteral("someone else's words"),
                          QString(64, QLatin1Char('b'))));
    QCOMPARE(issue->toPlainText(), QStringLiteral("the ask, in better words"));
    QVERIFY(!error->isHidden());
    sent.clear();
    QTest::keyClick(issue, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(sent.last().value("base_hash").toString(), QString(64, QLatin1Char('b')));
}

// ---- Discuss / Plan / Execute (#XS6Q, protocol 19.10) --------------------------------------

namespace {
// The action row is the console's since card #AGNT step 6, so its buttons are `QToolButton`s
// built from `Context::actions()` rather than this page's own `QPushButton`s. The text and the
// rule are unchanged, which is the point: `QAbstractButton` is what both have in common.
QAbstractButton *button(relay::BoardView &view, const QString &text)
{
    const auto buttons = view.findChildren<QAbstractButton *>();
    for (QAbstractButton *candidate : buttons)
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
    Consoles consoles(view);   // the console the window would make (#AGNT)
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    openCard(view, sent, card("K7Q2", "K7Q2 card", "the issue", "h1"));
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
    // The strip is the label and the ✕ since card #CTRN: what the turn is doing this second is
    // the tool rows in the console's transcript, not an elided line up here.
    QVERIFY2(!view.findChild<QLabel *>(QStringLiteral("boardBusyWhat")), "the progress line is back");

    // Enter in the reply box discusses.
    QPlainTextEdit *reply = replyBox(view);
    reply->setPlainText(QStringLiteral("Is this still wanted?"));
    QTest::keyClick(reply, Qt::Key_Return);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_ask"));
    QCOMPARE(sent.last().value("mode").toString(), QStringLiteral("discuss"));
    QCOMPARE(sent.last().value("text").toString(), QStringLiteral("Is this still wanted?"));
    // The verb is unchanged (card #CTRN, decision 6) and what it gains is the console's surface:
    // the turn is submitted to that card's own supervisor and its events come back tagged with it.
    QCOMPARE(sent.last().value("surface").toString(), QStringLiteral("card:K7Q2"));
    // While it runs, the strip over the box says which turn it is and carries the one control
    // that ends it. Plan is still offered — it **queues** behind the Discuss (card #CTRN,
    // Planning notes 5) — and a second Enter queues too, in the §12 strip of the card's console;
    // Execute waits, because it hands the card to a terminal pane rather than asking for a turn.
    QVERIFY(!strip->isHidden());
    QCOMPARE(busy->text(), QStringLiteral("✦ Switchboarding · discussing…"));
    QCOMPARE(stop->text(), QStringLiteral("✕ Stop discussing"));
    QTRY_VERIFY(button(view, QStringLiteral("Plan"))->isEnabled());
    QVERIFY(!button(view, QStringLiteral("Execute"))->isEnabled());
    sent.clear();
    reply->setPlainText(QStringLiteral("and this as well"));
    QTest::keyClick(reply, Qt::Key_Return);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_ask"));
    QCOMPARE(sent.last().value("text").toString(), QStringLiteral("and this as well"));
    QVERIFY2(reply->toPlainText().isEmpty(), "the queued prompt was left in the box");
    // The strip goes on naming the turn that is *running*: the one just sent is waiting.
    QCOMPARE(busy->text(), QStringLiteral("✦ Switchboarding · discussing…"));
    view.handleEvent(QJsonObject{{"event", "done"}, {"card_id", "K7Q2"}, {"mode", "discuss"}});
    QVERIFY(strip->isHidden());
    QTRY_VERIFY(button(view, QStringLiteral("Plan"))->isEnabled());

    // `p` plans with an empty box: no text travels, the strip names the plan, and its ✕ cancels.
    sent.clear();
    view.cardAction(QStringLiteral("plan"));
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value("mode").toString(), QStringLiteral("plan"));
    QVERIFY(!sent.last().contains("text"));
    QVERIFY(!strip->isHidden());
    QCOMPARE(busy->text(), QStringLiteral("✦ Switchboarding · planning…"));
    QCOMPARE(stop->text(), QStringLiteral("✕ Stop planning"));
    stop->click();
    // It stops *this card's* turn by name (protocol 19.16): the worker-wide `cancel` would stop
    // whichever turn the worker's own agent is running, which is a cleanup, and would leave the
    // other cards' turns going.
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_cancel"));
    QCOMPARE(sent.last().value("card").toString(), QStringLiteral("K7Q2"));
    QCOMPARE(sent.last().value("surface").toString(), QStringLiteral("card:K7Q2"));
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
    openCard(view, sent, withThread);
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
// card keeps its own strip and its own conversation — since card #CTRN that conversation is the
// card console's, one per (tab, card) — a `done` for one card does not end the other's turn, and
// the list marks the cards that are working.
void BoardModelTests::aCardKeepsItsOwnTurnWhileAnotherCardIsOnScreen()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    Consoles consoles(view);   // the console the window would make (#AGNT)
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features"), row("M3XJ", "inbox", "features")}));
    auto *strip = view.findChild<QWidget *>(QStringLiteral("boardBusyStrip"));
    auto *busy = view.findChild<QLabel *>(QStringLiteral("boardBusyLabel"));
    QVERIFY(strip && busy);

    // Plan #K7Q2. What it is doing while it reads the repository is drawn by that card's console
    // (the tool rows), so the page's job is the strip and the marker on the row.
    openCard(view, sent, card("K7Q2", "K7Q2 card", "the issue", "h1"));
    view.cardAction(QStringLiteral("plan"));
    QCOMPARE(sent.last().value("mode").toString(), QStringLiteral("plan"));
    QCOMPARE(sent.last().value("surface").toString(), QStringLiteral("card:K7Q2"));
    view.handleEvent(QJsonObject{{"event", "status"}, {"card_id", "K7Q2"}, {"mode", "plan"},
                                 {"text", "Requesting model · step 4/256"}});
    QVERIFY(!strip->isHidden());

    // Open #M3XJ while that one runs: this card is idle, and its own Plan is offered.
    openCard(view, sent, card("M3XJ", "M3XJ card", "another issue", "h2"));
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
    openCard(view, sent, card("K7Q2", "K7Q2 card", "the issue", "h1"));
    QVERIFY(strip->isHidden());

    // Back to #M3XJ: still planning, and the strip comes back with it. Its turn's own words are
    // in its console and were never on this page, so the thread view has nothing of them.
    view.handleEvent(QJsonObject{{"event", "status"}, {"card_id", "M3XJ"}, {"mode", "plan"},
                                 {"text", "Requesting model · step 2/256"}});
    view.handleEvent(QJsonObject{{"event", "delta"}, {"card_id", "M3XJ"}, {"mode", "plan"},
                                 {"text", "Looking at the header."}});
    openCard(view, sent, card("M3XJ", "M3XJ card", "another issue", "h2"));
    QVERIFY(!strip->isHidden());
    QCOMPARE(busy->text(), QStringLiteral("✦ Switchboarding · planning…"));
    auto *document = view.findChild<QTextBrowser *>(QStringLiteral("boardCardDocument"));
    QVERIFY(document);
    QVERIFY(!document->toPlainText().contains(QStringLiteral("Looking at the header.")));
    QVERIFY(!document->toPlainText().contains(QStringLiteral("Requesting model")));

    // A fourth card refused while three run says which cards are working, and keeps the text.
    QPlainTextEdit *reply = replyBox(view);
    reply->setPlainText(QStringLiteral("what about this one?"));
    QTest::keyClick(reply, Qt::Key_Return);
    view.handleEvent(QJsonObject{{"event", "error"}, {"code", "board_busy"},
                                 {"cleanup_running", false}, {"card_id", "K7Q2"},
                                 {"cards", QJsonArray{"K7Q2", "R4TT"}},
                                 {"text", "The Switchboard agent is busy with turns on #K7Q2 and #R4TT."}});
    QCOMPARE(reply->toPlainText(), QStringLiteral("what about this one?"));
    QVERIFY(strip->isHidden());
}

void BoardModelTests::executeHandsTheCardToAPaneAndMovesItToExecuting()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    Consoles consoles(view);   // the console the window would make (#AGNT)
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    QString handedCard, handedTask;
    int opened = 0;
    view.onExecuteCard = [&](const QString &id, const QString &task) {
        ++opened;
        handedCard = id;
        handedTask = task;
        return QStringLiteral("pane-session-token-0001");
    };
    view.handleEvent(::opened({row("K7Q2", "ready", "features")}));
    openCard(view, sent, card("K7Q2", "Voice mode", "the issue", QString(64, QLatin1Char('a'))));
    auto *error = view.findChild<QLabel *>(QStringLiteral("boardCardError"));

    // No plan and no acceptance: the first press asks, here on the card, and does nothing else.
    sent.clear();
    view.cardAction(QStringLiteral("execute"));
    QCOMPARE(opened, 0);
    QVERIFY(sent.isEmpty());
    QVERIFY(!error->isHidden());
    QVERIFY(error->text().contains(QStringLiteral("no plan and no acceptance")));

    // The second goes ahead, and the pane it opened makes the hand-off a **claim** (#R9G7): one
    // `board_claim` instead of the three writes Execute sent by hand — the worker does the
    // assignee, the move to Executing, the `session` field and the progress entry naming the pane
    // in one write, so a second agent reading the board cannot catch the card half claimed.
    button(view, QStringLiteral("Execute"))->click();
    QCOMPARE(opened, 1);
    QCOMPARE(handedCard, QStringLiteral("K7Q2"));
    QVERIFY(handedTask.startsWith(QStringLiteral("Execute #K7Q2: Voice mode")));
    QStringList types;
    for (const QJsonObject &message : std::as_const(sent))
        types << message.value("type").toString();
    QCOMPARE(types, (QStringList{"board_claim"}));
    QCOMPARE(sent.at(0).value("card").toString(), QStringLiteral("K7Q2"));
    // The pane it landed in (#HKAP, #R9G7): the token goes into the card's front matter, the
    // Switchboard draws it as the chip that reveals the pane, and the worker's progress entry
    // reads "Claimed (<its first 8 characters>)".
    QCOMPARE(sent.at(0).value("pane_token").toString(), QStringLiteral("pane-session-token-0001"));
    QVERIFY(sent.at(0).value("text").toString().isEmpty());   // nothing was typed under the buttons
    QVERIFY(error->isHidden());

    // And the claim reports itself where the move used to: the pane says which card it took, as
    // soon as the worker says the write landed.
    view.handleEvent(QJsonObject{{"event", "board_written"}, {"id", sent.at(0).value("id")},
                                 {"kind", "board_claim"}, {"card_id", "K7Q2"}, {"write_id", "w1"}});
    QVERIFY2(view.notice().contains(QStringLiteral("Claimed #K7Q2")), qPrintable(view.notice()));

    // A card with a plan goes at once, and the claim is the same one write whatever the card's
    // status and assignee already say — it is the worker's job to write only what is missing.
    QJsonObject planned = card("K7Q2", "Voice mode", "the issue", QString(64, QLatin1Char('b')));
    planned.insert("status", "in-progress");
    planned.insert("sections", QJsonArray{"Issue", "Plan"});
    planned.insert("front", QJsonObject{{"assignee", "agent"}});
    openCard(view, sent, planned);
    sent.clear();
    view.cardAction(QStringLiteral("execute"));
    QCOMPARE(opened, 2);
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_claim"));
    QCOMPARE(sent.last().value("pane_token").toString(), QStringLiteral("pane-session-token-0001"));
    QVERIFY(handedTask.contains(QStringLiteral("`## Plan`")));

    // No pane — the window could open none — so there is nothing to claim the card for and the
    // writes Execute has always made stand: the hash-checked assignee, the move to Executing, and
    // a progress entry that names no pane because there is none to name.
    view.onExecuteCard = [&](const QString &id, const QString &task) {
        ++opened;
        handedCard = id;
        handedTask = task;
        return QString();
    };
    openCard(view, sent, card("K7Q2", "Voice mode", "the issue", QString(64, QLatin1Char('c'))));
    sent.clear();
    view.cardAction(QStringLiteral("execute"));   // this card has no plan again, so it arms once
    view.cardAction(QStringLiteral("execute"));
    QCOMPARE(opened, 3);
    types.clear();
    for (const QJsonObject &message : std::as_const(sent))
        types << message.value("type").toString();
    QCOMPARE(types, (QStringList{"board_update", "board_move", "board_comment"}));
    QCOMPARE(sent.at(0).value("base_hash").toString(), QString(64, QLatin1Char('c')));
    QCOMPARE(sent.at(0).value("patch").toObject().value("fields").toObject().value("assignee").toString(),
             QStringLiteral("agent"));
    // The stage lifecycle (#3XZV, 4f5acd43): what Execute moves a card into is `executing`.
    QCOMPARE(sent.at(1).value("status").toString(), QStringLiteral("executing"));
    QVERIFY(!sent.at(2).contains(QStringLiteral("pane_token")));
    QVERIFY(sent.at(2).value("text").toString().startsWith(
        QStringLiteral("Execute · handed to a new terminal pane")));
}

void BoardModelTests::theExecuteTaskCarriesTheBoardsConventions()
{
    const QString task = relay::board::executeTask(QStringLiteral("XS6Q"), QStringLiteral("Modes"),
                                                   true, true, QStringLiteral("backend first"));
    QVERIFY(task.startsWith(QStringLiteral("Execute #XS6Q: Modes\n")));
    QVERIFY(task.contains(QStringLiteral("until the acceptance holds")));
    // #K3TY: where the plan has one, Execute says to follow its Orchestration block — and a
    // card with no plan cannot carry one, so the no-plan wordings never mention it.
    QVERIFY(task.contains(QStringLiteral("Where the plan has an Orchestration block, follow it")));
    QVERIFY(!relay::board::executeTask(QStringLiteral("XS6Q"), QStringLiteral("Modes"), false, false)
                 .contains(QStringLiteral("Orchestration")));
    QVERIFY(!relay::board::executeTask(QStringLiteral("XS6Q"), QStringLiteral("Modes"), false, true)
                 .contains(QStringLiteral("Orchestration")));
    QVERIFY(task.contains(QStringLiteral("implemented_by")));
    QVERIFY(task.contains(QStringLiteral("links.commits")));
    QVERIFY(task.contains(QStringLiteral("Put #XS6Q in the message of every commit")));
    // The convention the brief carries is the lifecycle's: land it into needs-verification with
    // the evidence and its tests, and its verifier moves it on to QA (#3XZV). Since #WC3E the
    // implementer writes the expectations *before* the work and no checklist at all — that
    // section is the verifying session's record.
    QVERIFY2(task.contains(QStringLiteral("needs-verification")), qPrintable(task));
    QVERIFY2(task.contains(QStringLiteral("write **no** `## QA checklist`")), qPrintable(task));
    QVERIFY2(task.contains(QStringLiteral("`## Done means`")), qPrintable(task));
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
    // The lane decides what a pass and a failure do (#3XZV), so the status is an argument: from
    // needs-verification a pass goes on to QA. Passed by name here, because it sits before the
    // owner's note and a note landing in it silently builds a brief with no note at all.
    const QString task = relay::board::verifyTask(QStringLiteral("T71W"), QStringLiteral("Signatures"),
                                                  QStringLiteral("Codex"),
                                                  QStringLiteral("anthropic/claude-opus-5"),
                                                  QStringLiteral("needs-verification"),
                                                  QStringLiteral("check the Xvfb run too"));
    QVERIFY2(task.contains(QStringLiteral("verify lane (needs-verification)")), qPrintable(task));
    QVERIFY(task.startsWith(QStringLiteral("Verify #T71W: Signatures\n")));
    QVERIFY(task.contains(QStringLiteral("you are its verifier (Codex)")));
    QVERIFY(task.contains(QStringLiteral("anthropic/claude-opus-5 implemented it")));
    QVERIFY(task.contains(QStringLiteral("`## QA checklist`")));
    QVERIFY(task.contains(QStringLiteral("docs/qa_evidence/")));
    // #WC3E: the checklist is this session's own record, against the `## Done means` the card
    // carried before the work; it names the revision, carries three fixed lines, and ends with a
    // dated line that is never left out — "not reviewed" must not read as "no findings".
    QVERIFY2(task.contains(QStringLiteral("There is no checklist waiting for you")), qPrintable(task));
    QVERIFY(task.contains(QStringLiteral("`## Done means`")));
    QVERIFY(task.contains(QStringLiteral("Name the revision you checked")));
    QVERIFY(task.contains(QStringLiteral("tests: passed|failed|missing evidence (revision <sha>)")));
    QVERIFY(task.contains(QStringLiteral("simulation: played|not applicable|could not stage")));
    QVERIFY(task.contains(QStringLiteral("staged: docs/qa_evidence/<today>-verify-T71W/")));
    QVERIFY(task.contains(QStringLiteral("no findings")));
    QVERIFY(task.contains(QStringLiteral("ai-pass.sh")));
    QVERIFY(task.contains(QStringLiteral("`## Human QA`")));
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
    Consoles consoles(view);   // the console the window would make (#AGNT)
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    QString handedCard, handedRunner, handedTask;
    int opened = 0;
    view.onVerifyCard = [&](const QString &id, const QString &runner, const QString &task) {
        ++opened;
        handedCard = id;
        handedRunner = runner;
        handedTask = task;
        return QStringLiteral("pane-session-token-0002");
    };
    QString hintId, hintKeys;
    view.onHint = [&](const QString &id, const QString &keys) { hintId = id; hintKeys = keys; };
    view.handleEvent(::opened({row("K7Q2", "needs-qa-llm", "features")}));
    openCard(view, sent, qaCard(qaBlock()));

    // The line sits under the fields, in the muted ink, and says the whole recommendation.
    auto *line = view.findChild<QLabel *>(QStringLiteral("boardCardVerifyLine"));
    QVERIFY(line);
    QVERIFY(!line->isHidden());
    QVERIFY2(line->text().contains(QStringLiteral("Verify with Codex (installed) · then GLM-5.3 · "
                                                  "Claude skipped: implemented this card")),
             qPrintable(line->text()));

    // The button carries its key like the others (#QG60), and is live because a runner exists.
    QAbstractButton *verify = button(view, QStringLiteral("Verify"));
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
    // Same treatment as Execute (#HKAP): the hand-off names the pane it landed in — its
    // session token rides on the note, the first line reads "Verifying (<its first 8 characters>)".
    QCOMPARE(sent.at(0).value("pane_token").toString(), QStringLiteral("pane-session-token-0002"));
    QVERIFY2(sent.at(0).value("text").toString().startsWith(
                 QStringLiteral("Verifying (pane-ses) · handed to a new terminal pane on Codex · first in the ranking")),
             qPrintable(sent.at(0).value("text").toString()));
    // A click is the slow path, so it says its key once (WARP.md hint rule).
    QCOMPARE(hintId, QStringLiteral("board.verify"));
    QCOMPARE(hintKeys, QStringLiteral("v"));

    // `v` on the card's document does the same, and the reply box goes with it as the owner's note.
    auto *doc = view.findChild<QTextBrowser *>(QStringLiteral("boardCardDocument"));
    QVERIFY(doc);
    QPlainTextEdit *reply = replyBox(view);
    QVERIFY(reply);
    reply->setPlainText(QStringLiteral("watch the Xvfb run"));
    sent.clear();
    QTest::keyClick(doc, Qt::Key_V);
    QCOMPARE(opened, 2);
    QCOMPARE(handedRunner, QStringLiteral("guest:codex"));
    QVERIFY(handedTask.endsWith(QStringLiteral("The owner adds, verbatim:\nwatch the Xvfb run")));
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.at(0).value("pane_token").toString(), QStringLiteral("pane-session-token-0002"));
    QVERIFY(sent.at(0).value("text").toString().endsWith(QStringLiteral("\n\nwatch the Xvfb run")));

    // `v` from the list opens the selected card and verifies it, as `x` executes it.
    view.selectCard(QStringLiteral("K7Q2"));
    view.cardAction(QStringLiteral("verify"));
    QCOMPARE(opened, 3);

    // A card the worker sent no `qa` for: no line, and nothing to press. The row is rebuilt from
    // the context on every card, so the button is looked up again rather than held.
    openCard(view, sent, qaCard(QJsonObject()));
    QVERIFY(line->isHidden());
    QVERIFY(button(view, QStringLiteral("Verify")));
    QVERIFY(!button(view, QStringLiteral("Verify"))->isEnabled());
    sent.clear();
    opened = 0;
    button(view, QStringLiteral("Verify"))->click();
    QCOMPARE(opened, 0);
    QVERIFY(sent.isEmpty());

    // And a card that is not in a QA lane at all does not offer it at all: an action the row must
    // not draw is an action the context does not list, so the button is gone rather than greyed.
    openCard(view, sent, card("K7Q2", "Voice mode", "the issue", QString(64, QLatin1Char('a'))));
    QVERIFY(line->isHidden());
    QVERIFY(!button(view, QStringLiteral("Verify")));
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

    Consoles consoles(view);   // the console the window would make (#AGNT)
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(::opened({row("K7Q2", "needs-qa-llm", "features")}));
    QJsonObject card = qaCard(none);
    openCard(view, sent, card);
    auto *line = view.findChild<QLabel *>(QStringLiteral("boardCardVerifyLine"));
    QVERIFY(line);
    QVERIFY(!line->isHidden());
    QVERIFY2(line->text().contains(free), qPrintable(line->text()));
    QAbstractButton *verify = button(view, QStringLiteral("Verify"));
    QVERIFY(verify);
    QVERIFY(!verify->isEnabled());
    QVERIFY2(verify->toolTip().contains(free), qPrintable(verify->toolTip()));

    // A warning beside a recommendation: the line stands and the note follows it, and Verify works.
    const QString weak = QStringLiteral("The recommended verifier shares the implementer's lineage "
                                        "(cn-open): it shares training data, so it is a weaker check.");
    QJsonObject warned = qaBlock();
    warned.insert(QStringLiteral("note"), weak);
    openCard(view, sent, qaCard(warned));
    QVERIFY(line->text().contains(QStringLiteral("Verify with Codex (installed)")));
    QVERIFY2(line->text().contains(weak.toHtmlEscaped()), qPrintable(line->text()));
    QVERIFY(button(view, QStringLiteral("Verify"))->isEnabled());
}

// ----------------------------------- the board and the card as contexts (#AGNT step 6) ------
//
// The list page and an open card no longer hold a chat widget of their own. Each supplies a
// `relay::agent::Context` — a spec, an action row, a link resolver, a placeholder — and the
// window hands back a console to draw it in. These drive the fake console above, so what is
// tested is what the board *asked the window for*, not what a widget of its own looks like.

namespace {

QJsonObject problem(const QString &path, const QString &message)
{
    return QJsonObject{{"code", "bad_front_matter"}, {"path", path}, {"message", message},
                       {"severity", "error"}};
}

// `board_survey {root, project, hints, counts, proposals, git}` (19.18).
QJsonObject surveyEvent(const QJsonArray &proposals, const QJsonObject &git,
                        const QJsonArray &hints = {}, int items = 0)
{
    return QJsonObject{{"event", "board_survey"}, {"root", "/tmp/workspace/issues"},
                       {"project", "/tmp/workspace"}, {"hints", hints},
                       {"counts", QJsonObject{{"items", items}}},
                       {"proposals", proposals}, {"git", git}};
}

QToolButton *rowButton(relay::BoardView &view, const QString &objectName)
{
    return view.findChild<QToolButton *>(objectName);
}

}  // namespace

// What the list page asks the window for: one console, over a `switchboard` context, keyed by the
// tab (§30.7). The spec is the only part of a context that crosses to the worker, so this is the
// `configure` block the board is responsible for.
void BoardModelTests::theListPageAsksForASwitchboardConsoleKeyedByTheTab()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    Consoles consoles(view);
    view.setTabId(QStringLiteral("tab-7"));
    view.onSend = [](const QJsonObject &) {};
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));

    FakeConsole *console = consoles.board();
    QVERIFY(console);
    QCOMPARE(consoles.made.size(), 1);            // asked for once, not once per redraw
    const relay::agent::ContextSpec spec = console->spec();
    QCOMPARE(spec.name, QStringLiteral("switchboard"));
    QCOMPARE(spec.surface, QStringLiteral("switchboard"));
    QCOMPARE(spec.agentRole, QStringLiteral("switchboard"));
    QCOMPARE(spec.workspace, QStringLiteral("/tmp/workspace"));
    // Named, never inferred — the accident this card found was `Agent.tools()` branching on
    // whether the board had a card scope, so a board-less helper got the full executor.
    QCOMPARE(spec.scope, QStringLiteral("console"));
    // `persist.scope` is a wire enum ("", "pane", "helper"), not a path, and the key is the tab's.
    QCOMPARE(spec.persistScope, QStringLiteral("helper"));
    QCOMPARE(spec.persistKey, QStringLiteral("tab-7"));
    QCOMPARE(spec.briefKey, QStringLiteral("switchboard"));
    QCOMPARE(spec.briefTitle, QStringLiteral("Switchboard agent"));
    QVERIFY(!spec.shell);                         // no shell: the console is the whole surface
    QCOMPARE(spec.routing, QStringLiteral("agent"));
    QVERIFY(console->context()->placeholder().contains(QStringLiteral("a second prompt queues")));

    // What is on screen rides on each ask as `screen`: the sections and their counts, the filter,
    // the open card. It is a hint about what is being read, not a dump of the board.
    QVERIFY2(spec.screen.contains(QStringLiteral("Inbox 1")), qPrintable(spec.screen));

    // A tab id that moves is a different conversation (§30.7's move test), so the context says so
    // and the console re-reads it.
    const int before = console->rebuilds();
    view.setTabId(QStringLiteral("tab-9"));
    QTRY_VERIFY(console->rebuilds() > before);
    QCOMPARE(console->liveSpec().persistKey, QStringLiteral("tab-9"));
    QVERIFY(console->liveSpec().persistId() != spec.persistId());
}

// The row of things that need no typing (owner, 2026-09-20: "we put the 'clean up' button there
// for the main switchboard agent, for example", and "it should have the letter hotkeys for each
// switchboard action as well"). Four actions, two letters, and the letters answer from the board.
void BoardModelTests::theBoardsRowIsCheckCleanUpTestsAndProfileWithTheirLetters()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    Consoles consoles(view);
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    int tests = 0;
    QWidget *profiled = nullptr;
    bool profileCalled = false;
    view.onOpenTestSuites = [&tests] { ++tests; };
    view.onProfile = [&profiled, &profileCalled](QWidget *anchor) {
        profiled = anchor;
        profileCalled = true;
    };
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));

    FakeConsole *console = consoles.board();
    QVERIFY(console);
    QStringList labels;
    for (const relay::agent::Action &action : console->actions())
        labels << action.fullLabel();
    QCOMPARE(labels, QStringList({QStringLiteral("Check (k)"), QStringLiteral("Clean up (u)"),
                                  QStringLiteral("Tests"), QStringLiteral("Profile")}));

    // Each button carries the action's key as its object name, which is what the theme and the
    // tests find it by; Tests and Profile are keyless, and the row does not invent a letter.
    QVERIFY(rowButton(view, QStringLiteral("boardChatCheck")));
    QVERIFY(rowButton(view, QStringLiteral("boardCleanup")));
    sent.clear();
    rowButton(view, QStringLiteral("boardChatCheck"))->click();
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_check"));
    QVERIFY(!sent.last().contains(QStringLiteral("section")));

    rowButton(view, QStringLiteral("boardTests"))->click();
    QCOMPARE(tests, 1);
    rowButton(view, QStringLiteral("boardProfile"))->click();
    QVERIFY(profileCalled);
    // The menu is anchored under the button, and an Action carries no widget — so the button is
    // found by the one name it is guaranteed to have, the action's key.
    QCOMPARE(profiled, static_cast<QWidget *>(rowButton(view, QStringLiteral("boardProfile"))));

    // Clean up runs, and the word on the row becomes Stop while it does. The letter does not move.
    sent.clear();
    rowButton(view, QStringLiteral("boardCleanup"))->click();
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_cleanup"));
    QVERIFY(sent.last().value("dry_run").toBool());       // the first run is always a preview
    QTRY_COMPARE(rowButton(view, QStringLiteral("boardCleanup"))->text(), QStringLiteral("Stop (u)"));
    const QString runId = sent.last().value("id").toString();
    view.handleEvent(cleanupStarted(runId, true));
    view.handleEvent(cleanupSummary(runId, true, QStringLiteral("done"), QJsonArray{}));
    QVERIFY(!view.cleanupRunning());
    QTRY_COMPARE(rowButton(view, QStringLiteral("boardCleanup"))->text(), QStringLiteral("Clean up (u)"));

    // A letter typed on the board page reaches the row through the console's handle, and the key
    // legend names every keyed action without this page listing them.
    sent.clear();
    QKeyEvent key(QEvent::KeyPress, Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
    QApplication::sendEvent(&view, &key);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_check"));
    auto *keys = view.findChild<QLabel *>(QStringLiteral("boardKeys"));
    QVERIFY(keys);
    QVERIFY2(keys->text().contains(QStringLiteral("<b>k</b> check")), qPrintable(keys->text()));
    QVERIFY(keys->text().contains(QStringLiteral("<b>u</b> clean up")));
}

// Check's findings are a board widget above the console — a list to act on, not a turn — and a
// click on one drafts a fix into the composer without sending it (owner, 2026-09-19: "draft you
// confirm").
void BoardModelTests::checkIsUnscopedAndASectionsTriageNamesItsSection()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    Consoles consoles(view);
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features"), row("M3XJ", "ready", "features")}));

    sent.clear();
    rowButton(view, QStringLiteral("boardChatCheck"))->click();
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_check"));
    QVERIFY(!sent.last().contains(QStringLiteral("section")));
    const QString unscoped = sent.last().value("id").toString();

    view.handleEvent(QJsonObject{{"event", "board_problems"}, {"id", unscoped},
                                 {"items", QJsonArray{problem("issues/features/a.md", "no id")}},
                                 {"section", QJsonValue()}});
    auto *findings = view.findChild<QWidget *>(QStringLiteral("boardChatFindings"));
    QVERIFY(findings && !findings->isHidden());

    // A click on the finding drafts the fix in the console's composer. Nothing is sent.
    sent.clear();
    QLabel *finding = nullptr;
    for (QLabel *label : findings->findChildren<QLabel *>())
        if (label->objectName() == QStringLiteral("boardChatFinding"))
            finding = label;
    QVERIFY(finding);
    finding->linkActivated(QStringLiteral("fix"));
    QCOMPARE(consoles.board()->drafts.size(), 1);
    QVERIFY(consoles.board()->drafts.constLast().contains(QStringLiteral("no id")));
    QVERIFY(consoles.board()->editor()->toPlainText().contains(QStringLiteral("a.md")));
    QVERIFY(sent.isEmpty());

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

// The problems banner over the list drafts the same request into the same box.
void BoardModelTests::aProblemDraftsAFixInTheConsoleWithoutSendingIt()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    Consoles consoles(view);
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

    QPlainTextEdit *box = consoles.board()->editor();
    QVERIFY(box->toPlainText().contains(QStringLiteral("2026-09-19-broken.md")));
    QVERIFY(box->toPlainText().contains(QStringLiteral("front matter has no id")));
    QVERIFY(sent.isEmpty());                      // a draft, never a send
}

// The survey is an import form, not a turn (19.18), so it stays a board widget above the console.
void BoardModelTests::theSurveysImportButtonSendsBoardImportApply()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    Consoles consoles(view);
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({}));
    sent.clear();

    view.handleEvent(surveyEvent(
        QJsonArray{QJsonObject{{"title", "Fix the flaky test"},
                               {"source", QJsonObject{{"kind", "todo-md"}, {"path", "TODO.md"},
                                                      {"key", "todo-md:TODO.md#0"}}}},
                   QJsonObject{{"title", "Ship the board"},
                               {"source", QJsonObject{{"kind", "todo-md"}, {"path", "TODO.md"},
                                                      {"key", "todo-md:TODO.md#1"}}}}},
        QJsonObject{{"is_repo", true}, {"primary", "origin"}, {"forge", "github"},
                    {"owner", "relay"}, {"repo", "relay-terminal"},
                    {"url", "git@github.com:relay/relay-terminal.git"}},
        QJsonArray{QJsonObject{{"kind", "vendor"}, {"message", "node_modules/ is left alone"}}}, 2));

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
// sync itself is #ZKR0's surface, so nothing here ever sends `forge_sync_run`.
void BoardModelTests::theSurveyOffersToLookOnGithubAndThatLookWritesNothing()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    Consoles consoles(view);
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({}));
    view.handleEvent(surveyEvent({}, QJsonObject{{"is_repo", true}, {"primary", "origin"},
                                                 {"forge", "github"}, {"owner", "relay"},
                                                 {"repo", "relay-terminal"},
                                                 {"url", "git@github.com:relay/relay-terminal.git"}}));

    auto *look = view.findChild<QToolButton *>(QStringLiteral("boardChatForgeLook"));
    QVERIFY(look);
    sent.clear();
    look->click();
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("forge_sync_plan"));
    QCOMPARE(sent.last().value("repo").toString(), QStringLiteral("relay/relay-terminal"));
    const QString request = sent.last().value("id").toString();
    QVERIFY(!request.isEmpty());
    QVERIFY(!look->isEnabled());

    view.handleEvent(QJsonObject{{"event", "forge_sync_planned"}, {"id", request},
                                 {"creates", 2}, {"pulled", 3}, {"pushed", 0}, {"conflicts", 0}});
    QVERIFY(look->isEnabled());
    QCOMPARE(look->text(), QStringLiteral("Look again"));
    QString result;
    for (QLabel *line : view.findChild<QWidget *>(QStringLiteral("boardChatSurvey"))->findChildren<QLabel *>())
        if (line->text().contains(QStringLiteral("would")))
            result = line->text();
    QVERIFY2(result.contains(QStringLiteral("3 issues would become cards")), qPrintable(result));
    QVERIFY(result.contains(QStringLiteral("Nothing was written on either side")));

    // A failure is an ordinary `error` carrying the same request id, and it says the same thing
    // about nothing being written.
    sent.clear();
    look->click();
    const QString second = sent.last().value("id").toString();
    view.handleEvent(QJsonObject{{"event", "error"}, {"id", second}, {"code", "forge_auth"},
                                 {"text", "no credential"}});
    QString failed;
    for (QLabel *line : view.findChild<QWidget *>(QStringLiteral("boardChatSurvey"))->findChildren<QLabel *>())
        if (line->text().contains(QStringLiteral("credential")))
            failed = line->text();
    QVERIFY2(failed.contains(QStringLiteral("Nothing was written on either side")), qPrintable(failed));
}

// The agent belongs to the list page and is there whenever the board is — including a board with
// no cards at all, which is precisely the board the survey has something to say about.
void BoardModelTests::anEmptyBoardStillShowsTheAgentBecauseThatIsWhereTheSurveyRuns()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    Consoles consoles(view);
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.resize(900, 700);
    view.handleEvent(opened({}));

    QWidget *area = view.findChild<QWidget *>(QStringLiteral("boardChatArea"));
    QWidget *splitter = view.findChild<QSplitter *>();
    QVERIFY(area && splitter);
    QVERIFY(splitter->isHidden());          // no cards: the list and its tools are away
    QVERIFY(!area->isHidden());             // the conversation is not
    QVERIFY(consoles.board());
    QCOMPARE(view.listConsole(), static_cast<QWidget *>(consoles.board()));

    // And the survey it is there for can be seen and acted on.
    view.handleEvent(surveyEvent(
        QJsonArray{QJsonObject{{"title", "Write the README"},
                               {"source", QJsonObject{{"kind", "todo-md"}, {"path", "TODO.md"},
                                                      {"key", "todo-md:TODO.md#0"}}}}},
        QJsonObject{{"is_repo", false}}, {}, 1));
    QWidget *survey = view.findChild<QWidget *>(QStringLiteral("boardChatSurvey"));
    QVERIFY(survey && !survey->isHidden());
    QVERIFY(view.findChild<QToolButton *>(QStringLiteral("boardChatImport")));

    // A card open keeps the page to itself: the board's conversation is the list page's.
    view.resize(500, 700);
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    openCard(view, sent, card("K7Q2", "K7Q2 card", "the issue", "h1"));
    QVERIFY(view.detailOpen());
    QVERIFY(area->isHidden());
    view.closeDetail();
    QVERIFY(!area->isHidden());
}

// `a` puts the keyboard in the console's composer from anywhere on the board, as `/` puts it in
// the filter — and from an open card it goes back to the list first.
void BoardModelTests::theAskKeyFocusesTheConsoleAndACardGoesBackFirst()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    Consoles consoles(view);
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.resize(900, 700);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));

    QPlainTextEdit *box = consoles.board()->editor();
    QVERIFY(!box->hasFocus());
    QTest::keyClick(&view, Qt::Key_A);
    QCOMPARE(consoles.board()->focusCalls, 1);
    QTRY_VERIFY(box->hasFocus());

    // Typing into the composer is typing, not a shortcut: the key reaches the box.
    QTest::keyClicks(box, QStringLiteral("and a"));
    QCOMPARE(box->toPlainText(), QStringLiteral("and a"));

    // From an open card it goes back to the list first, because the console is the list page's.
    openCard(view, sent, card("K7Q2", "K7Q2 card", "the issue", "h1"));
    QVERIFY(view.detailOpen());
    QTest::keyClick(&view, Qt::Key_A);
    QVERIFY(!view.detailOpen());
    QTRY_VERIFY(box->hasFocus());

    // The key line teaches it.
    auto *keys = view.findChild<QLabel *>(QStringLiteral("boardKeys"));
    QVERIFY(keys);
    QVERIFY(keys->text().contains(QStringLiteral("ask the agent")));
}

// The open card's own console: a different conversation from the board's, keyed by the card, with
// the three actions that need no typing on its row.
void BoardModelTests::theCardPageAsksForACardConsoleAndItsActionsFollowTheCard()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    Consoles consoles(view);
    view.setTabId(QStringLiteral("tab-7"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    openCard(view, sent, card("K7Q2", "K7Q2 card", "the issue", "h1"));

    FakeConsole *console = consoles.card();
    QVERIFY(console);
    QCOMPARE(view.cardConsole(), static_cast<QWidget *>(console));
    const relay::agent::ContextSpec spec = console->liveSpec();
    QCOMPARE(spec.name, QStringLiteral("card"));
    QCOMPARE(spec.surface, QStringLiteral("card:K7Q2"));     // what every card turn event carries
    // A card console is a console: step 3 of card #CTRN deleted the `card` tool scope, and the
    // name only still arrived because `agent_context.RETIRED_SCOPES` maps it for older GUIs.
    QCOMPARE(spec.scope, QStringLiteral("console"));
    QCOMPARE(spec.agentRole, QStringLiteral("switchboard"));
    QVERIFY(!spec.shell);
    QCOMPARE(spec.routing, QStringLiteral("agent"));
    // A card is its own conversation, persisted per (tab, card) — owner decision 1 on card
    // #CTRN. The tab id is in the key because a tab owns one worker and that worker owns its
    // conversation files: two tabs on one card are two conversations about it.
    QCOMPARE(spec.persistScope, QStringLiteral("helper"));
    QCOMPARE(spec.persistKey, QStringLiteral("tab-7/card:K7Q2"));
    QVERIFY(spec.persistId() != consoles.board()->liveSpec().persistId());
    // And the transcript is the live view of a card turn since #CTRN, so it is not hidden until
    // something prints in it.
    QCOMPARE(console->hideCalls, 0);
    QVERIFY(console->context()->placeholder().contains(QStringLiteral("Ctrl+Shift+Enter only comments")));


    // Discuss and Comment are what the box does, so they have no buttons; what is left on the row
    // is Plan and the two that leave the board. Verify is not offered outside a QA lane.
    QStringList labels;
    for (const relay::agent::Action &action : console->actions())
        labels << action.fullLabel();
    QCOMPARE(labels, QStringList({QStringLiteral("Plan (p)"), QStringLiteral("Execute (x)")}));
    // Execute and Verify hand the card to a pane, and that is what the accent outline means.
    QVERIFY(!console->actions().at(0).leaves);
    QVERIFY(console->actions().at(1).leaves);

    // What is typed in the console's composer is Execute's note: the box is the reply box.
    QString handedCard, handedTask;
    view.onExecuteCard = [&](const QString &id, const QString &task) {
        handedCard = id;
        handedTask = task;
        return QStringLiteral("tok");
    };
    console->editor()->setPlainText(QStringLiteral("start with the parser"));
    // No plan and no acceptance, so the first press arms and says why; the second goes ahead
    // (#XS6Q). The note stays in the box until the press that actually hands the card over.
    rowButton(view, QStringLiteral("boardExecute"))->click();
    QVERIFY(handedCard.isEmpty());
    QCOMPARE(console->editor()->toPlainText(), QStringLiteral("start with the parser"));
    rowButton(view, QStringLiteral("boardExecute"))->click();
    QCOMPARE(handedCard, QStringLiteral("K7Q2"));
    QVERIFY2(handedTask.contains(QStringLiteral("start with the parser")), qPrintable(handedTask));
    QVERIFY(console->editor()->toPlainText().isEmpty());     // taken out of the box with it

    // While a turn runs on the card, Execute waits — it hands the card to a terminal pane — and
    // Plan does not: it queues behind the running turn (card #CTRN). The row is rebuilt because
    // the context said something moved, not because the page poked a widget.
    sent.clear();
    console->editor()->setPlainText(QStringLiteral("is this still wanted?"));
    QTest::keyClick(console->editor(), Qt::Key_Return);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_ask"));
    QCOMPARE(sent.last().value("mode").toString(), QStringLiteral("discuss"));
    QCOMPARE(sent.last().value("text").toString(), QStringLiteral("is this still wanted?"));
    QTRY_VERIFY(!rowButton(view, QStringLiteral("boardExecute"))->isEnabled());
    QVERIFY(rowButton(view, QStringLiteral("boardReplyButton"))->isEnabled());
    view.handleEvent(QJsonObject{{"event", "done"}, {"card_id", "K7Q2"}, {"mode", "discuss"}});
    QTRY_VERIFY(rowButton(view, QStringLiteral("boardExecute"))->isEnabled());

    // Ctrl+Enter plans and Ctrl+Shift+Enter is a comment with no model call — the three chords
    // the card page has always had, in the box that now answers them.
    sent.clear();
    console->editor()->setPlainText(QStringLiteral("the parser first"));
    QTest::keyClick(console->editor(), Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(sent.last().value("mode").toString(), QStringLiteral("plan"));
    view.handleEvent(QJsonObject{{"event", "done"}, {"card_id", "K7Q2"}, {"mode", "plan"}});
    sent.clear();
    console->editor()->setPlainText(QStringLiteral("a note for later"));
    QTest::keyClick(console->editor(), Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_comment"));
    QCOMPARE(sent.last().value("kind").toString(), QStringLiteral("note"));
    QCOMPARE(sent.last().value("card").toString(), QStringLiteral("K7Q2"));
    QCOMPARE(sent.last().value("text").toString(), QStringLiteral("a note for later"));
    // The chord takes the words with it: a comment that left them in the box would be sent
    // twice by the next Enter. This is the page's half of what the live drive could not get to
    // land — and it lands here, on a card nothing is running on and on one that has just
    // answered, which is what says the failure out there is neither the route nor a busy guard.
    QVERIFY2(console->editor()->toPlainText().isEmpty(), "the comment chord left the words in the box");
    QTest::keyClick(console->editor(), Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(sent.last().value("text").toString(), QStringLiteral("a note for later"));   // not twice
}

// Verify is on the row only in a QA lane (#T71W): on any other card it would be a control for a
// question nobody has asked yet.
// One console, several cards (card #CTRN). The page keeps one console and points it at whatever
// card is open, so the scrollback would bleed from card to card: the conversation and the routing
// follow the card, and until this card the emulator did not. The page tells the console whose
// transcript it is drawing on every card it shows — including the first, so the card being left
// is always the surface the console was last told about — and says it once per card, so a card
// re-read under the same id (a change on disk, protocol #N5JJ) does not wipe what is on screen.
void BoardModelTests::theOneConsoleIsToldWhichCardsTranscriptItIsDrawing()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    Consoles consoles(view);
    view.setTabId(QStringLiteral("tab-7"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features"), row("M3PD", "inbox", "features")}));

    openCard(view, sent, card("K7Q2", "K7Q2 card", "the issue", "h1"));
    FakeConsole *console = consoles.card();
    QVERIFY(console);
    QCOMPARE(console->transcriptOf, QStringList({QStringLiteral("card:K7Q2")}));

    // The same card again — the file changed under it — is not a hand-over.
    openCard(view, sent, card("K7Q2", "K7Q2 card", "the issue, edited", "h2"));
    QCOMPARE(console->transcriptOf, QStringList({QStringLiteral("card:K7Q2")}));

    // Another card is, and it is the console's own surface that is named, so the pane can bank
    // what is on screen under the card it was printed for and draw that card's back.
    view.closeDetail();
    sent.clear();
    openCard(view, sent, card("M3PD", "M3PD card", "another issue", "h1"));
    QCOMPARE(console->transcriptOf,
             QStringList({QStringLiteral("card:K7Q2"), QStringLiteral("card:M3PD")}));
    QCOMPARE(console->liveSpec().surface, QStringLiteral("card:M3PD"));
}

void BoardModelTests::theCardsRowCarriesVerifyOnlyInAQaLane()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    Consoles consoles(view);
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({row("K7Q2", "needs-qa-llm", "features")}));

    openCard(view, sent, qaCard(qaBlock()));

    QStringList labels;
    for (const relay::agent::Action &action : consoles.card()->actions())
        labels << action.fullLabel();
    QCOMPARE(labels, QStringList({QStringLiteral("Plan (p)"), QStringLiteral("Execute (x)"),
                                  QStringLiteral("Verify (v)"), QStringLiteral("Try it (y)")}));
    QVERIFY(consoles.card()->actions().at(2).leaves);
    QVERIFY(!consoles.card()->actions().at(3).leaves);

    // Back on an ordinary card the action is gone from the list, not merely greyed out.
    QJsonObject plain = card("M3XJ", "M3XJ card", "the issue", "h2");
    view.handleEvent(opened({row("M3XJ", "inbox", "features")}));
    openCard(view, sent, plain);
    labels.clear();
    for (const relay::agent::Action &action : consoles.card()->actions())
        labels << action.fullLabel();
    QCOMPARE(labels, QStringList({QStringLiteral("Plan (p)"), QStringLiteral("Execute (x)")}));
}

// A link in an answer is offered to the context before the console opens it the ordinary way
// (#FEJQ §30.4, and the `option:`/`session:` link kinds step 8 landed). The same resolver serves
// both pages, because a helper's answer is about the app whichever page it was asked from.
void BoardModelTests::anAnswersCardOptionAndSessionLinksResolveThroughTheContext()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    Consoles consoles(view);
    view.onSend = [](const QJsonObject &) {};
    QString section, row_, session;
    view.onOpenOption = [&](const QString &s, const QString &r) { section = s; row_ = r; };
    view.onOpenSession = [&](const QString &id) { session = id; };
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));

    relay::agent::Context *context = consoles.board()->context();
    relay::links::Target option;
    option.valid = true;
    option.kind = relay::links::Kind::Option;
    option.target = relay::links::optionTarget(QStringLiteral("agent"), QStringLiteral("allow_writes"));
    QVERIFY(context->resolveLink(option));
    QCOMPARE(section, QStringLiteral("agent"));
    QCOMPARE(row_, QStringLiteral("allow_writes"));

    relay::links::Target conversation;
    conversation.valid = true;
    conversation.kind = relay::links::Kind::Session;
    conversation.target = relay::links::sessionTarget(QStringLiteral("0f3a-11d2"));
    QVERIFY(context->resolveLink(conversation));
    QCOMPARE(session, QStringLiteral("0f3a-11d2"));

    // A card zooms on this very board.
    relay::links::Target cardLink;
    cardLink.valid = true;
    cardLink.kind = relay::links::Kind::Card;
    cardLink.target = relay::links::cardTarget(QStringLiteral("K7Q2"));
    QVERIFY(context->resolveLink(cardLink));
    QCOMPARE(view.selectedCard(), QStringLiteral("K7Q2"));

    // A path is nobody's here: the console opens it the ordinary way.
    relay::links::Target path;
    path.valid = true;
    path.kind = relay::links::Kind::Path;
    path.target = QStringLiteral("/tmp/workspace/src/Pane.h");
    QVERIFY(!context->resolveLink(path));
}

// With no factory — a test, or `relay-board` linked on its own — the board shows no console and
// everything else on it goes on working. That property is what lets the whole file above drive a
// board without a window.
void BoardModelTests::aBoardWithNoConsoleFactoryStillWorks()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.resize(900, 700);
    view.handleEvent(opened({row("K7Q2", "inbox", "features")}));
    QVERIFY(!view.listConsole());
    QVERIFY(!view.cardConsole());

    // The area is there and holds the board's own widgets; only the agent is missing.
    QWidget *area = view.findChild<QWidget *>(QStringLiteral("boardChatArea"));
    QVERIFY(area && !area->isHidden());
    sent.clear();
    view.requestCheck();
    view.handleEvent(QJsonObject{{"event", "board_problems"},
                                 {"id", sent.last().value("id").toString()},
                                 {"items", QJsonArray{problem("issues/features/a.md", "no id")}},
                                 {"section", QJsonValue()}});
    QVERIFY(!view.findChild<QWidget *>(QStringLiteral("boardChatFindings"))->isHidden());

    // `a`, `k` and the findings' drafts all find nothing to draft into and say nothing about it.
    view.focusHelper();
    QKeyEvent key(QEvent::KeyPress, Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
    QApplication::sendEvent(&view, &key);

    // A card opens, is edited and is handed over with no console either — the note is simply empty.
    openCard(view, sent, card("K7Q2", "K7Q2 card", "the issue", "h1"));
    QVERIFY(view.detailOpen());
    QVERIFY(!replyBox(view));
    QString handed;
    view.onExecuteCard = [&handed](const QString &id, const QString &) {
        handed = id;
        return QStringLiteral("tok");
    };
    view.cardAction(QStringLiteral("execute"));
    view.cardAction(QStringLiteral("execute"));   // the arm-then-go rule: no plan, no acceptance
    QCOMPARE(handed, QStringLiteral("K7Q2"));
}

void BoardModelTests::aSelfClosedCardIsDoneAndStampedByWhoeverImplementedIt()
{
    const auto card = [](const char *status, const char *implemented, const char *verified) {
        QJsonObject json = row("K7Q2", QString::fromUtf8(status), "features");
        json.insert("implemented_by", QString::fromUtf8(implemented));
        json.insert("verified_by", QString::fromUtf8(verified));
        return Card::fromJson(json);
    };
    using relay::board::selfClosed;
    // Done, and both stamps are the one signature: the agent closed its own card.
    QVERIFY(selfClosed(card("done", "anthropic/claude-opus-5", "anthropic/claude-opus-5")));
    // A stray space is not a second model.
    QVERIFY(selfClosed(card("done", "anthropic/claude-opus-5 ", "anthropic/claude-opus-5")));
    // Neither empty field says anything about who closed the card, so neither is a match.
    QVERIFY(!selfClosed(card("done", "", "")));
    QVERIFY(!selfClosed(card("done", "anthropic/claude-opus-5", "")));
    QVERIFY(!selfClosed(card("done", "", "anthropic/claude-opus-5")));
    // Two different models is the cross-provider check the Verified section is for.
    QVERIFY(!selfClosed(card("done", "anthropic/claude-opus-5", "openai/codex")));
    // And an open card is not self-closed however its stamps read.
    QVERIFY(!selfClosed(card("in-progress", "anthropic/claude-opus-5", "anthropic/claude-opus-5")));
    QVERIFY(!selfClosed(card("dropped", "anthropic/claude-opus-5", "anthropic/claude-opus-5")));

    // It falls into Done, not Verified: nobody else checked it, and it wears no tick either.
    Model model;
    model.setConfig(config());
    QJsonObject own = row("K7Q2", "done", "features");
    own.insert("implemented_by", "anthropic/claude-opus-5");
    own.insert("verified_by", "anthropic/claude-opus-5");
    QJsonObject checked = row("M3XJ", "done", "features");
    checked.insert("implemented_by", "anthropic/claude-opus-5");
    checked.insert("verified_by", "openai/codex");
    model.reset(rows({own, checked}));
    QCOMPARE(model.sectionOf(*model.card("K7Q2")), QStringLiteral("done"));
    QCOMPARE(model.sectionOf(*model.card("M3XJ")), QStringLiteral("verified"));
    const auto names = [](const QList<relay::board::Badge> &list) {
        QStringList out;
        for (const relay::board::Badge &badge : list)
            out << badge.text;
        return out;
    };
    QVERIFY(!names(relay::board::badges(*model.card("K7Q2"), false))
                 .contains(QStringLiteral("✓ Claude Opus 5")));
    QVERIFY(names(relay::board::badges(*model.card("M3XJ"), false))
                .contains(QStringLiteral("✓ Codex")));

    QCOMPARE(relay::board::selfClosedTitle(1), QStringLiteral("1 closed by the agent"));
    QCOMPARE(relay::board::selfClosedTitle(12), QStringLiteral("12 closed by the agent"));
}

void BoardModelTests::theSelfClosedCardsOfASectionFoldIntoOneRow()
{
    Model model;
    model.setConfig(config());
    const auto own = [](const char *id, const char *rank) {
        QJsonObject json = row(QString::fromUtf8(id), "done", "features", QString::fromUtf8(rank));
        json.insert("implemented_by", "anthropic/claude-opus-5");
        json.insert("verified_by", "anthropic/claude-opus-5");
        return json;
    };
    // Two ordinary closed cards and three the agent closed itself, all in Done.
    model.reset(rows({row("AAA1", "done", "features", "a"), row("BBB2", "dropped", "features", "b"),
                      own("CCC3", "c"), own("DDD4", "d"), own("EEE5", "e"),
                      row("OPEN1", "ready", "features", "f")}));

    // Folded by default: the two ordinary rows, then one row standing for the three.
    QStringList shown = sketch(model.rows({}));
    QCOMPARE(shown.mid(shown.indexOf("# done 5")),
             (QStringList{"# done 5", "AAA1", "BBB2", "~ done 3 folded"}));
    // The header's count is honest: five cards are in the section, three of them behind the row.
    const QList<Row> list = model.rows({});
    const int header = relay::board::rowOfSection(list, QStringLiteral("done"));
    QCOMPARE(list.at(header).count, 5);
    const int fold = relay::board::rowOfFold(list, QStringLiteral("done"));
    QVERIFY(fold > header);
    QCOMPARE(list.at(fold).count, 3);
    QCOMPARE(list.at(fold).title, QStringLiteral("3 closed by the agent"));
    QCOMPARE(list.at(fold).cardId, QString());
    // Its cards are nowhere else in the list, and the row is not one of them.
    QCOMPARE(relay::board::rowOfCard(list, QStringLiteral("CCC3")), -1);
    QCOMPARE(relay::board::cardsInSection(list, QStringLiteral("done")),
             (QStringList{"AAA1", "BBB2"}));

    // Opened: the same row, and its cards under it as ordinary card rows.
    shown = sketch(model.rows({}, {}, {QStringLiteral("done")}));
    QCOMPARE(shown.mid(shown.indexOf("# done 5")),
             (QStringList{"# done 5", "AAA1", "BBB2", "~ done 3", "CCC3", "DDD4", "EEE5"}));
    QCOMPARE(relay::board::cardsInSection(model.rows({}, {}, {QStringLiteral("done")}),
                                          QStringLiteral("done")),
             (QStringList{"AAA1", "BBB2", "CCC3", "DDD4", "EEE5"}));
    // Another section's key does nothing to this one.
    QVERIFY(sketch(model.rows({}, {}, {QStringLiteral("ready")})).contains("~ done 3 folded"));

    // A section whose every card is self-closed is the header and the fold row, never an empty
    // section.
    Model all;
    all.setConfig(config());
    all.reset(rows({own("CCC3", "c"), own("DDD4", "d")}));
    const QStringList only = sketch(all.rows({}));
    QCOMPARE(only.mid(only.indexOf("# done 2")), (QStringList{"# done 2", "~ done 2 folded"}));

    // A filter shows what it matched: a matching self-closed card is an ordinary row and there is
    // no fold row left to hide it, exactly as a section is not folded while a filter is active.
    model.setFilter(QStringLiteral("CCC3"));
    QCOMPARE(sketch(model.rows({})), (QStringList{"# done 1", "CCC3"}));
    model.setFilter(QStringLiteral("card"));
    shown = sketch(model.rows({}));
    QVERIFY(!shown.contains("~ done 3 folded"));
    QCOMPARE(shown.mid(shown.indexOf("# done 5")),
             (QStringList{"# done 5", "AAA1", "BBB2", "CCC3", "DDD4", "EEE5"}));
    model.setFilter(QString());

    // A folded section says nothing about its fold row: its cards are all put away.
    QCOMPARE(sketch(model.rows({QStringLiteral("done")})).last(), QStringLiteral("# done 5 folded"));
}

// The row in the pane: it toggles on a click, on Enter and on the arrows, it is not a card, and
// which groups are open rides the layout node beside the folded sections (#93WR).
void BoardModelTests::theFoldRowTogglesOnClickEnterAndTheArrowsAndRidesTheLayout()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.setCollapsedSections(QJsonArray{});   // as a restored pane with everything open
    const auto own = [](const char *id, const char *rank) {
        QJsonObject json = row(QString::fromUtf8(id), "done", "features", QString::fromUtf8(rank));
        json.insert("implemented_by", "anthropic/claude-opus-5");
        json.insert("verified_by", "anthropic/claude-opus-5");
        return json;
    };
    view.handleEvent(opened({row("AAA1", "done", "features", "a"), own("CCC3", "c"),
                             own("DDD4", "d"), row("OPEN1", "ready", "features", "e")}));
    QListWidget *list = listOf(view);
    QVERIFY(list);

    // Folded by default: one ordinary row and one fold row, whose words are the row's own.
    const int fold = relay::board::rowOfFold(view.rows(), QStringLiteral("done"));
    QVERIFY(fold > 0);
    QCOMPARE(view.rows().at(fold).title, QStringLiteral("2 closed by the agent"));
    QVERIFY(view.rows().at(fold).collapsed);
    QCOMPARE(relay::board::rowOfCard(view.rows(), QStringLiteral("CCC3")), -1);
    // The section header still counts them: three cards are in Done, two of them behind the row.
    QCOMPARE(view.rows().at(relay::board::rowOfSection(view.rows(), QStringLiteral("done"))).count, 3);
    // It is a row in the list, it carries no card, and it says what it is.
    QCOMPARE(list->count(), view.rows().size());
    QVERIFY(list->item(fold)->flags().testFlag(Qt::ItemIsSelectable));
    QVERIFY(!list->item(fold)->flags().testFlag(Qt::ItemIsDragEnabled));
    QVERIFY(list->item(fold)->toolTip().startsWith(
        QStringLiteral("Cards the agent finished and closed itself, without a verifier.")));
    QVERIFY(list->item(fold)->toolTip().contains(QStringLiteral("Enter or → to show them.")));

    // A click on it shows its cards, and leaves the selection standing on the row itself.
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      list->visualItemRect(list->item(fold)).center());
    QVERIFY(relay::board::rowOfCard(view.rows(), QStringLiteral("CCC3")) >= 0);
    QVERIFY(relay::board::rowOfCard(view.rows(), QStringLiteral("DDD4")) >= 0);
    QCOMPARE(view.selectedFold(), QStringLiteral("done"));
    QVERIFY(view.selectedCard().isEmpty());       // a fold row is not a card
    QCOMPARE(view.rows().at(relay::board::rowOfFold(view.rows(), QStringLiteral("done"))).title,
             QStringLiteral("2 closed by the agent"));
    QVERIFY(!view.rows().at(relay::board::rowOfFold(view.rows(), QStringLiteral("done"))).collapsed);

    // Enter on it puts them away again, and a second click shows them.
    QTest::keyClick(list, Qt::Key_Return);
    QCOMPARE(relay::board::rowOfCard(view.rows(), QStringLiteral("CCC3")), -1);
    QCOMPARE(view.selectedFold(), QStringLiteral("done"));
    QTest::keyClick(list, Qt::Key_Return);
    QVERIFY(relay::board::rowOfCard(view.rows(), QStringLiteral("CCC3")) >= 0);

    // → from the row steps into its first card; ← from that card comes back to the row and puts
    // the cards away, and ← again folds the whole section.
    QTest::keyClick(list, Qt::Key_Right);
    QCOMPARE(view.selectedCard(), QStringLiteral("CCC3"));
    QTest::keyClick(list, Qt::Key_Left);
    QCOMPARE(view.selectedFold(), QStringLiteral("done"));
    QCOMPARE(relay::board::rowOfCard(view.rows(), QStringLiteral("CCC3")), -1);
    QTest::keyClick(list, Qt::Key_Left);
    QVERIFY(view.rows().at(relay::board::rowOfSection(view.rows(), QStringLiteral("done"))).collapsed);
    // ← on a card of an ordinary section still folds that section, as it always did.
    view.setCollapsedSections(QJsonArray{});
    view.selectCard(QStringLiteral("OPEN1"));
    QTest::keyClick(list, Qt::Key_Left);
    QVERIFY(view.rows().at(relay::board::rowOfSection(view.rows(), QStringLiteral("ready"))).collapsed);

    // → on the folded row shows its cards and stands on the first of them.
    view.setCollapsedSections(QJsonArray{});
    view.setOpenSelfClosed(QJsonArray{});
    view.selectCard(QStringLiteral("AAA1"));
    QTest::keyClick(list, Qt::Key_Down);
    QCOMPARE(view.selectedFold(), QStringLiteral("done"));
    QTest::keyClick(list, Qt::Key_Right);
    QCOMPARE(view.selectedCard(), QStringLiteral("CCC3"));

    // Which groups are open goes into the layout node and comes back from it, beside the folded
    // sections and in the same shape.
    QStringList open;
    for (const QJsonValue &value : view.openSelfClosed())
        open << value.toString();
    QCOMPARE(open, (QStringList{"done"}));
    view.setOpenSelfClosed(QJsonArray{});
    QVERIFY(view.openSelfClosed().isEmpty());
    QVERIFY(view.rows().at(relay::board::rowOfFold(view.rows(), QStringLiteral("done"))).collapsed);
    view.setOpenSelfClosed(QJsonArray{QStringLiteral("done")});
    QVERIFY(!view.rows().at(relay::board::rowOfFold(view.rows(), QStringLiteral("done"))).collapsed);
    QVERIFY(relay::board::rowOfCard(view.rows(), QStringLiteral("DDD4")) >= 0);
    // A new pane starts folded: the layout node is empty until a group is opened.
    relay::BoardView fresh(QStringLiteral("/tmp/workspace"));
    QVERIFY(fresh.openSelfClosed().isEmpty());

    // A filter shows a matching self-closed card as an ordinary row, with no fold row over it.
    view.setOpenSelfClosed(QJsonArray{});
    QLineEdit *filter = view.findChild<QLineEdit *>(QStringLiteral("boardFilter"));
    QVERIFY(filter);
    filter->setText(QStringLiteral("DDD4"));
    QCOMPARE(relay::board::rowOfFold(view.rows(), QStringLiteral("done")), -1);
    QVERIFY(relay::board::rowOfCard(view.rows(), QStringLiteral("DDD4")) >= 0);
    filter->clear();
    QVERIFY(relay::board::rowOfFold(view.rows(), QStringLiteral("done")) >= 0);
}

// A card reached by id — a `#ID` in a card's text, a link from a thread, the cleanup panel's
// anchors — opens whatever is holding it, so "reveal" really reveals it (#93WR).
void BoardModelTests::aSelfClosedCardReachedByIdUnfoldsItsGroup()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QJsonObject own = row("CCC3", "done", "features", "c");
    own.insert("implemented_by", "anthropic/claude-opus-5");
    own.insert("verified_by", "anthropic/claude-opus-5");
    // A brand-new pane: every section folded, and the group inside Done folded too.
    view.handleEvent(opened({own, row("AAA1", "done", "features", "a")}));
    QVERIFY(view.rows().at(relay::board::rowOfSection(view.rows(), QStringLiteral("done"))).collapsed);
    QCOMPARE(relay::board::rowOfCard(view.rows(), QStringLiteral("CCC3")), -1);

    view.selectCard(QStringLiteral("CCC3"));
    QVERIFY(relay::board::rowOfCard(view.rows(), QStringLiteral("CCC3")) >= 0);
    QCOMPARE(view.selectedCard(), QStringLiteral("CCC3"));
    QVERIFY(view.selectedFold().isEmpty());
    QVERIFY(!view.rows().at(relay::board::rowOfFold(view.rows(), QStringLiteral("done"))).collapsed);
    QStringList open;
    for (const QJsonValue &value : view.openSelfClosed())
        open << value.toString();
    QCOMPARE(open, (QStringList{"done"}));

    // An ordinary card asked for by id leaves the folds exactly as they were: only a card the
    // group is holding needs the group opened.
    relay::BoardView other(QStringLiteral("/tmp/workspace"));
    other.handleEvent(opened({own, row("AAA1", "done", "features", "a")}));
    other.selectCard(QStringLiteral("AAA1"));
    QVERIFY(other.openSelfClosed().isEmpty());
    QVERIFY(other.rows().at(relay::board::rowOfSection(other.rows(),
                                                       QStringLiteral("done"))).collapsed);
}

QTEST_MAIN(BoardModelTests)
#include "boardmodel_test.moc"
