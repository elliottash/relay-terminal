// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ScratchMonitor.h"

#include "Logging.h"
#include "Notifications.h"

#include <algorithm>

#include <QJsonArray>
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

// scratch.py's human(), so the ledger's numbers read like the check line's: B and KB whole,
// the rest one decimal.
QString humanBytes(qint64 bytes) {
    static const QStringList kUnits{QStringLiteral("B"), QStringLiteral("KB"), QStringLiteral("MB"),
                                    QStringLiteral("GB"), QStringLiteral("TB")};
    double size = double(qMax<qint64>(bytes, 0));
    for (int i = 0; i < kUnits.size(); ++i) {
        if (size < 1024.0 || i == kUnits.size() - 1)
            return QStringLiteral("%1 %2").arg(QString::number(size, 'f', i <= 1 ? 0 : 1), kUnits.at(i));
        size /= 1024.0;
    }
    return QStringLiteral("%1 B").arg(bytes);
}

qint64 bytesOf(const QList<LedgerRow> &rows) {
    qint64 total = 0;
    for (const LedgerRow &row : rows) total += row.bytes;
    return total;
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

Ledger parseLedger(const QByteArray &output) {
    Ledger ledger;
    const QList<QByteArray> lines = nonEmptyLines(output);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        if (!it->startsWith('{')) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(*it);
        if (!doc.isObject()) continue;
        const QJsonObject o = doc.object();
        if (!o.value(QStringLiteral("live")).isDouble() || !o.value(QStringLiteral("rows")).isArray())
            continue;   // not a ledger line (an older scratch.py's output, say): say nothing
        ledger.valid = true;
        ledger.live = o.value(QStringLiteral("live")).toInt();
        const auto buckets = [&o](const char *key) {
            QList<LedgerBucket> result;
            const QJsonObject map = o.value(QLatin1String(key)).toObject();
            for (auto entry = map.begin(); entry != map.end(); ++entry) {
                const QJsonObject totals = entry.value().toObject();
                LedgerBucket bucket;
                bucket.name = entry.key();
                bucket.total.rows = totals.value(QStringLiteral("rows")).toInt();
                bucket.total.bytes = qint64(totals.value(QStringLiteral("bytes")).toDouble());
                result.append(bucket);
            }
            return result;
        };
        ledger.byClass = buckets("by_class");
        ledger.bySession = buckets("by_session");
        const auto rows = [&o](const char *key) {
            QList<LedgerRow> result;
            const QJsonArray array = o.value(QLatin1String(key)).toArray();
            for (const QJsonValue &value : array) {
                const QJsonObject record = value.toObject();
                LedgerRow row;
                row.id = record.value(QStringLiteral("id")).toString();
                row.purpose = record.value(QStringLiteral("purpose")).toString();
                row.path = record.value(QStringLiteral("path")).toString();
                row.bytes = qint64(record.value(QStringLiteral("size")).toDouble());
                result.append(row);
            }
            return result;
        };
        ledger.unpromotedKeep = rows("unpromoted_keep");
        ledger.orphans = rows("orphans");
        break;
    }
    return ledger;
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

QString ledgerSummary(const Ledger &ledger) {
    if (!ledger.valid || ledger.live == 0) return QString();
    QStringList lines;
    // Totals first: what the ledger holds, by class and by session. The sessions are largest
    // first and capped, so a busy history cannot make the report unprintable.
    QStringList classes;
    for (const LedgerBucket &bucket : ledger.byClass)
        classes << QStringLiteral("%1 %2 (%3)")
                      .arg(bucket.name).arg(bucket.total.rows).arg(humanBytes(bucket.total.bytes));
    QList<LedgerBucket> sessions = ledger.bySession;
    std::sort(sessions.begin(), sessions.end(),
              [](const LedgerBucket &a, const LedgerBucket &b) { return a.total.bytes > b.total.bytes; });
    QStringList names;
    const int kMaxSessions = 3;
    for (int i = 0; i < sessions.size() && i < kMaxSessions; ++i)
        names << QStringLiteral("%1 %2 (%3)")
                     .arg(sessions.at(i).name).arg(sessions.at(i).total.rows).arg(humanBytes(sessions.at(i).total.bytes));
    if (sessions.size() > names.size())
        names << QStringLiteral("+%1 more").arg(sessions.size() - names.size());
    lines << QStringLiteral("Ledger: %1 live rows — %2; sessions %3.")
                 .arg(ledger.live)
                 .arg(classes.isEmpty() ? QStringLiteral("none") : classes.join(QStringLiteral(", ")))
                 .arg(names.isEmpty() ? QStringLiteral("none") : names.join(QStringLiteral(", ")));
    // Then the two lists that wait on the user; the monitor points at the verbs and decides
    // nothing itself — keep rows are promoted or dropped, orphans are never removed for them.
    if (!ledger.unpromotedKeep.isEmpty())
        lines << QStringLiteral("%1 unpromoted keep %2 (%3): promote into the project or drop (relay-scratch release).")
                     .arg(ledger.unpromotedKeep.size())
                     .arg(ledger.unpromotedKeep.size() == 1 ? QStringLiteral("row") : QStringLiteral("rows"))
                     .arg(humanBytes(bytesOf(ledger.unpromotedKeep)));
    if (!ledger.orphans.isEmpty())
        lines << QStringLiteral("%1 orphan %2 (%3) from before the ledger: kept until you decide (relay-scratch adopt, report).")
                     .arg(ledger.orphans.size())
                     .arg(ledger.orphans.size() == 1 ? QStringLiteral("tree") : QStringLiteral("trees"))
                     .arg(humanBytes(bytesOf(ledger.orphans)));
    return lines.join(QStringLiteral(" "));
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
    if (m_program.isEmpty() || m_check || m_ledger || m_cleanup) return;
    State state = loadState();
    if (!force && !checkDue(state, QDateTime::currentDateTime())) return;
    // Claim the interval before the walk starts, so a second Relay ticking meanwhile skips it.
    state.lastCheck = QDateTime::currentDateTime();
    saveState(state);
    runCheck(force ? After::ReportOnCleanup : After::Notify);
}

void Monitor::runCheck(After after) {
    m_after = after;
    m_checkOutput.clear();
    m_ledgerOutput.clear();
    // The verdict first, then what the ledger says is behind it: the pair runs side by side and
    // the report is composed from both at once, so its numbers and rows are from one moment.
    runVerb(m_check, {QStringLiteral("check"), QStringLiteral("--json")});
    runVerb(m_ledger, {QStringLiteral("ledger"), QStringLiteral("--json")});
}

void Monitor::runVerb(QPointer<QProcess> &slot, const QStringList &verbs) {
    auto *process = new QProcess(this);
    slot = process;
    process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, process](int, QProcess::ExitStatus) {
        // Exit 1 is "not ok", which the JSON says too; only the output matters.
        collect(process, process->readAllStandardOutput());
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;   // the rest end in finished()
        // The CLI could not run (missing python or scratch.py); the empty output says so plainly.
        relay::log::info(QStringLiteral("scratch_monitor verb did not start"));
        collect(process, QByteArray());
    });
    // A stuck check (a walk over a full disk) must not hold the GUI's check cadence hostage.
    QTimer::singleShot(kProcessTimeoutMs, process, [process] { process->kill(); });
    process->start(m_program, m_args + verbs);
}

void Monitor::collect(QProcess *process, const QByteArray &output) {
    if (m_check == process) {
        m_checkOutput = output;
        m_check.clear();
    } else if (m_ledger == process) {
        m_ledgerOutput = output;
        m_ledger.clear();
    } else {
        process->deleteLater();   // a process no tick owns any more: its output says nothing
        return;
    }
    process->deleteLater();
    // Report once the pair is in; a clean-up in flight keeps its turn — the check it asked for
    // is the one tick(true) starts when the clean-up is done.
    if (!m_check && !m_ledger && !m_cleanup) finishCheck(m_checkOutput, m_ledgerOutput, m_after);
}

void Monitor::finishCheck(const QByteArray &checkOutput, const QByteArray &ledgerOutput, After after) {
    const Verdict verdict = parseVerdict(checkOutput);
    if (!verdict.valid) {
        relay::log::info(QStringLiteral("scratch_monitor check gave no verdict"));
        return;
    }
    // The ledger is a second opinion, not a gate: empty, missing (an older relay-scratch) or
    // unparseable, and the verdict's report is exactly what it was.
    const QString ledgerPart = ledgerSummary(parseLedger(ledgerOutput));
    const QString ledgerSuffix = ledgerPart.isEmpty() ? QString() : QStringLiteral(" ") + ledgerPart;
    relay::log::info(QStringLiteral("scratch_monitor ok=%1 scratch=%2 reclaimable=%3 free=%4")
                         .arg(verdict.ok).arg(verdict.scratchBytes).arg(verdict.reclaimableBytes).arg(verdict.freeBytes)
                     + (ledgerPart.isEmpty() ? QString() : QStringLiteral(" | ") + ledgerPart));
    auto &center = relay::NotificationCenter::instance();
    if (after == After::ReportOnCleanup) {
        // The check that follows a clean-up says where things stand now, on the same entry.
        const QString body = (m_cleanupSummary.isEmpty() ? verdict.line
                                                         : m_cleanupSummary + QStringLiteral(". Now: ") + verdict.line)
                           + ledgerSuffix;
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
    const QString body = verdict.line + ledgerSuffix;
    m_noteId = offersCleanup(verdict)
        ? center.postWithAction(noticeTitle(verdict), body, NotificationCenter::kindWarning, QString(),
                                QStringLiteral("Clean up"), kCleanupAction)
        : center.post(noticeTitle(verdict), body, NotificationCenter::kindWarning);
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
