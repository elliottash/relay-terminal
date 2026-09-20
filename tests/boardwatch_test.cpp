// SPDX-License-Identifier: AGPL-3.0-or-later
// What the Switchboard pane notices on disk (#N5JJ).
//
// The pane holds a QFileSystemWatcher on the board's directories, and a directory watch fires
// when an entry is created, renamed or removed — never when an existing file's content changes.
// An append to a card or a thread therefore often never reached the pane: about 21 of 60 writes
// in a one-write-a-second storm. Relay's own writers replace their files now
// (`board.append_to_thread`, tested in tests/test_board.py), which a directory watch does see;
// this is the other half — the cards that get a watch of their own, and the catch-up refresh
// when the pane is looked at again, for the writers Relay does not own.
#include "BoardPane.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QJsonObject config()
{
    const auto json = QByteArrayLiteral(R"({
      "columns": ["inbox", "in-progress", "done"],
      "column_statuses": {"inbox": ["inbox"], "in-progress": ["in-progress"], "done": ["done"]},
      "all_statuses": ["inbox", "in-progress", "done"],
      "tabs": [{"id": "features", "folder": "features"}],
      "autonomy": "auto"
    })");
    return QJsonDocument::fromJson(json).object();
}

void write(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
    file.write(text);
}

}  // namespace

class BoardWatchTests : public QObject {
    Q_OBJECT

private slots:
    void anInPlaceAppendToAWorkedCardsThreadReachesThePane();
    void showingThePaneAsksForARefresh();
};

// A card an agent is executing gets a watch on its own file and thread while the budget lasts,
// so a guest CLI or an editor writing in place — which no directory watch would show — still
// moves the board.
void BoardWatchTests::anInPlaceAppendToAWorkedCardsThreadReachesThePane()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString workspace = tmp.path();
    const QString root = workspace + QStringLiteral("/issues");
    write(root + QStringLiteral("/board.yaml"),
          "version: 1\ntabs: [{id: features, folder: features}]\n");
    write(root + QStringLiteral("/features/k7q2.md"),
          "---\nid: K7Q2\ntype: work\nstatus: in-progress\n---\n# K7Q2 card\n");
    const QString thread = root + QStringLiteral("/threads/K7Q2.md");
    write(thread, "<!-- relay:entry 20260920T000000Z-aa author=owner kind=comment -->\nfirst\n");

    relay::BoardView view(workspace);
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    const QJsonObject card{{"id", "K7Q2"}, {"title", "K7Q2 card"}, {"type", "work"},
                           {"status", "in-progress"}, {"tab", "features"}, {"rank", "i"},
                           {"path", "issues/features/k7q2.md"}};
    view.handleEvent(QJsonObject{{"event", "board"}, {"root", root}, {"workspace", workspace},
                                 {"config", config()}, {"cards", QJsonArray{card}},
                                 {"problems", QJsonArray{}}});

    const auto refreshes = [&sent] {
        int n = 0;
        for (const QJsonObject &message : sent)
            if (message.value(QStringLiteral("type")).toString() == QStringLiteral("board_refresh"))
                ++n;
        return n;
    };
    // Let the watcher settle, and ignore anything the open itself asked for.
    QTest::qWait(300);
    sent.clear();

    // An append in place: the file grows, the directory does not change. This is exactly the
    // write a directory watch cannot see.
    QFile append(thread);
    QVERIFY(append.open(QIODevice::Append));
    append.write("\n<!-- relay:entry 20260920T000100Z-ab author=guest kind=comment -->\nsecond\n");
    append.close();

    QElapsedTimer clock;
    clock.start();
    while (refreshes() == 0 && clock.elapsed() < 5000)
        QTest::qWait(50);
    QVERIFY2(refreshes() > 0, "an in-place append to a worked card's thread did not reach the pane");
}

// And the pane catches up when it is looked at again, for everything a watch missed: a tab that
// was in the background, a write inotify coalesced away, a writer Relay does not own.
void BoardWatchTests::showingThePaneAsksForARefresh()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString workspace = tmp.path();
    const QString root = workspace + QStringLiteral("/issues");
    write(root + QStringLiteral("/board.yaml"),
          "version: 1\ntabs: [{id: features, folder: features}]\n");

    relay::BoardView view(workspace);
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(QJsonObject{{"event", "board"}, {"root", root}, {"workspace", workspace},
                                 {"config", config()}, {"cards", QJsonArray{}},
                                 {"problems", QJsonArray{}}});
    QTest::qWait(600);
    sent.clear();

    view.show();
    QElapsedTimer clock;
    clock.start();
    const auto asked = [&sent] {
        for (const QJsonObject &message : sent)
            if (message.value(QStringLiteral("type")).toString() == QStringLiteral("board_refresh"))
                return true;
        return false;
    };
    while (!asked() && clock.elapsed() < 3000)
        QTest::qWait(50);
    QVERIFY2(asked(), "showing the pane did not ask for a refresh");
}

QTEST_MAIN(BoardWatchTests)
#include "boardwatch_test.moc"
