// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// relay::usage — what a pane's own processes cost this machine, as CPU and memory percentages.
//
// Linux reads /proc; Windows uses Toolhelp/process counters; macOS uses libproc. Windows and
// macOS count live processes only, so short commands reaped between polls are not included.
//
// A terminal pane owns two process trees: the shell its pty spawned (and whatever it is running,
// an ssh client included) and the pane's agent worker. Summing their native counters over the
// status poll's interval gives the pane's share of the machine, which PaneChrome shows as a small
// "cpu 12% · mem 3%" chip beside the pane's title (issue #D03W) and RelayWindow appends to the tab
// label. Every surface prints that one wording (issue #6BGA) except the tab label, which prints
// the same words with both halves always and two digits each, on a 5 s clock of its own (card
// #MERX) — see tabSuffix() below.
//
// There is one /proc walk here, walkTrees(), and it is the only one in the program: the usage
// meter reads it for the counters and Pane::programWaitingForInput() reads it for the pids (the
// cheap form, which opens no stat files). Everything downstream of a Reading is arithmetic on
// numbers a test can make up, and the walk itself reads procRoot(), which a test can point at a
// directory it built — so tests/paneusage_test.cpp covers both without depending on what the
// machine happens to be doing.

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <QtGlobal>

namespace relay::usage {

// One process's own share, for the breakdown the tooltips print under the summed reading.
struct ProcessUsage {
    qint64 pid = 0;
    QString name;            // comm, or "shell" / "agent worker" for the roots the pane named
    double cpuPercent = 0.0; // of the whole machine, over the interval between two polls
    double ramPercent = 0.0; // of physical memory
    qint64 ramBytes = 0;
};

// One measurement of one pane (or one tab, by combining panes).
struct Sample {
    bool valid = false;      // two readings existed, so the percentages mean something
    double cpuPercent = 0.0; // of the whole machine: 100 = every core busy
    double ramPercent = 0.0; // of physical memory
    qint64 ramBytes = 0;     // resident set summed over the trees
    // Who the sum is made of: the busiest few processes, sorted and already filtered (see
    // topProcesses). The chip stays the sum alone; this is what its tooltip spells out. A
    // child that exited still counts toward the sum, through its parent's cutime/cstime, but
    // it has no row of its own — there is no longer a process to name.
    QList<ProcessUsage> processes;
};

// A process tree's counters at one instant. `ok` is false when there was nothing to measure.
struct Reading {
    bool ok = false;
    qint64 ticks = 0;     // CPU ticks booked to the tree (Linux includes reaped children)
    qint64 rssBytes = 0;  // resident memory summed over the live processes
};

// One process as the walk found it. `startTicks` is the native creation time, which together
// with the pid identifies the process across polls: pids are recycled, and without it a fresh
// process wearing a dead one's number would be read as one that had burned every tick of its
// predecessor's life in a single interval.
struct ProcessInfo {
    qint64 pid = 0;
    qint64 ppid = 0;
    QString comm;
    char state = '\0';      // 'R', 'S', 'D', 'Z' … as /proc spells it
    qint64 ticks = 0;       // native CPU units; clockTicksPerSecond() supplies their scale
    qint64 startTicks = 0;  // native creation time, the pid's identity
    qint64 rssBytes = 0;
};

// Parses one /proc/<pid>/stat line. The comm field (field 2) may contain spaces and parentheses,
// so the numbered fields are counted after the last ')'. `ticks` is the CPU booked to the process:
// its own utime and stime (fields 14 and 15) plus cutime and cstime (16 and 17), the time of the
// children it has already reaped. The reaped half is what makes a pane running short commands
// readable at all: `make` spends its life waiting while a hundred compilers each live for less
// than one poll interval, and only the parent's cutime remembers them.
bool parseStat(const QByteArray &line, ProcessInfo *info);
// The ticks alone, for callers that want nothing else.
bool parseStatTicks(const QByteArray &line, qint64 *ticks);

// Linux only: where the walk reads from: "/proc", unless a test points it at a directory it built.
QString procRoot();
void setProcRoot(const QString &root);

// How much of each process the walk reads. The input poll wants only the shape of the tree and
// runs four times a second, so it does not pay for a stat and a statm per process.
enum class Detail {
    Counters,  // pid, ppid, comm, state, ticks, starttime, rss
    PidsOnly,  // pid only; every other field stays at its default
};

// No more than this many processes per walk, so a fork bomb cannot make a poll expensive.
inline constexpr int kProcessCap = 256;

// **The** /proc walk. Breadth-first from `roots` (roots first, in the order given), following
// every thread's children: /proc/<pid>/task/<tid>/children for each <tid> in /proc/<pid>/task,
// not just the main thread's. A subprocess the Python worker spawns from a worker thread is
// parented to that thread, so a walk of task/<pid>/children alone never saw it and its CPU went
// unmeasured until the worker reaped it. Processes that cannot be read (gone, or another user's)
// are returned with whatever was readable rather than dropped: a tree that is half invisible
// still reports what is visible.
QList<ProcessInfo> walkTrees(const QList<qint64> &roots, int cap = kProcessCap,
                             Detail detail = Detail::Counters);

// Sums a walk's processes into one tree reading. Each process contributes its reaped children's
// ticks as well as its own, which is a sum and not a double count: a child's time is in its own
// utime/stime while it lives and moves into its parent's cutime/cstime the moment the parent
// reaps it, never both at once.
Reading summarize(const QList<ProcessInfo> &procs);

// walkTrees + summarize: the tree counters for callers that do not want the processes. Nothing
// to walk is not a reading (`ok` stays false), which is what makes the meter drop its baseline.
Reading readTrees(const QList<qint64> &roots);

// A tree the meter measures, and what to call it on a breakdown line. A root with no label is
// named after its comm, like every other process.
struct Root {
    qint64 pid = 0;
    QString label;
};

// Keeps the previous Reading and turns each new one into a percentage over the time between them.
class Meter {
public:
    // Walks the trees, stamps the clock, and fills in the per-process breakdown. Call from one
    // place, on a poll.
    Sample update(const QList<Root> &roots);
    Sample update(const QList<qint64> &roots);   // the same, with no names for the roots
    // The arithmetic half, on a Reading the caller made up: what the tests drive. No breakdown,
    // since a Reading is already summed.
    Sample compute(const Reading &reading, qint64 nowMs);
    // The same on a walk the caller made up: the sum *and* the per-process rows, each process's
    // CPU being its own tick delta since the last call over the interval between the two.
    Sample compute(const QList<ProcessInfo> &procs, qint64 nowMs,
                   const QHash<qint64, QString> &labels = {});
    // Forgets the baseline (and the per-pid history), so the next update() returns an invalid
    // Sample rather than a percentage over a gap (used when a pane's processes change wholesale).
    void reset();

private:
    struct PidTicks {
        qint64 startTicks = 0;
        qint64 ticks = 0;
    };
    bool m_hasLast = false;
    qint64 m_lastTicks = 0;
    qint64 m_lastMs = 0;
    QHash<qint64, PidTicks> m_lastPids;
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

// The same whole percent printed two digits wide — "00" to "99", and "100" unchanged, the only
// three-digit reading. Only the tab label uses this (card #MERX): a tab's width is every other
// tab's layout, so there a percent may not gain or lose a column as the number moves.
QString formatPercent2(double percent);

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

// Sums samples into one tab-level sample; valid when any input was. The breakdowns merge the
// same way the numbers do: every pane's rows together, re-sorted and cut back to the top few, so
// a tab's tooltip names the busiest processes in the tab rather than the busiest per pane.
Sample combined(const QList<Sample> &samples);

// **The** wording of a reading, and the only place it is written (issue #6BGA, owner 2026-09-19:
// "the cpu / mem bar things are ugly and unintuitive. i think it should be numbers"). Plain words
// and whole percents: "cpu 12% · mem 3%", or the single half that is worth showing on its own —
// "cpu 12%", "mem 3%". Empty when neither half is worth showing. `cpuOnly` drops the memory half
// even when it has something to say: that is the header ladder's last rung
// (relay::panes::UsageForm::CpuOnly), and the shortened form is the same grammar, not a new one.
//
// The pane chip, the tab suffix, the Sessions row's tag and the tooltips' first line all print
// this, so the reading is learned once and read anywhere.
QString readingText(const Sample &sample, bool cpuOnly = false);

// The same reading cut into the pieces a painter colours separately: the words in the header's
// muted ink, each percentage in the ink its own value earns (warning at 60 %, error at 85 %).
// Joining their texts is exactly readingText(), so the chip cannot drift from the strings.
struct ReadingPart {
    QString text;
    bool value = false;    // true for a percentage, false for a word or the separator
    double percent = 0.0;  // the value behind a percentage, which picks its ink
};
QList<ReadingPart> readingParts(const Sample &sample, bool cpuOnly = false);

// "  ·  cpu 07% · mem 00%" for a tab label: both halves always, each percent two digits wide
// (card #MERX, owner 2026-09-19: "always show cpu 00% mem 00% and 01% or 05%, always use 2
// digits, so they dont keep on widening and narrowing"). This is the one surface that does not
// print readingText(): the chip leaves a half with nothing to say out because the header row has
// room to breathe, but a tab suffix that appears, disappears and widens as work comes and goes
// shuffles the whole bar's layout — so idle is "cpu 00% · mem 00%" and the width never moves.
// A Sample with no reading behind it (nothing measured yet, or nothing to measure) formats as
// 00/00 all the same; the caller decides whether the tab has panes to measure at all.
QString tabSuffix(const Sample &sample);

// The tab bar's first give-way rung (#VWSD): retain every suffix only when the full, natural
// widths of all tab labels fit its usable width. This deliberately knows no QWidget, so the
// answer cannot feed back from labels whose suffix is currently hidden.
bool tabMetersFit(int barWidth, const QList<int> &fullLabelWidths);

// readingText() under its old name, for the session manager's rows. Empty when there is nothing
// worth showing.
QString liveTag(const Sample &sample);

// "cpu 12% · mem 3% (1.2 GiB)" — the long form for tooltips: the same wording as everywhere else,
// with the memory figure in bytes added, and both halves printed whether or not they clear the
// floors, because a tooltip is the place that spells things out.
QString describe(const Sample &sample);

// How many processes a breakdown names. The chip stays the sum alone; the tooltips owe the
// reader *what* is busy, and a handful of lines answers that without becoming a process list.
inline constexpr int kTopProcesses = 5;

// Sorts rows by CPU, then memory, then pid (so equal rows keep a stable order), drops the ones
// whose percentages both round to 0 — a line reading "cpu 0% · mem 0%" names nothing — and keeps
// the first `limit`.
QList<ProcessUsage> topProcesses(QList<ProcessUsage> rows, int limit = kTopProcesses);

// "cc1plus · cpu 30% · mem 2%" — one breakdown line, in the same words and the same whole
// percents as everything else here.
QString processLine(const ProcessUsage &row);
QStringList processLines(const Sample &sample);
// The lines as one block, empty when no process has anything to say.
QString processBreakdown(const Sample &sample);

// The sentence a tooltip owes the reader about the memory figure: it is a sum of resident sets
// over the tree (and, for a tab, over its panes), so pages two processes share are counted twice.
QString memoryNote();

// Whether the meters are on at all (`appearance/pane_usage`). The chip, the tab suffix, the tab
// tooltip's usage line and the Sessions row's tag all ask here, so one setting turns off all of
// them, which is what the settings row promises.
bool metersEnabled();

// The tab label's pace (card #MERX, owner 2026-09-19: "update only once every ~5 secs or so,
// using the 5 sec average"). The poll still measures at 2.5 Hz — the pane chip and the Sessions
// tag want that — but the tab label is refreshed from a window, not from the last poll: the mean
// of the kTabWindowMs before it, taken once every kTabUpdateMs.
inline constexpr qint64 kTabUpdateMs = 5000;
inline constexpr qint64 kTabWindowMs = 5000;

// The window itself: samples go in as the poll reads them, and average() is the plain mean of
// what kTabWindowMs still holds. The poll's cadence is even, so a mean of samples is a mean over
// time. Invalid samples — a pane that has not been measured twice yet — are left out of the
// numbers rather than counted as zeros; the average is valid when one entry was, and an empty
// window averages to an invalid Sample (the label reads 00/00 for the window it takes to fill).
class RollingMean {
public:
    void add(const Sample &sample, qint64 atMs);
    Sample average() const;
    void clear() { m_entries.clear(); }

private:
    struct Entry {
        bool valid = false;
        qint64 atMs = 0;
        double cpuPercent = 0.0;
        double ramPercent = 0.0;
        qint64 ramBytes = 0;
    };
    QList<Entry> m_entries;
};

}  // namespace relay::usage
