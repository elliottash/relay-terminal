// SPDX-License-Identifier: AGPL-3.0-or-later
// A pane's CPU / memory share (issue #D03W): the arithmetic on Readings a test can make up —
// percentages over an interval, rounding, when a meter is worth showing, tab-level sums, the
// label suffixes and the per-process breakdown — plus the tree walk, driven over a /proc this
// test builds out of directories and over the real one for this process.
#include "PaneUsage.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

#include <unistd.h>

using namespace relay::usage;

namespace {

// A /proc this test writes: one directory per process, with the stat and statm fields the walk
// reads and a task/<tid>/children per thread. It is how a process spawned from a non-main thread
// can be arranged on purpose, which is not something a test can ask the real kernel for.
class FakeProc {
public:
    bool ok() const { return m_dir.isValid(); }
    QString path() const { return m_dir.path(); }

    // `threads` maps a thread id to the pids that thread is the parent of. The main thread (tid
    // == pid) is always present, with whatever it was given.
    void add(qint64 pid, const QString &comm, qint64 ppid, qint64 ticks, qint64 startTicks,
             qint64 rssPages, const QMap<qint64, QList<qint64>> &threads = {})
    {
        QDir root(m_dir.path());
        root.mkpath(QStringLiteral("%1/task").arg(pid));
        // utime and stime are fields 14 and 15, cutime and cstime 16 and 17, starttime 22 —
        // indices 11..14 and 19 once the fields are counted after the last ')'.
        QStringList fields;
        for (int i = 0; i < 20; ++i) fields << QStringLiteral("0");
        fields[0] = QStringLiteral("S");
        fields[1] = QString::number(ppid);
        fields[11] = QString::number(ticks);   // all of it as utime; the parse sums four fields
        fields[19] = QString::number(startTicks);
        write(QStringLiteral("%1/stat").arg(pid),
              QStringLiteral("%1 (%2) %3\n").arg(pid).arg(comm, fields.join(QLatin1Char(' '))));
        write(QStringLiteral("%1/statm").arg(pid), QStringLiteral("9999 %1 0 0 0 0 0\n").arg(rssPages));
        QMap<qint64, QList<qint64>> all = threads;
        if (!all.contains(pid)) all.insert(pid, {});
        for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
            root.mkpath(QStringLiteral("%1/task/%2").arg(pid).arg(it.key()));
            QStringList kids;
            for (qint64 child : it.value()) kids << QString::number(child);
            write(QStringLiteral("%1/task/%2/children").arg(pid).arg(it.key()),
                  kids.join(QLatin1Char(' ')) + QStringLiteral(" "));
        }
    }

private:
    void write(const QString &relative, const QString &text)
    {
        QFile file(m_dir.path() + QLatin1Char('/') + relative);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.fileName()));
        file.write(text.toUtf8());
    }
    QTemporaryDir m_dir;
};

// Points relay::usage at a made-up /proc and puts the real one back, whatever the test does.
class ProcRootFor {
public:
    explicit ProcRootFor(const QString &root) : m_previous(procRoot()) { setProcRoot(root); }
    ~ProcRootFor() { setProcRoot(m_previous); }

private:
    QString m_previous;
};

// A process the meter can be fed without a /proc at all.
ProcessInfo proc(qint64 pid, const QString &comm, qint64 ticks, qint64 startTicks, qint64 rssBytes)
{
    ProcessInfo info;
    info.pid = pid;
    info.comm = comm;
    info.ticks = ticks;
    info.startTicks = startTicks;
    info.rssBytes = rssBytes;
    info.state = 'R';
    return info;
}

}  // namespace

class PaneUsageTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void cpuIsCoresOverTheInterval() {
        // 200 ticks at 100 ticks/second is 2 CPU-seconds; over 1 second of wall clock that is
        // two cores busy: the whole of a 2-core machine, half of a 4-core one.
        QCOMPARE(cpuPercentOf(200, 1000, 2, 100), 100.0);
        QCOMPARE(cpuPercentOf(200, 1000, 4, 100), 50.0);
        // Two panes each burning one core of four.
        QCOMPARE(cpuPercentOf(100, 1000, 4, 100), 25.0);
    }

    void cpuClampsAndGuards() {
        QCOMPARE(cpuPercentOf(-50, 1000, 4, 100), 0.0);     // a tree that shrank (pids gone)
        QCOMPARE(cpuPercentOf(100000, 1000, 4, 100), 100.0); // scheduling jitter cannot oversell
        QCOMPARE(cpuPercentOf(100, 0, 4, 100), 0.0);
        QCOMPARE(cpuPercentOf(100, 1000, 0, 100), 0.0);
        QCOMPARE(cpuPercentOf(100, 1000, 4, 0), 0.0);
    }

    void ramIsAFractionOfTheMachine() {
        const qint64 gib = qint64(1) << 30;
        QCOMPARE(ramPercentOf(8 * gib, 16 * gib), 50.0);
        QCOMPARE(ramPercentOf(0, 16 * gib), 0.0);
        QCOMPARE(ramPercentOf(-1, 16 * gib), 0.0);
        QCOMPARE(ramPercentOf(1, 0), 0.0);
        QCOMPARE(ramPercentOf(64 * gib, 16 * gib), 100.0);
    }

    void roundsToWholePercents() {
        QCOMPARE(formatPercent(12.4), QStringLiteral("12"));
        QCOMPARE(formatPercent(12.6), QStringLiteral("13"));
        QCOMPARE(formatPercent(0.4), QStringLiteral("0"));
        QCOMPARE(formatPercent(99.9), QStringLiteral("100"));
    }

    void quietPanesShowNothing() {
        const qint64 mib = 1 << 20;
        Sample idle;
        idle.valid = true;
        QVERIFY(!worthShowing(idle));
        Sample shell;
        shell.valid = true; shell.cpuPercent = 0.02; shell.ramPercent = 0.03;
        QVERIFY(!worthShowing(shell));
        Sample busy;
        busy.valid = true; busy.cpuPercent = 0.6;
        QVERIFY(worthShowing(busy));
        QVERIFY(showsCpu(busy));
        QVERIFY(!showsMemory(busy));
        // An idle agent pane: the worker holds 60 MB and will hold it all day. That is over half
        // a percent of a small machine, which used to pin the chip on reading "0% / 1%" — the
        // memory floor is 256 MiB of real memory, not a share of whatever this machine has.
        Sample worker;
        worker.valid = true; worker.ramBytes = 60 * mib; worker.ramPercent = 1.4;
        QVERIFY(!showsMemory(worker));
        QVERIFY(!worthShowing(worker));
        Sample heavy;
        heavy.valid = true; heavy.ramBytes = 2048 * mib; heavy.ramPercent = 12.0;
        QVERIFY(showsMemory(heavy));
        QVERIFY(worthShowing(heavy));
        // Past the byte floor but under half a percent of a large machine: "0%" is not worth a
        // section, so it is not shown either.
        Sample big;
        big.valid = true; big.ramBytes = 300 * mib; big.ramPercent = 0.45;
        QVERIFY(!showsMemory(big));
        // A sample that never had a baseline says nothing either way.
        QVERIFY(!worthShowing(Sample{}));
        Sample noBaseline; noBaseline.cpuPercent = 50; noBaseline.ramBytes = 4096 * mib; noBaseline.ramPercent = 30;
        QVERIFY(!showsCpu(noBaseline));
        QVERIFY(!showsMemory(noBaseline));
    }

    void combinesPanesIntoATab() {
        Sample a; a.valid = true; a.cpuPercent = 12.5; a.ramBytes = qint64(1.5 * (1 << 30)); a.ramPercent = 4.5;
        Sample b; b.valid = true; b.cpuPercent = 7.25; b.ramBytes = qint64(0.5 * (1 << 30)); b.ramPercent = 1.5;
        const Sample tab = combined({a, b});
        QVERIFY(tab.valid);
        QVERIFY(std::fabs(tab.cpuPercent - 19.75) < 0.001);
        QCOMPARE(tab.ramBytes, qint64(2.0 * (1 << 30)));
        QVERIFY(std::fabs(tab.ramPercent - 6.0) < 0.001);
        // Invalid panes contribute nothing, and only-invalid is not a tab reading.
        QCOMPARE(combined({Sample{}, a}).cpuPercent, 12.5);
        QVERIFY(!combined({Sample{}, Sample{}}).valid);
        QVERIFY(combined({}).valid == false);
        // Sums are still shares of one machine.
        Sample full; full.valid = true; full.cpuPercent = 80; full.ramPercent = 80;
        Sample more; more.valid = true; more.cpuPercent = 80; more.ramPercent = 80;
        QCOMPARE(combined({full, more}).cpuPercent, 100.0);
        QCOMPARE(combined({full, more}).ramPercent, 100.0);
    }

    // One wording, everywhere (issue #6BGA). The owner, on the mock-ups: "the cpu / mem bar
    // things are ugly and unintuitive. i think it should be numbers" — so the glyphs are gone and
    // the chip, the tab suffix and the Sessions tag all print the same words.
    void oneWordingInEveryPlace() {
        const qint64 gib = qint64(1) << 30;
        Sample idle; idle.valid = true;
        QVERIFY(readingText(idle).isEmpty());
        QVERIFY(tabSuffix(idle).isEmpty());
        QVERIFY(tabSuffix(Sample{}).isEmpty());
        QVERIFY(liveTag(idle).isEmpty());
        QVERIFY(liveTag(Sample{}).isEmpty());
        QVERIFY(readingParts(idle).isEmpty());

        Sample busy; busy.valid = true; busy.cpuPercent = 12.4; busy.ramBytes = 2 * gib; busy.ramPercent = 3.4;
        const QString one = QStringLiteral("cpu 12% · mem 3%");
        QCOMPARE(readingText(busy), one);
        // The chip's string, the tab's suffix and the Sessions row's tag are that one string.
        QCOMPARE(liveTag(busy), one);
        QCOMPARE(tabSuffix(busy), QStringLiteral("  ·  ") + one);
        QVERIFY(describe(busy).startsWith(one));
        // No slash, no bare pair, and nothing that has to be learned before it can be read.
        QVERIFY(!tabSuffix(busy).contains(QLatin1Char('/')));
        // The separator is one middle dot. Written as escaped UTF-8 bytes it used to come
        // out as the two characters "Â·" — QStringLiteral makes a UTF-16 literal of the
        // bytes it is given — and the tab label read "src Â· 5% cpu" on screen.
        QVERIFY(tabSuffix(busy).contains(QChar(0x00b7)));
        QVERIFY(!tabSuffix(busy).contains(QChar(0x00c2)));
        QVERIFY(!liveTag(busy).contains(QChar(0x00c2)));
        QVERIFY(!describe(busy).contains(QChar(0x00c2)));

        // A half with nothing to say is left out rather than printed as "0%", and what is left
        // is the same grammar shortened, not a different one.
        Sample cpuOnly; cpuOnly.valid = true; cpuOnly.cpuPercent = 20.0;
        cpuOnly.ramBytes = 60 << 20; cpuOnly.ramPercent = 0.2;
        QCOMPARE(readingText(cpuOnly), QStringLiteral("cpu 20%"));
        QCOMPARE(tabSuffix(cpuOnly), QStringLiteral("  ·  cpu 20%"));
        QCOMPARE(liveTag(cpuOnly), QStringLiteral("cpu 20%"));
        Sample memOnly; memOnly.valid = true; memOnly.cpuPercent = 0.1;
        memOnly.ramBytes = 3 * gib; memOnly.ramPercent = 9.4;
        QCOMPARE(readingText(memOnly), QStringLiteral("mem 9%"));
        QCOMPARE(tabSuffix(memOnly), QStringLiteral("  ·  mem 9%"));
        QCOMPARE(liveTag(memOnly), QStringLiteral("mem 9%"));
        // No section anywhere reads "0%": the evidence screenshot's "· 20% / 0%" cannot recur.
        QVERIFY(!tabSuffix(cpuOnly).contains(QStringLiteral(" 0%")));
        QVERIFY(!tabSuffix(cpuOnly).contains(QStringLiteral("/")));
        QVERIFY(!liveTag(cpuOnly).contains(QStringLiteral(" 0%")));
        QVERIFY(!tabSuffix(memOnly).contains(QStringLiteral(" 0%")));
        QVERIFY(!liveTag(memOnly).contains(QStringLiteral(" 0%")));
    }

    // The header ladder's last rung (relay::panes::UsageForm::CpuOnly): the memory half and its
    // separator go together, and what is left is still "cpu 12%" and not a bare number.
    void theNarrowRungKeepsTheGrammar() {
        const qint64 gib = qint64(1) << 30;
        Sample busy; busy.valid = true; busy.cpuPercent = 12.4; busy.ramBytes = 2 * gib; busy.ramPercent = 3.4;
        QCOMPARE(readingText(busy, true), QStringLiteral("cpu 12%"));
        QVERIFY(!readingText(busy, true).contains(QChar(0x00b7)));
        QVERIFY(!readingText(busy, true).contains(QStringLiteral("mem")));
        // Narrow and already memory-only: nothing is left, so the chip takes no room at all.
        Sample memOnly; memOnly.valid = true; memOnly.cpuPercent = 0.1;
        memOnly.ramBytes = 3 * gib; memOnly.ramPercent = 9.4;
        QVERIFY(readingText(memOnly, true).isEmpty());
        QVERIFY(readingParts(memOnly, true).isEmpty());
    }

    // What the chip paints: the words in the muted ink, each number in the ink its own value
    // earns. Joining the pieces has to be exactly the string every other surface prints.
    void thePaintedPiecesSpellTheSameString() {
        const qint64 gib = qint64(1) << 30;
        Sample busy; busy.valid = true; busy.cpuPercent = 91.0; busy.ramBytes = 30 * gib; busy.ramPercent = 62.0;
        const QList<ReadingPart> parts = readingParts(busy);
        QCOMPARE(int(parts.size()), 5);
        QString joined;
        for (const ReadingPart &part : parts) joined += part.text;
        QCOMPARE(joined, readingText(busy));
        QCOMPARE(parts.at(0).text, QStringLiteral("cpu "));
        QCOMPARE(parts.at(0).value, false);
        QCOMPARE(parts.at(1).text, QStringLiteral("91%"));
        QCOMPARE(parts.at(1).value, true);
        QCOMPARE(parts.at(1).percent, 91.0);
        QCOMPARE(parts.at(2).value, false);       // the separator
        QCOMPARE(parts.at(3).text, QStringLiteral("mem "));
        QCOMPARE(parts.at(4).percent, 62.0);      // the ink the memory half earns is its own
        // One half alone is two pieces, word then number: no separator left dangling.
        Sample cpuOnly; cpuOnly.valid = true; cpuOnly.cpuPercent = 20.0;
        cpuOnly.ramBytes = 60 << 20; cpuOnly.ramPercent = 0.2;
        QCOMPARE(int(readingParts(cpuOnly).size()), 2);
        QCOMPARE(readingParts(cpuOnly).at(1).text, QStringLiteral("20%"));
    }

    void aLabelHoldsStillUntilTheReadingMoves() {
        // The poll runs at 2.5 Hz; a percent that wobbles by a point four times a second is a
        // distraction, and on a tab label the text's width wobbles with it (issue #D03W).
        Sample shown; shown.valid = true; shown.cpuPercent = 12.0; shown.ramPercent = 3.0;
        Sample nudged = shown; nudged.cpuPercent = 13.0;
        QVERIFY(!labelShouldFollow(shown, nudged, 1000, 1400));   // one point, 400 ms on: hold
        QVERIFY(labelShouldFollow(shown, nudged, 1000, 2000));    // ... but not past the hold
        Sample jumped = shown; jumped.cpuPercent = 40.0;
        QVERIFY(labelShouldFollow(shown, jumped, 1000, 1040));    // a real move shows at once
        Sample memJumped = shown; memJumped.ramPercent = 9.0;
        QVERIFY(labelShouldFollow(shown, memJumped, 1000, 1040)); // either axis
        // Appearing and disappearing are not wobble.
        QVERIFY(labelShouldFollow(Sample{}, shown, 1000, 1040));
        QVERIFY(labelShouldFollow(shown, Sample{}, 1000, 1040));
        // Nothing shown yet: the first reading is always followed.
        QVERIFY(labelShouldFollow(shown, nudged, 0, 400));
    }

    void describesTheLongForm() {
        Sample s; s.valid = true; s.cpuPercent = 12.4; s.ramPercent = 3.2;
        s.ramBytes = qint64(2.06 * (1024.0 * 1024.0 * 1024.0));
        const QString text = describe(s);
        // The tooltip's first line opens with the same words as the chip and the tab, and then
        // adds the byte figure a percent of an unknown total cannot give (issue #6BGA).
        QVERIFY(text.startsWith(QStringLiteral("cpu 12% · mem 3%")));
        QVERIFY(text.contains(QStringLiteral("(2.1 GiB)")));
        QVERIFY(!text.contains(QStringLiteral("CPU ")));
        QVERIFY(describe(Sample{}).isEmpty());
        // Both halves print here whatever the floors say: a quiet pane's tooltip still answers.
        Sample quiet; quiet.valid = true; quiet.cpuPercent = 0.1; quiet.ramBytes = 60 << 20; quiet.ramPercent = 0.2;
        QVERIFY(readingText(quiet).isEmpty());
        QCOMPARE(describe(quiet), QStringLiteral("cpu 0% · mem 0% (60 MiB)"));
    }

    void meterNeedsABaselineBeforeAPercentage() {
        Meter meter;
        Reading first; first.ok = true; first.ticks = 1000; first.rssBytes = 10 << 20;
        const Sample baseline = meter.compute(first, 0);
        QVERIFY(!baseline.valid);
        // +150 ticks is 1.5 CPU-seconds over 1.2 s of wall clock: 1.25 cores, as a share of
        // whatever this machine has online.
        Reading second; second.ok = true; second.ticks = 1150; second.rssBytes = 12 << 20;
        const Sample sample = meter.compute(second, 1200);
        QVERIFY(sample.valid);
        const double expected = 1.25 / processorCount() * 100.0;
        QVERIFY(std::fabs(sample.cpuPercent - expected) < 0.01);
        QCOMPARE(sample.ramBytes, qint64(12 << 20));
        QVERIFY(std::fabs(sample.ramPercent - ramPercentOf(12 << 20, totalMemoryBytes())) < 0.001);
        // A tree that vanishes between readings is 0%, not a negative one.
        Reading gone; gone.ok = true; gone.ticks = 900; gone.rssBytes = 0;
        QCOMPARE(meter.compute(gone, 2400).cpuPercent, 0.0);
        // After a reset the next reading is a baseline again.
        Reading fresh; fresh.ok = true; fresh.ticks = 5000;
        meter.reset();
        QVERIFY(!meter.compute(fresh, 4800).valid);
    }

    void aMissedReadingDoesNotBecome100Percent() {
        // A miss drops the baseline instead of storing zero for it. Storing zero made the next
        // good reading look like the tree's whole lifetime of ticks happened in one interval,
        // which clamps to 100 %: one unreadable poll and the pane flashed "100%".
        const double expected = 1.25 / processorCount() * 100.0;
        const auto tick = [](Meter &meter, qint64 ticks, qint64 atMs) {
            Reading reading; reading.ok = true; reading.ticks = ticks; reading.rssBytes = 12 << 20;
            return meter.compute(reading, atMs);
        };
        Meter meter;
        QVERIFY(!tick(meter, 1000, 0).valid);                       // the baseline
        const Sample before = tick(meter, 1150, 1200);              // +150 ticks over 1.2 s
        QVERIFY(before.valid);
        QVERIFY(std::fabs(before.cpuPercent - expected) < 0.01);
        // One poll where nothing could be read: no percentage, and no baseline left behind.
        QVERIFY(!meter.compute(Reading{}, 2400).valid);
        // The next good reading is a fresh baseline, so it says nothing rather than something
        // wrong — and the one after it is the same modest number as before the miss.
        QVERIFY(!tick(meter, 1300, 3600).valid);
        const Sample after = tick(meter, 1450, 4800);
        QVERIFY(after.valid);
        QVERIFY(std::fabs(after.cpuPercent - expected) < 0.01);
        QVERIFY(after.cpuPercent < 99.0);
    }

    void statLinesCountReapedChildren() {
        // /proc/<pid>/stat, with a comm that has both a space and parentheses in it. utime and
        // stime are fields 14 and 15, cutime and cstime 16 and 17 — the reaped children's time,
        // which is the only record of a command that lived and died inside one poll interval.
        const QByteArray line =
            "4242 (my (odd) prog) S 1 4242 4242 34816 4242 4194304 900 800 0 0 "
            "11 22 33 44 20 0 1 0 12345 1000 200 0 0 0 0 0 0 0 0 0 0 0 0 0 17 3 0 0 0 0 0\n";
        qint64 ticks = 0;
        QVERIFY(parseStatTicks(line, &ticks));
        QCOMPARE(ticks, qint64(11 + 22 + 33 + 44));
        // A clock set backwards can make a child's time negative; that must not walk the tree's
        // total backwards, which the meter would read as a tree that shrank.
        qint64 negative = 0;
        QVERIFY(parseStatTicks("7 (sh) S 1 7 7 0 7 0 0 0 0 0 11 22 -5 -6 20 0 1 0 1 2 3\n", &negative));
        QCOMPARE(negative, qint64(33));
        // Nonsense is refused rather than read as zero.
        qint64 ignored = -1;
        QVERIFY(!parseStatTicks("no parenthesis here", &ignored));
        QVERIFY(!parseStatTicks("1 (sh) S 1 1 1\n", &ignored));
        QVERIFY(!parseStatTicks("1 (sh) S 1 1 1 0 1 0 0 0 0 0 x 22 0 0 20 0\n", &ignored));
    }

    void readsTheMachineItRunsOn() {
        QVERIFY(processorCount() >= 1);
        QVERIFY(totalMemoryBytes() > 0);
        QVERIFY(clockTicksPerSecond() > 0);
        // The test's own process: one /proc walk, real numbers. A tick is 10 ms of CPU, so
        // the walk is preceded by enough work to be sure at least one has been booked.
        volatile qint64 spin = 0;
        const qint64 until = QDateTime::currentMSecsSinceEpoch() + 60;
        while (QDateTime::currentMSecsSinceEpoch() < until) ++spin;
        const Reading own = readTrees({QCoreApplication::applicationPid()});
        QVERIFY(own.ok);
        QVERIFY(own.ticks > 0);
        QVERIFY(own.rssBytes > 0);
        // Nothing to walk is not a reading.
        QVERIFY(!readTrees({}).ok);
    }

    void childrenOfEveryThreadAreFound() {
        // The Python worker spawns a subprocess from a worker thread, so the kernel parents it to
        // that thread and /proc/<pid>/task/<pid>/children — the main thread's list, which the walk
        // used to be — never mentions it. Its CPU went unmeasured until the worker reaped it.
        FakeProc fake;
        QVERIFY(fake.ok());
        // 100 is the worker: nothing under its main thread, one child under thread 137.
        fake.add(100, QStringLiteral("python3"), 1, 40, 500, 10, {{100, {}}, {137, {200}}});
        fake.add(200, QStringLiteral("rg"), 137, 70, 900, 20, {{200, {201}}});
        fake.add(201, QStringLiteral("rg (worker)"), 200, 5, 910, 30);
        ProcRootFor root(fake.path());
        QList<qint64> pids;
        for (const ProcessInfo &info : walkTrees({100})) pids << info.pid;
        QCOMPARE(pids, QList<qint64>({100, 200, 201}));   // breadth-first, the root first
        const Reading reading = readTrees({100});
        QVERIFY(reading.ok);
        QCOMPARE(reading.ticks, qint64(40 + 70 + 5));
        QCOMPARE(reading.rssBytes, qint64(10 + 20 + 30) * ::sysconf(_SC_PAGESIZE));
    }

    void oneWalkServesBothReaders() {
        // Both the meter and Pane::programWaitingForInput() go through walkTrees. The input poll
        // runs at 250 ms and wants only the shape of the tree, so it asks for pids and pays for no
        // stat or statm; it keeps its own, smaller cap.
        FakeProc fake;
        QVERIFY(fake.ok());
        fake.add(7, QStringLiteral("bash"), 1, 11, 100, 5, {{7, {8, 9}}});
        fake.add(8, QStringLiteral("make"), 7, 22, 200, 6, {{8, {10}}});
        fake.add(9, QStringLiteral("tail"), 7, 33, 300, 7);
        fake.add(10, QStringLiteral("cc1plus"), 8, 44, 400, 8);
        ProcRootFor root(fake.path());
        const QList<ProcessInfo> full = walkTrees({7});
        QCOMPARE(int(full.size()), 4);
        QCOMPARE(full.at(1).comm, QStringLiteral("make"));
        QCOMPARE(full.at(1).ppid, qint64(7));
        QCOMPARE(full.at(1).state, 'S');
        QCOMPARE(full.at(1).startTicks, qint64(200));
        const QList<ProcessInfo> cheap = walkTrees({7}, 64, Detail::PidsOnly);
        QList<qint64> fullPids, cheapPids;
        for (const ProcessInfo &info : full) fullPids << info.pid;
        for (const ProcessInfo &info : cheap) cheapPids << info.pid;
        QCOMPARE(cheapPids, fullPids);                 // the same tree, in the same order
        QCOMPARE(cheap.at(1).ticks, qint64(0));        // ... and nothing was read for it
        QCOMPARE(cheap.at(1).rssBytes, qint64(0));
        QVERIFY(cheap.at(1).comm.isEmpty());
        // The summed reading is the walk, so the two readers cannot disagree about the tree.
        const Reading summed = summarize(full);
        QCOMPARE(summed.ticks, readTrees({7}).ticks);
        // The cap bounds the walk, whichever reader set it.
        QCOMPARE(int(walkTrees({7}, 2).size()), 2);
        QCOMPARE(int(walkTrees({7}, 0).size()), 0);
        // A root that is not there is a walk of one unreadable process, not a failure.
        const QList<ProcessInfo> gone = walkTrees({4242});
        QCOMPARE(int(gone.size()), 1);
        QCOMPARE(gone.at(0).ticks, qint64(0));
    }

    void perProcessDeltasKnowARecycledPid() {
        // Each row's CPU is that process's own tick delta over the interval. A pid the kernel
        // handed out again is a different process: without the starttime check its counters would
        // be subtracted from its predecessor's and the row would read a whole lifetime in one
        // poll, which is exactly the 100 % the summed meter is careful not to print.
        const qint64 full = qint64(processorCount()) * clockTicksPerSecond();  // one second, every core
        const qint64 total = totalMemoryBytes();
        const qint64 small = total / 1000;   // 0.1 % of the machine: nothing a row would report
        Meter meter;
        QList<ProcessInfo> first{proc(7, QStringLiteral("bash"), 10, 100, small),
                                 proc(8, QStringLiteral("yes"), 1000, 200, total / 50),
                                 proc(9, QStringLiteral("make"), 5, 300, small)};
        QVERIFY(!meter.compute(first, 1000).valid);   // the baseline: no deltas yet, no rows
        QVERIFY(meter.compute(first, 1000).processes.isEmpty());
        QList<ProcessInfo> second{proc(7, QStringLiteral("bash"), 10, 100, small),
                                  proc(8, QStringLiteral("yes"), 1000 + full / 2, 200, total / 50),
                                  // pid 9 was reused: a new starttime, and counters that do not
                                  // continue the old process's.
                                  proc(9, QStringLiteral("cc1plus"), full / 4, 999, small)};
        const Sample sample = meter.compute(second, 2000);
        QVERIFY(sample.valid);
        QCOMPARE(int(sample.processes.size()), 1);
        QCOMPARE(sample.processes.at(0).pid, qint64(8));
        QVERIFY(std::fabs(sample.processes.at(0).cpuPercent - 50.0) < 0.5);
        QVERIFY(std::fabs(sample.processes.at(0).ramPercent - 2.0) < 0.1);
        // The recycled pid contributed no CPU it can be held to this interval, so it is not a
        // row; bash sat still and holds nothing, and is not one either.
        for (const ProcessUsage &row : sample.processes) QVERIFY(row.pid != 9);
        // The next interval, on the other hand, measures the new process by its own baseline.
        QList<ProcessInfo> third{proc(7, QStringLiteral("bash"), 10, 100, small),
                                 proc(8, QStringLiteral("yes"), 1000 + full / 2, 200, total / 50),
                                 proc(9, QStringLiteral("cc1plus"), full / 2, 999, small)};
        const Sample after = meter.compute(third, 3000);
        QCOMPARE(after.processes.at(0).pid, qint64(9));
        QVERIFY(std::fabs(after.processes.at(0).cpuPercent - 25.0) < 0.5);
        // `yes` stopped but still holds 2 % of the machine's memory, so it keeps a row with a
        // zero CPU half rather than vanishing from the breakdown.
        QCOMPARE(int(after.processes.size()), 2);
        QCOMPARE(after.processes.at(1).pid, qint64(8));
        QCOMPARE(formatPercent(after.processes.at(1).cpuPercent), QStringLiteral("0"));
        // The rows are a breakdown of the sum, not a second opinion: a process's ticks are in
        // exactly one row, so they add up to the tree's.
        QVERIFY(std::fabs(after.cpuPercent - 25.0) < 0.5);
        // A missed reading drops the per-pid history with the baseline, so the reading after it
        // is a baseline again rather than a lifetime of ticks over one interval.
        QVERIFY(!meter.compute(Reading{}, 4000).valid);
        QVERIFY(!meter.compute(third, 5000).valid);
        const Sample quiet = meter.compute(third, 6000);
        QVERIFY(quiet.valid);
        QCOMPARE(quiet.cpuPercent, 0.0);   // nothing moved since, and nothing was carried over
        for (const ProcessUsage &row : quiet.processes)
            QCOMPARE(formatPercent(row.cpuPercent), QStringLiteral("0"));
    }

    void theBreakdownNamesTheBusiestFew() {
        const auto row = [](qint64 pid, const QString &name, double cpu, double mem) {
            ProcessUsage out;
            out.pid = pid; out.name = name; out.cpuPercent = cpu; out.ramPercent = mem;
            out.ramBytes = qint64(mem / 100.0 * double(qint64(64) << 30));
            return out;
        };
        QList<ProcessUsage> rows{row(1, QStringLiteral("shell"), 1.0, 0.2),
                                 row(2, QStringLiteral("cc1plus"), 40.0, 2.0),
                                 row(3, QStringLiteral("ld"), 5.0, 9.0),
                                 row(4, QStringLiteral("agent worker"), 0.0, 4.0),
                                 row(5, QStringLiteral("sleep"), 0.1, 0.1),
                                 row(6, QStringLiteral("rg"), 12.0, 0.4),
                                 row(7, QStringLiteral("jq"), 12.0, 3.0)};
        const QList<ProcessUsage> top = topProcesses(rows);
        QList<qint64> order;
        for (const ProcessUsage &r : top) order << r.pid;
        // CPU first; jq before rg on memory at the same CPU; the memory-only worker after them
        // all; the idle sleep dropped, because "0% cpu · 0% mem" names nothing.
        QCOMPARE(order, QList<qint64>({2, 7, 6, 3, 1}));
        QCOMPARE(int(topProcesses(rows, 2).size()), 2);
        QCOMPARE(int(topProcesses({}).size()), 0);
        // Equal on both axes: the pid keeps the order steady, so the tooltip does not shuffle.
        QList<ProcessUsage> tied{row(9, QStringLiteral("b"), 3.0, 1.0), row(4, QStringLiteral("a"), 3.0, 1.0)};
        QCOMPARE(topProcesses(tied).at(0).pid, qint64(4));

        QCOMPARE(processLine(row(2, QStringLiteral("cc1plus"), 40.4, 2.4)),
                 QStringLiteral("cc1plus · cpu 40% · mem 2%"));
        QVERIFY(!processLine(row(1, QStringLiteral("shell"), 1.0, 0.2)).contains(QChar(0x00c2)));
        Sample sample;
        sample.valid = true;
        sample.processes = topProcesses(rows, 3);
        QCOMPARE(int(processLines(sample).size()), 3);
        QCOMPARE(processBreakdown(sample).split(QLatin1Char('\n')).first(),
                 QStringLiteral("cc1plus · cpu 40% · mem 2%"));
        // A sample with no baseline has nothing to break down.
        Sample noBaseline;
        noBaseline.processes = sample.processes;
        QVERIFY(processBreakdown(noBaseline).isEmpty());
        QVERIFY(processBreakdown(Sample{}).isEmpty());

        // A tab's breakdown is its panes' put together and cut back again, so it names the
        // busiest processes in the tab rather than the busiest in each pane.
        Sample one; one.valid = true; one.cpuPercent = 40; one.processes = {row(2, QStringLiteral("cc1plus"), 40.0, 2.0)};
        Sample two; two.valid = true; two.cpuPercent = 12; two.processes = {row(6, QStringLiteral("rg"), 12.0, 0.4),
                                                                            row(3, QStringLiteral("ld"), 5.0, 9.0)};
        const Sample tab = combined({one, two});
        QCOMPARE(int(tab.processes.size()), 3);
        QCOMPARE(tab.processes.at(0).pid, qint64(2));
        QCOMPARE(tab.processes.at(2).pid, qint64(3));
    }

    void rootsAreNamedOnTheirRows() {
        // The two roots are the pane's own: "shell" and "agent worker" say more than "bash" and
        // "python3", and the rest of the tree is named after its comm.
        const qint64 full = qint64(processorCount()) * clockTicksPerSecond();
        Meter meter;
        QList<ProcessInfo> before{proc(7, QStringLiteral("bash"), 0, 100, 0),
                                  proc(8, QStringLiteral("python3"), 0, 200, 0),
                                  proc(9, QStringLiteral("cc1plus"), 0, 300, 0)};
        QList<ProcessInfo> after{proc(7, QStringLiteral("bash"), full / 10, 100, 0),
                                 proc(8, QStringLiteral("python3"), full / 5, 200, 0),
                                 proc(9, QStringLiteral("cc1plus"), full / 2, 300, 0)};
        const QHash<qint64, QString> labels{{7, QStringLiteral("shell")}, {8, QStringLiteral("agent worker")}};
        meter.compute(before, 1000, labels);
        const Sample sample = meter.compute(after, 2000, labels);
        QStringList names;
        for (const ProcessUsage &r : sample.processes) names << r.name;
        QCOMPARE(names, QStringList({QStringLiteral("cc1plus"), QStringLiteral("agent worker"),
                                     QStringLiteral("shell")}));
        // An unnamed process with no comm is still identifiable.
        ProcessInfo bare = proc(11, QString(), full, 400, 0);
        meter.reset();
        meter.compute({bare}, 3000);
        bare.ticks = full * 2;
        QCOMPARE(meter.compute({bare}, 4000).processes.at(0).name, QStringLiteral("pid 11"));
    }

    void statLinesCarryTheWholeProcess() {
        // parseStat reads what the walk needs beyond the ticks: the parent, the state, the comm
        // (spaces, parentheses and all) and the starttime that tells two lives of one pid apart.
        ProcessInfo info;
        QVERIFY(parseStat("4242 (my (odd) prog) S 17 4242 4242 34816 4242 4194304 900 800 0 0 "
                          "11 22 33 44 20 0 1 0 12345 1000 200 0 0 0 0 0 0 0 0\n", &info));
        QCOMPARE(info.pid, qint64(4242));
        QCOMPARE(info.comm, QStringLiteral("my (odd) prog"));
        QCOMPARE(info.ppid, qint64(17));
        QCOMPARE(info.state, 'S');
        QCOMPARE(info.ticks, qint64(11 + 22 + 33 + 44));
        QCOMPARE(info.startTicks, qint64(12345));
        // A short line still gives the ticks; only the recycled-pid guard goes.
        ProcessInfo shortLine;
        QVERIFY(parseStat("7 (sh) R 1 7 7 0 7 0 0 0 0 0 11 22 0 0 20\n", &shortLine));
        QCOMPARE(shortLine.ticks, qint64(33));
        QCOMPARE(shortLine.startTicks, qint64(0));
        QCOMPARE(shortLine.state, 'R');
    }
};

QTEST_GUILESS_MAIN(PaneUsageTests)
#include "paneusage_test.moc"
