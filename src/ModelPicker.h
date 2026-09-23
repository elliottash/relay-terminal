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
//   priorities   one scrolling page of **sections**, one per class — high, main, flash, and local
//                where this machine serves one — each a header line (the class, its "show this
//                class in the box" switch and a one-line note) over that class's numbered rows.
//                A section *is* that tier list. There is no lite section: lite is never a pane
//                mode, its storage stays, and the jobs tab is where a chore's model is set
//                (owner, 2026-09-21: "in a pane, i dont want separate tabs for the modes. they
//                should just be in divided sections. remove the lite section").
//   all                                        every available model: favorites, then a section per
//                                              provider **alphabetically**, the sort menu, and
//                                              "+ add a model by id…" at the end. No "recent"
//                                              section (owner, 2026-09-21): this tab is a
//                                              checklist read one provider at a time, and a block
//                                              of lately-picked rows moves under you between looks
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
//   ↑/↓            move through the rows — **across** the sections, skipping their headers
//   enter          use this row in the pane — the model and the level together
//   tab            the focus along filter → rows → providers → levels
//   →  on a row    the providers of this row, then the levels; ← comes back
//   ▲▼ / alt+↑↓    move the row up or down inside its section
//   drag           reorder within a section — or, across a header, move the model into that
//                  section's list at the rank it is dropped at (card #RKP3); a drop while the
//                  filter hides rows is not an order, and the limits line says so
//   delete         take the row out of its section (backspace does it while the filter is empty)
//   ctrl+enter     add the highlighted model to the section it is in, at the end
//   typing         searches every usable model; each section shows its own matches, then the
//                  models it does not list under "not in <class>", then an open-ended provider's
//                  long tail (OpenRouter's live listing) under "more from <provider>"
//   ctrl+z         undo a list edit made here
//   the "in box" column is a cutoff: it says how far down this class the Alt+M box shows, and the
//                  tick on a section's own header is that class's "show it in the box" switch
//
// Every edit reads the **row's** class (`rowTier`), not a tab: on the sectioned page four lists are
// on screen at once, so "which list is this" is a property of the row under the highlight.
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
class QResizeEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabBar;
class QTreeWidget;
class QTreeWidgetItem;
class QGridLayout;
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
        // What it opens on. `classes` is the sectioned priorities page and `all` the flat tab; a
        // single class id puts the widget on that one list alone, which is what a caller with one
        // list to show wants. On the sectioned page the served pane's class is where the highlight
        // starts (`focusClass`), so Ctrl+Shift+M from a /flash pane lands in the flash section.
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

    // The catalog and the served pane's current row again, without throwing the tab, the filter,
    // the class list or the undo stack away. A pane's catalog arrives **late** — an install with
    // no providers has nothing to draw until its worker answers `presets`, and a key added on the
    // models pane's providers tab changes what the other two tabs may offer — and `rebuild()`
    // alone would redraw the rows from the catalog this widget was built with (card #MDL1 t:a11).
    void setCatalog(const models::Catalog &catalog, const QString &currentKey,
                    const QString &currentEffort, qint64 now);

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
    void setBoxCutoffFromRow(const QString &tier, int rank, bool on);
    // The tick on a section's own header: "show this class in the box" (design 5.3), the same
    // statement `classSwitch()` makes on a single-class page.
    void setClassShown(const QString &tier, bool on);
    // The sectioned priorities page, and the one class list a single-class caller gets. `tier()`
    // answers `classes` on the page; `currentClass()` is the class of the row under the highlight,
    // which is the sectioned page's answer to "which list am I in".
    static QString classesTier() { return QStringLiteral("classes"); }
    static QString effortTier() { return QStringLiteral("effort"); }
    bool sectionsPage() const;
    bool effortPage() const;
    // high · main · flash, and local where this machine serves one: the sections, in order. Never
    // lite (design 5.3: it is not a pane mode and the box has never had a row for it).
    QStringList sectionTiers() const;
    QString currentClass() const;
    // Put the highlight in this class's section — rank 1, or the pane's own model where that
    // section holds it. A no-op off the sectioned page and for a class with no section.
    void focusClass(const QString &tier);

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
    // Hosted in the models pane (card #BXMS) there is no button and Enter only highlights.
    void use();

    // The list edits, as the keys above do them. Public because they are the dialog's second job
    // and a test presses them without a window manager; each one writes through
    // `curation::setTierList` and calls `onListsChanged`.
    void moveSelected(int delta);   // alt+↑ / alt+↓, and what the ▲▼ buttons call through to
    // The one-rank move the ▲▼ buttons and `moveSelected` share (card #RKP3): key moves delta
    // ranks inside tier's list, clamped at the ends; persisted, undoable, selection follows.
    // "+ add a model by id…", the last row of the `all` tab: the input that sat under every
    // open-ended provider on Options › Models until the checklist left the page (card #MDL1
    // t:a10, design 5.5). Returns the key it added, or an empty string. Public because a test
    // adds one without putting the little dialog on screen.
    QString addModelById(const QString &preset, const QString &id);
    void removeSelected();          // delete
    void addSelected();             // ctrl+enter, and the "+ add" cell
    void undo();                    // ctrl+z
    // A drag finished: the lists become what the rows now read — a reorder inside a section, a
    // move across one, or, while a filter hides rows, nothing (and the limits line says why).
    void commitDragOrder();
    void moveKey(const QString &tier, const QString &key, int delta);
    int undoDepth() const { return m_undo.size(); }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct UndoStep {
        QString tier;
        QList<models::curation::TierEntry> list;
    };

    void populateTabs();
    QStringList tabBarIds() const;   // tabIds(), less the flat tab while hosted
    void addSection(const QString &title, const QString &tier = QString());
    // A class's own header line on the sectioned page: the class, its "in box" switch and a note.
    QTreeWidgetItem *addClassHeader(const QString &tier);
    QTreeWidgetItem *addListRow(const QString &tier, int rank, const models::curation::TierEntry &item,
                                const models::Entry *entry);
    QTreeWidgetItem *addGroupRow(const models::Group &group, bool addable, const QString &tier = QString());
    void buildSections(const QString &query);
    void buildTier(const QString &tier, const QString &query);
    void buildAll(const QString &query);
    // The class this row belongs to: a list entry's own class, or the class whose "not in …"
    // section an addable row was drawn under. Empty on the flat tab, where no list is in play.
    QString rowTier(const QTreeWidgetItem *row) const;
    QString editTier() const;        // rowTier(currentRow()) — which list an edit key acts on
    void stepRow(int delta);         // ↑/↓ over the rows, skipping the section headers
    void addAddByIdRow(const QString &preset = QString());
    void promptAddModelById(const QString &preset = QString());
    void onCheckChanged(QTreeWidgetItem *item, int column);
    void refreshBoxChecks();   // the ticks again from the stored cutoffs, without rebuilding the rows
    // The availability ticks and the greying again, in place: an un-tick changes no row's place in
    // the `all` tab (the row stays, to be ticked back), so rebuilding under the signal that
    // delivered the click would only delete the item mid-click.
    void refreshAvailability();
    void applyAvailability(QTreeWidgetItem *row, bool available, const QString &reason);
    bool availabilityTab() const;    // the `all` tab, the one place step 2 is edited
    bool boxClassTier(const QString &tier) const;   // one of the four classes the box can draw
    void syncClassSwitch();
    void onRowChanged();
    void onViaChanged();
    void onLevelChanged();
    void updateFooter();
    void updateCompactLayout();
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
    QGridLayout *m_listsLayout = nullptr;
    QWidget *m_sidePanel = nullptr;
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
    bool m_compact = false;
    // The class the sectioned page was last pointed at (`focusClass`). It is what `currentClass()`
    // answers while the highlight is on no row of a class — an empty section, or a page with no
    // list at all — so "which class is this page on" has an answer before anything is ranked.
    QString m_focusClass;
};

}  // namespace relay
