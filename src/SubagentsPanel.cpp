// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SubagentsPanel.h"
#include "Theme.h"
#include <QElapsedTimer>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSet>
#include <vector>
#include <algorithm>

namespace relay {

namespace {
QString str(const QJsonObject &o, const char *key) { return o.value(QLatin1String(key)).toString(); }
// "T3 · write the docs" for a subagent working on todo T3 (card #QHR1).
QString withTodo(const QString &todoId, const QString &description) {
    return todoId.isEmpty() ? description : todoId + QStringLiteral(" · ") + description;
}

// Read at paint time so a theme switch recolours the list (issue 0JA7).
inline const QColor &kDone() { return theme::Success; }
inline const QColor &kFailed() { return theme::SyntaxUnknown; }

// Todo text comes straight from the model and is untrusted: drop control characters before it is
// elided into a row (the same filter the task list uses, RequestsPanel.cpp).
QString clean(const QString &text) {
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        const ushort u = c.unicode();
        if (u == '\n' || u == '\t' || (u >= 0x20 && u != 0x7f && !(u >= 0x80 && u < 0xa0))) out += c;
    }
    return out;
}

// The task list's own status colours, so the strip and the panel agree (RequestsPanel.cpp).
QColor taskColor(const QString &status) {
    if (status == QStringLiteral("done") || status == QStringLiteral("completed")) return theme::Success;
    if (status == QStringLiteral("in_progress")) return theme::Accent;
    if (status == QStringLiteral("blocked")) return theme::Error;
    if (status == QStringLiteral("deferred")) return theme::Warning;
    if (status.startsWith(QStringLiteral("cancelled"))) return theme::TextMuted;
    return theme::Text;
}
}  // namespace

SubagentModel::SubagentModel() {
    clock = [] {
        static QElapsedTimer timer;
        if (!timer.isValid()) timer.start();
        return timer.elapsed();
    };
}

SubagentRow *SubagentModel::find(const QString &id) {
    for (auto &row : m_rows) if (row.id == id) return &row;
    return nullptr;
}

const SubagentRow *SubagentModel::row(const QString &id) const {
    for (const auto &row : m_rows) if (row.id == id) return &row;
    return nullptr;
}

int SubagentModel::liveCount() const {
    return int(std::count_if(m_rows.cbegin(), m_rows.cend(), [](const SubagentRow &r) { return r.live(); }));
}

bool SubagentModel::hasLiveForeground() const {
    return std::any_of(m_rows.cbegin(), m_rows.cend(), [](const SubagentRow &r) { return r.live() && !r.background; });
}

qint64 SubagentModel::elapsedNow(const SubagentRow &row) const {
    if (!row.live() || row.reportedAt <= 0) return row.elapsedMs;
    return row.elapsedMs + std::max<qint64>(0, clock() - row.reportedAt);
}

qint64 SubagentModel::agentTokens() const {
    qint64 total = 0;
    for (const auto &row : m_rows) total += row.tokens;
    return total;
}

bool SubagentModel::agentTokensEstimated() const {
    return std::any_of(m_rows.cbegin(), m_rows.cend(), [](const SubagentRow &r) { return r.tokensEstimated; });
}

void SubagentModel::dismiss(const QString &id) {
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].id == id && !m_rows[i].live()) { m_rows.removeAt(i); changed(); return; }
}

void SubagentModel::clearFinished() {
    const int before = m_rows.size();
    m_rows.erase(std::remove_if(m_rows.begin(), m_rows.end(), [](const SubagentRow &r) { return !r.live(); }), m_rows.end());
    if (m_rows.size() != before) changed();
    if (onFinishedCleared) onFinishedCleared();
}

void SubagentModel::clear() {
    const bool had = !m_rows.isEmpty();
    m_rows.clear(); m_mainBusy = false; m_mainTokens = -1;
    if (had) changed();
}

QString SubagentModel::tokenSplit() const {
    const QString main = m_mainTokens >= 0 ? QStringLiteral("main ctx %1").arg(formatTokens(m_mainTokens, false)) : QStringLiteral("main");
    return QStringLiteral("%1 · agents %2 tok").arg(main, formatTokens(agentTokens(), agentTokensEstimated()));
}

QString SubagentModel::formatElapsed(qint64 ms) {
    const qint64 seconds = std::max<qint64>(0, ms) / 1000;
    if (seconds >= 3600) return QStringLiteral("%1:%2:%3").arg(seconds / 3600).arg((seconds / 60) % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString SubagentModel::formatTokens(qint64 tokens, bool estimated) {
    QString text;
    if (tokens < 1000) text = QString::number(std::max<qint64>(0, tokens));
    else if (tokens < 100000) text = QString::number(tokens / 1000.0, 'f', 1) + QLatin1Char('k');
    else text = QString::number(tokens / 1000) + QLatin1Char('k');
    text.replace(QStringLiteral(".0k"), QStringLiteral("k"));
    return estimated ? QLatin1Char('~') + text : text;
}

QString SubagentModel::statusIcon(const QString &status) {
    if (status == QStringLiteral("running")) return QStringLiteral("●");
    if (status == QStringLiteral("done")) return QStringLiteral("✓");
    if (status == QStringLiteral("failed")) return QStringLiteral("✗");
    if (status == QStringLiteral("stopped")) return QStringLiteral("■");
    return QStringLiteral("○");   // waiting
}

QString SubagentModel::handoffText(const QString &handoff, int wakeups, int maxAutoTurns) {
    if (handoff == QStringLiteral("wake")) return QStringLiteral("background agent finished → main agent continues");
    if (handoff == QStringLiteral("next_model_call")) return QStringLiteral("result goes to the main agent's next step");
    if (handoff == QStringLiteral("pending"))
        return QStringLiteral("result pending: automatic-turn limit reached (%1/%2); it goes with your next prompt").arg(wakeups).arg(maxAutoTurns);
    if (handoff == QStringLiteral("returned")) return QStringLiteral("result returned to the main agent");
    if (handoff == QStringLiteral("discarded")) return QStringLiteral("result discarded (new conversation)");
    return {};
}

bool SubagentModel::handle(const QJsonObject &event) {
    const QString type = str(event, "event");
    const QString id = str(event, "id");
    const qint64 now = clock();
    auto metrics = [&](SubagentRow &row) {
        if (event.contains(QStringLiteral("tools"))) row.tools = event.value(QStringLiteral("tools")).toInt();
        if (event.contains(QStringLiteral("tokens"))) row.tokens = qint64(event.value(QStringLiteral("tokens")).toDouble());
        if (event.contains(QStringLiteral("tokens_estimated"))) row.tokensEstimated = event.value(QStringLiteral("tokens_estimated")).toBool();
        if (event.contains(QStringLiteral("elapsed_ms"))) { row.elapsedMs = qint64(event.value(QStringLiteral("elapsed_ms")).toDouble()); row.reportedAt = now; }
    };
    auto ensure = [&](const QString &rowId) -> SubagentRow & {
        if (SubagentRow *existing = find(rowId)) return *existing;
        SubagentRow row; row.id = rowId; row.reportedAt = now;
        m_rows.append(row);
        return m_rows.last();
    };
    if (type == QStringLiteral("subagent_started")) {
        if (id.isEmpty()) return true;
        SubagentRow &row = ensure(id);
        row.type = str(event, "type"); row.todoId = str(event, "todo_id");
        row.description = withTodo(row.todoId, str(event, "description"));
        row.background = event.value(QStringLiteral("background")).toBool();
        row.model = str(event, "model"); row.effort = str(event, "effort");
        row.resumed = event.value(QStringLiteral("resumed")).toBool();
        row.warnings.clear();
        for (const auto &w : event.value(QStringLiteral("warnings")).toArray()) row.warnings << w.toString();
        row.status = QStringLiteral("waiting"); row.summary.clear(); row.handoff.clear();
        row.lastActivity = row.resumed ? QStringLiteral("resuming") : QStringLiteral("starting");
        if (!row.resumed) { row.elapsedMs = 0; row.reportedAt = now; }
        if (onInline) {
            QString line = QStringLiteral("✦ %1 %2 %3 · %4").arg(row.type, row.id,
                row.resumed ? QStringLiteral("resumed") : row.background ? QStringLiteral("started in the background") : QStringLiteral("started"),
                row.description);
            if (!row.warnings.isEmpty()) line += QStringLiteral(" (") + row.warnings.join(QStringLiteral("; ")) + QLatin1Char(')');
            onInline(line, row.id);
        }
        changed();
        return true;
    }
    if (type == QStringLiteral("subagent_progress")) {
        if (id.isEmpty()) return true;
        SubagentRow &row = ensure(id);
        const QString status = str(event, "status");
        if (!status.isEmpty()) row.status = status;
        if (event.contains(QStringLiteral("last_activity"))) row.lastActivity = str(event, "last_activity");
        metrics(row);
        changed();
        if (onTranscript) onTranscript(id, event);
        return true;
    }
    if (type == QStringLiteral("subagent_finished")) {
        if (id.isEmpty()) return true;
        SubagentRow &row = ensure(id);
        if (row.type.isEmpty()) row.type = str(event, "type");
        row.status = str(event, "outcome").isEmpty() ? QStringLiteral("done") : str(event, "outcome");
        row.summary = str(event, "summary"); row.handoff = str(event, "handoff");
        row.lastActivity = row.status;
        metrics(row);
        const SubagentRow copy = row;
        if (onInline) {
            QString line = QStringLiteral("✦ %1 %2 %3 · %4 · %5 tool%6 · %7 tok").arg(copy.type, copy.id, copy.status,
                formatElapsed(copy.elapsedMs)).arg(copy.tools).arg(copy.tools == 1 ? QString() : QStringLiteral("s"))
                .arg(formatTokens(copy.tokens, copy.tokensEstimated));
            const QString handoff = handoffText(copy.handoff, event.value(QStringLiteral("wakeups")).toInt(), event.value(QStringLiteral("max_auto_turns")).toInt());
            if (!handoff.isEmpty()) line += QStringLiteral(" · ") + handoff;
            onInline(line, copy.id);
        }
        changed();
        if (onTranscript) onTranscript(id, event);
        if (onFinished) onFinished(copy);
        return true;
    }
    if (type == QStringLiteral("subagent_handoff")) {
        const QString handoff = str(event, "handoff");
        if (SubagentRow *row = find(id)) row->handoff = handoff;
        if (onInline && (handoff == QStringLiteral("wake") || handoff == QStringLiteral("pending"))) {
            onInline(handoff == QStringLiteral("wake")
                ? QStringLiteral("✦ %1 result → main agent continues").arg(id)
                : QStringLiteral("✦ %1 %2").arg(id, handoffText(handoff, event.value(QStringLiteral("wakeups")).toInt(), event.value(QStringLiteral("max_auto_turns")).toInt())),
                id);
        }
        changed();
        return true;
    }
    if (type == QStringLiteral("subagent_model")) {
        SubagentRow *row = find(id);
        if (!row) return true;
        row->model = str(event, "model");
        QStringList warnings;
        for (const auto &w : event.value(QStringLiteral("warnings")).toArray()) warnings << w.toString();
        if (onStatus) onStatus(QStringLiteral("%1 → %2%3%4").arg(id, row->model,
            str(event, "applies") == QStringLiteral("next_step") ? QStringLiteral(" from its next step") : QString(),
            warnings.isEmpty() ? QString() : QStringLiteral(" (") + warnings.join(QStringLiteral("; ")) + QLatin1Char(')')));
        changed();
        return true;
    }
    if (type == QStringLiteral("subagent_transcript") || type == QStringLiteral("subagent_event")) {
        if (onTranscript) onTranscript(id, event);
        return true;
    }
    if (type == QStringLiteral("agents_status")) {
        for (const auto &value : event.value(QStringLiteral("items")).toArray()) {
            const QJsonObject item = value.toObject();
            SubagentRow &row = ensure(str(item, "id"));
            row.type = str(item, "type"); row.todoId = str(item, "todo_id");
            row.description = withTodo(row.todoId, str(item, "description"));
            row.background = item.value(QStringLiteral("background")).toBool(); row.model = str(item, "model");
            row.status = str(item, "status"); row.lastActivity = str(item, "last_activity");
            row.tools = item.value(QStringLiteral("tools")).toInt(); row.tokens = qint64(item.value(QStringLiteral("tokens")).toDouble());
            row.elapsedMs = qint64(item.value(QStringLiteral("elapsed_ms")).toDouble()); row.reportedAt = now;
        }
        changed();
        return true;
    }
    if (type == QStringLiteral("agents")) {
        m_definitions = event.value(QStringLiteral("items")).toArray();
        m_definitionWarnings.clear();
        for (const auto &value : event.value(QStringLiteral("skipped")).toArray())
            m_definitionWarnings << QStringLiteral("%1: %2").arg(str(value.toObject(), "path"), str(value.toObject(), "reason"));
        for (const auto &value : event.value(QStringLiteral("duplicates")).toArray())
            m_definitionWarnings << QStringLiteral("%1: duplicate name %2").arg(str(value.toObject(), "source"), str(value.toObject(), "name"));
        return true;
    }
    if (type == QStringLiteral("agent_stopped")) {
        const QJsonArray ids = event.value(QStringLiteral("ids")).toArray();
        if (onStatus) onStatus(ids.isEmpty() ? QStringLiteral("No running agents to stop.")
                                             : QStringLiteral("Stopping %1 agent(s)…").arg(ids.size()));
        return true;
    }
    if (type == QStringLiteral("agent_message_delivered")) {
        if (onStatus) onStatus(str(event, "delivered") == QStringLiteral("resumed")
                                   ? QStringLiteral("Message sent · %1 resumed in the background").arg(id)
                                   : QStringLiteral("Message sent · %1 reads it before its next step").arg(id));
        return true;
    }
    if (type == QStringLiteral("agent_options")) {
        if (onStatus) onStatus(QStringLiteral("Automatic agent turns: %1 used of %2").arg(event.value(QStringLiteral("wakeups")).toInt())
                                   .arg(event.value(QStringLiteral("max_auto_turns")).toInt() == 0 ? QStringLiteral("unlimited")
                                        : QString::number(event.value(QStringLiteral("max_auto_turns")).toInt()))
                                   // Turn limits and request audit (protocol 12.1), present once an agent is configured.
                                   + (event.contains(QStringLiteral("max_steps"))
                                      ? QStringLiteral(" · %1 steps / %2 tool calls per turn · request audit %3").arg(event.value(QStringLiteral("max_steps")).toInt())
                                            .arg(event.value(QStringLiteral("max_tool_calls")).toInt())
                                            .arg(event.value(QStringLiteral("audit_requests")).toBool() ? QStringLiteral("on") : QStringLiteral("off"))
                                      : QString()));
        return true;
    }
    // Observed, not consumed.
    if (type == QStringLiteral("agent_started")) { m_mainBusy = true; changed(); }
    else if (type == QStringLiteral("agent_finished")) { m_mainBusy = false; changed(); }
    else if (type == QStringLiteral("context")) { m_mainTokens = qint64(event.value(QStringLiteral("used_tokens")).toDouble()); changed(); }
    else if (type == QStringLiteral("reset")) clearFinished();
    else if (type == QStringLiteral("ready")) clear();
    return false;
}

// ----- the strip layout: subagents paired with the open task list ------------------------------
// Owner, 2026-09-19: the open task list under the prompt, up to five rows, centred on the marginal
// task, subagents on the left and tasks on the right, one row per (subagent, task) pair. Pure: no
// widgets, so tests/striplayout_test.cpp runs it without a window.

int StripLayout::subagentRows() const {
    int n = 0;
    for (const auto &row : rows) if (!row.subagentId.isEmpty()) ++n;
    return n;
}

int StripLayout::taskRows() const {
    int n = 0;
    for (const auto &row : rows) if (!row.todoId.isEmpty()) ++n;
    return n;
}

QList<LedgerTodo> currentTaskList(const RequestLedgerModel *ledger) {
    QList<LedgerTodo> out;
    if (!ledger) return out;
    // The same filter the task list panel uses (RequestsPanel::refresh): the current batch at top
    // level, earlier batches only behind its "Earlier" fold — they never reach the strip.
    QHash<QString, int> batch;
    for (const auto &task : ledger->tasks()) batch.insert(task.key, task.batch);
    const int current = ledger->currentBatch();
    const QList<LedgerTodo> todos = ledger->allTodos();
    for (const auto &todo : todos)
        if (batch.value(todo.id, current) == current) out << todo;
    return out;
}

int marginalWindowStart(const QStringList &statuses, int maxRows) {
    const int n = int(statuses.size());
    if (maxRows <= 0 || n <= maxRows) return 0;
    auto settled = [](const QString &status) {
        return status == QStringLiteral("completed") || status == QStringLiteral("done")
               || status == QStringLiteral("cancelled") || status == QStringLiteral("cancelled_by_user");
    };
    int marginal = statuses.indexOf(QStringLiteral("in_progress"));
    if (marginal < 0)
        for (int i = 0; i < n; ++i)
            if (!settled(statuses.at(i))) { marginal = i; break; }
    // Everything settled: show the tail of the list, the tasks that finished last.
    if (marginal < 0) return n - maxRows;
    return std::clamp(marginal - (maxRows - 1) / 2, 0, n - maxRows);
}

namespace {
// After the window is chosen (never before: its contents must not change), keep tasks that share a
// subagent next to each other, each group anchored at its earliest member. A no-op while the
// protocol gives a subagent one todo (card #QHR1); it makes the multi-todo case right on the day
// it does not.
QList<LedgerTodo> groupAdjacent(const QList<LedgerTodo> &window) {
    QList<LedgerTodo> out;
    std::vector<bool> taken(window.size(), false);
    for (int i = 0; i < window.size(); ++i) {
        if (taken[i]) continue;
        out << window.at(i);
        taken[i] = true;
        const QString &subagent = window.at(i).subagent;
        if (subagent.isEmpty()) continue;
        for (int j = i + 1; j < window.size(); ++j)
            if (!taken[j] && window.at(j).subagent == subagent) { out << window.at(j); taken[j] = true; }
    }
    return out;
}
}  // namespace

StripLayout layoutStrip(const SubagentModel &subagents, const RequestLedgerModel *ledger, int maxRows) {
    StripLayout out;
    maxRows = std::max(0, maxRows);
    const QList<LedgerTodo> tasks = currentTaskList(ledger);
    out.taskTotal = int(tasks.size());
    for (const auto &todo : tasks) if (todo.open()) ++out.openTasks;

    // With nothing open the task half stays away entirely, so a strip that only lists subagents is
    // exactly what it has always been.
    if (out.openTasks > 0 && maxRows > 0) {
        QStringList statuses;
        statuses.reserve(tasks.size());
        for (const auto &todo : tasks) statuses << todo.status;
        out.windowStart = marginalWindowStart(statuses, maxRows);
        const QList<LedgerTodo> window = groupAdjacent(tasks.mid(out.windowStart, std::min<int>(maxRows, tasks.size())));
        out.hiddenTasks = int(tasks.size()) - int(window.size());
        for (const auto &todo : window) {
            StripRow row;
            row.todoId = todo.id;
            // A task whose subagent is no longer listed (dismissed) keeps an empty left cell.
            if (!todo.subagent.isEmpty() && subagents.row(todo.subagent)) {
                row.subagentId = todo.subagent;
                row.linked = true;
            }
            out.rows << row;
        }
    }

    // Subagents not already paired with a task on screen: a re-run of a task the strip shows goes
    // straight under it (the task cell stays on the first row of the group), the rest take the
    // topmost free left cell, then a row of their own, then they are counted as hidden.
    QSet<QString> placed;
    for (const auto &row : std::as_const(out.rows)) if (!row.subagentId.isEmpty()) placed.insert(row.subagentId);
    QList<const SubagentRow *> leftover;
    for (const auto &row : subagents.rows()) if (row.live() && !placed.contains(row.id)) leftover << &row;
    for (const auto &row : subagents.rows()) if (!row.live() && !placed.contains(row.id)) leftover << &row;
    for (const SubagentRow *agent : std::as_const(leftover)) {
        int group = -1;
        if (!agent->todoId.isEmpty())
            for (int i = 0; i < out.rows.size(); ++i)
                if (out.rows.at(i).todoId == agent->todoId || (group >= 0 && out.rows.at(i).todoId.isEmpty())) group = i;
        if (group >= 0 && out.rows.size() < maxRows) {
            StripRow row;
            row.subagentId = agent->id;
            out.rows.insert(group + 1, row);
            continue;
        }
        int free = -1;
        for (int i = 0; i < out.rows.size(); ++i) if (out.rows.at(i).subagentId.isEmpty()) { free = i; break; }
        if (free >= 0) { out.rows[free].subagentId = agent->id; continue; }
        if (out.rows.size() < maxRows) { StripRow row; row.subagentId = agent->id; out.rows << row; continue; }
        ++out.hiddenSubagents;
    }

    for (int i = 1; i < out.rows.size(); ++i)
        out.rows[i].subagentRepeats = !out.rows.at(i).subagentId.isEmpty()
                                      && out.rows.at(i).subagentId == out.rows.at(i - 1).subagentId;
    return out;
}

// ----- panel ---------------------------------------------------------------------------------

SubagentsPanel::SubagentsPanel(SubagentModel *model, RequestLedgerModel *ledger, QWidget *parent)
    : QWidget(parent), m_model(model), m_ledger(ledger) {
    setObjectName(QStringLiteral("subagentsPanel"));
    setFocusPolicy(Qt::ClickFocus);
    setAccessibleName(QStringLiteral("Running agents"));
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setMouseTracking(false);
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, [this] { update(); });
    rebuild();
    hide();
}

int SubagentsPanel::rowHeight() const { return fontMetrics().height() + 6; }

int SubagentsPanel::visibleCount() const {
    return tasksMode() ? int(m_layout.rows.size()) : std::min<int>(kMaxVisible, m_model->rows().size());
}

int SubagentsPanel::rowCount() const {
    return tasksMode() ? int(m_layout.rows.size()) : int(m_model->rows().size());
}

int SubagentsPanel::firstVisible() const {
    // With a task column the rows are the layout's and there is no scrolling: two columns scrolling
    // independently in a five-row strip cannot be navigated, and both full lists are one key away.
    if (tasksMode()) return 0;
    const int n = m_model->rows().size();
    if (n <= kMaxVisible) return 0;
    const int index = std::max(0, m_selected - 1);
    return std::clamp(index - kMaxVisible + 1, 0, n - kMaxVisible);
}

int SubagentsPanel::leftWidth() const {
    return tasksMode() && m_layout.subagentRows() > 0 ? width() / 2 : width();
}

QSize SubagentsPanel::sizeHint() const {
    if (foldedNow()) return {200, rowHeight() + 6};
    const int extra = tasksMode() ? ((m_layout.hiddenSubagents > 0 || m_layout.hiddenTasks > 0) ? 1 : 0)
                                  : (m_model->rows().size() > kMaxVisible ? 1 : 0);
    const int lines = 1 + visibleCount() + extra;
    return {200, lines * rowHeight() + 6};
}

QString SubagentsPanel::selectedId() const {
    const int index = m_selected - 1;
    if (!tasksMode()) return index >= 0 && index < m_model->rows().size() ? m_model->rows().at(index).id : QString();
    if (m_column == Tasks) return QString();
    return index >= 0 && index < m_layout.rows.size() ? m_layout.rows.at(index).subagentId : QString();
}

QString SubagentsPanel::selectedTodoId() const {
    const int index = m_selected - 1;
    if (!tasksMode() || m_column != Tasks) return QString();
    return index >= 0 && index < m_layout.rows.size() ? m_layout.rows.at(index).todoId : QString();
}

const LedgerTodo *SubagentsPanel::todoFor(const QString &id) const {
    if (id.isEmpty()) return nullptr;
    for (const auto &todo : m_tasks) if (todo.id == id) return &todo;
    return nullptr;
}

void SubagentsPanel::rebuild() {
    m_layout = layoutStrip(*m_model, m_ledger, kMaxVisible);
    m_tasks = currentTaskList(m_ledger);
    m_taskRunning = false;
    for (const auto &todo : std::as_const(m_tasks))
        if (todo.status == QStringLiteral("in_progress")) { m_taskRunning = true; break; }
    // One column only: the selection can never sit in the other one.
    if (m_layout.taskRows() == 0) m_column = Subagents;
    else if (m_layout.subagentRows() == 0) m_column = Tasks;
}

void SubagentsPanel::refresh() {
    rebuild();
    // The strip is the running agents *and* the open task list: it comes up when either has
    // something and goes away when both are done (owner, 2026-09-19).
    const bool show = m_allowed && (!m_model->isEmpty() || m_layout.openTasks > 0);
    m_selected = std::clamp(m_selected, 0, rowCount());
    if (show != isVisible()) {
        if (!show && hasFocus() && onExit) onExit();
        setVisible(show);
    }
    // Tick while an agent runs or a task is in progress, so elapsed times and statuses stay live.
    if (show && (m_model->liveCount() > 0 || m_taskRunning)) { if (!m_tick.isActive()) m_tick.start(); }
    else m_tick.stop();
    updateGeometry();
    update();
}

void SubagentsPanel::setFolded(bool folded, const QString &keys) {
    if (m_folded == folded && m_foldKeys == keys) return;
    m_folded = folded; m_foldKeys = keys;
    refresh();
}

QString SubagentsPanel::foldedText() const {
    const int n = int(m_model->rows().size());
    const int live = m_model->liveCount();
    QString text = live > 0 ? QStringLiteral("%1 subagent%2 running").arg(live).arg(live == 1 ? QString() : QStringLiteral("s"))
                            : QStringLiteral("%1 subagent%2 finished").arg(n).arg(n == 1 ? QString() : QStringLiteral("s"));
    if (live > 0 && n > live) text += QStringLiteral(" · %1 finished").arg(n - live);
    if (m_layout.openTasks > 0)
        text += QStringLiteral(" · %1 task%2 open").arg(m_layout.openTasks).arg(m_layout.openTasks == 1 ? QString() : QStringLiteral("s"));
    return text + QStringLiteral(" · %1 to open").arg(m_foldKeys.isEmpty() ? QStringLiteral("Enter") : m_foldKeys);
}

void SubagentsPanel::enter() {
    if (m_model->isEmpty() && m_layout.taskRows() == 0) return;
    // Always start at the first subagent; with none listed, at the first task.
    m_column = m_model->rows().isEmpty() ? Tasks : Subagents;
    selectRow(1);
    setFocus(Qt::TabFocusReason);
    update();
}

// Move the selection to `row`, keeping the column where it is unless that cell is empty: then the
// selection follows the content, so Up/Down never lands on nothing.
void SubagentsPanel::selectRow(int row) {
    m_selected = std::clamp(row, 0, rowCount());
    if (tasksMode() && m_selected > 0) {
        const StripRow &current = m_layout.rows.at(m_selected - 1);
        if (m_column == Tasks && current.todoId.isEmpty() && !current.subagentId.isEmpty()) m_column = Subagents;
        else if (m_column == Subagents && current.subagentId.isEmpty() && !current.todoId.isEmpty()) m_column = Tasks;
    }
    update();
}

// Left/Right: keep the row, cross to the other column. An empty cell takes the nearest row below
// with one, else above; a column with nothing in it is ignored.
void SubagentsPanel::moveColumn(int column) {
    if (!tasksMode() || m_selected <= 0) return;
    if (column == Tasks ? m_layout.taskRows() == 0 : m_layout.subagentRows() == 0) return;
    const int n = int(m_layout.rows.size());
    auto filled = [&](int i) {
        return column == Tasks ? !m_layout.rows.at(i).todoId.isEmpty() : !m_layout.rows.at(i).subagentId.isEmpty();
    };
    int index = m_selected - 1;
    if (!filled(index)) {
        int found = -1;
        for (int j = index + 1; j < n && found < 0; ++j) if (filled(j)) found = j;
        for (int j = index - 1; j >= 0 && found < 0; --j) if (filled(j)) found = j;
        if (found < 0) return;
        index = found;
    }
    m_selected = index + 1;
    m_column = column;
    update();
}

// Enter (and a left click) on the selected cell: a subagent opens its tab; a task opens the
// subagent that has it when one is listed, else the task list on that task.
void SubagentsPanel::openSelected() {
    if (tasksMode() && m_column == Tasks) {
        const QString todoId = selectedTodoId();
        if (todoId.isEmpty()) return;
        const LedgerTodo *todo = todoFor(todoId);
        const QString subagent = todo ? todo->subagent : QString();
        if (!subagent.isEmpty() && m_model->row(subagent)) { if (onOpen) onOpen(subagent); }
        else if (onOpenTask) onOpenTask(todoId);
        return;
    }
    const QString id = selectedId();
    if (!id.isEmpty() && onOpen) onOpen(id);
}

int SubagentsPanel::rowAt(const QPoint &pos) const {
    const int line = (pos.y() - 2) / rowHeight();
    if (line <= 0) return 0;
    if (line > visibleCount()) return -1;
    return firstVisible() + line;
}

int SubagentsPanel::columnAt(const QPoint &pos) const {
    if (!tasksMode()) return Subagents;
    if (m_layout.subagentRows() == 0) return Tasks;
    return pos.x() < leftWidth() ? Subagents : Tasks;
}

void SubagentsPanel::act(bool stop) {
    const QString id = selectedId();
    const SubagentRow *row = m_model->row(id);
    if (!row) return;
    if (row->live()) { if (stop && onStop) onStop(id); }
    else m_model->dismiss(id);
    refresh();
}

void SubagentsPanel::pickModel(int row) {
    if (row <= 0 || !onPickModel) return;
    m_selected = row;
    const QString id = selectedId();
    if (id.isEmpty()) return;
    QRect chip = m_modelChips.value(row);
    if (chip.isNull()) chip = QRect(0, 2 + (row - firstVisible()) * rowHeight(), width(), rowHeight());
    update();
    onPickModel(id, mapToGlobal(chip.bottomLeft()));
}

void SubagentsPanel::keyPressEvent(QKeyEvent *event) {
    const auto mods = event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    const int n = rowCount();
    if (mods != Qt::NoModifier) { QWidget::keyPressEvent(event); return; }
    if (foldedNow()) {
        // One line: Enter goes to the open subagent pane, Down to the list beneath, Up/Esc back.
        switch (event->key()) {
        case Qt::Key_Return: case Qt::Key_Enter: if (onOpenPane) onOpenPane(); return;
        case Qt::Key_Down: if (onBelow) onBelow(); return;
        case Qt::Key_Up: case Qt::Key_Escape: if (onExit) onExit(); return;
        default: QWidget::keyPressEvent(event); return;
        }
    }
    switch (event->key()) {
    case Qt::Key_Up:
        if (m_selected <= 0) { if (onExit) onExit(); }
        else selectRow(m_selected - 1);
        return;
    case Qt::Key_Down:
        if (m_selected < n) selectRow(m_selected + 1);
        else if (onBelow) onBelow();
        return;
    case Qt::Key_Left: moveColumn(Subagents); return;
    case Qt::Key_Right: moveColumn(Tasks); return;
    case Qt::Key_Home: selectRow(0); return;
    case Qt::Key_End: selectRow(n); return;
    case Qt::Key_Return: case Qt::Key_Enter:
        if (m_selected == 0) { if (onExit) onExit(); }
        else openSelected();
        return;
    case Qt::Key_X: case Qt::Key_Delete: case Qt::Key_Backspace:
        // A task is not dismissible: act() finds no subagent in the task column and does nothing.
        act(true);
        return;
    case Qt::Key_M:
        pickModel(m_selected);
        return;
    case Qt::Key_S:
        // S hands the task to a new background subagent, exactly as in the task list (card #QHR1).
        if (tasksMode() && m_column == Tasks) {
            const LedgerTodo *todo = todoFor(selectedTodoId());
            if (todo && todo->delegable() && onRunTaskAsSubagent) onRunTaskAsSubagent(todo->id);
            return;
        }
        QWidget::keyPressEvent(event);
        return;
    case Qt::Key_Escape:
        if (onExit) onExit();
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}

void SubagentsPanel::mousePressEvent(QMouseEvent *event) {
    if (foldedNow()) {
        if (event->button() == Qt::LeftButton && onOpenPane) { onOpenPane(); if (onMouseOpen) onMouseOpen(); }
        return;
    }
    const int row = rowAt(event->pos());
    if (row < 0) return;
    m_selected = row;
    if (row > 0) m_column = columnAt(event->pos());
    setFocus(Qt::MouseFocusReason);
    // A click on a task cell does what Enter does there, then teaches the keyboard path.
    if (row > 0 && tasksMode() && m_column == Tasks) {
        if (event->button() == Qt::LeftButton && !selectedTodoId().isEmpty()) {
            update();
            openSelected();
            if (onMouseOpenTask) onMouseOpenTask();
            return;
        }
        update();
        return;
    }
    // The × at the right edge of the subagent cell stops or dismisses; the model chip opens the
    // model picker; anywhere else on a subagent row opens its tab (owner, 2026-09-18: a click).
    if (row > 0 && event->pos().x() >= leftWidth() - 24) act(true);
    else if (row > 0 && m_modelChips.value(row).contains(event->pos())) pickModel(row);
    else if (row > 0 && event->button() == Qt::LeftButton && !selectedId().isEmpty() && onOpen) {
        const QString id = selectedId();
        update();
        onOpen(id);
        if (onMouseOpen) onMouseOpen();
        return;
    }
    update();
}

// A second click of a double-click lands on a row the first click already opened.
void SubagentsPanel::mouseDoubleClickEvent(QMouseEvent *) {}

void SubagentsPanel::focusInEvent(QFocusEvent *event) { QWidget::focusInEvent(event); update(); }
void SubagentsPanel::focusOutEvent(QFocusEvent *event) { QWidget::focusOutEvent(event); update(); }

void SubagentsPanel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    const QFontMetrics fm = fontMetrics();
    const int h = rowHeight();
    const bool focused = hasFocus();
    // The list sits beneath the prompt box and draws its own card.
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(theme::Border);
    p.setBrush(theme::Surface);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    p.setBrush(Qt::NoBrush);
    p.setRenderHint(QPainter::Antialiasing, false);
    if (foldedNow()) {
        const QRect r(0, 2, width(), h);
        if (focused) p.fillRect(r.adjusted(4, 0, -4, 0), theme::SurfaceRaised.lighter(135));
        const bool live = m_model->liveCount() > 0;
        p.setPen(live ? theme::Accent : theme::TextMuted);
        p.drawText(QRect(10, r.top(), 16, h), Qt::AlignCenter, live ? QStringLiteral("●") : QStringLiteral("✓"));
        p.setPen(theme::Text);
        p.drawText(QRect(30, r.top(), width() - 40, h), Qt::AlignVCenter | Qt::AlignLeft,
                   fm.elidedText(foldedText(), Qt::ElideRight, width() - 40));
        return;
    }
    const int nameWidth = std::max(fm.horizontalAdvance(QStringLiteral("general a00")) + 8, 96);
    const int taskNameWidth = std::max(fm.horizontalAdvance(QStringLiteral("T12")) + 8, 40);
    // Both columns: subagents left, tasks right, split at half width. One column: the full width,
    // so a strip that only lists agents is drawn exactly as it always was.
    const bool split = tasksMode() && m_layout.subagentRows() > 0;
    const int lw = leftWidth();

    m_modelChips.clear();
    auto drawRow = [&](const QRect &cell, bool selected, const QString &icon, const QColor &iconColor, const QString &name,
                       const QColor &nameColor, int nameW, const QString &description, const QString &metrics,
                       const QString &badge, bool closable, const QString &model = QString(), int rowIndex = 0) {
        const QRect r = cell;
        if (selected) p.fillRect(r.adjusted(4, 0, -4, 0), focused ? theme::SurfaceRaised.lighter(135) : theme::SurfaceRaised);
        int x = cell.left() + 10;
        p.setPen(iconColor);
        p.drawText(QRect(x, r.top(), 16, h), Qt::AlignCenter, icon);
        x += 20;
        p.setPen(nameColor);
        QFont bold = font(); bold.setBold(true); p.setFont(bold);
        p.drawText(QRect(x, r.top(), nameW, h), Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(name, Qt::ElideRight, nameW - 4));
        p.setFont(font());
        x += nameW;
        int right = cell.left() + cell.width() - (closable ? 28 : 10);
        if (!badge.isEmpty()) {
            const int bw = fm.horizontalAdvance(badge) + 10;
            const QRect br(right - bw, r.top() + 3, bw, h - 6);
            p.setPen(theme::Border); p.setBrush(theme::SurfaceRaised);
            p.drawRoundedRect(br, 4, 4);
            p.setPen(theme::TextMuted);
            p.drawText(br, Qt::AlignCenter, badge);
            p.setBrush(Qt::NoBrush);
            right -= bw + 8;
        }
        if (rowIndex > 0) {
            // The model chip: click it, or press m on the row, to pick another model.
            const QString label = fm.elidedText(model.isEmpty() ? QStringLiteral("model") : model, Qt::ElideMiddle, 150)
                                  + QStringLiteral(" ▾");
            const int cw = fm.horizontalAdvance(label) + 12;
            const QRect cr(right - cw, r.top() + 3, cw, h - 6);
            p.setPen(theme::Border); p.setBrush(theme::SurfaceRaised);
            p.drawRoundedRect(cr, 4, 4);
            p.setPen(theme::Text);
            p.drawText(cr, Qt::AlignCenter, label);
            p.setBrush(Qt::NoBrush);
            m_modelChips.insert(rowIndex, cr);
            right -= cw + 8;
        }
        const int mw = std::min(fm.horizontalAdvance(metrics) + 8, std::max(0, (right - x) / 2));
        p.setPen(theme::TextMuted);
        p.drawText(QRect(right - mw, r.top(), mw, h), Qt::AlignVCenter | Qt::AlignRight, fm.elidedText(metrics, Qt::ElideLeft, mw));
        p.setPen(theme::Text);
        const int dw = std::max(0, right - mw - 12 - x);
        p.drawText(QRect(x, r.top(), dw, h), Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(description, Qt::ElideRight, dw));
        if (closable) {
            p.setPen(theme::TextMuted);
            p.drawText(QRect(cell.left() + cell.width() - 24, r.top(), 16, h), Qt::AlignCenter, QStringLiteral("×"));
        }
    };

    QString hint = QStringLiteral("↓ from the prompt to select");
    if (focused && !tasksMode()) hint = QStringLiteral("↑↓ select · Enter open tab · m model · x stop/dismiss · Esc back");
    else if (focused && split) hint = QStringLiteral("↑↓ select · ←→ columns · Enter open · S run as subagent · Esc back");
    else if (focused) hint = QStringLiteral("↑↓ select · Enter open · S run as subagent · Esc back");
    // With no agents to account for, the main row reports the task list's progress instead.
    const QString mainMetrics = (tasksMode() && m_model->rows().isEmpty() && m_ledger)
                                    ? m_ledger->summary().progress()
                                    : m_model->tokenSplit();
    drawRow(QRect(0, 2, width(), h), focused && m_selected == 0, m_model->mainBusy() ? QStringLiteral("●") : QStringLiteral("○"),
            m_model->mainBusy() ? theme::Accent : theme::TextMuted, QStringLiteral("main"), theme::Text, nameWidth,
            (m_model->mainBusy() ? QStringLiteral("relaying…") : QStringLiteral("idle")) + QStringLiteral("   ") + hint,
            mainMetrics, QString(), false);

    // The rows of the body: the layout's pairs when there are tasks, the scrolled subagent window
    // when there are not.
    const auto &modelRows = m_model->rows();
    const int first = firstVisible();
    QList<StripRow> lines;
    if (tasksMode()) lines = m_layout.rows;
    else for (int i = 0; i < visibleCount(); ++i) { StripRow line; line.subagentId = modelRows.at(first + i).id; lines << line; }

    for (int i = 0; i < lines.size(); ++i) {
        const StripRow &line = lines.at(i);
        const int rowIndex = first + i + 1;
        const int y = 2 + (i + 1) * h;
        const QRect leftCell = split ? QRect(0, y, lw, h) : QRect(0, y, width(), h);
        const QRect rightCell = split ? QRect(lw, y, width() - lw, h) : QRect(0, y, width(), h);
        const bool onRow = focused && m_selected == rowIndex;
        if (!line.subagentId.isEmpty()) {
            if (line.subagentRepeats) {
                // The same agent as the row above: a continuation mark, not the name again.
                drawRow(leftCell, onRow && (!split || m_column == Subagents), QString(), theme::Agent,
                        QStringLiteral("↳"), theme::Agent, taskNameWidth, QString(), QString(), QString(), false);
            } else if (const SubagentRow *row = m_model->row(line.subagentId)) {
                QColor color = theme::TextMuted;
                if (row->status == QStringLiteral("running")) color = theme::Accent;
                else if (row->status == QStringLiteral("done")) color = kDone();
                else if (row->status == QStringLiteral("failed")) color = kFailed();
                QStringList parts;
                if (!row->live()) parts << row->status;
                else if (row->status == QStringLiteral("waiting")) parts << QStringLiteral("waiting");
                parts << SubagentModel::formatElapsed(m_model->elapsedNow(*row));
                parts << QStringLiteral("%1 tool%2").arg(row->tools).arg(row->tools == 1 ? QString() : QStringLiteral("s"));
                parts << SubagentModel::formatTokens(row->tokens, row->tokensEstimated) + QStringLiteral(" tok");
                QString description = row->description;
                if (row->live() && !row->lastActivity.isEmpty() && row->lastActivity != QStringLiteral("starting"))
                    description += QStringLiteral("  ·  ") + row->lastActivity;
                drawRow(leftCell, onRow && (!split || m_column == Subagents), SubagentModel::statusIcon(row->status), color,
                        QStringLiteral("%1 %2").arg(row->type, row->id), theme::Text, nameWidth, description,
                        parts.join(QStringLiteral(" · ")), row->background ? QStringLiteral("bg") : QString(), true,
                        row->model, rowIndex);
            }
        }
        if (split) {
            // The link between the two halves: a violet ✦ where this agent works this task, a plain
            // rule everywhere else.
            if (line.linked) {
                p.setPen(theme::Agent);
                p.drawText(QRect(lw - 8, y, 16, h), Qt::AlignCenter, QStringLiteral("✦"));
            } else {
                p.setPen(theme::Border);
                p.drawLine(lw, y + 3, lw, y + h - 3);
            }
        }
        if (!line.todoId.isEmpty()) {
            const LedgerTodo *todo = todoFor(line.todoId);
            if (!todo) continue;
            // The subagent is named here only when this row does not already show it on the left.
            const QString metrics = (!todo->subagent.isEmpty() && todo->subagent != line.subagentId)
                                        ? QStringLiteral("✦ ") + todo->subagent
                                        : QString();
            drawRow(rightCell, onRow && (!split || m_column == Tasks), RequestLedgerModel::todoGlyph(todo->status),
                    taskColor(todo->status), todo->id, theme::Text, taskNameWidth,
                    clean(todo->text).simplified(), metrics, QString(), false);
        }
    }

    QStringList more;
    if (tasksMode()) {
        if (m_layout.hiddenSubagents > 0)
            more << QStringLiteral("+%1 agent%2").arg(m_layout.hiddenSubagents).arg(m_layout.hiddenSubagents == 1 ? QString() : QStringLiteral("s"));
        if (m_layout.hiddenTasks > 0)
            more << QStringLiteral("+%1 task%2").arg(m_layout.hiddenTasks).arg(m_layout.hiddenTasks == 1 ? QString() : QStringLiteral("s"));
        if (!more.isEmpty()) more << QStringLiteral("Agents… and /tasks in the palette");
    } else if (modelRows.size() > kMaxVisible) {
        more << QStringLiteral("+%1 more · ↑↓ scroll · Agents… in the palette").arg(modelRows.size() - kMaxVisible);
    }
    if (!more.isEmpty()) {
        p.setPen(theme::TextMuted);
        p.drawText(QRect(30, 2 + (visibleCount() + 1) * h, width() - 40, h), Qt::AlignVCenter | Qt::AlignLeft,
                   more.join(QStringLiteral(" · ")));
    }
}
}  // namespace relay
