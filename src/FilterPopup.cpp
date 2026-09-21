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
#include <QScreen>
#include <QShowEvent>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>

namespace relay {
namespace {

constexpr int kRowHeight = 23;
constexpr int kSeparatorHeight = 7;
constexpr int kSidePad = 9;
constexpr int kMaxWidth = 560;
constexpr int kMaxHeight = 420;

// Which original row an item stands for; -1 for the "no match" line.
constexpr int kRowRole = Qt::UserRole + 1;
constexpr int kSeparatorRole = Qt::UserRole + 2;

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

        if (current) {
            const QRectF band = QRectF(rect).adjusted(3, 1, -3, -1);
            painter->setBrush(mix(theme::Accent, theme::SurfaceRaised, 0.34));
            painter->setPen(QPen(mix(theme::Accent, theme::SurfaceRaised, 0.85), 1));
            painter->drawRoundedRect(band, 4, 4);
        }
        painter->setPen(enabled ? theme::Text : theme::TextMuted);
        painter->setFont(option.font);
        const QRect text = rect.adjusted(kSidePad + 3, 0, -(kSidePad + 3), 0);
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
    m_rows = rows;
    m_current = current >= 0 && current < rows.size() && !rows.at(current).separator && rows.at(current).enabled
        ? current
        : -1;
    rebuild(m_current);
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
    m_list->clear();
    for (int i = 0; i < m_rows.size(); ++i) {
        const FilterRow &row = m_rows.at(i);
        if (!rowMatches(row, query)) continue;
        auto *item = new QListWidgetItem(row.separator ? QString() : row.text, m_list);
        item->setData(kRowRole, i);
        item->setData(kSeparatorRole, row.separator);
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

// Size and place the list. `first` decides above-or-below once; afterwards the list keeps that
// side and, as the filter narrows it, shrinks against the edge it is anchored to — so a one-row
// result is a one-row box and not a tall empty one.
void FilterPopup::layoutForAnchor(bool first)
{
    QWidget *anchor = m_anchor;
    if (anchor == nullptr) return;
    const QFontMetrics metrics(anchor->font());
    int widest = anchor->width();
    for (const FilterRow &row : std::as_const(m_rows))
        widest = std::max(widest, metrics.horizontalAdvance(row.text) + 4 * kSidePad);
    const QRect screen = (anchor->screen() != nullptr ? anchor->screen() : QGuiApplication::primaryScreen())
                             ->availableGeometry();
    const int width = std::clamp(widest, 140, std::min(kMaxWidth, screen.width() - 16));

    // Measured, not guessed: sizeHintForRow asks the delegate with the font the list really has,
    // and the filter line answers for itself. Guessing left the last row under a scrollbar.
    int content = 2 * m_list->frameWidth();
    for (int r = 0; r < m_list->count(); ++r) content += m_list->sizeHintForRow(r);
    const int height = std::min(kMaxHeight, m_edit->sizeHint().height() + content + 8);

    const QPoint below = anchor->mapToGlobal(QPoint(0, anchor->height() + 2));
    const QPoint above = anchor->mapToGlobal(QPoint(0, -2));
    if (first) m_above = below.y() + height > screen.bottom();
    QPoint at(below.x(), m_above ? above.y() - height : below.y());
    at.setX(std::clamp(at.x(), screen.left() + 4, screen.right() - width - 4));
    at.setY(std::clamp(at.y(), screen.top() + 4, screen.bottom() - height - 4));
    setGeometry(QRect(at, QSize(width, height)));
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
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        return true;   // a popup has nothing to tab to
    default:
        return false;  // typing goes into the filter line, which rebuilds the list
    }
}

}  // namespace relay
