// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// relay::FilterPopup — the list a status picker drops open (owner, 2026-09-21: "when you use
// alt+m or alt+e, your current selection should be highlighted. then you should be able to
// select with up/down arrows, and also filter with text typing (like warp's model picker)").
//
// Why this exists rather than QComboBox's own popup. Relay forces the Fusion style
// (relay::theme::applyTheme), and Fusion answers SH_ComboBox_Popup with 1: Qt then draws a
// combo's list as a *menu*, through QComboMenuDelegate and CE_MenuItem. A menu item painted on a
// QListView finds none of the stylesheet's `QComboBox … QAbstractItemView` colours — the theme's
// `selection-background-color: @accent` is simply not consulted — so the current row came out
// filled with #1d1613 on a #241c18 list: a 3% step, invisible on a screen. All the box ever
// showed was a hairline focus rectangle, and Up/Down moved that hairline. Measured in
// docs/qa_evidence/2026-09-21-model-box-filter.
//
// So the list paints itself: one delegate, explicit tokens, no style hint in the path. On top of
// that it is what the owner asked for — a filter line you can see, over rows that narrow as you
// type, the way Warp's model picker behaves.
//
// It knows nothing about models, panes or windows: rows in, an index out. CurrentTextComboBox
// hands it whatever its combo already holds (text, data, tooltips, separators, disabled rows),
// so a workstream that changes what the rows *say* changes nothing here.
#include <QColor>
#include <QList>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <functional>

class QKeyEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace relay {

// One row, as the caller already has it. `data` is carried through untouched — the popup never
// reads it — so a pick can be answered in the caller's own vocabulary.
struct FilterRow {
    QString text;
    QString data;
    QString tooltip;
    bool separator = false;
    bool enabled = true;
    // A muted right-hand part, drawn at the far end of the row and elided last: the model box's
    // "via" column, which names the provider a model row would actually run on ("codex",
    // "z.ai +1"; card #MDL1, rule 2). Empty on every other row, and never matched by the filter —
    // typing a provider is the caller's job to fold into `text` if it wants that.
    QString trailing;
    // The section this row belongs to, where the caller has sections (the model box's classes;
    // card #MDL1, design 5.3). Two things read it: a **header** stays while any row of its own
    // group still matches the filter, and Left/Right hand it back to `onExpandKey`. Empty — every
    // other list — and it does nothing.
    QString group;
    // A section label rather than a choice: drawn in its own ink at the left margin, never
    // highlighted, and stepped over by Up and Down exactly as a separator is (owner, 2026-09-21:
    // "the class header rows are not selectable in the picker"). A list with any header row draws
    // its other rows indented under them.
    bool header = false;
};

// The item-data roles a QComboBox row carries the fields above in, so a box whose rows are read
// back out of its own model (CurrentTextComboBox::rowsFromModel) keeps the "via" column and its
// sections. Qt::UserRole itself is `data`, which QComboBox::addItem writes.
constexpr int kTrailingItemRole = Qt::UserRole + 1;
constexpr int kGroupItemRole = Qt::UserRole + 2;
constexpr int kHeaderItemRole = Qt::UserRole + 3;

class FilterPopup final : public QWidget {
public:
    explicit FilterPopup(QWidget *parent = nullptr);

    // The rows and which of them is current, as an index into `rows`. -1 for none. Called again
    // while the list is open (a class expanded, see `onExpandKey`) it keeps the filter line and
    // re-applies it — nothing is asked of the caller mid-keystroke and nothing is sent until Enter.
    void setRows(const QList<FilterRow> &rows, int current);
    // What is drawn now: the rows `setRows` was given, or — while something is typed and
    // `onQueryRows` answered — that wider list. `onPicked`'s index is into this.
    const QList<FilterRow> &rows() const { return m_rows; }

    // ----- Left / Right: the sections expand in place (card #MDL1, design 5.3) --------------
    // The first design paged between one list per mode; the owner replaced it with one list whose
    // **classes** expand. So Left and Right are handed the highlighted row's `group` and -1 or +1,
    // and the caller answers by calling `setRows` again with the new list and returning true — the
    // popup stays open, whatever was typed is kept and re-applied, and the highlight goes back on
    // the row it was on, found by its `data`. Unset, or answering false, and Left and Right stay
    // the filter line's own caret keys, which is what the Alt+E level box wants.
    std::function<bool(const QString &group, int delta)> onExpandKey;

    // ----- the rows a typed filter searches (card #MDL1, design 5.7) ------------------------
    // The owner, 2026-09-21: "the text filter isnt working -- its supposed to show all available
    // models, not just the ones selected for the box picker." The model box is given its classes
    // down to their cutoff, which is what it *shows at rest*; what the filter searches is a wider
    // list, and only the caller knows it. So the popup asks, on every keystroke, with the query as
    // it stands, and draws what comes back in place of the rows it was given — filtered by exactly
    // the same rule, so the caller hands over rows and not matches. An empty answer, or no hook at
    // all (the Alt+E level box, and every other list), means "the rows I already have".
    std::function<QList<FilterRow>(const QString &query)> onQueryRows;

    // Drop open under `anchor` (above it when there is no room below), as wide as the widest row
    // and no wider than the screen, with `anchor`'s font. Clears whatever was typed last time.
    void openFor(QWidget *anchor);
    void dismiss();          // hide with no pick; calls onCancelled if it was open

    // ----- the state the keyboard drives, and what the tests assert on ---------------------
    void setFilterText(const QString &text);
    QString filterText() const;
    void moveCurrent(int delta);      // Up/Down: skips separators and disabled rows
    int currentRow() const;           // index into rows(); -1 when nothing matches
    int visibleCount() const;         // rows the filter left, 0 when there is no match
    bool hasMatches() const { return visibleCount() > 0; }
    int activate();                   // Enter: pick the highlighted row, returns its rows() index
    bool scrolling() const;           // true only when a row is out of reach without scrolling
    bool rowVisible(int row) const;   // that rows() index is drawn whole, not scrolled out
    bool rowShown(int row) const;     // the filter left that rows() index in the list at all
    // Left / Right: open or close the highlighted row's section through `onExpandKey`. True when
    // it handled the key — the list was replaced and the highlight put back where it was.
    bool expandCurrent(int delta);

    // A row matches a query when the query is empty, or fuzzily (relayFuzzyScore) against its
    // text. Separators never match: a divider between groups that are no longer shown is noise.
    // A header never matches on its own words either — it is kept, or dropped, with its group:
    // see `groupHasMatch` (design 5.3, "headers stay while their class has a match").
    static bool rowMatches(const FilterRow &row, const QString &query);

    std::function<void(int)> onPicked;        // an index into rows()
    std::function<void()> onCancelled;        // Escape, or a click outside
    // A key with Ctrl/Alt/Meta that the popup does not use itself. Return true to say it was
    // handled — the popup then closes without picking anything.
    std::function<bool(QKeyEvent *)> onChordKey;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void rebuild(int preferRow);       // refill the list for the current filter text
    void layoutForAnchor(bool first);  // size and place it; `first` picks above or below
    void settleScroll();               // put the rows back at the top (or at the current row)
    bool keyPress(QKeyEvent *key);     // true when the popup consumed it
    void applyPalette();
    int rowOf(const QListWidgetItem *item) const;
    int firstSelectable() const;
    // Whether any row of that group is a model row the filter left: what decides a header's fate.
    bool groupHasMatch(const QString &group, const QString &query) const;

    QPointer<QWidget> m_anchor;
    bool m_above = false;              // the list opened upwards, so it shrinks from the top
    bool m_scrolls = false;            // the rows do not all fit: the last layout said so
    QList<FilterRow> m_base;           // what `setRows` was given: the list with nothing typed
    QList<FilterRow> m_rows;           // what is drawn: `m_base`, or `onQueryRows`'s answer
    int m_current = -1;                // index into m_rows
    QLineEdit *m_edit = nullptr;
    QListWidget *m_list = nullptr;
    bool m_picking = false;            // suppress onCancelled while a pick closes us
};

}  // namespace relay
