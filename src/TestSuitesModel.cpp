// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TestSuitesModel.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace relay::tests {

namespace {

const QString kDash = QStringLiteral("—");

QString str(const QJsonObject &o, const char *key) { return o.value(QLatin1String(key)).toString(); }

QStringList strList(const QJsonObject &o, const char *key) {
    QStringList out;
    for (const auto &value : o.value(QLatin1String(key)).toArray())
        if (value.isString() && !value.toString().isEmpty()) out << value.toString();
    return out;
}

// A number the worker may simply not have. `isDouble()` is the whole test: the contract says a
// missing optional key means "unknown", and JSON null is the same statement.
bool number(const QJsonObject &o, const char *key, double *out) {
    const QJsonValue value = o.value(QLatin1String(key));
    if (!value.isDouble()) return false;
    *out = value.toDouble();
    return true;
}

Execution readExecution(const QJsonObject &o) {
    Execution e;
    e.ts = str(o, "ts");
    e.runId = str(o, "run_id");
    e.id = str(o, "id");
    e.runner = str(o, "runner");
    e.result = str(o, "result");
    e.commit = str(o, "commit");
    e.host = str(o, "host");
    e.hasDuration = number(o, "duration", &e.duration);
    return e;
}

}  // namespace

// ----- formatting ---------------------------------------------------------------------------

QString formatDuration(double seconds, bool known) {
    if (!known || !std::isfinite(seconds) || seconds < 0) return kDash;
    if (seconds < 1.0) return QStringLiteral("%1 ms").arg(qRound(seconds * 1000.0));
    if (seconds < 10.0) return QStringLiteral("%1 s").arg(seconds, 0, 'f', 2);
    if (seconds < 600.0) return QStringLiteral("%1 s").arg(seconds, 0, 'f', 1);
    return QStringLiteral("%1 m").arg(seconds / 60.0, 0, 'f', 1);
}

QString relativeTime(const QString &iso, const QDateTime &now) {
    if (iso.isEmpty()) return kDash;
    QDateTime at = QDateTime::fromString(iso, Qt::ISODate);
    if (!at.isValid()) return iso;   // not ours to reformat: show what the worker said
    if (at.timeSpec() == Qt::LocalTime && iso.endsWith(QLatin1Char('Z'))) at.setTimeSpec(Qt::UTC);
    const qint64 secs = at.secsTo(now);
    if (secs < 60) return QStringLiteral("just now");
    if (secs < 3600) return QStringLiteral("%1 m ago").arg(secs / 60);
    if (secs < 86400) return QStringLiteral("%1 h ago").arg(secs / 3600);
    if (secs < 172800) return QStringLiteral("yesterday");
    if (secs < 86400 * 14) return QStringLiteral("%1 d ago").arg(secs / 86400);
    return at.toUTC().toString(QStringLiteral("yyyy-MM-dd"));
}

QString Execution::durationText() const { return formatDuration(duration, hasDuration); }

// ----- one row ------------------------------------------------------------------------------

bool TestRow::failing() const {
    return lastResult == QLatin1String("fail") || lastResult == QLatin1String("error")
        || lastResult == QLatin1String("timeout");
}

QString TestRow::reliabilityText() const {
    if (!hasReliability) return kDash;
    // Whole percents until the last one: 99.4% and 100% are a different message from "99%".
    const double rounded = std::round(reliability * 10.0) / 10.0;
    if (rounded >= 100.0) return QStringLiteral("100%");
    return QStringLiteral("%1%").arg(rounded, 0, 'f', rounded >= 99.0 ? 1 : 0);
}

QString TestRow::p50Text() const { return formatDuration(durationP50, hasP50); }
QString TestRow::p95Text() const { return formatDuration(durationP95, hasP95); }

QString TestRow::runsText() const {
    if (runs <= 0) return kDash;
    return QString::number(runs);
}

QString TestRow::lastRunText(const QDateTime &now) const { return relativeTime(lastRun, now); }

QString TestRow::cardsText() const {
    if (cards.isEmpty()) return kDash;
    QStringList out;
    for (const QString &card : cards) out << QStringLiteral("#") + card;
    return out.join(QStringLiteral(" "));
}

QStringList TestRow::badges() const {
    QStringList out;
    if (flaky) out << QStringLiteral("flaky");
    if (slow) out << QStringLiteral("slow");
    for (const QString &s : stale) {
        if (s == QLatin1String("never-run") && !out.contains(QStringLiteral("never run")))
            out << QStringLiteral("never run");
        else if (s == QLatin1String("gone")) out << QStringLiteral("gone");
        else if (s == QLatin1String("skipped-forever")) out << QStringLiteral("skipped");
        else if (s == QLatin1String("orphaned")) out << QStringLiteral("orphaned");
        else if (s == QLatin1String("edited")) out << QStringLiteral("edited");
    }
    if (neverRun() && !out.contains(QStringLiteral("never run"))) out << QStringLiteral("never run");
    return out;
}

// ----- the model ----------------------------------------------------------------------------

TestSuitesModel::TestSuitesModel() {
    clock = [] { return QDateTime::currentDateTimeUtc(); };
}

void TestSuitesModel::changed() {
    if (onChanged) onChanged();
}

TestRow *TestSuitesModel::find(const QString &id) {
    for (TestRow &row : m_all)
        if (row.id == id) return &row;
    return nullptr;
}

const TestRow *TestSuitesModel::row(const QString &id) const {
    for (const TestRow &row : m_all)
        if (row.id == id) return &row;
    return nullptr;
}

int TestSuitesModel::indexOf(const QString &id) const {
    for (int i = 0; i < m_view.size(); ++i)
        if (m_view.at(i).id == id) return i;
    return -1;
}

QVector<Execution> TestSuitesModel::history(const QString &id) const {
    const TestRow *found = row(id);
    return found ? found->history : QVector<Execution>{};
}

bool TestSuitesModel::handle(const QJsonObject &event) {
    const QString type = event.value(QStringLiteral("event"))
                             .toString(event.value(QStringLiteral("type")).toString());

    if (type == QStringLiteral("tests_list")) {
        m_attached = true;
        m_project = str(event, "project");
        QList<TestRow> rows;
        for (const auto &value : event.value(QStringLiteral("tests")).toArray()) {
            const QJsonObject item = value.toObject();
            TestRow row;
            row.id = str(item, "id");
            if (row.id.isEmpty()) continue;
            row.name = str(item, "name");
            if (row.name.isEmpty()) row.name = row.id;
            row.runner = str(item, "runner");
            // The worker always sends one (it falls back to the name), but an older one may not:
            // the name is a runnable line often enough to be a better answer than nothing.
            row.invocation = str(item, "invocation");
            if (row.invocation.isEmpty()) row.invocation = row.name;
            row.file = str(item, "file");
            double n = 0;
            row.hasLine = number(item, "line", &n);
            row.line = int(n);
            row.labels = strList(item, "labels");
            row.firstSeen = str(item, "first_seen");
            row.lastRun = str(item, "last_run");
            row.lastResult = str(item, "last_result");
            row.runs = item.value(QStringLiteral("runs")).toInt();
            row.passed = item.value(QStringLiteral("pass")).toInt();
            row.failed = item.value(QStringLiteral("fail")).toInt();
            row.skipped = item.value(QStringLiteral("skip")).toInt();
            row.hasP50 = number(item, "duration_p50", &row.durationP50);
            row.hasP95 = number(item, "duration_p95", &row.durationP95);
            row.hasLast = number(item, "duration_last", &row.durationLast);
            row.hasReliability = number(item, "reliability", &row.reliability);
            number(item, "flake_score", &row.flakeScore);
            row.slow = item.value(QStringLiteral("slow")).toBool();
            row.flaky = item.value(QStringLiteral("flaky")).toBool();
            row.stale = strList(item, "stale");
            row.sourceHash = str(item, "source_hash");
            for (const auto &exec : item.value(QStringLiteral("history")).toArray())
                row.history << readExecution(exec.toObject());
            const QJsonValue failure = item.value(QStringLiteral("last_failure"));
            if (failure.isObject()) {
                const QJsonObject f = failure.toObject();
                row.hasLastFailure = true;
                row.failureAt = str(f, "at");
                row.failureCommit = str(f, "commit");
                row.failureMessage = str(f, "message");
                row.failureExcerpt = str(f, "excerpt");
            }
            row.cards = strList(item, "cards");
            // What this side knows and the list does not: the run in flight, and a history this
            // pane fetched in full through tests_history.
            if (const TestRow *before = this->row(row.id)) {
                row.runState = before->runState;
                if (row.history.isEmpty()) row.history = before->history;
            }
            rows << row;
        }
        m_all = rows;
        const QJsonValue summary = event.value(QStringLiteral("summary"));
        recountSummary();
        if (summary.isObject()) {
            const QJsonObject s = summary.toObject();
            const auto take = [&s](const char *key, int *out) {
                const QJsonValue v = s.value(QLatin1String(key));
                if (v.isDouble()) *out = v.toInt();
            };
            take("total", &m_summary.total);
            take("passed", &m_summary.passed);
            take("failed", &m_summary.failed);
            take("skipped", &m_summary.skipped);
            take("never_run", &m_summary.neverRun);
            take("slow", &m_summary.slow);
            take("flaky", &m_summary.flaky);
            m_summary.hasDuration = number(s, "duration", &m_summary.duration);
            m_summary.line = str(s, "line");
        }
        reindex();
        changed();
        return true;
    }

    if (type == QStringLiteral("tests_run")) {
        const QString state = str(event, "state");
        m_runId = str(event, "run_id");
        m_runMessage = str(event, "message");
        if (event.value(QStringLiteral("done")).isDouble())
            m_runDone = event.value(QStringLiteral("done")).toInt();
        if (event.value(QStringLiteral("total")).isDouble())
            m_runTotal = event.value(QStringLiteral("total")).toInt();
        const QString id = str(event, "id");
        const QString result = str(event, "result");
        if (state == QStringLiteral("started") || state == QStringLiteral("progress")) {
            m_running = true;
            if (!id.isEmpty()) {
                if (TestRow *row = find(id)) {
                    if (result.isEmpty()) {
                        // The worker moved on to this test: the one before it is done.
                        row->runState = RunState::Running;
                        m_runningTest = row->name;
                    } else {
                        // A verdict. The counts stay the worker's business — it sends a fresh
                        // tests_list when the run ends — but the row shows its new colour at once.
                        row->runState = RunState::Idle;
                        row->lastResult = result;
                        row->lastRun = str(event, "ts");
                        if (row->lastRun.isEmpty()) row->lastRun = clock().toString(Qt::ISODate);
                        row->hasLast = number(event, "duration", &row->durationLast);
                        Execution exec;
                        exec.ts = row->lastRun;
                        exec.runId = m_runId;
                        exec.id = row->id;
                        exec.runner = row->runner;
                        exec.result = result;
                        exec.duration = row->durationLast;
                        exec.hasDuration = row->hasLast;
                        row->history.prepend(exec);
                        if (m_runningTest == row->name) m_runningTest.clear();
                    }
                }
            }
        } else {
            // finished / stopped / error: nothing is in flight any more, whatever it was queued for.
            m_running = false;
            m_runningTest.clear();
            for (TestRow &row : m_all) row.runState = RunState::Idle;
        }
        // The worker's ready-made line described the tree before this run; the composed one
        // follows the verdicts as they arrive, and the tests_list that ends a run brings a new one.
        m_summary.line.clear();
        recountSummary();
        reindex();
        changed();
        return true;
    }

    if (type == QStringLiteral("tests_history")) {
        const QString id = str(event, "id");
        if (TestRow *row = find(id)) {
            QVector<Execution> executions;
            for (const auto &value : event.value(QStringLiteral("executions")).toArray())
                executions << readExecution(value.toObject());
            row->history = executions;
            reindex();
            changed();
        }
        return true;
    }

    if (type == QStringLiteral("tests_check")) {
        m_checkCard = str(event, "card");
        m_checkFindings.clear();
        for (const auto &value : event.value(QStringLiteral("findings")).toArray()) {
            const QJsonObject f = value.toObject();
            m_checkFindings.append(CheckFinding{str(f, "test"), str(f, "verdict"),
                                                str(f, "message"), str(f, "severity")});
        }
        m_checkActions = strList(event, "actions");
        changed();
        return true;
    }

    if (type == QStringLiteral("reset") || type == QStringLiteral("ready")) clear();
    return false;
}

void TestSuitesModel::markRequested(const QStringList &ids) {
    bool moved = false;
    for (TestRow &row : m_all) {
        const bool wanted = ids.isEmpty() || ids.contains(row.id);
        if (!wanted || row.runState != RunState::Idle) continue;
        row.runState = RunState::Queued;
        moved = true;
    }
    if (!moved) return;
    m_running = true;
    m_runDone = 0;
    m_runTotal = ids.isEmpty() ? int(m_all.size()) : int(ids.size());
    m_runMessage.clear();
    reindex();
    changed();
}

QString TestSuitesModel::runLine() const {
    if (m_running) {
        QString line = m_runTotal > 0 ? QStringLiteral("running %1/%2").arg(m_runDone).arg(m_runTotal)
                                      : QStringLiteral("running");
        if (!m_runningTest.isEmpty()) line += QStringLiteral(" · ") + m_runningTest;
        return line;
    }
    return m_runMessage;
}

void TestSuitesModel::setFilter(const QString &text) {
    if (m_filter == text) return;
    m_filter = text;
    reindex();
    changed();
}

void TestSuitesModel::setSort(Sort sort, bool ascending) {
    if (m_sort == sort && m_ascending == ascending) return;
    m_sort = sort;
    m_ascending = ascending;
    reindex();
    changed();
}

void TestSuitesModel::clear() {
    const bool had = m_attached || !m_all.isEmpty();
    m_all.clear();
    m_view.clear();
    m_summary = Summary{};
    m_project.clear();
    m_runId.clear();
    m_runMessage.clear();
    m_runningTest.clear();
    m_checkCard.clear();
    m_checkFindings.clear();
    m_checkActions.clear();
    m_attached = false;
    m_running = false;
    m_runDone = m_runTotal = 0;
    if (had) changed();
}

void TestSuitesModel::recountSummary() {
    Summary s;
    s.total = int(m_all.size());
    for (const TestRow &row : m_all) {
        if (row.neverRun()) { ++s.neverRun; continue; }
        if (row.failing()) ++s.failed;
        else if (row.lastResult == QLatin1String("skip")) ++s.skipped;
        else if (row.lastResult == QLatin1String("pass")) ++s.passed;
        if (row.slow) ++s.slow;
        if (row.flaky) ++s.flaky;
        if (row.hasLast) { s.duration += row.durationLast; s.hasDuration = true; }
    }
    s.line = m_summary.line;   // the worker's line outlives a local recount
    m_summary = s;
}

QString TestSuitesModel::summaryLine() const {
    if (!m_summary.line.isEmpty()) return m_summary.line;
    if (!m_attached) return QStringLiteral("No worker attached.");
    if (m_summary.total == 0) return QStringLiteral("No tests discovered.");
    const Summary &s = m_summary;
    QStringList parts;
    parts << QStringLiteral("%1 test%2").arg(s.total).arg(s.total == 1 ? QString() : QStringLiteral("s"));
    QString passed = QStringLiteral("%1 passed").arg(s.passed);
    QStringList marks;
    if (s.slow > 0) marks << QStringLiteral("%1 slow").arg(s.slow);
    if (s.flaky > 0) marks << QStringLiteral("%1 flaky").arg(s.flaky);
    if (!marks.isEmpty()) passed += QStringLiteral(" (") + marks.join(QStringLiteral(", ")) + QStringLiteral(")");
    parts << passed;
    if (s.failed > 0) parts << QStringLiteral("%1 failed").arg(s.failed);
    if (s.skipped > 0) parts << QStringLiteral("%1 skipped").arg(s.skipped);
    if (s.neverRun > 0) parts << QStringLiteral("%1 never run").arg(s.neverRun);
    if (s.hasDuration) parts << formatDuration(s.duration);
    return parts.join(QStringLiteral(" · "));
}

// ----- filter and sort ------------------------------------------------------------------------

void TestSuitesModel::reindex() {
    QStringList tokens;
    QStringList words;
    for (const QString &piece : m_filter.split(QRegularExpression(QStringLiteral("\\s+")),
                                               Qt::SkipEmptyParts)) {
        if (piece.startsWith(QStringLiteral("is:"), Qt::CaseInsensitive)) tokens << piece.mid(3).toLower();
        else words << piece.toLower();
    }

    m_view.clear();
    for (const TestRow &row : m_all) {
        bool keep = true;
        for (const QString &token : tokens) {
            if (token == QLatin1String("slow")) keep = keep && row.slow;
            else if (token == QLatin1String("flaky")) keep = keep && row.flaky;
            else if (token == QLatin1String("failed")) keep = keep && row.failing();
            else if (token == QLatin1String("never")) keep = keep && row.neverRun();
            else if (token == QLatin1String("stale")) keep = keep && !row.stale.isEmpty();
            // An unknown token filters nothing away: the vocabulary may grow on the worker's side.
        }
        if (!keep) continue;
        for (const QString &word : words) {
            bool hit = row.name.toLower().contains(word) || row.id.toLower().contains(word)
                    || row.file.toLower().contains(word);
            if (!hit)
                for (const QString &label : row.labels)
                    if (label.toLower().contains(word)) { hit = true; break; }
            if (!hit) { keep = false; break; }
        }
        if (keep) m_view << row;
    }

    const Sort sort = m_sort;
    const bool ascending = m_ascending;
    // A row whose key the worker never measured sorts last in both directions: "unknown" is not
    // "zero" here either, and a table that opens on a column of dashes has told the reader nothing.
    const auto key = [sort](const TestRow &row, bool *known) -> double {
        switch (sort) {
        case Sort::FlakeScore: *known = true; return row.flakeScore;
        case Sort::P95: *known = row.hasP95; return row.durationP95;
        case Sort::Name: case Sort::LastFailure: case Sort::LastRun: break;
        }
        *known = true;
        return 0;
    };
    const auto text = [sort](const TestRow &row) -> QString {
        switch (sort) {
        case Sort::Name: return row.name.toLower();
        case Sort::LastFailure: return row.failureAt;
        case Sort::LastRun: return row.lastRun;
        default: break;
        }
        return QString();
    };
    const bool textual = sort == Sort::Name || sort == Sort::LastFailure || sort == Sort::LastRun;
    std::stable_sort(m_view.begin(), m_view.end(), [&](const TestRow &a, const TestRow &b) {
        int cmp = 0;
        if (textual) {
            const QString ka = text(a), kb = text(b);
            if (ka.isEmpty() != kb.isEmpty()) return !ka.isEmpty();   // unknown last, both ways
            cmp = ka < kb ? -1 : (ka > kb ? 1 : 0);
        } else {
            bool ka = true, kb = true;
            const double va = key(a, &ka), vb = key(b, &kb);
            if (ka != kb) return ka;
            cmp = va < vb ? -1 : (va > vb ? 1 : 0);
        }
        if (cmp == 0) {
            // One stable order for equal keys, so a redraw never shuffles the table.
            const QString na = a.name.toLower(), nb = b.name.toLower();
            if (na != nb) return na < nb;
            return a.id < b.id;
        }
        return ascending ? cmp < 0 : cmp > 0;
    });
}

}  // namespace relay::tests
