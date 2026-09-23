// SPDX-License-Identifier: AGPL-3.0-or-later
// Saved window layout ("reopen where I left off"): the parts that do not need a window —
// reading and writing state/windows.json, validating pane trees, clamping geometry onto a
// screen that still exists, the cwd/workspace/$HOME fallback, and the per-pane scrollback store
// a restored pane refills itself from.
#include "WindowState.h"
#include "PromptDraft.h"
#include "core/AnsiSerializer.h"
#include "core/InlineImage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace relay::windowstate;
// The conversation's own store (card #0TJ9) has read()/write() of its own, so it stays qualified.
namespace st = relay::sessiontext;

namespace {

QJsonObject pane(const QString &cwd, const QString &engine = QStringLiteral("relay")) {
    return {{"pane", QJsonObject{{"cwd", cwd}, {"workspace", cwd}, {"engine", engine},
                                 {"engine_core", "ghostty"}, {"agent_role", "main"},
                                 {"session_id", "abc123"}}}};
}

QJsonObject split(const QString &direction, const QJsonArray &children) {
    return {{"split", direction}, {"children", children}, {"sizes", QJsonArray{1000, 1000}}};
}

// The scrollback store names its own path under $XDG_DATA_HOME, so a test that touches it points
// that at a temporary directory and puts the old value back afterwards.
class DataHome {
public:
    DataHome() {
        m_previous = qgetenv("XDG_DATA_HOME");
        m_had = qEnvironmentVariableIsSet("XDG_DATA_HOME");
        if (m_dir.isValid()) qputenv("XDG_DATA_HOME", m_dir.path().toLocal8Bit());
    }
    ~DataHome() {
        if (m_had) qputenv("XDG_DATA_HOME", m_previous);
        else qunsetenv("XDG_DATA_HOME");
    }
    DataHome(const DataHome &) = delete;
    DataHome &operator=(const DataHome &) = delete;
    bool valid() const { return m_dir.isValid(); }

private:
    QTemporaryDir m_dir;
    QByteArray m_previous;
    bool m_had = false;
};

}  // namespace

class WindowStateTest : public QObject {
    Q_OBJECT

private slots:
    void promptDraftsArePrivateAndIndependent() {
        DataHome home;
        QVERIFY(home.valid());
        const QString first = QStringLiteral("pane/first");
        const QString second = QStringLiteral("console/tab/card:ABCD");
        const QString draft = QString::fromUtf8("first line\nsecond line π");
        QVERIFY(relay::promptdraft::write(first, draft));
        QVERIFY(relay::promptdraft::write(second, QStringLiteral("another card")));
        QCOMPARE(relay::promptdraft::read(first), draft);
        QCOMPARE(relay::promptdraft::read(second), QStringLiteral("another card"));
        const QFileInfo stored(relay::promptdraft::pathFor(first));
        QVERIFY(stored.exists());
        QCOMPARE(stored.permissions() & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther),
                 QFile::Permissions());
        QVERIFY(relay::promptdraft::write(first, QString()));
        QCOMPARE(relay::promptdraft::read(first), QString());
        QCOMPARE(relay::promptdraft::read(second), QStringLiteral("another card"));
        QVERIFY(relay::promptdraft::clearAll());
        QCOMPARE(relay::promptdraft::read(second), QString());
    }

    void documentRoundTrip() {
        const QJsonArray tabs{pane(QStringLiteral("/tmp")), pane(QStringLiteral("/usr"))};
        const QJsonObject window = windowRecord(QRect(10, 20, 800, 600), QStringLiteral("DP-1"), tabs, 1,
                                                {QStringLiteral("tmp"), QStringLiteral("usr")});
        const QJsonObject state = document(QJsonArray{window});
        QCOMPARE(state.value(QStringLiteral("version")).toInt(), kSchemaVersion);
        QVERIFY(state.value(QStringLiteral("saved")).toVariant().toLongLong() > 0);
        QCOMPARE(windowsOf(state).size(), 1);
        const QJsonObject back = windowsOf(state).first().toObject();
        QCOMPARE(geometryOf(back), QRect(10, 20, 800, 600));
        QCOMPARE(screenOf(back), QStringLiteral("DP-1"));
        QCOMPARE(currentOf(back), 1);
        QCOMPARE(tabsOf(back).size(), 2);
        QCOMPARE(titlesOf(back), QStringList({QStringLiteral("tmp"), QStringLiteral("usr")}));
    }

    void invalidGeometryIsOmitted() {
        const QJsonObject window = windowRecord(QRect(), QString(), QJsonArray{pane(QStringLiteral("/tmp"))}, 0);
        QVERIFY(!window.contains(QStringLiteral("geometry")));
        QVERIFY(!window.contains(QStringLiteral("screen")));
        QCOMPARE(geometryOf(window), QRect());
    }

    // The pane node keeps every field the restore needs, including both engines (the Relay engine
    // is the process default since commit 478b8a1, so konsole panes must round-trip too).
    void paneFieldsSurviveTheFile() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("state/windows.json"));
        QJsonObject konsole = pane(QStringLiteral("/tmp"), QStringLiteral("konsole"));
        QJsonObject leaf = konsole.value(QStringLiteral("pane")).toObject();
        leaf.insert(QStringLiteral("preset"), QStringLiteral("anthropic"));
        leaf.insert(QStringLiteral("effort"), QStringLiteral("low"));
        leaf.insert(QStringLiteral("agent_mode"), QStringLiteral("plan"));
        leaf.insert(QStringLiteral("input_mode"), QStringLiteral("agent"));
        konsole.insert(QStringLiteral("pane"), leaf);
        const QJsonArray tabs{split(QStringLiteral("v"), QJsonArray{konsole, pane(QStringLiteral("/usr"))})};
        QString error;
        QVERIFY2(write(path, document(QJsonArray{windowRecord(QRect(0, 0, 900, 700), QStringLiteral("eDP-1"), tabs, 0)}), &error),
                 qPrintable(error));
        const QJsonObject read = relay::windowstate::read(path, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const QJsonObject tab = tabsOf(windowsOf(read).first().toObject()).first().toObject();
        QCOMPARE(tab.value(QStringLiteral("split")).toString(), QStringLiteral("v"));
        const QJsonObject first = tab.value(QStringLiteral("children")).toArray().first().toObject()
                                      .value(QStringLiteral("pane")).toObject();
        QCOMPARE(first.value(QStringLiteral("engine")).toString(), QStringLiteral("konsole"));
        QCOMPARE(first.value(QStringLiteral("engine_core")).toString(), QStringLiteral("ghostty"));
        QCOMPARE(first.value(QStringLiteral("preset")).toString(), QStringLiteral("anthropic"));
        QCOMPARE(first.value(QStringLiteral("effort")).toString(), QStringLiteral("low"));
        QCOMPARE(first.value(QStringLiteral("agent_mode")).toString(), QStringLiteral("plan"));
        QCOMPARE(first.value(QStringLiteral("input_mode")).toString(), QStringLiteral("agent"));
        QCOMPARE(first.value(QStringLiteral("session_id")).toString(), QStringLiteral("abc123"));
        const QJsonObject second = tab.value(QStringLiteral("children")).toArray().at(1).toObject()
                                       .value(QStringLiteral("pane")).toObject();
        QCOMPARE(second.value(QStringLiteral("engine")).toString(), QStringLiteral("relay"));
    }

    void writeIsPrivateAndCreatesItsDirectory() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("relay/state/windows.json"));
        QVERIFY(write(path, document(QJsonArray{})));
        QVERIFY(QFile::exists(path));
        QCOMPARE(QFile::permissions(path) & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther),
                 QFile::Permissions());
        QVERIFY(QFile::permissions(path).testFlag(QFile::ReadOwner));
        QVERIFY(QFile::permissions(path).testFlag(QFile::WriteOwner));
    }

    // A replacement must be atomic: the reader either sees the old file or the whole new one.
    void writeReplacesTheWholeFile() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("windows.json"));
        QVERIFY(write(path, document(QJsonArray{windowRecord(QRect(0, 0, 10, 10), QString(), QJsonArray{pane(QStringLiteral("/tmp"))}, 0)})));
        QVERIFY(write(path, document(QJsonArray{})));
        QString error;
        QCOMPARE(windowsOf(relay::windowstate::read(path, &error)).size(), 0);
        QVERIFY(error.isEmpty());
        // No leftover temp files beside it.
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden), QStringList({QStringLiteral("windows.json")}));
    }

    void missingFileIsNotAnError() {
        QString error = QStringLiteral("stale");
        const QJsonObject state = read(QStringLiteral("/nonexistent/relay/windows.json"), &error);
        QVERIFY(state.isEmpty());
        QVERIFY(error.isEmpty());
    }

    void brokenFilesAreReported_data() {
        QTest::addColumn<QByteArray>("content");
        QTest::newRow("not json") << QByteArray("{ this is not json");
        QTest::newRow("not an object") << QByteArray("[1, 2, 3]");
        QTest::newRow("no version") << QByteArray(R"({"windows": []})");
        QTest::newRow("future version") << QByteArray(R"({"version": 99, "windows": []})");
        QTest::newRow("no window list") << QByteArray(R"({"version": 1})");
    }

    void brokenFilesAreReported() {
        QFETCH(QByteArray, content);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("windows.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
        file.close();
        QString error;
        QVERIFY(read(path, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void usableNodes() {
        QVERIFY(isUsableNode(pane(QStringLiteral("/tmp"))));
        QVERIFY(isUsableNode(split(QStringLiteral("h"), QJsonArray{pane(QStringLiteral("/tmp")), pane(QStringLiteral("/usr"))})));
        QVERIFY(isUsableNode(QJsonObject{{"explorer", QJsonObject{{"path", "/tmp"}}}}));
        QVERIFY(isUsableNode(QJsonObject{{"plan", QJsonObject{{"path", "/tmp/plan.md"}}}}));
        // The Switchboard is keyed on its workspace and tab, not on a file path.
        QVERIFY(isUsableNode(QJsonObject{{"board", QJsonObject{{"workspace", "/repo"}, {"tab", "bugs"}}}}));
        QVERIFY(isUsableNode(QJsonObject{{"board", QJsonObject{{"workspace", "/repo"}}}}));
        QVERIFY(!isUsableNode(QJsonObject{{"board", QJsonObject{}}}));               // no workspace
        QVERIFY(!isUsableNode(QJsonObject{{"board", QJsonObject{{"tab", "bugs"}}}}));
        // A subagent pane (#WD83) is its tabs' text; with no tabs there is nothing to bring back.
        QVERIFY(isUsableNode(QJsonObject{{"subagents", QJsonObject{{"owner", "p1"}, {"tabs", QJsonArray{QJsonObject{{"id", "a1"}}}}}}}));
        QVERIFY(!isUsableNode(QJsonObject{{"subagents", QJsonObject{{"owner", "p1"}, {"tabs", QJsonArray{}}}}}));
        // The Options/Actions pane node (card #XAME) needs nothing but the object: an empty one
        // restores the default Options pane.
        QVERIFY(isUsableNode(QJsonObject{{"settings", QJsonObject{{"mode", "actions"}, {"search", "theme"}}}}));
        QVERIFY(isUsableNode(QJsonObject{{"settings", QJsonObject{}}}));
        QVERIFY(!isUsableNode(QJsonObject{{"settings", "options"}}));
        // The Test suites pane (card #7BM4) restores empty and asks its tab's board worker again,
        // so a saved node holds only the project it was opened on — and an empty one still comes
        // back, exactly as an Options pane does.
        QVERIFY(isUsableNode(QJsonObject{{"testsuites", QJsonObject{{"cwd", "/repo"}}}}));
        QVERIFY(isUsableNode(QJsonObject{{"testsuites", QJsonObject{}}}));
        QVERIFY(!isUsableNode(QJsonObject{{"testsuites", "/repo"}}));
        // The models pane (card #MDL1 t:a11). It is listed here for more than itself: an unknown
        // node makes its whole split unusable, so a tab holding a terminal *and* a models pane was
        // dropped entirely — the driven quit-and-reopen came back with neither.
        QVERIFY(isUsableNode(QJsonObject{{"models", QJsonObject{{"cwd", "/repo"}, {"tab", "available"}}}}));
        QVERIFY(isUsableNode(QJsonObject{{"models", QJsonObject{}}}));
        QVERIFY(!isUsableNode(QJsonObject{{"models", "available"}}));
        // The Activity pane (card #QT8C) is written by PaneChrome::serialize as `internals` and was
        // never listed here, so a terminal beside it was dropped with its tab on reopen (card #ACT1).
        QVERIFY(isUsableNode(QJsonObject{{"internals", QJsonObject{{"cwd", "/repo"}, {"owner", "abc"}}}}));
        QVERIFY(!isUsableNode(QJsonObject{{"internals", "/repo"}}));
        QVERIFY(isUsableNode(split(QStringLiteral("h"), QJsonArray{pane(QStringLiteral("/repo")),
                                                                   QJsonObject{{"internals", QJsonObject{{"cwd", "/repo"}}}}})));
        QVERIFY(isUsableNode(split(QStringLiteral("h"), QJsonArray{pane(QStringLiteral("/repo")),
                                                                   QJsonObject{{"models", QJsonObject{{"cwd", "/repo"}}}}})));
        QVERIFY(!isUsableNode(QJsonObject{}));
        QVERIFY(!isUsableNode(QJsonObject{{"subagent", QJsonObject{}}}));
        QVERIFY(!isUsableNode(QJsonObject{{"explorer", QJsonObject{}}}));   // no path
        QVERIFY(!isUsableNode(split(QStringLiteral("h"), QJsonArray{})));   // empty split
        QVERIFY(!isUsableNode(split(QStringLiteral("h"), QJsonArray{QJsonObject{}})));
    }

    // A corrupt or hand-edited file must not drive buildNode() into unbounded recursion.
    void deepNestingIsRejected() {
        QJsonObject node = pane(QStringLiteral("/tmp"));
        for (int i = 0; i < kMaxDepth + 2; ++i) node = split(QStringLiteral("h"), QJsonArray{node, pane(QStringLiteral("/tmp"))});
        QVERIFY(!isUsableNode(node));
        QJsonObject shallow = pane(QStringLiteral("/tmp"));
        for (int i = 0; i < kMaxDepth - 1; ++i) shallow = split(QStringLiteral("h"), QJsonArray{shallow, pane(QStringLiteral("/tmp"))});
        QVERIFY(isUsableNode(shallow));
    }

    void usableWindowsDropsWhatCannotBeRebuilt() {
        const QJsonObject state = document(QJsonArray{
            windowRecord(QRect(0, 0, 800, 600), QString(), QJsonArray{pane(QStringLiteral("/tmp")), QJsonObject{}}, 5),
            windowRecord(QRect(0, 0, 800, 600), QString(), QJsonArray{QJsonObject{}}, 0),
            windowRecord(QRect(0, 0, 800, 600), QString(), QJsonArray{}, 0),
        });
        const QJsonArray windows = usableWindows(state);
        QCOMPARE(windows.size(), 1);
        QCOMPARE(tabsOf(windows.first().toObject()).size(), 1);
        // The current index is clamped to what is left.
        QCOMPARE(currentOf(windows.first().toObject()), 0);
    }

    // ----- a saved tab's project (card #JN7X) ---------------------------------------------------
    // A tab attached to a project saves as {"project", "node"}; an unattached one keeps the bare
    // node shape, so nothing changes for the quiet default and there is no schema bump. The whole
    // risk is a reader that forgets to unwrap: a wrapper judged as a node is an unknown kind, so
    // every attached tab — and any window whose tabs are all attached — is silently dropped on
    // the first restore.
    // A tab with a theme of its own saves it in the same wrapper, with or without a project; a
    // bare node and a project-only wrapper read as "no theme of its own", and the wrapper is still
    // judged by the node inside it.
    void aTabsOwnThemeRoundTrips() {
        const QJsonObject bare = pane(QStringLiteral("/tmp"));
        const QJsonObject themed{{"node", bare}, {"theme", "ibm-beige"}};
        const QJsonObject both{{"project", "/home/me/repo"}, {"node", bare}, {"theme", "gruvbox-dark"}};
        QCOMPARE(tabTheme(themed), QStringLiteral("ibm-beige"));
        QCOMPARE(tabNode(themed), bare);
        QVERIFY(tabProject(themed).isEmpty());
        QCOMPARE(tabTheme(both), QStringLiteral("gruvbox-dark"));
        QCOMPARE(tabProject(both), QStringLiteral("/home/me/repo"));
        QVERIFY(tabTheme(bare).isEmpty());
        QVERIFY(tabTheme(QJsonObject{{"project", "/repo"}, {"node", bare}}).isEmpty());
        QVERIFY(tabTheme(QJsonObject{{"theme", "ibm-beige"}}).isEmpty());   // no node: not a wrapper
        QVERIFY(isUsableNode(themed));
    }

    void anAttachedTabRoundTripsAndABareOneStillWorks() {
        const QJsonObject bare = pane(QStringLiteral("/tmp"));
        const QJsonObject wrapped{{"project", "/home/me/repo"}, {"node", bare}};

        // Both shapes read back, and the bare one is unchanged in every respect.
        QCOMPARE(tabNode(wrapped), bare);
        QCOMPARE(tabProject(wrapped), QStringLiteral("/home/me/repo"));
        QCOMPARE(tabNode(bare), bare);
        QVERIFY(tabProject(bare).isEmpty());
        // A layout written before this existed has no wrappers at all: every tab reads as
        // attached to nothing, which is the quiet default anyway.
        QVERIFY(tabProject(split(QStringLiteral("h"), QJsonArray{bare, bare})).isEmpty());
        // "project" without a "node" object is not a wrapper — a node kind called `project`
        // would be judged on its own merits rather than silently unwrapped.
        QVERIFY(tabProject(QJsonObject{{"project", "/repo"}}).isEmpty());

        // Usable, and kept by usableWindows() with its wrapper intact.
        QVERIFY(isUsableNode(wrapped));
        QVERIFY(isUsableNode(QJsonObject{{"project", "/repo"},
                                         {"node", split(QStringLiteral("h"), QJsonArray{bare, bare})}}));
        const QJsonArray windows = usableWindows(document(QJsonArray{
            windowRecord(QRect(0, 0, 800, 600), QString(), QJsonArray{wrapped, bare}, 0)}));
        QCOMPARE(windows.size(), 1);
        const QJsonArray tabs = tabsOf(windows.first().toObject());
        QCOMPARE(tabs.size(), 2);
        QCOMPARE(tabs.at(0).toObject(), wrapped);
        QCOMPARE(tabProject(tabs.at(0).toObject()), QStringLiteral("/home/me/repo"));
        QVERIFY(tabProject(tabs.at(1).toObject()).isEmpty());

        // A wrapper around a node that cannot be rebuilt is dropped like any other bad tab: the
        // project is not a reason to keep a tab there is nothing to put in.
        QVERIFY(!isUsableNode(QJsonObject{{"project", "/repo"}, {"node", QJsonObject{}}}));
        QVERIFY(!isUsableNode(QJsonObject{{"project", "/repo"},
                                          {"node", QJsonObject{{"explorer", QJsonObject{}}}}}));
        QVERIFY(!isUsableNode(QJsonObject{{"project", "/repo"},
                                          {"node", split(QStringLiteral("h"), QJsonArray{})}}));
        const QJsonArray dropped = usableWindows(document(QJsonArray{
            windowRecord(QRect(0, 0, 800, 600), QString(),
                         QJsonArray{QJsonObject{{"project", "/repo"}, {"node", QJsonObject{}}}}, 0)}));
        QCOMPARE(dropped.size(), 0);

        // A project that is gone is a restore question, not a validity one: the tab still comes
        // back (RelayWindow::addTab leaves it attached to nothing when the directory has gone).
        QVERIFY(isUsableNode(QJsonObject{{"project", "/no/such/project"}, {"node", bare}}));
    }

    // An attached tab's panes must keep their saved terminal text: scrollbackIds() is what the
    // prune keeps, so a wrapper it could not see through would delete every attached pane's file.
    void anAttachedTabsScrollbackIsStillFound() {
        DataHome home;
        QJsonObject leaf = pane(QStringLiteral("/tmp"));
        QJsonObject body = leaf.value(QStringLiteral("pane")).toObject();
        const QString id = QStringLiteral("2b2a4c1e9f7d4a1b8c3e5f6a7b8c9d0e");
        QVERIFY(isScrollbackId(id));
        body.insert(QStringLiteral("scrollback"), id);
        leaf.insert(QStringLiteral("pane"), body);
        const QJsonArray windows{windowRecord(QRect(), QString(),
                                              QJsonArray{QJsonObject{{"project", "/repo"}, {"node", leaf}}}, 0)};
        QCOMPARE(scrollbackIds(windows), QStringList{id});
    }

    // #RDQ7: a start opens exactly the windows the layout asks for and no more. usableWindows() is
    // the whole restore decision — main() opens one window per entry, and one plain window when it
    // comes back empty — so a fresh profile must yield none and a saved two-window set exactly two.
    void restoreOpensOneWindowPerSavedWindow() {
        // A fresh profile: read() of a missing file gives an empty object, and nothing is reopened,
        // so main() falls through to its single new window.
        QCOMPARE(usableWindows(QJsonObject{}).size(), 0);
        QCOMPARE(usableWindows(document(QJsonArray{})).size(), 0);
        // One saved window stays one window, never a second helper record.
        const QJsonObject one = document(QJsonArray{
            windowRecord(QRect(0, 0, 800, 600), QStringLiteral("DP-1"), QJsonArray{pane(QStringLiteral("/tmp"))}, 0)});
        QCOMPARE(usableWindows(one).size(), 1);
        // Two saved windows restore as exactly two, each keeping its own tabs.
        const QJsonObject two = document(QJsonArray{
            windowRecord(QRect(0, 0, 800, 600), QStringLiteral("DP-1"), QJsonArray{pane(QStringLiteral("/tmp"))}, 0),
            windowRecord(QRect(900, 0, 700, 500), QStringLiteral("DP-1"),
                         QJsonArray{pane(QStringLiteral("/tmp")), pane(QStringLiteral("/"))}, 1)});
        const QJsonArray windows = usableWindows(two);
        QCOMPARE(windows.size(), 2);
        QCOMPARE(tabsOf(windows.at(0).toObject()).size(), 1);
        QCOMPARE(tabsOf(windows.at(1).toObject()).size(), 2);
        QCOMPARE(currentOf(windows.at(1).toObject()), 1);
    }

    void clampKeepsAWindowOnItsOwnScreen() {
        const QList<Screen> screens{{QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080)},
                                    {QStringLiteral("HDMI-1"), QRect(1920, 0, 1280, 1024)}};
        const QRect onSecond(2000, 100, 600, 400);
        QCOMPARE(clampToScreens(onSecond, QStringLiteral("HDMI-1"), screens), onSecond);
    }

    void clampMovesAWindowOffAVanishedScreen() {
        const QList<Screen> screens{{QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080)}};
        const QRect result = clampToScreens(QRect(2400, 200, 800, 600), QStringLiteral("HDMI-1"), screens);
        QVERIFY(screens.first().available.contains(result));
        QCOMPARE(result.size(), QSize(800, 600));
    }

    void clampShrinksAWindowLargerThanTheScreen() {
        const QList<Screen> screens{{QStringLiteral("DP-1"), QRect(0, 24, 1280, 1000)}};
        const QRect result = clampToScreens(QRect(-200, -100, 3000, 2000), QStringLiteral("DP-1"), screens);
        QCOMPARE(result, QRect(0, 24, 1280, 1000));
    }

    void clampWithoutScreensOrGeometryChangesNothing() {
        QCOMPARE(clampToScreens(QRect(10, 10, 100, 100), QStringLiteral("DP-1"), {}), QRect(10, 10, 100, 100));
        QCOMPARE(clampToScreens(QRect(), QStringLiteral("DP-1"), {{QStringLiteral("DP-1"), QRect(0, 0, 100, 100)}}), QRect());
    }

    void directoryFallsBackToWorkspaceThenHome() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString workspace = dir.path();
        const QString cwd = dir.filePath(QStringLiteral("sub"));
        QVERIFY(QDir().mkpath(cwd));
        const QString home = QDir::homePath();
        QCOMPARE(resolveDirectory(cwd, workspace, home), cwd);
        QCOMPARE(resolveDirectory(workspace + QStringLiteral("/gone"), workspace, home), workspace);
        QCOMPARE(resolveDirectory(workspace + QStringLiteral("/gone"), workspace + QStringLiteral("/also-gone"), home), home);
        QCOMPARE(resolveDirectory(QString(), QString(), home), home);
        // A file is not a directory.
        QFile file(dir.filePath(QStringLiteral("f.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QCOMPARE(resolveDirectory(file.fileName(), workspace, home), workspace);
    }

    void defaultPathsLiveUnderTheDataDirectory() {
        QVERIFY(defaultPath().endsWith(QStringLiteral("/relay/state/windows.json")));
        QVERIFY(defaultDirectory().endsWith(QStringLiteral("/relay/state")));
        QVERIFY(defaultLockPath().endsWith(QStringLiteral("windows.lock")));
        QVERIFY(scrollbackDirectory().endsWith(QStringLiteral("/relay/state/scrollback")));
    }

    // ----- terminal scrollback across a restart (owner report, 2026-09-18) --------------------

    // The whole point: what a pane had on screen before the quit is what the restored pane reads
    // back, in the same order, without a byte of it changing on the way.
    void scrollbackSurvivesSaveAndRestore() {
        DataHome data;
        QVERIFY(data.valid());
        const QString id = QStringLiteral("2f9a7d41-0000-4000-8000-abcdefabcdef");
        const QStringList lines{QStringLiteral("$ ls"), QStringLiteral("README.md  src"),
                                QStringLiteral("$ echo é中"), QStringLiteral("é中")};
        QVERIFY(writeScrollback(id, lines));
        QCOMPARE(readScrollback(id), lines);
        // Next to the layout, not beside it in some new place, and readable by nobody else.
        const QString path = scrollbackPath(id);
        QVERIFY(path.startsWith(defaultDirectory()));
        QCOMPARE(QFileInfo(path).permissions() & (QFile::ReadGroup | QFile::ReadOther), QFileDevice::Permissions());
        // A restart rewrites the same file rather than leaving one behind per run.
        QVERIFY(writeScrollback(id, QStringList{QStringLiteral("after the restart")}));
        QCOMPARE(readScrollback(id), QStringList{QStringLiteral("after the restart")});
        QCOMPARE(QDir(scrollbackDirectory()).entryList({QStringLiteral("*.txt")}, QDir::Files).size(), 1);
    }

    // The file cannot grow without limit: the newest lines win, on both caps.
    // When the backend produces ANSI-formatted scrollback, the escape sequences survive the file.
    void formattedScrollbackSurvivesSaveAndRestore() {
        DataHome data;
        QVERIFY(data.valid());
        const QString id = QStringLiteral("2f9a7d41-0000-4000-8000-abcdefabcdef");
        const QStringList lines{QLatin1String("\x1b[1;31mred bold\x1b[0m"),
                                QLatin1String("\x1b[38;2;1;2;3mrgb\x1b[0m"),
                                QStringLiteral("plain")};
        QVERIFY(writeScrollback(id, lines));
        QCOMPARE(readScrollback(id), lines);
    }

    // Pictures come back after a restart (#1MGS): an image row's link survives the file, the
    // per-pane one and the conversation's alike, and the replay filter, byte for byte; any other
    // OSC, and cursor movement, still lose their escape on the way back in.
    void anImageRowSurvivesSaveReadAndReplay() {
        DataHome data;
        QVERIFY(data.valid());
        const QString uri = relay::inlineimage::imageUri({QStringLiteral("/tmp/pic one.png"), 1, 3, 12});
        const QString image = QStringLiteral("\x1b[1mab\x1b[0m\x1b]8;;") + uri + QStringLiteral("\x1b\\") + QChar(0x2800)
                              + QStringLiteral("\x1b]8;;\x1b\\ after");
        const QString hostile = QStringLiteral("\x1b]8;;https://example.com\x1b\\x\x1b]8;;\x1b\\\x1b]52;c;aGk=\x07\x1b[3Ay");
        const QStringList lines{image, hostile};
        const QString id = QStringLiteral("2f9a7d41-0000-4000-8000-abcdefabcdef");
        QVERIFY(writeScrollback(id, lines));
        QTemporaryDir sessions;
        const QString sessionFile = sessions.filePath(QStringLiteral("conversation.scrollback.txt"));
        QVERIFY(relay::sessiontext::write(sessionFile, lines));
        for (const QStringList &back : {readScrollback(id), relay::sessiontext::read(sessionFile)}) {
            QCOMPARE(back, lines);
            QCOMPARE(relay::restorableAnsi(back.at(0)), image);
            const QString clean = relay::restorableAnsi(back.at(1));
            QVERIFY2(!clean.contains(QLatin1Char('\x1b')) && !clean.contains(QLatin1Char('\x07')), qPrintable(clean));
        }
    }

    void scrollbackIsBoundedByLinesAndBytes() {
        QStringList many;
        for (int i = 0; i < kScrollbackMaxLines + 500; ++i) many << QStringLiteral("line %1").arg(i);
        const QStringList clamped = clampScrollback(many);
        QCOMPARE(clamped.size(), kScrollbackMaxLines);
        QCOMPARE(clamped.constLast(), many.constLast());
        QCOMPARE(clamped.constFirst(), many.at(many.size() - kScrollbackMaxLines));

        QStringList wide;
        for (int i = 0; i < 200; ++i) wide << QString(4096, QLatin1Char('x'));
        const QStringList byBytes = clampScrollback(wide);
        QVERIFY(byBytes.size() < wide.size());
        qint64 bytes = 0;
        for (const QString &line : byBytes) bytes += line.toUtf8().size() + 1;
        QVERIFY(bytes <= kScrollbackMaxBytes);

        // Trailing blank rows of the screen are not worth a restart; blanks inside the text are.
        QCOMPARE(clampScrollback(QStringList{QStringLiteral("a"), QString(), QStringLiteral("b"), QString(), QStringLiteral("   ")}),
                 (QStringList{QStringLiteral("a"), QString(), QStringLiteral("b")}));
        QVERIFY(clampScrollback(QStringList{QString(), QStringLiteral("  ")}).isEmpty());
    }

    void scrollbackFileIsCappedOnDiskToo() {
        DataHome data;
        QVERIFY(data.valid());
        const QString id = QStringLiteral("11111111-2222-3333-4444-555555555555");
        QStringList many;
        for (int i = 0; i < kScrollbackMaxLines + 2000; ++i) many << QStringLiteral("line %1").arg(i);
        QVERIFY(writeScrollback(id, many));
        QVERIFY(QFileInfo(scrollbackPath(id)).size() <= kScrollbackMaxBytes + 1);
        const QStringList back = readScrollback(id);
        QVERIFY(!back.isEmpty());
        QCOMPARE(back.constLast(), many.constLast());
        QVERIFY(back.size() <= kScrollbackMaxLines);
        // A caller may ask for less than the cap and gets the newest lines.
        QCOMPARE(readScrollback(id, 3).size(), 3);
        QCOMPARE(readScrollback(id, 3).constLast(), many.constLast());
    }

    // An empty pane leaves no file, so a fresh shell never inherits yesterday's output.
    void emptyScrollbackRemovesTheFile() {
        DataHome data;
        QVERIFY(data.valid());
        const QString id = QStringLiteral("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
        QVERIFY(writeScrollback(id, QStringList{QStringLiteral("something")}));
        QVERIFY(QFile::exists(scrollbackPath(id)));
        QVERIFY(writeScrollback(id, QStringList{QString(), QStringLiteral("   ")}));
        QVERIFY(!QFile::exists(scrollbackPath(id)));
        QVERIFY(readScrollback(id).isEmpty());
    }

    // A hand-edited layout cannot make the store write or read outside itself.
    void scrollbackIdsAreValidated() {
        DataHome data;
        QVERIFY(data.valid());
        for (const QString &bad : {QStringLiteral("../../windows.json"), QStringLiteral("a/b"), QStringLiteral("short"),
                                   QStringLiteral(""), QStringLiteral("has space"), QString(80, QLatin1Char('x'))}) {
            QVERIFY2(!isScrollbackId(bad), qPrintable(bad));
            QVERIFY(scrollbackPath(bad).isEmpty());
            QVERIFY(!writeScrollback(bad, QStringList{QStringLiteral("x")}));
            QVERIFY(readScrollback(bad).isEmpty());
        }
        QVERIFY(isScrollbackId(QStringLiteral("2f9a7d41-0000-4000-8000-abcdefabcdef")));
    }

    // The saved layout is the list of panes that can come back; every other file is dead weight.
    void prunedToThePanesTheLayoutKeeps() {
        DataHome data;
        QVERIFY(data.valid());
        const QString kept = QStringLiteral("11111111-1111-4111-8111-111111111111");
        const QString nested = QStringLiteral("22222222-2222-4222-8222-222222222222");
        const QString gone = QStringLiteral("33333333-3333-4333-8333-333333333333");
        for (const QString &id : {kept, nested, gone}) QVERIFY(writeScrollback(id, QStringList{id}));

        QJsonObject keptPane = pane(QStringLiteral("/tmp"));
        QJsonObject leaf = keptPane.value(QStringLiteral("pane")).toObject();
        leaf.insert(QStringLiteral("scrollback"), kept);
        keptPane.insert(QStringLiteral("pane"), leaf);
        QJsonObject nestedPane = pane(QStringLiteral("/usr"));
        leaf = nestedPane.value(QStringLiteral("pane")).toObject();
        leaf.insert(QStringLiteral("scrollback"), nested);
        nestedPane.insert(QStringLiteral("pane"), leaf);
        const QJsonArray windows{windowRecord(QRect(0, 0, 800, 600), QStringLiteral("DP-1"),
                                              QJsonArray{keptPane, split(QStringLiteral("h"), QJsonArray{nestedPane, pane(QStringLiteral("/"))})},
                                              0)};
        QCOMPARE(scrollbackIds(windows), (QStringList{kept, nested}));
        QCOMPARE(pruneScrollback(scrollbackIds(windows)), 1);
        QCOMPARE(readScrollback(kept), QStringList{kept});
        QCOMPARE(readScrollback(nested), QStringList{nested});
        QVERIFY(readScrollback(gone).isEmpty());

        // "Start a fresh window set" forgets the text with the layout.
        removeAllScrollback();
        QVERIFY(readScrollback(kept).isEmpty());
        QVERIFY(!QDir(scrollbackDirectory()).exists());
    }

    // ----- relay::sessiontext: the same text, keyed by the conversation (card #0TJ9) -----------

    // Ids become file names, so the shapes the two worlds really use are the only ones accepted:
    // 32 lowercase hex for a Relay session, a canonical UUID for a guest.
    void sessionTextIdsAreValidated() {
        QVERIFY(st::isSessionId(QStringLiteral("0123456789abcdef0123456789abcdef")));
        for (const QString &bad : {QStringLiteral("0123456789ABCDEF0123456789ABCDEF"),   // upper case
                                   QStringLiteral("0123456789abcdef0123456789abcde"),    // 31
                                   QStringLiteral("0123456789abcdef0123456789abcdefa"),  // 33
                                   QStringLiteral("../../../etc/passwd"), QString()})
            QVERIFY2(!st::isSessionId(bad), qPrintable(bad));

        QVERIFY(st::isGuestId(QStringLiteral("b5493c85-d25a-47ed-ad9b-88580e43772a")));       // claude
        QVERIFY(st::isGuestId(QStringLiteral("01A0BEC2-618A-7B52-A64F-F926497799F2")));       // codex, upper case
        for (const QString &bad : {QStringLiteral("b5493c85d25a47edad9b88580e43772a"),    // no dashes
                                   QStringLiteral("b5493c85-d25a-47ed-ad9b-88580e43772"), // short
                                   QStringLiteral("b5493c85-d25a-47ed-ad9b-88580e4377zz"),
                                   QStringLiteral("../b5493c85-d25a-47ed-ad9b-88580e437"), QString()})
            QVERIFY2(!st::isGuestId(bad), qPrintable(bad));

        QVERIFY(st::isGuestSource(QStringLiteral("claude")));
        QVERIFY(st::isGuestSource(QStringLiteral("codex")));
        for (const QString &bad : {QStringLiteral("Claude"), QStringLiteral("agent"),
                                   QStringLiteral(".."), QString()})
            QVERIFY2(!st::isGuestSource(bad), qPrintable(bad));
    }

    // A path is only built from a validated id and an absolute directory with no `..` in it, so
    // nothing here can be pointed outside the sessions tree.
    void sessionTextPaths() {
        DataHome data;
        QVERIFY(data.valid());
        const QString id = QStringLiteral("0123456789abcdef0123456789abcdef");
        const QString dir = QStringLiteral("/home/u/.local/share/relay/sessions");
        QCOMPARE(st::sessionPath(dir, id), dir + QLatin1Char('/') + id + QStringLiteral(".scrollback.txt"));
        QCOMPARE(st::rewoundPath(dir, id, 3), dir + QLatin1Char('/') + id + QStringLiteral(".rewound-3.scrollback.txt"));
        QVERIFY(st::rewoundPath(dir, id, 0).isEmpty());
        QVERIFY(st::rewoundPath(dir, id, 10000).isEmpty());
        QVERIFY(st::sessionPath(QStringLiteral("relative/sessions"), id).isEmpty());
        QVERIFY(st::sessionPath(QStringLiteral("/home/u/../../etc"), id).isEmpty());
        QVERIFY(st::sessionPath(QString(), id).isEmpty());
        QVERIFY(st::sessionPath(dir, QStringLiteral("nope")).isEmpty());

        const QString guest = QStringLiteral("b5493c85-d25a-47ed-ad9b-88580e43772a");
        QCOMPARE(st::guestPath(QStringLiteral("claude"), guest),
                 st::guestDirectory() + QStringLiteral("/claude/") + guest + QStringLiteral(".scrollback.txt"));
        QVERIFY(st::guestDirectory().endsWith(QStringLiteral("/relay/sessions/guests")));
        QVERIFY(st::guestPath(QStringLiteral("gemini"), guest).isEmpty());
        QVERIFY(st::guestPath(QStringLiteral("claude"), QStringLiteral("../x")).isEmpty());
    }

    // Round trip, the shared caps, and the empty-file rule — the per-pane store's contract, under
    // the conversation's name.
    void sessionTextRoundTrip() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString id = QStringLiteral("0123456789abcdef0123456789abcdef");
        const QString path = st::sessionPath(QDir(dir.path()).absolutePath(), id);
        QVERIFY(!path.isEmpty());

        QVERIFY(st::write(path, QStringList{QStringLiteral("first"), QStringLiteral("héllo"), QString()}));
        QCOMPARE(st::read(path), (QStringList{QStringLiteral("first"), QStringLiteral("héllo")}));
        QCOMPARE(QFile::permissions(path) & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther),
                 QFile::Permissions());

        // Past the line cap: the newest lines are what comes back.
        QStringList many;
        for (int i = 0; i < relay::windowstate::kScrollbackMaxLines + 40; ++i) many << QStringLiteral("line %1").arg(i);
        QVERIFY(st::write(path, many));
        const QStringList back = st::read(path);
        QCOMPARE(back.size(), relay::windowstate::kScrollbackMaxLines);
        QCOMPARE(back.constLast(), many.constLast());

        // An emptied conversation leaves no file to be replayed next time.
        QVERIFY(st::write(path, QStringList{QString(), QStringLiteral("  ")}));
        QVERIFY(!QFile::exists(path));
        QVERIFY(st::read(path).isEmpty());
        QVERIFY(st::read(QString()).isEmpty());
        QString error;
        QVERIFY(!st::write(QString(), QStringList{QStringLiteral("x")}, &error));
        QVERIFY(!error.isEmpty());
    }

    // What a delete of the conversation has to take with it: the text and every rewound branch.
    void sessionTextSidecarsAreListed() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = QDir(dir.path()).absolutePath();
        const QString id = QStringLiteral("0123456789abcdef0123456789abcdef");
        const QString other = QStringLiteral("fedcba9876543210fedcba9876543210");
        QVERIFY(st::write(st::sessionPath(root, id), QStringList{QStringLiteral("text")}));
        QVERIFY(st::write(st::rewoundPath(root, id, 1), QStringList{QStringLiteral("undone once")}));
        QVERIFY(st::write(st::rewoundPath(root, id, 2), QStringList{QStringLiteral("undone twice")}));
        QVERIFY(st::write(st::sessionPath(root, other), QStringList{QStringLiteral("someone else")}));

        const QStringList found = st::sidecars(root, id);
        QCOMPARE(found.size(), 3);
        QVERIFY(found.contains(st::sessionPath(root, id)));
        QVERIFY(found.contains(st::rewoundPath(root, id, 1)));
        QVERIFY(found.contains(st::rewoundPath(root, id, 2)));
        QVERIFY(!found.contains(st::sessionPath(root, other)));
        QCOMPARE(st::read(st::rewoundPath(root, id, 2)), QStringList{QStringLiteral("undone twice")});
        QVERIFY(st::sidecars(root, QStringLiteral("bad")).isEmpty());
    }

    // What a rewind undid starts at the turn's own first line — the ✦ the pane printed the prompt
    // behind — and the pane's other ✦ lines are not turns.
    void turnStartFindsThePromptLine() {
        const QStringList lines{QStringLiteral("✦ first question"),
                                QStringLiteral("an answer"),
                                QStringLiteral("✦ the command finished · its result went to the agent"),
                                QStringLiteral("▸ ran pytest"),
                                QStringLiteral("✦ second question"),
                                QStringLiteral("another answer")};
        QCOMPARE(st::turnStart(lines, QStringLiteral("second question")), 4);
        QCOMPARE(st::turnStart(lines, QStringLiteral("first question")), 0);
        // A multi-line prompt is anchored by its first line, which is all the pane printed there.
        QCOMPARE(st::turnStart(lines, QStringLiteral("second question\nand more of it")), 4);
        // Not in the text: scrolled away, or sent from a client with no terminal.
        QCOMPARE(st::turnStart(lines, QStringLiteral("never asked")), -1);
        QCOMPARE(st::turnStart(lines, QString()), -1);
        QCOMPARE(st::turnStart({}, QStringLiteral("anything")), -1);
    }

    // The printed line is the prompt cut at the pane's width, so it is a prefix of it; and a turn
    // asked twice is rewound at its latest telling, not its first.
    void turnStartTakesTheWrappedAndLatestLine() {
        const QString prompt = QStringLiteral("rewrite the parser so that it stops on the first bad token");
        const QStringList lines{QStringLiteral("✦ rewrite the parser so that it"),
                                QStringLiteral("a first attempt"),
                                QStringLiteral("✦ rewrite the parser so that it"),
                                QStringLiteral("a second attempt")};
        QCOMPARE(st::turnStart(lines, prompt), 2);
        // A line longer than the prompt is some other output, not this turn.
        QCOMPARE(st::turnStart({QStringLiteral("✦ rewrite the parser so that it stops on the first bad token and more")},
                               prompt),
                 -1);
    }
};

QTEST_MAIN(WindowStateTest)
#include "windowstate_test.moc"
