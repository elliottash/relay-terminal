// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// relay::ActionPalette — the modal command palette (card #MAGP; owner, 2026-09-22: "a magic
// action modal with text search and some buttons that show dropdowns that give you quick
// intuitive access to everything").
//
// A frameless overlay parented to a top-level window, centred horizontally near the top, about
// 640 px wide and never taller than 60% of the window. Top to bottom:
//  1. a search box — typing filters every item (and every submenu's children, as "Parent › Child")
//     across label, detail, aliases, section, shortcut and the item's `/slash` spelling;
//  2. a row of group buttons, one per distinct ActionItem::section in catalog order, each dropping
//     a QMenu of that section's items with their shortcut on the right;
//  3. the result list. With an empty query: Recent, For this pane, then every section in turn.
//
// Keys: Up/Down move; Enter runs the highlighted row and closes; Ctrl+Enter runs it and stays
// open; Tab moves from the search box into the button row, Left/Right walk it, Enter/Down/Space
// drop its menu, Esc goes back to the search box. Esc in the search box leaves a submenu first,
// then closes. A click anywhere else in the window closes it, and so does `toggle()` — the
// opening chord. An item with `children` is a submenu: choosing it lists its children in place.
// An item with `stayOpen` behaves as Ctrl+Enter always.
//
// It knows nothing about the window: items come in through the callbacks on every open (so
// shortcuts and contextual rows are read live), and a run is reported back through `chosen`, which
// is where the caller records its recent list. Opening again after any kind of close starts
// clean, every time (#XAME was a menu that would not reopen).
#include <QFrame>
#include <QKeySequence>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

#include "SettingsPane.h"   // relay::ActionItem

class QAbstractButton;
class QLineEdit;
class QListWidget;
class QMenu;
class QStyledItemDelegate;
class QToolButton;
class QVBoxLayout;

namespace relay {

class ActionPalette final : public QFrame {
public:
    // `window` is the top-level the palette floats over and closes on a click in; it is also the
    // palette's parent, so the palette dies with it. `recentKeys` is newest first. `chosen` is
    // called with an item's key after its `run` — not for an item that only opens a submenu.
    ActionPalette(QWidget *window, std::function<QList<ActionItem>()> catalog,
                  std::function<QList<ActionItem>()> forThisPane, std::function<QStringList()> recentKeys,
                  std::function<void(const QString &key)> chosen);
    ~ActionPalette() override;

    void open();
    void close();
    void toggle();
    bool isOpen() const;

    // The chords that opened it. Pressed inside the palette they close it, for a window whose own
    // dispatcher does not see keys while the search box has focus. Empty by default.
    void setToggleKeys(const QList<QKeySequence> &keys);

    // Parts, for tests and for a caller that wants to drive or inspect them.
    QLineEdit *searchBox() const { return m_search; }
    QListWidget *list() const { return m_list; }
    QList<QAbstractButton *> groupButtons() const;
    // The dropdown group button `index` shows, rebuilt from the catalog so its shortcuts are
    // current; owned by the palette and replaced on the next call.
    QMenu *groupMenu(int index);
    // The key of the highlighted row, or empty when the highlight is on no action.
    QString currentKey() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    struct Row {
        ActionItem item;
        QString header;   // non-empty: a section label, not a choice
        QString tag;      // shown after the label in the detail's ink (the section, in search results)
    };

    void rebuild();
    void rebuildButtons();
    void layoutButtons(int width);
    void reposition();
    void applyPalette();
    void moveCurrent(int steps);
    void setCurrentRow(int row);
    void activateCurrent(bool stayOpen);
    void runItem(const ActionItem &item, bool stayOpen);
    void enterSubmenu(const ActionItem &item);
    bool leaveSubmenu();
    void popupGroup(int index);
    void focusButton(int index);
    int focusedButton() const;
    bool isToggleKey(const QKeyEvent *event) const;
    QList<ActionItem> flatCatalog();
    void populateMenu(QMenu *menu, const QList<ActionItem> &items);

    QPointer<QWidget> m_window;
    std::function<QList<ActionItem>()> m_catalog;
    std::function<QList<ActionItem>()> m_forThisPane;
    std::function<QStringList()> m_recentKeys;
    std::function<void(const QString &)> m_chosen;

    QLineEdit *m_search = nullptr;
    QWidget *m_buttonRow = nullptr;
    QVBoxLayout *m_buttonLines = nullptr;   // one QHBoxLayout per line; the row wraps
    int m_buttonWidth = -1;                   // the width the lines were laid out for
    QList<QToolButton *> m_buttons;
    QStringList m_sections;
    QListWidget *m_list = nullptr;
    QStyledItemDelegate *m_delegate = nullptr;   // the list's row painter; it holds the window's colours
    QPointer<QMenu> m_menu;
    QList<QKeySequence> m_toggleKeys;

    QList<ActionItem> m_items;              // the catalog as read on open
    std::optional<QList<ActionItem>> m_flat;   // m_items with children flattened, read on first need
    QList<Row> m_rows;
    std::optional<ActionItem> m_submenu;    // the item whose children are listed, if any
    QPointer<QWidget> m_returnFocus;
    int m_lastButton = 0;
    bool m_open = false;
};

} // namespace relay
