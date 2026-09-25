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

void writeBytes(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    file.write(bytes);
}

// A checkout-shaped data root (card #FYEY): backend/worker.py and one module, with the noise a
// real tree has. Returns the backend directory's path.
QString makeBackendTree(const QString &dataRoot, const QString &workerSource) {
    const QString backend = dataRoot + QStringLiteral("/backend");
    writeBytes(backend + QStringLiteral("/worker.py"), workerSource.toUtf8());
    writeBytes(backend + QStringLiteral("/relay_core/logs.py"), "pass\n");
    QDir().mkpath(backend + QStringLiteral("/relay_core"));
    writeBytes(backend + QStringLiteral("/relay_core/x.pyc"), "bytecode");
    QDir().mkpath(backend + QStringLiteral("/__pycache__"));
    writeBytes(backend + QStringLiteral("/.gitignore"), "noise\n");
    return backend;
}

// Point the pin store at this test's temp dir (card #FYEY), keeping the machine's real cache —
// and any unpinned override — out of the way for the test's lifetime.
class PinStore {
public:
    explicit PinStore(const QString &root) {
        qputenv("RELAY_BACKEND_PINS_HOME", QFile::encodeName(root));
        qunsetenv("RELAY_BACKEND_UNPINNED");
    }
    ~PinStore() {
        qunsetenv("RELAY_BACKEND_PINS_HOME");
        qunsetenv("RELAY_BACKEND_UNPINNED");
    }
};

}  // namespace

class RuntimeDirsTest : public QObject {
    Q_OBJECT

private slots:
    void restartWaitRequiresTheExactOldProcess() {
        const Owner running = self();
        QVERIFY(running.isValid());
        QVERIFY(running.startTime != 0);
        QVERIFY(!waitForExit(running, 75));   // a still-running Relay must not be reopened over
        Owner recycled = running;
        ++recycled.startTime;
        QVERIFY(waitForExit(recycled, 75));   // a reused pid is not the old Relay
        QVERIFY(!waitForExit({}, 75));        // no identity must never bypass the wait
    }

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

    // Card #057J: the guest event spool is listed only when it has changed. Every case the poll
    // meets — nothing there, an event written, an event taken away, the directory replaced.
    void aDirectoryStampReportsOnlyRealChanges() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString spool = root.path() + QStringLiteral("/guest-events");
        QVERIFY(QDir().mkpath(spool));
        DirStamp stamp;
        QVERIFY2(stamp.changed(spool), "the first look is always a change: nothing has been seen");
        QVERIFY2(!stamp.changed(spool), "an untouched directory must not be listed again");
        QVERIFY(!stamp.changed(spool));

        // An event arrives: a file created in a directory bumps that directory's mtime, which is
        // the whole basis of the gate. The poll sees it on its very next tick.
        QFile event(spool + QStringLiteral("/0001-statusline.json"));
        QVERIFY(event.open(QIODevice::WriteOnly));
        event.write("{}\n");
        event.close();
        QVERIFY(stamp.changed(spool));
        QVERIFY(!stamp.changed(spool));

        // pollGuestEvents() deletes each event it has handled, which is a change of its own — so
        // the tick after a burst looks again, and finds the rest of it.
        QVERIFY(QFile::remove(spool + QStringLiteral("/0001-statusline.json")));
        QVERIFY(stamp.changed(spool));
        QVERIFY(!stamp.changed(spool));

        // A pane given a fresh runtime directory: same path, different inode. The contents are
        // somebody else's, so they have to be read.
        QVERIFY(QDir(spool).removeRecursively());
        QVERIFY2(!stamp.changed(spool), "a directory that is not there is not a change");
        QVERIFY(QDir().mkpath(spool));
        QVERIFY(stamp.changed(spool));
        QVERIFY(!stamp.changed(spool));
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

    // --- the pinned backend store (card #FYEY) ----------------------------------------------
    //
    // Every test builds a checkout-shaped data root (backend/worker.py plus noise) and points the
    // pin store at its own temp dir; the machine's real cache is never touched here.

    // A pin is created once per hash and then reused: same path, same id, same inode — never a
    // second copy of an id that already has one.
    void aPinIsCreatedOncePerHashAndReused() {
        QTemporaryDir data;
        PinStore store(data.path() + QStringLiteral("/pins"));
        makeBackendTree(data.path(), QStringLiteral("def start(): pass\n"));
        const BackendPin first = pinBackend(data.path());
        QCOMPARE(first.hash.size(), 12);
        QCOMPARE(first.path, backendPinRoot() + QStringLiteral("/backend-") + first.hash
                                 + QStringLiteral("/backend"));
        QVERIFY(QFileInfo::exists(first.path + QStringLiteral("/worker.py")));
        // The noise of a working tree is not the code: none of it is pinned.
        QVERIFY(!QFileInfo::exists(first.path + QStringLiteral("/__pycache__")));
        QVERIFY(!QFileInfo::exists(first.path + QStringLiteral("/.gitignore")));
        QVERIFY(!QFileInfo::exists(first.path + QStringLiteral("/relay_core/x.pyc")));
        const QFileInfo pinned(first.path + QStringLiteral("/relay_core/logs.py"));
        QVERIFY(pinned.isFile());

        // Same tree, one spawn later: the same pin, not a new copy of it.
        const BackendPin again = pinBackend(data.path());
        QCOMPARE(again.hash, first.hash);
        QCOMPARE(again.path, first.path);
        // The reused pin's files are the same inodes — Qt 6 dropped QFileInfo::inode(), so ask
        // the file system directly.
        struct stat original {}, reused {};
        QVERIFY(::stat(QFile::encodeName(first.path + QStringLiteral("/relay_core/logs.py")).constData(), &original) == 0);
        QVERIFY(::stat(QFile::encodeName(again.path + QStringLiteral("/relay_core/logs.py")).constData(), &reused) == 0);
        QCOMPARE(original.st_ino, reused.st_ino);
        QCOMPARE(pinnedBackend(data.path()), first.path);
        // Exactly one pin in the store, named for its hash.
        const QStringList pins = QDir(backendPinRoot()).entryList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        QCOMPARE(pins, QStringList{QStringLiteral("backend-") + first.hash});
    }

    // The id tracks the code, not the clock or the noise: an editor that touches every file
    // changes nothing, and __pycache__ was never the program.
    void theHashTracksContentsNotMtimesOrNoise() {
        QTemporaryDir data;
        PinStore store(data.path() + QStringLiteral("/pins"));
        makeBackendTree(data.path(), QStringLiteral("def start(): pass\n"));
        const QString first = backendTreeHash(data.path());
        QCOMPARE(first.size(), 12);

        // Only mtimes move.
        setMtimes(data.path() + QStringLiteral("/backend"), 24 * 60 * 60);
        QCOMPARE(backendTreeHash(data.path()), first);

        // Bytecode churn under the tree.
        writeBytes(data.path() + QStringLiteral("/backend/__pycache__/worker.cpython-312.pyc"), "churn");
        writeBytes(data.path() + QStringLiteral("/backend/.hidden"), "noise");
        QCOMPARE(backendTreeHash(data.path()), first);

        // Contents change: a different id. The same contents elsewhere: the same id (that is
        // what makes a pin shareable and a test reproducible).
        writeBytes(data.path() + QStringLiteral("/backend/worker.py"),
                   "def start(): return 1\n");
        const QString second = backendTreeHash(data.path());
        QCOMPARE(second.size(), 12);
        QVERIFY(second != first);

        QTemporaryDir copy;
        QDir(copy.path()).mkpath(QStringLiteral("root"));
        writeBytes(copy.path() + QStringLiteral("/root/backend/worker.py"), "def start(): return 1\n");
        writeBytes(copy.path() + QStringLiteral("/root/backend/relay_core/logs.py"), "pass\n");
        QCOMPARE(backendTreeHash(copy.path() + QStringLiteral("/root")), second);
    }

    // RELAY_BACKEND_UNPINNED=1 (and nothing to pin) fails open onto the live tree, the way
    // things worked before pins existed: the caller cannot tell the difference except by the id.
    void unpinnedRunsTheLiveTree() {
        QTemporaryDir data;
        PinStore store(data.path() + QStringLiteral("/pins"));
        makeBackendTree(data.path(), QStringLiteral("def start(): pass\n"));
        qputenv("RELAY_BACKEND_UNPINNED", "1");
        const BackendPin unpinned = pinBackend(data.path());
        QCOMPARE(unpinned.path, data.path() + QStringLiteral("/backend"));
        QVERIFY(unpinned.hash.isEmpty());
        QCOMPARE(pinnedBackend(data.path()), data.path() + QStringLiteral("/backend"));
        qunsetenv("RELAY_BACKEND_UNPINNED");
        QVERIFY(QFileInfo::exists(backendPinRoot()) == false || QDir(backendPinRoot()).isEmpty());

        // No backend tree at all: same fail-open answer.
        QTemporaryDir empty;
        const BackendPin missing = pinBackend(empty.path());
        QCOMPARE(missing.path, empty.path() + QStringLiteral("/backend"));
        QVERIFY(missing.hash.isEmpty());
        QVERIFY(backendTreeHash(empty.path()).isEmpty());
    }

    // The sweep: a pin whose owner is dead goes once its grace is past, the current hash is
    // never taken out from under the spawn using it, and a live Relay's pin stays put.
    void deadOwnerPinsAreSweptTheCurrentOneNever() {
        QTemporaryDir data;
        PinStore store(data.path() + QStringLiteral("/pins"));
        makeBackendTree(data.path(), QStringLiteral("one\n"));
        const QString keep = pinBackend(data.path()).hash;   // owned by this very test process

        // A second, different tree: its pin, with the owner switched to a process that is gone.
        writeBytes(data.path() + QStringLiteral("/backend/worker.py"), "two\n");
        const BackendPin other = pinBackend(data.path());
        writeOwner(backendPinRoot() + QStringLiteral("/backend-") + other.hash, deadPid(), 7);

        // Dead but inside the grace: an orphaned worker may still be running from it.
        QCOMPARE(sweepBackendPins(keep, kDefaultGraceSeconds), 0);
        QVERIFY(QFileInfo::exists(other.path));
        // The grace past: gone — and never the current hash, however dead its owner is.
        writeOwner(backendPinRoot() + QStringLiteral("/backend-") + keep, deadPid(), 8);
        setMtimes(backendPinRoot() + QStringLiteral("/backend-") + other.hash, 2 * 60 * 60);
        setMtimes(backendPinRoot() + QStringLiteral("/backend-") + keep, 2 * 60 * 60);
        QCOMPARE(sweepBackendPins(keep, 60 * 60), 1);
        QVERIFY(!QFileInfo::exists(other.path));
        QVERIFY(QFileInfo::exists(backendPinRoot() + QStringLiteral("/backend-") + keep));

        // A live Relay's pin — the normal case, every pane of one Relay sharing its pid —
        // survives a sweep that is not told any current hash at all.
        markOwned(backendPinRoot() + QStringLiteral("/backend-") + keep);
        QCOMPARE(sweepBackendPins(QString(), 0), 0);
        QVERIFY(QFileInfo::exists(backendPinRoot() + QStringLiteral("/backend-") + keep));
    }
};

QTEST_MAIN(RuntimeDirsTest)
#include "runtimedirs_test.moc"
