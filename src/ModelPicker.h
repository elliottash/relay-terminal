// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The model picker (owner, 2026-09-20): Ctrl+Alt+M, /model, and the "more models…" row of the
// pane's model box. Warp's shape: a filter over one list and a sort menu. The reasoning level is
// a second, separate pick (owner, 2026-09-20: "split the model picker into model and effort"): a
// short list beside the models, preset to the level the tier lists give the highlighted model,
// else the pane's own. → moves into it, ← back; Enter uses both. opencode's additions: favorites and the ten most recent picks
// above the rest, sections that give way to one flat list the moment you type, and the reasoning
// level remembered per model.
//
// The dialog reads a relay::models::Catalog and QSettings and returns a key and a level; it never
// talks to the worker. The pane does the switch (Pane::selectEntry) so that every door — the box,
// /model <name>, this dialog — takes the same path.
#include "ModelCatalog.h"

#include <QDialog>
#include <QString>
#include <QStringList>
#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QListWidget;
class QEvent;

namespace relay {

struct ModelPick {
    bool accepted = false;
    QString key;      // "<preset>|<model>"
    QString effort;   // the level chosen for it; empty when the model has no knob
};

class ModelPicker final : public QDialog {
public:
    struct Context {
        models::Catalog catalog;
        QString currentKey;      // the pane's model now; drawn bold and selected first
        QString currentEffort;   // the pane's level now; the default for a row with no memory
        qint64 now = 0;          // unix seconds, for the limits line
    };

    explicit ModelPicker(const Context &context, QWidget *parent = nullptr);

    // "customize…": the Models page, where providers, the checklist and the order live.
    std::function<void()> openModelsPage;

    ModelPick pick() const { return m_pick; }
    void rebuild();

    // For tests and for the pane's own tests: the controls by name.
    QLineEdit *filter() const { return m_filter; }
    QTreeWidget *list() const { return m_list; }
    QComboBox *sortBox() const { return m_sort; }
    QString selectedKey() const;
    QListWidget *levelList() const { return m_levels; }
    QString selectedEffort() const;   // the level list's pick; empty when the model has no knob
    void selectKey(const QString &key);
    void accept() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void addSection(const QString &title);
    QTreeWidgetItem *addRow(const models::Entry &entry);
    void onRowChanged();

    Context m_context;
    ModelPick m_pick;
    QLineEdit *m_filter = nullptr;
    QComboBox *m_sort = nullptr;
    QTreeWidget *m_list = nullptr;
    QListWidget *m_levels = nullptr;   // the reasoning level, a separate pick beside the model (owner, 2026-09-20)
    QLabel *m_limits = nullptr;
    QPushButton *m_favorite = nullptr;
    QPushButton *m_use = nullptr;
};

// Show the picker modally; the result says whether a row was used.
ModelPick pickModel(QWidget *parent, const ModelPicker::Context &context, std::function<void()> openModelsPage);

}  // namespace relay
