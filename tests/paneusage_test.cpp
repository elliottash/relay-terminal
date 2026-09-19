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
        Sample idle;
        idle.valid = true;
        QVERIFY(!worthShowing(idle));
        Sample shell;
        shell.valid = true; shell.cpuPercent = 0.02; shell.ramPercent = 0.03;
        QVERIFY(!worthShowing(shell));
        Sample busy;
        busy.valid = true; busy.cpuPercent = 0.6;
        QVERIFY(worthShowing(busy));
        Sample heavy;
        heavy.valid = true; heavy.ramPercent = 0.6;
        QVERIFY(worthShowing(heavy));
        // A sample that never had a baseline says nothing either way.
        QVERIFY(!worthShowing(Sample{}));
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
        Sample idle; idle.valid = true;
        QVERIFY(tabSuffix(idle).isEmpty());
        QVERIFY(tabSuffix(Sample{}).isEmpty());
        Sample busy; busy.valid = true; busy.cpuPercent = 12.4; busy.ramPercent = 3.4;
        QCOMPARE(tabSuffix(busy), QStringLiteral("  \xc2\xb7  12% / 3%"));
        QCOMPARE(liveTag(busy), QStringLiteral("cpu 12% \xc2\xb7 mem 3%"));
        QVERIFY(liveTag(idle).isEmpty());
        QVERIFY(liveTag(Sample{}).isEmpty());
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
        // A missing reading (no roots) gives no percentage; the baseline is kept.
        QCOMPARE(meter.compute(Reading{}, 3600).valid, false);
        // After a reset the next reading is a baseline again.
        Reading fresh; fresh.ok = true; fresh.ticks = 5000;
        meter.reset();
        QVERIFY(!meter.compute(fresh, 4800).valid);
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
