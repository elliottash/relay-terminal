// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The expanded request list opened from a pane's "Requests" chip, /requests or the palette.
// Keyboard-first: ↑/↓ select, Enter or Space (or →/←) expands a request to its todos, reasons and
// audit flags, d marks it done, x or Delete cancels it, o reopens it, r re-asks it (queues the
// verbatim text again), Esc closes. The selected request's verbatim text shows below the list.
#include "RequestLedger.h"
#include <QSet>
#include <QWidget>
#include <functional>

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;

namespace relay {

class RequestsPanel final : public QWidget {
    Q_OBJECT
public:
    explicit RequestsPanel(RequestLedgerModel *model, QWidget *parent = nullptr);

    std::function<void(const QString &ledgerId, const QString &status)> onSetStatus;   // request_set
    std::function<void(const QString &ledgerId)> onReask;                              // request_reask
    std::function<void(const QString &ledgerId)> onFetch;                              // request_get
    std::function<void()> onClose;

    // Rebuild from the model, keeping the selection and expanded rows.
    void refresh();
    // Focus the list with the first open request selected (else the newest).
    void enter();
    QString selectedId() const;
    void select(const QString &ledgerId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void act(const QString &action);
    void updateDetail();
    void updateButtons();
    RequestLedgerModel *m_model;
    QLabel *m_title = nullptr, *m_detail = nullptr, *m_keys = nullptr;
    QTreeWidget *m_tree = nullptr;
    QToolButton *m_done = nullptr, *m_cancel = nullptr, *m_reopen = nullptr, *m_reask = nullptr;
    QSet<QString> m_fetched;
    bool m_rebuilding = false;
};

}  // namespace relay
