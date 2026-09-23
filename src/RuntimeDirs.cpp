// SPDX-License-Identifier: AGPL-3.0-or-later
#include "RuntimeDirs.h"

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef Q_OS_MACOS
#include <libproc.h>
#include <signal.h>
#include <cerrno>
#include <climits>
#endif
#endif

namespace relay {
namespace runtimedirs {
namespace {

const char kOwnerMagic[] = "relay-owner";

// How deep a runtime directory may nest before removal gives up. A pane's directory holds two
// files; anything deeper than this is not ours to walk.
constexpr int kMaxRemoveDepth = 8;

// `/proc/<pid>/stat` field 22 (`starttime`), the boot ticks at which that process started. The
// second field is the executable name in parentheses and may itself contain spaces and brackets,
// so the fields are counted from the *last* ')' — the usual way to read this file.
bool startTimeOf(qint64 pid, qulonglong *out) {
#ifdef Q_OS_WIN
    if (pid <= 0 || quint64(pid) > MAXDWORD) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, DWORD(pid));
    if (!process) return false;
    FILETIME created{}, exited{}, kernel{}, user{};
    const bool ok = WaitForSingleObject(process, 0) == WAIT_TIMEOUT
                    && GetProcessTimes(process, &created, &exited, &kernel, &user);
    CloseHandle(process);
    if (ok) *out = (qulonglong(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    return ok;
#elif defined(Q_OS_MACOS)
    if (pid <= 0 || pid > INT_MAX) return false;
    struct proc_bsdinfo info {};
    if (proc_pidinfo(int(pid), PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info))
        return false;
    *out = qulonglong(info.pbi_start_tvsec) * 1000000 + info.pbi_start_tvusec;
    return true;
#else
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly)) return false;
    const QByteArray line = stat.readLine(8192);
    const int close = line.lastIndexOf(')');
    if (close < 0) return false;
    const QList<QByteArray> fields = line.mid(close + 1).simplified().split(' ');
    // fields[0] is field 3 (state), so field 22 sits at index 19.
    if (fields.size() < 20) return false;
    bool ok = false;
    const qulonglong ticks = fields.at(19).toULongLong(&ok);
    if (!ok) return false;
    *out = ticks;
    return true;
#endif
}

#ifndef Q_OS_WIN
// lstat(2): tells a symlink from what it points at, and hands back the mode, owner and mtime in
// one call. Every decision the sweep makes about an entry is made from this, never from a path
// that a symlink could have redirected.
bool statNoFollow(const QString &path, struct stat *out) {
    return ::lstat(QFile::encodeName(path).constData(), out) == 0;
}

// Remove a directory tree without ever descending through a symlink: a link is unlinked (which
// removes the link, not what it points at), a real directory is walked.
//
// Qt 5.15's QDir::removeRecursively() does the same thing — its loop recurses only when
// `fi.isDir() && !fi.isSymLink()` and otherwise calls QFile::remove(), which unlinks the link —
// so it would have been safe here. This does it by hand anyway: the guard is one line in Qt's
// implementation, it is not part of the documented contract, and what is being deleted is a
// user's /tmp. The depth cap is the same reasoning.
bool removeTree(const QString &path, int depth = 0) {
    if (depth > kMaxRemoveDepth) return false;
    bool ok = true;
    const QStringList names = QDir(path).entryList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::NoSort);
    for (const QString &name : names) {
        const QString child = path + QLatin1Char('/') + name;
        struct stat info {};
        if (!statNoFollow(child, &info)) { ok = false; continue; }
        if (S_ISDIR(info.st_mode)) ok = removeTree(child, depth + 1) && ok;
        else if (!QFile::remove(child)) ok = false;   // unlink(2): the link, never its target
    }
    return QDir().rmdir(path) && ok;
}

// The newest modification time of the directory itself and its direct children, in seconds. Used
// only for directories with no owner file, where age is all there is to go on.
qint64 newestMtime(const QString &path, const struct stat &dirInfo) {
    qint64 newest = qint64(dirInfo.st_mtime);
    const QStringList names = QDir(path).entryList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::NoSort);
    for (const QString &name : names) {
        struct stat info {};
        if (statNoFollow(path + QLatin1Char('/') + name, &info)) newest = qMax(newest, qint64(info.st_mtime));
    }
    return newest;
}


#endif
}  // namespace

bool DirStamp::changed(const QString &path) {
#ifdef Q_OS_WIN
    // NTFS directory timestamps can be deferred while file handles are open. Listing the
    // small spool is safer than suppressing events based on an unreliable timestamp.
    return QDir(path).exists();
#else
    struct stat info;
    if (::stat(QFile::encodeName(path).constData(), &info) != 0) { m_seen = false; return false; }
#ifdef Q_OS_MACOS
    const timespec modified = info.st_mtimespec;
#else
    const timespec modified = info.st_mtim;
#endif
    if (m_seen && info.st_ino == m_inode
        && modified.tv_sec == m_mtime.tv_sec && modified.tv_nsec == m_mtime.tv_nsec)
        return false;
    m_seen = true;
    m_inode = info.st_ino;
    m_mtime = modified;
    return true;
#endif
}

Owner self() {
    Owner owner;
#ifdef Q_OS_WIN
    const qint64 pid = qint64(GetCurrentProcessId());
#else
    const qint64 pid = qint64(::getpid());
#endif
    qulonglong ticks = 0;
    if (!startTimeOf(pid, &ticks)) return owner;   // no identity: better to leave no mark at all
    owner.pid = pid;
    owner.startTime = ticks;
    return owner;
}

Owner readOwner(const QString &dir) {
    Owner owner;
    QFile file(dir + QLatin1Char('/') + ownerFileName());
    if (!file.open(QIODevice::ReadOnly)) return owner;
    const QByteArray text = file.read(4096);
    qint64 pid = 0;
    qulonglong ticks = 0;
    bool magic = false, hasStart = false;
    for (const QByteArray &line : text.split('\n')) {
        const QByteArray trimmed = line.simplified();
        if (trimmed.isEmpty()) continue;
        const int space = trimmed.indexOf(' ');
        if (space <= 0) continue;
        const QByteArray key = trimmed.left(space), value = trimmed.mid(space + 1);
        bool ok = false;
        if (key == kOwnerMagic) magic = value.toInt(&ok) == kOwnerVersion && ok;
        else if (key == "pid") pid = value.toLongLong(&ok);
        else if (key == "starttime") { ticks = value.toULongLong(&ok); hasStart = ok; }
    }
    // A mark from a format we do not know, or without both halves, is no mark: the directory is
    // then judged on its age, which errs towards keeping it.
    if (!magic || pid <= 0 || !hasStart) return owner;
    owner.pid = pid;
    owner.startTime = ticks;
    return owner;
}

bool markOwned(const QString &dir) {
    const Owner owner = self();
    if (!owner.isValid()) return false;
    const QString path = dir + QLatin1Char('/') + ownerFileName();
    QSaveFile file(path);
    // Atomic like every other small file Relay writes: a sweeping Relay must never see half a mark
    // and conclude the directory is unowned.
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    const QByteArray text = QByteArray(kOwnerMagic) + ' ' + QByteArray::number(kOwnerVersion) + '\n'
                            + "pid " + QByteArray::number(owner.pid) + '\n'
                            + "starttime " + QByteArray::number(owner.startTime) + '\n';
    if (file.write(text) < 0) { file.cancelWriting(); return false; }
    if (!file.commit()) return false;
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

bool ownerAlive(const Owner &owner) {
    if (!owner.isValid()) return false;
    qulonglong ticks = 0;
    if (!startTimeOf(owner.pid, &ticks)) {
#ifdef Q_OS_MACOS
        // A denied identity query is not proof of death. Only ESRCH permits cleanup.
        if (owner.pid > INT_MAX) return false;
        return ::kill(pid_t(owner.pid), 0) == 0 || errno != ESRCH;
#else
        return false;
#endif
    }
    // The pid is in use again by somebody else if the start times differ.
    return ticks == owner.startTime;
}

bool ownerAlive(const QString &dir) { return ownerAlive(readOwner(dir)); }

bool waitForExit(const Owner &owner, qint64 timeoutMs) {
    if (!owner.isValid() || !owner.startTime || timeoutMs < 0) return false;
    QElapsedTimer elapsed;
    elapsed.start();
    while (ownerAlive(owner)) {
        if (elapsed.elapsed() >= timeoutMs) return false;
        QThread::msleep(50);
    }
    return true;
}

bool isRuntimeDirName(const QString &name) {
    QString suffix;
    if (name.startsWith(QLatin1String("relay-open-"))) suffix = name.mid(11);
    else if (name.startsWith(QLatin1String("relay-"))) suffix = name.mid(6);
    else return false;
    // QTemporaryDir replaces XXXXXX with six characters of this alphabet and nothing else.
    if (suffix.size() != 6) return false;
    for (const QChar c : suffix) {
        const ushort u = c.unicode();
        const bool ok = (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z');
        if (!ok) return false;
    }
    return true;
}

SweepResult sweep(const QString &tempRoot, qint64 graceSeconds, int maxDirectories) {
    SweepResult result;
#ifdef Q_OS_WIN
    // Unix mode/uid checks do not establish ownership on Windows. Keep crash leftovers
    // until an ACL/reparse-safe sweep is available; QTemporaryDir still cleans normal exits.
    Q_UNUSED(tempRoot);
    Q_UNUSED(graceSeconds);
    Q_UNUSED(maxDirectories);
    return result;
#else
    const QString root = QFileInfo(tempRoot).canonicalFilePath();
    if (root.isEmpty()) return result;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const uid_t uid = ::getuid();
    QElapsedTimer clock;
    clock.start();

    const QStringList names = QDir(root).entryList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::NoSort);
    int looked = 0;
    for (const QString &name : names) {
        if (!isRuntimeDirName(name)) continue;
        // The cap and the budget are about startup, not about correctness: whatever is left over
        // is swept by the next start.
        if (looked >= maxDirectories || clock.elapsed() > kDefaultBudgetMs) break;
        ++looked;

        const QString path = root + QLatin1Char('/') + name;
        struct stat info {};
        if (!statNoFollow(path, &info)) { ++result.errors; continue; }
        // A symlink dressed up as a runtime directory, something that is not a directory at all,
        // another user's directory, or one whose mode is not the 0700 Relay creates: not ours.
        if (!S_ISDIR(info.st_mode)) continue;
        if (info.st_uid != uid) continue;
        if ((info.st_mode & 07777) != 0700) continue;
        // ...and it must really sit directly in the temp root, with no link in the middle.
        if (QFileInfo(path).canonicalFilePath() != path) continue;

        const Owner owner = readOwner(path);
        if (owner.isValid()) {
            // A marked directory is judged on its owner alone. This process's own directories are
            // marked with this process's pid, so they always land here.
            if (ownerAlive(owner)) { ++result.keptAlive; continue; }
        } else if (now - newestMtime(path, info) < graceSeconds) {
            // No mark: either an older Relay's directory, or one created a moment ago whose mark
            // is still being written. Age is the only way to tell, so give it the grace
            // period (a week by default; RuntimeDirs.h says why not less).
            ++result.keptYoung;
            continue;
        }
        if (removeTree(path)) ++result.removed;
        else ++result.errors;
    }
    return result;
#endif
}

}  // namespace runtimedirs
}  // namespace relay
