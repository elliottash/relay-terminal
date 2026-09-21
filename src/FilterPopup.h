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
};

// The item-data role a QComboBox row carries its `trailing` part in, so a box whose rows are read
// back out of its own model (CurrentTextComboBox::rowsFromModel) keeps the "via" column. Qt::UserRole
// itself is `data`, which QComboBox::addItem writes.
constexpr int kTrailingItemRole = Qt::UserRole + 1;

// A page of rows, for a list whose Left and Right keys turn between several of them (card #MDL1,
// section 5.1: the model box's modes). The popup holds every page at once, so a turn is instant
// and nothing is asked of the caller mid-keystroke.
struct FilterPage {
    QString id;                // the caller's own word for it ("flash"); given back by currentPageId()
    QString label;             // what it is called, for a caller that draws a header; unused here
    QList<FilterRow> rows;
    int current = -1;          // the row this page opens on, an index into `rows`
};

class FilterPopup final : public QWidget {
public:
    explicit FilterPopup(QWidget *parent = nullptr);

    // The rows and which of them is current, as an index into `rows`. -1 for none. This is the
    // one-page form and it clears whatever pages were set: Left and Right then do nothing here and
    // stay the filter line's own caret keys, which is what the Alt+E level box wants.
    void setRows(const QList<FilterRow> &rows, int current);
    const QList<FilterRow> &rows() const { return m_rows; }

    // ----- pages: Left / Right turn between several lists in place (card #MDL1) -------------
    // Every page's rows up front, and which one to open on. The pages are a short ring the user
    // steps along — the model box's high / main / flash / local — so the list below redraws, the
    // highlight lands on that page's own current row, the popup stays open and whatever was typed
    // is kept and re-applied. Nothing is asked of the caller until Enter.
    void setPages(const QList<FilterPage> &pages, const QString &currentId);
    int pageCount() const { return int(m_pages.size()); }
    QString currentPageId() const;
    // Left / Right. Clamped, never wrapped, like every other list in Relay: two Lefts from the
    // third page land on the first and stay there.
    void turnPage(int delta);
    void showPage(const QString &id);
    std::function<void(const QString &id)> onPageChanged;   // after a turn, with the new page's id

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

    // A row matches a query when the query is empty, or fuzzily (relayFuzzyScore) against its
    // text. Separators never match: a divider between groups that are no longer shown is noise.
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
    bool keyPress(QKeyEvent *key);     // true when the popup consumed it
    void applyPalette();
    int rowOf(const QListWidgetItem *item) const;
    int firstSelectable() const;

    QPointer<QWidget> m_anchor;
    bool m_above = false;              // the list opened upwards, so it shrinks from the top
    QList<FilterRow> m_rows;           // the page being shown (the only rows, with no pages set)
    QList<FilterPage> m_pages;         // empty for a one-page list
    int m_page = 0;                    // index into m_pages
    int m_current = -1;                // index into m_rows
    QLineEdit *m_edit = nullptr;
    QListWidget *m_list = nullptr;
    bool m_picking = false;            // suppress onCancelled while a pick closes us
};

}  // namespace relay
