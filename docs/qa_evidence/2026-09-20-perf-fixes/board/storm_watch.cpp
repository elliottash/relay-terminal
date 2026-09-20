// SPDX-License-Identifier: AGPL-3.0-or-later
// How many of a storm of card writes the Switchboard pane actually notices (#N5JJ).
//
//   storm_watch <workspace> <card id> <writes> <inplace|replace>
//
// It builds the real `relay::BoardView`, hands it a `board` event naming that card as
// in-progress — what an agent executing it looks like — and then writes once a second, cycling
// the card file, its thread and BOARD.md, exactly as the profiler's `storm.sh` did. It counts the
// `board_refresh` messages the pane sends back. `inplace` appends with O_APPEND, which is what a
// guest CLI or an editor does; `replace` writes a temporary file and renames it, which is what
// every writer Relay owns does since #N5JJ.
#include "BoardPane.h"

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>

static void appendInPlace(const QString &path, const QByteArray &text)
{
    QFile file(path);
    if (!file.open(QIODevice::Append)) return;
    file.write(text);
    file.close();
}

static void appendByReplace(const QString &path, const QByteArray &text)
{
    QFile source(path);
    QByteArray body;
    if (source.open(QIODevice::ReadOnly)) { body = source.readAll(); source.close(); }
    const QString temp = path + QStringLiteral(".tmp");
    QFile out(temp);
    if (!out.open(QIODevice::WriteOnly)) return;
    out.write(body + text);
    out.close();
    QFile::remove(path);
    QFile::rename(temp, path);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString workspace = QString::fromLocal8Bit(argv[1]);
    const QString cardId = QString::fromLocal8Bit(argv[2]);
    const int writes = QByteArray(argv[3]).toInt();
    const bool replace = QByteArray(argv[4]) == "replace";
    const QString root = workspace + QStringLiteral("/issues");

    // The card the storm writes to, and its thread: the file that carries this id.
    QString cardPath;
    QDirIterator it(root, QStringList{QStringLiteral("*.md")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) continue;
        if (file.read(400).contains(("id: " + cardId).toUtf8())) { cardPath = path; break; }
    }
    if (cardPath.isEmpty()) { QTextStream(stderr) << "no card " << cardId << "\n"; return 2; }
    const QString thread = root + QStringLiteral("/threads/") + cardId + QStringLiteral(".md");
    const QString index = root + QStringLiteral("/BOARD.md");

    relay::BoardView view(workspace);
    int refreshes = 0;
    view.onSend = [&refreshes](const QJsonObject &message) {
        if (message.value(QStringLiteral("type")).toString() == QStringLiteral("board_refresh"))
            ++refreshes;
    };
    QJsonObject card{{"id", cardId}, {"title", "storm card"}, {"type", "work"},
                     {"status", "in-progress"}, {"tab", "features"}, {"rank", "i"},
                     {"path", QFileInfo(cardPath).absoluteFilePath().mid(workspace.size() + 1)}};
    view.handleEvent(QJsonObject{{"event", "board"}, {"root", root}, {"workspace", workspace},
                                 {"config", QJsonDocument::fromJson(QByteArrayLiteral(R"({
        "columns": ["inbox", "in-progress", "done"],
        "column_statuses": {"inbox": ["inbox"], "in-progress": ["in-progress"], "done": ["done"]},
        "all_statuses": ["inbox", "in-progress", "done"],
        "tabs": [{"id": "features", "folder": "features"}], "autonomy": "auto"})")).object()},
                                 {"cards", QJsonArray{card}}, {"problems", QJsonArray{}}});
    view.show();
    QElapsedTimer settle; settle.start();
    while (settle.elapsed() < 1500) app.processEvents();
    refreshes = 0;

    if (QByteArray(argv[4]) == "watch") {
        // Somebody else is doing the writing (relay_core.board's own writers, from the matching
        // tree): just hold the pane open for the same number of seconds and count.
        QElapsedTimer run; run.start();
        while (run.elapsed() < writes * 1000 + 2500) app.processEvents();
        QTextStream(stdout) << refreshes << " of " << writes
                            << " writes reached the pane (relay's own writers)\n";
        return 0;
    }
    const auto add = replace ? appendByReplace : appendInPlace;
    for (int i = 0; i < writes; ++i) {
        const QByteArray note = "\n<!-- storm " + QByteArray::number(i) + " -->\n";
        switch (i % 3) {
        case 0: add(cardPath, note); break;
        case 1: add(thread, "\n<!-- relay:entry 2026092" + QByteArray::number(i % 10)
                            + "T00000" + QByteArray::number(i % 10)
                            + "Z-s" + QByteArray::number(i % 10)
                            + " author=owner kind=comment -->\n- storm\n"); break;
        case 2: add(index, note); break;
        }
        QElapsedTimer tick; tick.start();
        while (tick.elapsed() < 1000) { app.processEvents(); }
    }
    QElapsedTimer drain; drain.start();
    while (drain.elapsed() < 2500) app.processEvents();
    QTextStream(stdout) << refreshes << " of " << writes << " writes reached the pane ("
                        << (replace ? "replace" : "in place") << ")\n";
    return 0;
}
