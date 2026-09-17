// SPDX-License-Identifier: GPL-3.0-or-later
// Saved window layout ("reopen where I left off"): the parts that do not need a window —
// reading and writing state/windows.json, validating pane trees, clamping geometry onto a
// screen that still exists, and the cwd/workspace/$HOME fallback.
#include "WindowState.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace relay::windowstate;

namespace {

QJsonObject pane(const QString &cwd, const QString &engine = QStringLiteral("relay")) {
    return {{"pane", QJsonObject{{"cwd", cwd}, {"workspace", cwd}, {"engine", engine},
                                 {"engine_core", "ghostty"}, {"agent_role", "main"},
                                 {"session_id", "abc123"}}}};
}

QJsonObject split(const QString &direction, const QJsonArray &children) {
    return {{"split", direction}, {"children", children}, {"sizes", QJsonArray{1000, 1000}}};
}

}  // namespace

class WindowStateTest : public QObject {
    Q_OBJECT

private slots:
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
    }
};

QTEST_MAIN(WindowStateTest)
#include "windowstate_test.moc"
