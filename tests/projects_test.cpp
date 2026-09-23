// SPDX-License-Identifier: AGPL-3.0-or-later
// The project model behind the per-project Switchboard (src/Projects.h): the candidate project of a
// terminal directory, the project key that has to match the backend's workspace digest, which board
// a project's cards go to (and whether the user has to be asked first), and the removable registry
// of known and declined projects.
//
// All of it is QtCore, so every case runs on a QTemporaryDir with no window and no git.
#include "Projects.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace relay::projects;

namespace {

// The literal that pins C++ and Python to the same digest. It is absolute, it does not exist, and
// nothing above it is a symlink, so `QFileInfo::absoluteFilePath()` and Python's `Path.resolve()`
// cannot disagree about it. The same pair is asserted in tests/test_conv_index.py against
// `conv_index.workspace_digest()`; change one and the other fails.
const QString kPinnedPath = QStringLiteral("/nonexistent/relay-projects-key-test");
const QString kPinnedKey = QStringLiteral("9cfae240c229914d");

bool touch(const QString &path)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write("# marker\n");
    return true;
}

// A project whose board is in the folder Relay creates today: `board/` (owner, 2026-09-21, #1CXD).
bool makeNewBoard(const QString &dir) { return touch(dir + QStringLiteral("/.board/board.yaml")); }
bool makePreviousBoard(const QString &dir) { return touch(dir + QStringLiteral("/board/board.yaml")); }
// A project whose board was made between 2026-09-19 and 2026-09-21, when the folder was hidden.
bool makeHiddenBoard(const QString &dir) { return touch(dir + QStringLiteral("/.switchboard/board.yaml")); }
// A project whose board was made between 2026-09-18 and 2026-09-19, when the folder was shown.
bool makeBoard(const QString &dir) { return touch(dir + QStringLiteral("/switchboard/board.yaml")); }
// A project whose board predates 2026-09-18 and is still where it was.
bool makeLegacyBoard(const QString &dir) { return touch(dir + QStringLiteral("/issues/board.yaml")); }
bool makeGitDir(const QString &dir) { return QDir().mkpath(dir + QStringLiteral("/.git")); }
bool makeGitFile(const QString &dir) { return touch(dir + QStringLiteral("/.git")); }

// The registry names its own path under $XDG_DATA_HOME, so a test that touches it points that at a
// temporary directory and puts the old value back. Copied from tests/windowstate_test.cpp, which
// does the same for the scrollback store.
class DataHome {
public:
    DataHome()
    {
        m_previous = qgetenv("XDG_DATA_HOME");
        m_had = qEnvironmentVariableIsSet("XDG_DATA_HOME");
        if (m_dir.isValid()) qputenv("XDG_DATA_HOME", m_dir.path().toLocal8Bit());
    }
    ~DataHome()
    {
        if (m_had) qputenv("XDG_DATA_HOME", m_previous);
        else qunsetenv("XDG_DATA_HOME");
    }
    DataHome(const DataHome &) = delete;
    DataHome &operator=(const DataHome &) = delete;
    bool valid() const { return m_dir.isValid(); }
    QString path() const { return m_dir.path(); }

private:
    QTemporaryDir m_dir;
    QByteArray m_previous;
    bool m_had = false;
};

}  // namespace

class ProjectsTest : public QObject {
    Q_OBJECT

private slots:

    // The two options this model reads live in QSettings, so the tests get their own store rather
    // than the developer's. QSettings::setPath and not QStandardPaths::setTestModeEnabled: the
    // registry's own path comes from GenericDataLocation, which the DataHome cases below point at a
    // temporary directory of their own and which test mode would redirect underneath them.
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminalTest"));
        QCoreApplication::setApplicationName(QStringLiteral("projects-test"));
        QVERIFY(m_settings.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_settings.path());
        QSettings().clear();
    }

    // ----- candidateFor -------------------------------------------------------------------------

    void candidateNothingFound()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString deep = root.path() + QStringLiteral("/a/b/c");
        QVERIFY(QDir().mkpath(deep));
        // A bare temporary tree is not a project, and the walk must not fall out of it into
        // whatever happens to be above /tmp.
        QCOMPARE(candidateFor(deep), QString());
    }

    void candidateEmptyAndRelative()
    {
        QCOMPARE(candidateFor(QString()), QString());
        // A relative path would be resolved against the process's own directory — the launch
        // directory this model exists to stop consulting.
        QCOMPARE(candidateFor(QStringLiteral("src")), QString());
        QCOMPARE(candidateFor(QStringLiteral("./src")), QString());
        QCOMPARE(candidateFor(QStringLiteral("../src")), QString());
    }

    void candidateFindsEveryBoardFolder()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString hidden = root.path() + QStringLiteral("/hidden");
        const QString shown = root.path() + QStringLiteral("/shown");
        const QString legacy = root.path() + QStringLiteral("/legacy");
        QVERIFY(QDir().mkpath(hidden + QStringLiteral("/src")));
        QVERIFY(QDir().mkpath(shown + QStringLiteral("/src")));
        QVERIFY(QDir().mkpath(legacy + QStringLiteral("/src")));
        QVERIFY(makeHiddenBoard(hidden));
        QVERIFY(makeBoard(shown));
        // A board that was already at issues/board.yaml keeps working exactly as it is.
        QVERIFY(makeLegacyBoard(legacy));
        QCOMPARE(candidateFor(hidden + QStringLiteral("/src")), normalize(hidden));
        QCOMPARE(candidateFor(shown + QStringLiteral("/src")), normalize(shown));
        QCOMPARE(candidateFor(legacy + QStringLiteral("/src")), normalize(legacy));

        // An empty folder is not a board: only the marker file makes one.
        const QString bare = root.path() + QStringLiteral("/bare");
        QVERIFY(QDir().mkpath(bare + QStringLiteral("/.switchboard")));
        QCOMPARE(candidateFor(bare), QString());
    }

    void candidateBoardBeatsNearerGit()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString outer = root.path() + QStringLiteral("/outer");
        const QString inner = outer + QStringLiteral("/vendor/inner");
        QVERIFY(QDir().mkpath(inner));
        QVERIFY(makeBoard(outer));       // the board is two levels up …
        QVERIFY(makeGitDir(inner));      // … and the checkout is right here
        QCOMPARE(candidateFor(inner + QStringLiteral("/src")), normalize(outer));
        QCOMPARE(candidateFor(inner), normalize(outer));
    }

    void candidateNearestBoardWinsAcrossNames()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString outer = root.path() + QStringLiteral("/outer");
        const QString inner = outer + QStringLiteral("/packages/inner");
        QVERIFY(QDir().mkpath(inner));
        // The two folder names are one rule, not two passes: the nearest ancestor holding either
        // marker wins, whichever name it uses.
        QVERIFY(makeBoard(outer));
        QVERIFY(makeLegacyBoard(inner));
        QCOMPARE(candidateFor(inner + QStringLiteral("/src/deep")), normalize(inner));
        QCOMPARE(candidateFor(outer + QStringLiteral("/docs")), normalize(outer));

        QTemporaryDir other;
        QVERIFY(other.isValid());
        const QString legacyOuter = other.path() + QStringLiteral("/outer");
        const QString freshInner = legacyOuter + QStringLiteral("/packages/inner");
        QVERIFY(QDir().mkpath(freshInner));
        QVERIFY(makeLegacyBoard(legacyOuter));
        QVERIFY(makeBoard(freshInner));
        QCOMPARE(candidateFor(freshInner + QStringLiteral("/src")), normalize(freshInner));

        QTemporaryDir third;
        QVERIFY(third.isValid());
        const QString shownOuter = third.path() + QStringLiteral("/outer");
        const QString hiddenInner = shownOuter + QStringLiteral("/packages/inner");
        QVERIFY(QDir().mkpath(hiddenInner));
        QVERIFY(makeBoard(shownOuter));
        QVERIFY(makeHiddenBoard(hiddenInner));
        QCOMPARE(candidateFor(hiddenInner + QStringLiteral("/src")), normalize(hiddenInner));
    }

    void candidateNearestGitWins()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString outer = root.path() + QStringLiteral("/outer");
        const QString inner = outer + QStringLiteral("/sub/inner");
        QVERIFY(QDir().mkpath(inner + QStringLiteral("/src")));
        QVERIFY(makeGitDir(outer));
        QVERIFY(makeGitDir(inner));
        QCOMPARE(candidateFor(inner + QStringLiteral("/src")), normalize(inner));
    }

    void candidateGitAsAFile()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        // A linked worktree and a submodule have `.git` as a file pointing at the real gitdir;
        // both are projects, so the walk must not ask for a directory.
        const QString worktree = root.path() + QStringLiteral("/worktree");
        QVERIFY(QDir().mkpath(worktree + QStringLiteral("/src")));
        QVERIFY(makeGitFile(worktree));
        QCOMPARE(candidateFor(worktree + QStringLiteral("/src")), normalize(worktree));
    }

    void candidateProjectItselfCounts()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString project = root.path() + QStringLiteral("/project");
        QVERIFY(QDir().mkpath(project));
        QVERIFY(makeBoard(project));
        QCOMPARE(candidateFor(project), normalize(project));
    }

    void candidateIgnoresTheProcessDirectory()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString project = root.path() + QStringLiteral("/launched-from");
        const QString elsewhere = root.path() + QStringLiteral("/elsewhere/deep");
        QVERIFY(QDir().mkpath(elsewhere));
        QVERIFY(makeBoard(project));

        const QString previous = QDir::currentPath();
        QVERIFY(QDir::setCurrent(project));
        // This is card #JN7X's bug in one line: standing in a project must not give every other
        // directory that project's board.
        const QString found = candidateFor(elsewhere);
        QVERIFY(QDir::setCurrent(previous));
        QCOMPARE(found, QString());
    }

    // ----- boardDirOf ---------------------------------------------------------------------------

    void theBoardFoldersAreOneOrderedList()
    {
        // The order both languages walk. `BOARD_FOLDERS` in backend/relay_core/board.py is read
        // here rather than repeated, so the two cannot drift.
        QCOMPARE(boardFolders(), (QStringList{QStringLiteral(".board"), QStringLiteral("board"), QStringLiteral(".switchboard"),
                                              QStringLiteral("switchboard"), QStringLiteral("issues")}));
        QFile python(QStringLiteral(RELAY_SOURCE_DIR "/backend/relay_core/board.py"));
        QVERIFY(python.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString text = QString::fromUtf8(python.readAll());
        const QRegularExpressionMatch tuple =
            QRegularExpression(QStringLiteral("^BOARD_FOLDERS = \\(([^)]*)\\)$"),
                               QRegularExpression::MultilineOption)
                .match(text);
        QVERIFY2(tuple.hasMatch(), "backend/relay_core/board.py has no BOARD_FOLDERS tuple");
        const QStringList names = tuple.captured(1).split(QLatin1Char(','), Qt::SkipEmptyParts);
        QCOMPARE(names.size(), boardFolders().size());
        for (int i = 0; i < names.size(); ++i) {
            // Each name in the tuple, resolved to the string it is assigned, in tuple order.
            const QRegularExpressionMatch assigned =
                QRegularExpression(QStringLiteral("^%1 = \"([^\"]*)\"$").arg(names.at(i).trimmed()),
                                   QRegularExpression::MultilineOption)
                    .match(text);
            QVERIFY2(assigned.hasMatch(), qPrintable(names.at(i)));
            QCOMPARE(assigned.captured(1), boardFolders().at(i));
        }

        // `board/` is the folder a board is created in, and nothing changes that: the "Hidden
        // Switchboard folder" option went with the owner's decision of 2026-09-21 (#1CXD). It
        // never changed what is *read*, and the three older spellings are read for ever.
        QCOMPARE(newBoardFolder(), QStringLiteral(".board"));
        QCOMPARE(boardFolders().first(), newBoardFolder());
    }

    void boardDirOfWalksTheFoldersInOrder()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());

        const QString made = root.path() + QStringLiteral("/made");
        QVERIFY(makeNewBoard(made));
        QCOMPARE(boardDirOf(made), made + QStringLiteral("/.board"));
        const QString previous = root.path() + QStringLiteral("/previous");
        QVERIFY(makePreviousBoard(previous));
        QCOMPARE(boardDirOf(previous), previous + QStringLiteral("/board"));

        const QString hidden = root.path() + QStringLiteral("/hidden");
        QVERIFY(makeHiddenBoard(hidden));
        QCOMPARE(boardDirOf(hidden), hidden + QStringLiteral("/.switchboard"));

        const QString fresh = root.path() + QStringLiteral("/fresh");
        QVERIFY(makeBoard(fresh));
        QCOMPARE(boardDirOf(fresh), fresh + QStringLiteral("/switchboard"));

        const QString legacy = root.path() + QStringLiteral("/legacy");
        QVERIFY(makeLegacyBoard(legacy));
        QCOMPARE(boardDirOf(legacy), legacy + QStringLiteral("/issues"));

        // Several present: the first of `boardFolders()` wins and the others are left where they
        // are rather than merged.
        const QString both = root.path() + QStringLiteral("/both");
        QVERIFY(makeBoard(both));
        QVERIFY(makeLegacyBoard(both));
        QCOMPARE(boardDirOf(both), both + QStringLiteral("/switchboard"));
        QVERIFY(makeHiddenBoard(both));
        QCOMPARE(boardDirOf(both), both + QStringLiteral("/.switchboard"));
        QVERIFY(makeNewBoard(both));
        QCOMPARE(boardDirOf(both), both + QStringLiteral("/.board"));

        // A folder with no marker in it is not a board.
        const QString bare = root.path() + QStringLiteral("/bare");
        QVERIFY(QDir().mkpath(bare + QStringLiteral("/issues")));
        QVERIFY(QDir().mkpath(bare + QStringLiteral("/board")));
        QVERIFY(QDir().mkpath(bare + QStringLiteral("/.switchboard")));
        QCOMPARE(boardDirOf(bare), QString());
        QCOMPARE(boardDirOf(root.path() + QStringLiteral("/nothing")), QString());
        QCOMPARE(boardDirOf(QString()), QString());
        // A trailing slash is not a different project.
        QCOMPARE(boardDirOf(hidden + QLatin1Char('/')), hidden + QStringLiteral("/.switchboard"));
    }

    // ----- keyFor -------------------------------------------------------------------------------

    void keyMatchesTheBackendDigest()
    {
        QCOMPARE(keyFor(kPinnedPath), kPinnedKey);
        QCOMPARE(keyFor(kPinnedPath).size(), 16);
        // The same canonicalisation Python does: a trailing slash and a `.` are not a new project.
        QCOMPARE(keyFor(kPinnedPath + QLatin1Char('/')), kPinnedKey);
        QCOMPARE(keyFor(QStringLiteral("/nonexistent/./relay-projects-key-test")), kPinnedKey);
        QCOMPARE(keyFor(QStringLiteral("/nonexistent/x/../relay-projects-key-test")), kPinnedKey);
        QCOMPARE(keyFor(QString()), QString());
        QCOMPARE(digest(QString()), QString());
        // Pure and impure agree on an absolute path with no symlinks above it.
        QCOMPARE(digest(kPinnedPath), kPinnedKey);
    }

    void keyFollowsSymlinks()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString real = root.path() + QStringLiteral("/real");
        const QString link = root.path() + QStringLiteral("/link");
        QVERIFY(QDir().mkpath(real));
        QVERIFY(QFile::link(real, link));
        // A symlinked workspace has to find its own conversations, which is why normalize()
        // canonicalises whatever exists.
        QCOMPARE(keyFor(link), keyFor(real));
        QCOMPARE(normalize(link), normalize(real));
    }

    void nameOfAProject()
    {
        QCOMPARE(nameFor(QStringLiteral("/srv/alpha")), QStringLiteral("alpha"));
        QCOMPARE(nameFor(QStringLiteral("/srv/alpha/")), QStringLiteral("alpha"));
        QCOMPARE(nameFor(QStringLiteral("/")), QStringLiteral("/"));
        QCOMPARE(nameFor(QString()), QString());
    }

    void reasonsAreAClosedSet()
    {
        QCOMPARE(reasons().size(), 11);
        for (const QString &reason : reasons()) QVERIFY(isReason(reason));
        QVERIFY(isReason(QStringLiteral("switchboard")));
        QVERIFY(isReason(QStringLiteral("init-command")));
        QVERIFY(isReason(QStringLiteral("agent-card")));
        // The first prompt sent to the agent in a project with no board is trigger (1) of the
        // init question: a yes attaches the tab with this reason (src/ProjectInit.h).
        QVERIFY(isReason(QStringLiteral("agent-work")));
        QVERIFY(!isReason(QStringLiteral("because I felt like it")));
        QVERIFY(!isReason(QString()));
        QVERIFY(!isReason(QStringLiteral("Switchboard")));
    }

    // ----- chooseBoard --------------------------------------------------------------------------

    void chooseBoardTable()
    {
        const QString project = QStringLiteral("/srv/alpha");
        const QString key = keyFor(project);
        QVERIFY(!key.isEmpty());

        // No project at all: no board, and nothing to ask about.
        const Board none = chooseBoard(QString(), QString(), nullptr);
        QCOMPARE(none.kind, Board::None);
        QVERIFY(none.dir.isEmpty());
        QVERIFY(none.project.isEmpty());
        QVERIFY(none.key.isEmpty());
        QVERIFY(!none.needsConsent);
        QVERIFY(!none.isValid());
        QVERIFY(!none.isWritable());

        // A relative path can never be a board's home.
        QCOMPARE(chooseBoard(QStringLiteral("alpha"), QStringLiteral("alpha/.switchboard"), nullptr).kind,
                 Board::None);

        // A board that is already there is used exactly where it is.
        const Board fresh = chooseBoard(project, QStringLiteral("/srv/alpha/.switchboard"), nullptr);
        QCOMPARE(fresh.kind, Board::InRepo);
        QCOMPARE(fresh.dir, QStringLiteral("/srv/alpha/.switchboard"));
        QCOMPARE(fresh.project, project);
        QCOMPARE(fresh.key, key);
        QVERIFY(!fresh.needsConsent);
        QVERIFY(fresh.isValid());
        QVERIFY(fresh.isWritable());

        // Including one that predates any of the renames: Relay never moves it.
        for (const QString &dir : {QStringLiteral("/srv/alpha/switchboard"),
                                   QStringLiteral("/srv/alpha/issues")}) {
            const Board older = chooseBoard(project, dir, nullptr);
            QCOMPARE(older.kind, Board::InRepo);
            QCOMPARE(older.dir, dir);
            QVERIFY(older.isWritable());
        }

        // No board yet: `dir` is where one *would* go, and nothing may be written until the user
        // has answered the one-time question.
        // `board/` is the default, and `newFolder` is how a caller with another answer — an older
        // board being pointed at, say — reaches a pure function.
        const Board uninitialized = chooseBoard(project, QString(), nullptr);
        QCOMPARE(uninitialized.kind, Board::Uninitialized);
        QCOMPARE(uninitialized.dir, QStringLiteral("/srv/alpha/.board"));
        QCOMPARE(chooseBoard(project, QString(), nullptr, QStringLiteral("switchboard")).dir,
                 QStringLiteral("/srv/alpha/switchboard"));
        QCOMPARE(chooseBoard(project, QString(), nullptr, newBoardFolder()).dir,
                 uninitialized.dir);
        QCOMPARE(uninitialized.project, project);
        QCOMPARE(uninitialized.key, key);
        QVERIFY(uninitialized.needsConsent);
        QVERIFY(uninitialized.isValid());
        QVERIFY(!uninitialized.isWritable());

        // A record cannot conjure a board that is not on disk: the probe decides, not the registry.
        Record known;
        known.path = project;
        known.key = key;
        known.board = QString::fromLatin1(kBoardRepo);
        known.boardDir = QStringLiteral("/srv/alpha/.switchboard");
        QCOMPARE(chooseBoard(project, QString(), &known).kind, Board::Uninitialized);
        QVERIFY(chooseBoard(project, QString(), &known).needsConsent);

        // A record's key is used as given, so a project that moved keeps its conversations.
        Record moved = known;
        moved.key = QStringLiteral("0123456789abcdef");
        QCOMPARE(chooseBoard(project, QString(), &moved).key, QStringLiteral("0123456789abcdef"));

        // A trailing slash on either side is not a different board.
        QCOMPARE(chooseBoard(project + QLatin1Char('/'), QStringLiteral("/srv/alpha/.switchboard/"),
                             nullptr)
                     .dir,
                 QStringLiteral("/srv/alpha/.switchboard"));
        QCOMPARE(chooseBoard(project + QLatin1Char('/'), QString(), nullptr).dir, uninitialized.dir);
    }

    void theKindsAreTheThreeAProjectCanBeIn()
    {
        // There is no fourth kind: the personal inbox was dropped on 2026-09-19 (card #916B). Every
        // board is in a project, and a card filed with no project goes to `defaultProject()` or to
        // the project the user picks.
        QCOMPARE(boardKindName(Board::InRepo), QString::fromLatin1(kBoardRepo));
        QCOMPARE(boardKindName(Board::Uninitialized), QString::fromLatin1(kBoardNone));
        QCOMPARE(boardKindName(Board::None), QString::fromLatin1(kBoardNone));
    }

    // ----- the option and the default project ---------------------------------------------------

    // A new board is `board/`, and no setting changes that (owner, 2026-09-21, #1CXD): the old
    // `board/hidden_folder` key is dead, so a profile that still carries it — every profile that
    // ever turned it off — gets `board/` like everyone else, with nothing to migrate.
    void aNewBoardIsAlwaysTheBoardFolder()
    {
        QCOMPARE(newBoardFolder(), QStringLiteral(".board"));
        QSettings().setValue(QStringLiteral("board/hidden_folder"), false);
        QCOMPARE(newBoardFolder(), QStringLiteral(".board"));
        QSettings().setValue(QStringLiteral("board/hidden_folder"), true);
        QCOMPARE(newBoardFolder(), QStringLiteral(".board"));
        QSettings().remove(QStringLiteral("board/hidden_folder"));
        QCOMPARE(newBoardFolder(), QStringLiteral(".board"));
    }

    void theDefaultProjectIsEmptyUntilTheUserChoosesOne()
    {
        QSettings().remove(QLatin1String(kDefaultProjectSetting));
        QCOMPARE(defaultProject(), QString());

        QTemporaryDir root;
        QVERIFY(root.isValid());
        QSettings().setValue(QLatin1String(kDefaultProjectSetting), root.path());
        QCOMPARE(defaultProject(), normalize(root.path()));

        // A project that has been moved or deleted reads as unset, so the picker asks again rather
        // than a card being written somewhere that is no longer there.
        QSettings().setValue(QLatin1String(kDefaultProjectSetting),
                             root.path() + QStringLiteral("/gone"));
        QCOMPARE(defaultProject(), QString());
        // Whitespace is not a project either.
        QSettings().setValue(QLatin1String(kDefaultProjectSetting), QStringLiteral("   "));
        QCOMPARE(defaultProject(), QString());
        QSettings().remove(QLatin1String(kDefaultProjectSetting));
    }

    void boardForProbesTheFilesystem()
    {
        DataHome home;
        QVERIFY(home.valid());
        QTemporaryDir root;
        QVERIFY(root.isValid());

        const QString fresh = root.path() + QStringLiteral("/withboard");
        QVERIFY(makeHiddenBoard(fresh));
        const Board found = boardFor(fresh);
        QCOMPARE(found.kind, Board::InRepo);
        QCOMPARE(found.dir, normalize(fresh) + QStringLiteral("/.switchboard"));
        QVERIFY(found.isWritable());

        const QString shown = root.path() + QStringLiteral("/shown");
        QVERIFY(makeBoard(shown));
        QCOMPARE(boardFor(shown).dir, normalize(shown) + QStringLiteral("/switchboard"));

        const QString legacy = root.path() + QStringLiteral("/legacy");
        QVERIFY(makeLegacyBoard(legacy));
        QCOMPARE(boardFor(legacy).dir, normalize(legacy) + QStringLiteral("/issues"));

        const QString plain = root.path() + QStringLiteral("/plain");
        QVERIFY(QDir().mkpath(plain));
        const Board waiting = boardFor(plain);
        QCOMPARE(waiting.kind, Board::Uninitialized);
        // Where a board *would* go: `board/`, the folder a new board gets.
        QCOMPARE(waiting.dir, normalize(plain) + QLatin1Char('/') + newBoardFolder());
        QVERIFY(waiting.needsConsent);

        // Declining does not change where a board would go; it only says whether Relay may ask.
        Registry registry(root.path() + QStringLiteral("/projects.json"));
        QVERIFY(registry.load());
        QVERIFY(registry.decline(plain, 1000));
        QCOMPARE(boardFor(plain, &registry).kind, Board::Uninitialized);
        QVERIFY(registry.isDeclined(plain));

        QCOMPARE(boardFor(QString()).kind, Board::None);
    }

    // ----- the registry -------------------------------------------------------------------------

    void registryRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.path() + QStringLiteral("/state/projects.json");

        Registry registry(file);
        QVERIFY(registry.load());   // a missing file is an empty registry, not a failure
        QCOMPARE(registry.count(), 0);
        QVERIFY(registry.knownProjects().isEmpty());

        QString error;
        QVERIFY2(registry.remember(QStringLiteral("/srv/alpha"), QStringLiteral("switchboard"),
                                   QStringLiteral("repo"), QStringLiteral("/srv/alpha/switchboard"),
                                   1000, &error),
                 qPrintable(error));
        QVERIFY(QFile::exists(file));

        Registry reopened(file);
        QVERIFY(reopened.load(&error));
        QVERIFY(error.isEmpty());
        QCOMPARE(reopened.count(), 1);
        const Record record = reopened.record(QStringLiteral("/srv/alpha"));
        QVERIFY(record.isValid());
        QCOMPARE(record.path, QStringLiteral("/srv/alpha"));
        QCOMPARE(record.key, keyFor(QStringLiteral("/srv/alpha")));
        QCOMPARE(record.name, QStringLiteral("alpha"));
        QCOMPARE(record.board, QStringLiteral("repo"));
        QCOMPARE(record.boardDir, QStringLiteral("/srv/alpha/switchboard"));
        QCOMPARE(record.reason, QStringLiteral("switchboard"));
        QCOMPARE(record.knownSince, qint64(1000));
        QCOMPARE(record.lastAttached, qint64(1000));
        QVERIFY(reopened.isKnown(QStringLiteral("/srv/alpha")));
        QVERIFY(!reopened.isKnown(QStringLiteral("/srv/beta")));
        QVERIFY(!reopened.record(QStringLiteral("/srv/beta")).isValid());

        // The file is version 1 with both lists, so a reader knows what it is looking at.
        QFile handle(file);
        QVERIFY(handle.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(handle.readAll()).object();
        QCOMPARE(root.value(QStringLiteral("version")).toInt(), kSchemaVersion);
        QCOMPARE(root.value(QStringLiteral("projects")).toArray().size(), 1);
        QVERIFY(root.value(QStringLiteral("declined")).isArray());
    }

    void rememberKeepsTheFirstReason()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Registry registry(dir.path() + QStringLiteral("/projects.json"));
        QVERIFY(registry.load());

        QVERIFY(registry.remember(QStringLiteral("/srv/alpha"), QString::fromLatin1(kReasonSwitchboard),
                                  QString::fromLatin1(kBoardNone), QString(), 1000));
        QVERIFY(registry.remember(QStringLiteral("/srv/alpha"), QString::fromLatin1(kReasonAgentWrite),
                                  QString(), QString(), 2000));
        QCOMPARE(registry.count(), 1);
        Record record = registry.record(QStringLiteral("/srv/alpha"));
        // Why Relay first noticed the project is history; only the last touch moves.
        QCOMPARE(record.reason, QString::fromLatin1(kReasonSwitchboard));
        QCOMPARE(record.knownSince, qint64(1000));
        QCOMPARE(record.lastAttached, qint64(2000));
        // An empty board/boardDir leaves what was stored alone.
        QCOMPARE(record.board, QString::fromLatin1(kBoardNone));

        // `/init` created the folder: the record follows, the reason does not.
        QVERIFY(registry.remember(QStringLiteral("/srv/alpha"), QString::fromLatin1(kReasonInitCommand),
                                  QString::fromLatin1(kBoardRepo), QStringLiteral("/srv/alpha/switchboard"),
                                  3000));
        record = registry.record(QStringLiteral("/srv/alpha"));
        QCOMPARE(record.board, QString::fromLatin1(kBoardRepo));
        QCOMPARE(record.boardDir, QStringLiteral("/srv/alpha/switchboard"));
        QCOMPARE(record.reason, QString::fromLatin1(kReasonSwitchboard));

        // The closed set is enforced, and a refused reason changes nothing.
        QString error;
        QVERIFY(!registry.remember(QStringLiteral("/srv/beta"), QStringLiteral("felt-like-it"),
                                   QString(), QString(), 4000, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(registry.count(), 1);
        QVERIFY(!registry.remember(QString(), QString::fromLatin1(kReasonPicker)));
    }

    void knownProjectsAreOrderedByLastAttached()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Registry registry(dir.path() + QStringLiteral("/projects.json"));
        QVERIFY(registry.load());

        QVERIFY(registry.remember(QStringLiteral("/srv/alpha"), QString::fromLatin1(kReasonPicker),
                                  QString(), QString(), 1000));
        QVERIFY(registry.remember(QStringLiteral("/srv/beta"), QString::fromLatin1(kReasonPicker),
                                  QString(), QString(), 3000));
        QVERIFY(registry.remember(QStringLiteral("/srv/gamma"), QString::fromLatin1(kReasonPicker),
                                  QString(), QString(), 2000));
        auto paths = [](const QList<Record> &records) {
            QStringList out;
            for (const Record &record : records) out << record.path;
            return out;
        };
        QCOMPARE(paths(registry.knownProjects()),
                 (QStringList{QStringLiteral("/srv/beta"), QStringLiteral("/srv/gamma"),
                              QStringLiteral("/srv/alpha")}));

        // Touching the oldest puts it first.
        QVERIFY(registry.remember(QStringLiteral("/srv/alpha"), QString::fromLatin1(kReasonCardCommand),
                                  QString(), QString(), 4000));
        QCOMPARE(paths(registry.knownProjects()).first(), QStringLiteral("/srv/alpha"));

        // Two projects attached in the same second still have one stable order.
        QVERIFY(registry.remember(QStringLiteral("/srv/delta"), QString::fromLatin1(kReasonAgentCard),
                                  QString(), QString(), 4000));
        QCOMPARE(paths(registry.knownProjects()).mid(0, 2),
                 (QStringList{QStringLiteral("/srv/alpha"), QStringLiteral("/srv/delta")}));

        // The order survives a reload, which is what the list in the picker reads.
        Registry reopened(dir.path() + QStringLiteral("/projects.json"));
        QVERIFY(reopened.load());
        QCOMPARE(paths(reopened.knownProjects()), paths(registry.knownProjects()));
    }

    void forgetRemovesOnlyTheRecord()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.path() + QStringLiteral("/projects.json");
        Registry registry(file);
        QVERIFY(registry.load());
        QVERIFY(registry.remember(QStringLiteral("/srv/alpha"), QString::fromLatin1(kReasonPicker),
                                  QString(), QString(), 1000));
        QVERIFY(registry.remember(QStringLiteral("/srv/beta"), QString::fromLatin1(kReasonPicker),
                                  QString(), QString(), 1000));

        QVERIFY(registry.forget(QStringLiteral("/srv/alpha")));
        QCOMPARE(registry.count(), 1);
        QVERIFY(!registry.isKnown(QStringLiteral("/srv/alpha")));
        QVERIFY(registry.isKnown(QStringLiteral("/srv/beta")));
        // Forgetting what was never known is not an error.
        QVERIFY(registry.forget(QStringLiteral("/srv/nothing")));

        Registry reopened(file);
        QVERIFY(reopened.load());
        QCOMPARE(reopened.count(), 1);
    }

    void aBoardFolderIsKnownWithoutARecord()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString fresh = dir.path() + QStringLiteral("/withboard");
        const QString legacy = dir.path() + QStringLiteral("/legacy");
        QVERIFY(makeBoard(fresh));
        QVERIFY(makeLegacyBoard(legacy));
        Registry registry(dir.path() + QStringLiteral("/projects.json"));
        QVERIFY(registry.load());
        // An initialised project says "this is a project" by itself; asking the user to set up what
        // they already set up would be noise.
        QVERIFY(registry.isKnown(fresh));
        QVERIFY(registry.isKnown(legacy));
        QCOMPARE(registry.count(), 0);
        QVERIFY(!registry.record(fresh).isValid());
        QVERIFY(!registry.isKnown(dir.path() + QStringLiteral("/plain")));
        QVERIFY(!registry.isKnown(QString()));
    }

    void decliningRemembersTheNo()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.path() + QStringLiteral("/projects.json");
        Registry registry(file);
        QVERIFY(registry.load());

        // "No, do not make a Switchboard here" is remembered so the question is asked once.
        QVERIFY(registry.decline(QStringLiteral("/srv/notes"), 1000));
        QVERIFY(registry.isDeclined(QStringLiteral("/srv/notes")));
        QVERIFY(!registry.isDeclined(QStringLiteral("/srv/alpha")));
        QVERIFY(!registry.isDeclined(QString()));
        QVERIFY(!registry.decline(QString()));

        QVERIFY(registry.decline(QStringLiteral("/srv/scratch"), 2000));
        QCOMPARE(registry.declined(),
                 (QStringList{QStringLiteral("/srv/scratch"), QStringLiteral("/srv/notes")}));

        // Declining a known project removes its record: the two lists never hold the same path.
        QVERIFY(registry.remember(QStringLiteral("/srv/alpha"), QString::fromLatin1(kReasonPicker),
                                  QString(), QString(), 1500));
        QVERIFY(registry.decline(QStringLiteral("/srv/alpha"), 3000));
        QVERIFY(!registry.isKnown(QStringLiteral("/srv/alpha")));
        QVERIFY(registry.isDeclined(QStringLiteral("/srv/alpha")));

        // An explicit `/init` is the user changing their mind, and it clears the refusal.
        QVERIFY(registry.remember(QStringLiteral("/srv/alpha"), QString::fromLatin1(kReasonInitCommand),
                                  QString::fromLatin1(kBoardRepo), QStringLiteral("/srv/alpha/switchboard"),
                                  4000));
        QVERIFY(!registry.isDeclined(QStringLiteral("/srv/alpha")));
        QVERIFY(registry.isKnown(QStringLiteral("/srv/alpha")));

        QVERIFY(registry.undecline(QStringLiteral("/srv/notes")));
        QVERIFY(!registry.isDeclined(QStringLiteral("/srv/notes")));
        QVERIFY(registry.undecline(QStringLiteral("/srv/never-declined")));

        Registry reopened(file);
        QVERIFY(reopened.load());
        QCOMPARE(reopened.declined(), (QStringList{QStringLiteral("/srv/scratch")}));
        QVERIFY(reopened.isKnown(QStringLiteral("/srv/alpha")));
    }

    void aCorruptFileIsKeptAsideNotLost()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.path() + QStringLiteral("/projects.json");
        const QByteArray damaged = "{\"version\": 1, \"projects\": [ truncated";
        QFile handle(file);
        QVERIFY(handle.open(QIODevice::WriteOnly));
        handle.write(damaged);
        handle.close();

        Registry registry(file);
        QString error;
        QVERIFY(!registry.load(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(registry.count(), 0);
        // The unreadable bytes are beside the file, so the next write does not take them with it.
        QVERIFY(QFile::exists(registry.corruptPath()));
        QFile aside(registry.corruptPath());
        QVERIFY(aside.open(QIODevice::ReadOnly));
        QCOMPARE(aside.readAll(), damaged);
        aside.close();

        // The registry is still usable, and the next save replaces the damaged file.
        QVERIFY(registry.remember(QStringLiteral("/srv/alpha"), QString::fromLatin1(kReasonPicker),
                                  QString(), QString(), 1000, &error));
        Registry reopened(file);
        QVERIFY(reopened.load(&error));
        QVERIFY(error.isEmpty());
        QCOMPARE(reopened.count(), 1);
    }

    void aForeignVersionIsNotGuessedAt()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.path() + QStringLiteral("/projects.json");
        QFile handle(file);
        QVERIFY(handle.open(QIODevice::WriteOnly));
        handle.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), kSchemaVersion + 1},
                                               {QStringLiteral("projects"), QJsonArray{}}})
                         .toJson());
        handle.close();

        Registry registry(file);
        QString error;
        QVERIFY(!registry.load(&error));
        QVERIFY(error.contains(QString::number(kSchemaVersion + 1)));
        QVERIFY(QFile::exists(registry.corruptPath()));

        // A JSON array, and an object with no project list, are refused the same way.
        for (const QByteArray &body : {QByteArray("[]"), QByteArray("{\"version\": 1}")}) {
            QFile again(file);
            QVERIFY(again.open(QIODevice::WriteOnly));
            again.write(body);
            again.close();
            Registry other(file);
            QVERIFY(!other.load(&error));
            QVERIFY(!error.isEmpty());
        }
    }

    void theFileIsPrivate()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.path() + QStringLiteral("/state/projects.json");
        Registry registry(file);
        QVERIFY(registry.load());
        QVERIFY(registry.remember(QStringLiteral("/srv/alpha"), QString::fromLatin1(kReasonPicker),
                                  QString(), QString(), 1000));
        // Which projects a user works on is theirs; the file is 0600 like windows.json.
        QCOMPARE(QFile::permissions(file)
                     & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther),
                 QFileDevice::Permissions());
        QVERIFY(QFile::permissions(file).testFlag(QFile::ReadOwner));
        QVERIFY(QFile::permissions(file).testFlag(QFile::WriteOwner));
        QVERIFY(QFileInfo(file).absoluteDir().exists());
        // The state directory it made is the user's own too.
        const QFile::Permissions dirMode = QFileInfo(dir.path() + QStringLiteral("/state")).permissions();
        QCOMPARE(dirMode & (QFile::ReadGroup | QFile::ReadOther), QFileDevice::Permissions());
    }

    void setPathRepointsTheRegistry()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString first = dir.path() + QStringLiteral("/one.json");
        const QString second = dir.path() + QStringLiteral("/two.json");
        Registry registry(first);
        QVERIFY(registry.load());
        QVERIFY(registry.remember(QStringLiteral("/srv/alpha"), QString::fromLatin1(kReasonPicker),
                                  QString(), QString(), 1000));

        registry.setPath(second);
        QCOMPARE(registry.path(), second);
        QCOMPARE(registry.count(), 0);   // repointing drops what was in memory
        QVERIFY(registry.load());
        QCOMPARE(registry.count(), 0);

        registry.setPath(first);
        QVERIFY(registry.load());
        QCOMPARE(registry.count(), 1);
    }

    void theDefaultPathFollowsXdgDataHome()
    {
        DataHome home;
        QVERIFY(home.valid());
        QCOMPARE(defaultPath(), home.path() + QStringLiteral("/relay/state/projects.json"));
        QCOMPARE(stateDirectory(), home.path() + QStringLiteral("/relay/state"));
        // An empty constructor argument means the default, which is what the application uses.
        QCOMPARE(Registry().path(), defaultPath());
    }

private:
    // Where this test's QSettings live, so the developer's own options are never read or written.
    QTemporaryDir m_settings;
};

QTEST_MAIN(ProjectsTest)
#include "projects_test.moc"
