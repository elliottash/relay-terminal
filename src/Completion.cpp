// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Completion.h"

#include <QDir>
#include <QFileInfo>

namespace relay {
namespace {

const QString kSpecial = QStringLiteral(" \t\"'\\$&();|<>*?[]{}`!#~");

// The token under the cursor ends at the cursor and starts after the last unescaped space.
int tokenStart(const QString &line, int cursor) {
    int start = cursor;
    while (start > 0) {
        const QChar ch = line.at(start - 1);
        if (!ch.isSpace()) { --start; continue; }
        // A space is part of the token when it was escaped (`my\ file`).
        int slashes = 0;
        for (int i = start - 2; i >= 0 && line.at(i) == '\\'; --i) ++slashes;
        if (slashes % 2 == 0) break;
        --start;
    }
    return start;
}

bool firstWord(const QString &line, int start) {
    for (int i = start - 1; i >= 0; --i) {
        const QChar ch = line.at(i);
        if (ch.isSpace()) continue;
        return ch == '|' || ch == ';' || ch == '&';  // after a pipe or ; a command name follows again
    }
    return true;
}

QString longestCommonPrefix(const QStringList &items) {
    if (items.isEmpty()) return QString();
    QString prefix = items.first();
    for (const QString &item : items) {
        int i = 0;
        while (i < prefix.size() && i < item.size() && prefix.at(i) == item.at(i)) ++i;
        prefix.truncate(i);
    }
    return prefix;
}

}  // namespace

QString escapeToken(const QString &text) {
    QString out;
    out.reserve(text.size());
    for (const QChar ch : text) {
        if (kSpecial.contains(ch)) out += '\\';
        out += ch;
    }
    return out;
}

QString unescapeToken(const QString &text) {
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == '\\' && i + 1 < text.size()) ++i;
        out += text.at(i);
    }
    return out;
}

Completion completionToken(const QString &line, int cursor) {
    Completion result;
    cursor = std::clamp(cursor, 0, int(line.size()));
    result.start = tokenStart(line, cursor);
    result.length = cursor - result.start;
    result.commands = firstWord(line, result.start);
    return result;
}

Completion completeAt(const QString &line, int cursor, const QString &cwd, const QStringList &commands) {
    Completion result = completionToken(line, cursor);
    result.commands = false;
    const QString token = unescapeToken(line.mid(result.start, result.length));

    // The first word of a command line is a command name, unless it is written as a path.
    if (firstWord(line, result.start) && !token.contains('/') && !token.startsWith('~')) {
        result.commands = true;
        for (const QString &name : commands)
            if (name.startsWith(token)) { result.inserts << escapeToken(name); result.labels << name; }
        result.inserts.removeDuplicates();
        result.labels.removeDuplicates();
        result.inserts.sort();
        result.labels.sort();
        if (!result.inserts.isEmpty()) {
            result.common = longestCommonPrefix(result.inserts);
            return result;
        }
        result.commands = false;  // no command matched: fall through and try paths
    }

    // Split the token into the directory to list and the prefix to match in it.
    QString head = token, prefix;
    const int slash = token.lastIndexOf('/');
    if (slash >= 0) { head = token.left(slash + 1); prefix = token.mid(slash + 1); }
    else { head.clear(); prefix = token; }

    QString dirPath = head;
    if (dirPath.startsWith(QStringLiteral("~/")) || dirPath == QStringLiteral("~"))
        dirPath = QDir::homePath() + dirPath.mid(1);
    if (dirPath.isEmpty()) dirPath = cwd;
    else if (!QDir::isAbsolutePath(dirPath)) dirPath = QDir(cwd).filePath(dirPath);

    QDir dir(dirPath);
    if (!dir.exists()) return result;
    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
    // Hidden files only once a dot is typed, as Readline does.
    if (prefix.startsWith('.')) filters |= QDir::Hidden;
    const QStringList names = dir.entryList(filters, QDir::Name | QDir::DirsFirst | QDir::IgnoreCase);
    for (const QString &name : names) {
        if (!name.startsWith(prefix)) continue;
        const bool isDir = QFileInfo(dir.filePath(name)).isDir();
        result.labels << (isDir ? name + '/' : name);
        // A leading ~ stays unescaped, or the shell would look for a directory called "~".
        const QString escapedHead = head.startsWith('~') ? '~' + escapeToken(head.mid(1)) : escapeToken(head);
        result.inserts << escapedHead + escapeToken(name) + (isDir ? QStringLiteral("/") : QString());
    }
    result.common = longestCommonPrefix(result.inserts);
    return result;
}

}  // namespace relay
