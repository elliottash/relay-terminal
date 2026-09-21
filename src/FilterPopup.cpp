// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FilterPopup.h"

#include "AppPaths.h"   // relayFuzzyScore: the same ranking the palette and the `@` picker use
#include "Theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPair>
#include <QScreen>
#include <QScrollBar>
#include <QShowEvent>
#include <QStyledItemDelegate>
#include <QLayout>
#include <QVBoxLayout>

#include <algorithm>

namespace relay {
namespace {

constexpr int kRowHeight = 23;
constexpr int kSeparatorHeight = 7;
constexpr int kSidePad = 9;
constexpr int kMaxWidth = 560;
constexpr int kMaxRows = 14;   // past this the list scrolls; below it, it never does

// Which original row an item stands for; -1 for the "no match" line.
constexpr int kRowRole = Qt::UserRole + 1;
constexpr int kSeparatorRole = Qt::UserRole + 2;
constexpr int kTrailingRole = Qt::UserRole + 3;
// The marker gutter: a fixed-width column at the left of every row of a page that has a marked row,
// so the names line up whether or not their own row carries the mark. kMarkerRole is 1 on the rows
// that do; kGutterRole is 1 on every row of such a page. See `markerOf` below for why the mark is
// carried in the row's text and taken back out here.
constexpr int kMarkerRole = Qt::UserRole + 4;
constexpr int kGutterRole = Qt::UserRole + 5;

// The mark the model box puts on the mode the pane is in (relay::modelrows::modeRowText). It is in
// the row's *text* because the combo, the popup and the phone's menu all draw the same string and
// only one of the three can paint a gutter; the popup takes it back out and draws it itself, which
// is the only way "high", "main" and "flash" can start at the same x in a proportional font. Two
// spaces stand in its place on the rows that are not marked.
const QString kMarker = QStringLiteral("\u2022");

// (marked, the text without the marker column) for one row.
QPair<bool, QString> markerOf(const QString &text)
{
    if (text.startsWith(kMarker + QLatin1Char(' '))) return {true, text.mid(kMarker.size() + 1)};
    if (text.startsWith(QLatin1String("  "))) return {false, text.mid(2)};
    return {false, text};
}

bool hasMarkerColumn(const QString &text)
{
    return text.startsWith(kMarker + QLatin1Char(' ')) || text.startsWith(QLatin1String("  "));
}

// How wide that column is: the mark, with room on either side of it. One number for every row of
// the page, so the gutter cannot depend on which rows the filter happened to leave.
int gutterWidth(const QFontMetrics &metrics)
{
    return metrics.horizontalAdvance(kMarker) + 7;
}

QColor mix(const QColor &over, const QColor &under, qreal amount)
{
    const qreal a = std::clamp(amount, 0.0, 1.0);
    return QColor::fromRgbF(over.redF() * a + under.redF() * (1 - a),
                            over.greenF() * a + under.greenF() * (1 - a),
                            over.blueF() * a + under.blueF() * (1 - a));
}

// The list paints itself, on purpose: this is the whole reason the popup exists (FilterPopup.h).
// No style hint, no palette group, no `selection-background-color` that a menu-item painter will
// not read — a filled rounded rect in the theme's accent, and text that stays legible on it.
class RowDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (index.data(kSeparatorRole).toBool()) return QSize(option.rect.width(), kSeparatorHeight);
        return QSize(option.rect.width(), std::max(kRowHeight, option.fontMetrics.height() + 8));
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QRect rect = option.rect;
        if (index.data(kSeparatorRole).toBool()) {
            painter->setPen(theme::Border);
            const int y = rect.center().y();
            painter->drawLine(rect.left() + kSidePad, y, rect.right() - kSidePad, y);
            painter->restore();
            return;
        }
        const bool enabled = index.flags().testFlag(Qt::ItemIsEnabled);
        const bool current = option.state.testFlag(QStyle::State_Selected);
        const int gutter = index.data(kGutterRole).toBool() ? gutterWidth(option.fontMetrics) : 0;

        if (current) {
            const QRectF band = QRectF(rect).adjusted(3, 1, -3, -1);
            painter->setBrush(mix(theme::Accent, theme::SurfaceRaised, 0.34));
            painter->setPen(QPen(mix(theme::Accent, theme::SurfaceRaised, 0.85), 1));
            painter->drawRoundedRect(band, 4, 4);
        }
        painter->setFont(option.font);
        QRect text = rect.adjusted(kSidePad + 3, 0, -(kSidePad + 3), 0);
        if (gutter > 0) {
            if (index.data(kMarkerRole).toBool()) {
                painter->setPen(enabled ? theme::Text : theme::TextMuted);
                painter->drawText(QRect(text.left(), text.top(), gutter, text.height()),
                                  Qt::AlignVCenter | Qt::AlignLeft, kMarker);
            }
            text.setLeft(text.left() + gutter);
        }
        // The "via" part, at the far end and in the muted ink: which provider this row would
        // actually run on (card #MDL1, rule 2). It is drawn first and the name is elided into
        // what is left, so a narrow box loses the end of a long model id rather than the column
        // that says where the turn is going.
        const QString trailing = index.data(kTrailingRole).toString();
        if (!trailing.isEmpty()) {
            const int width = option.fontMetrics.horizontalAdvance(trailing);
            if (width + 24 < text.width()) {
                painter->setPen(theme::TextMuted);
                painter->drawText(text, Qt::AlignVCenter | Qt::AlignRight, trailing);
                text.setRight(text.right() - width - 12);
            }
        }
        painter->setPen(enabled ? theme::Text : theme::TextMuted);
        painter->drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
                          option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(),
                                                        Qt::ElideRight, text.width()));
        painter->restore();
    }
};

}  // namespace

FilterPopup::FilterPopup(QWidget *parent)
    : QWidget(parent, Qt::Popup)
{
    setObjectName(QStringLiteral("filterPopup"));
    setAttribute(Qt::WA_StyledBackground, true);   // a plain QWidget draws no stylesheet ground without it
    setFocusPolicy(Qt::StrongFocus);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);

    m_edit = new QLineEdit(this);
    m_edit->setObjectName(QStringLiteral("filterPopupEdit"));
    m_edit->setFrame(false);
    m_edit->setClearButtonEnabled(false);
    m_edit->setPlaceholderText(QStringLiteral("type to filter"));
    m_edit->setTextMargins(kSidePad + 1, 0, kSidePad, 0);
    m_edit->installEventFilter(this);
    layout->addWidget(m_edit);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("filterPopupList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setUniformItemSizes(false);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setMouseTracking(true);
    m_list->viewport()->setMouseTracking(true);
    m_list->setItemDelegate(new RowDelegate(m_list));
    m_list->setMinimumSize(0, 0);   // a one-row answer is a one-row box, scroll area floor and all
    m_list->installEventFilter(this);
    m_list->viewport()->installEventFilter(this);
    layout->addWidget(m_list, 1);

    connect(m_edit, &QLineEdit::textChanged, this, [this](const QString &) { rebuild(-1); });
    // The row under the pointer is the highlighted one, as it is in Warp's picker: a mouse user
    // then presses Enter on what they are looking at, and a keyboard user is never fighting a
    // highlight left behind somewhere else.
    connect(m_list, &QListWidget::itemEntered, this, [this](QListWidgetItem *item) {
        if (item == nullptr || !item->flags().testFlag(Qt::ItemIsEnabled)) return;
        m_current = rowOf(item);
        m_list->setCurrentItem(item);
    });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item == nullptr || !item->flags().testFlag(Qt::ItemIsEnabled)) return;
        m_current = rowOf(item);
        activate();
    });
    applyPalette();
}

void FilterPopup::applyPalette()
{
    // Read at open time, not cached: picking another theme reassigns the tokens (Theme.h).
    const QString ground = theme::SurfaceRaised.name();
    setStyleSheet(QStringLiteral(
                      "QWidget#filterPopup { background: %1; border: 1px solid %2; border-radius: 6px; }"
                      "QLineEdit#filterPopupEdit { background: transparent; border: none;"
                      " border-bottom: 1px solid %2; color: %3; padding: 4px 0; }"
                      "QListWidget#filterPopupList { background: transparent; border: none; outline: none; }")
                      .arg(ground, theme::Border.name(), theme::Text.name()));
    QPalette pal = m_edit->palette();
    pal.setColor(QPalette::Text, theme::Text);
    pal.setColor(QPalette::PlaceholderText, theme::TextMuted);
    m_edit->setPalette(pal);
    // The viewport is its own widget and takes QPalette::Base, not the rule above, on the styles
    // that paint a scroll area's ground themselves.
    QPalette list = m_list->palette();
    list.setColor(QPalette::Base, theme::SurfaceRaised);
    list.setColor(QPalette::Text, theme::Text);
    m_list->setPalette(list);
}

int FilterPopup::rowOf(const QListWidgetItem *item) const
{
    return item == nullptr ? -1 : item->data(kRowRole).toInt();
}

bool FilterPopup::rowMatches(const FilterRow &row, const QString &query)
{
    if (query.trimmed().isEmpty()) return true;
    if (row.separator) return false;
    return relayFuzzyScore(query.trimmed(), row.text) > 0;
}

void FilterPopup::setRows(const QList<FilterRow> &rows, int current)
{
    m_pages.clear();
    m_page = 0;
    m_rows = rows;
    m_current = current >= 0 && current < rows.size() && !rows.at(current).separator && rows.at(current).enabled
        ? current
        : -1;
    rebuild(m_current);
}

// ----- pages (card #MDL1, section 5.1) ---------------------------------------------------------
// The whole point of holding every page is that a turn changes nothing but which rows are drawn:
// the popup stays open, the filter line is untouched and simply re-applied to the new rows, and
// nothing is said to the caller until Enter. Escape after any number of turns is still "leave
// everything as it was", because nothing has been sent.

void FilterPopup::setPages(const QList<FilterPage> &pages, const QString &currentId)
{
    m_pages = pages;
    m_page = 0;
    for (int i = 0; i < m_pages.size(); ++i)
        if (m_pages.at(i).id == currentId) { m_page = i; break; }
    if (m_pages.isEmpty()) { setRows({}, -1); return; }
    const FilterPage &page = m_pages.at(m_page);
    const QList<FilterPage> keep = m_pages;
    const int at = m_page;
    setRows(page.rows, page.current);   // clears m_pages, so put them back
    m_pages = keep;
    m_page = at;
}

QString FilterPopup::currentPageId() const
{
    return m_page >= 0 && m_page < m_pages.size() ? m_pages.at(m_page).id : QString();
}

void FilterPopup::turnPage(int delta)
{
    if (m_pages.size() < 2 || delta == 0) return;
    const int next = std::clamp(m_page + delta, 0, int(m_pages.size()) - 1);
    if (next == m_page) return;
    m_page = next;
    const FilterPage &page = m_pages.at(m_page);
    m_rows = page.rows;
    m_current = page.current >= 0 && page.current < m_rows.size() && !m_rows.at(page.current).separator
                    && m_rows.at(page.current).enabled
        ? page.current
        : -1;
    rebuild(m_current);   // the filter text is untouched and re-applied by rebuild
    if (onPageChanged) onPageChanged(page.id);
}

void FilterPopup::showPage(const QString &id)
{
    for (int i = 0; i < m_pages.size(); ++i)
        if (m_pages.at(i).id == id) { turnPage(i - m_page); return; }
}

void FilterPopup::setFilterText(const QString &text)
{
    if (m_edit->text() == text) { rebuild(-1); return; }
    m_edit->setText(text);   // textChanged rebuilds
}

QString FilterPopup::filterText() const { return m_edit->text(); }

// The list for the filter as it stands. `preferRow` is the original row to leave highlighted when
// it survived the filter; otherwise the first row that can be picked takes the highlight, which is
// what "the first match becomes highlighted as you type" means.
void FilterPopup::rebuild(int preferRow)
{
    const QString query = m_edit->text();
    const bool filtering = !query.trimmed().isEmpty();
    // Whether this page carries a marker column at all, decided over *every* row rather than the
    // ones the filter left: a page must not shift sideways as you type. A list with no marked rows
    // — the Alt+E level box, and every other box — gets no gutter and is drawn exactly as before.
    bool gutter = false;
    for (const FilterRow &row : std::as_const(m_rows))
        if (!row.separator && hasMarkerColumn(row.text)) { gutter = true; break; }
    m_list->clear();
    for (int i = 0; i < m_rows.size(); ++i) {
        const FilterRow &row = m_rows.at(i);
        if (!rowMatches(row, query)) continue;
        const QPair<bool, QString> mark = gutter ? markerOf(row.text) : qMakePair(false, row.text);
        auto *item = new QListWidgetItem(row.separator ? QString() : mark.second, m_list);
        item->setData(kRowRole, i);
        item->setData(kSeparatorRole, row.separator);
        item->setData(kTrailingRole, row.trailing);
        item->setData(kMarkerRole, mark.first);
        item->setData(kGutterRole, gutter);
        if (!row.tooltip.isEmpty()) item->setToolTip(row.tooltip);
        if (row.separator || !row.enabled) item->setFlags(Qt::NoItemFlags);
    }
    if (visibleCount() == 0) {
        // A quiet line rather than an empty box: an empty popup reads as a broken one.
        m_list->clear();
        auto *item = new QListWidgetItem(QStringLiteral("no match"), m_list);
        item->setData(kRowRole, -1);
        item->setData(kSeparatorRole, false);
        item->setFlags(Qt::NoItemFlags);
        m_current = -1;
        m_list->setCurrentItem(nullptr);
        if (isVisible()) layoutForAnchor(false);
        return;
    }
    // Which row ends up highlighted: the asked-for one when it survived the filter, else — while
    // filtering — the first match, which is what "the first match becomes highlighted as you
    // type" means, and otherwise whatever was highlighted before.
    const int want = preferRow >= 0 ? preferRow : filtering ? -1 : m_current;
    int pick = -1;
    for (int r = 0; r < m_list->count() && pick < 0; ++r) {
        const QListWidgetItem *item = m_list->item(r);
        if (!item->flags().testFlag(Qt::ItemIsEnabled)) continue;
        if (rowOf(item) == want) pick = r;
    }
    if (pick < 0) pick = firstSelectable();
    if (pick < 0) { m_current = -1; m_list->setCurrentItem(nullptr); return; }
    m_current = rowOf(m_list->item(pick));
    m_list->setCurrentRow(pick);
    m_list->scrollToItem(m_list->item(pick), QAbstractItemView::EnsureVisible);
    if (isVisible()) layoutForAnchor(false);
}

int FilterPopup::firstSelectable() const
{
    for (int r = 0; r < m_list->count(); ++r)
        if (m_list->item(r)->flags().testFlag(Qt::ItemIsEnabled)) return r;
    return -1;
}

int FilterPopup::currentRow() const { return m_current; }

int FilterPopup::visibleCount() const
{
    int n = 0;
    for (int r = 0; r < m_list->count(); ++r)
        if (m_list->item(r)->flags().testFlag(Qt::ItemIsEnabled)) ++n;
    return n;
}

// Up/Down over the rows that can be picked: a separator and a disabled row are stepped over, not
// landed on. No wrap, which is what every other list in Relay does.
void FilterPopup::moveCurrent(int delta)
{
    if (delta == 0 || m_list->count() == 0) return;
    QList<int> pickable;
    for (int r = 0; r < m_list->count(); ++r)
        if (m_list->item(r)->flags().testFlag(Qt::ItemIsEnabled)) pickable << r;
    if (pickable.isEmpty()) return;
    int at = 0;
    for (int i = 0; i < pickable.size(); ++i)
        if (rowOf(m_list->item(pickable.at(i))) == m_current) { at = i; break; }
    const int next = std::clamp(at + delta, 0, int(pickable.size()) - 1);
    const int row = pickable.at(next);
    m_current = rowOf(m_list->item(row));
    m_list->setCurrentRow(row);
    m_list->scrollToItem(m_list->item(row), QAbstractItemView::EnsureVisible);
}

int FilterPopup::activate()
{
    const int row = m_current;
    if (row < 0) return -1;
    m_picking = true;
    hide();
    m_picking = false;
    if (onPicked) onPicked(row);
    return row;
}

void FilterPopup::dismiss()
{
    if (!isVisible()) return;
    hide();   // hideEvent reports the cancel
}

void FilterPopup::openFor(QWidget *anchor)
{
    if (anchor == nullptr) return;
    anchor->ensurePolished();
    m_anchor = anchor;
    setFont(anchor->font());
    m_edit->setFont(anchor->font());
    m_list->setFont(anchor->font());
    applyPalette();
    {
        const QSignalBlocker block(m_edit);
        m_edit->clear();
    }
    rebuild(m_current);
    layoutForAnchor(true);
    show();
    raise();
    m_edit->setFocus(Qt::PopupFocusReason);
}

// Size and place the list.
//
// Two rules, both of them things the first cut got wrong (the Alt+E list had a scrollbar over two
// rows, and hung off the right edge of the window because the level box sits at the end of the
// strip):
//
//  * **A list that fits does not scroll.** The height is the real row heights the delegate
//    returns, plus the filter line's own, and the scrollbar is switched *off* rather than left to
//    ScrollBarAsNeeded — a few pixels short and "as needed" means "always". Only past kMaxRows, or
//    past the room the window leaves, does it become a scrolling list, and then the bar's width is
//    added to the popup instead of taken out of the rows.
//  * **It stays inside the window it hangs from.** Left-aligned with the box as before; when that
//    would run off the right, right-aligned to the box instead, and clamped to the window's own
//    rectangle (intersected with the screen) either way.
//
// `first` decides above-or-below once; afterwards the list keeps that side and, as the filter
// narrows it, shrinks against the edge it is anchored to — so a one-row result is a one-row box.
void FilterPopup::layoutForAnchor(bool first)
{
    QWidget *anchor = m_anchor;
    if (anchor == nullptr) return;
    const QFontMetrics metrics(anchor->font());
    // Polished first, every time: an unpolished QLineEdit answers sizeHint() without the
    // stylesheet's padding, and sizing the box from that number left a gap under the last row on
    // the first open and clipped it on the next.
    ensurePolished();
    m_edit->ensurePolished();
    m_list->ensurePolished();

    // The rectangle the list may live in: the window it hangs from, and never off the screen.
    const QScreen *screen = anchor->screen() != nullptr ? anchor->screen() : QGuiApplication::primaryScreen();
    QRect bound = screen != nullptr ? screen->availableGeometry() : QRect(0, 0, 1024, 768);
    if (const QWidget *top = anchor->window(); top != nullptr && top->isVisible()) {
        const QRect window = bound.intersected(top->frameGeometry());
        if (window.width() > 160 && window.height() > 160) bound = window;
    }

    // How tall the rows really are, straight from the delegate, and how many of them may show.
    QList<int> rows;
    int wanted = 0;
    for (int r = 0; r < m_list->count(); ++r) {
        rows << m_list->sizeHintForRow(r) + 2 * m_list->spacing();
        wanted += rows.last();
    }
    const int chrome = 2 * m_list->frameWidth() + 2;   // the list's frame and the popup's own 1px
    // The filter line is pinned as well as the list. applyPalette() re-sets the stylesheet on
    // every open, so the first sizeHint() after it is the unstyled one and the real layout pass a
    // moment later used the styled one — the box then had a spare row's worth of ground under the
    // last row, or cut through it, depending on which way the two numbers differed. With both
    // children fixed there is nothing left for the layout to redistribute.
    const int editHeight = std::max({m_edit->sizeHint().height(), m_edit->minimumSizeHint().height(),
                                     metrics.height() + 10});
    m_edit->setFixedHeight(editHeight);
    int cap = 0;
    for (int r = 0; r < rows.size() && r < kMaxRows; ++r) cap += rows.at(r);
    const int room = std::max(bound.height() - editHeight - chrome - 8, 3 * kRowHeight);
    const int listHeight = std::min({std::max(wanted, 1), cap, room});
    const bool scrolls = listHeight < wanted;
    m_list->setVerticalScrollBarPolicy(scrolls ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    m_list->setFixedHeight(listHeight);
    // The layout caches the widget's minimum size and only refreshes it on the next pass, so a
    // setGeometry straight after a shrink was clamped to the height the list used to want.
    if (QLayout *box = layout(); box != nullptr) box->activate();
    // The layout's own total, not a sum of guesses: with the list at a fixed height this is
    // exactly the room the filter line and the rows need, so there is no slack for the layout to
    // leave under the last row and nothing for it to clip.
    const int height = std::min(bound.height() - 8, editHeight + listHeight + chrome);

    // No narrower than the box it hangs from, no wider than its own longest row — with room for
    // the filter line's own words, which are content too.
    int widest = std::max(anchor->width(), metrics.horizontalAdvance(m_edit->placeholderText()) + 4 * kSidePad);
    // The marker column is drawn beside the text, not over it, so it is width the rows need.
    for (const FilterRow &row : std::as_const(m_rows))
        if (!row.separator && hasMarkerColumn(row.text)) { widest += gutterWidth(metrics); break; }
    for (const FilterRow &row : std::as_const(m_rows))
        widest = std::max(widest, metrics.horizontalAdvance(row.text) + 4 * kSidePad
                                      + (row.trailing.isEmpty() ? 0
                                                                : metrics.horizontalAdvance(row.trailing) + 24));
    if (scrolls) widest += m_list->verticalScrollBar()->sizeHint().width();
    const int width = std::clamp(widest, 64, std::min(kMaxWidth, bound.width() - 8));

    const QPoint corner = anchor->mapToGlobal(QPoint(0, 0));
    const int below = corner.y() + anchor->height() + 2;
    if (first) m_above = below + height > bound.bottom();
    int x = corner.x();
    if (x + width - 1 > bound.right()) x = corner.x() + anchor->width() - width;   // right-align to the box
    x = std::clamp(x, bound.left(), std::max(bound.left(), bound.right() - width + 1));
    int y = m_above ? corner.y() - 2 - height : below;
    y = std::clamp(y, bound.top(), std::max(bound.top(), bound.bottom() - height + 1));
    setGeometry(QRect(QPoint(x, y), QSize(width, height)));

    // Only now does the list have its new size, and only now can it be scrolled sensibly. A list
    // that grew keeps whatever offset it scrolled to while it was short: three levels with the
    // middle one current drew as "high", "max" and a row of empty ground, with "low" scrolled off
    // the top — the second half of the Alt+E fault, and the one that survived the sizing fix.
    if (QLayout *box = layout(); box != nullptr) box->activate();
    if (!scrolls) m_list->verticalScrollBar()->setValue(0);
    else if (QListWidgetItem *item = m_list->currentItem(); item != nullptr)
        m_list->scrollToItem(item, QAbstractItemView::EnsureVisible);
}

// Whether that row (an index into rows()) is drawn whole, rather than scrolled out of the
// viewport. Every row of a list short enough to draw whole answers true.
bool FilterPopup::rowVisible(int row) const
{
    for (int r = 0; r < m_list->count(); ++r) {
        const QListWidgetItem *item = m_list->item(r);
        if (rowOf(item) != row) continue;
        const QRect rect = m_list->visualItemRect(item);
        return rect.top() >= 0 && rect.bottom() <= m_list->viewport()->height();
    }
    return false;
}

// Whether any row is out of reach without scrolling. Asked by tests/filterpopup_test.cpp, and the
// answer is "no" for every list short enough to draw whole.
bool FilterPopup::scrolling() const
{
    if (m_list->verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff) return false;
    const QScrollBar *bar = m_list->verticalScrollBar();
    return bar->maximum() > bar->minimum();
}

void FilterPopup::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_edit->setFocus(Qt::PopupFocusReason);
}

void FilterPopup::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (!m_picking && onCancelled) onCancelled();
}

// Qt hands an open popup every mouse press, wherever it landed: one outside our own rectangle is
// the user pointing at something else, and closes the list without picking anything.
void FilterPopup::mousePressEvent(QMouseEvent *event)
{
    if (!rect().contains(event->pos())) { dismiss(); return; }
    QWidget::mousePressEvent(event);
}

bool FilterPopup::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit) {
        // Every key is the popup's while it is open: a window shortcut must not take Up, Down or
        // a letter out from under it. Chords are given back on purpose, below.
        if (event->type() == QEvent::ShortcutOverride) { event->accept(); return true; }
        if (event->type() == QEvent::KeyPress) return keyPress(static_cast<QKeyEvent *>(event));
        return false;
    }
    if ((watched == m_list || watched == m_list->viewport()) && event->type() == QEvent::MouseButtonPress) {
        // A press inside the list is the list's; anything else closes us (Qt hands a popup every
        // press, including the ones outside its own rectangle).
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (!m_list->viewport()->rect().contains(mouse->pos())) { dismiss(); return true; }
    }
    return QWidget::eventFilter(watched, event);
}

bool FilterPopup::keyPress(QKeyEvent *key)
{
    const auto chord = key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (chord != 0u) {
        // Alt+M again, or any other chord the caller knows: it is not ours. Everything else
        // (Ctrl+A, Ctrl+V) stays the filter line's.
        if (onChordKey && onChordKey(key)) return true;
        return false;
    }
    switch (key->key()) {
    case Qt::Key_Escape:
        dismiss();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        activate();
        return true;
    case Qt::Key_Up:
        moveCurrent(-1);
        return true;
    case Qt::Key_Down:
        moveCurrent(1);
        return true;
    case Qt::Key_PageUp:
        moveCurrent(-8);
        return true;
    case Qt::Key_PageDown:
        moveCurrent(8);
        return true;
    case Qt::Key_Home:
        moveCurrent(-m_rows.size() - 1);
        return true;
    case Qt::Key_End:
        moveCurrent(m_rows.size() + 1);
        return true;
    case Qt::Key_Left:
    case Qt::Key_Right:
        // The owner's words were "left/right changes mode", with no condition on them, so that is
        // what they do — **always**, whatever has been typed, and not only while the filter line is
        // empty. A key that means one thing with an empty box and another with a letter in it is
        // exactly the sort of guessing this popup replaced. The filter is still editable: typing
        // appends, Backspace takes the last character back and Ctrl+A / Ctrl+U clear it, so the
        // caret simply lives at the end of what you typed. A one-page list (the Alt+E level box)
        // has no modes to turn between, and there Left and Right stay the line edit's own caret
        // keys, exactly as they were.
        if (m_pages.size() > 1) { turnPage(key->key() == Qt::Key_Left ? -1 : 1); return true; }
        return false;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        return true;   // a popup has nothing to tab to
    default:
        return false;  // typing goes into the filter line, which rebuilds the list
    }
}

}  // namespace relay
