// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FileIndex.h"

#include <QDir>
#include <QDirIterator>
#include <QProcess>
#include <QTimer>

namespace relay {

FileIndex::FileIndex(QObject *parent) : QObject(parent) {}

FileIndex::~FileIndex() {
    QProcess *process = m_process;
    cancel();
    // Nothing will spin the event loop again, so the child killed just above is reaped here
    // rather than left behind as a zombie. It has had SIGKILL; this returns immediately.
    if (process) process->waitForFinished(100);
}

void FileIndex::refresh(const QString &cwd) {
    if (cwd == m_cwd && (m_loading || (m_age.isValid() && m_age.elapsed() < kFreshMs))) return;
    const bool elsewhere = cwd != m_cwd;
    cancel();
    m_cwd = cwd;
    m_loading = true;
    m_next.clear();
    m_nextChanged.clear();
    m_walked = 0;
    if (elsewhere) {
        // Another directory's files are not stale results, they are the wrong ones.
        m_files.clear();
        m_changed.clear();
        m_age.invalidate();
    }
    run({QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")}, kToplevelTimeoutMs,
        [this](const QByteArray &output) { gotToplevel(output); });
}

void FileIndex::cancel() {
    ++m_generation;
    if (m_process) {
        QProcess *process = m_process;
        m_process = nullptr;
        // Its answer is for a directory nobody is looking at any more, so it is dropped rather
        // than parsed. The object outlives the kill on purpose: deleting a QProcess whose child
        // has not been reaped yet is what prints "Destroyed while process is still running".
        process->disconnect();
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process, &QObject::deleteLater);
        process->kill();
    }
    m_walk.reset();
    m_loading = false;
}

void FileIndex::run(const QStringList &args, int timeoutMs, Step then) {
    const int generation = m_generation;
    auto *process = new QProcess(this);
    m_process = process;
    process->setWorkingDirectory(m_cwd);
    // git is chatty on stderr in a broken repository; only the listing interests us.
    process->setStandardErrorFile(QProcess::nullDevice());

    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, process, [process] { process->kill(); });

    auto done = [this, generation, process, then](bool ok) {
        // A failed step hands on a null array and a successful one never does, so "git said there
        // is nothing" and "git did not answer" stay different things to the step that follows.
        QByteArray output;
        if (ok) {
            output = process->readAllStandardOutput();
            if (output.isNull()) output = QByteArray("");   // empty, but an answer
        }
        if (m_process == process) m_process = nullptr;
        process->deleteLater();
        if (generation != m_generation) return;
        then(output);
    };
    // A process that never starts emits errorOccurred and no finished; anything else (including
    // the kill above) ends in finished, so only the missing-binary case is handled here.
    connect(process, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) done(false);
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [done](int code, QProcess::ExitStatus status) { done(status == QProcess::NormalExit && code == 0); });
    process->start(QStringLiteral("git"), args);
    timer->start(timeoutMs);
}

void FileIndex::gotToplevel(const QByteArray &output) {
    const QString root = QString::fromUtf8(output).trimmed();
    if (root.isEmpty()) { startWalk(); return; }   // not a repository, or git is not installed
    // Tracked plus untracked-but-not-ignored files, relative to the repository root.
    run({QStringLiteral("-C"), root, QStringLiteral("ls-files"), QStringLiteral("--cached"), QStringLiteral("--others"),
         QStringLiteral("--exclude-standard"), QStringLiteral("-z")},
        kListTimeoutMs, [this, root](const QByteArray &files) { gotFiles(root, files); });
}

void FileIndex::gotFiles(const QString &root, const QByteArray &output) {
    if (output.isNull()) {
        // The listing failed or timed out. What the picker already shows for this directory is
        // better than nothing, so it stays, and the next refresh tries again.
        m_next = m_files;
        m_nextChanged = m_changed;
        publish(true);
        return;
    }
    const QDir base(root);
    for (const QByteArray &file : output.split('\0')) {
        if (file.isEmpty()) continue;
        m_next.append(base.filePath(QString::fromUtf8(file)));
        if (m_next.size() >= kMaxFiles) break;
    }
    publish(false);   // the names are worth showing before the changed set arrives
    run({QStringLiteral("-C"), root, QStringLiteral("status"), QStringLiteral("--porcelain"), QStringLiteral("-z")},
        kStatusTimeoutMs, [this, root](const QByteArray &status) { gotStatus(root, status); });
}

void FileIndex::gotStatus(const QString &root, const QByteArray &output) {
    if (output.isNull()) m_nextChanged = m_changed;   // no answer: keep the changed set we had
    const QDir base(root);
    const QList<QByteArray> entries = output.split('\0');
    for (int i = 0; i < entries.size(); ++i) {
        const QByteArray &entry = entries.at(i);
        if (entry.size() <= 3) continue;   // "XY " and a path
        m_nextChanged.insert(base.filePath(QString::fromUtf8(entry.mid(3))));
        // A rename or a copy is followed by its original path in a field of its own, with no
        // status bytes in front of it; skipping it keeps a truncated name out of the set.
        if (entry.at(0) == 'R' || entry.at(0) == 'C' || entry.at(1) == 'R' || entry.at(1) == 'C') ++i;
    }
    publish(true);
}

void FileIndex::startWalk() {
    m_walk = std::make_unique<QDirIterator>(m_cwd, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    walkSlice();
}

// The walk is the same one the blocking version did, cut into slices: a directory tree deep enough
// to matter is also deep enough to stutter the window, so each slice stops after a few hundred
// entries and the next one comes back through the event loop.
void FileIndex::walkSlice() {
    const int generation = m_generation;
    const QDir base(m_cwd);
    int entries = 0;
    while (m_walk->hasNext() && m_next.size() < kMaxWalkFiles && m_walked < kMaxWalkEntries
           && entries++ < kWalkSliceEntries) {
        ++m_walked;
        const QString path = m_walk->next();
        const QString rel = base.relativeFilePath(path);
        if (rel.startsWith('.') || rel.contains(QStringLiteral("/.")) || rel.contains(QStringLiteral("node_modules/"))) continue;
        m_next.append(path);
    }
    if (!m_walk->hasNext() || m_next.size() >= kMaxWalkFiles || m_walked >= kMaxWalkEntries) {
        m_walk.reset();
        publish(true);
        return;
    }
    publish(false);
    QTimer::singleShot(0, this, [this, generation] {
        if (generation == m_generation && m_walk) walkSlice();
    });
}

void FileIndex::publish(bool complete) {
    m_files = m_next;
    m_changed = m_nextChanged;
    if (complete) {
        m_age.start();
        m_loading = false;
    }
    if (updated) updated();
}

}  // namespace relay
