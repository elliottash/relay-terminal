// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Where Relay's data files are (the backend, the shell integration, the scripts it runs) and the
// fuzzy score the palette and the `@` picker rank their rows with. Two small free functions that
// everything else in main.cpp reaches for, so they come first and depend on nothing of Relay's.
// The RELAY_* fallbacks live here because dataRoot() is what reads them.

#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <stdexcept>

#include <QDateTime>
#include <QFile>

#ifndef RELAY_VERSION
#define RELAY_VERSION "0.0.0-dev"
#endif
#ifndef RELAY_DATA_DIR
#define RELAY_DATA_DIR "/usr/local/share/relay"
#endif
#ifndef RELAY_SOURCE_DIR
#define RELAY_SOURCE_DIR "."
#endif

inline QString dataRoot() {
    const QStringList choices{qEnvironmentVariable("RELAY_DATA_DIR"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../share/relay"),
        QStringLiteral(RELAY_DATA_DIR), QStringLiteral(RELAY_SOURCE_DIR)};
    for (const auto &path : choices) {
        if (!path.isEmpty() && QFileInfo::exists(path + QStringLiteral("/backend/worker.py")))
            return QDir(path).absolutePath();
    }
    throw std::runtime_error("Relay's backend and shell data files were not found.");
}


// Case-insensitive subsequence score; 0 means no match. Contiguous and earlier matches score higher.
inline int relayFuzzyScore(const QString &needle, const QString &haystack) {
    if (needle.isEmpty()) return 1;
    const QString n = needle.toLower(), h = haystack.toLower();
    const int direct = h.indexOf(n);
    if (direct >= 0) return 10000 - direct;
    int pos = 0, first = -1, last = -1;
    for (const QChar c : n) {
        pos = h.indexOf(c, pos);
        if (pos < 0) return 0;
        if (first < 0) first = pos;
        last = pos++;
    }
    return std::max(1, 5000 - (last - first) * 10 - first);
}

// Which build this is (owner, 2026-09-19: "where does relay say what build it is? put that in
// settings"). scripts/build-id.py numbers every relink of the app — 2026-09-19.14H.01: the date,
// the 24-hour hour, the count that hour — into `relay.build-id` beside the binary. capture() reads
// it once, at start-up, which is the build this *process* is; idOnDisk() reads it again whenever
// asked, which is the build a fresh launch would be. They differ after a rebuild: a running Relay
// keeps the binary it started with, and so does every window it opens.
namespace relay::buildinfo {
inline QString idOnDisk() {
    const QFileInfo exe(QCoreApplication::applicationFilePath());
    QFile file(exe.absolutePath() + QStringLiteral("/relay.build-id"));
    if (file.open(QIODevice::ReadOnly)) {
        const QString id = QString::fromUtf8(file.readLine(64)).trimmed();
        if (!id.isEmpty()) return id;
    }
    // A binary without its number (copied by hand, or built before numbering): its file time, in
    // the same shape, with no count.
    return exe.lastModified().toString(QStringLiteral("yyyy-MM-dd.HH'H'")) + QStringLiteral(".--");
}
struct Running { QString id; QDateTime started; };
inline Running &running() { static Running r; return r; }
inline void capture() { running() = Running{idOnDisk(), QDateTime::currentDateTime()}; }
}  // namespace relay::buildinfo
