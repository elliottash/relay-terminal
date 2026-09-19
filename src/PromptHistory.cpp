// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PromptHistory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace relay {
namespace prompthistory {

namespace {
// The same shape windowstate::isScrollbackId accepts (src/WindowState.cpp): a pane token, a UUID
// without braces. Duplicated rather than shared so this library stays QtCore-only and standalone.
bool isPaneId(const QString &id) {
    if (id.size() < 8 || id.size() > 64) return false;
    for (const QChar c : id) {
        const ushort u = c.unicode();
        const bool ok = (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '-';
        if (!ok) return false;
    }
    return true;
}
}

QString directory() {
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (data.isEmpty()) return {};
    return data + QStringLiteral("/relay/state/prompt-history");
}

QString pathFor(const QString &paneId) {
    const QString dir = directory();
    if (dir.isEmpty() || !isPaneId(paneId)) return {};
    return dir + QLatin1Char('/') + paneId + QStringLiteral(".txt");
}

QString legacyPath() {
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (data.isEmpty()) return {};
    return data + QStringLiteral("/relay/state/prompt-history.txt");
}

QString encode(const QString &entry) {
    QString line;
    line.reserve(entry.size() + 8);
    for (const QChar c : entry) {
        if (c == QLatin1Char('\\')) line += QStringLiteral("\\\\");
        else if (c == QLatin1Char('\n')) line += QStringLiteral("\\n");
        else if (c == QLatin1Char('\r')) continue;   // a pasted CRLF is one newline here
        else line += c;
    }
    return line;
}

QString decode(const QString &line) {
    QString entry;
    entry.reserve(line.size());
    for (int i = 0; i < line.size(); ++i) {
        if (line.at(i) != QLatin1Char('\\') || i + 1 >= line.size()) { entry += line.at(i); continue; }
        const QChar next = line.at(++i);
        if (next == QLatin1Char('n')) entry += QLatin1Char('\n');
        else if (next == QLatin1Char('\\')) entry += QLatin1Char('\\');
        else { entry += QLatin1Char('\\'); entry += next; }   // not an escape we wrote; keep it as it reads
    }
    return entry;
}

bool storable(const QString &entry) {
    return !entry.trimmed().isEmpty() && entry.size() <= kMaxEntryChars;
}

QStringList read(const QString &path, int maxEntries) {
    if (path.isEmpty() || maxEntries <= 0) return {};
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) return {};
    QStringList entries;
    while (!file.atEnd()) {
        // Only the line ending goes: an entry may begin or end with spaces the person typed.
        QString line = QString::fromUtf8(file.readLine());
        while (line.endsWith(QLatin1Char('\n')) || line.endsWith(QLatin1Char('\r'))) line.chop(1);
        if (line.isEmpty()) continue;
        const QString entry = decode(line);
        if (storable(entry)) entries.append(entry);
    }
    if (entries.size() > maxEntries) entries = entries.mid(entries.size() - maxEntries);
    return entries;
}

QString lastEntry(const QString &path) {
    if (path.isEmpty()) return {};
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) return {};
    // Enough for any storable entry: kMaxEntryChars characters, at most four bytes each, plus the
    // newline before it. Anything longer than that was never written by us.
    const qint64 window = qint64(kMaxEntryChars) * 4 + 16;
    if (file.size() > window) file.seek(file.size() - window);
    QByteArray tail = file.readAll();
    while (tail.endsWith('\n') || tail.endsWith('\r')) tail.chop(1);
    const int cut = tail.lastIndexOf('\n');
    const QString entry = decode(QString::fromUtf8(cut < 0 ? tail : tail.mid(cut + 1)));
    return storable(entry) ? entry : QString();
}

bool trim(const QString &path, int maxEntries, QString *error) {
    auto fail = [error](const QString &text) {
        if (error) *error = text;
        return false;
    };
    if (error) error->clear();
    if (path.isEmpty()) return fail(QStringLiteral("No writable data directory for the prompt history."));
    const QStringList kept = read(path, std::max(1, maxEntries));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    QByteArray text;
    for (const QString &entry : kept) text += encode(entry).toUtf8() + '\n';
    if (file.write(text) < 0) {
        file.cancelWriting();
        return fail(file.errorString());
    }
    if (!file.commit()) return fail(file.errorString());
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

bool append(const QString &path, const QString &entry, QString *error) {
    auto fail = [error](const QString &text) {
        if (error) *error = text;
        return false;
    };
    if (error) error->clear();
    if (!storable(entry)) return true;
    if (path.isEmpty()) return fail(QStringLiteral("No writable data directory for the prompt history."));
    if (lastEntry(path) == entry) return true;   // the same line twice in a row is stored once
    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir)) return fail(QStringLiteral("Could not create %1.").arg(dir));
    QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    // Append, never rewrite: the write is one O_APPEND write(2), so two Relays running at once
    // interleave entries instead of losing each other's.
    QFile file(path);
    const bool fresh = !file.exists();
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return fail(file.errorString());
    if (fresh) file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (file.write(encode(entry).toUtf8() + '\n') < 0) return fail(file.errorString());
    if (!file.flush()) return fail(file.errorString());
    const qint64 size = file.size();
    file.close();
    if (size > kTrimBytes) return trim(path, kMaxEntries, error);
    return true;
}

bool clearDirectory(const QString &dir, QString *error) {
    if (error) error->clear();
    if (dir.isEmpty()) return true;
    if (!QDir(dir).exists()) return true;
    if (QDir(dir).removeRecursively()) return true;
    if (error) *error = QStringLiteral("Could not remove %1.").arg(dir);
    return false;
}

bool clearAll(QString *error) {
    if (error) error->clear();
    if (!clearDirectory(directory(), error)) return false;
    // The file every pane shared until 2026-09-19: nothing reads it, but forgetting everything
    // means it goes too.
    const QString legacy = legacyPath();
    if (!legacy.isEmpty() && QFile::exists(legacy) && !QFile::remove(legacy)) {
        if (error) *error = QStringLiteral("Could not remove %1.").arg(legacy);
        return false;
    }
    return true;
}

int prune(const QString &dir, const QStringList &keepPaneIds) {
    if (dir.isEmpty()) return 0;
    int removed = 0;
    const auto files = QDir(dir).entryInfoList({QStringLiteral("*.txt")}, QDir::Files);
    for (const QFileInfo &file : files)
        if (!keepPaneIds.contains(file.completeBaseName()) && QFile::remove(file.absoluteFilePath())) ++removed;
    return removed;
}

}  // namespace prompthistory
}  // namespace relay
