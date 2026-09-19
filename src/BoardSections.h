// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The section editor behind the gear at the end of the Switchboard's section checkboxes
// (owner, 2026-09-19: "put a gear after the list of switchboard sections, which allows you to
// add, remove, merge, or rename sections").
//
// All four verbs are one rewrite of `board.yaml` — `columns:`, `column_statuses:`,
// `column_titles:` — because **a section is a view of the statuses**: no card moves and no
// status changes, whichever of them you do. That is also why nothing here can lose a card. A
// status that the edit leaves uncollected is not hidden; `Model::sections()` gives it a section
// of its own, after the configured ones.
//
// `SectionPlan` is the whole rule set and holds no widgets, so it is tested on its own
// (tests/boardsections_test.cpp). `SectionEditor` is the page the pane shows: rows in, one
// `board_sections` message out.
#include "BoardModel.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace relay {
namespace board {

// The staged edit: what the gear has been told, before anything is written. Nothing here writes,
// and a plan that has not been touched produces no message at all.
class SectionPlan {
public:
    struct Row {
        QString id;              // the section id in board.yaml; never changed by a rename
        QString name;            // the board's own name, empty when Relay's wording is used
        QString fallbackName;    // Relay's wording, shown when `name` is empty
        QStringList statuses;    // what it collects; empty only for Verified
        bool configured = false; // listed in `columns:` — the only kind that can be removed
        bool fixed = false;      // Verified and Done: always there, always last
        bool added = false;      // invented in this plan, not yet in the file

        QString title() const { return name.isEmpty() ? fallbackName : name; }
    };

    // Read the board as it is now: the drawn sections for the rows, and the configured list for
    // the message, so a section that only exists because a card has that status is shown but is
    // never written into `columns:` by accident.
    static SectionPlan from(const Model &model);

    QList<Row> rows() const { return m_rows; }
    const Row *row(const QString &id) const;

    // A section that is drawn because a card carries that status cannot be removed — it would
    // come straight back — and the two that are always last cannot be removed or merged.
    bool canRemove(const QString &id) const;
    bool canMerge(const QString &id) const;
    // Why not, in one clause, for the disabled button's tooltip. Empty when it is allowed.
    QString whyNotRemove(const QString &id) const;
    QString whyNotMerge(const QString &id) const;
    // The sections `id` may be merged into, in the order they are drawn.
    QStringList mergeTargets(const QString &id) const;
    // Statuses no section in this plan collects: what a new section may be given, and what
    // removing a section frees up.
    QStringList freeStatuses() const;

    void rename(const QString &id, const QString &name);
    void remove(const QString &id);
    void merge(const QString &from, const QString &into);
    // The id is derived from the name; returns it, or an empty string when the name or the
    // statuses do not make a section (the caller shows `addRefusal` instead).
    QString add(const QString &name, const QStringList &statuses);
    QString addRefusal(const QString &name, const QStringList &statuses) const;

    bool dirty() const { return m_dirty; }
    // One line for the footer: what this plan does, in the order it was done.
    QString summary() const;
    // The `board_sections` message: the ordered `columns`, the statuses that differ from what a
    // section collects by default, and the names the board gives its sections.
    QJsonObject message() const;

    // The id a name would get, so the caller can warn about a clash before it happens.
    static QString idFor(const QString &name);

private:
    int indexOf(const QString &id) const;

    QList<Row> m_rows;
    QStringList m_columns;              // `columns:`, in order: what the message writes
    QStringList m_allStatuses;          // every status a section may collect
    QStringList m_changes;              // what to tell the owner, in the order it happened
    bool m_dirty = false;
};

// The page the gear opens, in the pane rather than over it: one row per section, a row to add
// one, and Save/Cancel. It sends nothing itself — `onSave` hands the message to the pane, which
// owns the connection to the worker.
class SectionEditor : public QWidget {
public:
    explicit SectionEditor(QWidget *parent = nullptr);

    // Draw the board as it is now. Any staged edit is dropped: the gear opens on the truth.
    void setModel(const Model &model);
    // Re-read after a write landed, keeping the page open so several edits can be made in a row.
    void refresh(const Model &model);

    const SectionPlan &plan() const { return m_plan; }

    std::function<void(const QJsonObject &)> onSave;
    std::function<void()> onClose;

private:
    void rebuild();
    void updateFooter();

    SectionPlan m_plan;
    QStringList m_pendingStatuses;      // ticked for the section being added
    QString m_pendingName;
    QVBoxLayout *m_rowsLayout = nullptr;
    QWidget *m_rowsHost = nullptr;
    QLabel *m_summary = nullptr;
    QPushButton *m_save = nullptr;
};

}  // namespace board
}  // namespace relay
