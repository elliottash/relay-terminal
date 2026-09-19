// SPDX-License-Identifier: GPL-3.0-or-later
#include "PaneUsage.h"

#include <QFile>
#include <QSet>

#include <chrono>
#include <cmath>
#include <utility>

#include <unistd.h>

#ifndef Q_OS_UNIX
#error "relay::usage reads /proc; this file is Linux-only (see docs/ARCHITECTURE.md, platform notes)"
#endif

namespace relay::usage {

namespace {

// sysconf answers are constant for the life of the process; ask once.
template <int Name>
qint64 cachedSysconf()
{
    static const qint64 value = ::sysconf(Name);
    return value;
}

qint64 pageSizeBytes() { return cachedSysconf<_SC_PAGESIZE>(); }

// /proc/<pid>/stat: "pid (comm) state ppid ...". The comm field may contain spaces and
// parentheses, so the fields are counted after the last ')'. utime and stime are fields 14 and
// 15, which is offsets 11 and 12 once the first three are dropped.
bool readStat(qint64 pid, qint64 *ticks)
{
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly)) return false;
    const QByteArray line = stat.readAll();
    const int close = int(line.lastIndexOf(')'));
    if (close < 0 || close + 1 >= line.size()) return false;
    const QList<QByteArray> fields = line.mid(close + 1).simplified().split(' ');
    if (fields.size() < 13) return false;
    bool okUser = false, okSystem = false;
    const qint64 user = fields.at(11).toLongLong(&okUser);
    const qint64 system = fields.at(12).toLongLong(&okSystem);
    if (!okUser || !okSystem) return false;
    *ticks = user + system;
    return true;
}

// /proc/<pid>/statm: "size resident shared text lib data dt", in pages. Field 2 is what top calls
// resident — cheaper to read than /proc/<pid>/status and the same number.
bool readResident(qint64 pid, qint64 *bytes)
{
    QFile statm(QStringLiteral("/proc/%1/statm").arg(pid));
    if (!statm.open(QIODevice::ReadOnly)) return false;
    const QList<QByteArray> fields = statm.readAll().simplified().split(' ');
    if (fields.size() < 2) return false;
    bool ok = false;
    const qint64 pages = fields.at(1).toLongLong(&ok);
    if (!ok || pages < 0) return false;
    *bytes = pages * pageSizeBytes();
    return true;
}

}  // namespace

Reading readTrees(const QList<qint64> &roots)
{
    Reading out;
    if (roots.isEmpty()) return out;
    QList<qint64> queue;
    QSet<qint64> seen;
    for (qint64 root : roots)
        if (root > 0 && !seen.contains(root)) { seen.insert(root); queue.append(root); }
    for (int i = 0; i < queue.size() && seen.size() < 256; ++i) {
        const qint64 pid = queue.at(i);
        qint64 ticks = 0, resident = 0;
        if (readStat(pid, &ticks)) out.ticks += ticks;
        if (readResident(pid, &resident)) out.rssBytes += resident;
        QFile children(QStringLiteral("/proc/%1/task/%2/children").arg(pid).arg(pid));
        if (children.open(QIODevice::ReadOnly)) {
            for (const QByteArray &child : children.readAll().simplified().split(' ')) {
                if (child.isEmpty()) continue;
                bool ok = false;
                const qint64 next = child.toLongLong(&ok);
                if (ok && next > 0 && !seen.contains(next) && seen.size() < 256) {
                    seen.insert(next);
                    queue.append(next);
                }
            }
        }
    }
    out.ok = true;
    return out;
}

Sample Meter::update(const QList<qint64> &roots)
{
    const qint64 nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch()).count();
    return compute(readTrees(roots), nowMs);
}

Sample Meter::compute(const Reading &reading, qint64 nowMs)
{
    Sample sample;
    if (!m_hasLast) {
        m_hasLast = true;
        m_lastTicks = reading.ok ? reading.ticks : 0;
        m_lastMs = nowMs;
        return sample;   // the baseline: no percentage until a second reading exists
    }
    const qint64 previousMs = std::exchange(m_lastMs, nowMs);
    const qint64 previousTicks = std::exchange(m_lastTicks, reading.ok ? reading.ticks : 0);
    const qint64 elapsed = nowMs - previousMs;
    if (!reading.ok || elapsed <= 0) return sample;
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
}

int processorCount() { return int(cachedSysconf<_SC_NPROCESSORS_ONLN>()); }

qint64 totalMemoryBytes() { return cachedSysconf<_SC_PHYS_PAGES>() * pageSizeBytes(); }

qint64 clockTicksPerSecond() { return cachedSysconf<_SC_CLK_TCK>(); }

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

bool worthShowing(const Sample &sample)
{
    return sample.cpuPercent >= 0.5 || sample.ramPercent >= 0.5;
}

Sample combined(const QList<Sample> &samples)
{
    Sample out;
    for (const Sample &sample : samples) {
        if (!sample.valid) continue;
        out.valid = true;
        out.cpuPercent += sample.cpuPercent;
        out.ramBytes += sample.ramBytes;
        out.ramPercent += sample.ramPercent;
    }
    out.cpuPercent = std::clamp(out.cpuPercent, 0.0, 100.0);
    out.ramPercent = std::clamp(out.ramPercent, 0.0, 100.0);
    return out;
}

QString tabSuffix(const Sample &sample)
{
    if (!sample.valid || !worthShowing(sample)) return {};
    return QStringLiteral("  \xc2\xb7  %1% / %2%")
        .arg(formatPercent(sample.cpuPercent), formatPercent(sample.ramPercent));
}

QString liveTag(const Sample &sample)
{
    if (!sample.valid || !worthShowing(sample)) return {};
    return QStringLiteral("cpu %1% \xc2\xb7 mem %2%")
        .arg(formatPercent(sample.cpuPercent), formatPercent(sample.ramPercent));
}

QString describe(const Sample &sample)
{
    if (!sample.valid) return {};
    const double gib = double(sample.ramBytes) / (1024.0 * 1024.0 * 1024.0);
    const QString memory = gib >= 1.0 ? QStringLiteral("%1 GiB").arg(gib, 0, 'f', 1)
                                      : QStringLiteral("%1 MiB").arg(sample.ramBytes / (1024 * 1024));
    return QStringLiteral("CPU %1% \xc2\xb7 memory %2 (%3%)")
        .arg(formatPercent(sample.cpuPercent), memory, formatPercent(sample.ramPercent));
}

}  // namespace relay::usage
