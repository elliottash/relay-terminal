// SPDX-License-Identifier: GPL-3.0-or-later
// Which project's Switchboard a pane is looking at (src/BoardWorkspace.h). The Switchboard is
// per project, so the rule has to answer from the candidate directories it is handed and from
// nothing else — in particular never from the directory Relay itself was started in, which is
// what made one project's board appear in every pane of every window (owner report, 2026-09-18).
#include "BoardWorkspace.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// A project root: `<dir>/issues/board.yaml`, which is the marker the rule looks for.
void makeBoard(const QString &root)
{
    QVERIFY(QDir().mkpath(root + QStringLiteral("/issues")));
    QFile marker(root + QStringLiteral("/issues/board.yaml"));
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.write("columns: [inbox]\n");
}

}  // namespace

class BoardWorkspaceTests : public QObject {
    Q_OBJECT

private slots:
    void theNearestAncestorWithABoardWins();
    void theFirstCandidateWithABoardWins();
    void noBoardAnywhereIsEmpty();
    void theProcessesOwnDirectoryIsNeverConsulted();
    void theWindowsRuleAsksThePaneAndNothingElse();
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

// RelayWindow::boardWorkspace() needs a whole window to run, so the one thing that must never come
// back is read out of the header as text, the way settingspane_test.cpp reads settingsSections().
// The two fallbacks below are the launch directory of the whole process: with either of them, a
// board is "found" from every pane in every window.
void BoardWorkspaceTests::theWindowsRuleAsksThePaneAndNothingElse()
{
    QFile source(QStringLiteral(RELAY_SOURCE_DIR "/src/RelayWindow.h"));
    QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(source.fileName()));
    const QString text = QString::fromUtf8(source.readAll());
    const int start = text.indexOf(QStringLiteral("QString boardWorkspace() const {"));
    QVERIFY2(start > 0, "RelayWindow::boardWorkspace() is gone");
    const int end = text.indexOf(QStringLiteral("\n    }"), start);
    QVERIFY(end > start);
    const QString body = text.mid(start, end - start);
    QVERIFY2(!body.contains(QStringLiteral("m_manager->workspace()")),
             "boardWorkspace() is back to falling back on the window manager's launch directory");
    QVERIFY2(!body.contains(QStringLiteral("QDir::currentPath()")),
             "boardWorkspace() is back to falling back on the process's current directory");
    QVERIFY2(body.contains(QStringLiteral("relay::boardRootFor")), qPrintable(body));
    // The terminal's own directory is asked first; the frozen launch workspace is the fallback.
    const int cwd = body.indexOf(QStringLiteral("m_active->cwd()"));
    const int workspace = body.indexOf(QStringLiteral("m_active->workspace()"));
    QVERIFY2(cwd > 0 && workspace > cwd, qPrintable(body));
}

QTEST_MAIN(BoardWorkspaceTests)
#include "boardworkspace_test.moc"
