// SPDX-License-Identifier: GPL-3.0-or-later
#include "PromptHistory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace relay {
namespace prompthistory {

QString defaultDirectory() {
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (data.isEmpty()) return {};
    return data + QStringLiteral("/relay/state");
}

QString defaultPath() {
    const QString dir = defaultDirectory();
    return dir.isEmpty() ? QString() : dir + QStringLiteral("/prompt-history.txt");
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

bool clear(const QString &path, QString *error) {
    if (error) error->clear();
    if (path.isEmpty()) return true;
    QFile file(path);
    if (!file.exists()) return true;
    if (file.remove()) return true;
    if (error) *error = file.errorString();
    return false;
}

}  // namespace prompthistory
}  // namespace relay
