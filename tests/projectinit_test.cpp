// SPDX-License-Identifier: GPL-3.0-or-later
// "Initialize a project and create a Switchboard here?" (src/ProjectInit.h, protocol 19.12/19.13).
//
// The owner's rule of 2026-09-18 is that `<project>/switchboard/` appears only after one yes, and
// that only five acts may ask. Both halves are rules, not pixels, so both are tested here with no
// window and no worker: `decide()` is the whole table, and `questionFrom()` turns one
// `project_probe_result` into the lines the pane draws.
//
// The wiring that cannot be linked — Pane and RelayWindow are one translation unit that needs a
// whole application — is read as text, exactly as tests/boardworkspace_test.cpp reads
// RelayWindow::boardWorkspace() and tests/settingspane_test.cpp reads settingsSections().
#include "ProjectInit.h"
#include "Projects.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace relay::projectinit;

namespace {

const QString kProject = QStringLiteral("/home/e/src/widgetworks");

Situation inProject(const QString &project = kProject)
{
    Situation situation;
    situation.project = project;
    situation.cwd = project + QStringLiteral("/backend");
    return situation;
}

QString fileText(const QString &relative)
{
    QFile source(QStringLiteral(RELAY_SOURCE_DIR "/") + relative);
    if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(source.readAll());
}

// The body of one function, read as text: from its signature to the first line that closes at the
// same indent. Good enough to assert what a function does and does not name.
QString bodyOf(const QString &text, const QString &signature, const QString &closer)
{
    const int start = text.indexOf(signature);
    if (start < 0) return {};
    const int end = text.indexOf(closer, start);
    return end > start ? text.mid(start, end - start) : QString();
}

// docs/PROJECT-INIT-AND-IMPORT.md section 3, verbatim enough to be the shape the GUI must read.
QJsonObject probeFixture()
{
    static const char kJson[] =
        "{\"version\": 1, \"project\": \"/home/e/src/widgetworks\", \"git\": {\"is_repo\": true, \"remotes\": [{\"name\": \"origin\","
        " \"url\": \"git@github.com:me/widgetworks.git\", \"host\": \"github.com\", \"owner\": \"me\","
        " \"repo\": \"widgetworks\", \"forge\": \"github\"}, {\"name\": \"upstream\", \"url\": \"https://github.com/widgetworks/widgetworks.git\","
        " \"host\": \"github.com\", \"owner\": \"widgetworks\", \"repo\": \"widgetworks\", \"forge\": \"github\"}],"
        " \"primary\": \"upstream\", \"primary_reason\": \"upstream over origin: origin looks like a fork,"
        " and issues are filed on the repository it was forked from\", \"branch\": \"feature/ENG-1204-dark-mode\","
        " \"ticket_keys\": [{\"key\": \"ENG-1204\", \"pattern\": \"x\", \"looks_like\": \"jira-or-linear\"}]},"
        " \"board\": {\"present\": false, \"kind\": \"none\", \"folder\": null, \"path\": null, \"cards\": 0,"
        " \"convertible\": 0}, \"trackers\": [{\"kind\": \"backlog-md\", \"path\": \"backlog\", \"count\": 23,"
        " \"summary\": \"Backlog.md in backlog/: 23 task(s), 20 not done; 3 in completed/ and archive/ are left alone\"},"
        " {\"kind\": \"checklist\", \"path\": \"TODO.md\", \"count\": 7, \"summary\": \"TODO.md: 6 unchecked item(s),"
        " 1 headed section(s)\"}], \"hints\": [{\"kind\": \"github-issue-templates\", \"path\": \".github/ISSUE_TEMPLATE\","
        " \"count\": 3, \"detail\": \"this project files issues on GitHub with 3 issue template(s)\"},"
        " {\"kind\": \"ticket-key-in-branch\", \"path\": \".git/HEAD\", \"count\": 1, \"key\": \"ENG-1204\","
        " \"detail\": \"the branch name carries ENG-1204, which looks like a Jira or Linear key; Relay does not contact either\"}],"
        " \"counts\": {\"trackers\": 2, \"items\": 30}, \"gh_hosts\": [\"github.com\"]}";
    return QJsonDocument::fromJson(QByteArray(kJson)).object();
}

}  // namespace

class ProjectInitTests : public QObject {
    Q_OBJECT

private slots:
    // ----- the triggers -------------------------------------------------------------------------
    void everyTriggerIsAnAttachReason();
    void triggerNamesRoundTrip();

    // ----- decide() -----------------------------------------------------------------------------
    void aProjectWithNoBoardIsAskedAbout();
    void nothingOutsideAProjectIsEverAsked();
    void aProjectWithABoardIsAttachedNotAsked();
    void aNoIsForEver();
    void notNowStopsTheAgentPromptAndNothingElse();
    void initCommandOverrulesANoAndOffersTheCurrentDirectory();
    void aGuestIsNeverAskedAndNeitherIsASecondQuestion();

    // ----- the question -------------------------------------------------------------------------
    void theQuestionNamesTheFolderItWouldCreate();
    void theProbeBecomesCheckboxesAndNotes();
    void anEmptyProbeIsAQuestionWithNoFindings();
    void aPreBoardIssuesTreeIsANoteAndNotAnImport();
    void tickedBoxesBecomeImportKinds();
    void theOneQuietLineAfterAYes();

    // ----- the wiring that cannot be linked ------------------------------------------------------
    void thePaneRaisesEveryTriggerAndBlocksNone();
    void theWindowOffersItPassivelyAndRemembersTheAnswer();
    void aGuestsPaneNeverDrawsTheQuestion();
    void theseEventsStayOnTheDesktop();
};

// ----- the triggers ---------------------------------------------------------------------------

// Every trigger's name is a reason the registry accepts, so a yes can always be written down as
// what caused it. `agent-work` was added to that closed set for trigger (1).
void ProjectInitTests::everyTriggerIsAnAttachReason()
{
    QCOMPARE(triggerNames().size(), 5);
    for (const QString &name : triggerNames()) QVERIFY2(relay::projects::isReason(name), qPrintable(name));
    QCOMPARE(triggerName(Trigger::AgentWork), QStringLiteral("agent-work"));
    QCOMPARE(triggerName(Trigger::AgentCard), QStringLiteral("agent-card"));
    QCOMPARE(triggerName(Trigger::Switchboard), QStringLiteral("switchboard"));
    QCOMPARE(triggerName(Trigger::CardCommand), QStringLiteral("card-command"));
    QCOMPARE(triggerName(Trigger::InitCommand), QStringLiteral("init-command"));
}

void ProjectInitTests::triggerNamesRoundTrip()
{
    for (Trigger trigger : {Trigger::AgentWork, Trigger::AgentCard, Trigger::Switchboard,
                            Trigger::CardCommand, Trigger::InitCommand}) {
        Trigger back = Trigger::AgentCard;
        QVERIFY(parseTrigger(triggerName(trigger), &back));
        QCOMPARE(int(back), int(trigger));
    }
    Trigger untouched = Trigger::InitCommand;
    QVERIFY(!parseTrigger(QStringLiteral("cd"), &untouched));
    QVERIFY(!parseTrigger(QString(), &untouched));
    QCOMPARE(int(untouched), int(Trigger::InitCommand));
}

// ----- decide() -------------------------------------------------------------------------------

void ProjectInitTests::aProjectWithNoBoardIsAskedAbout()
{
    for (Trigger trigger : {Trigger::AgentWork, Trigger::AgentCard, Trigger::Switchboard,
                            Trigger::CardCommand, Trigger::InitCommand}) {
        const Decision decision = decide(trigger, inProject());
        QVERIFY2(decision.asks(), qPrintable(triggerName(trigger)));
        QCOMPARE(decision.project, kProject);
        QCOMPARE(decision.reason, triggerName(trigger));
    }
}

// ~/Downloads: no project, so nothing is offered and nothing is said. The owner's rule — no
// switchboard folders all over the machine — starts here.
void ProjectInitTests::nothingOutsideAProjectIsEverAsked()
{
    Situation nowhere;
    nowhere.cwd = QStringLiteral("/home/e/Downloads");
    for (Trigger trigger : {Trigger::AgentWork, Trigger::AgentCard, Trigger::Switchboard,
                            Trigger::CardCommand}) {
        const Decision decision = decide(trigger, nowhere);
        QCOMPARE(int(decision.outcome), int(Outcome::Nothing));
        QVERIFY(decision.message.isEmpty());
    }
}

void ProjectInitTests::aProjectWithABoardIsAttachedNotAsked()
{
    Situation ready = inProject();
    ready.hasBoard = true;
    for (Trigger trigger : {Trigger::AgentWork, Trigger::Switchboard, Trigger::CardCommand}) {
        const Decision decision = decide(trigger, ready);
        QCOMPARE(int(decision.outcome), int(Outcome::Attach));
        QCOMPARE(decision.reason, triggerName(trigger));
        QVERIFY(decision.message.isEmpty());   // an attach is silent; only /init says so
    }
    const Decision typed = decide(Trigger::InitCommand, ready);
    QCOMPARE(int(typed.outcome), int(Outcome::Attach));
    QCOMPARE(typed.message, QStringLiteral("widgetworks already has a Switchboard."));
}

// A no is written to the registry and outlives the process: no trigger but `/init` asks again, in
// any tab, after any restart.
void ProjectInitTests::aNoIsForEver()
{
    Situation no = inProject();
    no.declined = true;
    for (Trigger trigger : {Trigger::AgentWork, Trigger::AgentCard, Trigger::Switchboard,
                            Trigger::CardCommand}) {
        const Decision decision = decide(trigger, no);
        QCOMPARE(int(decision.outcome), int(Outcome::Nothing));
    }
    QVERIFY(decide(Trigger::InitCommand, no).asks());
}

void ProjectInitTests::notNowStopsTheAgentPromptAndNothingElse()
{
    Situation snoozed = inProject();
    snoozed.snoozed = true;
    QCOMPARE(int(decide(Trigger::AgentWork, snoozed).outcome), int(Outcome::Nothing));
    // Every explicit act asks again: "not now" is not a no.
    for (Trigger trigger : {Trigger::AgentCard, Trigger::Switchboard, Trigger::CardCommand,
                            Trigger::InitCommand})
        QVERIFY2(decide(trigger, snoozed).asks(), qPrintable(triggerName(trigger)));
}

void ProjectInitTests::initCommandOverrulesANoAndOffersTheCurrentDirectory()
{
    Situation no = inProject();
    no.declined = true;
    const Decision again = decide(Trigger::InitCommand, no);
    QVERIFY(again.asks());
    QVERIFY(again.clearsDecline);
    QCOMPARE(again.reason, QStringLiteral("init-command"));

    // No candidate at all: `/init` offers the pane's own directory as the project. Nothing else
    // does — that is the whole difference between the command and the other four triggers.
    Situation loose;
    loose.cwd = QStringLiteral("/home/e/notes");
    const Decision offered = decide(Trigger::InitCommand, loose);
    QVERIFY(offered.asks());
    QCOMPARE(offered.project, QStringLiteral("/home/e/notes"));
    // …and a declined *candidate* says nothing about the directory being offered instead.
    loose.declined = true;
    QVERIFY(decide(Trigger::InitCommand, loose).asks());

    Situation nothingAtAll;
    const Decision nowhere = decide(Trigger::InitCommand, nothingAtAll);
    QCOMPARE(int(nowhere.outcome), int(Outcome::Say));
    QVERIFY(!nowhere.message.isEmpty());
}

void ProjectInitTests::aGuestIsNeverAskedAndNeitherIsASecondQuestion()
{
    Situation guest = inProject();
    guest.remote = true;
    for (Trigger trigger : {Trigger::AgentWork, Trigger::AgentCard, Trigger::Switchboard,
                            Trigger::CardCommand, Trigger::InitCommand})
        QCOMPARE(int(decide(trigger, guest).outcome), int(Outcome::Nothing));

    Situation busy = inProject();
    busy.asking = true;
    for (Trigger trigger : {Trigger::AgentWork, Trigger::Switchboard, Trigger::InitCommand})
        QCOMPARE(int(decide(trigger, busy).outcome), int(Outcome::Nothing));
}

// ----- the question ---------------------------------------------------------------------------

void ProjectInitTests::theQuestionNamesTheFolderItWouldCreate()
{
    QCOMPARE(titleLine(), QStringLiteral("Initialize a project and create a Switchboard here?"));
    QCOMPARE(boardFolderFor(kProject), kProject + QStringLiteral("/switchboard"));
    QCOMPARE(boardFolderFor(kProject + QStringLiteral("/")), kProject + QStringLiteral("/switchboard"));
    QCOMPARE(boardFolderFor(QString()), QString());
    // One folder, named, and the sentence says nothing else is written.
    const QString line = folderLineFor(kProject);
    QVERIFY(line.contains(kProject + QStringLiteral("/switchboard/")));
    QVERIFY(line.contains(QStringLiteral("nothing else")));
    QVERIFY(!line.contains(QStringLiteral("issues")));
}

void ProjectInitTests::theProbeBecomesCheckboxesAndNotes()
{
    const Question question = questionFrom(probeFixture());
    QVERIFY(question.isValid());
    QCOMPARE(question.project, kProject);
    QCOMPARE(question.folder, kProject + QStringLiteral("/switchboard"));
    QCOMPARE(question.items, 30);

    // One checkbox per tracker, in the probe's order, labelled with the probe's own summary.
    QCOMPARE(question.imports.size(), 2);
    QCOMPARE(question.imports.at(0).kind, QStringLiteral("backlog-md"));
    QCOMPARE(question.imports.at(0).count, 23);
    QVERIFY(question.imports.at(0).importable);
    QVERIFY(question.imports.at(0).text.contains(QStringLiteral("23 task(s)")));
    QCOMPARE(question.imports.at(1).kind, QStringLiteral("checklist"));
    QVERIFY(question.imports.at(1).text.contains(QStringLiteral("TODO.md")));

    // The primary remote is `upstream`, not `origin`: the issues live on the repository the fork
    // came from, and the probe's sentence saying so is shown rather than a code.
    QStringList notes;
    for (const Finding &note : question.notes) notes << note.text;
    QVERIFY2(notes.contains(QStringLiteral("On GitHub as widgetworks/widgetworks")), qPrintable(notes.join('|')));
    QVERIFY(!notes.contains(QStringLiteral("On GitHub as me/widgetworks")));
    QVERIFY(notes.join(QLatin1Char('|')).contains(QStringLiteral("origin looks like a fork")));
    QVERIFY(notes.join(QLatin1Char('|')).contains(QStringLiteral("3 issue template(s)")));
    QVERIFY(notes.join(QLatin1Char('|')).contains(QStringLiteral("ENG-1204")));
    // Nothing in the notes is importable: they are what Relay found and will not touch.
    for (const Finding &note : question.notes) QVERIFY(!note.importable);
}

// A probe that could not read the project — or never answered — must not stop the user
// initializing it: the question is the same one, with nothing found.
void ProjectInitTests::anEmptyProbeIsAQuestionWithNoFindings()
{
    const Question question = questionFrom(QJsonObject{{QStringLiteral("project"), kProject}});
    QVERIFY(question.isValid());
    QVERIFY(question.imports.isEmpty());
    QVERIFY(question.notes.isEmpty());
    QCOMPARE(question.title, titleLine());
    QCOMPARE(question.folder, kProject + QStringLiteral("/switchboard"));
    QVERIFY(!questionFrom(QJsonObject{}).isValid());
}

void ProjectInitTests::aPreBoardIssuesTreeIsANoteAndNotAnImport()
{
    QJsonObject probe = probeFixture();
    probe.insert(QStringLiteral("board"),
                 QJsonObject{{QStringLiteral("present"), false},
                             {QStringLiteral("kind"), QStringLiteral("pre-board")},
                             {QStringLiteral("path"), QStringLiteral("issues")},
                             {QStringLiteral("cards"), 41},
                             {QStringLiteral("convertible"), 38}});
    const Question question = questionFrom(probe);
    QCOMPARE(question.imports.size(), 2);   // still only the two trackers
    const Finding &note = question.notes.first();
    QCOMPARE(note.kind, QStringLiteral("pre-board"));
    QVERIFY(!note.importable);
    QVERIFY(note.text.contains(QStringLiteral("issues")));
    QVERIFY(note.text.contains(QStringLiteral("41")));
    QVERIFY(note.text.contains(QStringLiteral("left exactly as it is")));
}

void ProjectInitTests::tickedBoxesBecomeImportKinds()
{
    const Question question = questionFrom(probeFixture());
    QCOMPARE(importKinds(question.imports, {false, false}), QStringList{});
    QCOMPARE(importKinds(question.imports, {true, false}), QStringList{QStringLiteral("backlog-md")});
    QCOMPARE(importKinds(question.imports, {false, true}), QStringList{QStringLiteral("checklist")});
    QCOMPARE(importKinds(question.imports, {true, true}),
             (QStringList{QStringLiteral("backlog-md"), QStringLiteral("checklist")}));
    // A short or missing tick list is "nothing ticked", never "everything ticked": a box the user
    // never saw must not import.
    QCOMPARE(importKinds(question.imports, {}), QStringList{});
    QCOMPARE(importKinds(question.imports, {true}), QStringList{QStringLiteral("backlog-md")});
    // The same kind twice asks for it once, and a note can never be ticked into an import.
    QList<Finding> twice = question.imports;
    twice.append(question.imports.at(0));
    twice.append(question.notes.at(0));
    QCOMPARE(importKinds(twice, {true, false, true, true}), QStringList{QStringLiteral("backlog-md")});
}

void ProjectInitTests::theOneQuietLineAfterAYes()
{
    QCOMPARE(createdLine(kProject, -1),
             QStringLiteral("Switchboard created in /home/e/src/widgetworks/switchboard/"));
    QVERIFY(createdLine(kProject, 23).endsWith(QStringLiteral("switchboard/ · 23 card(s) imported")));
    QVERIFY(createdLine(kProject, 0).endsWith(QStringLiteral("nothing left to import")));
    QCOMPARE(createdLine(QString(), 3), QString());
    // A no says what was remembered and how to undo it; "not now" says only how to come back.
    QVERIFY(declinedLine(kProject).contains(QStringLiteral("not ask again")));
    QVERIFY(declinedLine(kProject).contains(QStringLiteral("/init")));
    QVERIFY(notNowLine(kProject).contains(QStringLiteral("/init")));
    QVERIFY(!notNowLine(kProject).contains(QStringLiteral("not ask again")));
}

// ----- the wiring that cannot be linked --------------------------------------------------------

// Pane and RelayWindow are one translation unit needing a whole QApplication, a shell and a
// worker, so what must never come back is pinned as text.
void ProjectInitTests::thePaneRaisesEveryTriggerAndBlocksNone()
{
    const QString pane = fileText(QStringLiteral("src/Pane.h"));
    QVERIFY(!pane.isEmpty());

    // Trigger (1): the prompt goes to the agent and the question is raised beside it, on the next
    // turn of the event loop — never before the work, and never from a guest's line.
    const QString submit = bodyOf(pane, QStringLiteral("void submitAgent("), QStringLiteral("\n    }"));
    QVERIFY(!submit.isEmpty());
    QVERIFY(submit.contains(QStringLiteral("Trigger::AgentWork")));
    QVERIFY(submit.contains(QStringLiteral("QTimer::singleShot(0")));
    QVERIFY(submit.contains(QStringLiteral("!m_remoteSubmit")));

    // Trigger (2): the worker's `board_init_request` is answered — always, because its turn thread
    // is parked on it — and triggers (4) and (5) are where the owner put them.
    QVERIFY(pane.contains(QStringLiteral("void handleBoardInitRequest(")));
    QVERIFY(pane.contains(QStringLiteral("QStringLiteral(\"board_init_request\")")));
    QVERIFY(pane.contains(QStringLiteral("QStringLiteral(\"board_init_answer\")")));
    QVERIFY(pane.contains(QStringLiteral("Trigger::CardCommand, args.trimmed()")));
    QVERIFY(pane.contains(QStringLiteral("Trigger::InitCommand")));
    QVERIFY(pane.contains(QStringLiteral("{QStringLiteral(\"init\"), QString(),")));

    // The probe comes first and nothing is created before the answer: `board_init` is only ever
    // sent from the step that runs after a yes.
    const QString begin = bodyOf(pane, QStringLiteral("void beginProjectInit("), QStringLiteral("\n    }"));
    QVERIFY(begin.contains(QStringLiteral("project_probe")));
    QVERIFY(!begin.contains(QStringLiteral("\"board_init\"")));
    const QString step = bodyOf(pane, QStringLiteral("void projectInitBoardState("), QStringLiteral("\n    }"));
    QVERIFY(step.contains(QStringLiteral("board_init")));
    QVERIFY(step.contains(QStringLiteral("board_import_propose")));

    // The import is propose-then-apply: only keys go back on the wire (19.13).
    const QString proposals = bodyOf(pane, QStringLiteral("void projectInitProposals("), QStringLiteral("\n    }"));
    QVERIFY(proposals.contains(QStringLiteral("source_key")));
    QVERIFY(proposals.contains(QStringLiteral("board_import_apply")));
}

void ProjectInitTests::theWindowOffersItPassivelyAndRemembersTheAnswer()
{
    const QString window = fileText(QStringLiteral("src/RelayWindow.h"));
    QVERIFY(!window.isEmpty());

    // Trigger (3), and the one passive entry point the owner allowed.
    QVERIFY(window.contains(QStringLiteral("askProjectInit(relay::projectinit::Trigger::Switchboard)")));
    QVERIFY(window.contains(QStringLiteral("Initialize a project here…")));
    QVERIFY(window.contains(QStringLiteral("Next time: type /init in any prompt box")));

    // The window answers facts and remembers answers; the rules are the library's.
    QVERIFY(window.contains(QStringLiteral("pane->onProjectInitSituation")));
    QVERIFY(window.contains(QStringLiteral("pane->onProjectAttach")));
    QVERIFY(window.contains(QStringLiteral("pane->onProjectInitEvent")));
    QVERIFY(window.contains(QStringLiteral("projects().decline(project)")));
    QVERIFY(window.contains(QStringLiteral("projects().undecline(project)")));
    QVERIFY(window.contains(QStringLiteral("initSnoozed()")));

    // A `cd` never asks: the only thing that reads the pane's directory for this is the situation
    // the pane asks for, and the cwd handler is not one of the five triggers.
    QVERIFY(!window.contains(QStringLiteral("askProjectInit(relay::projectinit::Trigger::AgentWork")));
}

// A guest's pane is relay::RemotePane, which has no worker of its own: it neither draws this
// question nor could answer it. Nothing in the remote surfaces may name it.
void ProjectInitTests::aGuestsPaneNeverDrawsTheQuestion()
{
    for (const QString &file : {QStringLiteral("src/RemotePane.h"), QStringLiteral("src/RemotePane.cpp")}) {
        const QString text = fileText(file);
        QVERIFY2(!text.isEmpty(), qPrintable(file));
        QVERIFY2(!text.contains(QStringLiteral("ProjectInitBlock")), qPrintable(file));
        QVERIFY2(!text.contains(QStringLiteral("askProjectInit")), qPrintable(file));
        QVERIFY2(!text.contains(QStringLiteral("board_init")), qPrintable(file));
    }
}

// The question's events carry local file paths and answer a dialog only the desktop can show, so
// the sidecar keeps every one of them off the wire (remote/wire.py), and the three messages that
// would answer it are not client-sendable at all.
void ProjectInitTests::theseEventsStayOnTheDesktop()
{
    const QString wire = fileText(QStringLiteral("remote/wire.py"));
    QVERIFY(!wire.isEmpty());
    for (const QString &event : {QStringLiteral("board_init_request"), QStringLiteral("board_created"),
                                 QStringLiteral("board_state"), QStringLiteral("project_probe_result"),
                                 QStringLiteral("board_import_proposals"), QStringLiteral("board_imported")})
        QVERIFY2(wire.contains(QStringLiteral("\"%1\": \"desktop-local").arg(event)), qPrintable(event));
    const int clients = wire.indexOf(QStringLiteral("CLIENT_TYPES"));
    QVERIFY(clients > 0);
    for (const QString &message : {QStringLiteral("set_board"), QStringLiteral("board_init"),
                                   QStringLiteral("board_init_answer"), QStringLiteral("project_probe")})
        QVERIFY2(!wire.contains(QStringLiteral("\"%1\":").arg(message)), qPrintable(message));
}

QTEST_MAIN(ProjectInitTests)
#include "projectinit_test.moc"
