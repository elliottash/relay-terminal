// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Tasks UI state (request ledger + todos): a model fed by the worker's `requests`, `todos`, `request` and
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
    QString source, origin, status, reason, turnId, queueItem;
    int turn = 0;
    bool requiresCompletion = true, delivered = false;
    bool waiting = false;               // derived: submitted again (re-ask, requeue) and not delivered yet
    QStringList todoIds, attachments;
    QStringList auditQuotes;            // "may be unaddressed" flags, oldest first
    bool open() const { return status == QStringLiteral("open") || status == QStringLiteral("in_progress"); }
    QString fullText() const { return text.isEmpty() ? preview : text; }
};

// How a task counts in the Tasks chip. A task is one of the model's todos, or a user request
// with no linked todos (the request itself). See docs/ARCHITECTURE.md, "Tasks".
enum class TaskOutcome { Active, Completed, Failed, Deferred, Cancelled, Unfinished };

struct TaskItem {
    QString key;          // "T3" (todo) or "R2" (request counted as its own task)
    bool todo = true;
    QString text, status, note;   // status: the raw todo or request status
    QStringList requestIds;       // a todo's links; a request task: its own id
    TaskOutcome outcome = TaskOutcome::Active;
    int batch = 0;
};

struct TaskSummary {
    int total = 0, completed = 0, failed = 0, deferred = 0, cancelled = 0, unfinished = 0, active = 0;
    bool running = false;   // a request of this list is in progress (its turn has not ended)
    void add(TaskOutcome outcome);
    bool settled() const { return active == 0 && !running; }
    // "1 failed, 1 deferred" in the order failed, deferred, cancelled, unfinished (empty when none).
    QString suffix() const;
    // "3/5", plus " (1 failed)" when a suffix applies and (unless always) nothing is still active.
    QString progress(bool alwaysSuffix = false) const;
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
    // The todo list plus settled todos the model dropped from it later (kept so 5/5 does not shrink).
    QList<LedgerTodo> allTodos() const;

    // ----- tasks -----
    // Every task of the session with its outcome and batch. The current batch is the task list
    // since everything was last settled; see "Batches" in RequestLedger.cpp.
    QList<TaskItem> tasks() const;
    int currentBatch() const { return m_batch; }
    TaskSummary summary() const;                     // current batch
    TaskSummary earlierSummary() const;              // all earlier batches
    bool hasTasks() const;
    bool turnRunning() const;                        // any request in progress
    // Batch a request row belongs to in the panel: the current batch when the request or any of its
    // todos is in it, else the request's own batch.
    bool inCurrentBatch(const QString &requestId) const;

    // "Tasks 3/5" while work runs; "Tasks 5/5" or "Tasks 3/5 (1 failed, 1 deferred)" when settled.
    QString chipText() const;
    // "none" (no tasks), "running", "done" (all completed) or "attention" (settled with a suffix).
    QString chipState() const;
    QString chipToolTip() const;
    // End-of-turn line: "Tasks 3/5 (1 failed, 1 deferred) · T4 “…” failed, T5 “…” deferred".
    // Empty for a single completed task (nothing worth a line) or no tasks.
    QString turnEndLine(int maxChars = 160) const;
    static QString outcomeWord(TaskOutcome outcome);   // "failed", "unfinished", "in progress"…
    // Re-ask is refused by the worker while a request runs; queued state is not known here.
    static bool canReask(const LedgerRequest &request) { return request.status != QStringLiteral("in_progress"); }

    static QString statusGlyph(const QString &status);   // ✓ ◐ ○ ✕ ⏸ ✗
    static QString statusLabel(const QString &status);   // "cancelled by you", blocked → "failed"
    static QString todoGlyph(const QString &status);
    static QList<LedgerOpenItem> parseOpenItems(const QJsonArray &items);
    // "2 requests still open: R3 “fix the docs”, R4 “…” · 1 todo open" (empty when nothing is open).
    static QString openItemsLine(const QList<LedgerOpenItem> &items, int maxChars = 160);
    // "‖ Stopped at the step limit (50 model steps, limit 50) · unfinished tasks stay open" for done{stop_reason:limit}.
    static QString limitLine(const QJsonObject &done);
    // "✦ checking open items (1/2)"
    static QString completionCheckLine(const QJsonObject &event);
    // "may be unaddressed: R3 “…”" for request_audit (empty when nothing was flagged).
    static QString auditLine(const QJsonObject &event);

private:
    void changed() { if (onChanged) onChanged(); }
    void resetBatches();
    void updateBatches();
    QList<TaskItem> deriveTasks() const;             // outcomes, batch unset
    QList<LedgerRequest> m_requests;
    QList<LedgerTodo> m_todos;
    QHash<QString, LedgerTodo> m_retainedTodos;      // settled todos no longer in the list
    QHash<QString, int> m_counts;
    int m_total = 0, m_open = 0, m_openTodos = 0;
    // Batches: request and todo id → batch number (1-based); m_batch is the current one.
    QHash<QString, int> m_requestBatch, m_todoBatch;
    QHash<QString, QString> m_seenPreview;           // detects a different session's ledger
    int m_batch = 0, m_maxSeen = 0;
};

}  // namespace relay
