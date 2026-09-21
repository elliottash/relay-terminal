// SPDX-License-Identifier: AGPL-3.0-or-later
// Which project's Switchboard a pane is looking at (src/BoardWorkspace.h). The Switchboard is
// per project, so the rule has to answer from the candidate directories it is handed and from
// nothing else — in particular never from the directory Relay itself was started in, which is
// what made one project's board appear in every pane of every window (owner report, 2026-09-18).
#include "BoardWorkspace.h"
#include "Projects.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// A project root: `<dir>/<folder>/board.yaml`, which is the marker the rule looks for. `folder`
// is `switchboard` on a board made from 2026-09-18 on and `issues` on one filed before that;
// both are found, and neither is ever created by Relay behind the user's back.
void makeBoard(const QString &root, const QString &folder = QStringLiteral("issues"))
{
    QVERIFY(QDir().mkpath(root + '/' + folder));
    QFile marker(root + '/' + folder + QStringLiteral("/board.yaml"));
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.write("columns: [inbox]\n");
}

// The body of a member function of RelayWindow.h, read as text: the window needs a whole Qt
// application and a worker to run, so the rules that must never come back are pinned here the way
// settingspane_test.cpp reads settingsSections().
QString windowSource()
{
    QFile source(QStringLiteral(RELAY_SOURCE_DIR "/src/RelayWindow.h"));
    if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(source.readAll());
}

QString bodyOf(const QString &text, const QString &signature)
{
    const int start = text.indexOf(signature);
    if (start < 0) return {};
    const int end = text.indexOf(QStringLiteral("\n    }"), start);
    return end > start ? text.mid(start, end - start) : QString();
}

}  // namespace

class BoardWorkspaceTests : public QObject {
    Q_OBJECT

private slots:
    void theNearestAncestorWithABoardWins();
    void theFirstCandidateWithABoardWins();
    void noBoardAnywhereIsEmpty();
    void theProcessesOwnDirectoryIsNeverConsulted();
    void eitherFolderNameIsABoardAndTheNearestOneWins();
    void theWindowsRuleAsksThePaneAndNothingElse();
    void attachTabIsTheOnlyWriterOfTheTabsProject();
    void theConfigureFunnelAlwaysSendsABoardBlock();
    void theHelpersConfigureCarriesTheKeybindings();
    void theHelperOutlivesItsPanelsAndOnlyTheTabEndsIt();
    void aHelperThatDiesMidTurnPutsItsPanelsBack();
    void anEmbeddedConsoleIsNotOneOfTheWindowsPanes();
    void theWindowMakesConsolesAndWiresThemAsPanesExceptWhereItMustNot();
    void theConsolesOfATabShareOneWorkerAndOneConversation();
    void theWindowsWrapperForwardsEveryContextVirtual();
    void theHelperPanelAndItsModelBoxAreGoneFromTheTree();
    void aConsoleIsNotALeafSoTheActivePaneIsNeverOne();
    void anOptionOrSessionLinkOpensWhereItNames();
    void theBoardPanePaintsFromTheBoardMaterials();
    void tabMetersGiveWayOnlyWhenFullLabelsDoNotFit();
};

// A pane deep inside a project finds the project's board, and the answer is the project root
// rather than the directory the pane is standing in.
void BoardWorkspaceTests::theNearestAncestorWithABoardWins()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString project = tmp.path() + QStringLiteral("/project");
    makeBoard(project);
    QVERIFY(QDir().mkpath(project + QStringLiteral("/src/deep/deeper")));

    QCOMPARE(relay::boardRootFor({project}), project);
    QCOMPARE(relay::boardRootFor({project + QStringLiteral("/src/deep/deeper")}), project);

    // A nested project of its own is the nearer ancestor, so it wins over the outer one.
    const QString nested = project + QStringLiteral("/vendor/inner");
    makeBoard(nested);
    QVERIFY(QDir().mkpath(nested + QStringLiteral("/src")));
    QCOMPARE(relay::boardRootFor({nested + QStringLiteral("/src")}), nested);
}

// Candidates are tried in order: the pane's terminal directory first, the workspace it was made
// in second. A pane that has `cd`-ed into another checkout is working in *that* project.
void BoardWorkspaceTests::theFirstCandidateWithABoardWins()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString first = tmp.path() + QStringLiteral("/first");
    const QString second = tmp.path() + QStringLiteral("/second");
    makeBoard(first);
    makeBoard(second);
    QCOMPARE(relay::boardRootFor({first, second}), first);
    QCOMPARE(relay::boardRootFor({second, first}), second);

    // Empty candidates are skipped, and a candidate with no board falls through to the next.
    const QString bare = tmp.path() + QStringLiteral("/bare");
    QVERIFY(QDir().mkpath(bare));
    QCOMPARE(relay::boardRootFor({QString(), bare, second}), second);
}

void BoardWorkspaceTests::noBoardAnywhereIsEmpty()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString bare = tmp.path() + QStringLiteral("/bare/deeper");
    QVERIFY(QDir().mkpath(bare));
    QVERIFY(relay::boardRootFor({bare}).isEmpty());
    QVERIFY(relay::boardRootFor({}).isEmpty());
    QVERIFY(relay::boardRootFor({QString()}).isEmpty());
    // A directory that does not exist is not a board root either.
    QVERIFY(relay::boardRootFor({tmp.path() + QStringLiteral("/gone")}).isEmpty());
}

// The one that matters: Relay started from a project used to find that project's board from every
// pane, because the rule ended in QDir::currentPath(). A board sitting in the process's current
// directory must be invisible to a pane that is not in it.
void BoardWorkspaceTests::theProcessesOwnDirectoryIsNeverConsulted()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString launched = tmp.path() + QStringLiteral("/launch-project");
    makeBoard(launched);
    const QString elsewhere = tmp.path() + QStringLiteral("/elsewhere/work");
    QVERIFY(QDir().mkpath(elsewhere));

    const QString before = QDir::currentPath();
    QVERIFY(QDir::setCurrent(launched));
    const QString found = relay::boardRootFor({elsewhere});
    const QString relative = relay::boardRootFor({QStringLiteral(".")});
    QVERIFY(QDir::setCurrent(before));

    QVERIFY2(found.isEmpty(), qPrintable(found));
    // Nor by the back door: a relative candidate would mean the same thing.
    QVERIFY2(relative.isEmpty(), qPrintable(relative));
}

// The two spellings of the board folder, and the order they are tried in (protocol 19.1). The
// nearest ancestor wins whatever its folder is called — a `switchboard/` project nested inside an
// `issues/` one is its own board and vice versa — and a directory holding both is its
// `switchboard/`, which is the folder Relay creates today.
void BoardWorkspaceTests::eitherFolderNameIsABoardAndTheNearestOneWins()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    // A project whose board is the new `switchboard/`.
    const QString fresh = tmp.path() + QStringLiteral("/fresh");
    makeBoard(fresh, QStringLiteral("switchboard"));
    QVERIFY(QDir().mkpath(fresh + QStringLiteral("/src/deep")));
    QCOMPARE(relay::boardRootFor({fresh + QStringLiteral("/src/deep")}), fresh);

    // An old `issues/` board nested inside it: the nearer one wins, older name and all.
    const QString old = fresh + QStringLiteral("/vendor/legacy");
    makeBoard(old, QStringLiteral("issues"));
    QVERIFY(QDir().mkpath(old + QStringLiteral("/src")));
    QCOMPARE(relay::boardRootFor({old + QStringLiteral("/src")}), old);

    // And the other way round: a `switchboard/` project inside an `issues/` one.
    const QString outer = tmp.path() + QStringLiteral("/outer");
    makeBoard(outer, QStringLiteral("issues"));
    const QString inner = outer + QStringLiteral("/tools/inner");
    makeBoard(inner, QStringLiteral("switchboard"));
    QVERIFY(QDir().mkpath(inner + QStringLiteral("/src")));
    QCOMPARE(relay::boardRootFor({inner + QStringLiteral("/src")}), inner);
    // A directory between the two belongs to the outer, older board.
    QCOMPARE(relay::boardRootFor({outer + QStringLiteral("/tools")}), outer);

    // One directory with both folders is still one board root, and it is the `switchboard/` one
    // (projects::boardDirOf decides, so the GUI and the registry cannot disagree).
    const QString both = tmp.path() + QStringLiteral("/both");
    makeBoard(both, QStringLiteral("issues"));
    makeBoard(both, QStringLiteral("switchboard"));
    QCOMPARE(relay::boardRootFor({both}), both);
    QCOMPARE(relay::projects::boardDirOf(both), both + QStringLiteral("/switchboard"));

    // A folder with no `board.yaml` in it is not a board: the marker is the switch, not the name.
    const QString named = tmp.path() + QStringLiteral("/named");
    QVERIFY(QDir().mkpath(named + QStringLiteral("/switchboard")));
    QVERIFY(QDir().mkpath(named + QStringLiteral("/issues")));
    QVERIFY(relay::boardRootFor({named}).isEmpty());
}

// RelayWindow::boardWorkspace() needs a whole window to run, so the one thing that must never come
// back is read out of the header as text, the way settingspane_test.cpp reads settingsSections().
// The two fallbacks below are the launch directory of the whole process: with either of them, a
// board is "found" from every pane in every window.
void BoardWorkspaceTests::theWindowsRuleAsksThePaneAndNothingElse()
{
    const QString text = windowSource();
    QVERIFY2(!text.isEmpty(), "src/RelayWindow.h could not be read");
    const QString body = bodyOf(text, QStringLiteral("QString boardWorkspace() const {"));
    QVERIFY2(!body.isEmpty(), "RelayWindow::boardWorkspace() is gone");
    QVERIFY2(!body.contains(QStringLiteral("m_manager->workspace()")),
             "boardWorkspace() is back to falling back on the window manager's launch directory");
    QVERIFY2(!body.contains(QStringLiteral("QDir::currentPath()")),
             "boardWorkspace() is back to falling back on the process's current directory");
    QVERIFY2(body.contains(QStringLiteral("relay::boardRootFor")), qPrintable(body));
    // The pane's *live* terminal directory and nothing else. `m_active->workspace()` is frozen
    // when the pane is made and inherited from the launch directory, so it is not a candidate:
    // with it, a pane sitting in ~/Downloads still "belongs to" the project Relay was started in.
    QVERIFY2(body.contains(QStringLiteral("m_active->cwd()")), qPrintable(body));
    QVERIFY2(!body.contains(QStringLiteral("m_active->workspace()")),
             "boardWorkspace() is back to treating the pane's frozen launch workspace as a candidate");
    // An attached tab keeps its own project wherever its panes wander (#JN7X).
    QVERIFY2(body.contains(QStringLiteral("tabProject(")), qPrintable(body));

    // The candidate is derived fresh from the pane's cwd, never cached on the pane.
    const QString candidate = bodyOf(text, QStringLiteral("QString candidateProject() const {"));
    QVERIFY2(!candidate.isEmpty(), "RelayWindow::candidateProject() is gone");
    QVERIFY2(candidate.contains(QStringLiteral("relay::projects::candidateFor(m_active->cwd())")),
             qPrintable(candidate));
}

// One funnel for attaching (card #JN7X): `attachTab` is the only thing that writes the map of
// tab → project, the only caller of the registry's remember(), and the only place a tab's panes
// are re-pointed. Two writers would be two answers to "how did this tab get a project", and the
// one that skipped remember() or repointTabPanes() would leave the panes on the old board.
void BoardWorkspaceTests::attachTabIsTheOnlyWriterOfTheTabsProject()
{
    const QString text = windowSource();
    QVERIFY2(!text.isEmpty(), "src/RelayWindow.h could not be read");
    const QString attach = bodyOf(text, QStringLiteral("void attachTab(QWidget *page, const QString &project, const QString &reason) {"));
    QVERIFY2(!attach.isEmpty(), "RelayWindow::attachTab() is gone");
    QVERIFY(attach.contains(QStringLiteral("m_tabProject.insert(page, normalized)")));
    QVERIFY(attach.contains(QStringLiteral("remember(")));
    QVERIFY(attach.contains(QStringLiteral("repointTabPanes(page)")));

    // Nowhere else inserts into the map, and only detachTab()/forgetTab() take a tab out of it.
    QCOMPARE(text.count(QStringLiteral("m_tabProject.insert")), 1);
    QCOMPARE(text.count(QStringLiteral("m_tabProject.take")), 1);
    QCOMPARE(text.count(QStringLiteral("m_tabProject.remove")), 1);
    QCOMPARE(text.count(QStringLiteral("projects().remember(")), 1);
    QVERIFY(bodyOf(text, QStringLiteral("void detachTab(QWidget *page) {"))
                .contains(QStringLiteral("m_tabProject.take(page)")));
    QVERIFY(bodyOf(text, QStringLiteral("void forgetTab(QWidget *page) {"))
                .contains(QStringLiteral("m_tabProject.remove(page)")));

    // The three shapes of the block a pane's worker is configured with (protocol 19.1): no board
    // at all while the tab is attached to nothing, and an uninitialized project attaches without
    // anything being created.
    const QString settings = bodyOf(text, QStringLiteral("QJsonObject boardSettingsFor(QWidget *page) const {"));
    QVERIFY2(!settings.isEmpty(), "RelayWindow::boardSettingsFor() is gone");
    QVERIFY(settings.contains(QStringLiteral("QStringLiteral(\"attach\"), false")));
    QVERIFY(settings.contains(QStringLiteral("uninitialized")));
    QVERIFY(settings.contains(QStringLiteral("boardDirOf(project)")));
}

// The pane's one configure funnel carries the board block, so a pane can never reach the worker
// without saying which Switchboard it is on. A `configure` with no block at all makes the worker
// walk up from the workspace — the launch directory — which is the bug this card is about.
void BoardWorkspaceTests::theConfigureFunnelAlwaysSendsABoardBlock()
{
    QFile source(QStringLiteral(RELAY_SOURCE_DIR "/src/Pane.h"));
    QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(source.fileName()));
    const QString text = QString::fromUtf8(source.readAll());
    const QString body = bodyOf(text, QStringLiteral("QJsonObject withSessionFields(QJsonObject request) const {"));
    QVERIFY2(!body.isEmpty(), "Pane::withSessionFields() is gone");
    QVERIFY2(body.contains(QStringLiteral("request.insert(QStringLiteral(\"board\"), onBoardSettings())")),
             qPrintable(body));
    // Every `configure` this pane sends goes through that funnel.
    QCOMPARE(text.count(QStringLiteral("{\"type\", \"configure\"}")),
             text.count(QStringLiteral("withSessionFields(QJsonObject{{\"type\", \"configure\"}")));
    // Re-pointing an attached pane is `set_board`, not another configure: the conversation lives.
    QVERIFY(text.contains(QStringLiteral("QStringLiteral(\"set_board\")")));
}

// The tab helper's `configure` carries the keybinding catalogue, the same block a pane's does and
// from the same builder (#GMCF, owner 2026-09-20). Without it `app_action_list` has no keys to
// show, and the Actions pane's brief promises the palette "with its keyboard shortcut beside it";
// with it the helper is also offered `set_keybinding`, which is the owner's decision. A reload has
// to reach the helper too — a helper writing against last hour's keys is the failure this pins.
void BoardWorkspaceTests::theHelpersConfigureCarriesTheKeybindings()
{
    const QString text = windowSource();
    QVERIFY2(!text.isEmpty(), "src/RelayWindow.h could not be read");
    const QString start = bodyOf(text, QStringLiteral("void startBoardWorker(QWidget *page) {"));
    QVERIFY2(!start.isEmpty(), "RelayWindow::startBoardWorker() is gone");
    // The pane's builder, not a second copy of the catalogue.
    QVERIFY2(start.contains(QStringLiteral("configure.insert(QStringLiteral(\"keybindings\"), Keymap::instance().catalog())")),
             qPrintable(start));
    const QString send = bodyOf(text, QStringLiteral("void sendHelperKeybindings() {"));
    QVERIFY2(!send.isEmpty(), "RelayWindow::sendHelperKeybindings() is gone");
    QVERIFY(send.contains(QStringLiteral("QStringLiteral(\"keybindings\")")));
    QVERIFY(send.contains(QStringLiteral("Keymap::instance().path()")));
    // An unconfigured worker has no agent to hand it to and answers with an error.
    QVERIFY2(send.contains(QStringLiteral("worker->configured()")), qPrintable(send));
    // And the reload listener calls it, beside the panes'.
    QVERIFY2(text.contains(QStringLiteral("sendHelperKeybindings();")), "nothing reloads the helper");
}

// The tab's helper is the tab's (§30.7): it serves the Switchboard, Options, Actions and Sessions
// panes of that tab, so a Switchboard put away must not stop it and only the tab closing may.
// This is the rule the old per-board-pane teardown broke, and the one a future edit is most
// likely to break again by hanging the worker off a pane once more.
void BoardWorkspaceTests::theHelperOutlivesItsPanelsAndOnlyTheTabEndsIt()
{
    const QString text = windowSource();
    QVERIFY2(!text.isEmpty(), "src/RelayWindow.h could not be read");
    // One owner: the tab, through forgetTab().
    const QString forget = bodyOf(text, QStringLiteral("void forgetTab(QWidget *page) {"));
    QVERIFY2(forget.contains(QStringLiteral("releaseBoardWorker(page)")), qPrintable(forget));
    QCOMPARE(text.count(QStringLiteral("releaseBoardWorker(page);")), 1);
    // And nothing takes a worker out of the map except that release and the window's own
    // shutdown: `m_boardWorkers.take(...)` anywhere else is a pane stopping the tab's helper.
    QCOMPARE(text.count(QStringLiteral("m_boardWorkers.take(")), 1);
}

// A worker that dies mid-turn (its tab closed under it, it crashed, it could not start) tells
// nobody: `done` never comes, so the panel that asked keeps its busy strip and refuses the next
// prompt for ever. That is what the owner met — "message queue isn't working in the sessions
// helper, i can't interrupt" (2026-09-20) — after the same worker died with an `app_open` still
// in flight. Every panel that was waiting is put back, and the next ask starts a fresh worker.
void BoardWorkspaceTests::aHelperThatDiesMidTurnPutsItsPanelsBack()
{
    const QString text = windowSource();
    QVERIFY2(!text.isEmpty(), "src/RelayWindow.h could not be read");
    // The worker reports it, and the window listens.
    QVERIFY2(text.contains(QStringLiteral("worker->onExit = [guard, tab](bool crashed) {")),
             "nothing is listening for a helper worker's death");
    const QString gone = bodyOf(text, QStringLiteral("void helperWorkerGone(const QString &tab, bool crashed) {"));
    QVERIFY2(!gone.isEmpty(), "RelayWindow::helperWorkerGone() is gone");
    // It ends the turn the way a turn ends, so every surface already knows how to draw it…
    QVERIFY2(gone.contains(QStringLiteral("QStringLiteral(\"error\")")), qPrintable(gone));
    QVERIFY2(gone.contains(QStringLiteral("m_helperWaiting.take(tab)")), qPrintable(gone));
    // …including a **card** turn, which the consoles cannot answer for: the card page follows its
    // own turn by card id, so with the worker gone its busy strip would stay up for ever and
    // p/x/v would stay disabled. The surface a card turn carries is `card:<ID>` (protocol 33).
    QVERIFY2(gone.contains(QStringLiteral("QStringLiteral(\"card:\")")), qPrintable(gone));
    QVERIFY2(gone.contains(QStringLiteral("{QStringLiteral(\"card_id\"), surface.mid(5)}")), qPrintable(gone));
    // The `chat: true` / `pane: <name>` tags the panels filtered on are gone with the panels
    // (card #AGNT step 9), and so is the second delivery to them: nothing reads either.
    QVERIFY2(!gone.contains(QStringLiteral("{QStringLiteral(\"chat\"), true}")), qPrintable(gone));
    QVERIFY2(!gone.contains(QStringLiteral("QStringLiteral(\"pane\")")), qPrintable(gone));
    QVERIFY2(!gone.contains(QStringLiteral("deliverToHelperPanels(page, event)")), qPrintable(gone));
    // `deliverToHelperPanels` itself stays: the Test suites pane's `tests_*` and the Profile
    // pane's `profile_*` ride the same tab worker and subscribe through `listenToHelper`.
    QVERIFY(text.contains(QStringLiteral("void deliverToHelperPanels(QWidget *page, const QJsonObject &event) {")));
    // The waiting list is kept from the worker's own events. Since card #AGNT the wire says it in
    // one word: `board_chat_started`/`board_chat_queued` are retired, a console's turn is an
    // ordinary pane turn, and `surface` rides on `queued`, `agent_started` and `agent_finished`.
    QVERIFY2(!text.contains(QStringLiteral("board_chat_started")), "board_chat is back on the wire");
    QVERIFY2(!text.contains(QStringLiteral("board_chat_queued")), "board_chat is back on the wire");
    QVERIFY(text.contains(QStringLiteral("guard->m_helperWaiting[tab].insert(surface)")));
    QVERIFY(text.contains(QStringLiteral("guard->m_helperWaiting[tab].remove(surface)")));
    // And the consoles of the tab are told as well, once: it is one agent that died and every
    // console of the tab was on it.
    QVERIFY2(gone.contains(QStringLiteral("deliverToConsoles(tab,")), qPrintable(gone));
    // A deliberate stop is not a death: the callbacks come off before the worker is asked to go.
    const QString release = bodyOf(text, QStringLiteral("void releaseBoardWorker(QWidget *page) {"));
    QVERIFY2(release.contains(QStringLiteral("worker->onExit = nullptr")), qPrintable(release));

    QFile source(QStringLiteral(RELAY_SOURCE_DIR "/src/BoardWorker.cpp"));
    QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(source.fileName()));
    const QString worker = QString::fromUtf8(source.readAll());
    QVERIFY2(worker.contains(QStringLiteral("onExit(status == QProcess::CrashExit)")),
             "BoardWorker no longer reports an exit nobody asked for");
    QVERIFY2(worker.contains(QStringLiteral("if (m_stopping)\n                    return;")),
             "a deliberate stop reports itself as a death");
}

// ----- card #AGNT step 5: the window makes consoles ------------------------------------------
//
// A console is a `Pane` with a non-terminal context, embedded in a host's `ToolPane`. The one
// line that keeps it out of everything a *window's* pane is in is `panesIn` stopping at a
// `ToolPane`, the way `leavesIn` already does. Without it the console is published to the phone,
// counted in the close dialog, makes the first real terminal default to Flash, churns the
// scrollback prune, resolves a notification to a non-leaf and takes a `set_board` meant for a
// terminal agent — six things, from one recursion.
void BoardWorkspaceTests::anEmbeddedConsoleIsNotOneOfTheWindowsPanes()
{
    const QString text = windowSource();
    QVERIFY2(!text.isEmpty(), "src/RelayWindow.h could not be read");
    const QString walk = bodyOf(text, QStringLiteral("static QList<Pane *> panesIn(QWidget *root) {"));
    QVERIFY2(!walk.isEmpty(), "RelayWindow::panesIn() is gone");
    QVERIFY2(walk.contains(QStringLiteral("if (dynamic_cast<ToolPane *>(root)) return panes;")),
             qPrintable(walk));
    // It must stop *before* the child walk, or the early-out is decoration.
    QVERIFY(walk.indexOf(QStringLiteral("if (dynamic_cast<ToolPane *>(root)) return panes;"))
            < walk.indexOf(QStringLiteral("findChildren<QWidget *>")));
    // `leavesIn` already stopped there, and `isLeaf` is what says a ToolPane is a leaf at all —
    // and what says an embedded console is **not** one, which is the walk *up*
    // (aConsoleIsNotALeafSoTheActivePaneIsNeverOne, below).
    QVERIFY(text.contains(QStringLiteral("static bool isLeaf(QWidget *widget) {")));
    QVERIFY(bodyOf(text, QStringLiteral("    static bool isLeaf(QWidget *widget) {"))
                .contains(QStringLiteral("dynamic_cast<ToolPane *>(widget) != nullptr")));
    // The six readers go on reading `panesIn`/`allPanes()` — they are not to be taught about
    // consoles one by one, which is the whole point of fixing the walk instead.
    QVERIFY(text.contains(QStringLiteral("!allPanes().isEmpty()")));   // the first-pane Flash default
    // And the window reaches a console the only other way there is: its own list.
    QVERIFY2(text.contains(QStringLiteral("QList<ConsoleEntry> m_consoles;")), "the console list is gone");
    // Nothing may put a console in a leaf list: it is created with a parent and registered
    // nowhere else. `createAgentConsole` is the one place a console is made.
    QCOMPARE(text.count(QStringLiteral("new Pane(here, here, m_manager->cleanShell()")), 1);
}

// What the factory wires, and what it deliberately does not. The callbacks a console shares with
// a terminal pane are the ones that answer "open this thing in this window"; the ones it must not
// have are the terminal's (a shell, a guest, a fork) and a leaf's (a title, a share, a close).
void BoardWorkspaceTests::theWindowMakesConsolesAndWiresThemAsPanesExceptWhereItMustNot()
{
    const QString text = windowSource();
    QVERIFY2(!text.isEmpty(), "src/RelayWindow.h could not be read");
    const QString make = bodyOf(text, QStringLiteral("relay::agent::ConsoleHandle createAgentConsole(relay::agent::Context *context, QWidget *parent) {"));
    QVERIFY2(!make.isEmpty(), "RelayWindow::createAgentConsole() is gone");
    // Every call the handle promises is answered, and each one guards the pane it points at.
    for (const QString &call : {QStringLiteral("handle.widget = console;"),
                                QStringLiteral("handle.focusComposer"), QStringLiteral("handle.draftInComposer"),
                                QStringLiteral("handle.composerText"), QStringLiteral("handle.setCollapsed"),
                                QStringLiteral("handle.collapsed"), QStringLiteral("handle.runActionLetter")})
        QVERIFY2(make.contains(call), qPrintable(call));
    const QString wire = bodyOf(text, QStringLiteral("void wireAgentConsole(Pane *console) {"));
    QVERIFY2(!wire.isEmpty(), "RelayWindow::wireAgentConsole() is gone");
    for (const QString &hook : {QStringLiteral("console->onWorkerLine"), QStringLiteral("console->onStatus"),
                                QStringLiteral("console->onOpenPath"), QStringLiteral("console->onOpenCard"),
                                QStringLiteral("console->onOpenOption"), QStringLiteral("console->onOpenSessions"),
                                QStringLiteral("console->onOpenInternals"), QStringLiteral("console->onOpenInfo"),
                                QStringLiteral("console->onAppCatalog"), QStringLiteral("console->onBoardSettings")})
        QVERIFY2(wire.contains(hook), qPrintable(hook));
    // `app_command` is answered once per tab by the window, so a console must not answer it too —
    // four consoles on one worker would run the same write four times.
    QVERIFY2(!wire.contains(QStringLiteral("console->onAppCommand")), qPrintable(wire));
    const QString deliver = bodyOf(text, QStringLiteral("void deliverToConsoles(const QString &tab, const QJsonObject &event) {"));
    QVERIFY2(deliver.contains(QStringLiteral("QStringLiteral(\"app_command\")")), qPrintable(deliver));
    // A console is a terminal pane in nothing that needs a shell or a place in the splitter tree.
    for (const QString &absent : {QStringLiteral("console->onShellExited"), QStringLiteral("console->onOpenGuestPane"),
                                  QStringLiteral("console->onForkState"), QStringLiteral("console->onShareTab"),
                                  QStringLiteral("console->onRenameTab"), QStringLiteral("console->onTitleChanged"),
                                  QStringLiteral("console->hasPaneSiblings"), QStringLiteral("console->onPersonPrompt")})
        QVERIFY2(!wire.contains(absent), qPrintable(absent));
    // The openers that insist on a `Pane *owner` dock beside a leaf, and a console is not one:
    // they are handed the tab's own terminal pane, or nothing.
    QVERIFY(wire.contains(QStringLiteral("w->paneForConsoleOpen(guard)")));
    const QString fallback = bodyOf(text, QStringLiteral("Pane *paneForConsoleOpen(QWidget *console) const {"));
    QVERIFY2(fallback.contains(QStringLiteral("return panes.isEmpty() ? nullptr : panes.first();")), qPrintable(fallback));
}

// One worker per tab, one conversation in it, and each ask tagged with the console that asked
// (owner decision 1). The window supplies what a host cannot know — the tab's project and the
// tab's conversation key — and nothing else about the context is the window's to invent.
void BoardWorkspaceTests::theConsolesOfATabShareOneWorkerAndOneConversation()
{
    const QString text = windowSource();
    QVERIFY2(!text.isEmpty(), "src/RelayWindow.h could not be read");
    const QString spec = bodyOf(text, QStringLiteral("relay::agent::ContextSpec spec() const override {"));
    QVERIFY2(!spec.isEmpty(), "RelayWindow::TabConsoleContext::spec() is gone");
    // The conversation: the helper store of §30.7, keyed by the tab's own persistent id — so two
    // consoles of one tab resolve to one conversation and two tabs on one project do not.
    QVERIFY2(spec.contains(QStringLiteral("spec.persistScope = QStringLiteral(\"helper\")")), qPrintable(spec));
    QVERIFY2(spec.contains(QStringLiteral("page->property(\"relayTabId\").toString()")), qPrintable(spec));
    // The project is the tab's, and a tab attached to nothing gives a board-less console rather
    // than an error — the same rule `startBoardWorker` follows for its `workspace`.
    QVERIFY2(spec.contains(QStringLiteral("m_window->boardWorkspaceOfTab(page)")), qPrintable(spec));
    // A console never spawns a shell and a line typed in it has nowhere else to go.
    QVERIFY(spec.contains(QStringLiteral("spec.shell = false;")));
    QVERIFY(spec.contains(QStringLiteral("spec.routing = QStringLiteral(\"agent\")")));
    // What the host says stays the host's: the name, the actions, the links, the placeholder.
    QVERIFY2(!spec.contains(QStringLiteral("spec.name =")), "the window is naming the host's context");

    // One worker: a console's line goes on `helperWorker(page, true)`, the same call a Switchboard
    // pane makes, and the worker is started by the first thing a console says.
    const QString send = bodyOf(text, QStringLiteral("void sendFromConsole(Pane *console, const QJsonObject &message) {"));
    QVERIFY2(send.contains(QStringLiteral("helperWorker(page, true)")), qPrintable(send));
    // And the window owns the configure: a console building one of its own would overwrite the
    // tab's context block and its conversation key with a pane's.
    QVERIFY2(send.contains(QStringLiteral("QStringLiteral(\"configure\")")), qPrintable(send));
    QCOMPARE(text.count(QStringLiteral("void startBoardWorker(QWidget *page) {")), 1);
    const QString start = bodyOf(text, QStringLiteral("void startBoardWorker(QWidget *page) {"));
    QVERIFY2(start.contains(QStringLiteral("configure.insert(QStringLiteral(\"context\"), console->contextBlock())")),
             qPrintable(start));
    // A console made after the worker was already up is handed the handshake it missed, in order,
    // so it is configured before it can be asked anything.
    const QString attach = bodyOf(text, QStringLiteral("void attachConsoleToTab(Pane *console) {"));
    QVERIFY2(attach.contains(QStringLiteral("handshakeKey(tab, QStringLiteral(\"ready\"))")), qPrintable(attach));
    QVERIFY2(attach.contains(QStringLiteral("handshakeKey(tab, QStringLiteral(\"configured\"))")), qPrintable(attach));
    QVERIFY(attach.indexOf(QStringLiteral("QStringLiteral(\"ready\")")) < attach.indexOf(QStringLiteral("QStringLiteral(\"configured\")")));
    // And the key is built rather than spelled: "\\x1fconfigured" is one out-of-range hex escape,
    // not a separator followed by a word, which is the bug that shape invites.
    QVERIFY2(!text.contains(QStringLiteral("\\x1fconfigured\"")), "a hex escape is eating the word after it");
    // Every event of the tab's worker reaches every console of that tab: the conversation is one.
    QVERIFY(text.contains(QStringLiteral("guard->deliverToConsoles(tab, event);")));

    // The panel template that gave those surfaces a second chat implementation is gone, and the
    // tab worker it used is not: `sendToHelper` no longer carries `board_chat`'s `pane` tag.
    QVERIFY2(!text.contains(QStringLiteral("void wireHelperPanel(")), "wireHelperPanel is back");
    QVERIFY2(text.contains(QStringLiteral("void sendToHelper(QWidget *page, QJsonObject message) {")),
             "sendToHelper still carries a pane tag");
    QVERIFY(text.contains(QStringLiteral("void listenToHelper(")));   // tests_* and profile_* still ride it
}

// Every console the window makes is a `Pane` with the window's **wrapper** as its context, not
// the host's own — `TabConsoleContext` is what knows the tab's project and where the tab's one
// conversation is kept (above). So the wrapper has to forward every virtual of
// `relay::agent::Context`, and a virtual it forgets is silently swallowed: `submit` was, and a
// card's Enter travelled as an ordinary `ask` instead of the `board_ask` of 19.10 — the thread
// unwritten, the stage not advanced (owner decision 2). Nothing in `consolemode` could see it,
// because those cases hand a context to a `Pane` directly.
//
// The list is read out of `src/AgentContext.h` rather than written here, so the **next** virtual
// somebody adds fails this test on the day it is added rather than on the day a card stops
// writing its thread.
void BoardWorkspaceTests::theWindowsWrapperForwardsEveryContextVirtual()
{
    QFile header(QStringLiteral(RELAY_SOURCE_DIR "/src/AgentContext.h"));
    QVERIFY2(header.open(QIODevice::ReadOnly | QIODevice::Text), "src/AgentContext.h could not be read");
    const QString context = QString::fromUtf8(header.readAll());
    const QString text = windowSource();
    QVERIFY2(!text.isEmpty(), "src/RelayWindow.h could not be read");
    const int wrapper = text.indexOf(QStringLiteral("class TabConsoleContext final : public relay::agent::Context {"));
    QVERIFY2(wrapper > 0, "RelayWindow::TabConsoleContext is gone");
    const int end = text.indexOf(QStringLiteral("\n    };"), wrapper);
    QVERIFY(end > wrapper);
    const QString body = text.mid(wrapper, end - wrapper);

    // The names of the virtuals, as the interface declares them: `virtual <type> name(`.
    QRegularExpression declaration(QStringLiteral("\\n    virtual [^;{]*?\\b(\\w+)\\("));
    QStringList virtuals;
    for (auto it = declaration.globalMatch(context); it.hasNext();) {
        const QString name = it.next().captured(1);
        if (name != QStringLiteral("Context") && !virtuals.contains(name)) virtuals << name;
    }
    virtuals.removeAll(QStringLiteral("~Context"));
    QVERIFY2(virtuals.contains(QStringLiteral("submit")), qPrintable(virtuals.join(' ')));
    QVERIFY2(virtuals.size() >= 5, qPrintable(virtuals.join(' ')));

    for (const QString &name : virtuals) {
        QVERIFY2(body.contains(name + QStringLiteral("(")),
                 qPrintable(QStringLiteral("TabConsoleContext does not override Context::%1").arg(name)));
        // `spec()` is the one it does not simply hand on — it fills in the tab's project and
        // persist key, which is what the wrapper exists for — but it still starts from the host's.
        QVERIFY2(body.contains(QStringLiteral("m_host->") + name + QStringLiteral("(")),
                 qPrintable(QStringLiteral("TabConsoleContext::%1 does not reach the host's").arg(name)));
    }
    // And the other direction: a card going busy or Options swapping mode still reaches the row.
    QVERIFY2(body.contains(QStringLiteral("m_host->onChanged = [this] { changed(); }")), qPrintable(body));
}

// The walk **up**. `panesIn` stopping at a `ToolPane` (above) keeps a console out of everything
// that walks the splitter tree downwards; `leafOf()` walks the other way — from a clicked widget
// up to the first leaf — and `isLeaf` said a console was one, because a console is a `Pane`.
//
// So a click in the Switchboard's or the Sessions helper's prompt box made that console
// `m_activeLeaf` and `m_active`, and every window path that aims at "the active pane" landed on
// a surface whose leaf-shaped hooks are deliberately unset. The integration drive caught it as
// `app_open {target: conversation, ids}` from the Sessions helper: the tool answered ok and not
// one pane opened, because `m_active->openSavedSession()` reached a console with no
// `onOpenSessionInNewPane`.
void BoardWorkspaceTests::aConsoleIsNotALeafSoTheActivePaneIsNeverOne()
{
    const QString text = windowSource();
    QVERIFY2(!text.isEmpty(), "src/RelayWindow.h could not be read");
    const QString leaf = bodyOf(text, QStringLiteral("    static bool isLeaf(QWidget *widget) {"));
    QVERIFY2(!leaf.isEmpty(), "RelayWindow::isLeaf() is gone or is a one-liner again");
    // `sharesWorker()` is the test: the window sets `onWorkerLine` on a console it made and on
    // nothing else, so it is "I am embedded" rather than a name or a parent to guess from.
    QVERIFY2(leaf.contains(QStringLiteral("sharesWorker()")), qPrintable(leaf));
    QVERIFY2(leaf.contains(QStringLiteral("ToolPane")), qPrintable(leaf));
    // And the one walker that needs it still asks: leafOf() is what a press goes through.
    const QString of = bodyOf(text, QStringLiteral("    static QWidget *leafOf(QWidget *widget) {"));
    QVERIFY2(of.contains(QStringLiteral("isLeaf(w)")), qPrintable(of));
    QVERIFY2(bodyOf(text, QStringLiteral("    bool activateOnPress(QObject *object, QEvent *event) {"))
                 .contains(QStringLiteral("leafOf(widget)")),
             "a press no longer resolves the pane it landed in");
    // The one place that turns a leaf into `m_active` reads the same answer.
    const QString active = bodyOf(text, QStringLiteral("    void setActiveLeaf(QWidget *leaf) {"));
    QVERIFY2(active.contains(QStringLiteral("dynamic_cast<Pane *>(leaf)")), qPrintable(active));

    // And the other thing a console's insides are found by. `RichEditor` declares no `Q_OBJECT`,
    // so `findChild<RichEditor *>()` matches on `QPlainTextEdit`'s metaobject and answers the
    // first plain text edit in the console — which is the transcript's fallback view whenever
    // that has been built, not the prompt box. The card page pointed `m_reply` at it, read it
    // empty and returned, and **Enter on a card did nothing**: no `board_ask`, no thread entry,
    // no stage advance (19.10, owner decision 2). The name is the answer.
    QFile pane(QStringLiteral(RELAY_SOURCE_DIR "/src/BoardPane.cpp"));
    QVERIFY2(pane.open(QIODevice::ReadOnly | QIODevice::Text), "src/BoardPane.cpp could not be read");
    const QString board = QString::fromUtf8(pane.readAll());
    QVERIFY2(!board.contains(QStringLiteral("findChild<RichEditor *>()")),
             "the card's reply box is being found by a type that has no metaobject of its own");
    QVERIFY2(board.contains(QStringLiteral("findChild<RichEditor *>(QStringLiteral(\"composerEditor\"))")),
             "the card's reply box is no longer found by name");
}

// Card #AGNT step 9. The helper was a second implementation of the prompt box — its own panel,
// its own `board_chat` FIFO, its own model box — and the card is about there being one. These
// five files are the second implementation, and a file that comes back is a second one coming
// back, which is the thing to notice on the day it happens rather than a year later.
void BoardWorkspaceTests::theHelperPanelAndItsModelBoxAreGoneFromTheTree()
{
    for (const char *gone : {"src/HelperChat.h", "src/HelperChat.cpp", "src/HelperModelBox.h",
                             "src/HelperModelBox.cpp", "src/BoardChat.h",
                             "tests/helpermodelbox_test.cpp"})
        QVERIFY2(!QFile::exists(QStringLiteral(RELAY_SOURCE_DIR "/") + QLatin1String(gone)),
                 qPrintable(QStringLiteral("%1 is back").arg(QLatin1String(gone))));

    QFile lists(QStringLiteral(RELAY_SOURCE_DIR "/CMakeLists.txt"));
    QVERIFY2(lists.open(QIODevice::ReadOnly | QIODevice::Text), "CMakeLists.txt could not be read");
    const QString cmake = QString::fromUtf8(lists.readAll());
    QVERIFY2(!cmake.contains(QStringLiteral("add_library(relay-helperchat")), "the library is back");
    QVERIFY2(!cmake.contains(QStringLiteral("relay-helpermodelbox-tests")), "its test target is back");
    // relay-board linked the panel's library for `relay-editor` and `relay-toollabel`; it names
    // those two itself now, so the card page's reply box and a turn's progress line still build.
    QVERIFY2(cmake.contains(QStringLiteral("target_link_libraries(relay-board PUBLIC relay-editor relay-toollabel")),
             "relay-board lost the two libraries relay-helperchat used to bring it");

    // And nothing in the window still names them. `relay::helpermodel` went with the box: every
    // prompt box in Relay draws its own rows from its own worker now (#PK5Q).
    const QString text = windowSource();
    QVERIFY2(!text.contains(QStringLiteral("helpermodel")), "the window still names that namespace");
    QVERIFY2(!text.contains(QStringLiteral("HelperModelBox")), "the window still includes HelperModelBox.h");
    QVERIFY2(!text.contains(QStringLiteral("m_helperModels")), "the per-tab helper model state is back");
}

// `option:sec/row` and `session:<id>` are kinds of the transcript since step 8, so an answer that
// names a setting or a saved conversation is one click from it in *any* pane — not only inside the
// helper panel's browser. The window is what opens them.
void BoardWorkspaceTests::anOptionOrSessionLinkOpensWhereItNames()
{
    const QString text = windowSource();
    QVERIFY2(!text.isEmpty(), "src/RelayWindow.h could not be read");
    // Both prompt boxes route it the same way, and each takes the two steps `app_open {target:
    // "options", row}` takes — the section, then the row. Read out of the two bodies rather than
    // counted over the file: the Switchboard's own answers and `app_open` reveal rows too, and a
    // count would make this test fail for their reasons.
    const QString wire = bodyOf(text, QStringLiteral("void wireAgentConsole(Pane *console) {"));
    const QString create = bodyOf(text, QStringLiteral("Pane *createPane(const QJsonObject &spec) {"));
    for (const QString &body : {wire, create}) {
        QVERIFY2(body.contains(QStringLiteral("onOpenOption = [guard](const QString &section, const QString &row) {")),
                 qPrintable(body.left(120)));
        QVERIFY2(body.contains(QStringLiteral("openSettingsPane(relay::SettingsPane::Mode::Options, section)")),
                 qPrintable(body.left(120)));
        QVERIFY2(body.contains(QStringLiteral("settings()->revealOption(section, row)")), qPrintable(body.left(120)));
    }
    QVERIFY(wire.contains(QStringLiteral("console->onOpenSessions = [guard](const QString &query) {")));

    QFile source(QStringLiteral(RELAY_SOURCE_DIR "/src/Pane.h"));
    QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(source.fileName()));
    const QString pane = QString::fromUtf8(source.readAll());
    const QString open = bodyOf(pane, QStringLiteral("void openOutputTarget(const QString &target, int line, bool fromMouse) {"));
    QVERIFY2(!open.isEmpty(), "Pane::openOutputTarget() is gone");
    QVERIFY2(open.contains(QStringLiteral("relay::links::optionOf(target, &section, &row) && onOpenOption")), qPrintable(open));
    QVERIFY2(open.contains(QStringLiteral("relay::links::sessionIdOf(target)")), qPrintable(open));
    // The context still gets first refusal: Options reveals its own row without opening a second
    // pane, and only what it declines reaches the window.
    QVERIFY(open.indexOf(QStringLiteral("resolveContextLink(activated)"))
            < open.indexOf(QStringLiteral("url.host() == QStringLiteral(\"option\")")));
    // And the engine's right-click menu stops reading those two as file paths.
    QFile view(QStringLiteral(RELAY_SOURCE_DIR "/engine/view/TerminalView.cpp"));
    QVERIFY2(view.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(view.fileName()));
    const QString engine = QString::fromUtf8(view.readAll());
    QVERIFY2(engine.contains(QStringLiteral("links::optionOf(link.target, &section, &row) || !links::sessionIdOf(link.target).isEmpty()")),
             "a right-click on an option:/session: link is a file path again");
}

// The Switchboard's materials (docs/SWITCHBOARD-AESTHETIC.md 3.4, owner 2026-09-19 "yeah build
// that out"): the pane is painted on the board's face, and its hardware — the engraved rule over a
// section name, the jack rings of an empty board — in the board's brass. src/BoardPane.cpp needs a
// whole worker and a project on disk to run, so the plumbing is pinned here as text, the way
// theWindowsRuleAsksThePaneAndNothingElse() above reads RelayWindow.h.
//
// What must not come back: `theme::Background` under the rows. The board's ground is its face, and
// a row's hover band, the drag image and the empty board all have to be mixed from that face, or
// the board goes back to being the window with a list on it.
void BoardWorkspaceTests::theBoardPanePaintsFromTheBoardMaterials()
{
    QFile source(QStringLiteral(RELAY_SOURCE_DIR "/src/BoardPane.cpp"));
    QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(source.fileName()));
    const QString text = QString::fromUtf8(source.readAll());
    // The rule over a section name is the board's hardware, lit only under the pointer.
    QVERIFY2(text.contains(QStringLiteral("hover ? theme::BoardMetal : theme::BoardMetalDim")),
             "the section rule no longer lights the board's brass");
    // The hover band and the drag image are the face, not the window.
    QVERIFY2(text.contains(QStringLiteral("mix(theme::BoardFace, theme::Text, 0.05)")), "the hover band left the face");
    QVERIFY2(text.contains(QStringLiteral("pixmap.fill(theme::BoardFace)")), "the drag image left the face");
    QVERIFY2(!text.contains(QStringLiteral("theme::Background")),
             "something in the Switchboard is painted on the window's ground again, not the board's face");
    // The empty board is the doc's unpatched board: one unlit jack per section.
    QVERIFY2(text.contains(QStringLiteral("class EmptyBoard final : public QLabel")), "EmptyBoard is gone");
    QVERIFY2(text.contains(QStringLiteral("setSections(titles)")), "the empty board is no longer told the sections");
    // Anchor on EmptyBoard itself: another widget above it paints too (#DPJB), and the first
    // paintEvent in the file is no longer the empty board's.
    const QString jacks = bodyOf(text.mid(text.indexOf(QStringLiteral("class EmptyBoard final : public QLabel"))),
                                QStringLiteral("    void paintEvent(QPaintEvent *) override\n    {"));
    QVERIFY2(jacks.contains(QStringLiteral("drawEllipse")), qPrintable(jacks.left(200)));
    QVERIFY2(jacks.contains(QStringLiteral("theme::BoardMetalDim")), "the jack rings are not unlit brass");
    // And its words stay on the legible text tokens: no material behind anything read (2.2).
    QVERIFY2(jacks.contains(QStringLiteral("theme::TextMuted")), "the empty board's words left the text tokens");
}

// The width rule lives in RelayWindow because Qt owns the tab geometry, but the decision itself
// is pure in PaneUsage. Pin the two halves together: a narrow strip must remove the whole suffix,
// and Resize must reconsider the labels so widening restores it.
void BoardWorkspaceTests::tabMetersGiveWayOnlyWhenFullLabelsDoNotFit()
{
    const QString text = windowSource();
    QVERIFY2(!text.isEmpty(), "src/RelayWindow.h could not be read");
    const QString fit = bodyOf(text, QStringLiteral("bool tabMetersHaveRoom() const {"));
    QVERIFY2(!fit.isEmpty(), "RelayWindow::tabMetersHaveRoom() is gone");
    QVERIFY(fit.contains(QStringLiteral("relay::usage::tabMetersFit(usable, tabsFullLabelWidths())")));
    const QString label = bodyOf(text, QStringLiteral("QString tabLabelText(QWidget *page, const QStringList &titles) const {"));
    QVERIFY2(!label.isEmpty(), "RelayWindow::tabLabelText() is gone");
    QVERIFY(label.contains(QStringLiteral("if (tabMetersHaveRoom()) title += tabUsageSuffix(page);")));
    const QString eventFilter = bodyOf(text, QStringLiteral("bool eventFilter(QObject *object, QEvent *event) override {"));
    QVERIFY(eventFilter.contains(QStringLiteral("if (resized) relabelTabsForWidth();")));
}

QTEST_MAIN(BoardWorkspaceTests)
#include "boardworkspace_test.moc"
