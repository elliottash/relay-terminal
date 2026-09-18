// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileIndex.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

using relay::FileIndex;

namespace {

// The test drives git itself, where blocking is fine; only FileIndex is under the no-waiting rule.
bool git(const QString &cwd, const QStringList &args) {
    QProcess process;
    process.setWorkingDirectory(cwd);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(QStringLiteral("git"), args);
    return process.waitForFinished(20000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool writeFile(const QString &path, const QString &text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    QTextStream(&file) << text;
    return true;
}

}  // namespace

class FileIndexTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        if (QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty())
            QSKIP("git is not installed");
        // The child git processes inherit this: no user or system configuration, so a global
        // excludesfile or a signing key cannot change what the index sees.
        qputenv("GIT_CONFIG_GLOBAL", "/dev/null");
        qputenv("GIT_CONFIG_SYSTEM", "/dev/null");
        qputenv("GIT_AUTHOR_NAME", "Relay Test");
        qputenv("GIT_AUTHOR_EMAIL", "test@example.invalid");
        qputenv("GIT_COMMITTER_NAME", "Relay Test");
        qputenv("GIT_COMMITTER_EMAIL", "test@example.invalid");

        QVERIFY(m_repo.isValid());
        // /tmp is a symlink on some systems and `git rev-parse` answers with the real path.
        m_root = QFileInfo(m_repo.path()).canonicalFilePath();
        QVERIFY(git(m_root, {QStringLiteral("init"), QStringLiteral("-q")}));
        QVERIFY(writeFile(QDir(m_root).filePath(QStringLiteral(".gitignore")), QStringLiteral("ignored.log\n")));
        QVERIFY(writeFile(QDir(m_root).filePath(QStringLiteral("tracked.txt")), QStringLiteral("one\n")));
        QVERIFY(writeFile(QDir(m_root).filePath(QStringLiteral("ignored.log")), QStringLiteral("noise\n")));
        QVERIFY(git(m_root, {QStringLiteral("add"), QStringLiteral(".gitignore"), QStringLiteral("tracked.txt")}));
        QVERIFY(git(m_root, {QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("first")}));
        // Now one untracked file and one modification, so both halves of the listing have content.
        QVERIFY(writeFile(QDir(m_root).filePath(QStringLiteral("untracked.txt")), QStringLiteral("two\n")));
        QVERIFY(writeFile(QDir(m_root).filePath(QStringLiteral("tracked.txt")), QStringLiteral("one changed\n")));

        QVERIFY(m_plain.isValid());
        m_plainRoot = QFileInfo(m_plain.path()).canonicalFilePath();
        QVERIFY(QDir(m_plainRoot).mkdir(QStringLiteral("sub")));
        QVERIFY(writeFile(QDir(m_plainRoot).filePath(QStringLiteral("sub/loose.txt")), QStringLiteral("three\n")));
        QVERIFY(writeFile(QDir(m_plainRoot).filePath(QStringLiteral(".hidden")), QStringLiteral("four\n")));
    }

    void listsTrackedAndUntrackedButNotIgnored() {
        FileIndex index;
        int updates = 0;
        index.updated = [&updates] { ++updates; };
        index.refresh(m_root);
        QTRY_VERIFY(!index.isLoading());
        QVERIFY(updates > 0);
        QCOMPARE(index.cwd(), m_root);
        QVERIFY(index.files().contains(QDir(m_root).filePath(QStringLiteral("tracked.txt"))));
        QVERIFY(index.files().contains(QDir(m_root).filePath(QStringLiteral("untracked.txt"))));
        QVERIFY(!index.files().contains(QDir(m_root).filePath(QStringLiteral("ignored.log"))));
        // The modified file is what the picker floats to the top.
        QVERIFY(index.changedFiles().contains(QDir(m_root).filePath(QStringLiteral("tracked.txt"))));
        QVERIFY(!index.changedFiles().contains(QDir(m_root).filePath(QStringLiteral(".gitignore"))));
    }

    void aFreshIndexIsNotRebuilt() {
        FileIndex index;
        index.refresh(m_root);
        QTRY_VERIFY(!index.isLoading());
        const QStringList first = index.files();
        int updates = 0;
        index.updated = [&updates] { ++updates; };
        index.refresh(m_root);
        QVERIFY(!index.isLoading());          // nothing started
        QTest::qWait(150);
        QCOMPARE(updates, 0);
        QCOMPARE(index.files(), first);
    }

    void aNewDirectorySupersedesTheOneInFlight() {
        FileIndex index;
        index.refresh(m_root);
        index.refresh(m_plainRoot);           // mid-flight: the repository answer must be dropped
        QCOMPARE(index.cwd(), m_plainRoot);
        QTRY_VERIFY(!index.isLoading());
        QTest::qWait(200);                    // long enough for a stale answer to arrive late
        QCOMPARE(index.cwd(), m_plainRoot);
        for (const QString &path : index.files()) QVERIFY(!path.startsWith(m_root));
        QVERIFY(index.changedFiles().isEmpty());
    }

    void aPlainDirectoryUsesTheWalk() {
        FileIndex index;
        index.refresh(m_plainRoot);
        QTRY_VERIFY(!index.isLoading());
        QVERIFY(index.files().contains(QDir(m_plainRoot).filePath(QStringLiteral("sub/loose.txt"))));
        QVERIFY(!index.files().contains(QDir(m_plainRoot).filePath(QStringLiteral(".hidden"))));
    }

    // The point of the exercise: a git that takes seconds must not hold up the caller for even a
    // frame. A stub on PATH sleeps far longer than the step's timeout.
    void refreshReturnsBeforeGitDoes() {
        QTemporaryDir stub;
        QVERIFY(stub.isValid());
        const QString script = QDir(stub.path()).filePath(QStringLiteral("git"));
        QVERIFY(writeFile(script, QStringLiteral("#!/bin/sh\nsleep 2\n")));
        QVERIFY(QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", stub.path().toLocal8Bit() + ':' + path);

        FileIndex index;
        QElapsedTimer clock;
        clock.start();
        index.refresh(m_root);
        const qint64 elapsed = clock.elapsed();
        QVERIFY(index.isLoading());
        // The old code blocked here for up to five seconds.
        QVERIFY2(elapsed < 100, qPrintable(QStringLiteral("refresh() blocked for %1 ms").arg(elapsed)));
        // It still finishes: the first step is killed by its timeout and the walk takes over.
        QTRY_VERIFY_WITH_TIMEOUT(!index.isLoading(), 10000);
        QVERIFY(index.changedFiles().isEmpty());   // no git answered, so nothing is "changed"
        qputenv("PATH", path);
    }

    // A listing that fails (a git that errors, or one killed by its timeout) must not blank a
    // picker that already has this directory's files.
    void aFailedListingKeepsWhatWasThere() {
        FileIndex index;
        index.refresh(m_root);
        QTRY_VERIFY_WITH_TIMEOUT(!index.isLoading(), 10000);
        const QStringList before = index.files();
        const QSet<QString> changedBefore = index.changedFiles();
        QVERIFY(!before.isEmpty());

        QTemporaryDir stub;
        QVERIFY(stub.isValid());
        const QString script = QDir(stub.path()).filePath(QStringLiteral("git"));
        // Knows where the repository is, then fails at everything else.
        QVERIFY(writeFile(script, QStringLiteral("#!/bin/sh\ncase \"$*\" in *show-toplevel*) echo '%1';; *) exit 1;; esac\n").arg(m_root)));
        QVERIFY(QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", stub.path().toLocal8Bit() + ':' + path);
        index.expire();
        index.refresh(m_root);
        QVERIFY(index.isLoading());
        QTRY_VERIFY_WITH_TIMEOUT(!index.isLoading(), 10000);
        qputenv("PATH", path);
        QCOMPARE(index.files(), before);
        QCOMPARE(index.changedFiles(), changedBefore);
    }

private:
    QTemporaryDir m_repo, m_plain;
    QString m_root, m_plainRoot;
};

QTEST_MAIN(FileIndexTest)
#include "fileindex_test.moc"
