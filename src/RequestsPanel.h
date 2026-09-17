// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The task list opened from a pane's "Tasks" chip, /tasks (/requests, /todos), Ctrl+Shift+K or the
// palette. It shows the model's todos for the current task list, with a folded "Earlier" row for
// the lists before it. The request ledger behind the model is internal and is never listed here.
// Keyboard-first: ↑/↓ select, Enter or Space (or →/←) folds the Earlier group, Esc closes.
// The selected task's full text, status and note show below the list.
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
    QTreeWidgetItem *rowFor(const QString &todoId) const;
    RequestLedgerModel *m_model;
    QLabel *m_title = nullptr, *m_detail = nullptr, *m_keys = nullptr;
    QTreeWidget *m_tree = nullptr;
    QHash<QString, bool> m_expanded;   // "#earlier" → expanded
    bool m_rebuilding = false;
};

}  // namespace relay
