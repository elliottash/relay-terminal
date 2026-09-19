// SPDX-License-Identifier: AGPL-3.0-or-later
// Private runtime directories: the owner mark, "is that Relay still running?", and the sweep that
// removes what a crash left in /tmp — without touching a live Relay's directories, a neighbour's
// files, or anything in /tmp that is not Relay's (issue 9JYK).
//
// Every test builds its own fake temp root in a QTemporaryDir. The real /tmp is never swept here.
#include "RuntimeDirs.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace relay::runtimedirs;

namespace {

// A runtime directory as Relay makes one: mode 0700, with a file in it.
QString makeDir(const QString &root, const QString &name, int mode = 0700) {
    const QString path = root + QLatin1Char('/') + name;
    QDir().mkpath(path);
    QFile state(path + QStringLiteral("/state.json"));
    state.open(QIODevice::WriteOnly);
    state.write("{}\n");
    state.close();
    ::chmod(QFile::encodeName(path).constData(), mode_t(mode));
    return path;
}

// An owner file naming a pid and a start time, written the plain way (the sweep only reads).
void writeOwner(const QString &dir, qint64 pid, qulonglong startTime) {
    QFile file(dir + QStringLiteral("/owner"));
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    file.write("relay-owner 1\npid " + QByteArray::number(pid) + "\nstarttime "
               + QByteArray::number(startTime) + "\n");
}

// A pid that is certainly not running: allocate one, let it exit, reap it. The kernel will not
// hand the number out again while this test runs.
qint64 deadPid() {
    const pid_t child = ::fork();
    if (child == 0) ::_exit(0);
    int status = 0;
    ::waitpid(child, &status, 0);
    return qint64(child);
}

// Age a directory and everything directly in it, which is what the sweep looks at when there is
// no owner file to go on.
void setMtimes(const QString &dir, qint64 secondsAgo) {
    const time_t when = time_t(QDateTime::currentSecsSinceEpoch() - secondsAgo);
    struct timeval times[2];
    times[0].tv_sec = when; times[0].tv_usec = 0;   // atime
    times[1].tv_sec = when; times[1].tv_usec = 0;   // mtime
    for (const QString &name : QDir(dir).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot))
        ::utimes(QFile::encodeName(dir + QLatin1Char('/') + name).constData(), times);
    ::utimes(QFile::encodeName(dir).constData(), times);
}

}  // namespace

class RuntimeDirsTest : public QObject {
    Q_OBJECT

private slots:
    void namesAreQtsOwnAlphabet() {
        QVERIFY(isRuntimeDirName(QStringLiteral("relay-aB3xyZ")));
        QVERIFY(isRuntimeDirName(QStringLiteral("relay-open-000000")));
        // Everything else in /tmp is somebody else's business.
        QVERIFY(!isRuntimeDirName(QStringLiteral("relay-qa-wrong-mode")));
        QVERIFY(!isRuntimeDirName(QStringLiteral("relay-abc")));
        QVERIFY(!isRuntimeDirName(QStringLiteral("relay-open-toolongname")));
        QVERIFY(!isRuntimeDirName(QStringLiteral("relayXXXXXX")));
        QVERIFY(!isRuntimeDirName(QStringLiteral("relay-abc-12")));
        QVERIFY(!isRuntimeDirName(QStringLiteral("relay-2026-shots")));
        QVERIFY(!isRuntimeDirName(QStringLiteral("notrelay-abcdef")));
    }

    void markAndReadBackTheOwner() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString dir = makeDir(root.path(), QStringLiteral("relay-aaaaaa"));
        QVERIFY(markOwned(dir));

        const Owner owner = readOwner(dir);
        QCOMPARE(owner.pid, qint64(::getpid()));
        QCOMPARE(owner.startTime, self().startTime);
        QVERIFY(owner.startTime > 0);
        QVERIFY(ownerAlive(dir));
        // 0600: the mark says which process holds the directory and nobody else needs to read or
        // write it. (Qt reports the owner bits twice on Unix, as Owner and as User.)
        const QFile::Permissions mode = QFileInfo(dir + QStringLiteral("/owner")).permissions();
        QVERIFY(mode.testFlag(QFile::ReadOwner) && mode.testFlag(QFile::WriteOwner));
        QVERIFY(!(mode & (QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup
                          | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther)));
    }

    void aRecycledPidIsNotTheSameProcess() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString dir = makeDir(root.path(), QStringLiteral("relay-bbbbbb"));
        // This process's own pid, but the start time of a process that is not it: exactly what a
        // recycled pid looks like, and the reason the mark carries more than a number.
        writeOwner(dir, qint64(::getpid()), self().startTime + 1);
        QVERIFY(!ownerAlive(dir));

        writeOwner(dir, deadPid(), 12345);
        QVERIFY(!ownerAlive(dir));

        // An unmarked or half-written mark is not "alive" either.
        QVERIFY(QFile::remove(dir + QStringLiteral("/owner")));
        QVERIFY(!ownerAlive(dir));
        QVERIFY(!readOwner(dir).isValid());
    }

    void deadOwnersGoAndLiveOnesStay() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString live = makeDir(root.path(), QStringLiteral("relay-live00"));
        const QString liveSocket = makeDir(root.path(), QStringLiteral("relay-open-live01"));
        QVERIFY(markOwned(live));
        QVERIFY(markOwned(liveSocket));
        const QString gone = makeDir(root.path(), QStringLiteral("relay-dead00"));
        writeOwner(gone, deadPid(), 4242);
        const QString recycled = makeDir(root.path(), QStringLiteral("relay-open-dead01"));
        writeOwner(recycled, qint64(::getpid()), self().startTime + 7);

        const SweepResult result = sweep(root.path());
        QCOMPARE(result.removed, 2);
        QCOMPARE(result.keptAlive, 2);
        QCOMPARE(result.keptYoung, 0);
        QCOMPARE(result.errors, 0);
        // The sweeping process's own directories survive a sweep, always.
        QVERIFY(QFileInfo(live).isDir());
        QVERIFY(QFileInfo(liveSocket).isDir());
        QVERIFY(!QFileInfo::exists(gone));
        QVERIFY(!QFileInfo::exists(recycled));
    }

    void unmarkedDirectoriesWaitOutTheGracePeriod() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString old = makeDir(root.path(), QStringLiteral("relay-old000"));
        const QString young = makeDir(root.path(), QStringLiteral("relay-new000"));
        setMtimes(old, 3 * 60 * 60);   // three hours ago

        // A one-hour grace: the old one is rubbish from a build that never marked anything, the
        // young one could be a Relay that is one millisecond from writing its mark.
        const SweepResult result = sweep(root.path(), 60 * 60);
        QCOMPARE(result.removed, 1);
        QCOMPARE(result.keptYoung, 1);
        QVERIFY(!QFileInfo::exists(old));
        QVERIFY(QFileInfo(young).isDir());

        // With the default grace neither is old enough.
        const SweepResult again = sweep(root.path());
        QCOMPARE(again.removed, 0);
        QCOMPARE(again.keptYoung, 1);
        QVERIFY(QFileInfo(young).isDir());
    }

    void nonMatchingNamesAndModesAreNeverTouched() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QStringList bystanders{QStringLiteral("relay-qa-wrong-mode"), QStringLiteral("relay-abc"),
                                     QStringLiteral("relay-open-toolongname"), QStringLiteral("relayXXXXXX"),
                                     QStringLiteral("relay-2026-09-18-shots")};
        for (const QString &name : bystanders) setMtimes(makeDir(root.path(), name), 10 * 24 * 60 * 60);
        // A Relay-shaped name, but not a mode Relay ever creates: somebody else's directory.
        const QString wrongMode = makeDir(root.path(), QStringLiteral("relay-loose0"), 0755);
        setMtimes(wrongMode, 10 * 24 * 60 * 60);
        QFile loose(root.path() + QStringLiteral("/relay-file00"));   // a file, not a directory
        loose.open(QIODevice::WriteOnly);
        loose.close();

        const SweepResult result = sweep(root.path());
        QCOMPARE(result.removed, 0);
        QCOMPARE(result.errors, 0);
        for (const QString &name : bystanders) QVERIFY(QFileInfo(root.path() + QLatin1Char('/') + name).isDir());
        QVERIFY(QFileInfo(wrongMode).isDir());
        QVERIFY(QFileInfo::exists(root.path() + QStringLiteral("/relay-file00")));
    }

    void aSymlinkNamedLikeARuntimeDirectoryIsNotFollowed() {
        QTemporaryDir root, elsewhere;
        QVERIFY(root.isValid() && elsewhere.isValid());
        const QString target = elsewhere.path() + QStringLiteral("/precious");
        QDir().mkpath(target);
        QFile inside(target + QStringLiteral("/keepme.txt"));
        inside.open(QIODevice::WriteOnly);
        inside.write("keep\n");
        inside.close();
        QVERIFY(QFile::link(target, root.path() + QStringLiteral("/relay-link00")));

        const SweepResult result = sweep(root.path(), 0 /* nothing is too young */);
        QCOMPARE(result.removed, 0);
        QVERIFY(QFileInfo::exists(root.path() + QStringLiteral("/relay-link00")));
        QVERIFY(QFileInfo::exists(target + QStringLiteral("/keepme.txt")));
    }

    void aSymlinkInsideADeadDirectoryLeavesItsTargetAlone() {
        QTemporaryDir root, elsewhere;
        QVERIFY(root.isValid() && elsewhere.isValid());
        QFile outside(elsewhere.path() + QStringLiteral("/outside.txt"));
        outside.open(QIODevice::WriteOnly);
        outside.write("still here\n");
        outside.close();
        const QString outsideDir = elsewhere.path() + QStringLiteral("/outside-dir");
        QDir().mkpath(outsideDir);
        QFile deeper(outsideDir + QStringLiteral("/deeper.txt"));
        deeper.open(QIODevice::WriteOnly);
        deeper.close();

        const QString dead = makeDir(root.path(), QStringLiteral("relay-dead02"));
        writeOwner(dead, deadPid(), 9);
        QVERIFY(QFile::link(elsewhere.path() + QStringLiteral("/outside.txt"), dead + QStringLiteral("/link.txt")));
        QVERIFY(QFile::link(outsideDir, dead + QStringLiteral("/link-dir")));

        const SweepResult result = sweep(root.path());
        QCOMPARE(result.removed, 1);
        QCOMPARE(result.errors, 0);
        QVERIFY(!QFileInfo::exists(dead));
        // The links went; what they pointed at did not.
        QVERIFY(QFileInfo::exists(elsewhere.path() + QStringLiteral("/outside.txt")));
        QVERIFY(QFileInfo(outsideDir).isDir());
        QVERIFY(QFileInfo::exists(outsideDir + QStringLiteral("/deeper.txt")));
    }

    void nestedFilesInsideADeadDirectoryGoWithIt() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString dead = makeDir(root.path(), QStringLiteral("relay-dead03"));
        writeOwner(dead, deadPid(), 11);
        QVERIFY(QDir().mkpath(dead + QStringLiteral("/sub/deeper")));
        QFile file(dead + QStringLiteral("/sub/deeper/input.txt"));
        file.open(QIODevice::WriteOnly);
        file.write("x\n");
        file.close();

        const SweepResult result = sweep(root.path());
        QCOMPARE(result.removed, 1);
        QCOMPARE(result.errors, 0);
        QVERIFY(!QFileInfo::exists(dead));
    }

    void theCapBoundsTheWorkPerSweep() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const qint64 dead = deadPid();
        for (int i = 0; i < 6; ++i) {
            const QString dir = makeDir(root.path(), QStringLiteral("relay-cap%1").arg(i, 3, 10, QLatin1Char('0')));
            writeOwner(dir, dead, 33);
        }
        // Three per sweep: a /tmp with thousands of leftovers must not hold up a start.
        QCOMPARE(sweep(root.path(), kDefaultGraceSeconds, 3).removed, 3);
        QCOMPARE(sweep(root.path(), kDefaultGraceSeconds, 3).removed, 3);
        const SweepResult empty = sweep(root.path(), kDefaultGraceSeconds, 3);
        QCOMPARE(empty.removed, 0);
        QCOMPARE(QDir(root.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 0);
    }

    void aMissingOrUnreadableRootIsHarmless() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const SweepResult missing = sweep(root.path() + QStringLiteral("/not-here"));
        QCOMPARE(missing.removed, 0);
        QCOMPARE(missing.errors, 0);
        // An empty root is simply an empty result.
        const SweepResult none = sweep(root.path());
        QCOMPARE(none.removed + none.keptAlive + none.keptYoung + none.errors, 0);
        // And a directory that cannot be marked still reads back as unowned rather than crashing.
        QVERIFY(!markOwned(root.path() + QStringLiteral("/not-here")));
    }
};

QTEST_MAIN(RuntimeDirsTest)
#include "runtimedirs_test.moc"
