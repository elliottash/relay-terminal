// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The `@` file picker's index: every file the composer may attach in one directory, plus the set
// git calls changed (which the picker ranks higher).
//
// It used to be built by three blocking `git` calls on the GUI thread — `rev-parse`, `ls-files`
// and `status`, each with its own waitForFinished — so the first `@` typed in a large monorepo
// froze the whole window for as long as five seconds. Nothing here waits: refresh() starts the
// chain and returns, each step lands on a QProcess signal, and the caller is told through
// `updated` when there is more to show. The results already in hand stay readable while a refresh
// runs, so the popup never blinks empty just because the index went stale.
//
// Plain QtCore, no window, so the whole chain can be driven from a test (tests/fileindex_test.cpp).
#include <QElapsedTimer>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

class QDirIterator;
class QProcess;

namespace relay {

class FileIndex final : public QObject {
    Q_OBJECT
public:
    static constexpr int kFreshMs = 15000;       // an index younger than this is reused as it is
    // How long each git step may take. The blocking version allowed 1 s, 2.5 s and 1.5 s because
    // the window was frozen for every one of them. Nothing waits on these any more, and the
    // repositories this was made asynchronous for are the ones where `ls-files --others` takes
    // longer than that: killing it early left their picker empty for good. These only stop a git
    // that is truly stuck (a dead network mount) from being left running.
    static constexpr int kToplevelTimeoutMs = 5000, kListTimeoutMs = 60000, kStatusTimeoutMs = 60000;
    static constexpr int kMaxFiles = 20000;      // a repository listing longer than this is noise
    static constexpr int kMaxWalkFiles = 5000;   // the non-git walk gives up far sooner
    static constexpr int kWalkSliceEntries = 400;    // entries per event-loop slice while walking
    static constexpr int kMaxWalkEntries = 100000;   // and a hard stop for a tree of hidden files

    explicit FileIndex(QObject *parent = nullptr);
    ~FileIndex() override;

    // Build the index for `cwd`, unless it is already fresh or already being built. Returns
    // immediately in every case; the work happens on the event loop.
    void refresh(const QString &cwd);
    // Forget how fresh the index is, so the next refresh() really asks again. The results stay.
    void expire() { m_age.invalidate(); }

    const QStringList &files() const { return m_files; }          // absolute paths
    const QSet<QString> &changedFiles() const { return m_changed; }
    bool isLoading() const { return m_loading; }
    QString cwd() const { return m_cwd; }

    // Called on the GUI thread whenever new results land, including the partial ones.
    std::function<void()> updated;

private:
    using Step = std::function<void(const QByteArray &output)>;
    // Run one `git` invocation; `then` is called with its standard output, or with nothing when it
    // fails, is killed by its timeout, or belongs to a superseded refresh.
    void run(const QStringList &args, int timeoutMs, Step then);
    void gotToplevel(const QByteArray &output);
    void gotFiles(const QString &root, const QByteArray &output);
    void gotStatus(const QString &root, const QByteArray &output);
    void startWalk();
    void walkSlice();
    void publish(bool complete);
    void cancel();

    QStringList m_files, m_next;
    QSet<QString> m_changed, m_nextChanged;
    QString m_cwd;
    QElapsedTimer m_age;
    QProcess *m_process = nullptr;
    std::unique_ptr<QDirIterator> m_walk;
    int m_walked = 0;
    // Bumped whenever a refresh is superseded, so a late answer from the old directory cannot
    // overwrite the new one's index.
    int m_generation = 0;
    bool m_loading = false;
};

}  // namespace relay
