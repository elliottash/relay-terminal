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
    void theBoardPanePaintsFromTheBoardMaterials();
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
    const QString jacks = bodyOf(text, QStringLiteral("    void paintEvent(QPaintEvent *) override\n    {"));
    QVERIFY2(jacks.contains(QStringLiteral("drawEllipse")), qPrintable(jacks.left(200)));
    QVERIFY2(jacks.contains(QStringLiteral("theme::BoardMetalDim")), "the jack rings are not unlit brass");
    // And its words stay on the legible text tokens: no material behind anything read (2.2).
    QVERIFY2(jacks.contains(QStringLiteral("theme::TextMuted")), "the empty board's words left the text tokens");
}

QTEST_MAIN(BoardWorkspaceTests)
#include "boardworkspace_test.moc"
