// SPDX-License-Identifier: GPL-3.0-or-later
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

