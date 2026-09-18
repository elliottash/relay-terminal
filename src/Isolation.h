// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Per-pane process isolation: each pane's shell and agent worker run in their own transient
// systemd user scope, so a runaway command is stopped inside its pane instead of taking Relay down
// with it. Free functions over `systemd-run` and `systemctl`, with the probes cached; Qt only.

#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QRegularExpression>

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
inline bool available() {
    static int state = -1;
    if (state < 0) {
        state = 0;
        const QString tool = QStandardPaths::findExecutable(QStringLiteral("systemd-run"));
        if (!tool.isEmpty()) {
            QProcess probe;
            probe.start(tool, {QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("--quiet"), QStringLiteral("--"), QStringLiteral("true")});
            if (probe.waitForFinished(3000) && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0) state = 1;
            else probe.kill();
        }
    }
    return state == 1;
}

// A systemd size such as "8G", "512M" or "infinity"; anything else falls back to the default.
inline QString memory(const char *key, const char *fallback) {
    const QString value = QSettings().value(QString::fromLatin1(key), QString::fromLatin1(fallback)).toString().trimmed();
    static const QRegularExpression valid(QStringLiteral("^(\\d+[KMGT]?|infinity)$"));
    return valid.match(value).hasMatch() ? value : QString::fromLatin1(fallback);
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

