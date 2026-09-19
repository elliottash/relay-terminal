// SPDX-License-Identifier: GPL-3.0-or-later
#include "RequestLedger.h"

#include "CallLines.h"   // relay::calllines::taskGlyph: the one status-glyph table

#include <QSet>
#include <algorithm>

namespace relay {

namespace {
QStringList strings(const QJsonValue &value) {
    QStringList out;
    for (const auto &item : value.toArray()) if (item.isString()) out << item.toString();
    return out;
}
QString quoted(const QString &text, int maxChars) {
    QString simple = text.simplified();
    if (simple.size() > maxChars) simple = simple.left(std::max(1, maxChars - 1)).trimmed() + QStringLiteral("…");
    return QStringLiteral("“") + simple + QStringLiteral("”");
}
int requestNumber(const QString &id) { return id.startsWith('R') ? id.mid(1).toInt() : 0; }
bool todoSettled(const QString &status) {
    return status == QStringLiteral("completed") || status == QStringLiteral("blocked") || status == QStringLiteral("deferred")
        || status == QStringLiteral("cancelled");
}
}  // namespace

void TaskSummary::add(TaskOutcome outcome) {
    ++total;
    switch (outcome) {
    case TaskOutcome::Active: ++active; break;
    case TaskOutcome::Completed: ++completed; break;
    case TaskOutcome::Failed: ++failed; break;
    case TaskOutcome::Deferred: ++deferred; break;
    case TaskOutcome::Cancelled: ++cancelled; break;
    case TaskOutcome::Unfinished: ++unfinished; break;
    }
}

QString TaskSummary::suffix() const {
    QStringList parts;
    if (failed) parts << QStringLiteral("%1 failed").arg(failed);
    if (deferred) parts << QStringLiteral("%1 deferred").arg(deferred);
    if (cancelled) parts << QStringLiteral("%1 cancelled").arg(cancelled);
    if (unfinished) parts << QStringLiteral("%1 unfinished").arg(unfinished);
    return parts.join(QStringLiteral(", "));
}

QString TaskSummary::progress(bool alwaysSuffix) const {
    QString text = QStringLiteral("%1/%2").arg(completed).arg(total);
    const QString rest = suffix();
    if (!rest.isEmpty() && (alwaysSuffix || settled())) text += QStringLiteral(" (") + rest + ')';
    return text;
}

bool RequestLedgerModel::handle(const QJsonObject &event) {
    const QString type = event.value(QStringLiteral("event")).toString();
    if (type == QStringLiteral("requests")) {
        // A different ledger (new chat, resume, rewind): ids went back or an id now means another text.
        const QJsonArray incoming = event.value(QStringLiteral("items")).toArray();
        int newMax = 0;
        bool other = false;
        for (const auto &value : incoming) {
            const QJsonObject item = value.toObject();
            const QString id = item.value(QStringLiteral("id")).toString();
            newMax = std::max(newMax, requestNumber(id));
            const auto seen = m_seenPreview.constFind(id);
            if (seen != m_seenPreview.constEnd() && *seen != item.value(QStringLiteral("text_preview")).toString()) other = true;
        }
        if (other || newMax < m_maxSeen) { resetBatches(); m_requests.clear(); m_todos.clear(); m_openTodos = 0; }
        const QList<LedgerRequest> previous = m_requests;
        QHash<QString, QString> fetched;   // keep verbatim text already fetched
        for (const auto &request : std::as_const(m_requests)) if (!request.text.isEmpty()) fetched.insert(request.id, request.text);
        m_requests.clear();
        for (const auto &value : event.value(QStringLiteral("items")).toArray()) {
            const QJsonObject item = value.toObject();
            LedgerRequest request;
            request.id = item.value(QStringLiteral("id")).toString();
            if (request.id.isEmpty()) continue;
            request.preview = item.value(QStringLiteral("text_preview")).toString();
            request.text = fetched.value(request.id);
            request.source = item.value(QStringLiteral("source")).toString();
            request.origin = item.value(QStringLiteral("origin")).toString(QStringLiteral("user"));
            request.status = item.value(QStringLiteral("status")).toString(QStringLiteral("open"));
            request.reason = item.value(QStringLiteral("reason")).toString();
            request.turnId = item.value(QStringLiteral("turn_id")).toString();
            request.queueItem = item.value(QStringLiteral("queue_item")).toString();
            request.turn = item.value(QStringLiteral("turn")).toInt();
            request.requiresCompletion = item.value(QStringLiteral("requires_completion")).toBool(true);
            request.delivered = item.value(QStringLiteral("delivered")).toBool();
            request.todoIds = strings(item.value(QStringLiteral("todo_ids")));
            request.attachments = strings(item.value(QStringLiteral("attachments")));
            for (const auto &flag : item.value(QStringLiteral("audit")).toArray())
                request.auditQuotes << flag.toObject().value(QStringLiteral("quote")).toString();
            m_requests << request;
        }
        // A re-ask or requeue keeps the entry and gives it a new queue item while it is still
        // delivered from before: it waits in the queue until its turn_id changes.
        for (auto &request : m_requests) {
            const auto before = std::find_if(previous.cbegin(), previous.cend(), [&](const LedgerRequest &r) { return r.id == request.id; });
            if (before != previous.cend()) {
                request.waiting = before->waiting;
                if (!request.queueItem.isEmpty() && request.queueItem != before->queueItem) request.waiting = true;
                if (request.turnId != before->turnId) request.waiting = false;
            }
            if (request.status != QStringLiteral("open")) request.waiting = false;
        }
        m_total = event.value(QStringLiteral("total")).toInt(m_requests.size());
        m_open = event.value(QStringLiteral("open")).toInt();
        m_counts.clear();
        const QJsonObject counts = event.value(QStringLiteral("counts")).toObject();
        for (auto it = counts.begin(); it != counts.end(); ++it) m_counts.insert(it.key(), it.value().toInt());
        updateBatches();
        changed();
        return true;
    }
    if (type == QStringLiteral("todos")) {
        const QList<LedgerTodo> previous = m_todos;
        m_todos.clear();
        for (const auto &value : event.value(QStringLiteral("items")).toArray()) {
            const QJsonObject item = value.toObject();
            LedgerTodo todo;
            todo.id = item.value(QStringLiteral("id")).toString();
            todo.text = item.value(QStringLiteral("text")).toString();
            todo.status = item.value(QStringLiteral("status")).toString(QStringLiteral("pending"));
            todo.note = item.value(QStringLiteral("note")).toString();
            todo.requestIds = strings(item.value(QStringLiteral("request_ids")));
            todo.subagent = item.value(QStringLiteral("subagent")).toString();
            todo.subagentRunning = !todo.subagent.isEmpty() && item.value(QStringLiteral("subagent_running")).toBool();
            m_todos << todo;
        }
        m_openTodos = 0;
        QSet<QString> ids;
        for (const auto &todo : std::as_const(m_todos)) { ids.insert(todo.id); m_retainedTodos.remove(todo.id); if (todo.open()) ++m_openTodos; }
        // The model sends the whole list each time; keep settled todos it dropped so the count does
        // not shrink. Open todos it dropped are gone (it removed them).
        for (const auto &todo : previous)
            if (!ids.contains(todo.id) && todoSettled(todo.status)) m_retainedTodos.insert(todo.id, todo);
        updateBatches();
        changed();
        return true;
    }
    if (type == QStringLiteral("request")) {
        const QJsonObject item = event.value(QStringLiteral("item")).toObject();
        const QString id = item.value(QStringLiteral("id")).toString();
        for (auto &request : m_requests)
            if (request.id == id) { request.text = item.value(QStringLiteral("text")).toString(); changed(); break; }
        return true;
    }
    if (type == QStringLiteral("request_audit")) {
        // Flags are also stored on the entries and arrive with the `requests` event before this one.
        return true;
    }
    return false;
}

void RequestLedgerModel::clear() {
    m_requests.clear(); m_todos.clear(); m_counts.clear();
    m_total = m_open = m_openTodos = 0;
    resetBatches();
    changed();
}

void RequestLedgerModel::resetBatches() {
    m_retainedTodos.clear(); m_requestBatch.clear(); m_todoBatch.clear(); m_seenPreview.clear();
    m_batch = m_maxSeen = 0;
}

QList<LedgerTodo> RequestLedgerModel::allTodos() const {
    QList<LedgerTodo> out;
    for (auto it = m_retainedTodos.cbegin(); it != m_retainedTodos.cend(); ++it) out << it.value();
    std::sort(out.begin(), out.end(), [](const LedgerTodo &a, const LedgerTodo &b) { return a.id.mid(1).toInt() < b.id.mid(1).toInt(); });
    out << m_todos;
    return out;
}

bool RequestLedgerModel::turnRunning() const {
    for (const auto &request : m_requests) if (request.status == QStringLiteral("in_progress")) return true;
    return false;
}

// Tasks are the model's todos. `deriveAll()` additionally derives a pseudo-task per user request;
// those never reach tasks() and exist only so the batch walk below can tell one turn's list from the
// next (a turn with no todos still closes a batch). Outcome rules (the owner's mapping):
//   todo completed → Completed; blocked → Failed; deferred → Deferred; cancelled → Cancelled.
//   todo pending/in_progress → Active while a turn runs, a linked request still waits in the queue or a
//     subagent is running it, else Unfinished (its turn ended: error, cancel, step limit, or the completion check gave up).
//     When every linked request was marked done (or cancelled) by the user, it follows the request.
//   request without todos: done → Completed; cancelled/cancelled_by_user → Cancelled; blocked →
//     Failed; deferred → Deferred; in_progress → Active; open → Active while queued (not delivered,
//     or re-asked/requeued and not delivered again) or while another turn runs, else Unfinished.
//   A request whose todos are all settled but which is open again also counts itself.
// Relay-origin requests (requires_completion false) are not tasks; their todos are.
QList<TaskItem> RequestLedgerModel::deriveAll() const {
    const bool running = turnRunning();
    QList<TaskItem> out;
    const QList<LedgerTodo> todos = allTodos();
    QSet<QString> withTodos, withOpenTodos;
    for (const auto &todo : todos)
        for (const QString &id : todo.requestIds) { withTodos.insert(id); if (!todoSettled(todo.status)) withOpenTodos.insert(id); }
    for (const auto &request : m_requests) {
        if (!request.requiresCompletion) continue;
        // A request with todos is counted through them, unless it is open again while all of them
        // are settled (reopened, re-asked, or stopped after its last todo): then it counts itself too.
        if (withTodos.contains(request.id) && (request.status != QStringLiteral("open") || withOpenTodos.contains(request.id))) continue;
        TaskItem task;
        task.key = request.id; task.todo = false; task.text = request.preview; task.status = request.status;
        task.note = request.reason; task.requestIds = QStringList{request.id};
        const QString &s = request.status;
        if (s == QStringLiteral("done")) task.outcome = TaskOutcome::Completed;
        else if (s == QStringLiteral("cancelled") || s == QStringLiteral("cancelled_by_user")) task.outcome = TaskOutcome::Cancelled;
        else if (s == QStringLiteral("blocked")) task.outcome = TaskOutcome::Failed;
        else if (s == QStringLiteral("deferred")) task.outcome = TaskOutcome::Deferred;
        else if (s == QStringLiteral("in_progress") || !request.delivered || request.waiting || running) task.outcome = TaskOutcome::Active;
        else task.outcome = TaskOutcome::Unfinished;
        out << task;
    }
    for (const auto &todo : todos) {
        TaskItem task;
        task.key = todo.id; task.text = todo.text; task.status = todo.status; task.note = todo.note; task.requestIds = todo.requestIds;
        const QString &s = todo.status;
        if (s == QStringLiteral("completed")) task.outcome = TaskOutcome::Completed;
        else if (s == QStringLiteral("blocked")) task.outcome = TaskOutcome::Failed;
        else if (s == QStringLiteral("deferred")) task.outcome = TaskOutcome::Deferred;
        else if (s == QStringLiteral("cancelled")) task.outcome = TaskOutcome::Cancelled;
        else {
            int known = 0, userDone = 0, userCancelled = 0;
            bool queued = false;
            for (const QString &id : todo.requestIds) {
                const LedgerRequest *request = find(id);
                if (!request) continue;
                ++known;
                if (request->status == QStringLiteral("done")) ++userDone;   // the worker never marks done over an open todo
                else if (request->status == QStringLiteral("cancelled_by_user")) ++userCancelled;
                else if (request->status == QStringLiteral("open") && (!request->delivered || request->waiting)) queued = true;
            }
            if (known > 0 && userDone + userCancelled == known) task.outcome = userCancelled ? TaskOutcome::Cancelled : TaskOutcome::Completed;
            // A todo a subagent is running is being worked on after the main turn ended (card #QHR1).
            else if (running || queued || todo.subagentRunning) task.outcome = TaskOutcome::Active;
            else task.outcome = TaskOutcome::Unfinished;
        }
        out << task;
    }
    return out;
}

// The user-facing task list: the model's todos only. A request is never a task — the ledger is
// internal (it links todos, survives compaction and drives re-asks), and showing a prompt as a
// task made every turn report "Tasks 1/1" for having ended normally.
QList<TaskItem> RequestLedgerModel::deriveTasks() const {
    QList<TaskItem> out;
    for (const auto &task : deriveAll()) if (task.todo) out << task;
    return out;
}

// Batches. The current task list is everything since all tasks were last settled:
//  1. New requests are taken in id order. A request with requires_completion starts a new batch
//     when the current batch has tasks, none of them is Active, no older request is in progress,
//     and it did not arrive in the same turn as a request of the current batch. Otherwise it joins
//     the current batch. (After a restart or /resume the same walk runs over the loaded ledger.)
//  2. A todo seen for the first time joins its request's batch when the request is new in this
//     event; otherwise an open todo joins the current batch and a settled one the latest batch of
//     its linked requests.
//  3. Tasks of earlier batches that are Active or Unfinished again (a stopped turn's todos, a
//     reopened or re-asked request) move into the current batch, so the latest list always shows
//     what is left. A re-asked (Active) task starts a new batch first when the current one is settled.
void RequestLedgerModel::updateBatches() {
    QList<TaskItem> list = deriveAll();
    QHash<QString, TaskOutcome> outcome;
    for (const auto &task : list) outcome.insert(task.key, task.outcome);
    auto batchOf = [&](const TaskItem &task) {
        return task.todo ? m_todoBatch.value(task.key) : m_requestBatch.value(task.key);
    };
    auto currentHasActive = [&](bool *nonEmpty) {
        bool active = false;
        *nonEmpty = false;
        for (const auto &task : std::as_const(list)) {
            if (batchOf(task) != m_batch || m_batch == 0) continue;
            *nonEmpty = true;
            if (task.outcome == TaskOutcome::Active) active = true;
        }
        return active;
    };
    bool olderRunning = false;
    for (const auto &request : std::as_const(m_requests))
        if (requestNumber(request.id) <= m_maxSeen && request.status == QStringLiteral("in_progress")) olderRunning = true;

    QList<const LedgerRequest *> fresh;
    for (const auto &request : std::as_const(m_requests)) if (requestNumber(request.id) > m_maxSeen) fresh << &request;
    std::sort(fresh.begin(), fresh.end(), [](const LedgerRequest *a, const LedgerRequest *b) { return requestNumber(a->id) < requestNumber(b->id); });
    const QList<LedgerTodo> todos = allTodos();
    for (const LedgerRequest *request : std::as_const(fresh)) {
        if (request->requiresCompletion) {
            bool nonEmpty = false;
            const bool active = currentHasActive(&nonEmpty);
            bool sameTurn = false;
            if (!request->turnId.isEmpty())
                for (const auto &other : std::as_const(m_requests))
                    if (&other != request && other.turnId == request->turnId && m_requestBatch.value(other.id) == m_batch && m_batch > 0) sameTurn = true;
            if (m_batch == 0 || (nonEmpty && !active && !olderRunning && !sameTurn)) ++m_batch;
        } else if (m_batch == 0) {
            m_batch = 1;
        }
        m_requestBatch.insert(request->id, m_batch);
        for (const auto &todo : todos)
            if (todo.requestIds.contains(request->id) && !m_todoBatch.contains(todo.id)) m_todoBatch.insert(todo.id, m_batch);
        m_maxSeen = std::max(m_maxSeen, requestNumber(request->id));
    }
    for (const auto &request : std::as_const(m_requests)) m_seenPreview.insert(request.id, request.preview);

    for (const auto &task : std::as_const(list)) {
        if (!task.todo || m_todoBatch.contains(task.key)) continue;
        if (m_batch == 0) m_batch = 1;
        int batch = m_batch;
        if (task.outcome != TaskOutcome::Active && task.outcome != TaskOutcome::Unfinished) {
            int latest = 0;
            for (const QString &id : task.requestIds) latest = std::max(latest, m_requestBatch.value(id));
            if (latest > 0) batch = latest;
        }
        m_todoBatch.insert(task.key, batch);
    }

    bool pullActive = false, pullAny = false;
    for (const auto &task : std::as_const(list)) {
        if (task.outcome != TaskOutcome::Active && task.outcome != TaskOutcome::Unfinished) continue;
        if (batchOf(task) >= m_batch) continue;
        pullAny = true;
        if (task.outcome == TaskOutcome::Active) pullActive = true;
    }
    if (!pullAny) return;
    if (pullActive) {
        bool nonEmpty = false;
        if (!currentHasActive(&nonEmpty) && nonEmpty && !olderRunning) {
            ++m_batch;
            // Unfinished tasks of the batch just closed come along as well (rule 3).
        }
    }
    for (const auto &task : std::as_const(list)) {
        if (task.outcome != TaskOutcome::Active && task.outcome != TaskOutcome::Unfinished) continue;
        if (batchOf(task) >= m_batch) continue;
        (task.todo ? m_todoBatch : m_requestBatch).insert(task.key, m_batch);
    }
}

QList<TaskItem> RequestLedgerModel::tasks() const {
    QList<TaskItem> list = deriveTasks();
    for (auto &task : list) {
        task.batch = task.todo ? m_todoBatch.value(task.key, m_batch) : m_requestBatch.value(task.key, m_batch);
    }
    return list;
}

TaskSummary RequestLedgerModel::summary() const {
    TaskSummary out;
    for (const auto &task : tasks()) if (task.batch == m_batch) out.add(task.outcome);
    for (const auto &request : m_requests)
        if (request.status == QStringLiteral("in_progress") && inCurrentBatch(request.id)) out.running = true;
    return out;
}

TaskSummary RequestLedgerModel::earlierSummary() const {
    TaskSummary out;
    for (const auto &task : tasks()) if (task.batch < m_batch) out.add(task.outcome);
    return out;
}

bool RequestLedgerModel::hasTasks() const { return !deriveTasks().isEmpty(); }

bool RequestLedgerModel::inCurrentBatch(const QString &requestId) const {
    if (m_requestBatch.value(requestId, m_batch) == m_batch) return true;
    for (const auto &task : tasks())
        if (task.todo && task.batch == m_batch && task.requestIds.contains(requestId)) return true;
    return false;
}

const LedgerRequest *RequestLedgerModel::find(const QString &id) const {
    for (const auto &request : m_requests) if (request.id == id) return &request;
    return nullptr;
}

QList<LedgerTodo> RequestLedgerModel::todosFor(const QString &requestId) const {
    QList<LedgerTodo> out;
    for (const auto &todo : m_todos) if (todo.requestIds.contains(requestId)) out << todo;
    return out;
}

QList<LedgerTodo> RequestLedgerModel::unlinkedTodos() const {
    QList<LedgerTodo> out;
    for (const auto &todo : m_todos) {
        bool linked = false;
        for (const QString &id : todo.requestIds) if (find(id)) { linked = true; break; }
        if (!linked) out << todo;
    }
    return out;
}

QString RequestLedgerModel::chipText() const {
    const TaskSummary sum = summary();
    return sum.total == 0 ? QStringLiteral("Tasks") : QStringLiteral("Tasks ") + sum.progress();
}

QString RequestLedgerModel::chipState() const {
    const TaskSummary sum = summary();
    if (sum.total == 0) return QStringLiteral("none");
    if (!sum.settled()) return QStringLiteral("running");
    return sum.completed == sum.total ? QStringLiteral("done") : QStringLiteral("attention");
}

QString RequestLedgerModel::outcomeWord(TaskOutcome outcome) {
    switch (outcome) {
    case TaskOutcome::Active: return QStringLiteral("in progress");
    case TaskOutcome::Completed: return QStringLiteral("completed");
    case TaskOutcome::Failed: return QStringLiteral("failed");
    case TaskOutcome::Deferred: return QStringLiteral("deferred");
    case TaskOutcome::Cancelled: return QStringLiteral("cancelled");
    case TaskOutcome::Unfinished: return QStringLiteral("unfinished");
    }
    return QString();
}

QString RequestLedgerModel::chipToolTip() const {
    const TaskSummary sum = summary();
    QString tip = QStringLiteral("Current task list: %1 of %2 completed").arg(sum.completed).arg(sum.total);
    if (sum.active) tip += QStringLiteral(", %1 not done yet").arg(sum.active);
    if (!sum.suffix().isEmpty()) tip += QStringLiteral(", ") + sum.suffix();
    const TaskSummary earlier = earlierSummary();
    if (earlier.total > 0) tip += QStringLiteral("\nEarlier: %1").arg(earlier.progress(true));
    return tip;
}

QString RequestLedgerModel::turnEndLine(int maxChars) const {
    const TaskSummary sum = summary();
    const int notCompleted = sum.failed + sum.deferred + sum.cancelled + sum.unfinished;
    if (sum.total == 0 || (sum.total == 1 && notCompleted == 0)) return QString();
    QString line = QStringLiteral("Tasks ") + sum.progress(true);
    QStringList entries;
    static const QList<TaskOutcome> order{TaskOutcome::Failed, TaskOutcome::Deferred, TaskOutcome::Cancelled, TaskOutcome::Unfinished};
    const QList<TaskItem> list = tasks();
    for (TaskOutcome outcome : order)
        for (const auto &task : list)
            if (task.batch == m_batch && task.outcome == outcome)
                entries << QStringLiteral("%1 %2 %3").arg(task.key, quoted(task.text, 32), outcomeWord(outcome));
    if (!entries.isEmpty()) {
        QString list;
        int shown = 0;
        for (const QString &entry : std::as_const(entries)) {
            const QString next = list.isEmpty() ? entry : list + QStringLiteral(", ") + entry;
            if (shown > 0 && line.size() + 3 + next.size() > maxChars) break;
            list = next; ++shown;
        }
        if (shown < entries.size()) list += QStringLiteral(" +%1 more").arg(entries.size() - shown);
        line += QStringLiteral(" · ") + list;
    }
    return line;
}

// One table for the whole program (card #BDXG): the tool-call fold of an `update_todos` draws the
// same list this panel does, and it has no widgets, so the table lives in the pure library and this
// calls it. Changing a glyph there changes it here, in the strip and in the task menu at once.
QString RequestLedgerModel::statusGlyph(const QString &status) { return relay::calllines::taskGlyph(status); }

QString RequestLedgerModel::statusLabel(const QString &status) {
    if (status == QStringLiteral("in_progress")) return QStringLiteral("in progress");
    if (status == QStringLiteral("cancelled_by_user")) return QStringLiteral("cancelled by you");
    if (status == QStringLiteral("blocked")) return QStringLiteral("failed");
    return status;
}

QString RequestLedgerModel::todoGlyph(const QString &status) { return statusGlyph(status); }

QList<LedgerOpenItem> RequestLedgerModel::parseOpenItems(const QJsonArray &items) {
    QList<LedgerOpenItem> out;
    for (const auto &value : items) {
        const QJsonObject object = value.toObject();
        LedgerOpenItem item;
        item.kind = object.value(QStringLiteral("kind")).toString(QStringLiteral("request"));
        item.id = object.value(QStringLiteral("id")).toString();
        item.status = object.value(QStringLiteral("status")).toString();
        item.reason = object.value(QStringLiteral("reason")).toString();
        item.preview = object.value(QStringLiteral("preview")).toString();
        item.requestIds = strings(object.value(QStringLiteral("request_ids")));
        if (!item.id.isEmpty()) out << item;
    }
    return out;
}

QString RequestLedgerModel::openItemsLine(const QList<LedgerOpenItem> &items, int maxChars) {
    // Counted as tasks, like the chip: the model's open todos. Open requests are ledger state and
    // are never named to the user.
    QStringList entries;
    for (const auto &item : items)
        if (item.kind == QStringLiteral("todo")) entries << item.id + ' ' + quoted(item.preview, 48);
    if (entries.isEmpty()) return QString();
    QString line = QStringLiteral("%1 task%2 still open: ").arg(entries.size()).arg(entries.size() == 1 ? QString() : QStringLiteral("s"));
    QString list;
    int shown = 0;
    for (const QString &entry : std::as_const(entries)) {
        const QString next = list.isEmpty() ? entry : list + QStringLiteral(", ") + entry;
        if (shown > 0 && line.size() + next.size() > maxChars) break;
        list = next; ++shown;
    }
    if (shown < entries.size()) list += QStringLiteral(" +%1 more").arg(entries.size() - shown);
    return line + list;
}

QString RequestLedgerModel::limitLine(const QJsonObject &done) {
    const QJsonObject limit = done.value(QStringLiteral("limit")).toObject();
    const bool tools = limit.value(QStringLiteral("which")).toString() == QStringLiteral("tool_calls");
    const QString counts = tools
        ? QStringLiteral("%1 tool calls, limit %2").arg(limit.value(QStringLiteral("tool_calls")).toInt()).arg(limit.value(QStringLiteral("max_tool_calls")).toInt())
        : QStringLiteral("%1 model steps, limit %2").arg(limit.value(QStringLiteral("steps")).toInt()).arg(limit.value(QStringLiteral("max_steps")).toInt());
    return QStringLiteral("‖ Stopped at the %1 limit (%2) · unfinished tasks stay open")
        .arg(tools ? QStringLiteral("tool-call") : QStringLiteral("step"), counts);
}

QString RequestLedgerModel::completionCheckLine(const QJsonObject &event) {
    return QStringLiteral("✦ checking open items (%1/%2)")
        .arg(event.value(QStringLiteral("reminder")).toInt(1)).arg(event.value(QStringLiteral("max_reminders")).toInt(2));
}

QString RequestLedgerModel::auditLine(const QJsonObject &event) {
    QStringList parts;
    for (const auto &value : event.value(QStringLiteral("unaddressed")).toArray()) {
        const QJsonObject flag = value.toObject();
        parts << quoted(flag.value(QStringLiteral("quote")).toString(), 60);   // no ledger id: the ledger is internal
    }
    return parts.isEmpty() ? QString() : QStringLiteral("may be unaddressed: ") + parts.join(QStringLiteral(", "));
}

}  // namespace relay
