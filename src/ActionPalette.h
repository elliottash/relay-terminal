// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// relay::ActionPalette — the modal command palette (card #MAGP; owner, 2026-09-22: "a magic
// action modal with text search and some buttons that show dropdowns that give you quick
// intuitive access to everything").
//
// A frameless overlay parented to a top-level window, centred horizontally near the top, about
// 800 px wide and never taller than 60% of the window. The search box sits at the centre of
// three fixed group arms (Agent/Models/Sessions in a horizontal row above, Panes/Files/Board left,
// Terminal/Remote/Options right), with results below. Each group drops a QMenu. Catalog sections
// that are not yet assigned to an arm remain reachable under Options; narrow windows stack the
// arms as rows. Typing searches every item, including submenu children and `/slash` spellings.
//
// Keys: Down enters results; Up enters Models, and Left/Right traverse the top row. From search,
// Left/Right enter the corresponding side arm only at a text edge. Repeating a side-arm direction
// moves outward. Enter runs a result or opens a group;
// Ctrl+Enter runs a result and stays open. Tab walks groups, and typing from a group returns to
// search. Esc on a group goes home. Esc in search leaves a submenu first,
// then closes. A click anywhere else in the window closes it, and so does `toggle()` — the
// opening chord. An item with `children` is a submenu: choosing it lists its children in place.
// An item with `stayOpen` behaves as Ctrl+Enter always. With `setEditShortcut`, a right-click on
// a row or a dropdown entry offers "Change shortcut…".
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
class QGridLayout;
class QTimer;

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

    // Where a shortcut is changed (Options › Keyboard at that action). Set, a right-click on a
    // result row or a dropdown entry — or the Menu key / Shift+F10 on the highlighted one — offers
    // "Change shortcut…", which closes the palette and calls `edit` with the item's key. Unset,
    // there is no such menu and a right-click in a dropdown behaves as Qt's own.
    void setEditShortcut(std::function<void(const QString &key)> edit);
    // The window supplies indexed matches for the typed query; replies may arrive out of order.
    void setConversationSearch(std::function<void(const QString &query)> search);
    void setConversationResults(const QString &query, const QList<ActionItem> &items);
    void setCardResults(const QString &query, const QList<ActionItem> &items);

    // Parts, for tests and for a caller that wants to drive or inspect them.
    QLineEdit *searchBox() const { return m_search; }
    QListWidget *list() const { return m_list; }
    QList<QAbstractButton *> groupButtons() const;
    // The dropdown group button `index` shows, rebuilt from the catalog so its shortcuts are
    // current; owned by the palette and replaced on the next call.
    QMenu *groupMenu(int index);
    // The key of the highlighted row, or empty when the highlight is on no action.
    QString currentKey() const;
    // The "Change shortcut…" menu last offered, while it exists.
    QMenu *shortcutMenu() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

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
    int buttonInDirection(int direction, int from = -1) const;
    int armOf(int index) const;
    bool isToggleKey(const QKeyEvent *event) const;
    QList<ActionItem> flatCatalog();
    void populateMenu(QMenu *menu, const QList<ActionItem> &items);
    void showShortcutMenu(const QString &key, const QPoint &globalPos);

    QPointer<QWidget> m_window;
    std::function<QList<ActionItem>()> m_catalog;
    std::function<QList<ActionItem>()> m_forThisPane;
    std::function<QStringList()> m_recentKeys;
    std::function<void(const QString &)> m_chosen;
    std::function<void(const QString &)> m_editShortcut;
    std::function<void(const QString &)> m_conversationSearch;
    QList<ActionItem> m_conversations;
    QList<ActionItem> m_cards;
    QTimer *m_searchTimer = nullptr;

    QLineEdit *m_search = nullptr;
    QWidget *m_buttonRow = nullptr;
    QGridLayout *m_buttonGrid = nullptr;
    int m_buttonWidth = -1;
    QList<QToolButton *> m_buttons;
    QStringList m_sections;              // the nine stable Compass group names, when populated
    QList<int> m_groupIds;
    QListWidget *m_list = nullptr;
    QStyledItemDelegate *m_delegate = nullptr;   // the list's row painter; it holds the window's colours
    QPointer<QMenu> m_menu;
    QPointer<QMenu> m_shortcutMenu;
    QList<QKeySequence> m_toggleKeys;

    QList<ActionItem> m_items;              // the catalog as read on open
    std::optional<QList<ActionItem>> m_flat;   // m_items with children flattened, read on first need
    QList<Row> m_rows;
    std::optional<ActionItem> m_submenu;    // the item whose children are listed, if any
    QPointer<QWidget> m_returnFocus;
    int m_lastButton = 0;
    bool m_inResults = false;
    bool m_compact = false;
    bool m_open = false;
};

} // namespace relay
