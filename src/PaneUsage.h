// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// relay::usage — what a pane's own processes cost this machine, as CPU and memory percentages.
//
// A terminal pane owns two process trees: the shell its pty spawned (and whatever it is running,
// an ssh client included) and the pane's agent worker. Summing their /proc counters over the
// status poll's interval gives the pane's share of the machine, which PaneChrome shows as a small
// "12% 3%" chip beside the pane's title (issue #D03W) and RelayWindow appends to the tab label.
//
// The /proc walk lives in readTrees(); everything downstream of a Reading is arithmetic on
// numbers a test can make up, so the meter's math is covered by tests/paneusage_test.cpp without
// depending on what the machine happens to be doing.

#include <QList>
#include <QString>

#include <QtGlobal>

namespace relay::usage {

// One measurement of one pane (or one tab, by combining panes).
struct Sample {
    bool valid = false;      // two readings existed, so the percentages mean something
    double cpuPercent = 0.0; // of the whole machine: 100 = every core busy
    double ramPercent = 0.0; // of physical memory
    qint64 ramBytes = 0;     // resident set summed over the trees
};

// A process tree's counters at one instant. `ok` is false when there was nothing to measure.
struct Reading {
    bool ok = false;
    qint64 ticks = 0;     // utime + stime summed over the live processes, clock ticks
    qint64 rssBytes = 0;  // resident memory summed over them
};

// Sums CPU ticks and resident memory over the process trees rooted at `roots`, by walking
// /proc/<pid>/task/<pid>/children from each root. Processes that cannot be read (gone, or another
// user's) are skipped, not failed: a tree that is half invisible still reports what is visible.
// Capped at 256 processes per call so a fork bomb cannot make the poll expensive.
Reading readTrees(const QList<qint64> &roots);

// Keeps the previous Reading and turns each new one into a percentage over the time between them.
class Meter {
public:
    // Reads the trees and stamps the clock. Call from one place, on a poll.
    Sample update(const QList<qint64> &roots);
    // The arithmetic half, on a Reading the caller made up: what the tests drive.
    Sample compute(const Reading &reading, qint64 nowMs);
    // Forgets the baseline, so the next update() returns an invalid Sample rather than a
    // percentage over a gap (used when a pane's processes change wholesale).
    void reset();

private:
    bool m_hasLast = false;
    qint64 m_lastTicks = 0;
    qint64 m_lastMs = 0;
};

// The machine the panes are measured against, read once per process.
int processorCount();
qint64 totalMemoryBytes();
qint64 clockTicksPerSecond();

// Tick delta over an interval, as a percentage of all the machine's cores together. Clamped to
// [0, 100]: scheduling jitter can make a honest delta land a hair outside.
double cpuPercentOf(qint64 tickDelta, qint64 elapsedMs, int cores, qint64 clkTck);
double ramPercentOf(qint64 bytes, qint64 totalBytes);

// Whole-number percents; sub-half rounds to 0.
QString formatPercent(double percent);

// A quiet pane (an idle shell) shows nothing: the chip appears when there is something to read.
bool worthShowing(const Sample &sample);

// Sums samples into one tab-level sample; valid when any input was.
Sample combined(const QList<Sample> &samples);

// "  ·  12% / 3%" for a tab label, or empty when there is nothing worth showing.
QString tabSuffix(const Sample &sample);

// "cpu 12% · mem 3%" — the same reading as a labelled tag for the session manager's rows, where
// bare numbers would sit next to words. Empty when there is nothing worth showing.
QString liveTag(const Sample &sample);

// "CPU 12% · memory 1.2 GB (3%)" — the long form for tooltips.
QString describe(const Sample &sample);

}  // namespace relay::usage
