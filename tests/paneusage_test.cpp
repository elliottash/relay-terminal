// SPDX-License-Identifier: GPL-3.0-or-later
// A pane's CPU / memory share (issue #D03W): the arithmetic on Readings a test can make up —
// percentages over an interval, rounding, when a meter is worth showing, tab-level sums and the
// label suffixes — plus one /proc walk over this test's own process to prove the parsing.
#include "PaneUsage.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTest>

#include <cmath>

using namespace relay::usage;

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

    void tabSuffixOnlyWhenWorthShowing() {
        const qint64 gib = qint64(1) << 30;
        Sample idle; idle.valid = true;
        QVERIFY(tabSuffix(idle).isEmpty());
        QVERIFY(tabSuffix(Sample{}).isEmpty());
        Sample busy; busy.valid = true; busy.cpuPercent = 12.4; busy.ramBytes = 2 * gib; busy.ramPercent = 3.4;
        QCOMPARE(tabSuffix(busy), QStringLiteral("  ·  12% / 3%"));
        QCOMPARE(liveTag(busy), QStringLiteral("cpu 12% · mem 3%"));
        // The separator is one middle dot. Written as escaped UTF-8 bytes it used to come
        // out as the two characters "Â·" — QStringLiteral makes a UTF-16 literal of the
        // bytes it is given — and the tab label read "src Â· 5% cpu" on screen.
        QVERIFY(tabSuffix(busy).contains(QChar(0x00b7)));
        QVERIFY(!tabSuffix(busy).contains(QChar(0x00c2)));
        QVERIFY(!liveTag(busy).contains(QChar(0x00c2)));
        QVERIFY(!describe(busy).contains(QChar(0x00c2)));
        QVERIFY(liveTag(idle).isEmpty());
        QVERIFY(liveTag(Sample{}).isEmpty());
        // A half with nothing to say is left out rather than printed as "0%" — and a lone number
        // is named, because "· 20%" alone does not say whether it is CPU or memory.
        Sample cpuOnly; cpuOnly.valid = true; cpuOnly.cpuPercent = 20.0;
        cpuOnly.ramBytes = 60 << 20; cpuOnly.ramPercent = 0.2;
        QCOMPARE(tabSuffix(cpuOnly), QStringLiteral("  ·  20% cpu"));
        QCOMPARE(liveTag(cpuOnly), QStringLiteral("cpu 20%"));
        Sample memOnly; memOnly.valid = true; memOnly.cpuPercent = 0.1;
        memOnly.ramBytes = 3 * gib; memOnly.ramPercent = 9.4;
        QCOMPARE(tabSuffix(memOnly), QStringLiteral("  ·  9% mem"));
        QCOMPARE(liveTag(memOnly), QStringLiteral("mem 9%"));
        // No section anywhere reads "0%": the evidence screenshot's "· 20% / 0%" cannot recur.
        QVERIFY(!tabSuffix(cpuOnly).contains(QStringLiteral(" 0%")));
        QVERIFY(!tabSuffix(cpuOnly).contains(QStringLiteral("/")));
        QVERIFY(!liveTag(cpuOnly).contains(QStringLiteral(" 0%")));
        QVERIFY(!tabSuffix(memOnly).contains(QStringLiteral(" 0%")));
        QVERIFY(!liveTag(memOnly).contains(QStringLiteral(" 0%")));
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
        QVERIFY(text.contains(QStringLiteral("CPU 12%")));
        QVERIFY(text.contains(QStringLiteral("2.1 GiB")));
        QVERIFY(text.contains(QStringLiteral("(3%)")));
        QVERIFY(describe(Sample{}).isEmpty());
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
};

QTEST_GUILESS_MAIN(PaneUsageTests)
#include "paneusage_test.moc"
