// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Where Relay's data files are (the backend, the shell integration, the scripts it runs) and the
// fuzzy score the palette and the `@` picker rank their rows with. Two small free functions that
// everything else in main.cpp reaches for, so they come first and depend on nothing of Relay's.
// The RELAY_* fallbacks live here because dataRoot() is what reads them.

#include "SourceDir.h"
#include <QDir>
#include <QStandardPaths>
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

// Packaged Windows runtimes are private to Relay; never rely on the Store alias.
inline QString relayPython() {
#ifdef Q_OS_WIN
    const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/../runtime/python/python.exe");
    if (QFileInfo::exists(bundled)) return QDir::cleanPath(bundled);
    const QString configured = qEnvironmentVariable("RELAY_PYTHON");
    if (!configured.isEmpty() && QFileInfo::exists(configured)) return configured;
    return QStandardPaths::findExecutable(QStringLiteral("python.exe"));
#elif defined(Q_OS_MACOS)
    const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/python/bin/python3");
    if (QFileInfo::exists(bundled)) return QDir::cleanPath(bundled);
    const QString configured = qEnvironmentVariable("RELAY_PYTHON");
    if (!configured.isEmpty() && QFileInfo::exists(configured)) return configured;
    return QStandardPaths::findExecutable(QStringLiteral("python3"));
#else
    return QStandardPaths::findExecutable(QStringLiteral("python3"));
#endif
}
inline QString relayBash() {
#ifdef Q_OS_MACOS
    const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/bash/bin/bash");
    if (QFileInfo::exists(bundled)) return QDir::cleanPath(bundled);
    const QString configured = qEnvironmentVariable("RELAY_BASH");
    if (!configured.isEmpty() && QFileInfo::exists(configured)) return configured;
    // macOS system Bash is 3.2; source builds need a modern Bash too.
    return QStandardPaths::findExecutable(QStringLiteral("bash"),
        {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin")});
#else
    return QStringLiteral("/bin/bash");
#endif
}
inline QString relayPowerShell() {
    const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/../runtime/powershell/pwsh.exe");
    if (QFileInfo::exists(bundled)) return QDir::cleanPath(bundled);
    return QStandardPaths::findExecutable(QStringLiteral("pwsh.exe"));
}

inline QString dataRoot() {
    const QStringList choices{qEnvironmentVariable("RELAY_DATA_DIR"),
#ifdef Q_OS_MACOS
        QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/relay"),
#endif
        QCoreApplication::applicationDirPath() + QStringLiteral("/../share/relay"),
        QStringLiteral(RELAY_DATA_DIR), QString::fromUtf8(relaySourceDir())};
    for (const auto &path : choices) {
        if (!path.isEmpty() && QFileInfo::exists(path + QStringLiteral("/backend/worker.py")))
            return QDir(path).absolutePath();
    }
    throw std::runtime_error("Relay's backend and shell data files were not found.");
}


// Relay owns agent scratch (card #DVV2): three classes with fixed homes — disposable `scratch`
// under the cache dir, `install` under the data dir, `keep` under <project>/.relay/work/ — and one
// append-only ledger of every one of them under the state dir. An agent never picks these paths;
// it asks, and these functions (mirrored by backend/relay_core/scratch.py — the two definitions
// must not drift; docs/SCRATCH.md is the contract) decide where things live.
namespace relay::scratchpaths {
// $<override> wins, then $XDG_<home>, then the XDG default under $HOME; only when $HOME itself is
// unset does it fall back to Qt's idea of the location, which is the only non-UNIX case that
// reaches it.
inline QString xdgBase(const char *override_, const char *xdgHome, const QString &homeDefault,
                       QStandardPaths::StandardLocation qtFallback) {
    QString set = qEnvironmentVariable(override_);
    if (!set.isEmpty()) return QDir(set).absolutePath();
    set = qEnvironmentVariable(xdgHome);
    if (!set.isEmpty()) return QDir(set).absolutePath();
    const QString home = qEnvironmentVariable("HOME");
    if (!home.isEmpty()) return QDir(home).filePath(homeDefault);
    return QStandardPaths::writableLocation(qtFallback);
}
// Class `scratch`: disposable trees, per session. The cache dir, not /tmp — it survives a reboot
// mid-task, it is per-user, and cleaners and backup tools already know to skip it.
inline QString scratchRoot() {
    return QDir(xdgBase("RELAY_SCRATCH_HOME", "XDG_CACHE_HOME", QStringLiteral(".cache"),
                        QStandardPaths::GenericCacheLocation))
        .filePath(QStringLiteral("relay/scratch"));
}
// Class `install`: tools an agent installs for the user. Never a new top-level folder in $HOME.
inline QString toolsRoot() {
    return QDir(xdgBase("RELAY_TOOLS_HOME", "XDG_DATA_HOME", QStringLiteral(".local/share"),
                        QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("relay/tools"));
}
// The ledger itself: one append-only JSONL row per scratch dir the user owns.
inline QString ledgerPath() {
    return QDir(xdgBase("RELAY_STATE_HOME", "XDG_STATE_HOME", QStringLiteral(".local/state"),
                        QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("relay/scratch-ledger.jsonl"));
}
// A session's own subroot — where TMPDIR points for its shells, so mktemp lands somewhere owned.
// Named by the first 12 characters of the session (#H1BS), as backend/relay_core/scratch.py
// session_root does: <scratchRoot()>/<36-char pane uuid>/tmp left no room under the 108-byte
// Unix socket limit for Chrome's singleton socket under TMPDIR.
inline QString sessionRoot(const QString &session) {
    return QDir(scratchRoot()).filePath(session.left(12));
}
// Class `keep`: work that must outlive the task, in the project (git-ignored via /.relay/),
// promoted into the repo on request. It is never scratch, and never in /tmp.
inline QString keepRoot(const QString &project) {
    return QDir(project).filePath(QStringLiteral(".relay/work"));
}
}  // namespace relay::scratchpaths

// The guests' Relay-owned home (card #5A37). Every Claude Code and Codex this process starts — the
// headless harnesses, the model picker's launch line, and whatever is typed into a pane shell —
// inherits CLAUDE_CONFIG_DIR / CODEX_HOME pointing at <data>/relay/guests/<guest>, so transcripts
// and state land under Relay and claude's 30-day cleanup does not reach them. The user's own
// directories are recorded first (RELAY_USER_CLAUDE_CONFIG_DIR / RELAY_USER_CODEX_HOME), because
// backend/relay_core/guest_home.py links their settings, skills and login into the new homes and
// guest_sessions.py keeps reading the conversations already there. backend/relay_core/guest.py
// reads the same variables; RELAY_GUEST_HOME=off in Relay's own environment turns all of it off.
namespace relay::guesthome {
inline bool exportEnvironment() {
    const QString inherited = qEnvironmentVariable("RELAY_GUEST_HOME").trimmed();
    const QString lowered = inherited.toLower();
    if (lowered == QLatin1String("off") || lowered == QLatin1String("0")
        || lowered == QLatin1String("false") || lowered == QLatin1String("no"))
        return false;
    // Inherited when this Relay was started from another one's pane: the same homes, then.
    const QString root = !inherited.isEmpty() && QDir::isAbsolutePath(inherited)
        ? QDir::cleanPath(inherited)
        : QDir(scratchpaths::xdgBase("XDG_DATA_HOME", "XDG_DATA_HOME", QStringLiteral(".local/share"),
                                     QStandardPaths::GenericDataLocation))
              .filePath(QStringLiteral("relay/guests"));
    struct Guest { const char *name, *variable, *saved, *dotDir; };
    const Guest guests[] = {{"claude", "CLAUDE_CONFIG_DIR", "RELAY_USER_CLAUDE_CONFIG_DIR", ".claude"},
                            {"codex", "CODEX_HOME", "RELAY_USER_CODEX_HOME", ".codex"}};
    for (const Guest &guest : guests) {
        const QString owned = QDir(root).filePath(QString::fromLatin1(guest.name));
        if (qEnvironmentVariableIsEmpty(guest.saved)) {
            const QString current = qEnvironmentVariable(guest.variable).trimmed();
            const QString user = !current.isEmpty() && QDir::isAbsolutePath(current)
                    && QDir::cleanPath(current) != owned
                ? QDir::cleanPath(current)
                : QDir(QDir::homePath()).filePath(QString::fromLatin1(guest.dotDir));
            qputenv(guest.saved, user.toUtf8());
        }
        if (!QDir().mkpath(owned)) return false;   // no home to point at: leave the CLIs' own
        QFile::setPermissions(owned, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        qputenv(guest.variable, owned.toUtf8());
    }
    qputenv("RELAY_GUEST_HOME", root.toUtf8());
    return true;
}
}  // namespace relay::guesthome

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
#ifdef Q_OS_MACOS
    QFile file(exe.absolutePath() + QStringLiteral("/../Resources/relay.build-id"));
#else
    QFile file(exe.absolutePath() + QStringLiteral("/relay.build-id"));
#endif
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
