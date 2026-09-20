// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Per-pane process isolation: each pane's shell and agent worker run in their own transient
// systemd user scope, so a runaway command is stopped inside its pane instead of taking Relay down
// with it. Free functions over `systemd-run` and `systemctl`, with the probes cached; Qt only.

#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>

#include <unistd.h>

// ----- per-pane process isolation ----------------------------------------------------------
//
// Each pane's shell and agent worker run in their own transient systemd user scope with memory
// limits, so a runaway command is stopped inside its pane instead of taking down Relay (the kernel
// OOM killer and systemd-oomd otherwise act on Relay's whole app cgroup). `systemd-run --scope`
// execs the command in place, so the PID Relay tracks is still bash's / python's own.
namespace isolation {

inline bool enabled() { return QSettings().value(QStringLiteral("isolation/enabled"), true).toBool(); }

// systemd-run exists and the user manager accepts transient scopes; probed once per process.
//
// The probe is a subprocess, and it used to be run — and waited on for up to three seconds — from
// the first pane's constructor, i.e. before the first window was shown. On a healthy machine
// `systemd-run --user --scope --quiet -- true` answers in 0–10 ms, but on one whose `systemd --user`
// or D-Bus is not answering that was three seconds of window-less Relay with nothing on screen to
// say why (#GMCF, decision 5; startup finding 3a). So main() starts the probe before it builds
// anything and nothing waits for it there; the first consumer blocks for at most kProbeWaitMs, and
// a probe that has not answered by then leaves the answer *unknown*: that pane runs unisolated —
// the same path a machine without systemd-run takes, "isolation unavailable" notice and all — and
// the next pane picks up the late answer. Once answered it is cached for the process, as before.
namespace detail {
inline int &probeState() { static int state = -1; return state; }        // -1 unknown, 0 no, 1 yes
inline QProcess *&probeProcess() { static QProcess *process = nullptr; return process; }
inline bool &probeWaited() { static bool waited = false; return waited; }
}

inline constexpr int kProbeWaitMs = 300;       // a local systemd-run slower than this is not usable now
inline constexpr int kProbeGiveUpMs = 10000;   // ...and one still silent after this never will be

// Start the probe without waiting for it. Called from main() before the first window, and by
// available() itself when nobody did (a test, a pane in a process with no GUI). A no-op once the
// answer is in or a probe is already running.
inline void beginProbe() {
    if (detail::probeState() >= 0 || detail::probeProcess()) return;
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("systemd-run"));
    if (tool.isEmpty()) { detail::probeState() = 0; return; }
    auto *probe = new QProcess;
    detail::probeProcess() = probe;
    auto answer = [probe](int state) {
        detail::probeState() = state;
        if (detail::probeProcess() == probe) detail::probeProcess() = nullptr;
        probe->deleteLater();
    };
    QObject::connect(probe, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), probe,
                     [answer](int code, QProcess::ExitStatus exit) {
        answer(exit == QProcess::NormalExit && code == 0 ? 1 : 0);
    });
    QObject::connect(probe, &QProcess::errorOccurred, probe, [answer](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) answer(0);   // anything later still reaches finished()
    });
    // Nothing reaps a systemd-run that never returns, and the old code's timeout at least ended it.
    // Same end, off the GUI thread: kill it, and the finished handler calls isolation unavailable.
    QTimer::singleShot(kProbeGiveUpMs, probe, [probe] { if (probe->state() != QProcess::NotRunning) probe->kill(); });
    probe->start(tool, {QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("--quiet"), QStringLiteral("--"), QStringLiteral("true")});
}

inline bool available() {
    if (detail::probeState() < 0) {
        beginProbe();
        QProcess *probe = detail::probeProcess();
        // Exactly one consumer ever blocks on an outstanding probe: a pane asks three times while
        // it starts (the worker's environment, the worker's scope, the shell's scope), and three
        // 300 ms waits each would be the freeze this exists to remove. Everyone after the first
        // takes the unknown answer and runs unisolated until the event loop delivers the real one.
        if (probe && !detail::probeWaited()) {
            detail::probeWaited() = true;
            probe->waitForFinished(kProbeWaitMs);   // emits finished(), so the handler above answers
        }
    }
    return detail::probeState() == 1;
}

// Total RAM / swap in bytes, from /proc/meminfo; 0 when it cannot be read.
inline qulonglong memInfo(const char *key) {
    QFile file(QStringLiteral("/proc/meminfo"));
    if (!file.open(QIODevice::ReadOnly)) return 0;
    const QByteArray prefix = QByteArray(key) + ':';
    for (const QByteArray &row : file.readAll().split('\n'))
        if (row.startsWith(prefix)) return row.mid(prefix.size()).trimmed().split(' ').first().toULongLong() * 1024;
    return 0;
}

// Defaults scaled to the machine, so the same binary protects a 121G workstation and an 8G
// laptop: the caps exist to stop one pane taking the machine down, so they follow its size.
// An unreadable /proc/meminfo falls back to the old flat defaults. Rounded to whole GiB.
inline QString sized(qulonglong bytes) {
    return QString::number(std::max<qulonglong>(1, (bytes + (1ULL << 29)) >> 30)) + QStringLiteral("G");
}

// Agent worker: clamp(RAM/16, 2G, 8G) — a long conversation plus the builds it runs needs
// multi-GB, while a small machine keeps a 2G floor.
inline QString agentDefault() {
    const qulonglong ram = memInfo("MemTotal");
    return ram ? sized(std::min(8ULL << 30, std::max(2ULL << 30, ram / 16))) : QStringLiteral("2G");
}

// Pane shell: clamp(RAM/2, 4G, 16G) — shells run whatever the user runs, so they get more.
inline QString shellDefault() {
    const qulonglong ram = memInfo("MemTotal");
    return ram ? sized(std::min(16ULL << 30, std::max(4ULL << 30, ram / 2))) : QStringLiteral("8G");
}

// Swap caps: a slice of total swap, floored so a kill still comes quickly.
inline QString agentSwapDefault() {
    const qulonglong swap = memInfo("SwapTotal");
    return swap ? sized(std::min(2ULL << 30, std::max(512ULL << 20, swap / 8))) : QStringLiteral("512M");
}

inline QString shellSwapDefault() {
    const qulonglong swap = memInfo("SwapTotal");
    return swap ? sized(std::min(4ULL << 30, std::max(1ULL << 30, swap / 4))) : QStringLiteral("2G");
}

// A systemd size such as "8G", "512M" or "infinity"; anything else falls back to the default.
inline QString memory(const char *key, const QString &fallback) {
    const QString value = QSettings().value(QString::fromLatin1(key), fallback).toString().trimmed();
    static const QRegularExpression valid(QStringLiteral("^(\\d+[KMGT]?|infinity)$"));
    return valid.match(value).hasMatch() ? value : fallback;
}

// A valid systemd size in bytes, or 0 when it does not parse.
inline qulonglong parseSize(const QString &size) {
    QString digits = size;
    qulonglong multiplier = 1;
    if (!digits.isEmpty() && !digits.back().isDigit()) {
        const QChar unit = digits.back();
        digits.chop(1);
        if (unit == QLatin1Char('K')) multiplier = 1ULL << 10;
        else if (unit == QLatin1Char('M')) multiplier = 1ULL << 20;
        else if (unit == QLatin1Char('G')) multiplier = 1ULL << 30;
        else if (unit == QLatin1Char('T')) multiplier = 1ULL << 40;
        else return 0;
    }
    bool ok = false;
    const qulonglong value = digits.toULongLong(&ok);
    return ok ? value * multiplier : 0;
}

// `percent`% of a resolved size, so MemoryHigh follows whatever MemoryMax resolved to;
// "infinity" and unparseable sizes pass through unchanged.
inline QString fractionOf(const QString &size, int percent) {
    const qulonglong bytes = parseSize(size);
    return bytes ? sized(bytes * percent / 100) : size;
}

// Arguments that run `command` inside the named scope.
inline QStringList wrap(const QString &unit, const QStringList &properties, const QStringList &command) {
    QStringList args{QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("--quiet"), QStringLiteral("--unit=") + unit};
    for (const QString &property : properties) args << QStringLiteral("-p") << property;
    args << QStringLiteral("--") << command;
    return args;
}

// systemd's result for a finished scope ("oom-kill" when memory limits or systemd-oomd stopped it).
// Failed scopes stay loaded until reset, which also lets the name be reused.
inline QString takeResult(const QString &unit) {
    if (unit.isEmpty()) return {};
    QProcess show;
    show.start(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("show"), unit + QStringLiteral(".scope"), QStringLiteral("-p"), QStringLiteral("Result"), QStringLiteral("--value")});
    QString result;
    if (show.waitForFinished(2000)) result = QString::fromUtf8(show.readAllStandardOutput()).trimmed();
    else show.kill();
    QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("reset-failed"), unit + QStringLiteral(".scope")});
    return result;
}

// memory.events oom_kill counter of the cgroup `pid` belongs to, or -1.
inline long oomKills(int pid) {
    if (pid <= 0) return -1;
    QFile cgroup(QStringLiteral("/proc/%1/cgroup").arg(pid));
    if (!cgroup.open(QIODevice::ReadOnly)) return -1;
    const QString line = QString::fromUtf8(cgroup.readLine()).trimmed();   // "0::/user.slice/..."
    const int split = line.indexOf(QStringLiteral("::"));
    if (split < 0) return -1;
    QFile events(QStringLiteral("/sys/fs/cgroup") + line.mid(split + 2) + QStringLiteral("/memory.events"));
    if (!events.open(QIODevice::ReadOnly)) return -1;
    for (const QByteArray &row : events.readAll().split('\n'))
        if (row.startsWith("oom_kill ")) return row.mid(9).trimmed().toLong();
    return -1;
}

}  // namespace isolation

