// SPDX-License-Identifier: AGPL-3.0-or-later
#include "BoardWorkspace.h"

#include "Projects.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace relay {

QString boardCardIdForFile(const QString &path, const QString &project)
{
    if (!QDir::isAbsolutePath(path) || project.isEmpty()) return {};
    const QString board = projects::boardDirOf(project);
    if (board.isEmpty()) return {};
    const QFileInfo info(path);
    if (!info.isFile() || info.suffix() != QLatin1String("md")) return {};
    const QString relative = QDir(QFileInfo(board).canonicalFilePath())
                                 .relativeFilePath(info.canonicalFilePath());
    const QStringList parts = relative.split(QLatin1Char('/'));
    if (parts.size() < 2 || parts.first() == QLatin1String("..")
        || parts.contains(QStringLiteral("threads"))) return {};

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    // Read only bounded front matter, never a potentially large card body. Card ids are
    // scalar strings in the board format; a heading or an id mentioned in prose is not one.
    if (file.readLine(4096).trimmed() != "---") return {};
    static const QRegularExpression idLine(
        QStringLiteral("^id:[ \\t]*(?:([0-9A-HJKMNP-TV-Z]{4})|'([0-9A-HJKMNP-TV-Z]{4})'|\"([0-9A-HJKMNP-TV-Z]{4})\")[ \\t]*(?:#.*)?$"));
    QString id;
    while (!file.atEnd() && file.pos() < 65536) {
        QString line = QString::fromUtf8(file.readLine(4096));
        if (line.endsWith(QLatin1Char('\n'))) line.chop(1);
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        if (line == QLatin1String("---")) return id;
        const auto match = idLine.match(line);
        if (match.hasMatch()) {
            id = match.captured(1) + match.captured(2) + match.captured(3);
            bool letter = false;
            for (const QChar c : id) letter |= c.isLetter();
            if (!letter) return {};
        }
    }
    return {};
}

QString boardRootFor(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        // A relative candidate would be resolved against the process's current directory, which is
        // the launch directory this rule exists to stop consulting; there is no pane it could
        // honestly stand for, so it is skipped rather than guessed at.
        if (candidate.isEmpty() || !QDir::isAbsolutePath(candidate))
            continue;
        for (QDir dir(candidate); ; ) {
            // Every folder of `projects::boardFolders()` at *this* directory before going up, so
            // the nearest ancestor wins whatever the folder is called (protocol 19.1). boardDirOf()
            // is the one place that ordering is written down.
            if (!projects::boardDirOf(dir.absolutePath()).isEmpty())
                return dir.absolutePath();
            if (!dir.cdUp())
                break;
        }
    }
    return QString();
}

}  // namespace relay
