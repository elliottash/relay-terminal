// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Request ledger UI state: a model fed by the worker's `requests`, `todos`, `request` and
// `request_audit` events (docs/AGENT-SESSIONS-PROTOCOL.md section 12), plus the text helpers the
// pane uses for its inline status lines. No widgets, so it can be unit tested.
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

namespace relay {

struct LedgerTodo {
    QString id, text, status, note;
    QStringList requestIds;
    bool open() const { return status == QStringLiteral("pending") || status == QStringLiteral("in_progress"); }
};

struct LedgerRequest {
    QString id, preview, text;          // text: verbatim, once fetched with request_get
    QString source, origin, status, reason, turnId;
    int turn = 0;
    bool requiresCompletion = true, delivered = false;
    QStringList todoIds, attachments;
    QStringList auditQuotes;            // "may be unaddressed" flags, oldest first
    bool open() const { return status == QStringLiteral("open") || status == QStringLiteral("in_progress"); }
    QString fullText() const { return text.isEmpty() ? preview : text; }
};

// An item of `open_items` on done/cancelled/error, or of recap.open_items.
struct LedgerOpenItem {
    QString kind = QStringLiteral("request"), id, status, reason, preview;
    QStringList requestIds;
};

class RequestLedgerModel {
public:
    // Requests, todos or flags changed.
    std::function<void()> onChanged;

    // Consumes `requests`, `todos`, `request` and `request_audit`; returns true for those.
    // Every other event is ignored (false). `reset` does not clear: the worker sends fresh lists.
    bool handle(const QJsonObject &event);
    void clear();                                    // worker restarted

    const QList<LedgerRequest> &requests() const { return m_requests; }
    const QList<LedgerTodo> &todos() const { return m_todos; }
    const LedgerRequest *find(const QString &id) const;
    QList<LedgerTodo> todosFor(const QString &requestId) const;
    QList<LedgerTodo> unlinkedTodos() const;
    int total() const { return m_total; }
    int openCount() const { return m_open; }
    int count(const QString &status) const { return m_counts.value(status); }
    int openTodos() const { return m_openTodos; }
    bool isEmpty() const { return m_total == 0 && m_todos.isEmpty(); }

    // "Requests 2 open" while anything the user asked is unfinished, else "Requests ✓ 5".
    QString chipText() const;
    QString chipToolTip() const;
    // Re-ask is refused by the worker while a request runs; queued state is not known here.
    static bool canReask(const LedgerRequest &request) { return request.status != QStringLiteral("in_progress"); }

    static QString statusGlyph(const QString &status);   // ✓ ◐ ○ ✕ ⏸
    static QString statusLabel(const QString &status);   // "cancelled by you"
    static QString todoGlyph(const QString &status);
    static QList<LedgerOpenItem> parseOpenItems(const QJsonArray &items);
    // "2 requests still open: R3 “fix the docs”, R4 “…” · 1 todo open" (empty when nothing is open).
    static QString openItemsLine(const QList<LedgerOpenItem> &items, int maxChars = 160);
    // "Stopped at the model-step limit (50 of 50) · the request stays open" for done{stop_reason:limit}.
    static QString limitLine(const QJsonObject &done);
    // "✦ checking open items (1/2)"
    static QString completionCheckLine(const QJsonObject &event);
    // "may be unaddressed: R3 “…”" for request_audit (empty when nothing was flagged).
    static QString auditLine(const QJsonObject &event);

private:
    void changed() { if (onChanged) onChanged(); }
    QList<LedgerRequest> m_requests;
    QList<LedgerTodo> m_todos;
    QHash<QString, int> m_counts;
    int m_total = 0, m_open = 0, m_openTodos = 0;
};

}  // namespace relay
