// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The models widget: pick a model *and* prioritize the lists (card #MDL1, design 5.2 and 5.8).
//
// It was a modal dialog on Ctrl+Alt+M until 2026-09-21. The owner retired both the modal and the
// key — "lets build the models pane … and just remove ctrl alt m, not worth the extra confusion"
// — so this is now a plain QWidget that `relay::ModelsPane` embeds as its **available** and
// **priorities** tabs (src/ModelsPane.h). Nothing here opens, closes or focuses anything: the host
// says which tab is in front (`setTier`), the host is told when a row is used (`onUse`) and when
// Escape is pressed (`onEscape`), and the host decides what either means.
//
// It began as Warp's model picker — a filter line over one list, a sort menu, and the reasoning
// level as a second pick beside the models (owner, 2026-09-20: "split the model picker into model
// and effort"). On 2026-09-21 the owner gave it the other half of the job: "the model priority
// chooser is crtical, and currently its too hard to find -- model options, then scroll down. i
// think we should beef up the ctrl alt m dialogue to be the main way to select / prioritize
// models." So the dialog is now tabbed:
//
//   high · main · flash · lite · local        each tab *is* that tier list, numbered, in order
//   all                                        every usable model: favorites, recents, the sort,
//                                              and "+ add a model by id…" at the end
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
//   tab            the focus along filter → rows → providers → levels (←/→ belong to the tabs)
//   →  on a row    the providers of this row, then the levels; ← comes back
//   alt+↑ / alt+↓  move the row up or down the list (a drag does the same)
//   delete         take the row out of the list (backspace does it while the filter is empty)
//   ctrl+enter     add the highlighted model to this list, at the end
//   typing         searches every usable model, this tab's list first; an open-ended provider's
//                  long tail (OpenRouter's live listing) comes under "more from <provider>"
//   ctrl+z         undo a list edit made in this dialog
//   the "in box" column is a cutoff: it says how far down this class the Alt+M box shows
//
// It reads a relay::models::Catalog and QSettings and hands back a key and a level; it never talks
// to the worker. The **served pane** does the switch (Pane::selectEntry) so that every door — the
// box, /model <name>, this widget — takes the same path.
#include "ModelCatalog.h"

#include <QWidget>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

class QCheckBox;
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

class ModelPicker final : public QWidget {
public:
    struct Context {
        models::Catalog catalog;
        QString currentKey;      // the pane's model now; drawn bold and selected first
        QString currentEffort;   // the pane's level now; the default for a row with no memory
        // The tab it opens on: the tier the served pane is running in (`modelrows::roleTier` of
        // its agent role), so Ctrl+Shift+M from a /flash pane lands on the flash list. "all" is
        // the flat tab. An id no tab has falls back to main.
        QString tier = QStringLiteral("main");
        // What the filter line starts with. Options › Models' per-provider "models… (N of M
        // available)" link opens the available tab with the provider's name typed, so step 2 is
        // one click from step 1 (card #MDL1, design 5.7). Empty — every other caller — and the
        // filter opens blank, exactly as before.
        QString filter;
        qint64 now = 0;          // unix seconds, for the limits line
        // "fill from defaults" (card #MDL1 t:a8, design 5.5): the two buttons Options › Models'
        // `models.tier.defaults` row presses, handed in rather than copied — the lists a worker
        // computed are the pane's, not this dialog's. `withOpenrouter` picks the second button;
        // false back means the worker has not sent defaults yet. Unset, and the buttons are not
        // drawn, which is what a caller with no worker behind it wants.
        std::function<bool(bool withOpenrouter)> fillFromDefaults;
    };

    explicit ModelPicker(const Context &context, QWidget *parent = nullptr);

    // "providers…": where providers and keys live — the models pane's first tab.
    std::function<void()> openModelsPage;
    // A row was used: the model and the level together, for the pane this widget serves. The
    // widget itself switches nothing (design 5.8) — `Pane::selectEntry` is the one door.
    std::function<void(const ModelPick &)> onUse;
    // Escape. The host puts the focus back on the pane it serves and leaves itself open (design
    // 5.8); with no handler Escape does nothing here.
    std::function<void()> onEscape;
    // A list edit landed. The dialog has already written it through `curation::setTierList`; this
    // is what carries it to the rest of the app — the same exit Options › Models takes
    // (`RelayWindow::modelsCurated`, which a pane reaches through `onProfileApplied`): the page
    // redraws, every pane re-reads the lists and every running worker is re-sent its tiers.
    std::function<void()> onListsChanged;

    ModelPick pick() const { return m_pick; }
    void rebuild();

    // Hosted in `relay::ModelsPane`: the flat tab is the host's own **available** tab, so it
    // leaves this widget's tab row — which then holds the five classes and nothing else, and
    // hides itself altogether while the flat tab is in front. `setTier("all")` still reaches it;
    // it is the host that says so.
    void setHosted(bool hosted);
    bool hosted() const { return m_hosted; }

    // For tests and for the pane's own tests: the controls by name.
    QLineEdit *filter() const { return m_filter; }
    QTreeWidget *list() const { return m_list; }
    QComboBox *sortBox() const { return m_sort; }
    QTabBar *tabBar() const { return m_tabs; }
    QComboBox *profileBox() const { return m_profile; }
    QListWidget *viaList() const { return m_vias; }
    QListWidget *levelList() const { return m_levels; }
    QLabel *footer() const { return m_footer; }
    QCheckBox *classSwitch() const { return m_boxSwitch; }
    QPushButton *defaultsButton(bool withOpenrouter) const { return withOpenrouter ? m_defaultsOpenrouter : m_defaults; }
    // The "show in box" column of a tier tab, as a cutoff (owner, 2026-09-21, design 5.3):
    // checking rank n shows ranks 1..n of this class in the Alt+M box, unchecking n hides n and
    // everything under it. Unchecking rank 1 switches the class off altogether, which is the same
    // statement. Public because a test presses it without a window manager.
    void setBoxCutoffFromRow(int rank, bool on);
    // The `all` tab's availability column — step 2 of the owner's four (card #MDL1, design 5.7):
    // whether this model exists for the lists, the box and the box's typed filter at all. It
    // covers every provider of the row, because a row is one model (rule 2): un-ticking sonnet
    // un-ticks sonnet, not "sonnet through Claude Code". Public because a test presses it.
    void setRowAvailable(const QString &groupKey, bool on);
    QString tier() const { return m_tier; }
    void setTier(const QString &tier);
    QStringList tabIds() const;
    QString selectedKey() const;
    QString selectedEffort() const;   // the level list's pick; empty = the model's own default
    void selectKey(const QString &key);
    // Enter, a double click or the "use" button: the highlighted row and its level go to `onUse`.
    void use();

    // The list edits, as the keys above do them. Public because they are the dialog's second job
    // and a test presses them without a window manager; each one writes through
    // `curation::setTierList` and calls `onListsChanged`.
    void moveSelected(int delta);   // alt+↑ / alt+↓, and the end of a drag
    // "+ add a model by id…", the last row of the `all` tab: the input that sat under every
    // open-ended provider on Options › Models until the checklist left the page (card #MDL1
    // t:a10, design 5.5). Returns the key it added, or an empty string. Public because a test
    // adds one without putting the little dialog on screen.
    QString addModelById(const QString &preset, const QString &id);
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

    void populateTabs();
    QStringList tabBarIds() const;   // tabIds(), less the flat tab while hosted
    void addSection(const QString &title);
    QTreeWidgetItem *addListRow(int rank, const models::curation::TierEntry &item, const models::Entry *entry);
    QTreeWidgetItem *addGroupRow(const models::Group &group, bool addable);
    void buildTier(const QString &query);
    void buildAll(const QString &query);
    void addAddByIdRow();
    void promptAddModelById();
    void onCheckChanged(QTreeWidgetItem *item, int column);
    void refreshBoxChecks();   // the ticks again from the stored cutoff, without rebuilding the rows
    // The availability ticks and the greying again, in place: an un-tick changes no row's place in
    // the `all` tab (the row stays, to be ticked back), so rebuilding under the signal that
    // delivered the click would only delete the item mid-click.
    void refreshAvailability();
    void applyAvailability(QTreeWidgetItem *row, bool available, const QString &reason);
    bool availabilityTab() const;    // the `all` tab, the one place step 2 is edited
    bool boxClassTab() const;        // this tab is one of the four classes the box can draw
    void syncClassSwitch();
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
    QCheckBox *m_boxSwitch = nullptr;          // "show this class in the box"
    QPushButton *m_customize = nullptr;        // "providers…"
    QPushButton *m_defaults = nullptr;         // "fill from defaults"
    QPushButton *m_defaultsOpenrouter = nullptr;
    QList<UndoStep> m_undo;
    // The provider chosen by hand for a folded row, by group name, so it survives a rebuild.
    QHash<QString, QString> m_viaChoice;
    bool m_filling = false;   // the right-hand lists are being populated: their signals are not picks
    bool m_building = false;  // the rows are being built: an itemChanged is ours, not a click
    bool m_hosted = false;    // embedded in the models pane, which owns the flat tab
};

}  // namespace relay
