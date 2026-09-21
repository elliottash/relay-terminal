// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PaneUsage.h"

#include "SettingsCache.h"   // metersEnabled() is on the status poll, once per pane (#057J)

#include <QDir>
#include <QFile>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#else
#include <unistd.h>
#endif

namespace relay::usage {

namespace {

#ifndef Q_OS_WIN
// sysconf answers are constant for the life of the process; ask once.
template <int Name>
qint64 cachedSysconf()
{
    static const qint64 value = ::sysconf(Name);
    return value;
}

qint64 pageSizeBytes() { return cachedSysconf<_SC_PAGESIZE>(); }

#endif

// The root every path here is built from. A global rather than an argument threaded through the
// walk: only a test ever moves it, and it moves it before anything is running.
QString g_procRoot = QStringLiteral("/proc");

#ifndef Q_OS_WIN
QString procPath(qint64 pid, const QString &leaf)
{
    return QStringLiteral("%1/%2/%3").arg(g_procRoot).arg(pid).arg(leaf);
}

bool readStat(qint64 pid, ProcessInfo *info)
{
    QFile stat(procPath(pid, QStringLiteral("stat")));
    if (!stat.open(QIODevice::ReadOnly)) return false;
    return parseStat(stat.readAll(), info);
}

// /proc/<pid>/statm: "size resident shared text lib data dt", in pages. Field 2 is what top calls
// resident — cheaper to read than /proc/<pid>/status and the same number.
bool readResident(qint64 pid, qint64 *bytes)
{
    QFile statm(procPath(pid, QStringLiteral("statm")));
    if (!statm.open(QIODevice::ReadOnly)) return false;
    const QList<QByteArray> fields = statm.readAll().simplified().split(' ');
    if (fields.size() < 2) return false;
    bool ok = false;
    const qint64 pages = fields.at(1).toLongLong(&ok);
    if (!ok || pages < 0) return false;
    *bytes = pages * pageSizeBytes();
    return true;
}


#endif
}  // namespace

bool parseStat(const QByteArray &line, ProcessInfo *info)
{
    // "pid (comm) state ppid ...". Past the last ')' the first field is state (field 3), so
    // field N is at offset N - 3: ppid 4, utime 14, stime 15, cutime 16, cstime 17, starttime 22.
    const int close = int(line.lastIndexOf(')'));
    if (close < 0 || close + 1 >= line.size()) return false;
    const QList<QByteArray> fields = line.mid(close + 1).simplified().split(' ');
    if (fields.size() < 15) return false;
    qint64 total = 0;
    for (int index : {11, 12, 13, 14}) {
        bool ok = false;
        const qint64 value = fields.at(index).toLongLong(&ok);
        if (!ok) return false;
        // cutime/cstime are signed and a child's clock can be set back; a negative would make the
        // tree's total go backwards, which the caller would read as a tree that shrank.
        total += std::max<qint64>(0, value);
    }
    info->ticks = total;
    if (!fields.at(0).isEmpty()) info->state = char(fields.at(0).at(0));
    info->ppid = fields.at(1).toLongLong();
    // starttime is only wanted as an identity, so an old kernel with a short line loses the
    // recycled-pid guard rather than the whole reading.
    info->startTicks = fields.size() > 19 ? std::max<qint64>(0, fields.at(19).toLongLong()) : 0;
    const int open = int(line.indexOf('('));
    if (open >= 0 && open < close) info->comm = QString::fromUtf8(line.mid(open + 1, close - open - 1));
    if (const qint64 pid = line.left(std::max(0, open)).simplified().toLongLong(); pid > 0) info->pid = pid;
    return true;
}

bool parseStatTicks(const QByteArray &line, qint64 *ticks)
{
    ProcessInfo info;
    if (!parseStat(line, &info)) return false;
    *ticks = info.ticks;
    return true;
}

QString procRoot() { return g_procRoot; }

void setProcRoot(const QString &root) { g_procRoot = root; }

QList<ProcessInfo> walkTrees(const QList<qint64> &roots, int cap, Detail detail)
{
#ifdef Q_OS_WIN
    QList<ProcessInfo> out;
    if (cap <= 0 || roots.isEmpty()) return out;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return out;
    QHash<qint64, PROCESSENTRY32W> entries;
    QMultiHash<qint64, qint64> children;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) do {
        entries.insert(entry.th32ProcessID, entry);
        children.insert(entry.th32ParentProcessID, entry.th32ProcessID);
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    QList<qint64> queue;
    QSet<qint64> seen;
    for (qint64 pid : roots) {
        if (pid > 0 && entries.contains(pid) && !seen.contains(pid) && queue.size() < cap) {
            seen.insert(pid);
            queue.append(pid);
        }
    }
    const auto ticks = [](FILETIME value) {
        return qint64((quint64(value.dwHighDateTime) << 32) | value.dwLowDateTime);
    };
    for (int i = 0; i < queue.size() && i < cap; ++i) {
        const qint64 pid = queue.at(i);
        const auto row = entries.value(pid);
        ProcessInfo info;
        info.pid = pid;
        info.ppid = row.th32ParentProcessID;
        info.comm = QString::fromWCharArray(row.szExeFile);
        if (detail == Detail::Counters) {
            HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, DWORD(pid));
            if (!process) process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
            if (process) {
                FILETIME created{}, exited{}, kernel{}, user{};
                if (GetProcessTimes(process, &created, &exited, &kernel, &user)) {
                    info.startTicks = ticks(created);
                    info.ticks = ticks(kernel) + ticks(user);
                }
                PROCESS_MEMORY_COUNTERS memory{};
                memory.cb = sizeof(memory);
                if (GetProcessMemoryInfo(process, &memory, sizeof(memory)))
                    info.rssBytes = qint64(memory.WorkingSetSize);
                CloseHandle(process);
            }
        }
        out.append(info);
        for (qint64 child : children.values(pid)) {
            if (!seen.contains(child) && queue.size() < cap) {
                seen.insert(child);
                queue.append(child);
            }
        }
    }
    // These are the live processes' counters. Unlike wait()/proc on Linux, Windows
    // does not transfer reaped children's CPU time into their parent's counters.
    return out;
#else
    QList<ProcessInfo> out;
    if (cap <= 0) return out;
    QList<qint64> queue;
    QSet<qint64> seen;
    for (qint64 root : roots)
        if (root > 0 && !seen.contains(root)) { seen.insert(root); queue.append(root); }
    for (int i = 0; i < queue.size() && i < cap; ++i) {
        const qint64 pid = queue.at(i);
        ProcessInfo info;
        info.pid = pid;
        if (detail == Detail::Counters) {
            ProcessInfo read;
            read.pid = pid;
            if (readStat(pid, &read)) info = read;
            info.pid = pid;   // the walk's number wins; a stat line is only ever read by pid
            qint64 resident = 0;
            if (readResident(pid, &resident)) info.rssBytes = resident;
        }
        out.append(info);
        // Every thread's children, not only the main thread's: a thread that forks is the
        // child's parent, and /proc/<pid>/task/<pid>/children does not list it. The Python
        // worker spawning a subprocess off a worker thread is the case this exists for.
        const QDir tasks(QStringLiteral("%1/%2/task").arg(g_procRoot).arg(pid));
        for (const QString &tid : tasks.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort)) {
            QFile children(tasks.filePath(tid) + QStringLiteral("/children"));
            if (!children.open(QIODevice::ReadOnly)) continue;
            for (const QByteArray &child : children.readAll().simplified().split(' ')) {
                if (child.isEmpty()) continue;
                bool ok = false;
                const qint64 next = child.toLongLong(&ok);
                if (ok && next > 0 && !seen.contains(next) && seen.size() < cap) {
                    seen.insert(next);
                    queue.append(next);
                }
            }
        }
    }
    return out;
#endif
}

Reading summarize(const QList<ProcessInfo> &procs)
{
    Reading out;
    out.ok = true;
    for (const ProcessInfo &info : procs) {
        out.ticks += info.ticks;
        out.rssBytes += info.rssBytes;
    }
    return out;
}

Reading readTrees(const QList<qint64> &roots)
{
    if (roots.isEmpty()) return {};
    return summarize(walkTrees(roots));
}

Sample Meter::update(const QList<qint64> &roots)
{
    QList<Root> named;
    named.reserve(int(roots.size()));
    for (qint64 pid : roots) named.append(Root{pid, {}});
    return update(named);
}

Sample Meter::update(const QList<Root> &roots)
{
    const qint64 nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch()).count();
    // Nothing to measure is a missed reading, and the baseline goes with it — the same answer
    // readTrees({}) used to give.
    if (roots.isEmpty()) return compute(Reading{}, nowMs);
    QList<qint64> pids;
    QHash<qint64, QString> labels;
    pids.reserve(int(roots.size()));
    for (const Root &root : roots) {
        if (root.pid <= 0) continue;
        pids.append(root.pid);
        if (!root.label.isEmpty()) labels.insert(root.pid, root.label);
    }
    return compute(walkTrees(pids), nowMs, labels);
}

Sample Meter::compute(const QList<ProcessInfo> &procs, qint64 nowMs, const QHash<qint64, QString> &labels)
{
    // compute(Reading) below moves the baseline on, so the interval has to be read first.
    const qint64 previousMs = m_lastMs;
    Sample sample = compute(summarize(procs), nowMs);
    const qint64 elapsed = nowMs - previousMs;
    QHash<qint64, PidTicks> nextPids;
    nextPids.reserve(int(procs.size()));
    QList<ProcessUsage> rows;
    rows.reserve(int(procs.size()));
    for (const ProcessInfo &info : procs) {
        if (info.pid <= 0) continue;
        nextPids.insert(info.pid, PidTicks{info.startTicks, info.ticks});
        ProcessUsage row;
        row.pid = info.pid;
        row.name = labels.value(info.pid, info.comm.isEmpty()
                                              ? QStringLiteral("pid %1").arg(info.pid)
                                              : info.comm);
        row.ramBytes = info.rssBytes;
        row.ramPercent = ramPercentOf(info.rssBytes, totalMemoryBytes());
        const auto previous = m_lastPids.constFind(info.pid);
        // A pid the kernel handed out again is a different process: its counters do not continue
        // the old one's, and subtracting them would book a whole lifetime to one interval.
        if (sample.valid && previous != m_lastPids.constEnd() && previous->startTicks == info.startTicks)
            row.cpuPercent = cpuPercentOf(std::max<qint64>(0, info.ticks - previous->ticks), elapsed,
                                          processorCount(), clockTicksPerSecond());
        rows.append(row);
    }
    // Recorded even on the poll that only sets the baseline, so the next one has deltas; dropped
    // with the baseline when a reading is missed (compute(Reading) calls reset()).
    if (m_hasLast) m_lastPids = nextPids;
    if (sample.valid) sample.processes = topProcesses(rows);
    return sample;
}

Sample Meter::compute(const Reading &reading, qint64 nowMs)
{
    Sample sample;
    if (!reading.ok) {
        // Nothing was readable this time. The baseline goes with it: keeping the last one and
        // pretending the tree was at zero ticks would make the next good reading a whole tree's
        // lifetime over one interval, which clamps to 100 % — the meter would flash "100%" every
        // time a pane's processes blinked out of sight for a tick.
        reset();
        return sample;
    }
    if (!m_hasLast) {
        m_hasLast = true;
        m_lastTicks = reading.ticks;
        m_lastMs = nowMs;
        return sample;   // the baseline: no percentage until a second reading exists
    }
    const qint64 previousMs = std::exchange(m_lastMs, nowMs);
    const qint64 previousTicks = std::exchange(m_lastTicks, reading.ticks);
    const qint64 elapsed = nowMs - previousMs;
    if (elapsed <= 0) return sample;
    sample.valid = true;
    const qint64 delta = std::max<qint64>(0, reading.ticks - previousTicks);
    sample.cpuPercent = cpuPercentOf(delta, elapsed, processorCount(), clockTicksPerSecond());
    sample.ramBytes = reading.rssBytes;
    sample.ramPercent = ramPercentOf(reading.rssBytes, totalMemoryBytes());
    return sample;
}

void Meter::reset()
{
    m_hasLast = false;
    m_lastTicks = 0;
    m_lastMs = 0;
    m_lastPids.clear();
}

#ifdef Q_OS_WIN
int processorCount() { return int(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)); }
qint64 totalMemoryBytes() {
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    return GlobalMemoryStatusEx(&memory) ? qint64(memory.ullTotalPhys) : 0;
}
qint64 clockTicksPerSecond() { return 10000000; } // FILETIME units: 100 ns
#else
int processorCount() { return int(cachedSysconf<_SC_NPROCESSORS_ONLN>()); }

qint64 totalMemoryBytes() { return cachedSysconf<_SC_PHYS_PAGES>() * pageSizeBytes(); }

qint64 clockTicksPerSecond() { return cachedSysconf<_SC_CLK_TCK>(); }
#endif

double cpuPercentOf(qint64 tickDelta, qint64 elapsedMs, int cores, qint64 clkTck)
{
    if (elapsedMs <= 0 || cores <= 0 || clkTck <= 0) return 0.0;
    // Ticks are CPU-seconds * clkTck; against the wall clock they are cores in use, which over
    // every core together is the percentage of the machine this tree is taking.
    const double cpuSeconds = double(tickDelta) / double(clkTck);
    const double coresUsed = cpuSeconds / (double(elapsedMs) / 1000.0);
    return std::clamp(coresUsed / double(cores) * 100.0, 0.0, 100.0);
}

double ramPercentOf(qint64 bytes, qint64 totalBytes)
{
    if (totalBytes <= 0 || bytes < 0) return 0.0;
    return std::clamp(double(bytes) / double(totalBytes) * 100.0, 0.0, 100.0);
}

QString formatPercent(double percent)
{
    return QString::number(int(std::lround(percent)));
}

QString formatPercent2(double percent)
{
    return formatPercent(percent).rightJustified(2, QLatin1Char('0'));
}

bool showsCpu(const Sample &sample)
{
    return sample.valid && sample.cpuPercent >= 0.5;
}

bool showsMemory(const Sample &sample)
{
    return sample.valid && sample.ramBytes >= kMemoryFloorBytes && sample.ramPercent >= 0.5;
}

bool worthShowing(const Sample &sample)
{
    return showsCpu(sample) || showsMemory(sample);
}

Sample combined(const QList<Sample> &samples)
{
    Sample out;
    QList<ProcessUsage> rows;
    for (const Sample &sample : samples) {
        if (!sample.valid) continue;
        out.valid = true;
        out.cpuPercent += sample.cpuPercent;
        out.ramBytes += sample.ramBytes;
        out.ramPercent += sample.ramPercent;
        rows += sample.processes;
    }
    out.cpuPercent = std::clamp(out.cpuPercent, 0.0, 100.0);
    out.ramPercent = std::clamp(out.ramPercent, 0.0, 100.0);
    out.processes = topProcesses(rows);
    return out;
}

QList<ProcessUsage> topProcesses(QList<ProcessUsage> rows, int limit)
{
    // A row whose two percentages both print as "0" names nothing the reader did not already
    // know from the chip, and an idle shell's tree is mostly such rows.
    rows.erase(std::remove_if(rows.begin(), rows.end(),
                              [](const ProcessUsage &row) {
                                  return row.cpuPercent < 0.5 && row.ramPercent < 0.5;
                              }),
               rows.end());
    std::sort(rows.begin(), rows.end(), [](const ProcessUsage &a, const ProcessUsage &b) {
        if (a.cpuPercent != b.cpuPercent) return a.cpuPercent > b.cpuPercent;
        if (a.ramBytes != b.ramBytes) return a.ramBytes > b.ramBytes;
        return a.pid < b.pid;   // equal rows keep one order, so the tooltip does not shuffle
    });
    if (limit >= 0 && int(rows.size()) > limit) rows.erase(rows.begin() + limit, rows.end());
    return rows;
}

QString processLine(const ProcessUsage &row)
{
    // Same word order as readingText(), so a tooltip's first line and the lines under it read
    // the same way round rather than mirroring each other (issue #6BGA).
    return QStringLiteral("%1 · cpu %2% · mem %3%")
        .arg(row.name, formatPercent(row.cpuPercent), formatPercent(row.ramPercent));
}

QStringList processLines(const Sample &sample)
{
    QStringList lines;
    if (!sample.valid) return lines;
    for (const ProcessUsage &row : sample.processes) lines << processLine(row);
    return lines;
}

QString processBreakdown(const Sample &sample)
{
    return processLines(sample).join(QStringLiteral("\n"));
}

// The separator between the two halves, and the words in front of each number. Written as the
// character, not as escaped UTF-8 bytes: QStringLiteral builds a UTF-16 literal out of whatever
// bytes it is handed, so "\xc2\xb7" came out as the two characters "Â·" and a tab read
// "src Â· 5% cpu".
static QString usageSeparator()
{
    return QStringLiteral(" · ");
}
static QString cpuPiece(const Sample &sample)
{
    return QStringLiteral("cpu %1%").arg(formatPercent(sample.cpuPercent));
}
static QString memoryPiece(const Sample &sample)
{
    return QStringLiteral("mem %1%").arg(formatPercent(sample.ramPercent));
}

QList<ReadingPart> readingParts(const Sample &sample, bool cpuOnly)
{
    // A half with nothing to say is left out entirely rather than printed as "0%": an idle
    // pane's agent worker used to hold the chip open reading "0% / 1%".
    const bool cpu = showsCpu(sample), memory = showsMemory(sample) && !cpuOnly;
    QList<ReadingPart> out;
    if (!cpu && !memory) return out;
    // The word is the header's muted ink whatever the reading is; only the number warns.
    if (cpu) {
        out << ReadingPart{QStringLiteral("cpu "), false, 0.0};
        out << ReadingPart{formatPercent(sample.cpuPercent) + QStringLiteral("%"), true,
                           sample.cpuPercent};
    }
    if (cpu && memory) out << ReadingPart{usageSeparator(), false, 0.0};
    if (memory) {
        out << ReadingPart{QStringLiteral("mem "), false, 0.0};
        out << ReadingPart{formatPercent(sample.ramPercent) + QStringLiteral("%"), true,
                           sample.ramPercent};
    }
    return out;
}

QString readingText(const Sample &sample, bool cpuOnly)
{
    QString out;
    for (const ReadingPart &part : readingParts(sample, cpuOnly)) out += part.text;
    return out;
}

QString tabSuffix(const Sample &sample)
{
    // Both halves, always, two digits each (card #MERX): the text's width never moves, so the
    // tab bar's layout never shuffles. readingText()'s rule — a quiet half is left out — is the
    // chip's, not the tab's.
    // The tab bar's own separator convention, which every other thing on a tab label uses.
    return QStringLiteral("  ·  cpu %1% · mem %2%")
        .arg(formatPercent2(sample.cpuPercent), formatPercent2(sample.ramPercent));
}

bool tabMetersFit(int barWidth, const QList<int> &fullLabelWidths)
{
    int total = 0;
    for (int width : fullLabelWidths) total += width;
    return total <= barWidth;
}

QString liveTag(const Sample &sample)
{
    return readingText(sample);
}

QString describe(const Sample &sample)
{
    if (!sample.valid) return {};
    const double gib = double(sample.ramBytes) / (1024.0 * 1024.0 * 1024.0);
    const QString memory = gib >= 1.0 ? QStringLiteral("%1 GiB").arg(gib, 0, 'f', 1)
                                      : QStringLiteral("%1 MiB").arg(sample.ramBytes / (1024 * 1024));
    // The same wording as the chip and the label, with the byte figure the tooltip owes on top.
    // Both halves print here even when they do not clear the floors the chip is held to: a
    // tooltip is asked for, and the reader who asked wants the number rather than a gap.
    return cpuPiece(sample) + usageSeparator() + memoryPiece(sample)
           + QStringLiteral(" (%1)").arg(memory);
}

QString memoryNote()
{
    return QStringLiteral("Memory is resident set summed over those processes, so pages they "
                          "share with each other are counted more than once.");
}

bool metersEnabled()
{
    // Read through the hot-path cache (card #057J): the 400 ms status poll asks once for the
    // window and once more for every pane in it, and a QSettings construction each time was
    // 75 statx a second at one idle pane, for one boolean. Options drops the cache when it
    // writes the row, so the toggle still takes effect on the next poll.
    return relay::settings::boolValue(QStringLiteral("appearance/pane_usage"), true);
}

void RollingMean::add(const Sample &sample, qint64 atMs)
{
    // The window is the kTabWindowMs before the newest entry: whatever is older than that has
    // had its turn on the label and is dropped, so a stall does not leave a stale reading
    // steering the average forever.
    while (!m_entries.isEmpty() && atMs - m_entries.first().atMs > kTabWindowMs)
        m_entries.removeFirst();
    // A sample with no reading behind it is kept for the window's length — time is what the
    // window is measured in — but its numbers are not averaged in (average() skips it): an
    // unread pane was not using nothing, it was not measured, the same rule combined() keeps.
    m_entries.append(Entry{sample.valid, atMs, sample.cpuPercent, sample.ramPercent,
                           sample.ramBytes});
}

Sample RollingMean::average() const
{
    Sample out;
    int counted = 0;
    for (const Entry &entry : m_entries) {
        if (!entry.valid) continue;
        out.cpuPercent += entry.cpuPercent;
        out.ramPercent += entry.ramPercent;
        out.ramBytes += entry.ramBytes;
        ++counted;
    }
    if (counted == 0) return out;   // nothing measured: an invalid Sample, read as 00/00
    out.valid = true;
    out.cpuPercent /= counted;
    out.ramPercent /= counted;
    out.ramBytes /= counted;
    return out;
}

}  // namespace relay::usage
