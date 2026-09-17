// SPDX-License-Identifier: GPL-3.0-or-later
#include "RequestLedger.h"

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
}  // namespace

bool RequestLedgerModel::handle(const QJsonObject &event) {
    const QString type = event.value(QStringLiteral("event")).toString();
    if (type == QStringLiteral("requests")) {
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
            request.turn = item.value(QStringLiteral("turn")).toInt();
            request.requiresCompletion = item.value(QStringLiteral("requires_completion")).toBool(true);
            request.delivered = item.value(QStringLiteral("delivered")).toBool();
            request.todoIds = strings(item.value(QStringLiteral("todo_ids")));
            request.attachments = strings(item.value(QStringLiteral("attachments")));
            for (const auto &flag : item.value(QStringLiteral("audit")).toArray())
                request.auditQuotes << flag.toObject().value(QStringLiteral("quote")).toString();
            m_requests << request;
        }
        m_total = event.value(QStringLiteral("total")).toInt(m_requests.size());
        m_open = event.value(QStringLiteral("open")).toInt();
        m_counts.clear();
        const QJsonObject counts = event.value(QStringLiteral("counts")).toObject();
        for (auto it = counts.begin(); it != counts.end(); ++it) m_counts.insert(it.key(), it.value().toInt());
        changed();
        return true;
    }
    if (type == QStringLiteral("todos")) {
        m_todos.clear();
        for (const auto &value : event.value(QStringLiteral("items")).toArray()) {
            const QJsonObject item = value.toObject();
            LedgerTodo todo;
            todo.id = item.value(QStringLiteral("id")).toString();
            todo.text = item.value(QStringLiteral("text")).toString();
            todo.status = item.value(QStringLiteral("status")).toString(QStringLiteral("pending"));
            todo.note = item.value(QStringLiteral("note")).toString();
            todo.requestIds = strings(item.value(QStringLiteral("request_ids")));
            m_todos << todo;
        }
        m_openTodos = 0;
        for (const auto &todo : std::as_const(m_todos)) if (todo.open()) ++m_openTodos;
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
    changed();
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
    if (m_open > 0) return QStringLiteral("Requests %1 open").arg(m_open);
    return QStringLiteral("Requests ✓ %1").arg(m_total);
}

QString RequestLedgerModel::chipToolTip() const {
    QStringList parts;
    static const QStringList order{QStringLiteral("in_progress"), QStringLiteral("open"), QStringLiteral("done"), QStringLiteral("blocked"),
                                   QStringLiteral("deferred"), QStringLiteral("cancelled"), QStringLiteral("cancelled_by_user")};
    for (const QString &status : order)
        if (m_counts.value(status) > 0) parts << QStringLiteral("%1 %2").arg(m_counts.value(status)).arg(statusLabel(status));
    QString tip = QStringLiteral("%1 request%2 this session").arg(m_total).arg(m_total == 1 ? QString() : QStringLiteral("s"));
    if (!parts.isEmpty()) tip += QStringLiteral(": ") + parts.join(QStringLiteral(", "));
    if (m_openTodos > 0) tip += QStringLiteral("\n%1 open todo%2").arg(m_openTodos).arg(m_openTodos == 1 ? QString() : QStringLiteral("s"));
    return tip;
}

QString RequestLedgerModel::statusGlyph(const QString &status) {
    if (status == QStringLiteral("done") || status == QStringLiteral("completed")) return QStringLiteral("✓");
    if (status == QStringLiteral("in_progress")) return QStringLiteral("◐");
    if (status == QStringLiteral("cancelled") || status == QStringLiteral("cancelled_by_user")) return QStringLiteral("✕");
    if (status == QStringLiteral("deferred") || status == QStringLiteral("blocked")) return QStringLiteral("⏸");
    return QStringLiteral("○");   // open, pending
}

QString RequestLedgerModel::statusLabel(const QString &status) {
    if (status == QStringLiteral("in_progress")) return QStringLiteral("in progress");
    if (status == QStringLiteral("cancelled_by_user")) return QStringLiteral("cancelled by you");
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
    QStringList requests;
    int todos = 0, requestCount = 0;
    for (const auto &item : items) {
        if (item.kind == QStringLiteral("todo")) { ++todos; continue; }
        ++requestCount;
        requests << item.id + ' ' + quoted(item.preview, 48);
    }
    QString line;
    if (requestCount > 0) {
        line = QStringLiteral("%1 request%2 still open: ").arg(requestCount).arg(requestCount == 1 ? QString() : QStringLiteral("s"));
        QString list;
        int shown = 0;
        for (const QString &entry : std::as_const(requests)) {
            const QString next = list.isEmpty() ? entry : list + QStringLiteral(", ") + entry;
            if (shown > 0 && line.size() + next.size() > maxChars) break;
            list = next; ++shown;
        }
        if (shown < requests.size()) list += QStringLiteral(" +%1 more").arg(requests.size() - shown);
        line += list;
    }
    if (todos > 0) {
        const QString part = QStringLiteral("%1 todo%2 open").arg(todos).arg(todos == 1 ? QString() : QStringLiteral("s"));
        line = line.isEmpty() ? part : line + QStringLiteral(" · ") + part;
    }
    return line;
}

QString RequestLedgerModel::limitLine(const QJsonObject &done) {
    const QJsonObject limit = done.value(QStringLiteral("limit")).toObject();
    const bool tools = limit.value(QStringLiteral("which")).toString() == QStringLiteral("tool_calls");
    const QString counts = tools
        ? QStringLiteral("%1 tool calls, limit %2").arg(limit.value(QStringLiteral("tool_calls")).toInt()).arg(limit.value(QStringLiteral("max_tool_calls")).toInt())
        : QStringLiteral("%1 model steps, limit %2").arg(limit.value(QStringLiteral("steps")).toInt()).arg(limit.value(QStringLiteral("max_steps")).toInt());
    return QStringLiteral("‖ Stopped at the %1 limit (%2) · the request stays open")
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
        parts << flag.value(QStringLiteral("request_id")).toString() + ' ' + quoted(flag.value(QStringLiteral("quote")).toString(), 60);
    }
    return parts.isEmpty() ? QString() : QStringLiteral("may be unaddressed: ") + parts.join(QStringLiteral(", "));
}

}  // namespace relay
