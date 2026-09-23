// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Unsent composer text. Each pane or hosted console owns one key; an edit is written before the
// next event can run, so a fatal signal leaves the last completed keystroke on disk. Password
// fields are separate widgets and never call this store.
#include "WindowState.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QString>

namespace relay::promptdraft {

inline QString pathFor(const QString &key) {
    if (key.isEmpty()) return {};
    const QString root = relay::windowstate::defaultDirectory();
    if (root.isEmpty()) return {};
    const QByteArray id = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex();
    return root + QStringLiteral("/prompt-drafts/") + QString::fromLatin1(id) + QStringLiteral(".txt");
}

inline QString read(const QString &key) {
    QFile file(pathFor(key));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll());
}

inline bool write(const QString &key, const QString &text) {
    const QString path = pathFor(key);
    if (path.isEmpty()) return false;
    if (text.isEmpty()) return !QFileInfo::exists(path) || QFile::remove(path);
    const QString directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory)) return false;
    QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    const QByteArray bytes = text.toUtf8();
    return file.write(bytes) == bytes.size() && file.commit();
}

inline bool clearAll() {
    const QString root = relay::windowstate::defaultDirectory();
    if (root.isEmpty()) return false;
    QDir drafts(root + QStringLiteral("/prompt-drafts"));
    return !drafts.exists() || drafts.removeRecursively();
}

} // namespace relay::promptdraft
