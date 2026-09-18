// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The "Recently closed" list: the last 25 closed panes, tabs and windows (src/ClosedStack.h),
// newest first, with a filter. It is the session manager pane's second tab (card #R6J0 mounts it
// through RelayWindow::addSessionsTab), and it knows nothing about windows: it is fed records with
// setRecords() and asks through onReopen / onDiscard / onClear, so it is built and tested alone.
//
// One row per closed item: what it was, where, and how long ago. → (or the arrow) unfolds it into
// what was inside — a window's tabs, a tab's panes — and under each terminal pane the last lines
// of the text it had when it closed, read from the scrollback store only when the row is opened.
// Enter or a double click reopens the item; Delete drops it from the list.
#include "ClosedStack.h"

#include <QList>
#include <QString>
#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace relay {
namespace closed {

// How many lines of a pane's saved text the unfolded row shows.
constexpr int kPreviewLines = 8;

// The tail of a pane's saved terminal text that is worth showing: trailing blank lines dropped,
// blank runs squeezed to one, the last `maxLines` kept, each cut to `maxChars`. Pure.
QStringList previewTail(const QStringList &lines, int maxLines = kPreviewLines, int maxChars = 160);

class ListView : public QWidget {
public:
    explicit ListView(QWidget *parent = nullptr);

    // Oldest first, as the manager keeps them; shown newest first. Keeps the selection and what
    // was unfolded, by record id.
    void setRecords(const QList<Record> &records);
    void setFilter(const QString &text);
    // The rows when there are any (↓ ↑ → Enter work at once; typing starts the filter), else the filter.
    void focusInput();

    // For tests and for whoever hosts the list.
    int visibleCount() const;
    QString selectedId() const;
    QTreeWidget *tree() const { return m_tree; }

    std::function<void(const QString &id)> onReopen;
    std::function<void(const QString &id)> onDiscard;
    std::function<void()> onClear;
    // Reads a pane's saved text; windowstate::readScrollback unless a test replaces it.
    std::function<QStringList(const QString &scrollbackId)> readText;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void rebuild();
    void refreshAges();
    void unfold(QTreeWidgetItem *record);
    void fillPreview(QTreeWidgetItem *paneRow);
    void reopenCurrent();
    void discardCurrent();
    QTreeWidgetItem *topOf(QTreeWidgetItem *item) const;

    QList<Record> m_records;
    QString m_filter;
    QLineEdit *m_search = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_empty = nullptr;
    QLabel *m_hint = nullptr;
    QPushButton *m_reopen = nullptr, *m_clear = nullptr;
    QTimer *m_ageTimer = nullptr;
};

}  // namespace closed
}  // namespace relay
