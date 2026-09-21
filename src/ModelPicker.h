// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The model dialog (Ctrl+Alt+M): pick a model *and* prioritize the lists (card #MDL1, design 5.2).
//
// It began as Warp's model picker — a filter line over one list, a sort menu, and the reasoning
// level as a second pick beside the models (owner, 2026-09-20: "split the model picker into model
// and effort"). On 2026-09-21 the owner gave it the other half of the job: "the model priority
// chooser is crtical, and currently its too hard to find -- model options, then scroll down. i
// think we should beef up the ctrl alt m dialogue to be the main way to select / prioritize
// models." So the dialog is now tabbed:
//
//   high · main · flash · lite · local        each tab *is* that tier list, numbered, in order
//   all                                        the old flat picker: favorites, recents, the sort
//
// A tier tab is the list itself, so editing it here is editing it on Options › Models: both write
// `models/tier/<tier>` through `curation::setTierList`, which writes through to the current
// profile. There is no second copy and no "apply" — every edit is live, `onListsChanged` tells the
// window (the page redraws, running workers are re-sent their tiers), and Ctrl+Z takes it back.
//
// One row per model (rule 2): in a tier tab a row is one list *entry*, because the order between
// two providers of one model is exactly what a tier list expresses — the model's name is printed
// once and the provider goes in the "via" column. Folding happens where the order is not the
// point: the `all` tab and the "not in this list" section run through `models::grouped`, and →
// opens the group's providers beside the levels so a specific one can be chosen.
//
// The keys, which the footer line spells out for the tab you are on:
//
//   ←/→            change tab (in the filter: when it is empty, or the caret is at that end)
//   ctrl+tab       change tab, wherever the focus is; ctrl+shift+tab goes back
//   ↑/↓            move through the rows, from the filter too
//   enter          use this row in the pane — the model and the level together
//   →  on a row    the providers of this row, then the levels; ← comes back
//   alt+↑ / alt+↓  move the row up or down the list (a drag does the same)
//   delete         take the row out of the list (backspace does it while the filter is empty)
//   ctrl+enter     add the highlighted model to this list, at the end
//   ctrl+z         undo a list edit made in this dialog
//
// The dialog reads a relay::models::Catalog and QSettings and returns a key and a level; it never
// talks to the worker. The pane does the switch (Pane::selectEntry) so that every door — the box,
// /model <name>, this dialog — takes the same path.
#include "ModelCatalog.h"

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

class QComboBox;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabBar;
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
        // The tab it opens on: the tier the pane is running in (`modelrows::roleTier` of its
        // agent role), so Ctrl+Alt+M from a /flash pane lands on the flash list. "all" is the
        // flat tab. An id no tab has falls back to main.
        QString tier = QStringLiteral("main");
        qint64 now = 0;          // unix seconds, for the limits line
    };

    explicit ModelPicker(const Context &context, QWidget *parent = nullptr);

    // "customize…": the Models page, where providers and keys live.
    std::function<void()> openModelsPage;
    // A list edit landed. The dialog has already written it through `curation::setTierList`; this
    // is what carries it to the rest of the app — the same exit Options › Models takes
    // (`RelayWindow::modelsCurated`, which a pane reaches through `onProfileApplied`): the page
    // redraws, every pane re-reads the lists and every running worker is re-sent its tiers.
    std::function<void()> onListsChanged;

    ModelPick pick() const { return m_pick; }
    void rebuild();

    // For tests and for the pane's own tests: the controls by name.
    QLineEdit *filter() const { return m_filter; }
    QTreeWidget *list() const { return m_list; }
    QComboBox *sortBox() const { return m_sort; }
    QTabBar *tabBar() const { return m_tabs; }
    QComboBox *profileBox() const { return m_profile; }
    QListWidget *viaList() const { return m_vias; }
    QListWidget *levelList() const { return m_levels; }
    QLabel *footer() const { return m_footer; }
    QString tier() const { return m_tier; }
    void setTier(const QString &tier);
    QStringList tabIds() const;
    QString selectedKey() const;
    QString selectedEffort() const;   // the level list's pick; empty = the model's own default
    void selectKey(const QString &key);
    void accept() override;

    // The list edits, as the keys above do them. Public because they are the dialog's second job
    // and a test presses them without a window manager; each one writes through
    // `curation::setTierList` and calls `onListsChanged`.
    void moveSelected(int delta);   // alt+↑ / alt+↓, and the end of a drag
    void removeSelected();          // delete
    void addSelected();             // ctrl+enter, and the "+ add" cell
    void undo();                    // ctrl+z
    void commitDragOrder();         // a drag finished: the list becomes what the rows now read
    int undoDepth() const { return m_undo.size(); }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct UndoStep {
        QString tier;
        QList<models::curation::TierEntry> list;
    };

    void addSection(const QString &title);
    QTreeWidgetItem *addListRow(int rank, const models::curation::TierEntry &item, const models::Entry *entry);
    QTreeWidgetItem *addGroupRow(const models::Group &group, bool addable);
    void buildTier(const QString &query);
    void buildAll(const QString &query);
    void onRowChanged();
    void onViaChanged();
    void onLevelChanged();
    void updateFooter();
    void selectFirstRow();
    void syncTabBar();
    void stepTab(int delta);
    void pushUndo(const QString &tier);
    void changed();
    qint64 nowSeconds() const;
    QString effectiveEffort(const models::Entry &entry) const;
    bool handleShortcut(QKeyEvent *event);
    QTreeWidgetItem *currentRow() const;

    Context m_context;
    ModelPick m_pick;
    QString m_tier;
    QTabBar *m_tabs = nullptr;
    QComboBox *m_profile = nullptr;
    QLabel *m_profileLabel = nullptr;
    QLineEdit *m_filter = nullptr;
    QLabel *m_sortLabel = nullptr;
    QComboBox *m_sort = nullptr;
    QTreeWidget *m_list = nullptr;
    QLabel *m_viaLabel = nullptr;
    QListWidget *m_vias = nullptr;     // the providers of a folded row, when it has more than one
    QListWidget *m_levels = nullptr;   // the reasoning level, a separate pick beside the model
    QLabel *m_limits = nullptr;
    QLabel *m_footer = nullptr;
    QPushButton *m_favorite = nullptr;
    QPushButton *m_use = nullptr;
    QList<UndoStep> m_undo;
    // The provider chosen by hand for a folded row, by group name, so it survives a rebuild.
    QHash<QString, QString> m_viaChoice;
    bool m_filling = false;   // the right-hand lists are being populated: their signals are not picks
};

// Show the picker modally; the result says whether a row was used. `onListsChanged` is optional:
// without it a list edit is still written, it just reaches nobody until the next read.
ModelPick pickModel(QWidget *parent, const ModelPicker::Context &context, std::function<void()> openModelsPage,
                    std::function<void()> onListsChanged = {});

}  // namespace relay
