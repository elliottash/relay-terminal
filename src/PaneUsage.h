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
    qint64 ticks = 0;     // CPU ticks booked to the tree, live and reaped alike (see below)
    qint64 rssBytes = 0;  // resident memory summed over the live processes
};

// Parses one /proc/<pid>/stat line into the CPU ticks booked to that process: its own utime and
// stime (fields 14 and 15) plus cutime and cstime (fields 16 and 17), the time of the children it
// has already reaped. The reaped half is what makes a pane running short commands readable at
// all: `make` spends its life waiting while a hundred compilers each live for less than one poll
// interval, and only the parent's cutime remembers them. The comm field (field 2) may contain
// spaces and parentheses, so the fields are counted after the last ')'.
bool parseStatTicks(const QByteArray &line, qint64 *ticks);

// Sums CPU ticks and resident memory over the process trees rooted at `roots`, by walking
// /proc/<pid>/task/<pid>/children from each root. Processes that cannot be read (gone, or another
// user's) are skipped, not failed: a tree that is half invisible still reports what is visible.
// Capped at 256 processes per call so a fork bomb cannot make the poll expensive.
//
// Each visited process contributes its reaped children's ticks as well as its own, which is a sum
// and not a double count: a child's time is in its own utime/stime while it lives and moves into
// its parent's cutime/cstime the moment the parent reaps it, never both at once.
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

// Memory below this never puts the meter on screen, however large a share of the machine it is.
// An agent worker sitting idle holds 60-80 MB and will do so all day; on a small machine that is
// a percent or two, which used to pin the chip on permanently reading "0% / 1%".
inline constexpr qint64 kMemoryFloorBytes = qint64(256) << 20;

// Which halves of a sample have something to say. CPU speaks from half a percent up, so it never
// renders as "0%". Memory has to clear both floors: 256 MiB, which keeps an idle worker off a
// small machine's chip, and half a percent, which keeps a "0%" off a large machine's — the same
// 256 MiB is 1.6 % of 16 GB and 0.4 % of 64 GB.
bool showsCpu(const Sample &sample);
bool showsMemory(const Sample &sample);

// A quiet pane (an idle shell) shows nothing: the chip appears when there is something to read.
bool worthShowing(const Sample &sample);

// Sums samples into one tab-level sample; valid when any input was.
Sample combined(const QList<Sample> &samples);

// "  ·  12% / 3%" for a tab label, or "  ·  12% cpu" / "  ·  3% mem" when only one half is worth
// printing — a bare number would not say which. Empty when there is nothing worth showing.
QString tabSuffix(const Sample &sample);

// "cpu 12% · mem 3%" — the same reading as a labelled tag for the session manager's rows, where
// bare numbers would sit next to words. Empty when there is nothing worth showing.
QString liveTag(const Sample &sample);

// "CPU 12% · memory 1.2 GiB (3%)" — the long form for tooltips.
QString describe(const Sample &sample);

// The sentence a tooltip owes the reader about the memory figure: it is a sum of resident sets
// over the tree (and, for a tab, over its panes), so pages two processes share are counted twice.
QString memoryNote();

// Whether the meters are on at all (`appearance/pane_usage`). The chip, the tab suffix, the tab
// tooltip's usage line and the Sessions row's tag all ask here, so one setting turns off all of
// them, which is what the settings row promises.
bool metersEnabled();

// Hysteresis for a percentage on a label (issue #D03W). The poll runs at 2.5 Hz and a rounded
// percent that wobbles by a point twice a second is a distraction — worse on a tab label, where
// the text's width moves too. A freshly measured sample only replaces the one on screen when it
// moved by kLabelStep points on either axis, or kLabelHoldMs has passed since the label last
// changed. `shownAtMs` is when that happened; 0 (never) always follows.
inline constexpr double kLabelStep = 3.0;
inline constexpr qint64 kLabelHoldMs = 1000;
bool labelShouldFollow(const Sample &shown, const Sample &measured, qint64 shownAtMs, qint64 nowMs);

}  // namespace relay::usage
