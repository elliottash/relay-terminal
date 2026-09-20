// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The section editor behind the gear at the end of the Switchboard's section checkboxes
// (owner, 2026-09-19: "put a gear after the list of switchboard sections, which allows you to
// add, remove, merge, or rename sections"; then 2026-09-19: "we do need sorting of sections
// though. enable those to be dragged and dropped, with up and down buttons for moving them, in
// the section settings modal").
//
// All five verbs — add, remove, merge, rename, move — are one rewrite of `board.yaml` —
// `columns:`, `column_statuses:`, `column_titles:` — because **a section is a view of the
// statuses**: no card moves and no status changes, whichever of them you do. That is also why
// nothing here can lose a card. A status that the edit leaves uncollected is not hidden;
// `Model::sections()` gives it a section of its own, after the configured ones.
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

class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QKeyEvent;
class QLabel;
class QMouseEvent;
class QPaintEvent;
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

    // ---- the order the sections are drawn in (owner, 2026-09-19) ---------------------------
    //
    // The list draws the sections in this order, and `columns:` is written in it, so moving a
    // section is a rewrite of the section list and never of a card. A section the board does not
    // configure — one that is only there because cards carry that status — moves like any other
    // and is simply not written into `columns:`. Verified and Done are always the last two:
    // nothing moves past them and they do not move themselves.
    // `delta` is -1 up or +1 down; `canMove` and `whyNotMove` answer for the row's buttons.
    bool canMove(const QString &id, int delta) const;
    QString whyNotMove(const QString &id, int delta) const;
    void move(const QString &id, int delta);
    // A drag and drop: `id` goes in front of `beforeId`, or to the end of the movable part of the
    // list when `beforeId` is empty. True when the order actually changed, so a drop that would
    // write the same file does not mark the plan dirty.
    bool moveBefore(const QString &id, const QString &beforeId);
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
    // `columns:` in the order the list draws: the configured sections in their drawn order, then
    // any configured section that has no row of its own (a `done` the list keeps as its last
    // section whatever the file says) where it was.
    void syncColumns();

    QList<Row> m_rows;
    QStringList m_columns;              // `columns:`, in order: what the message writes
    QStringList m_allStatuses;          // every status a section may collect
    QStringList m_changes;              // what to tell the owner, in the order it happened
    bool m_dirty = false;
};

// One row of the section list (owner, 2026-09-19: "enable those to be dragged and dropped, with up
// and down buttons for moving them, in the section settings modal"). The row is the drag source —
// the handle at its left and the row's own background start the drag, while the name field and the
// buttons keep their own presses — and it accepts a section dropped above or below it, drawing the
// line where it would land. Qt delivers a drop only through its own drag machinery, so the drop
// itself is `dropHere`, which is what the event calls and what a test can call.
class SectionRow : public QWidget {
public:
    explicit SectionRow(const QString &id, QWidget *parent = nullptr);

    // The section `id` was dropped on this row, above or below it: the editor turns that into
    // SectionPlan::moveBefore. Verified and Done are not draggable (nothing moves past them).
    std::function<void(const QString &id, bool above)> onDrop;
    bool draggable = true;

    // A section dropped at `y` in this row: the top half means above it, the bottom half below.
    void dropHere(const QString &id, int y);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    // The line the section would land on, drawn over this row's own edge.
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_id;
    QPoint m_press;
    bool m_hover = false, m_above = false;
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

    // The board's folder, and the one action that moves it (protocol 19.17, card #916B): "Hide this
    // board's folder" on a `switchboard/` board, "Show this board's folder" on a `.switchboard/`
    // one. `setFolder` takes the folder's name as the worker reported it; an `issues/` board — the
    // original spelling, which whole repositories name in their own scripts and hooks — gets no
    // button, because the worker would refuse the move anyway. `onFolder(hidden)` is the click.
    void setFolder(const QString &folderName);
    QString folder() const { return m_folder; }
    std::function<void(bool hidden)> onFolder;

protected:
    // Esc leaves the sections as they are, which is what Cancel's tooltip promises and what Esc
    // does everywhere else in this pane.
    void keyPressEvent(QKeyEvent *event) override;

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
    QString m_folder;
    QWidget *m_folderRow = nullptr;
    QLabel *m_folderLabel = nullptr;
    QPushButton *m_folderButton = nullptr;
};

}  // namespace board
}  // namespace relay
