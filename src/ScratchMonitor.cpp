// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ScratchMonitor.h"

#include "Logging.h"
#include "Notifications.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QTimer>

namespace relay::scratch {

namespace {
const QString kLastCheck = QStringLiteral("scratch/monitor_last_check");
const QString kLastNotified = QStringLiteral("scratch/monitor_last_notified");
const QString kNotifiedCondition = QStringLiteral("scratch/monitor_notified_condition");
const QString kEnabled = QStringLiteral("scratch/monitor");
constexpr int kProcessTimeoutMs = 10 * 60 * 1000;   // a walk that takes longer is stuck

QList<QByteArray> nonEmptyLines(const QByteArray &output) {
    QList<QByteArray> lines;
    for (const QByteArray &line : output.split('\n'))
        if (!line.trimmed().isEmpty()) lines.append(line.trimmed());
    return lines;
}
}  // namespace

Verdict parseVerdict(const QByteArray &output) {
    Verdict verdict;
    const QList<QByteArray> lines = nonEmptyLines(output);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        if (!it->startsWith('{')) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(*it);
        if (!doc.isObject()) continue;
        const QJsonObject o = doc.object();
        if (!o.value(QStringLiteral("ok")).isBool() || !o.value(QStringLiteral("line")).isString()) continue;
        verdict.valid = true;
        verdict.ok = o.value(QStringLiteral("ok")).toBool();
        verdict.line = o.value(QStringLiteral("line")).toString();
        auto bytes = [&o](const char *key) { return qint64(o.value(QLatin1String(key)).toDouble()); };
        verdict.scratchBytes = bytes("scratch_bytes");
        verdict.reclaimableBytes = bytes("reclaimable_bytes");
        verdict.budgetBytes = bytes("budget_bytes");
        verdict.freeBytes = bytes("free_bytes");
        verdict.minFreeBytes = bytes("min_free_bytes");
        verdict.diskBytes = bytes("disk_bytes");
        break;
    }
    return verdict;
}

QString conditionOf(const Verdict &verdict) {
    if (!verdict.valid || verdict.ok) return QString();
    QStringList problems;
    if (verdict.scratchBytes > verdict.budgetBytes) problems << QStringLiteral("budget");
    if (verdict.freeBytes < verdict.minFreeBytes) problems << QStringLiteral("free");
    // Not ok but neither number says why (an older scratch.py): still a problem, of its own kind.
    if (problems.isEmpty()) problems << QStringLiteral("other");
    return problems.join(QLatin1Char('+'));
}

bool checkDue(const State &state, const QDateTime &now, qint64 intervalSecs) {
    if (!state.lastCheck.isValid()) return true;
    const qint64 age = state.lastCheck.secsTo(now);
    return age < 0 || age >= intervalSecs;
}

bool shouldNotify(const Verdict &verdict, const State &state, const QDateTime &now, qint64 renotifySecs) {
    if (!verdict.valid || verdict.ok) return false;
    if (!state.lastNotified.isValid()) return true;
    const qint64 age = state.lastNotified.secsTo(now);
    if (age < 0 || age >= renotifySecs) return true;
    // Worse since the last notice: the disk is now low where before only the budget was exceeded.
    const QStringList now_ = conditionOf(verdict).split(QLatin1Char('+'));
    const QStringList before = state.notifiedCondition.split(QLatin1Char('+'));
    return now_.contains(QStringLiteral("free")) && !before.contains(QStringLiteral("free"));
}

QString noticeTitle(const Verdict &verdict) {
    return verdict.freeBytes < verdict.minFreeBytes ? QStringLiteral("Disk space is low: agent scratch")
                                                    : QStringLiteral("Agent scratch is over its budget");
}

bool offersCleanup(const Verdict &verdict) { return verdict.valid && verdict.reclaimableBytes > 0; }

QString gcSummary(const QByteArray &output) {
    const QList<QByteArray> lines = nonEmptyLines(output);
    return lines.isEmpty() ? QString() : QString::fromUtf8(lines.last());
}

bool enabled(const QString &envValue, bool settingOn) {
    const QString v = envValue.trimmed().toLower();
    if (v == QLatin1String("0") || v == QLatin1String("false") || v == QLatin1String("off") || v == QLatin1String("no"))
        return false;
    return settingOn;
}

// ----- the running monitor ------------------------------------------------------------------------

Monitor &Monitor::instance() {
    static Monitor monitor;
    return monitor;
}

State Monitor::loadState() {
    QSettings settings;
    settings.sync();   // another Relay process may have written since this one read
    State state;
    state.lastCheck = settings.value(kLastCheck).toDateTime();
    state.lastNotified = settings.value(kLastNotified).toDateTime();
    state.notifiedCondition = settings.value(kNotifiedCondition).toString();
    return state;
}

void Monitor::saveState(const State &state) {
    QSettings settings;
    settings.setValue(kLastCheck, state.lastCheck);
    settings.setValue(kLastNotified, state.lastNotified);
    settings.setValue(kNotifiedCondition, state.notifiedCondition);
    settings.sync();
}

void Monitor::start(const QString &program, const QStringList &args) {
    if (m_timer) return;
    if (!enabled(qEnvironmentVariable("RELAY_SCRATCH_MONITOR"), QSettings().value(kEnabled, true).toBool())) {
        relay::log::info(QStringLiteral("scratch_monitor off"));
        return;
    }
    if (program.isEmpty()) return;
    m_program = program;
    m_args = args;
    relay::NotificationCenter::instance().addActionHandler(
        [this](const QString &, const QString &actionId) { return handleAction(actionId); });
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this] {
        m_timer->setInterval(kTickMs);
        tick();
    });
    m_timer->start(kFirstTickMs);
}

void Monitor::tick(bool force) {
    if (m_program.isEmpty() || m_check || m_cleanup) return;
    State state = loadState();
    if (!force && !checkDue(state, QDateTime::currentDateTime())) return;
    // Claim the interval before the walk starts, so a second Relay ticking meanwhile skips it.
    state.lastCheck = QDateTime::currentDateTime();
    saveState(state);
    runCheck(force ? After::ReportOnCleanup : After::Notify);
}

void Monitor::runCheck(After after) {
    auto *process = new QProcess(this);
    m_check = process;
    process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, process, after](int, QProcess::ExitStatus) {
        // Exit 1 is "not ok", which the JSON says too; only the output matters.
        const QByteArray out = process->readAllStandardOutput();
        m_check.clear();
        process->deleteLater();
        finishCheck(out, after);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;   // the rest end in finished()
        relay::log::info(QStringLiteral("scratch_monitor check did not start"));
        m_check.clear();
        process->deleteLater();
    });
    QTimer::singleShot(kProcessTimeoutMs, process, [process] { process->kill(); });
    process->start(m_program, m_args + QStringList{QStringLiteral("check"), QStringLiteral("--json")});
}

void Monitor::finishCheck(const QByteArray &output, After after) {
    const Verdict verdict = parseVerdict(output);
    if (!verdict.valid) {
        relay::log::info(QStringLiteral("scratch_monitor check gave no verdict"));
        return;
    }
    relay::log::info(QStringLiteral("scratch_monitor ok=%1 scratch=%2 reclaimable=%3 free=%4")
                         .arg(verdict.ok).arg(verdict.scratchBytes).arg(verdict.reclaimableBytes).arg(verdict.freeBytes));
    auto &center = relay::NotificationCenter::instance();
    if (after == After::ReportOnCleanup) {
        // The check that follows a clean-up says where things stand now, on the same entry.
        const QString body = m_cleanupSummary.isEmpty() ? verdict.line
                                                        : m_cleanupSummary + QStringLiteral(". Now: ") + verdict.line;
        const QString kind = verdict.ok ? NotificationCenter::kindSuccess : NotificationCenter::kindWarning;
        const QString title = QStringLiteral("Agent scratch cleaned up");
        const bool stillThere = [&] {
            for (const Notification &note : center.entries())
                if (note.id == m_noteId) return true;
            return false;
        }();
        if (stillThere) center.amend(m_noteId, title, body, QString(), QString(), kind);
        else m_noteId = center.post(title, body, kind);
        m_cleanupSummary.clear();
        return;
    }
    State state = loadState();
    if (!shouldNotify(verdict, state, QDateTime::currentDateTime())) return;
    state.lastNotified = QDateTime::currentDateTime();
    state.notifiedCondition = conditionOf(verdict);
    saveState(state);
    if (!m_noteId.isEmpty()) center.remove(m_noteId);   // yesterday's entry says the same thing
    m_noteId = offersCleanup(verdict)
        ? center.postWithAction(noticeTitle(verdict), verdict.line, NotificationCenter::kindWarning, QString(),
                                QStringLiteral("Clean up"), kCleanupAction)
        : center.post(noticeTitle(verdict), verdict.line, NotificationCenter::kindWarning);
}

bool Monitor::handleAction(const QString &actionId) {
    if (actionId != kCleanupAction) return false;
    if (!m_cleanup && !m_program.isEmpty()) runCleanup();
    return true;
}

void Monitor::runCleanup() {
    auto &center = relay::NotificationCenter::instance();
    if (!m_noteId.isEmpty())
        center.amend(m_noteId, QString(), QStringLiteral("Removing idle agent scratch that no process is using…"),
                     QString(), QString());
    auto *process = new QProcess(this);
    m_cleanup = process;
    process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, process](int code, QProcess::ExitStatus status) {
        m_cleanupSummary = gcSummary(process->readAllStandardOutput());
        if (status != QProcess::NormalExit || code != 0)
            m_cleanupSummary = QStringLiteral("relay-scratch gc stopped early (%1)")
                                   .arg(m_cleanupSummary.isEmpty() ? QString::number(code) : m_cleanupSummary);
        relay::log::info(QStringLiteral("scratch_monitor cleanup: %1").arg(m_cleanupSummary));
        m_cleanup.clear();   // before tick(), which refuses to start while a clean-up runs
        process->deleteLater();
        tick(true);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        if (!m_noteId.isEmpty())
            relay::NotificationCenter::instance().amend(m_noteId, QString(),
                QStringLiteral("Could not start relay-scratch."), QString(), QString(), NotificationCenter::kindError);
        m_cleanup.clear();
        process->deleteLater();
    });
    QTimer::singleShot(kProcessTimeoutMs, process, [process] { process->kill(); });
    process->start(m_program, m_args + QStringList{QStringLiteral("gc"), QStringLiteral("--apply")});
}

}  // namespace relay::scratch
