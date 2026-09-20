// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The model picker (owner, 2026-09-20): Ctrl+Shift+M, /model, and the "more models…" row of the
// pane's model box. Warp's shape: a filter over one list, the reasoning level chosen separately
// from the model, and a sort menu. opencode's additions: favorites and the ten most recent picks
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
class QButtonGroup;
class QHBoxLayout;
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
    QButtonGroup *effortGroup() const { return m_efforts; }
    QString selectedKey() const;
    QString selectedEffort() const;
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
    QLabel *m_effortLabel = nullptr;
    QHBoxLayout *m_effortRow = nullptr;
    QButtonGroup *m_efforts = nullptr;
    QLabel *m_limits = nullptr;
    QPushButton *m_favorite = nullptr;
    QPushButton *m_use = nullptr;
};

// Show the picker modally; the result says whether a row was used.
ModelPick pickModel(QWidget *parent, const ModelPicker::Context &context, std::function<void()> openModelsPage);

}  // namespace relay
