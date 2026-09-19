// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The task list opened from a pane's "Tasks" chip, /tasks (/requests, /todos), Ctrl+Shift+K or the
// palette. It shows the model's todos for the current task list, with a folded "Earlier" row for
// the lists before it. The request ledger behind the model is internal and is never listed here.
// Keyboard-first: ↑/↓ select, Enter or Space (or →/←) folds the Earlier group, Esc closes.
// The selected task's full text, status and note show below the list.
// Tasks and subagents (card #QHR1): a row a subagent has shows "✦ a2"; Enter (or a double click) opens
// that subagent's tab. S (or "Run as subagent" in the row's menu) hands a todo (not completed or cancelled) to a new
// background subagent.
#include "RequestLedger.h"
#include <QHash>
#include <QWidget>
#include <functional>

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

namespace relay {

class RequestsPanel final : public QWidget {
    Q_OBJECT
public:
    explicit RequestsPanel(RequestLedgerModel *model, QWidget *parent = nullptr);

    std::function<void()> onClose;
    // Open the subagent working on a todo (its tab in the subagent pane). `mouse`: reached with the mouse.
    std::function<void(const QString &subagentId, bool mouse)> onOpenSubagent;
    // Hand a todo (not completed or cancelled) to a new background subagent (the worker's todo_subagent command).
    std::function<void(const QString &todoId, bool mouse)> onRunAsSubagent;

    // Rebuild from the model, keeping the selection and expanded rows.
    void refresh();
    // Focus the list with the first task that is not done selected (else the last one).
    void enter();
    QString selectedId() const;          // todo id, e.g. "T3"
    void select(const QString &todoId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateDetail();
    const LedgerTodo *todoFor(const QString &todoId) const;
    void showRowMenu(const QPoint &pos);
    QList<LedgerTodo> m_todos;          // allTodos() as of the last refresh
    QTreeWidgetItem *rowFor(const QString &todoId) const;
    RequestLedgerModel *m_model;
    QLabel *m_title = nullptr, *m_detail = nullptr, *m_keys = nullptr;
    QTreeWidget *m_tree = nullptr;
    QHash<QString, bool> m_expanded;   // "#earlier" → expanded
    bool m_rebuilding = false;
};

}  // namespace relay
