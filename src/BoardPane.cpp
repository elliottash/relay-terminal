// SPDX-License-Identifier: GPL-3.0-or-later
#include "BoardPane.h"
#include "Projects.h"   // which folder of a project is its board: `switchboard/`, else `issues/`
#include "ToolLabel.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDrag>
#include <QDir>
#include <QDirIterator>
#include <QDropEvent>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextLayout>
#include <QTimeZone>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "CopyOnSelect.h"
#include "RichEditor.h"
#include "Theme.h"

namespace relay {
namespace {

constexpr int kCardRole = Qt::UserRole;          // the card id on a card row; empty on a header

// Row geometry (owner review, 2026-09-18: with ~96 cards the Trello columns ran off the right
// edge and wasted the height, and a single-owner tracker reads better as rows). One line per
// card and one per section header, measured the same way for sizeHint() and paint().
constexpr int kRowPadX = 10;
constexpr int kGlyphWidth = 15;      // the status mark's box, so every title starts at one x
constexpr int kBadgeGap = 5;
constexpr int kIdGap = 8;
constexpr int kTitleMin = 80;        // the title never shrinks past this; badges go instead
constexpr int kAddWidth = 22;        // the `+` at the right of a section header
// Below this width the open card takes the whole pane instead of squeezing the list.
constexpr int kStackedWidth = 900;

// A section's tooltip, with what the section is for on a line of its own underneath. Every
// surface that names a section without showing its cards goes through here, so the answer to
// "what is Ready to start?" is in one place (board::sectionMeaning) and reads the same in all
// of them. A section id with no definition — a column a board invented — keeps its tip as is.
QString withMeaning(const QString &tip, const QString &sectionId)
{
    const QString why = board::sectionMeaning(sectionId);
    return why.isEmpty() ? tip : tip + QLatin1Char('\n') + why;
}

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

QColor alpha(QColor color, int value)
{
    color.setAlpha(value);
    return color;
}

// Both stop at theme::FloorPt: a factor of the row's font is how the list keeps its proportions,
// but 0.8 of a 9pt default was 7.2pt (docs/ARCHITECTURE.md, "Legible text").
QFont smaller(const QFont &base, qreal factor)
{
    QFont font(base);
    if (base.pointSizeF() > 0)
        font.setPointSizeF(base.pointSizeF() * factor);
    else
        font.setPixelSize(qMax(8, int(base.pixelSize() * factor)));
    return theme::legible(font);
}

QFont monoFont(const QFont &base, qreal factor)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (base.pointSizeF() > 0)
        font.setPointSizeF(base.pointSizeF() * factor);
    return theme::legible(font);
}

// The ink a badge is drawn in, and its pill's edge. An invalid edge means no pill at all: the
// quiet end of the row (the age, the thread count) is plain text, so a busy row has fewer boxes.
QPair<QColor, QColor> badgeInk(board::Badge::Kind kind)
{
    switch (kind) {
    case board::Badge::Agent:
        return {theme::Agent, mix(theme::Agent, theme::Surface, 0.55)};
    case board::Badge::Waiting:
        return {theme::Warning, mix(theme::Warning, theme::Surface, 0.5)};
    case board::Badge::TasksDone:
        return {theme::Success, mix(theme::Success, theme::Surface, 0.6)};
    // The same green as a finished checklist, and for the same reason: something that had to be
    // done is done. It is a fact about the card, so it wears a pill like the other facts.
    case board::Badge::Verified:
        return {theme::Success, mix(theme::Success, theme::Surface, 0.45)};
    case board::Badge::Status:
        return {theme::Text, theme::BorderStrong};
    case board::Badge::Assignee:
        return {theme::Text, theme::Border};
    case board::Badge::Private:
        // Brass, not amber: a card being private is a fact about it, not somebody waiting on you,
        // and amber means only the second (be81edb). Waiting above keeps the amber.
        return {theme::Tool, mix(theme::Tool, theme::Surface, 0.5)};
    case board::Badge::Age:
    case board::Badge::Thread:
        return {theme::TextMuted, QColor()};
    default:
        return {theme::TextMuted, theme::Border};
    }
}

// The colour of a card row's status mark: muted where nothing is committed, the accent where
// work is running, the warning colour where somebody is being waited on.
QColor glyphInk(const QString &status)
{
    if (status == QStringLiteral("in-progress") || status == QStringLiteral("executing"))
        return theme::Accent;
    if (status.startsWith(QStringLiteral("needs-qa")))
        return theme::Agent;
    if (status.startsWith(QStringLiteral("needs-")))
        return theme::Warning;
    if (status == QStringLiteral("done"))
        return theme::Success;
    if (status == QStringLiteral("ready") || status == QStringLiteral("approved")
        || status == QStringLiteral("active"))
        return theme::Text;
    return theme::TextMuted;
}

// Where everything on one card row goes, relative to the row's top-left corner. The badges that
// do not fit are already gone (board::fitBadges) and the title is elided into what is left, so a
// narrow pane loses decoration before it loses meaning.
struct CardShape {
    QRect glyphRect, titleRect, idRect;
    QString title;
    QList<QPair<board::Badge, QRect>> badges;
    int height = 0;
};

CardShape cardShape(const board::Card &card, bool showStatus, const QFont &font, int width,
                    const QDate &today)
{
    CardShape shape;
    const QFontMetrics metrics(font);
    const QFontMetrics badgeMetrics(smaller(font, 0.85));
    const QFontMetrics idMetrics(monoFont(font, 0.85));
    shape.height = qMax(24, metrics.height() + 10);

    int x = kRowPadX;
    shape.glyphRect = QRect(x, 0, kGlyphWidth, shape.height);
    x += kGlyphWidth + 4;
    const int available = qMax(40, width - x - kRowPadX);

    // No single badge may eat the row. A card whose `assignee` holds a sentence is a mistake in
    // the file, but the row still has to be readable, so a badge is capped at a quarter of the
    // width and elided inside its pill.
    const int cap = qMax(60, available / 4);
    QHash<QString, int> widths;
    QList<QPair<board::Badge, int>> measured;
    for (const board::Badge &badge : board::rowBadges(card, showStatus, today)) {
        const int w = qMin(cap, badgeMetrics.horizontalAdvance(badge.text) + 12);
        widths.insert(badge.text, w);
        measured << qMakePair(badge, w);
    }
    const int idWidth = idMetrics.horizontalAdvance(card.reference()) + kIdGap;
    // What the title is owed before a badge may have anything: the rest of the row is decoration
    // next to knowing which card this is.
    const int titleFloor = qBound(kTitleMin, available * 45 / 100, 280);
    const QList<board::Badge> kept =
        board::fitBadges(measured, qMax(0, available - titleFloor - idWidth - 12), kBadgeGap);

    // Right to left from the row's right edge, prepending, so the order on screen ends up the
    // reading order board::rowBadges returned.
    int right = width - kRowPadX;
    for (int i = int(kept.size()) - 1; i >= 0; --i) {
        const int w = widths.value(kept.at(i).text);
        const int h = badgeMetrics.height() + 2;
        right -= w;
        shape.badges.prepend(qMakePair(kept.at(i), QRect(right, (shape.height - h) / 2, w, h)));
        right -= kBadgeGap;
    }
    const int titleEnd = shape.badges.isEmpty() ? width - kRowPadX : right + kBadgeGap - 10;
    const int titleZone = qMax(30, titleEnd - x);
    const int titleWidth = qMax(30, titleZone - idWidth);
    shape.title = metrics.elidedText(card.title.isEmpty() ? QStringLiteral("(untitled)") : card.title,
                                     Qt::ElideRight, titleWidth);
    const int drawn = qMin(titleWidth, metrics.horizontalAdvance(shape.title));
    shape.titleRect = QRect(x, 0, drawn, shape.height);
    shape.idRect = QRect(x + drawn + kIdGap, 0, qMax(0, idWidth - kIdGap), shape.height);
    return shape;
}

// A section header: a chevron, the status name, its count, and a `+` that adds into it.
struct SectionShape {
    QRect chevronRect, titleRect, addRect;
    int height = 0;
};

SectionShape sectionShape(const QFont &font, int width, bool first, bool canAdd)
{
    SectionShape shape;
    const QFontMetrics metrics(smaller(font, 0.85));
    const int top = first ? 6 : 14;              // the rule above needs air; the first has none
    shape.height = top + metrics.height() + 6;
    int x = kRowPadX;
    shape.chevronRect = QRect(x, top, 12, metrics.height());
    x += 14;
    shape.titleRect = QRect(x, top, qMax(20, width - x - kRowPadX - (canAdd ? kAddWidth : 0)),
                            metrics.height());
    shape.addRect = canAdd ? QRect(width - kRowPadX - kAddWidth, top, kAddWidth, metrics.height())
                           : QRect();
    return shape;
}

// Paints the single list: a section header or one card per row. It reads the card from the model
// by id, so a row holds nothing but the id and a refill never copies card data into the view.
class RowDelegate final : public QStyledItemDelegate {
public:
    RowDelegate(const board::Model *model, const QList<board::Row> *rows, QListWidget *list)
        : QStyledItemDelegate(list), m_model(model), m_rows(rows), m_list(list)
    {
    }

    // The row's width comes from the list with room for its scrollbar kept whether or not the
    // scrollbar is showing: measured at one width and painted at another, the elision would be
    // computed for a row wider than the one drawn.
    int rowWidth() const
    {
        const int reserve = m_list->verticalScrollBar()->sizeHint().width() + 2;
        return qMax(120, m_list->width() - 2 * m_list->frameWidth() - reserve);
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const board::Row *row = rowAt(index.row());
        const int width = rowWidth();
        if (!row)
            return QSize(width, 0);
        if (row->kind == board::Row::Section)
            return QSize(width, sectionShape(option.font, width, index.row() == 0, false).height);
        const board::Card *card = m_model->card(row->cardId);
        if (!card)
            return QSize(width, 0);
        return QSize(width, cardShape(*card, row->showStatus, option.font, width, today()).height);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const board::Row *row = rowAt(index.row());
        if (!row)
            return;
        QRect rect = option.rect;
        rect.setWidth(qMin(rect.width(), rowWidth()));
        painter->save();
        painter->setClipRect(rect);
        painter->setRenderHint(QPainter::Antialiasing);
        if (row->kind == board::Row::Section)
            paintSection(painter, option, rect, *row, index.row() == 0);
        else
            paintCard(painter, option, rect, *row);
        painter->restore();
    }

    // The `+` of the header at `rowIndex`, given that row's rect, or an empty rect.
    QRect addRectOf(int rowIndex, const QRect &itemRect) const
    {
        const board::Row *row = rowAt(rowIndex);
        if (!row || row->kind != board::Row::Section || !adds(row->columnId))
            return QRect();
        return sectionShape(m_list->font(), rowWidth(), rowIndex == 0, true)
            .addRect.translated(itemRect.topLeft());
    }

    // Nothing is created straight into Done: a card gets there by being closed.
    std::function<bool(const QString &columnId)> adds = [](const QString &) { return true; };

private:
    static QDate today() { return QDate::currentDate(); }

    const board::Row *rowAt(int index) const
    {
        return index >= 0 && index < m_rows->size() ? &m_rows->at(index) : nullptr;
    }

    void paintSection(QPainter *painter, const QStyleOptionViewItem &option, const QRect &rect,
                      const board::Row &row, bool first) const
    {
        const bool canAdd = adds(row.columnId);
        const SectionShape shape = sectionShape(option.font, rect.width(), first, canAdd);
        const QPoint origin = rect.topLeft();
        const bool hover = option.state & QStyle::State_MouseOver;
        // The engraved rule over a section name is the board's hardware, not a border
        // (docs/SWITCHBOARD-AESTHETIC.md 3.1: brass is permitted on 1px rules): unlit brass on the
        // face, lit under the pointer, so exactly one rule in the pane is ever lit. On a theme with
        // `[flags] board_material = false` both tokens are the plain hairline pair.
        if (!first) {
            painter->setPen(QPen(hover ? theme::BoardMetal : theme::BoardMetalDim, 1.0));
            painter->drawLine(rect.left() + kRowPadX, origin.y() + 4,
                              rect.left() + rect.width() - kRowPadX, origin.y() + 4);
        }
        painter->setFont(smaller(option.font, 0.85));
        painter->setPen(hover ? theme::Text : theme::TextMuted);
        painter->drawText(shape.chevronRect.translated(origin), Qt::AlignLeft | Qt::AlignVCenter,
                          row.collapsed ? QStringLiteral("▸") : QStringLiteral("▾"));

        QFont title = smaller(option.font, 0.85);
        title.setWeight(QFont::DemiBold);
        title.setLetterSpacing(QFont::PercentageSpacing, 108);
        painter->setFont(title);
        painter->setPen(hover ? theme::Text : theme::TextMuted);
        const QString text = row.title.toUpper();
        const int titleWidth = QFontMetrics(title).horizontalAdvance(text);
        painter->drawText(shape.titleRect.translated(origin), Qt::AlignLeft | Qt::AlignVCenter, text);

        painter->setFont(monoFont(option.font, 0.8));
        painter->setPen(theme::TextMuted);
        painter->drawText(QRect(shape.titleRect.left() + titleWidth + 8 + origin.x(),
                                shape.titleRect.top() + origin.y(),
                                qMax(0, shape.titleRect.width() - titleWidth - 8),
                                shape.titleRect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, QString::number(row.count));
        if (canAdd && hover) {
            painter->setFont(smaller(option.font, 0.95));
            painter->setPen(theme::Text);
            painter->drawText(shape.addRect.translated(origin), Qt::AlignCenter,
                              QStringLiteral("+"));
        }
    }

    void paintCard(QPainter *painter, const QStyleOptionViewItem &option, const QRect &rect,
                   const board::Row &row) const
    {
        const board::Card *card = m_model->card(row.cardId);
        if (!card)
            return;
        const CardShape shape = cardShape(*card, row.showStatus, option.font, rect.width(), today());
        const QPoint origin = rect.topLeft();
        const bool selected = option.state & QStyle::State_Selected;
        const bool hover = option.state & QStyle::State_MouseOver;
        const bool focused = m_list->hasFocus();

        // A band, not a box: rows read as a list, and the selection is the one thing with an
        // edge — a 2px bar at the left, so it is visible without a fill loud enough to hurt.
        if (selected || hover) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? alpha(theme::Accent, focused ? 34 : 20)
                                       : mix(theme::BoardFace, theme::Text, 0.05));
            painter->drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        }
        if (selected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(focused ? theme::Accent : theme::BorderStrong);
            painter->drawRoundedRect(QRectF(rect.left() + 1, rect.top() + 3, 2.0,
                                            rect.height() - 6), 1, 1);
        }

        painter->setFont(option.font);
        painter->setPen(glyphInk(card->status));
        painter->drawText(shape.glyphRect.translated(origin), Qt::AlignCenter,
                          board::statusGlyph(card->status));

        painter->setPen(card->closed() ? theme::TextMuted : theme::Text);
        painter->drawText(shape.titleRect.translated(origin), Qt::AlignLeft | Qt::AlignVCenter,
                          shape.title);

        painter->setFont(monoFont(option.font, 0.85));
        painter->setPen(theme::TextMuted);
        painter->drawText(shape.idRect.translated(origin), Qt::AlignLeft | Qt::AlignVCenter,
                          card->reference());

        const QFont badgeFont = smaller(option.font, 0.85);
        painter->setFont(badgeFont);
        for (const auto &placed : shape.badges) {
            const QRect box = placed.second.translated(origin);
            const auto [ink, edge] = badgeInk(placed.first.kind);
            if (edge.isValid()) {
                painter->setPen(QPen(edge, 1.0));
                painter->setBrush(theme::Surface);
                painter->drawRoundedRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
            }
            painter->setPen(ink);
            painter->drawText(box, Qt::AlignCenter,
                              QFontMetrics(badgeFont).elidedText(placed.first.text, Qt::ElideRight,
                                                                 box.width() - 8));
        }
    }

    const board::Model *m_model;
    const QList<board::Row> *m_rows;
    QListWidget *m_list;
};

// The section checkboxes at the top of the list page have to wrap: a pane can be ~350 px wide and
// there are eight or nine of them. Qt ships no flow layout, so this is the minimal one — pack
// left to right, break when the next item would not fit, and report the height that takes.
class FlowLayout final : public QLayout {
public:
    FlowLayout(QWidget *parent, int horizontal, int vertical)
        : QLayout(parent), m_h(horizontal), m_v(vertical) {}
    ~FlowLayout() override
    {
        while (QLayoutItem *item = takeAt(0))
            delete item;
    }

    void addItem(QLayoutItem *item) override { m_items.append(item); }
    int count() const override { return int(m_items.size()); }
    QLayoutItem *itemAt(int index) const override { return m_items.value(index); }
    QLayoutItem *takeAt(int index) override
    {
        return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return place(QRect(0, 0, width, 0), true); }
    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        place(rect, false);
    }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override
    {
        QSize size;
        for (const QLayoutItem *item : m_items)
            size = size.expandedTo(item->minimumSize());
        const QMargins margins = contentsMargins();
        return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    }

private:
    int place(const QRect &rect, bool measureOnly) const
    {
        const QMargins margins = contentsMargins();
        const QRect area = rect.adjusted(margins.left(), margins.top(), -margins.right(),
                                         -margins.bottom());
        int x = area.x(), y = area.y(), lineHeight = 0;
        for (QLayoutItem *item : m_items) {
            const QSize hint = item->sizeHint();
            if (x + hint.width() > area.right() + 1 && lineHeight > 0) {
                x = area.x();
                y += lineHeight + m_v;
                lineHeight = 0;
            }
            if (!measureOnly)
                item->setGeometry(QRect(QPoint(x, y), hint));
            x += hint.width() + m_h;
            lineHeight = qMax(lineHeight, hint.height());
        }
        return y + lineHeight - rect.y() + margins.bottom();
    }

    QList<QLayoutItem *> m_items;
    int m_h, m_v;
};

}  // namespace

// The one list: section headers and cards in a single vertical scroll. It reports a drop instead
// of moving the row itself — the card only moves once the worker has written the file and sent
// board_changed back — and draws the line where the card would land.
class RowList final : public QListWidget {
public:
    explicit RowList(const QList<board::Row> *rows, QWidget *parent = nullptr)
        : QListWidget(parent), m_rows(rows)
    {
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::MoveAction);
        setDropIndicatorShown(false);    // drawn by paintEvent below, between the rows
        setSelectionMode(QAbstractItemView::SingleSelection);
        setUniformItemSizes(false);
        setResizeMode(QListView::Adjust);           // rows re-elide when the pane resizes
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setFrameShape(QFrame::NoFrame);
        setMouseTracking(true);
        viewport()->setAttribute(Qt::WA_Hover);
        setObjectName(QStringLiteral("boardList"));
    }

    QString placeholder;             // drawn when the list is empty
    // (card id, the section it lands in, the id it goes before, the id it goes after)
    std::function<void(const QString &, const QString &, const QString &, const QString &)> onDropped;
    std::function<void(bool)> onDragging;   // a drag from this list started (true) or ended
    std::function<void(const QString &columnId)> onToggleSection, onAddInSection;
    std::function<QRect(int rowIndex, const QRect &itemRect)> addRectOf;
    static QString dragging;

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QListWidget::resizeEvent(event);
        if (event->size().width() != event->oldSize().width())
            scheduleDelayedItemsLayout();   // the rows re-elide to the new width
    }

    void showEvent(QShowEvent *event) override
    {
        QListWidget::showEvent(event);
        scheduleDelayedItemsLayout();       // measured while hidden, at whatever width it had
    }

    // A click on a section header toggles it, or adds into it; it never becomes a selection.
    void mousePressEvent(QMouseEvent *event) override
    {
#if QT_VERSION_MAJOR >= 6
        const QPoint at = event->position().toPoint();
#else
        const QPoint at = event->pos();
#endif
        const QModelIndex index = indexAt(at);
        const board::Row *row = rowAt(index.row());
        if (row && row->kind == board::Row::Section && event->button() == Qt::LeftButton) {
            const QRect add = addRectOf ? addRectOf(index.row(), visualRect(index)) : QRect();
            if (add.isValid() && add.adjusted(-5, -5, 5, 5).contains(at)) {
                if (onAddInSection)
                    onAddInSection(row->columnId);
            } else if (onToggleSection) {
                onToggleSection(row->columnId);
            }
            event->accept();
            return;
        }
        QListWidget::mousePressEvent(event);
    }

    // Our own drag, not QListWidget's: the default image is the row painted on a transparent
    // pixmap, which without a compositor is a black box. This one is the row on the board's
    // background. The payload is the text `#ID`, so dropping a card on the prompt box types it.
    void startDrag(Qt::DropActions) override
    {
        QListWidgetItem *item = currentItem();
        if (!item || item->data(kCardRole).toString().isEmpty())
            return;
        dragging = item->data(kCardRole).toString();
        QRect rect = visualItemRect(item);
        rect.setWidth(qMin(rect.width(), qMax(260, width() / 2)));
        const qreal ratio = devicePixelRatioF();
        QPixmap pixmap(rect.size() * ratio);
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(theme::BoardFace);
        {
            QPainter painter(&pixmap);
            QStyleOptionViewItem option;
            option.initFrom(this);
            option.rect = QRect(QPoint(0, 0), rect.size());
            option.state |= QStyle::State_Selected;
            itemDelegate()->paint(&painter, option, indexFromItem(item));
        }
        auto *drag = new QDrag(this);
        auto *mime = new QMimeData;
        mime->setText(QStringLiteral("#") + dragging + QLatin1Char(' '));
        drag->setMimeData(mime);
        drag->setPixmap(pixmap);
        drag->setHotSpot(QPoint(qMin(40, rect.width() / 2), rect.height() / 2));
        if (onDragging)
            onDragging(true);
        drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);   // until dropped or cancelled
        dragging.clear();
        m_dropActive = false;
        m_dropRow = m_dropHeader = -1;
        viewport()->update();
        if (onDragging)
            onDragging(false);
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (dragging.isEmpty()) {   // only cards: a file or text dragged in is not a card
            event->ignore();
            return;
        }
        event->acceptProposedAction();
        m_dropActive = true;
        viewport()->update();
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (dragging.isEmpty()) {
            event->ignore();
            return;
        }
        event->acceptProposedAction();
#if QT_VERSION_MAJOR >= 6
        aimAt(event->position().toPoint());
#else
        aimAt(event->pos());
#endif
    }

    void dragLeaveEvent(QDragLeaveEvent *event) override
    {
        QListWidget::dragLeaveEvent(event);
        m_dropActive = false;
        m_dropRow = m_dropHeader = -1;
        viewport()->update();
    }

    void dropEvent(QDropEvent *event) override
    {
        const QString card = dragging;
        dragging.clear();
        m_dropActive = false;
        const int header = m_dropHeader, before = m_dropRow;
        m_dropHeader = m_dropRow = -1;
        viewport()->update();
        if (card.isEmpty()) {
            event->ignore();
            return;
        }
        // The model is the file on disk; nothing moves in the view until the worker says so.
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
        drop(card, header, before);
    }

    void paintEvent(QPaintEvent *event) override
    {
        QListWidget::paintEvent(event);
        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing);
        if (count() == 0 && !placeholder.isEmpty()) {
            painter.setPen(theme::TextMuted);
            painter.setFont(smaller(font(), 0.95));
            painter.drawText(viewport()->rect().adjusted(16, 24, -16, 0),
                             Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, placeholder);
        }
        if (!m_dropActive)
            return;
        if (m_dropHeader >= 0 && m_dropHeader < count()) {
            const QRect rect = visualItemRect(item(m_dropHeader));
            painter.setPen(QPen(alpha(theme::Accent, 130), 1.0));
            painter.setBrush(alpha(theme::Accent, 26));
            painter.drawRoundedRect(QRectF(rect).adjusted(2.5, 2.5, -2.5, -1.5), 5, 5);
            return;
        }
        int y = 1;
        if (count() > 0) {
            const int row = qBound(0, m_dropRow, count());
            y = row < count() ? visualItemRect(item(row)).top()
                              : visualItemRect(item(count() - 1)).bottom() + 1;
        }
        painter.setPen(QPen(theme::Accent, 2.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(kRowPadX, y), QPointF(viewport()->width() - kRowPadX, y));
    }

public:
    // Where a card dropped with `header` aimed at (or, at -1, between rows at `before`) goes.
    // Public so the pane's keyboard move and a test can take the same path as the mouse.
    void drop(const QString &card, int header, int before)
    {
        const auto [columnId, slot] =
            board::dropTarget(*m_rows, header >= 0 ? header + 1 : before);
        if (columnId.isEmpty())
            return;
        const QStringList order = board::cardsInSection(*m_rows, columnId);
        const int from = order.indexOf(card);
        const int target = from >= 0 && from < slot ? slot - 1 : slot;
        if (from >= 0 && target == from)
            return;   // dropped where it already was: no write, no thread entry
        const auto [beforeId, afterId] = board::placement(order, card, target);
        if (onDropped)
            onDropped(card, columnId, beforeId, afterId);
    }

private:
    const board::Row *rowAt(int index) const
    {
        return index >= 0 && index < m_rows->size() ? &m_rows->at(index) : nullptr;
    }

    // A pointer inside a section header aims at that section; anywhere else it aims between two
    // rows, and board::dropTarget decides which section that point belongs to.
    void aimAt(const QPoint &pos)
    {
        int header = -1, before = count();
        const QModelIndex index = indexAt(pos);
        const board::Row *row = rowAt(index.row());
        if (row && row->kind == board::Row::Section) {
            header = index.row();
        } else {
            for (int i = 0; i < count(); ++i) {
                if (pos.y() < visualItemRect(item(i)).center().y()) {
                    before = i;
                    break;
                }
            }
        }
        if (header == m_dropHeader && before == m_dropRow)
            return;
        m_dropHeader = header;
        m_dropRow = before;
        viewport()->update();
    }

    const QList<board::Row> *m_rows;
    bool m_dropActive = false;
    int m_dropRow = -1, m_dropHeader = -1;
};

QString RowList::dragging;


// The empty Switchboard: a board with nothing patched through (docs/SWITCHBOARD-AESTHETIC.md
// intervention 5, "DO"). One unlit jack per section over its engraved name, then the words the label
// was given. These rings are the only brass circles in Relay, and what lets them past the taste
// guard in §6 ("at most two brass circles and one cord in a default window") is that the widget
// carrying them disappears the moment there is a card.
//
// It is a QLabel so that setText(), setVisible() and hide() keep working on it unchanged; only the
// painting is its own.
class EmptyBoard final : public QLabel {
public:
    explicit EmptyBoard(QWidget *parent = nullptr) : QLabel(parent) {}

    // The section names, in board order. Empty until the first `board` event arrives, which is what
    // keeps the rings off "Loading the Switchboard…": an unpatched board is a fact, not a guess.
    void setSections(const QStringList &names)
    {
        if (names == m_names)
            return;
        m_names = names;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QStringList lines = text().split(QLatin1Char('\n'));
        const QFont body = theme::legible(font(), theme::SecondaryPt);
        const QFontMetrics metrics(body);
        QFont enamel = monoFont(font(), 0.8);        // uppercase, letter-spaced: an enamel label
        enamel.setLetterSpacing(QFont::AbsoluteSpacing, 1);
        const QFontMetrics enamelMetrics(enamel);

        constexpr int kRing = 14, kRingGap = 12, kNameGap = 7, kBandGap = 26;
        // The names go under the rings when they fit; a pane 300px wide with ten sections gets the
        // rings alone rather than a row of stubs. Whichever it is, nothing wraps and nothing is
        // half drawn: the columns that do not fit are simply not there.
        int column = 0;
        for (const QString &name : m_names)
            column = qMax(column, enamelMetrics.horizontalAdvance(name.toUpper()) + 10);
        column = qBound(kRing + kRingGap, column, 110);
        const int room = qMax(0, width() - 24);
        int count = m_names.size();
        bool named = column > 0 && room / qMax(1, column) >= 3;
        if (named)
            count = qMin(count, room / column);
        const int pitch = named ? column : kRing + kRingGap;
        const int bandHeight = m_names.isEmpty() ? 0 : kRing + (named ? kNameGap + enamelMetrics.height() : 0);
        int textHeight = 0;
        for (const QString &line : lines)
            textHeight += line.isEmpty() ? metrics.height() / 2 : metrics.lineSpacing();

        int y = qMax(12, (height() - (bandHeight + (bandHeight ? kBandGap : 0) + textHeight)) / 2);
        if (bandHeight) {
            int x = (width() - pitch * count) / 2;
            for (int i = 0; i < count; ++i) {
                // A jack: an unlit brass ring, the collar's shade inside it, and the hole itself
                // as a small dark disc — small, or on a light theme the hole swallows the ring and
                // the jack reads as a bullet. The hole is mixed out of whichever of the face and
                // the metal is already the darker, so it is darker than both in every theme.
                // Nothing here is ever lit: a board with no cards has no line patched through it.
                const QRectF ring(x + (pitch - kRing) / 2.0, y, kRing, kRing);
                const QColor deeper = qGray(theme::BoardFace.rgb()) <= qGray(theme::BoardMetalDim.rgb())
                                          ? theme::BoardFace : theme::BoardMetalDim;
                painter.setPen(QPen(theme::BoardMetalDim, 1.4));
                painter.setBrush(mix(theme::BoardFace, theme::BoardMetalDim, 0.15));
                painter.drawEllipse(ring);
                painter.setPen(Qt::NoPen);
                painter.setBrush(mix(deeper, QColor(Qt::black), 0.45));
                painter.drawEllipse(ring.center(), 2.6, 2.6);
                if (named) {
                    painter.setFont(enamel);
                    painter.setPen(theme::TextMuted);
                    const QString name = m_names.at(i).toUpper();
                    painter.drawText(QRect(x, y + kRing + kNameGap, pitch, enamelMetrics.height()),
                                     Qt::AlignCenter,
                                     enamelMetrics.elidedText(name, Qt::ElideRight, pitch - 4));
                }
                x += pitch;
            }
            y += bandHeight + kBandGap;
        }
        painter.setFont(body);
        painter.setPen(theme::TextMuted);
        for (const QString &line : lines) {
            if (line.isEmpty()) {
                y += metrics.height() / 2;
                continue;
            }
            painter.drawText(QRect(12, y, qMax(0, width() - 24), metrics.lineSpacing()), Qt::AlignCenter,
                             metrics.elidedText(line, Qt::ElideRight, qMax(0, width() - 24)));
            y += metrics.lineSpacing();
        }
    }

private:
    QStringList m_names;
};

// --------------------------------------------------------------------- card detail

// The right half of the Switchboard: the card as one document (body, then its thread, the way an
// issue page reads), a reply box under it, and the pickers and links in a header above it.
class CardDetail final : public QWidget {
public:
    explicit CardDetail(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("boardDetail"));
        setAttribute(Qt::WA_StyledBackground);
        setMinimumWidth(320);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(8);

        auto *top = new QHBoxLayout;
        top->setSpacing(6);
        m_ref = new QLabel(this);
        m_ref->setObjectName(QStringLiteral("boardCardRef"));
        top->addWidget(m_ref);
        top->addStretch();
        m_edit = textButton(QStringLiteral("Edit (e)"),
                            QStringLiteral("Edit the title and the issue text (e)"));
        m_toPrompt = textButton(QStringLiteral("#ID → prompt (t)"),
                                QStringLiteral("Insert this card's #ID in the terminal's prompt (t)"));
        m_openFile = textButton(QStringLiteral("Open file (o)"),
                                QStringLiteral("Open the card's Markdown file in a pane (o)"));
        top->addWidget(m_edit);
        top->addWidget(m_toPrompt);
        top->addWidget(m_openFile);
        m_close = new QToolButton(this);
        m_close->setObjectName(QStringLiteral("boardCardClose"));
        m_close->setText(QStringLiteral("×"));
        m_close->setToolTip(QStringLiteral("Close the card (Esc)"));
        m_close->setCursor(Qt::PointingHandCursor);
        top->addWidget(m_close);
        layout->addLayout(top);

        m_title = new QLabel(this);
        m_title->setWordWrap(true);
        m_title->setObjectName(QStringLiteral("boardCardTitle"));
        m_title->setCursor(Qt::IBeamCursor);
        m_title->setToolTip(QStringLiteral("Click to edit the title (e)"));
        m_title->installEventFilter(this);
        layout->addWidget(m_title);
        // The same line, as a field: editing swaps the two so the title never jumps.
        m_titleEdit = new QLineEdit(this);
        m_titleEdit->setObjectName(QStringLiteral("boardCardTitleEdit"));
        m_titleEdit->setPlaceholderText(QStringLiteral("Title — one line"));
        m_titleEdit->setToolTip(QStringLiteral("Enter saves, Esc cancels"));
        m_titleEdit->installEventFilter(this);
        m_titleEdit->hide();
        layout->addWidget(m_titleEdit);

        auto *pickers = new QHBoxLayout;
        pickers->setSpacing(6);
        m_status = new QComboBox(this);
        m_status->setObjectName(QStringLiteral("boardPicker"));
        m_status->setToolTip(QStringLiteral("Status: the column the card sits in"));
        m_tab = new QComboBox(this);
        m_tab->setObjectName(QStringLiteral("boardPicker"));
        m_tab->setToolTip(QStringLiteral("Category: the folder the card's file lives in"));
        pickers->addWidget(m_status);
        pickers->addWidget(m_tab);
        pickers->addStretch();
        layout->addLayout(pickers);

        m_meta = new QLabel(this);
        m_meta->setWordWrap(true);
        m_meta->setTextFormat(Qt::RichText);
        // Links only, no text selection: theme::polishWindow() renames every selectable QLabel
        // to `cwd`, which would take these labels out of their own stylesheet rules.
        m_meta->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        m_meta->setObjectName(QStringLiteral("boardCardMeta"));
        layout->addWidget(m_meta);

        // Cross-provider QA (#T71W): one line under the fields, in the same muted ink, naming the
        // verifier the worker recommends for this card and what it skipped to get there. It is
        // there only for a card in a QA lane whose `qa` block arrived with it.
        m_verifyLine = new QLabel(this);
        m_verifyLine->setWordWrap(true);
        m_verifyLine->setObjectName(QStringLiteral("boardCardVerifyLine"));
        m_verifyLine->hide();
        layout->addWidget(m_verifyLine);

        m_doc = new QTextBrowser(this);
        m_doc->setObjectName(QStringLiteral("boardCardDocument"));
        m_doc->setOpenLinks(false);      // a relative link would otherwise replace the card
        m_doc->document()->setDocumentMargin(12);
        m_doc->installEventFilter(this);
        m_doc->viewport()->installEventFilter(this);
        relay::installCopyOnSelect(m_doc);
        layout->addWidget(m_doc, 1);

        // Editing the card's own words (`## Issue`). It takes the document's place rather than
        // opening beside it, so the card is either being read or being written, never both.
        m_editFrame = new QFrame(this);
        m_editFrame->setObjectName(QStringLiteral("boardEdit"));
        auto *editLayout = new QVBoxLayout(m_editFrame);
        editLayout->setContentsMargins(8, 6, 8, 6);
        editLayout->setSpacing(4);
        auto *editHint = new QLabel(QStringLiteral("Issue — Ctrl+Enter saves, Esc cancels"), m_editFrame);
        editHint->setObjectName(QStringLiteral("boardEditHint"));
        editLayout->addWidget(editHint);
        m_issueEdit = new QPlainTextEdit(m_editFrame);
        m_issueEdit->setObjectName(QStringLiteral("boardIssueEditor"));
        m_issueEdit->setPlaceholderText(QStringLiteral("What the card is about, in your own words"));
        m_issueEdit->installEventFilter(this);
        editLayout->addWidget(m_issueEdit, 1);
        auto *editButtons = new QHBoxLayout;
        editButtons->setSpacing(6);
        editButtons->addStretch(1);
        m_cancelEdit = new QPushButton(QStringLiteral("Cancel (Esc)"), m_editFrame);
        m_cancelEdit->setObjectName(QStringLiteral("boardReplyButton"));
        m_cancelEdit->setToolTip(QStringLiteral("Leave the card as it is (Esc)"));
        m_saveEdit = new QPushButton(QStringLiteral("Save (Ctrl+Enter)"), m_editFrame);
        m_saveEdit->setObjectName(QStringLiteral("primary"));
        m_saveEdit->setToolTip(QStringLiteral("Write the title and the issue to the card file (Ctrl+Enter)"));
        editButtons->addWidget(m_cancelEdit);
        editButtons->addWidget(m_saveEdit);
        editLayout->addLayout(editButtons);
        m_editFrame->hide();
        layout->addWidget(m_editFrame, 1);

        m_error = new QLabel(this);
        m_error->setObjectName(QStringLiteral("boardCardError"));
        m_error->setWordWrap(true);
        m_error->hide();
        layout->addWidget(m_error);

        auto *reply = new QFrame(this);
        m_replyFrame = reply;
        reply->setObjectName(QStringLiteral("boardReply"));
        auto *replyLayout = new QVBoxLayout(reply);
        replyLayout->setContentsMargins(8, 6, 8, 6);
        replyLayout->setSpacing(4);
        m_reply = new RichEditor(reply);
        m_reply->setObjectName(QStringLiteral("boardReplyEditor"));
        m_reply->setPlaceholders({QStringLiteral("Reply — Enter discusses, Ctrl+Enter plans, Ctrl+Shift+Enter only comments"),
                                  QStringLiteral("Reply — Enter discusses, Ctrl+Enter plans"),
                                  QStringLiteral("Reply to this card…"), QStringLiteral("Reply…")});
        m_reply->setAutoHeight(2, 8);
        replyLayout->addWidget(m_reply);
        auto *buttons = new QHBoxLayout;
        buttons->setSpacing(6);
        buttons->addStretch(1);
        m_comment = new QPushButton(QStringLiteral("Comment (Ctrl+Shift+Enter)"), reply);
        m_comment->setObjectName(QStringLiteral("boardReplyButton"));
        m_comment->setToolTip(QStringLiteral("Append to the thread without calling a model (Ctrl+Shift+Enter)"));
        // The three things the agent can do with a card (#XS6Q, owner 2026-09-18): talk it
        // through and change it, write its plan, or take it to a terminal pane and build it.
        m_discuss = new QPushButton(QStringLiteral("Discuss (Enter)"), reply);
        m_discuss->setObjectName(QStringLiteral("primary"));
        m_plan = new QPushButton(QStringLiteral("Plan (p)"), reply);
        m_plan->setObjectName(QStringLiteral("boardReplyButton"));
        m_execute = new QPushButton(QStringLiteral("Execute (x)"), reply);
        m_execute->setObjectName(QStringLiteral("boardExecute"));
        // Verify (#T71W): the QA lane's counterpart of Execute — it also leaves the board for a
        // pane, so it wears the same outline, and it is on screen only while the card is in a QA
        // lane and the worker has said who should check it.
        m_verify = new QPushButton(QStringLiteral("Verify (v)"), reply);
        m_verify->setObjectName(QStringLiteral("boardExecute"));
        m_verify->hide();
        for (QPushButton *button : {m_comment, m_discuss, m_plan, m_execute, m_verify}) {
            button->setFocusPolicy(Qt::NoFocus);   // Tab stays between the reply box and the card
            button->setProperty("fullLabel", button->text());   // what fitButtons() shortens from
        }
        buttons->addWidget(m_comment);
        buttons->addWidget(m_discuss);
        buttons->addWidget(m_plan);
        buttons->addWidget(m_execute);
        buttons->addWidget(m_verify);
        setModeTips();
        replyLayout->addLayout(buttons);
        layout->addWidget(reply);

        m_render = new QTimer(this);
        m_render->setSingleShot(true);
        m_render->setInterval(40);   // a streamed answer re-renders at most 25 times a second
        connect(m_render, &QTimer::timeout, this, [this] { render(Scroll::Follow); });

        // A click is the slow path: each says its key once (WARP.md hint rule).
        connect(m_discuss, &QPushButton::clicked, this, [this] {
            if (stopIfBusy(QStringLiteral("discuss")))
                return;
            if (onModeHint)
                onModeHint(QStringLiteral("discuss"));
            submit(QStringLiteral("discuss"));
        });
        connect(m_plan, &QPushButton::clicked, this, [this] {
            if (stopIfBusy(QStringLiteral("plan")))
                return;
            if (onModeHint)
                onModeHint(QStringLiteral("plan"));
            plan();
        });
        connect(m_execute, &QPushButton::clicked, this, [this] {
            if (onModeHint)
                onModeHint(QStringLiteral("execute"));
            execute();
        });
        connect(m_verify, &QPushButton::clicked, this, [this] {
            if (onModeHint)
                onModeHint(QStringLiteral("verify"));
            verify();
        });
        connect(m_comment, &QPushButton::clicked, this, [this] { submit(QString()); });
        connect(m_edit, &QToolButton::clicked, this, [this] {
            if (onEditHint)
                onEditHint();
            beginEdit(false);
        });
        connect(m_saveEdit, &QPushButton::clicked, this, [this] { saveEdit(); });
        connect(m_cancelEdit, &QPushButton::clicked, this, [this] { cancelEdit(); });
        connect(m_close, &QToolButton::clicked, this, [this] { if (onClose) onClose(); });
        connect(m_toPrompt, &QToolButton::clicked, this, [this] { if (onToPrompt) onToPrompt(); });
        connect(m_openFile, &QToolButton::clicked, this, [this] { if (onOpenPath) onOpenPath(m_path); });
        // Ctrl+Shift+Enter is the composer's "terminal, never the model" chord; here it means
        // the same thing: a note on the thread with no model call.
        // Enter discusses, Ctrl+Enter plans (with what was typed as the owner's note).
        m_reply->onSubmit = [this](const QString &route) {
            if (route == QStringLiteral("shell"))
                submit(QString());
            else if (route == QStringLiteral("agent"))
                plan();
            else
                submit(QStringLiteral("discuss"));
        };
        m_reply->installEventFilter(this);
        connect(m_meta, &QLabel::linkActivated, this, [this](const QString &link) {
            if (onOpenPath)
                onOpenPath(link);
        });
        connect(m_doc, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
            if (!url.scheme().isEmpty() && url.scheme() != QStringLiteral("file")) {
                QDesktopServices::openUrl(url);
                return;
            }
            if (onOpenPath)
                onOpenPath(url.isLocalFile() ? url.toLocalFile() : url.path());
        });
        connect(m_status, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            if (!m_loading && onMove)
                onMove(QStringLiteral("status"), m_status->itemData(index).toString());
        });
        connect(m_tab, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            if (!m_loading && onMove)
                onMove(QStringLiteral("tab"), m_tab->itemData(index).toString());
        });
    }

    // `mode` is "discuss" or "plan" for an agent turn (protocol 19.10), empty for a plain comment.
    std::function<void(const QString &text, const QString &mode)> onReply;
    // Execute: hand the card to a terminal pane. `note` is what was in the reply box.
    std::function<void(const QString &note)> onExecute;
    // Verify (#T71W): hand the card to a terminal pane on the *recommended verifier*, which is a
    // different provider family from the one that implemented it. `note` is the reply box again.
    std::function<void(const QString &note)> onVerify;
    std::function<void(const QString &mode)> onModeHint;   // a mode button was clicked, not keyed
    std::function<void()> onClose, onCancel, onToPrompt, onEscape;
    std::function<void(const QString &what, const QString &value)> onMove;
    // A path relative to the workspace (the card file) or to the card (a link in its body).
    std::function<void(const QString &path)> onOpenPath;
    // A `board_update` patch and the hash the edit started from, so the worker can refuse a
    // write over a file that changed meanwhile. The GUI never writes the file itself.
    std::function<void(const QJsonObject &patch, const QString &baseHash)> onEdit;
    std::function<void()> onEditHint;        // the Edit button was clicked, not the key

    QString cardId() const { return m_id; }
    QString path() const { return m_path; }
    bool editing() const { return m_editing; }
    QString hash() const { return m_hash; }
    QString title() const { return m_title->text(); }
    QJsonObject front() const { return m_front; }
    QString status() const { return m_statusValue; }
    bool hasPlan() const { return m_sections.contains(QStringLiteral("Plan"), Qt::CaseInsensitive); }
    bool hasAcceptance() const { return !m_front.value(QStringLiteral("acceptance")).toString().trimmed().isEmpty(); }
    bool busy() const { return m_busy; }
    // The worker's `qa` block for this card, as it arrived (#T71W). Empty for a card it sent none
    // for — an old worker, or a card with no `implemented_by` yet.
    QJsonObject qa() const { return m_qa; }
    // Whether this card is in a QA lane at all: that, and a `qa` block, is what puts the verify
    // line and its button on screen.
    bool inQaLane() const { return m_statusValue.startsWith(QStringLiteral("needs-qa")); }
    // Whether there is something to open: the worker found a verifier that is not the implementer
    // and is actually on this machine.
    bool hasVerifier() const { return !board::verifyRunner(m_qa).isEmpty(); }

    // Plan: a turn that writes the card's `## Plan` (protocol 19.10). Words in the reply box go
    // with it as the owner's note; an empty box is fine — the card is the brief.
    void plan()
    {
        if (m_id.isEmpty() || m_editing || m_busy || !onReply)
            return;
        const QString text = m_reply->toPlainText().trimmed();
        if (!text.isEmpty())
            m_reply->remember(text);
        m_reply->clear();
        m_error->hide();
        onReply(text, QStringLiteral("plan"));
    }

    // Execute: hand the card to a terminal pane's agent. A card with neither a plan nor an
    // acceptance line asks once, here on the card rather than in a dialog: the pane's agent would
    // be working from the issue text alone. The second press (or `x`) goes ahead.
    void execute()
    {
        if (m_id.isEmpty() || m_editing || !onExecute)
            return;
        if (m_busy) {
            showError(QStringLiteral("The agent is still answering on #%1. Stop it, or wait for it, "
                                     "before handing the card to a pane.").arg(m_id));
            return;
        }
        if (!hasPlan() && !hasAcceptance() && m_executeArmed != m_id) {
            m_executeArmed = m_id;
            showError(QStringLiteral("#%1 has no plan and no acceptance yet, so the pane's agent "
                                     "would work from the issue alone. Execute again (x) to hand "
                                     "it over as it is, or Plan (p) first.").arg(m_id));
            return;
        }
        m_executeArmed.clear();
        const QString note = m_reply->toPlainText().trimmed();
        if (!note.isEmpty())
            m_reply->remember(note);
        m_reply->clear();
        m_error->hide();
        onExecute(note);
    }

    // Verify (#T71W): hand the card to a pane on the recommended verifier. Unlike Execute it
    // changes no status — the card stays in its QA lane until the verifier's verdict moves it —
    // and there is nothing to arm: the checklist on the card is the brief, and a card with no
    // recommendation says so rather than opening a pane on nobody.
    void verify()
    {
        if (m_id.isEmpty() || m_editing || !onVerify)
            return;
        if (m_busy) {
            showError(QStringLiteral("The agent is still answering on #%1. Stop it, or wait for it, "
                                     "before handing the card to a verifier.").arg(m_id));
            return;
        }
        if (!inQaLane()) {
            showError(QStringLiteral("#%1 is not in a QA lane yet, so there is nothing to verify. "
                                     "Move it to Needs QA (LLM) when it lands.").arg(m_id));
            return;
        }
        if (!hasVerifier()) {
            const QString why = board::verifyLine(m_qa);
            showError(why.isEmpty()
                          ? QStringLiteral("No verifier is available for #%1 yet.").arg(m_id)
                          : why);
            return;
        }
        const QString note = m_reply->toPlainText().trimmed();
        if (!note.isEmpty())
            m_reply->remember(note);
        m_reply->clear();
        m_error->hide();
        onVerify(note);
    }

    void setChoices(const QStringList &statuses, const QList<QPair<QString, QString>> &tabs)
    {
        m_loading = true;
        m_status->clear();
        for (const QString &status : statuses)
            m_status->addItem(board::statusTitle(status), status);
        m_tab->clear();
        for (const auto &tab : tabs)
            m_tab->addItem(tab.second, tab.first);
        m_loading = false;
    }

    void show(const QJsonObject &card)
    {
        m_loading = true;
        const QString id = card.value(QStringLiteral("card_id")).toString();
        const bool sameCard = id == m_id;
        if (!sameCard) {
            // Each card keeps its own unsent reply, so following the selection never carries a
            // draft over to the next card.
            if (!m_id.isEmpty())
                m_drafts.insert(m_id, m_reply->toPlainText());
            m_reply->setPlainText(m_drafts.take(id));
            m_error->hide();
            m_streaming.clear();
            m_executeArmed.clear();
        }
        m_id = id;
        m_path = card.value(QStringLiteral("path")).toString();
        const QJsonObject front = card.value(QStringLiteral("front")).toObject();
        m_front = front;
        m_statusValue = card.value(QStringLiteral("status")).toString();
        m_sections.clear();
        for (const QJsonValue &heading : card.value(QStringLiteral("sections")).toArray())
            m_sections << heading.toString();
        const QString title = card.value(QStringLiteral("title")).toString();
        // What an edit writes over, and what it would have to be saved against. A card read
        // again while it is being edited (the file changed under us, or our own write was
        // refused) hands the editor a new hash and says so, rather than throwing the text away.
        const QString hash = card.value(QStringLiteral("hash")).toString();
        const QString issue = card.value(QStringLiteral("issue")).toString();
        if (m_editing && sameCard && (title != m_title->text() || issue != m_issue))
            showError(QStringLiteral("This card changed on disk while you were editing it. "
                                     "Save writes your text over that version (the thread keeps "
                                     "the old one); Esc drops your edit."));
        m_hash = hash;
        m_issue = issue;
        if (!m_editing || !sameCard) {
            m_issueEdit->setPlainText(issue);
            m_titleEdit->setText(title);
        }
        m_ref->setText(QStringLiteral("#") + m_id);
        m_title->setText(title);
        const int status = m_status->findData(card.value(QStringLiteral("status")).toString());
        if (status >= 0)
            m_status->setCurrentIndex(status);
        const int tab = m_tab->findData(card.value(QStringLiteral("tab")).toString());
        if (tab >= 0)
            m_tab->setCurrentIndex(tab);
        m_tab->setVisible(m_tab->count() > 1);
        const QString meta = metaText(front, card.value(QStringLiteral("tasks")).toArray(), m_path);
        m_meta->setText(meta);
        m_meta->setVisible(!meta.isEmpty());
        m_qa = card.value(QStringLiteral("qa")).toObject();
        showVerify();
        m_openFile->setEnabled(!m_path.isEmpty());
        m_body = board::bodyWithoutTitle(card.value(QStringLiteral("body")).toString(), title);

        m_entries.clear();
        const QJsonArray thread = card.value(QStringLiteral("thread")).toArray();
        for (const QJsonValue &value : thread)
            m_entries << value.toObject();
        m_threadTotal = qMax(card.value(QStringLiteral("thread_total")).toInt(), int(m_entries.size()));
        render(sameCard ? Scroll::Keep : Scroll::Top);
        m_loading = false;
    }

    void appendEntry(const QJsonObject &entry)
    {
        m_streaming.clear();
        m_entries << entry;
        ++m_threadTotal;
        render(Scroll::Bottom);
    }

    void appendDelta(const QString &text)
    {
        m_streaming += text;
        if (!m_render->isActive())
            m_render->start();
    }

    // While a Discuss or a Plan runs, its own button is Stop and the other two wait.
    void setBusy(bool busy, const QString &mode = QString())
    {
        m_busy = busy;
        m_busyMode = busy ? (mode.isEmpty() ? QStringLiteral("discuss") : mode) : QString();
        setModeTips();
        if (!busy)
            m_streaming.clear();
        render(busy ? Scroll::Bottom : Scroll::Keep);
    }

    void showError(const QString &text)
    {
        // The line takes height from the document; keep the thread's end (the question that
        // failed) in view if that is where the reader was.
        QScrollBar *bar = m_doc->verticalScrollBar();
        const bool atBottom = bar->value() >= bar->maximum() - 8;
        m_error->setText(text);
        m_error->setVisible(!text.isEmpty());
        if (atBottom)
            QTimer::singleShot(0, m_doc, [bar] { bar->setValue(bar->maximum()); });
    }

    void focusReply() { m_reply->setFocus(); }
    void focusDocument() { m_doc->setFocus(); }
    // How much of the card's bottom is controls (the reply box, or the editor's Save row). The
    // board's notice floats over this widget when a card has the pane to itself, and a cleanup's
    // progress line stays up for minutes, so it has to be placed clear of them.
    int controlsHeight() const
    {
        const QFrame *bottom = m_editFrame->isHidden() ? m_replyFrame : m_editFrame;
        int height = bottom->isHidden() ? 0 : bottom->height();
        if (!m_error->isHidden())
            height += m_error->height() + 6;   // the refusal line belongs to them
        return height;
    }
    // An ask that was refused before it reached the model (19.9's busy rule): submit() has already
    // emptied the box, so the words go back into it rather than being lost with the refusal.
    void restoreReply(const QString &text)
    {
        if (m_reply->toPlainText().trimmed().isEmpty())
            m_reply->setPlainText(text);
        m_reply->setFocus();
    }

    // ---- editing the card's own words (the title and `## Issue`)
    //
    // Both at once and in one write, because they are one thought: the card says what the issue
    // is, and its title is that in a line. The document becomes the editor, the reply box stands
    // down, and Save sends one `board_update` patch hash-checked against the file we read.
    void beginEdit(bool titleFirst)
    {
        if (m_id.isEmpty())
            return;
        if (!m_editing) {
            m_editing = true;
            m_titleEdit->setText(m_title->text());
            m_issueEdit->setPlainText(m_issue);
            m_title->hide();
            m_titleEdit->show();
            m_doc->hide();
            m_editFrame->show();
            m_replyFrame->hide();
            m_edit->setEnabled(false);
            m_error->hide();
        }
        if (titleFirst) {
            m_titleEdit->setFocus();
            m_titleEdit->selectAll();
        } else {
            m_issueEdit->setFocus();
            m_issueEdit->moveCursor(QTextCursor::End);
        }
    }

    // Leave edit mode: after a save the worker took, or on Esc / Cancel.
    void endEdit()
    {
        if (!m_editing)
            return;
        m_editing = false;
        m_titleEdit->hide();
        m_title->show();
        m_editFrame->hide();
        m_doc->show();
        m_replyFrame->show();
        m_edit->setEnabled(true);
        m_error->hide();
        m_doc->setFocus();
    }

    void cancelEdit()
    {
        m_titleEdit->setText(m_title->text());
        m_issueEdit->setPlainText(m_issue);
        endEdit();
    }

    void saveEdit()
    {
        if (!m_editing || !onEdit)
            return;
        const QString title = m_titleEdit->text().trimmed();
        const QString issue = m_issueEdit->toPlainText().trimmed();
        if (title.isEmpty()) {
            showError(QStringLiteral("A card needs a title."));
            m_titleEdit->setFocus();
            return;
        }
        QJsonObject patch;
        if (title != m_title->text())
            patch.insert(QStringLiteral("title"), title);
        if (issue != m_issue.trimmed())
            patch.insert(QStringLiteral("replace_section"),
                         QJsonObject{{QStringLiteral("heading"), board::issueHeading()},
                                     {QStringLiteral("text"), issue}});
        if (patch.isEmpty()) {          // nothing was changed: the same as cancelling
            endEdit();
            return;
        }
        onEdit(patch, m_hash);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        fitButtons();
    }

    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == m_reply && event->type() == QEvent::KeyPress
            && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            if (onEscape)
                onEscape();
            return true;
        }
        // Click the title, or double-click the text, to edit them; `e` on the document does the
        // same from the keyboard. Esc anywhere in the editor leaves the card as it was.
        if (object == m_title && event->type() == QEvent::MouseButtonRelease && !m_editing
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            if (onEditHint)
                onEditHint();
            beginEdit(true);
            return true;
        }
        // `m_doc` is still null while the widgets above it are being built and their first
        // show() runs through this filter.
        if (m_doc && object == m_doc->viewport() && event->type() == QEvent::MouseButtonDblClick
            && !m_editing && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            if (onEditHint)
                onEditHint();
            beginEdit(false);
            return true;
        }
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            const auto mods = key->modifiers() & ~Qt::KeypadModifier;
            const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
            if (object == m_doc && !m_editing && mods == Qt::NoModifier
                && key->text() == QStringLiteral("e")) {
                beginEdit(false);
                return true;
            }
            // `p` plans, `x` executes, `d` goes to the reply box to discuss (#XS6Q).
            if (object == m_doc && !m_editing && mods == Qt::NoModifier) {
                if (key->text() == QStringLiteral("p")) {
                    plan();
                    return true;
                }
                if (key->text() == QStringLiteral("x")) {
                    execute();
                    return true;
                }
                if (key->text() == QStringLiteral("v")) {   // #T71W
                    verify();
                    return true;
                }
                if (key->text() == QStringLiteral("d")) {
                    m_reply->setFocus();
                    return true;
                }
            }
            if (m_editing && (object == m_titleEdit || object == m_issueEdit)) {
                if (key->key() == Qt::Key_Escape) {
                    cancelEdit();
                    return true;
                }
                // One line saves on Enter, as the quick-add field does; the issue text needs
                // Enter for its own newlines, so there it is Ctrl+Enter.
                if (enter && (mods == Qt::ControlModifier
                              || (object == m_titleEdit && mods == Qt::NoModifier))) {
                    saveEdit();
                    return true;
                }
                if (object == m_titleEdit && key->key() == Qt::Key_Down && mods == Qt::NoModifier) {
                    m_issueEdit->setFocus();
                    return true;
                }
            }
        }
        return QWidget::eventFilter(object, event);
    }

private:
    enum class Scroll { Top, Keep, Follow, Bottom };

    static QToolButton *textButton(const QString &text, const QString &tip)
    {
        auto *button = new QToolButton;
        button->setObjectName(QStringLiteral("boardTextButton"));
        button->setText(text);
        button->setToolTip(tip);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    }

    void submit(const QString &mode)
    {
        const QString text = m_reply->toPlainText().trimmed();
        if (text.isEmpty() || !onReply || (!mode.isEmpty() && m_busy))
            return;
        m_reply->remember(text);
        m_reply->clear();
        m_error->hide();
        onReply(text, mode);
    }

    // The running mode's button stops it; the others are off until it is done.
    bool stopIfBusy(const QString &mode)
    {
        if (!m_busy)
            return false;
        if (mode == m_busyMode && onCancel)
            onCancel();
        return true;
    }

    void setModeTips()
    {
        const auto set = [this](QPushButton *button, const QString &mode, const QString &label,
                                const QString &tip) {
            const bool running = m_busy && m_busyMode == mode;
            setLabel(button, running ? QStringLiteral("Stop") : label);
            button->setToolTip(running ? QStringLiteral("Stop the Switchboard agent") : tip);
            button->setEnabled(!m_busy || running);
        };
        set(m_discuss, QStringLiteral("discuss"), QStringLiteral("Discuss (Enter)"),
            QStringLiteral("Talk the card through with the agent; it may edit the title, the issue, "
                           "the labels or the status as you go (Enter)"));
        set(m_plan, QStringLiteral("plan"), QStringLiteral("Plan (p)"),
            QStringLiteral("The agent reads the code and writes the card's plan; it changes no code "
                           "and no other card. Anything typed goes with it (p, or Ctrl+Enter)"));
        m_execute->setEnabled(!m_busy);
        m_execute->setToolTip(QStringLiteral("Hand the card to a new terminal pane beside the board: "
                                             "its agent builds it, and the card moves to In progress (x)"));
        m_verify->setEnabled(!m_busy && hasVerifier());
        fitButtons();
    }

    // A reply-row label without its key: "Discuss (Enter)" -> "Discuss". The key does not vanish —
    // it is in the button's tooltip and in the pane's key legend either way.
    static QString labelWithoutKey(const QString &label)
    {
        const int at = label.lastIndexOf(QStringLiteral(" ("));
        return at > 0 && label.endsWith(QLatin1Char(')')) ? label.left(at) : label;
    }

    // Set a reply-row label and remember it at full length, so shortening is never one-way.
    static void setLabel(QPushButton *button, const QString &text)
    {
        button->setProperty("fullLabel", text);
        button->setText(text);
    }

    // The reply row carries each key in its label (#QG60: "Discuss (Enter)"). Five of those do not
    // fit a narrow card — CardDetail's own minimum width lets the layout squeeze a button below its
    // size hint, and the label then paints cut off at both ends — so when the row is tight the keys
    // drop out of the labels first, the way a card row drops its decorative badges before its
    // meaning (board::fitBadges). Only while the card is actually on screen: a width nothing has
    // laid out yet says nothing about what fits.
    void fitButtons()
    {
        if (!isVisible() || !m_replyFrame || m_replyFrame->isHidden())
            return;
        const QList<QPushButton *> row{m_comment, m_discuss, m_plan, m_execute, m_verify};
        const int spacing = 6, margins = 16;
        int wide = 0, shown = 0;
        for (QPushButton *button : row) {
            if (button->isHidden())
                continue;
            const QString label = button->property("fullLabel").toString();
            const QString was = button->text();
            button->setText(label);
            wide += button->sizeHint().width();
            button->setText(was);
            ++shown;
        }
        const bool keys = wide + qMax(0, shown - 1) * spacing <= m_replyFrame->width() - margins;
        for (QPushButton *button : row) {
            const QString label = button->property("fullLabel").toString();
            if (label.isEmpty())
                continue;
            const QString wanted = keys ? label : labelWithoutKey(label);
            if (button->text() != wanted)
                button->setText(wanted);
        }
    }

    // The verify line and its button, from the `qa` block the card arrived with (#T71W). Both are
    // on screen only while the card is in a QA lane: on any other card the recommendation would be
    // an answer to a question nobody has asked yet.
    void showVerify()
    {
        const QString line = inQaLane() ? board::verifyLine(m_qa) : QString();
        const QString note = inQaLane() ? board::verifyNote(m_qa) : QString();
        // Amber is "a human should look at this" everywhere in Relay, and that is exactly what a
        // missing verifier or a same-lineage one is. With a recommendation the line itself stays
        // in the fields' muted ink and only the warning is amber; with none, the whole line is.
        const QString ink = hasVerifier() ? theme::TextMuted.name() : theme::Warning.name();
        QString html = line.isEmpty()
                           ? QString()
                           : QStringLiteral("<span style=\"color:%1\">%2</span>")
                                 .arg(ink, line.toHtmlEscaped());
        if (!html.isEmpty() && hasVerifier() && !note.isEmpty())
            html += QStringLiteral(" <span style=\"color:%1\">· %2</span>")
                        .arg(theme::Warning.name(), note.toHtmlEscaped());
        m_verifyLine->setText(html);
        m_verifyLine->setVisible(!html.isEmpty());
        m_verify->setVisible(inQaLane());
        m_verify->setEnabled(!m_busy && hasVerifier());
        const QString label = board::verifyLabel(m_qa);
        m_verify->setToolTip(hasVerifier()
                                 ? QStringLiteral("Hand the card to a new terminal pane on %1, from "
                                                  "a different provider family than the one that "
                                                  "implemented it; it runs the QA checklist (v)")
                                       .arg(label)
                                 : QStringLiteral("No verifier is available for this card: %1")
                                       .arg(!note.isEmpty() ? note
                                            : line.isEmpty() ? QStringLiteral("the board has not said who should check it")
                                                             : line));
        fitButtons();
    }

    // Two short lines of facts under the pickers, keys muted, the file a link that opens it.
    static QString metaText(const QJsonObject &front, const QJsonArray &tasks, const QString &path)
    {
        QStringList parts;
        const auto item = [](const QString &key, const QString &value) {
            return QStringLiteral("<span style=\"color:%1\">%2</span>&nbsp;%3")
                .arg(theme::TextMuted.name(), key.toHtmlEscaped(), value.toHtmlEscaped());
        };
        const auto add = [&](const char *key, const QString &label) {
            const QJsonValue value = front.value(QLatin1String(key));
            if (value.isString() && !value.toString().isEmpty())
                parts << item(label, value.toString());
            else if (value.isArray() && !value.toArray().isEmpty()) {
                QStringList items;
                for (const QJsonValue &entry : value.toArray())
                    items << entry.toString();
                parts << item(label, items.join(QStringLiteral(", ")));
            }
        };
        add("labels", QStringLiteral("labels"));
        add("assignee", QStringLiteral("assignee"));
        add("waiting_on", QStringLiteral("waiting on"));
        add("milestone", QStringLiteral("milestone"));
        add("component", QStringLiteral("component"));
        add("implemented_by", QStringLiteral("implemented by"));
        // Who closed it out of a QA lane, stamped by the worker (#T71W). Beside the implementer,
        // because the pair is the point: two different models.
        add("verified_by", QStringLiteral("verified by"));
        if (!tasks.isEmpty()) {
            int done = 0;
            for (const QJsonValue &task : tasks)
                done += task.toObject().value(QStringLiteral("done")).toBool() ? 1 : 0;
            parts << item(QStringLiteral("tasks"), QStringLiteral("%1/%2").arg(done).arg(tasks.size()));
        }
        const QJsonObject links = front.value(QStringLiteral("links")).toObject();
        for (auto it = links.begin(); it != links.end(); ++it) {
            if (!it.value().isArray() || it.value().toArray().isEmpty())
                continue;
            QStringList items;
            for (const QJsonValue &entry : it.value().toArray())
                items << entry.toString();
            parts << item(it.key(), items.join(QStringLiteral(", ")));
        }
        QString html = parts.join(QStringLiteral(" &nbsp;·&nbsp; "));
        const QString acceptance = front.value(QStringLiteral("acceptance")).toString();
        if (!acceptance.isEmpty())
            html += (html.isEmpty() ? QString() : QStringLiteral("<br>"))
                    + item(QStringLiteral("acceptance"), acceptance);
        if (!path.isEmpty())
            html += (html.isEmpty() ? QString() : QStringLiteral("<br>"))
                    + QStringLiteral("<a href=\"%1\" style=\"color:%2\">%3</a>")
                          .arg(path.toHtmlEscaped(), theme::TextMuted.name(), path.toHtmlEscaped());
        return html;
    }

    // Markdown headings come out of QTextDocument at browser sizes (an H1 is ~2x the text), which
    // shouts inside a side panel. Bring them down to a document scale and give them air above.
    static void tuneHeadings(QTextDocument *doc, qreal base)
    {
        static const qreal scale[] = {1.0, 1.3, 1.15, 1.05, 1.0, 1.0, 1.0};
        for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
            const int level = block.blockFormat().headingLevel();
            if (level <= 0)
                continue;
            QTextCursor cursor(block);
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            QTextCharFormat format;
            format.setProperty(QTextFormat::FontSizeAdjustment, 0);
            format.setFontPointSize(base * scale[qMin(level, 6)]);
            format.setFontWeight(QFont::DemiBold);
            cursor.mergeCharFormat(format);
            QTextBlockFormat blockFormat = block.blockFormat();
            blockFormat.setTopMargin(level <= 2 ? 14 : 10);
            blockFormat.setBottomMargin(4);
            cursor.setBlockFormat(blockFormat);
        }
    }

    void insertMarkdown(QTextCursor &cursor, const QString &markdown, qreal base)
    {
        QTextDocument part;
        part.setDefaultFont(m_doc->document()->defaultFont());
        part.setMarkdown(markdown);
        tuneHeadings(&part, base);
        cursor.insertFragment(QTextDocumentFragment(&part));
    }

    void insertLine(QTextCursor &cursor, const QString &text, const QTextCharFormat &format,
                    int topMargin)
    {
        QTextBlockFormat block;
        block.setTopMargin(topMargin);
        block.setBottomMargin(2);
        cursor.insertBlock(block, format);
        cursor.insertText(text, format);
    }

    // The body, then the thread under a small heading. Events (moves, status changes) are one
    // muted line each; comments get an author line and their Markdown.
    void render(Scroll scroll)
    {
        QScrollBar *bar = m_doc->verticalScrollBar();
        const int was = bar->value();
        const bool atBottom = was >= bar->maximum() - 8;
        QTextDocument *doc = m_doc->document();
        const qreal base = m_doc->font().pointSizeF() > 0 ? m_doc->font().pointSizeF() : 10.0;
        doc->setMarkdown(m_body.trimmed().isEmpty() ? QStringLiteral("*No description.*") : m_body);
        tuneHeadings(doc, base);

        QTextCursor cursor(doc);
        cursor.movePosition(QTextCursor::End);
        QTextCharFormat muted;
        muted.setForeground(theme::TextMuted);
        muted.setFontPointSize(qMax(theme::FloorPt, base * 0.9));
        QTextCharFormat heading = muted;
        heading.setFontWeight(QFont::DemiBold);
        heading.setFontLetterSpacing(105);
        const int shown = int(m_entries.size());
        QString threadTitle = m_threadTotal > 0 ? QStringLiteral("THREAD · %1").arg(m_threadTotal)
                                                : QStringLiteral("THREAD");
        if (m_threadTotal > shown)
            threadTitle += QStringLiteral("  (last %1)").arg(shown);
        insertLine(cursor, threadTitle, heading, 22);
        if (m_entries.isEmpty() && !m_busy)
            insertLine(cursor, QStringLiteral("No replies yet. Discuss the card with the agent, have "
                                              "it Plan the work, Execute it in a terminal pane, or "
                                              "leave a comment for whoever picks it up."), muted, 4);

        const QDateTime now = QDateTime::currentDateTimeUtc();
        for (const QJsonObject &entry : std::as_const(m_entries)) {
            const QJsonObject attrs = entry.value(QStringLiteral("attrs")).toObject();
            const QString author = entry.value(QStringLiteral("author")).toString(
                attrs.value(QStringLiteral("author")).toString());
            const QString kind = entry.value(QStringLiteral("kind")).toString(
                attrs.value(QStringLiteral("kind")).toString());
            const QString text = entry.value(QStringLiteral("text")).toString().trimmed();
            const QString age = board::entryAge(entry.value(QStringLiteral("entry_id")).toString(), now);
            if (kind == QStringLiteral("event")) {
                QString line = text;
                if (line.startsWith(QStringLiteral("- ")))
                    line = line.mid(2);
                insertLine(cursor, line + (age.isEmpty() ? QString() : QStringLiteral("  · ") + age),
                           muted, 8);
                continue;
            }
            QTextCharFormat who;
            who.setFontWeight(QFont::DemiBold);
            who.setForeground(author == QStringLiteral("agent") ? theme::Agent : theme::Text);
            insertLine(cursor, author == QStringLiteral("agent") ? QStringLiteral("✦ agent") : author,
                       who, 14);
            QStringList extra;
            const QString model = attrs.value(QStringLiteral("model")).toString();
            // The mode first, so the history reads "Plan · …", "Discuss · …" (#XS6Q).
            const QString mode = board::modeTitle(attrs.value(QStringLiteral("mode")).toString(
                entry.value(QStringLiteral("mode")).toString()));
            if (!mode.isEmpty())
                extra << mode;
            if (!model.isEmpty())
                extra << model;
            if (!kind.isEmpty() && kind != QStringLiteral("comment"))
                extra << kind;
            if (!age.isEmpty())
                extra << age;
            if (!extra.isEmpty())
                cursor.insertText(QStringLiteral("  ") + extra.join(QStringLiteral(" · ")), muted);
            cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
            insertMarkdown(cursor, board::threadMarkdown(text, kind), base);
        }
        if (m_busy || !m_streaming.isEmpty()) {
            QTextCharFormat who;
            who.setFontWeight(QFont::DemiBold);
            who.setForeground(theme::Agent);
            insertLine(cursor, QStringLiteral("✦ agent"), who, 14);
            if (const QString mode = board::modeTitle(m_busyMode); m_busy && !mode.isEmpty())
                cursor.insertText(QStringLiteral("  ") + mode, muted);
            cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
            if (m_streaming.isEmpty()) {
                QTextCharFormat thinking = muted;
                thinking.setFontItalic(true);
                cursor.insertText(QStringLiteral("thinking…"), thinking);
            } else {
                insertMarkdown(cursor, m_streaming, base);
            }
        }

        switch (scroll) {
        case Scroll::Top:
            // Also once the new document is laid out: a card opened while the panel was hidden
            // is laid out on show, and that would otherwise leave it scrolled part way down.
            m_doc->moveCursor(QTextCursor::Start);
            bar->setValue(0);
            QTimer::singleShot(0, m_doc, [bar] { bar->setValue(0); });
            break;
        case Scroll::Keep:
            bar->setValue(was);
            break;
        case Scroll::Follow:
            bar->setValue(atBottom ? bar->maximum() : was);
            break;
        case Scroll::Bottom:
            bar->setValue(bar->maximum());
            break;
        }
    }

    QLabel *m_ref = nullptr, *m_title = nullptr, *m_meta = nullptr, *m_error = nullptr;
    QLabel *m_verifyLine = nullptr;   // the cross-provider QA recommendation (#T71W)
    QComboBox *m_status = nullptr, *m_tab = nullptr;
    QToolButton *m_close = nullptr, *m_toPrompt = nullptr, *m_openFile = nullptr, *m_edit = nullptr;
    QTextBrowser *m_doc = nullptr;
    RichEditor *m_reply = nullptr;
    QPushButton *m_discuss = nullptr, *m_plan = nullptr, *m_execute = nullptr, *m_comment = nullptr;
    QPushButton *m_verify = nullptr;
    QFrame *m_replyFrame = nullptr, *m_editFrame = nullptr;
    QLineEdit *m_titleEdit = nullptr;
    QPlainTextEdit *m_issueEdit = nullptr;
    QPushButton *m_saveEdit = nullptr, *m_cancelEdit = nullptr;
    QTimer *m_render = nullptr;
    QList<QJsonObject> m_entries;
    QHash<QString, QString> m_drafts;
    QString m_body, m_streaming, m_id, m_path;
    // The card as the worker last handed it over: the hash an edit is written against, and the
    // `## Issue` text an edit starts from and is compared with.
    QString m_hash, m_issue;
    QJsonObject m_front;
    QJsonObject m_qa;                 // the worker's verifier recommendation for this card (#T71W)
    QString m_statusValue;
    QStringList m_sections;           // the body's `## ` headings, for "has it a plan?"
    QString m_busyMode;               // "discuss" or "plan" while a turn runs
    QString m_executeArmed;           // the card that was warned it has no plan or acceptance
    int m_threadTotal = 0;
    bool m_loading = false, m_busy = false, m_editing = false;
};

// ------------------------------------------------------------------------ the view

BoardView::BoardView(const QString &workspace, QWidget *parent)
    : QWidget(parent), m_workspace(workspace)
{
    setObjectName(QStringLiteral("boardView"));
    m_requestPrefix = QStringLiteral("sb%1-").arg(quintptr(this), 0, 36);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    buildChrome(layout);
    setFocusPolicy(Qt::StrongFocus);
    watchIssues();
}

void BoardView::buildChrome(QVBoxLayout *layout)
{
    // The pane's header row. There are no tabs (owner decision, 2026-09-18) and, since
    // 2026-09-18, no tools either: the filter, "+ New card" and the section checkboxes are the
    // top of the *list page* (buildListTools), so a card that is open is not also looking at the
    // list's controls. All this row ever holds is the way back, and it is hidden while the list
    // is on screen.
    m_head = new QWidget(this);
    m_head->setObjectName(QStringLiteral("boardHead"));
    auto *headLayout = new QVBoxLayout(m_head);
    headLayout->setContentsMargins(8, 5, 8, 6);
    headLayout->setSpacing(6);
    m_tools = new QHBoxLayout;
    m_tools->setContentsMargins(0, 0, 0, 0);
    m_tools->setSpacing(6);
    m_back = new QToolButton(m_head);
    m_back->setObjectName(QStringLiteral("boardBack"));
    m_back->setText(QStringLiteral("←  Back to board (Esc)"));
    m_back->setToolTip(QStringLiteral("Close the card and go back to the list (Esc)"));
    m_back->setCursor(Qt::PointingHandCursor);
    m_back->setFocusPolicy(Qt::NoFocus);
    m_tools->addWidget(m_back);
    m_tools->addStretch(1);
    headLayout->addLayout(m_tools);
    m_head->hide();                 // shown only while a card is open
    layout->addWidget(m_head);
    connect(m_back, &QToolButton::clicked, this, [this] {
        if (onHint)
            onHint(QStringLiteral("board.back"), QStringLiteral("Esc"));
        closeDetail();
    });

    m_problems = new QLabel(this);
    m_problems->setMinimumWidth(1);   // one line, clipped in a narrow pane; the tooltip has it all
    m_problems->setObjectName(QStringLiteral("boardProblems"));
    m_problems->setTextFormat(Qt::RichText);
    m_problems->hide();
    connect(m_problems, &QLabel::linkActivated, this, [this](const QString &path) {
        if (onOpenFile && !path.isEmpty())
            onOpenFile(QDir(m_workspace).absoluteFilePath(path));
    });
    // It is added to the list page's tools below, not here: with the pane's header empty while the
    // list is up, a problems line at this level would be the topmost row and the pane's hover
    // buttons would sit on its text.

    m_notice = new QFrame(this);
    m_notice->setObjectName(QStringLiteral("boardNotice"));
    m_notice->setAttribute(Qt::WA_StyledBackground);
    auto *noticeLayout = new QHBoxLayout(m_notice);
    noticeLayout->setContentsMargins(10, 4, 4, 4);
    noticeLayout->setSpacing(6);
    m_noticeText = new QLabel(m_notice);
    m_noticeText->setObjectName(QStringLiteral("boardNoticeText"));
    m_noticeText->setWordWrap(true);
    noticeLayout->addWidget(m_noticeText, 1);
    m_noticeUndo = new QToolButton(m_notice);
    m_noticeUndo->setObjectName(QStringLiteral("boardTextButton"));
    m_noticeUndo->setText(QStringLiteral("Undo (Ctrl+Z)"));
    m_noticeUndo->setToolTip(QStringLiteral("Put it back (Ctrl+Z)"));
    m_noticeUndo->setCursor(Qt::PointingHandCursor);
    m_noticeUndo->setFocusPolicy(Qt::NoFocus);
    noticeLayout->addWidget(m_noticeUndo);
    auto *dismiss = new QToolButton(m_notice);
    dismiss->setObjectName(QStringLiteral("boardTextButton"));
    dismiss->setText(QStringLiteral("×"));
    dismiss->setFocusPolicy(Qt::NoFocus);
    noticeLayout->addWidget(dismiss);
    // It floats over the bottom of the board rather than taking a row: a notice that pushed the
    // columns down would move every card under the mouse, and again when it went away.
    m_notice->hide();
    m_noticeTimer = new QTimer(this);
    m_noticeTimer->setSingleShot(true);
    connect(m_noticeTimer, &QTimer::timeout, m_notice, &QWidget::hide);
    connect(dismiss, &QToolButton::clicked, m_notice, &QWidget::hide);
    connect(m_noticeUndo, &QToolButton::clicked, this, [this] { undoLast(); });

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    // The left half: the quick-add field (hidden until used) over the one scrolling list.
    m_listPane = new QWidget(m_splitter);
    m_listPane->setObjectName(QStringLiteral("boardListPane"));
    m_listPane->setMinimumWidth(240);
    m_listPane->installEventFilter(this);   // its width decides whether the tools row wraps
    auto *listLayout = new QVBoxLayout(m_listPane);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);
    buildListTools(listLayout);
    buildCleanupPanel(listLayout);
    buildQuickAdd(listLayout);
    m_list = new RowList(&m_rows, m_listPane);
    auto *delegate = new RowDelegate(&m_model, &m_rows, m_list);
    delegate->adds = [this](const QString &columnId) { return sectionTakesNewCards(columnId); };
    m_list->setItemDelegate(delegate);
    m_list->addRectOf = [delegate](int rowIndex, const QRect &itemRect) {
        return delegate->addRectOf(rowIndex, itemRect);
    };
    m_list->onToggleSection = [this](const QString &columnId) { toggleSection(columnId); };
    m_list->onAddInSection = [this](const QString &columnId) {
        if (onHint)
            onHint(QStringLiteral("board.quickAdd"), QStringLiteral("n"));
        quickAddIn(columnId);
    };
    m_list->onDropped = [this](const QString &card, const QString &columnId, const QString &before,
                               const QString &after) {
        if (onHint)
            onHint(QStringLiteral("board.drag"), QStringLiteral("Alt+Shift+Arrows"));
        m_selected = card;
        moveCard(card, columnId, before, after);
    };
    m_list->onDragging = [this](bool on) {
        m_dragActive = on;
        if (on) {
            m_dragScroll->start();
            return;
        }
        m_dragScroll->stop();
        if (m_rebuildPending) {
            // Not from inside startDrag(): a rebuild there would run under its nested loop.
            QTimer::singleShot(0, this, [this] {
                if (m_rebuildPending && !m_dragActive)
                    rebuild();
            });
        }
    };
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this] {
        auto *item = m_list->currentItem();
        if (!item || !item->isSelected())
            return;
        const QString id = item->data(kCardRole).toString();
        if (id.isEmpty())
            return;
        m_selected = id;
        if (detailOpen())
            m_follow->start();
    });
    // A click opens the card, the way a card board did; a drag never counts as a click.
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString id = item->data(kCardRole).toString();
        if (id.isEmpty() || QApplication::keyboardModifiers() != Qt::NoModifier)
            return;
        m_selected = id;
        openSelected();
    });
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        const QString id = item->data(kCardRole).toString();
        if (id.isEmpty())
            return;
        m_selected = id;
        openSelected();
    });
    m_list->installEventFilter(this);
    listLayout->addWidget(m_list, 1);
    m_splitter->addWidget(m_listPane);

    m_detail = new CardDetail(m_splitter);
    m_detail->hide();
    m_splitter->addWidget(m_detail);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->hide();                 // until the first `board` event: the loading line instead
    layout->addWidget(m_splitter, 1);

    auto *empty = new EmptyBoard(this);
    empty->setText(QStringLiteral("Loading the Switchboard…"));
    m_empty = empty;
    m_empty->setObjectName(QStringLiteral("boardEmpty"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    layout->addWidget(m_empty, 1);

    m_keys = new QLabel(this);
    m_keys->setObjectName(QStringLiteral("boardKeys"));
    m_keys->setTextFormat(Qt::RichText);
    m_keys->setMinimumWidth(1);         // clipped in a narrow pane, never widening it
    layout->addWidget(m_keys);

    m_follow = new QTimer(this);
    m_follow->setSingleShot(true);
    m_follow->setInterval(120);
    connect(m_follow, &QTimer::timeout, this, [this] {
        if (detailOpen() && !m_selected.isEmpty() && m_selected != m_detail->cardId())
            openSelected();
    });
    m_dragScroll = new QTimer(this);
    m_dragScroll->setInterval(30);
    connect(m_dragScroll, &QTimer::timeout, this, [this] { autoScrollDuringDrag(); });

    updateDetailLayout();   // the key line's first text
    m_detail->onClose = [this] { closeDetail(); };
    m_detail->onEscape = [this] {
        // Esc in the reply box goes back to the rows; a second Esc there closes the card. With
        // the list hidden (a narrow pane) there is nothing to go back to but the board.
        if (!m_listPane->isHidden() && board::rowOfCard(m_rows, m_detail->cardId()) >= 0)
            selectCard(m_detail->cardId());
        else
            closeDetail();
    };
    m_detail->onToPrompt = [this] {
        if (onSendToTerminal && !m_detail->cardId().isEmpty())
            onSendToTerminal(QStringLiteral("#") + m_detail->cardId() + QLatin1Char(' '));
    };
    m_detail->onOpenPath = [this](const QString &path) {
        if (!onOpenFile || path.isEmpty())
            return;
        // A card file path is relative to the workspace; a link in the body may be relative to
        // the card's own folder. Whichever exists wins, the workspace first.
        const QString cardDir = QFileInfo(QDir(m_workspace).absoluteFilePath(m_detail->path())).absolutePath();
        for (const QString &base : {m_workspace, cardDir}) {
            const QString candidate = QDir(base).absoluteFilePath(QDir::cleanPath(path));
            if (QFileInfo::exists(candidate)) {
                onOpenFile(candidate);
                return;
            }
        }
        showNotice(QStringLiteral("No file at %1").arg(path), true);
    };
    m_detail->onReply = [this](const QString &text, const QString &mode) {
        const QString card = m_detail->cardId();
        if (card.isEmpty())
            return;
        if (!mode.isEmpty()) {
            // The worker would refuse it anyway (19.9's busy rule), but saying so here keeps the
            // question in the box instead of sending it away to bounce.
            if (cleanupRunning()) {
                m_busyCard = card;
                m_detail->restoreReply(text);
                m_detail->showError(QStringLiteral("A cleanup is running — it is rewriting cards, "
                                                   "so the agent cannot answer on one until it is "
                                                   "done. Your message is still here, unsent. Stop "
                                                   "the run with the button at the top of the "
                                                   "board, or wait for it."));
                // The line takes height at the bottom of the card, where the progress notice is.
                QTimer::singleShot(0, this, [this] { placeNotice(); });
                return;
            }
            m_askCard = card;
            m_askText = text;
            m_detail->setBusy(true, mode);
            // protocol 19.10: one `board_ask`, its mode "discuss" or "plan"; a plan may be wordless.
            QJsonObject ask{{QStringLiteral("type"), QStringLiteral("board_ask")},
                            {QStringLiteral("card"), card}, {QStringLiteral("mode"), mode}};
            if (!text.isEmpty())
                ask.insert(QStringLiteral("text"), text);
            send(ask);
        } else {
            send({{QStringLiteral("type"), QStringLiteral("board_comment")},
                  {QStringLiteral("card"), card}, {QStringLiteral("text"), text},
                  {QStringLiteral("kind"), QStringLiteral("note")}});
        }
    };
    m_detail->onCancel = [this] { send({{QStringLiteral("type"), QStringLiteral("cancel")}}); };
    m_detail->onExecute = [this](const QString &note) { executeCard(note); };
    m_detail->onVerify = [this](const QString &note) { verifyCard(note); };
    m_detail->onModeHint = [this](const QString &mode) {
        if (!onHint)
            return;
        if (mode == QStringLiteral("plan"))
            onHint(QStringLiteral("board.plan"), QStringLiteral("p"));
        else if (mode == QStringLiteral("execute"))
            onHint(QStringLiteral("board.execute"), QStringLiteral("x"));
        else if (mode == QStringLiteral("verify"))
            onHint(QStringLiteral("board.verify"), QStringLiteral("v"));
        else
            onHint(QStringLiteral("board.discuss"), QStringLiteral("Enter"));
    };
    m_detail->onMove = [this](const QString &what, const QString &value) {
        const QString card = m_detail->cardId();
        if (card.isEmpty() || value.isEmpty())
            return;
        const QString id = nextRequestId();
        m_pendingNotes.insert(id, what == QStringLiteral("tab")
                                      ? QStringLiteral("Moved #%1 to %2").arg(card, board::tabTitle(value))
                                      : QStringLiteral("Moved #%1 to %2").arg(card, board::statusTitle(value)));
        send({{QStringLiteral("type"), QStringLiteral("board_move")}, {QStringLiteral("id"), id},
              {QStringLiteral("card"), card}, {what, value},
              {QStringLiteral("reason"), QStringLiteral("changed in the Switchboard")}});
    };
    m_detail->onEdit = [this](const QJsonObject &patch, const QString &baseHash) {
        saveCardEdit(patch, baseHash);
    };
    m_detail->onEditHint = [this] {
        if (onHint)
            onHint(QStringLiteral("board.edit"), QStringLiteral("e"));
    };
}

// The top of the list page (owner, 2026-09-18: "put 'new card' and filter at the top of the main
// org page, not in the pane header ... they shouldn't show when you are clicked on a card"). It
// lives inside the list pane, so it is there exactly when the list is: an open card that has the
// pane to itself sees the way back instead, and never the list's tools.
void BoardView::buildListTools(QVBoxLayout *layout)
{
    auto *tools = new QWidget(m_listPane);
    tools->setObjectName(QStringLiteral("boardListTools"));
    // The checkbox row below wraps, and height-for-width only reaches it if every widget between
    // it and the list pane's layout passes the question on.
    QSizePolicy wrapping(QSizePolicy::Preferred, QSizePolicy::Minimum);
    wrapping.setHeightForWidth(true);
    tools->setSizePolicy(wrapping);
    auto *toolsLayout = new QVBoxLayout(tools);
    toolsLayout->setContentsMargins(8, 5, 8, 6);
    toolsLayout->setSpacing(5);

    m_listTools = new QHBoxLayout;
    m_listTools->setContentsMargins(0, 0, 0, 0);
    m_listTools->setSpacing(6);
    m_count = new QLabel(tools);
    m_count->setObjectName(QStringLiteral("boardCount"));
    m_listTools->addWidget(m_count);
    m_filter = new QLineEdit(tools);
    m_filter->setObjectName(QStringLiteral("boardFilter"));
    m_filter->setPlaceholderText(QStringLiteral("Filter  —  words, label:bug, status:done, "
                                                "folder:changes, @agent, waiting:me"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setMinimumWidth(60);      // it gives way to the buttons rather than pushing them out
    m_listTools->addWidget(m_filter, 1);
    m_add = new QToolButton(tools);
    m_add->setObjectName(QStringLiteral("boardAddButton"));
    m_add->setText(QStringLiteral("+  New card (n)"));
    m_add->setToolTip(QStringLiteral("New card in the focused section (n)"));
    m_add->setCursor(Qt::PointingHandCursor);
    m_add->setFocusPolicy(Qt::NoFocus);
    // Fixed, not the tool button's default: left to shrink, a QToolButton elides its own label to
    // "…" long before the filter box has given up any of its room.
    m_add->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_listTools->addWidget(m_add);
    m_cleanup = new QToolButton(tools);
    m_cleanup->setObjectName(QStringLiteral("boardCleanup"));
    m_cleanup->setText(QStringLiteral("Clean up"));
    m_cleanup->setToolTip(QStringLiteral("Have the agent tidy the board: merge or split sections "
                                         "and cards, review statuses"));
    m_cleanup->setCursor(Qt::PointingHandCursor);
    m_cleanup->setFocusPolicy(Qt::NoFocus);
    m_cleanup->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_listTools->addWidget(m_cleanup);
    toolsLayout->addLayout(m_listTools);

    // Where those two go when the pane is too narrow to hold them beside the filter: the pane's
    // hover buttons keep their room at the right of the top row whatever happens, and at ~350 px
    // that leaves the filter nothing. Right-aligned, so the row still reads as the tools' end.
    m_toolsWrapRow = new QWidget(tools);
    m_toolsWrap = new QHBoxLayout(m_toolsWrapRow);
    m_toolsWrap->setContentsMargins(0, 0, 0, 0);
    m_toolsWrap->setSpacing(6);
    m_toolsWrap->addStretch(1);
    m_toolsWrapRow->hide();
    toolsLayout->addWidget(m_toolsWrapRow);

    // One checkbox per section, all ticked until one is unticked. They wrap onto a second line
    // rather than running off a ~350 px pane, which is why this is a flow layout and not a row.
    m_checks = new QWidget(tools);
    m_checks->setObjectName(QStringLiteral("boardSectionChecks"));
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    policy.setHeightForWidth(true);     // so the QVBoxLayout gives the wrapped rows their height
    m_checks->setSizePolicy(policy);
    auto *flow = new FlowLayout(m_checks, 10, 3);
    flow->setContentsMargins(0, 0, 0, 0);
    m_checksLayout = flow;
    toolsLayout->addWidget(m_checks);

    // The format problems belong to the list page too — they are about the cards it is showing —
    // and here they are under the tools rather than above them, where the pane's hover buttons
    // would cover the file name that fixes them.
    toolsLayout->addWidget(m_problems);

    layout->addWidget(tools);

    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_model.setFilter(text);
        rebuild();
    });
    connect(m_add, &QToolButton::clicked, this, [this] {
        if (onHint)
            onHint(QStringLiteral("board.quickAdd"), QStringLiteral("n"));
        quickAdd();
    });
    connect(m_cleanup, &QToolButton::clicked, this, [this] { requestCleanup(); });
    m_filter->installEventFilter(this);
}

// What a cleanup leaves behind, in the list page rather than over it: a panel between the tools
// and the rows, dismissible, that never takes the keyboard off the list (owner rule: a new
// surface is a pane or in-pane, never a floating strip).
void BoardView::buildCleanupPanel(QVBoxLayout *layout)
{
    m_cleanupPanel = new QWidget(m_listPane);
    m_cleanupPanel->setObjectName(QStringLiteral("boardCleanupPanel"));
    m_cleanupPanel->setAttribute(Qt::WA_StyledBackground);
    auto *panel = new QVBoxLayout(m_cleanupPanel);
    panel->setContentsMargins(10, 8, 10, 8);
    panel->setSpacing(6);

    auto *top = new QHBoxLayout;
    top->setSpacing(6);
    m_cleanupHead = new QLabel(m_cleanupPanel);
    m_cleanupHead->setObjectName(QStringLiteral("boardCleanupHead"));
    m_cleanupHead->setWordWrap(true);
    top->addWidget(m_cleanupHead, 1);
    m_cleanupApply = new QToolButton(m_cleanupPanel);
    m_cleanupApply->setObjectName(QStringLiteral("boardAddButton"));
    m_cleanupApply->setText(QStringLiteral("Apply"));
    m_cleanupApply->setToolTip(QStringLiteral("Run the cleanup for real and write these changes. "
                                              "Every write goes in the changelog, and nothing is "
                                              "committed for you."));
    m_cleanupApply->setCursor(Qt::PointingHandCursor);
    m_cleanupApply->setFocusPolicy(Qt::NoFocus);
    m_cleanupApply->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    top->addWidget(m_cleanupApply);
    m_cleanupLog = new QToolButton(m_cleanupPanel);
    m_cleanupLog->setObjectName(QStringLiteral("boardTextButton"));
    m_cleanupLog->setText(QStringLiteral("Changelog"));
    m_cleanupLog->setToolTip(QStringLiteral("Open the run's changelog in a pane"));
    m_cleanupLog->setCursor(Qt::PointingHandCursor);
    m_cleanupLog->setFocusPolicy(Qt::NoFocus);
    top->addWidget(m_cleanupLog);
    m_cleanupDismiss = new QToolButton(m_cleanupPanel);
    m_cleanupDismiss->setObjectName(QStringLiteral("boardTextButton"));
    m_cleanupDismiss->setText(QStringLiteral("Dismiss"));
    m_cleanupDismiss->setToolTip(QStringLiteral("Put this away. The changelog keeps the record."));
    m_cleanupDismiss->setCursor(Qt::PointingHandCursor);
    m_cleanupDismiss->setFocusPolicy(Qt::NoFocus);
    top->addWidget(m_cleanupDismiss);
    panel->addLayout(top);

    m_cleanupBody = new QTextBrowser(m_cleanupPanel);
    m_cleanupBody->setObjectName(QStringLiteral("boardCleanupBody"));
    m_cleanupBody->setOpenLinks(false);          // a card id opens the card, not a web browser
    m_cleanupBody->setFocusPolicy(Qt::NoFocus);  // the arrows stay with the list
    m_cleanupBody->setMaximumHeight(260);
    relay::installCopyOnSelect(m_cleanupBody);
    panel->addWidget(m_cleanupBody);

    m_cleanupPanel->hide();
    layout->addWidget(m_cleanupPanel);

    connect(m_cleanupDismiss, &QToolButton::clicked, this, [this] { hideCleanupPanel(); });
    connect(m_cleanupApply, &QToolButton::clicked, this, [this] { startCleanup(false); });
    connect(m_cleanupLog, &QToolButton::clicked, this, [this] {
        if (onOpenFile && !m_cleanupChangelog.isEmpty())
            onOpenFile(QDir(m_workspace).absoluteFilePath(m_cleanupChangelog));
    });
    connect(m_cleanupBody, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        // `card:K7Q2` goes through the same path a row click does; anything else is a file.
        if (url.scheme() == QStringLiteral("card")) {
            const QString id = (url.path().isEmpty() ? url.host() : url.path()).toUpper();
            selectCard(id);
            m_selected = id;
            openSelected();
            return;
        }
        if (onOpenFile && !url.path().isEmpty())
            onOpenFile(QDir(m_workspace).absoluteFilePath(url.path()));
    });
}

// The pane's hover buttons take their room out of whichever row is on top for good, and in a
// ~350 px pane that leaves the filter box nothing. Rather than let two QToolButtons elide to a
// pair of identical "…", they drop to a line of their own under the filter.
void BoardView::layoutListTools()
{
    if (!m_toolsWrap || !m_add || !m_cleanup)
        return;
    const int inset = m_head && m_head->isHidden() ? m_rightInset : 0;
    const int room = m_listPane->width() - 16 - inset;
    // The filter is owed a legible width before either button may sit beside it.
    const int need = m_count->sizeHint().width() + 150 + m_add->sizeHint().width()
                     + m_cleanup->sizeHint().width() + 18;
    const bool wrap = room < need;
    if (wrap == m_toolsWrapped)
        return;
    m_toolsWrapped = wrap;
    QHBoxLayout *from = wrap ? m_listTools : m_toolsWrap;
    QHBoxLayout *to = wrap ? m_toolsWrap : m_listTools;
    for (QToolButton *button : {m_add, m_cleanup}) {
        from->removeWidget(button);
        to->addWidget(button);
    }
    m_toolsWrapRow->setVisible(wrap);
}

// The boxes follow the sections the model has right now — board.yaml's columns, whatever extra
// statuses the cards carry, and Done — rather than a hard-coded list. Rebuilt only when that set
// changes, so ticking one does not delete the box under the pointer.
void BoardView::syncSectionChecks()
{
    const QList<board::Column> sections = m_model.sections();
    QStringList ids, titles;
    for (const board::Column &section : sections) {
        ids << section.id;
        titles << section.title;
    }
    // The empty board draws one unlit jack per section under these names (EmptyBoard, above).
    static_cast<EmptyBoard *>(m_empty)->setSections(titles);
    if (ids == m_checkIds)
        return;
    m_checkIds = ids;
    while (QLayoutItem *item = m_checksLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const board::Column &section : sections) {
        auto *box = new QCheckBox(section.title.toUpper(), m_checks);
        box->setObjectName(QStringLiteral("boardSectionCheck"));
        box->setChecked(!m_hidden.contains(section.id));
        box->setCursor(Qt::PointingHandCursor);
        box->setFocusPolicy(Qt::NoFocus);
        const QString id = section.id;
        connect(box, &QCheckBox::toggled, this, [this, id](bool on) {
            if (on)
                m_hidden.remove(id);
            else
                m_hidden.insert(id);
            rebuild();
            // The selection may have been in what just went away: stand on the first card left.
            if (board::rowOfCard(m_rows, m_selected) < 0) {
                const int first = board::stepRow(m_rows, -1, 1);
                m_selected = first >= 0 ? m_rows.at(first).cardId : QString();
            }
        });
        m_checksLayout->addWidget(box);
    }
    m_checks->setVisible(!sections.isEmpty());
}

// The pane's hover buttons float over the top right of the pane, so whichever row is actually on
// top has to give up that much room — the header while a card is open, the list's tools otherwise.
void BoardView::applyRightInset()
{
    const bool head = m_head && !m_head->isHidden();
    if (m_tools)
        m_tools->setContentsMargins(0, 0, head ? m_rightInset : 0, 0);
    if (m_listTools)
        m_listTools->setContentsMargins(0, 0, head ? 0 : m_rightInset, 0);
    layoutListTools();
}

// The quick-add field, built once and shown when a card is added. It lines up with the rows
// under it rather than living inside a section, so a refill never takes it away mid-typing.
void BoardView::buildQuickAdd(QVBoxLayout *layout)
{
    m_quickAddRow = new QWidget(m_listPane);
    m_quickAddRow->setObjectName(QStringLiteral("boardQuickAddRow"));
    auto *row = new QHBoxLayout(m_quickAddRow);
    row->setContentsMargins(8, 5, 8, 3);
    auto *field = new QLineEdit(m_quickAddRow);
    field->setObjectName(QStringLiteral("boardQuickAdd"));
    field->setToolTip(QStringLiteral("The text is kept verbatim as the card's request"));
    row->addWidget(field);
    m_quickAdd = field;
    m_quickAddRow->hide();
    layout->addWidget(m_quickAddRow);
    field->installEventFilter(this);
    connect(field, &QLineEdit::returnPressed, this, [this, field] {
        const QString text = field->text().trimmed();
        if (text.isEmpty()) {
            closeQuickAdd();
            focusInput();
            return;
        }
        const QString status = m_model.dropStatus(m_quickAddColumn);
        send({{QStringLiteral("type"), QStringLiteral("board_create")},
              {QStringLiteral("tab"), defaultCategory()},
              {QStringLiteral("status"), status.isEmpty() ? QStringLiteral("inbox") : status},
              {QStringLiteral("card_type"), QStringLiteral("work")},
              {QStringLiteral("text"), text}});
        field->clear();
    });
}

// Watch the board folder and its subfolders. A card write anywhere (this window, a pane agent, a
// collaborator's merge) becomes one debounced board_refresh, which the worker answers with the
// rows that actually changed.
void BoardView::watchIssues()
{
    // Which folder that is, rather than an assumption: `switchboard/` on a board made from
    // 2026-09-18 on and `issues/` on an older one (protocol 19.12). The worker says so on the
    // `board` event (`m_root`); before the first one, ask the filesystem the same way it does.
    const QString root = m_root.isEmpty() ? projects::boardDirOf(m_workspace) : m_root;
    if (root.isEmpty() || !QFileInfo::exists(root))
        return;
    if (!m_refresh) {
        m_refresh = new QTimer(this);
        m_refresh->setSingleShot(true);
        m_refresh->setInterval(400);
        connect(m_refresh, &QTimer::timeout, this, [this] {
            if (m_open)
                send({{QStringLiteral("type"), QStringLiteral("board_refresh")}});
            watchIssues();   // folders come and go as cards move between state subfolders
        });
    }
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        const auto touched = [this](const QString &) { m_refresh->start(); };
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, touched);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, touched);
    }
    QStringList wanted{root};
    QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext() && wanted.size() < 200)
        wanted << it.next();
    const QStringList known = m_watcher->directories();
    QStringList fresh;
    for (const QString &path : wanted)
        if (!known.contains(path))
            fresh << path;
    if (!fresh.isEmpty())
        m_watcher->addPaths(fresh);
}

QString BoardView::nextRequestId()
{
    return m_requestPrefix + QString::number(++m_requestSeq);
}

void BoardView::send(QJsonObject message)
{
    if (!message.contains(QStringLiteral("id")))
        message.insert(QStringLiteral("id"), nextRequestId());
    if (onSend)
        onSend(message);
}

void BoardView::reload()
{
    send({{QStringLiteral("type"), QStringLiteral("board_open")}});
}

// "Switchboard · 84 open": the number the pane is actually about, not every card ever filed.
QString BoardView::title() const
{
    if (m_model.total() == 0)
        return QStringLiteral("Switchboard");
    return QStringLiteral("Switchboard · %1 open").arg(m_model.openCount());
}

void BoardView::setHeaderRightInset(int pixels)
{
    m_rightInset = pixels;
    applyRightInset();
}

QString BoardView::notice() const
{
    return m_notice->isHidden() ? QString() : m_noticeText->text();
}

void BoardView::showNotice(const QString &text, bool error, const QString &undoWriteId)
{
    m_noticeText->setText(text);
    m_notice->setProperty("error", error);
    m_notice->style()->unpolish(m_notice);
    m_notice->style()->polish(m_notice);
    m_noticeUndo->setVisible(!undoWriteId.isEmpty());
    m_notice->show();
    placeNotice();
    m_noticeTimer->start(error ? 12000 : 10000);
}

void BoardView::placeNotice()
{
    if (m_notice->isHidden())
        return;
    const int width = qMin(560, this->width() - 24);
    m_notice->setFixedWidth(qMax(120, width));
    // The wrapped text's real height at this width, not adjustSize()'s guess at a narrower one.
    QLayout *layout = m_notice->layout();
    m_notice->setFixedHeight(layout->hasHeightForWidth() ? layout->totalHeightForWidth(m_notice->width())
                                                         : layout->totalSizeHint().height());
    const int x = (this->width() - m_notice->width()) / 2;
    // From the size hint, not the geometry: on a resize the layout has not placed m_keys yet.
    int bottom = height() - (m_keys->isVisible() ? m_keys->sizeHint().height() : 0) - 10;
    // A card with the pane to itself has its reply box and its Ask button along that bottom, and a
    // cleanup's progress line stays up for minutes rather than ten seconds: it goes above them.
    if (detailOpen() && m_listPane->isHidden())
        bottom -= m_detail->controlsHeight();
    m_notice->move(x, bottom - m_notice->height());
    m_notice->raise();
}

// ------------------------------------------------------------------------- events

void BoardView::handleEvent(const QJsonObject &event)
{
    const QString type = event.value(QStringLiteral("event")).toString();
    const QString requestId = event.value(QStringLiteral("id")).toString();
    const bool mine = !requestId.isEmpty() && requestId.startsWith(m_requestPrefix);
    // Defence in depth for the per-project Switchboard: every board event carries the `issues`
    // directory it came from. The window already routes each worker's events to the views of its
    // own board root, but a view that has learned its root will not take another project's cards
    // even if that routing is ever wrong — a wrong reset here silently repoints the view, and the
    // next drag or quick add writes into the other repository. Events with no `root` (an older
    // worker) are handled exactly as before.
    const QString root = event.value(QStringLiteral("root")).toString();
    if (!root.isEmpty()) {
        if (!m_root.isEmpty() && root != m_root)
            return;
        if (m_root.isEmpty() && type == QStringLiteral("board"))
            m_root = root;
    }
    if (type == QStringLiteral("board")) {
        m_open = true;
        m_model.setConfig(event.value(QStringLiteral("config")).toObject());
        m_model.reset(event.value(QStringLiteral("cards")).toArray());
        const bool hadFocus = hasFocus();   // opened with Ctrl+Shift+S before the cards arrived
        rebuild();
        if (hadFocus)
            focusInput();
        showProblems(event.value(QStringLiteral("problems")).toArray());
        if (onTitleChanged)
            onTitleChanged(title());
        return;
    }
    if (type == QStringLiteral("board_changed")) {
        const QJsonArray upserts = event.value(QStringLiteral("upserts")).toArray();
        const QJsonArray gone = event.value(QStringLiteral("removed")).toArray();
        showProblems(event.value(QStringLiteral("problems")).toArray());
        // The watcher's refresh after our own write finds nothing new; do not redraw for it.
        if (upserts.isEmpty() && gone.isEmpty())
            return;
        m_model.upsert(upserts);
        QStringList removed;
        for (const QJsonValue &value : gone)
            removed << value.toString();
        m_model.remove(removed);
        rebuild();
        if (onTitleChanged)
            onTitleChanged(title());
        // A card that is open stays in step with its file; other cards' changes leave it alone.
        const QString open = m_detail->cardId();
        if (!open.isEmpty() && detailOpen()) {
            if (removed.contains(open)) {
                closeDetail();
                showNotice(QStringLiteral("#%1 was removed from the board.").arg(open), true);
            } else {
                for (const QJsonValue &value : upserts)
                    if (value.toObject().value(QStringLiteral("id")).toString() == open) {
                        send({{QStringLiteral("type"), QStringLiteral("board_card_get")},
                              {QStringLiteral("card"), open}});
                        break;
                    }
            }
        }
        return;
    }
    if (type == QStringLiteral("board_card")) {
        QStringList statuses;
        const QList<board::Column> sections = m_model.sections();
        for (const board::Column &column : sections)
            for (const QString &status : column.statuses)
                if (!statuses.contains(status))
                    statuses << status;
        const QString current = event.value(QStringLiteral("status")).toString();
        if (!current.isEmpty() && !statuses.contains(current))
            statuses.prepend(current);
        QList<QPair<QString, QString>> tabs;
        for (const board::Tab &item : m_model.tabs())
            if (!item.folder.isEmpty())
                tabs << qMakePair(item.id, item.title);
        m_detail->setChoices(statuses, tabs);
        m_detail->show(event);
        const bool wasOpen = detailOpen();
        m_detail->setVisible(true);
        if (!wasOpen)
            m_detailSized = false;
        updateDetailLayout();
        if (m_editOnOpen) {
            m_editOnOpen = false;
            m_detail->beginEdit(false);
        } else if (!m_actionOnOpen.isEmpty()) {
            const QString action = m_actionOnOpen;
            m_actionOnOpen.clear();
            m_detail->focusDocument();
            cardAction(action);
        } else if (m_replyOnOpen) {
            m_replyOnOpen = false;
            m_detail->focusReply();
        } else if (m_detail->editing()) {
            // A re-read while the card is being edited (its file changed, or a save was refused)
            // leaves the fields, and the focus, exactly where they were.
        } else if (m_listPane->isHidden() && !m_detail->isAncestorOf(QApplication::focusWidget())) {
            m_detail->focusDocument();   // the list it came from is hidden in a narrow pane
        }
        return;
    }
    if (type == QStringLiteral("board_thread_appended")) {
        if (event.value(QStringLiteral("card_id")).toString() == m_detail->cardId())
            m_detail->appendEntry(event);
        return;
    }
    if (type == QStringLiteral("board_problems")) {
        showProblems(event.value(QStringLiteral("items")).toArray());
        return;
    }
    if (type == QStringLiteral("board_written") && mine) {
        const QString writeId = event.value(QStringLiteral("write_id")).toString();
        const QString kind = event.value(QStringLiteral("kind")).toString();
        const QString card = event.value(QStringLiteral("card_id")).toString();
        QString note = m_pendingNotes.take(requestId);
        if (kind == QStringLiteral("board_create")) {
            note = QStringLiteral("Created #%1").arg(card);
            m_selected = card;   // the new card is the selection, so Enter opens it
        }
        if (kind == QStringLiteral("board_comment"))
            return;              // the thread itself shows it
        // The edit is written: the card goes back to being read (the new text arrives with the
        // re-read that `board_changed` asks for).
        if (kind == QStringLiteral("board_update") && card == m_detail->cardId())
            m_detail->endEdit();
        m_lastWrite = writeId;
        if (!note.isEmpty())
            showNotice(note, false, writeId);
        return;
    }
    if (type == QStringLiteral("board_undone") && mine) {
        m_lastWrite.clear();
        showNotice(QStringLiteral("Undone: #%1 is back as it was.")
                       .arg(event.value(QStringLiteral("card_id")).toString()), false);
        return;
    }
    // A cleanup's events first, and whole: they are tagged `cleanup: true` with a run id and no
    // card id (19.9), and an open card's thread must never see one of them.
    if (handleCleanupEvent(type, event))
        return;
    // The worker runs one turn at a time, and a cleanup and a card's ask refuse each other rather
    // than queue (19.9): say which it is, and put back whatever this pane had started.
    if (type == QStringLiteral("error")
        && event.value(QStringLiteral("code")).toString() == QStringLiteral("board_busy")) {
        const bool cleanupRuns = event.value(QStringLiteral("cleanup_running")).toBool();
        const QString busyCard = event.value(QStringLiteral("card_id")).toString();
        const QString what = cleanupRuns
            ? QStringLiteral("A cleanup is running on this board.")
            : (busyCard.isEmpty()
                   ? QStringLiteral("The Switchboard agent is busy.")
                   : QStringLiteral("The agent is answering on #%1.").arg(busyCard));
        if (!m_cleanupRequest.isEmpty() && requestId == m_cleanupRequest) {
            endCleanup();
            m_notice->hide();
            showNotice(what + QStringLiteral(" The cleanup did not start — nothing was written. "
                                             "Try again when it has finished."), true);
            return;
        }
        if (!m_askCard.isEmpty()) {
            const bool here = m_askCard == m_detail->cardId();
            const QString unsent = m_askText;
            m_detail->setBusy(false);
            m_askCard.clear();
            m_askText.clear();
            if (here) {
                // The worker checks before it writes, so the question never reached the thread.
                if (cleanupRuns)
                    m_busyCard = m_detail->cardId();
                m_detail->restoreReply(unsent);
                m_detail->showError(what + QStringLiteral(" The agent answers one thing at a time, "
                                                          "so your message was not sent and is not "
                                                          "in the thread — it is back in the reply "
                                                          "box. Try again when it has finished."));
                QTimer::singleShot(0, this, [this] { placeNotice(); });
                return;
            }
        }
        showNotice(what, true);
        return;
    }
    // A board_ask turn: the answer streams into the open card.
    const QString card = event.value(QStringLiteral("card_id")).toString();
    if (!card.isEmpty() && card == m_detail->cardId()) {
        if (type == QStringLiteral("delta")) {
            m_detail->appendDelta(event.value(QStringLiteral("text")).toString());
            return;
        }
        if (type == QStringLiteral("done") || type == QStringLiteral("error")
            || type == QStringLiteral("cancelled")) {
            m_detail->setBusy(false);
            m_askCard.clear();
            m_askText.clear();
            if (type == QStringLiteral("error"))
                m_detail->showError(event.value(QStringLiteral("text")).toString());
            return;
        }
    }
    // A write this pane asked for and the worker refused (a move into Needs QA without evidence,
    // a stale hash): say so where the card was dropped instead of in a status bar.
    if (type == QStringLiteral("error") && mine) {
        m_pendingNotes.remove(requestId);
        const QString text = event.value(QStringLiteral("text")).toString();
        // The card was written by someone else between the read and the save. Nothing was
        // overwritten and nothing typed is lost: read the card again (which brings the new hash
        // and says what changed) and leave the text in the editor for a second Save.
        if (event.value(QStringLiteral("code")).toString() == QStringLiteral("board_conflict")
            && m_detail->editing()) {
            m_detail->showError(QStringLiteral("#%1 changed on disk, so nothing was written. Your "
                                               "text is still here: Save again to write it over "
                                               "that version, or Esc to drop it.")
                                    .arg(m_detail->cardId()));
            send({{QStringLiteral("type"), QStringLiteral("board_card_get")},
                  {QStringLiteral("card"), m_detail->cardId()}});
            return;
        }
        if (!m_askCard.isEmpty()) {
            // A question the agent could not take (no provider key, say): the question itself is
            // already in the thread, so say that under it rather than over the board.
            const bool here = m_askCard == m_detail->cardId();
            m_detail->setBusy(false);
            m_askCard.clear();
            m_askText.clear();
            if (here) {
                m_detail->showError(QStringLiteral("The Switchboard agent could not answer: %1 "
                                                   "Your message is kept in the thread.").arg(text));
                return;
            }
        }
        showNotice(text, true);
    }
}

void BoardView::showProblems(const QJsonArray &problems)
{
    int errors = 0;
    QString first, path;
    for (const QJsonValue &value : problems) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("severity")).toString() != QStringLiteral("error"))
            continue;
        ++errors;
        if (first.isEmpty()) {
            path = item.value(QStringLiteral("path")).toString();
            first = item.value(QStringLiteral("message")).toString();
        }
    }
    m_problems->setVisible(errors > 0);
    if (errors == 0)
        return;
    // The file first (it is the link that fixes it), then the message.
    QString text = QStringLiteral("⚠ %1").arg(errors == 1 ? QStringLiteral("1 problem")
                                                         : QStringLiteral("%1 problems").arg(errors));
    if (!path.isEmpty())
        text += QStringLiteral(" · <a href=\"%1\" style=\"color:%2\">%3</a>")
                    .arg(path.toHtmlEscaped(), theme::Warning.name(),
                         QFileInfo(path).fileName().toHtmlEscaped());
    text += QStringLiteral(" · ") + first.toHtmlEscaped();
    m_problems->setText(text);
    m_problems->setToolTip(path + QStringLiteral(": ") + first
                           + QStringLiteral("\n\npython3 scripts/relay-board.py check lists every problem."));
}

// ------------------------------------------------------------------------ rendering

// "84 open" at the left of the filter row, with what the filter is hiding when one is set, and
// "62 of 84 open" when a section checkbox is keeping cards off the page. The pane's own title
// carries the board's own number, so the count is never only inside the list.
void BoardView::updateCounts()
{
    const int open = m_model.openCount();
    const int hidden = m_model.hiddenCount(m_hidden);
    QString text = QStringLiteral("%1 open").arg(open);
    if (!m_model.filter().trimmed().isEmpty()) {
        // With a filter set, what the rows say is the honest number: it counts the closed cards
        // a `status:done` search turns up, which "open" never does.
        int matched = 0;
        for (const board::Row &row : std::as_const(m_rows))
            if (row.kind == board::Row::Card)
                ++matched;
        text = QStringLiteral("%1 shown").arg(matched);
    } else if (hidden > 0) {
        text = QStringLiteral("%1 of %2 open").arg(open - hidden).arg(open);
    }
    m_count->setText(text);
    QString tip = QStringLiteral("%1 card%2 on the board in all").arg(m_model.total())
                      .arg(m_model.total() == 1 ? QString() : QStringLiteral("s"));
    if (hidden > 0)
        tip += QStringLiteral("\n%1 open card%2 hidden by the section checkboxes")
                   .arg(hidden).arg(hidden == 1 ? QString() : QStringLiteral("s"));
    m_count->setToolTip(tip);

    // Each box says how many cards its section holds right now, so unticking one is a decision
    // taken with the number in view rather than after the fact.
    for (int i = 0; i < m_checkIds.size() && i < m_checksLayout->count(); ++i) {
        auto *box = qobject_cast<QCheckBox *>(m_checksLayout->itemAt(i)->widget());
        if (!box)
            continue;
        const QString id = m_checkIds.at(i);
        const int header = board::rowOfSection(m_rows, id);
        const QString title = sectionTitle(id);
        QString tip;
        if (m_hidden.contains(id))
            tip = QStringLiteral("%1 — hidden; tick to put the section back").arg(title);
        else if (header >= 0)
            tip = QStringLiteral("%1 · %2 card%3 — untick to hide the section")
                      .arg(title).arg(m_rows.at(header).count)
                      .arg(m_rows.at(header).count == 1 ? QString() : QStringLiteral("s"));
        else
            tip = QStringLiteral("%1 — nothing matches the filter").arg(title);
        box->setToolTip(withMeaning(tip, id));
    }
}

QStringList BoardView::columnIds() const
{
    QStringList out;
    const QList<board::Column> sections = m_model.sections();
    for (const board::Column &section : sections)
        out << section.id;
    return out;
}

QString BoardView::defaultCategory() const
{
    for (const board::Tab &tab : m_model.tabs())
        if (!tab.folder.isEmpty() && tab.type == QStringLiteral("work"))
            return tab.id;
    return QStringLiteral("features");
}

QString BoardView::sectionTitle(const QString &columnId) const
{
    const QList<board::Column> sections = m_model.sections();
    for (const board::Column &section : sections)
        if (section.id == columnId)
            return section.title;
    return board::statusTitle(columnId);
}

// Nothing is created straight into Done: a card gets there by being closed.
bool BoardView::sectionTakesNewCards(const QString &columnId) const
{
    const QString landing = m_model.dropStatus(columnId);
    return !landing.isEmpty() && landing != QStringLiteral("done")
           && landing != QStringLiteral("dropped");
}

// The section the keys act on: the one holding the selection, else the first on screen.
QString BoardView::focusedSection() const
{
    const int at = board::rowOfCard(m_rows, m_selected);
    if (at >= 0)
        return m_rows.at(at).columnId;
    for (const board::Row &row : m_rows)
        if (row.kind == board::Row::Section)
            return row.columnId;
    return columnIds().value(0);
}

void BoardView::selectRow(int index)
{
    if (index < 0 || index >= m_rows.size() || index >= m_list->count())
        return;
    QListWidgetItem *item = m_list->item(index);
    m_selected = item->data(kCardRole).toString();
    m_list->setCurrentItem(item);
    item->setSelected(true);
    m_list->scrollToItem(item);
    m_list->setFocus();
}

// One list, refilled in place: the scroll position, the selection, the focus and the open
// quick-add field all survive a change, and nothing flashes.
void BoardView::refill()
{
    const bool hadFocus = m_list->hasFocus();
    const int scroll = m_list->verticalScrollBar()->value();
    // Done (and anything parked) starts folded: the list is the live work, and the closed cards
    // are a count at the bottom that opens on demand.
    if (!m_collapsedSeeded) {
        m_collapsedSeeded = true;
        m_collapsed.insert(board::doneSection());
        m_collapsed.insert(QStringLiteral("deferred"));
    }
    m_rows = m_model.rows(m_collapsed, m_hidden);

    if (!m_model.filter().trimmed().isEmpty())
        m_list->placeholder = QStringLiteral("No card matches this filter.\nEsc clears it.");
    else if (!m_hidden.isEmpty() && m_model.openCount() > 0)
        m_list->placeholder = QStringLiteral("Every section is hidden.\nTick one at the top to see "
                                             "its cards.");
    else
        m_list->placeholder = QStringLiteral("Nothing open.\nPress n to add a card.");

    const QSignalBlocker block(m_list);
    m_list->setUpdatesEnabled(false);
    m_list->clear();
    for (const board::Row &row : std::as_const(m_rows)) {
        auto *item = new QListWidgetItem(m_list);
        if (row.kind == board::Row::Section) {
            // A header is not a card: it is never selected, never dragged, and Up/Down steps
            // straight over it (board::stepRow).
            item->setFlags(Qt::ItemIsEnabled);
            item->setToolTip(withMeaning(row.collapsed
                                             ? QStringLiteral("%1 · %2 cards — click to show them")
                                                   .arg(row.title).arg(row.count)
                                             : QStringLiteral("%1 · %2 cards — click to fold")
                                                   .arg(row.title).arg(row.count),
                                         row.columnId));
            continue;
        }
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        item->setData(kCardRole, row.cardId);
        if (const board::Card *card = m_model.card(row.cardId))
            item->setToolTip(QStringLiteral("%1 · %2\n%3").arg(card->reference(), card->title,
                                                               card->path));
        if (row.cardId == m_selected) {
            m_list->setCurrentItem(item);
            item->setSelected(true);
        }
    }
    m_list->doItemsLayout();
    m_list->verticalScrollBar()->setValue(scroll);
    m_list->setUpdatesEnabled(true);
    m_list->viewport()->update();
    // A card moved with the keyboard keeps the focus: it follows the card to its new section.
    if (hadFocus) {
        m_list->setFocus();
        if (QListWidgetItem *current = m_list->currentItem())
            m_list->scrollToItem(current);
    }
}

void BoardView::rebuild()
{
    if (m_dragActive) {
        m_rebuildPending = true;
        return;
    }
    m_rebuildPending = false;
    syncSectionChecks();   // before the refill: the boxes decide which sections it puts in
    refill();
    updateCounts();

    const bool empty = m_model.total() == 0;
    if (!m_open)
        m_empty->setText(QStringLiteral("Loading the Switchboard…"));
    else
        m_empty->setText(QStringLiteral("No cards yet.\n\nPress n or click “+ New card” to add the first one."));
    m_empty->setVisible(!m_open || empty);
    m_splitter->setVisible(m_open && !empty);
    m_keys->setVisible(m_open && !empty);
}

// A section folds and unfolds; which sections are folded is saved with the window's layout, so
// Done stays folded across a restart.
void BoardView::toggleSection(QString columnId)
{
    if (columnId.isEmpty())
        return;
    m_collapsedSeeded = true;
    if (m_collapsed.contains(columnId))
        m_collapsed.remove(columnId);
    else
        m_collapsed.insert(columnId);
    const bool hadFocus = m_list->hasFocus();
    rebuild();
    // The selection may be inside what just folded: stand on the nearest card still on screen.
    if (board::rowOfCard(m_rows, m_selected) < 0) {
        const int header = board::rowOfSection(m_rows, columnId);
        int next = board::stepRow(m_rows, header, 1);
        if (next < 0)
            next = board::stepRow(m_rows, header, -1);
        if (next >= 0) {
            if (hadFocus)
                selectRow(next);
            else
                m_selected = m_rows.at(next).cardId;
        } else {
            m_selected.clear();
        }
    }
}

QJsonArray BoardView::collapsedSections() const
{
    QStringList ids(m_collapsed.begin(), m_collapsed.end());
    ids.sort();
    return QJsonArray::fromStringList(ids);
}

void BoardView::setCollapsedSections(const QJsonArray &state)
{
    m_collapsed.clear();
    m_collapsedSeeded = true;      // a restored pane keeps what the window remembered, even none
    for (const QJsonValue &value : state)
        if (!value.toString().isEmpty())
            m_collapsed.insert(value.toString());
    if (m_open)
        rebuild();
}

// The same shape and the same home in the layout node as the folded set, one key over. Unticked
// is not folded: a folded section is a header with its cards put away, an unticked one is not on
// the page at all and its cards are out of the count.
QJsonArray BoardView::hiddenSections() const
{
    QStringList ids(m_hidden.begin(), m_hidden.end());
    ids.sort();
    return QJsonArray::fromStringList(ids);
}

void BoardView::setHiddenSections(const QJsonArray &state)
{
    m_hidden.clear();
    for (const QJsonValue &value : state)
        if (!value.toString().isEmpty())
            m_hidden.insert(value.toString());
    // The boxes exist only once the sections do; syncSectionChecks reads m_hidden when it builds
    // them, and this keeps any that are already up in step.
    for (int i = 0; i < m_checkIds.size() && m_checksLayout && i < m_checksLayout->count(); ++i) {
        if (auto *box = qobject_cast<QCheckBox *>(m_checksLayout->itemAt(i)->widget())) {
            const QSignalBlocker block(box);
            box->setChecked(!m_hidden.contains(m_checkIds.at(i)));
        }
    }
    if (m_open)
        rebuild();
}

void BoardView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateDetailLayout();
    placeNotice();
}

// Wide enough, the open card sits beside the list; in a narrow pane it takes the whole pane
// (the list comes back when it closes) instead of squeezing the rows to a sliver.
void BoardView::updateDetailLayout()
{
    // The key line says what the keys do in what is on screen: the list, or a card that has the
    // pane to itself.
    static const QString boardKeys = QStringLiteral(
        "<b>Enter</b> open &nbsp; <b>e</b> edit &nbsp; <b>p</b> plan &nbsp; <b>x</b> execute &nbsp; "
        "<b>v</b> verify &nbsp; "
        "<b>n</b> new &nbsp; <b>←/→</b> fold section &nbsp; "
        "<b>Alt+Shift+↑↓</b> reorder &nbsp; <b>Alt+Shift+←→</b> status &nbsp; <b>m</b> move "
        "&nbsp; <b>/</b> filter &nbsp; <b>t</b> #ID to prompt &nbsp; <b>y</b> copy &nbsp; "
        "<b>o</b> file &nbsp; <b>Ctrl+Z</b> undo");
    static const QString cardKeys = QStringLiteral(
        "<b>Esc</b> back to the board &nbsp; <b>e</b> edit &nbsp; <b>d</b>/<b>Tab</b> reply &nbsp; "
        "<b>Enter</b> discuss &nbsp; <b>p</b> or <b>Ctrl+Enter</b> plan &nbsp; <b>x</b> execute "
        "&nbsp; <b>v</b> verify &nbsp; <b>Ctrl+Shift+Enter</b> comment only");
    const bool stacked = width() < kStackedWidth;
    const QString keys = detailOpen() && stacked ? cardKeys : boardKeys;
    if (m_keys->text() != keys)
        m_keys->setText(keys);
    // The header exists only while a card is open, and then it says one thing: the way back.
    if (m_head->isHidden() == detailOpen()) {
        m_head->setVisible(detailOpen());
        applyRightInset();
    }
    if (!detailOpen()) {
        m_listPane->setVisible(true);
        placeNotice();      // back to the bottom of the list
        return;
    }
    const bool wasStacked = m_listPane->isHidden();
    m_listPane->setVisible(!stacked);
    placeNotice();          // the top of the pane while the card has it, the bottom otherwise
    if (stacked)
        return;
    if (!m_detailSized || wasStacked) {
        const int total = qMax(1, m_splitter->width());
        const int detail = qBound(360, total * 45 / 100, total - 300);
        m_splitter->setSizes({total - detail, detail});
        m_detailSized = true;
    }
}

bool BoardView::detailOpen() const
{
    return !m_detail->isHidden();
}

// ------------------------------------------------------------------------- actions

void BoardView::moveCard(const QString &id, const QString &columnId, const QString &beforeId,
                         const QString &afterId)
{
    if (id.isEmpty())
        return;
    // Verified is not a status, it is a fact the worker stamped (#T71W). Dropping a card in would
    // have to invent a signature, so the drop is refused here rather than sent and refused there,
    // and the line says the one way in. Reordering *inside* Verified is fine.
    const board::Card *moving = m_model.card(id);
    if (columnId == board::verifiedSection()
        && !(moving && m_model.sectionOf(*moving) == columnId)) {
        showNotice(QStringLiteral("A card is verified by closing it from a QA lane with a "
                                  "different model."),
                   true);
        return;
    }
    const QString requestId = nextRequestId();
    QJsonObject message{{QStringLiteral("type"), QStringLiteral("board_move")},
                        {QStringLiteral("id"), requestId},
                        {QStringLiteral("card"), id},
                        {QStringLiteral("reason"), QStringLiteral("moved in the Switchboard")}};
    const QString status = m_model.dropStatus(columnId);
    const bool sameSection = moving && m_model.sectionOf(*moving) == columnId;
    QString note;
    if (!status.isEmpty() && !sameSection) {
        // Within its own section a card keeps its exact status (Needs QA stays LLM or human).
        message.insert(QStringLiteral("status"), status);
        note = QStringLiteral("Moved #%1 to %2").arg(id, sectionTitle(columnId));
    } else {
        note = QStringLiteral("Reordered #%1").arg(id);
    }
    if (!beforeId.isEmpty())
        message.insert(QStringLiteral("before"), beforeId);
    if (!afterId.isEmpty())
        message.insert(QStringLiteral("after"), afterId);
    m_pendingNotes.insert(requestId, note);
    send(message);
}

void BoardView::moveToTab(const QString &id, const QString &tabId)
{
    if (id.isEmpty() || tabId.isEmpty())
        return;
    const QString requestId = nextRequestId();
    m_pendingNotes.insert(requestId, QStringLiteral("Moved #%1 to %2").arg(
                                         id, m_model.tab(tabId) ? m_model.tab(tabId)->title : tabId));
    send({{QStringLiteral("type"), QStringLiteral("board_move")}, {QStringLiteral("id"), requestId},
          {QStringLiteral("card"), id}, {QStringLiteral("tab"), tabId},
          {QStringLiteral("reason"), QStringLiteral("moved in the Switchboard")}});
}

void BoardView::undoLast()
{
    if (m_lastWrite.isEmpty()) {
        showNotice(QStringLiteral("Nothing to undo."), false);
        return;
    }
    send({{QStringLiteral("type"), QStringLiteral("board_undo")},
          {QStringLiteral("write_id"), m_lastWrite}});
}

void BoardView::quickAdd()
{
    quickAddIn(focusedSection());
}

// The field sits over the list rather than inside a section: with one long list, a field at a
// section's head would be scrolled out of sight as often as not. It names the section it adds
// to. Enter adds the card and keeps the field open for the next one (a burst of ideas is the
// common case); Esc, or leaving it empty, closes it.
void BoardView::quickAddIn(const QString &columnId)
{
    if (!m_open)
        return;
    if (m_model.total() == 0) {
        // The empty board hides the list; show it so the field has somewhere to go.
        m_empty->hide();
        m_splitter->show();
    }
    QString id = columnId;
    if (id.isEmpty() || !sectionTakesNewCards(id))
        id = columnIds().value(0);
    if (id.isEmpty())
        return;
    m_quickAddColumn = id;
    m_quickAdd->setPlaceholderText(QStringLiteral("New card in %1 — Enter adds, Esc closes")
                                       .arg(sectionTitle(id)));
    m_quickAddRow->show();
    m_quickAdd->setFocus();
}

void BoardView::closeQuickAdd()
{
    m_quickAdd->clear();
    m_quickAddRow->hide();
}

void BoardView::openSelected()
{
    if (m_selected.isEmpty())
        return;
    if (detailOpen() && m_detail->cardId() == m_selected) {
        updateDetailLayout();
        return;
    }
    // A card being edited is not swapped for another one under the typing: the selection may
    // move on the list, but the open card stays until the edit is saved or dropped.
    if (detailOpen() && m_detail->editing())
        return;
    send({{QStringLiteral("type"), QStringLiteral("board_card_get")},
          {QStringLiteral("card"), m_selected}});
}

// `e`, the Edit button, a click on the title or a double-click in the text: the card's own words
// become fields, and Save sends one `board_update` for the title and the `## Issue` text
// together (protocol 19.3). The GUI never writes the file; the worker does, hash-checked against
// the version this card was read at, so an edit made elsewhere meanwhile is never lost silently.
void BoardView::editSelected()
{
    if (detailOpen() && (m_selected.isEmpty() || m_detail->cardId() == m_selected)) {
        m_detail->beginEdit(false);
        return;
    }
    if (m_selected.isEmpty())
        return;
    m_editOnOpen = true;
    openSelected();
}

// `p` / `x`: Plan or Execute the open card, or the selected one once it has been read (the
// buttons need its sections and front matter to know whether it has a plan).
void BoardView::cardAction(const QString &action)
{
    if (!(detailOpen() && (m_selected.isEmpty() || m_detail->cardId() == m_selected))) {
        if (m_selected.isEmpty())
            return;
        m_actionOnOpen = action;
        openSelected();
        return;
    }
    if (action == QStringLiteral("plan"))
        m_detail->plan();
    else if (action == QStringLiteral("verify"))
        m_detail->verify();
    else
        m_detail->execute();
}

// Execute (#XS6Q): the card goes to a terminal pane's agent. The board records the hand-off with
// the writes it already has — assignee, In progress, a progress note in the thread — and the
// window opens the pane (`onExecuteCard`), whose agent gets the card attached and the task text
// that tells it the board's conventions (board::executeTask). Nothing here is a model turn, so
// the Switchboard worker stays free for the next Discuss or Plan.
void BoardView::executeCard(const QString &note)
{
    const QString card = m_detail->cardId();
    if (card.isEmpty())
        return;
    if (!onExecuteCard) {
        m_detail->showError(QStringLiteral("This window cannot open a terminal pane for the card."));
        return;
    }
    const QJsonObject front = m_detail->front();
    // In this order on the worker's stdin: the hash-checked update first, while the hash is the
    // one the card was read at, then the move (which re-reads the file), then the note.
    if (front.value(QStringLiteral("assignee")).toString() != QStringLiteral("agent"))
        send({{QStringLiteral("type"), QStringLiteral("board_update")}, {QStringLiteral("id"), nextRequestId()},
              {QStringLiteral("card"), card}, {QStringLiteral("base_hash"), m_detail->hash()},
              {QStringLiteral("patch"), QJsonObject{{QStringLiteral("fields"),
                                                     QJsonObject{{QStringLiteral("assignee"), QStringLiteral("agent")}}}}}});
    if (m_detail->status() != QStringLiteral("in-progress")) {
        const QString id = nextRequestId();
        m_pendingNotes.insert(id, QStringLiteral("Moved #%1 to In progress · Execute").arg(card));
        send({{QStringLiteral("type"), QStringLiteral("board_move")}, {QStringLiteral("id"), id},
              {QStringLiteral("card"), card}, {QStringLiteral("status"), QStringLiteral("in-progress")},
              {QStringLiteral("reason"), QStringLiteral("Execute: handed to a terminal pane")}});
    }
    send({{QStringLiteral("type"), QStringLiteral("board_comment")}, {QStringLiteral("card"), card},
          {QStringLiteral("kind"), QStringLiteral("progress")},
          {QStringLiteral("text"), QStringLiteral("Execute · handed to a new terminal pane beside the "
                                                  "Switchboard, whose agent works on it and records "
                                                  "its commits in `links.commits`.")
                                       + (note.isEmpty() ? QString()
                                                         : QStringLiteral("\n\n") + note)}});
    onExecuteCard(card, board::executeTask(card, m_detail->title(), m_detail->hasPlan(),
                                           m_detail->hasAcceptance(), note));
}

// Verify (#T71W): the card goes to a terminal pane on the verifier the worker recommends — a
// different provider family from the one that implemented it. The board records the hand-off the
// way Execute does, minus the two writes that would be wrong here: the card keeps its QA status
// (only the verdict moves it) and its assignee (the implementer is still the implementer). Then
// the window opens the pane on that runner (`onVerifyCard`), whose agent — or guest CLI — gets the
// QA brief (board::verifyTask).
void BoardView::verifyCard(const QString &note)
{
    const QString card = m_detail->cardId();
    if (card.isEmpty())
        return;
    if (!onVerifyCard) {
        m_detail->showError(QStringLiteral("This window cannot open a terminal pane for the card."));
        return;
    }
    const QJsonObject qa = m_detail->qa();
    const QString runner = board::verifyRunner(qa);
    if (runner.isEmpty())
        return;                       // CardDetail::verify() has already said why on the card
    const QString label = board::verifyLabel(qa);
    const QString why = qa.value(QStringLiteral("recommended")).toObject()
                            .value(QStringLiteral("why")).toString();
    QString text = QStringLiteral("Verify · handed to a new terminal pane on %1").arg(label);
    if (!why.isEmpty())
        text += QStringLiteral(" · ") + why;
    if (!note.isEmpty())
        text += QStringLiteral("\n\n") + note;
    send({{QStringLiteral("type"), QStringLiteral("board_comment")}, {QStringLiteral("card"), card},
          {QStringLiteral("kind"), QStringLiteral("progress")}, {QStringLiteral("text"), text}});
    // The canonical signature is the `qa` block's; the card's own field is the fallback for a
    // worker that sends no `qa` (and then there is no runner either, so this is belt and braces).
    QString implementedBy = qa.value(QStringLiteral("implemented_by")).toString();
    if (implementedBy.isEmpty())
        implementedBy = m_detail->front().value(QStringLiteral("implemented_by")).toString();
    onVerifyCard(card, runner,
                 board::verifyTask(card, m_detail->title(), label, implementedBy, note));
}

void BoardView::saveCardEdit(const QJsonObject &patch, const QString &baseHash)
{
    const QString card = m_detail->cardId();
    if (card.isEmpty() || patch.isEmpty())
        return;
    if (baseHash.isEmpty()) {
        showNotice(QStringLiteral("#%1 cannot be saved: reopen the card and try again.").arg(card), true);
        return;
    }
    const QString id = nextRequestId();
    m_pendingNotes.insert(id, QStringLiteral("Saved #%1").arg(card));
    send({{QStringLiteral("type"), QStringLiteral("board_update")}, {QStringLiteral("id"), id},
          {QStringLiteral("card"), card}, {QStringLiteral("base_hash"), baseHash},
          {QStringLiteral("patch"), patch}});
}

void BoardView::closeDetail()
{
    m_follow->stop();
    m_detail->hide();
    updateDetailLayout();
    focusInput();
}

void BoardView::focusFilter()
{
    // The filter is the list page's, so `/` from a card that has the pane to itself goes back to
    // the board first rather than typing into a box nobody can see.
    if (m_listPane->isHidden())
        closeDetail();
    m_filter->setFocus();
    m_filter->selectAll();
}

void BoardView::selectCard(const QString &id)
{
    m_selected = id;
    const int at = board::rowOfCard(m_rows, id);
    if (at >= 0)
        selectRow(at);
}

void BoardView::copyReference()
{
    if (m_selected.isEmpty())
        return;
    QApplication::clipboard()->setText(QStringLiteral("#") + m_selected);
    showNotice(QStringLiteral("Copied #%1").arg(m_selected), false);
}

void BoardView::sendSelectionToTerminal()
{
    if (m_selected.isEmpty() || !onSendToTerminal)
        return;
    onSendToTerminal(QStringLiteral("#") + m_selected + QLatin1Char(' '));
}

void BoardView::openSelectedFile()
{
    const board::Card *card = m_model.card(m_selected);
    if (card && onOpenFile && !card->path.isEmpty())
        onOpenFile(m_workspace + QLatin1Char('/') + card->path);
}

void BoardView::moveSelected()
{
    if (m_selected.isEmpty())
        return;
    QMenu menu(this);
    const QList<board::Column> columns = m_model.sections();
    const board::Card *card = m_model.card(m_selected);
    const QString here = card ? m_model.sectionOf(*card) : QString();
    int index = 1;
    for (const board::Column &column : columns) {
        QAction *action = menu.addAction(QStringLiteral("&%1  %2").arg(index++).arg(column.title));
        action->setEnabled(column.id != here);
        const QString id = column.id;
        connect(action, &QAction::triggered, this, [this, id] { moveCard(m_selected, id, {}, {}); });
    }
    menu.addSeparator();
    for (const board::Tab &tab : m_model.tabs()) {
        if (tab.folder.isEmpty() || (card && tab.id == card->tab))
            continue;
        QAction *action = menu.addAction(QStringLiteral("Move to %1").arg(tab.title));
        const QString id = tab.id;
        connect(action, &QAction::triggered, this, [this, id] { moveToTab(m_selected, id); });
    }
    // Under the selected row rather than wherever the mouse happens to be.
    QPoint at = QCursor::pos();
    if (QListWidgetItem *current = m_list->currentItem())
        at = m_list->viewport()->mapToGlobal(m_list->visualItemRect(current).bottomLeft());
    menu.exec(at);
}

void BoardView::focusInput()
{
    const int at = board::rowOfCard(m_rows, m_selected);
    if (at >= 0) {
        selectRow(at);
        return;
    }
    const int first = board::stepRow(m_rows, -1, 1);
    if (first >= 0) {
        selectRow(first);
        return;
    }
    // No card to stand on (an empty board, or a filter with no match): the view itself takes the
    // keys, so `n` still adds and `/` still filters.
    setFocus();
}

// Up/Down walk the card rows of the whole list, stepping over the section headers, so the
// selection crosses a section break without a detour (design 4.6).
void BoardView::step(int delta)
{
    const int at = m_list->currentRow();
    const int from = at >= 0 ? at : (delta > 0 ? -1 : int(m_rows.size()));
    const int next = board::stepRow(m_rows, from, delta);
    if (next >= 0)
        selectRow(next);
}

// Alt+Shift+Up/Down: one place up or down inside the card's own section.
void BoardView::reorder(int delta)
{
    const int at = board::rowOfCard(m_rows, m_selected);
    if (at < 0)
        return;
    const QString columnId = m_rows.at(at).columnId;
    const QStringList order = board::cardsInSection(m_rows, columnId);
    const int from = order.indexOf(m_selected);
    const int slot = from + delta;
    if (from < 0 || slot < 0 || slot >= order.size())
        return;
    const auto [before, after] = board::placement(order, m_selected, slot);
    moveCard(m_selected, columnId, before, after);
}

// Alt+Shift+Left/Right: to the previous or next status, which is the section above or below.
void BoardView::shiftSection(int delta)
{
    const int at = board::rowOfCard(m_rows, m_selected);
    if (at < 0)
        return;
    const QStringList ids = columnIds();
    const int here = ids.indexOf(m_rows.at(at).columnId);
    const int next = here + delta;
    if (here < 0 || next < 0 || next >= ids.size())
        return;
    moveCard(m_selected, ids.at(next), {}, {});
}

// Left folds the section the selection is in.
void BoardView::foldSelected()
{
    const int at = board::rowOfCard(m_rows, m_selected);
    if (at < 0)
        return;
    toggleSection(m_rows.at(at).columnId);
}

// Right unfolds the folded section nearest the selection, above or below, and stands on its
// first card. Nearest, not "the next one down": right after Left the folded header is next to
// the selection, so the two keys undo each other, and at the end of the list Right opens Done
// rather than jumping back to whatever was folded at the top.
void BoardView::unfoldNearest()
{
    int from = board::rowOfCard(m_rows, m_selected);
    if (from < 0)
        from = 0;
    QString target;
    int best = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).kind != board::Row::Section || !m_rows.at(i).collapsed)
            continue;
        const int distance = qAbs(i - from);
        if (best < 0 || distance < best) {
            best = distance;
            target = m_rows.at(i).columnId;
        }
    }
    if (target.isEmpty())
        return;
    toggleSection(target);
    const int first = board::stepRow(m_rows, board::rowOfSection(m_rows, target), 1);
    if (first >= 0 && m_rows.at(first).columnId == target)
        selectRow(first);
}

// Keys that work anywhere in the pane that is not a text field: the list, the view itself.
bool BoardView::handleBoardKey(QKeyEvent *key)
{
    const auto mods = key->modifiers() & ~Qt::KeypadModifier;
    if (mods == Qt::ControlModifier && key->key() == Qt::Key_Z) {
        undoLast();
        return true;
    }
    if (mods != Qt::NoModifier && mods != Qt::ShiftModifier)
        return false;
    const QString text = key->text();
    if (text == QStringLiteral("n")) {
        quickAdd();
        return true;
    }
    if (text == QStringLiteral("/")) {
        focusFilter();
        return true;
    }
    // `e` edits the open card wherever the keyboard is inside the pane — the rows, the card's
    // document, or the pane itself when an open card left the focus nowhere in particular.
    if (text == QStringLiteral("e") && (detailOpen() || !m_selected.isEmpty())) {
        editSelected();
        return true;
    }
    // `p` plans, `x` executes and `v` verifies the open card, or the selected one (opening it
    // first) (#XS6Q; `v` is #T71W).
    if ((text == QStringLiteral("p") || text == QStringLiteral("x") || text == QStringLiteral("v"))
        && (detailOpen() || !m_selected.isEmpty())) {
        cardAction(text == QStringLiteral("p") ? QStringLiteral("plan")
                   : text == QStringLiteral("v") ? QStringLiteral("verify")
                                                 : QStringLiteral("execute"));
        return true;
    }
    if (key->key() == Qt::Key_Escape && detailOpen()) {
        closeDetail();
        return true;
    }
    return false;
}

void BoardView::keyPressEvent(QKeyEvent *event)
{
    if (!handleBoardKey(event))
        QWidget::keyPressEvent(event);
}

// While a card is dragged near the top or bottom of the list, scroll that way: otherwise a
// section off screen can never be dropped on.
void BoardView::autoScrollDuringDrag()
{
    QWidget *viewport = m_list->viewport();
    const QPoint at = viewport->mapFromGlobal(QCursor::pos());
    if (at.x() < -40 || at.x() > viewport->width() + 40)
        return;
    QScrollBar *bar = m_list->verticalScrollBar();
    if (at.y() >= -8 && at.y() < 36)
        bar->setValue(bar->value() - 14);
    else if (at.y() > viewport->height() - 36 && at.y() <= viewport->height() + 8)
        bar->setValue(bar->value() + 14);
}

bool BoardView::eventFilter(QObject *object, QEvent *event)
{
    // The list pane's width, not the view's, decides whether the tools row wraps: with a card
    // open beside it, the list has only its half of the splitter.
    if (object == m_listPane && event->type() == QEvent::Resize) {
        layoutListTools();
        return false;
    }
    // An empty quick-add field that loses the focus has been abandoned; one with text in it is
    // waiting for the person to come back to it. Enter leaves it open and focused either way.
    if (object == m_quickAdd && event->type() == QEvent::FocusOut) {
        if (m_quickAdd->text().trimmed().isEmpty())
            closeQuickAdd();
        return false;
    }
    if (event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(object, event);
    auto *key = static_cast<QKeyEvent *>(event);
    if (object == m_filter) {
        if (key->key() == Qt::Key_Escape) {
            if (m_filter->text().isEmpty())
                focusInput();
            else
                m_filter->clear();
            return true;
        }
        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            focusInput();
            return true;
        }
        return QWidget::eventFilter(object, event);
    }
    if (object == m_quickAdd) {
        if (key->key() == Qt::Key_Escape) {
            closeQuickAdd();
            focusInput();
            return true;
        }
        return QWidget::eventFilter(object, event);
    }
    if (object != m_list)
        return QWidget::eventFilter(object, event);

    const auto mods = key->modifiers() & ~Qt::KeypadModifier;
    if (mods == (Qt::AltModifier | Qt::ShiftModifier)) {
        switch (key->key()) {
        case Qt::Key_Left:
            shiftSection(-1);
            return true;
        case Qt::Key_Right:
            shiftSection(1);
            return true;
        case Qt::Key_Up:
            reorder(-1);
            return true;
        case Qt::Key_Down:
            reorder(1);
            return true;
        default:
            break;
        }
    }
    if (handleBoardKey(key))
        return true;
    if (mods == Qt::NoModifier) {
        switch (key->key()) {
        // Up/Down (and Page/Home/End) are taken over from QListWidget, which would leave the
        // current row standing on a section header it cannot select.
        case Qt::Key_Up:
            step(-1);
            return true;
        case Qt::Key_Down:
            step(1);
            return true;
        case Qt::Key_PageUp:
        case Qt::Key_PageDown: {
            const int page = qMax(1, m_list->viewport()->height() / qMax(1, rowHeight()) - 1);
            for (int i = 0; i < page; ++i)
                step(key->key() == Qt::Key_PageUp ? -1 : 1);
            return true;
        }
        case Qt::Key_Home:
        case Qt::Key_End: {
            const int edge = key->key() == Qt::Key_Home
                                 ? board::stepRow(m_rows, -1, 1)
                                 : board::stepRow(m_rows, int(m_rows.size()), -1);
            if (edge >= 0)
                selectRow(edge);
            return true;
        }
        case Qt::Key_Left:
            foldSelected();
            return true;
        case Qt::Key_Right:
            unfoldNearest();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            openSelected();
            return true;
        case Qt::Key_Escape:
            if (!m_filter->text().isEmpty()) {
                m_filter->clear();
                return true;
            }
            return false;
        default:
            break;
        }
        const QString text = key->text();
        if (text == QStringLiteral("m")) {
            moveSelected();
            return true;
        }
        if (text == QStringLiteral("c")) {
            if (detailOpen() && m_detail->cardId() == m_selected) {
                m_detail->focusReply();
            } else {
                m_replyOnOpen = true;
                openSelected();
            }
            return true;
        }
        if (text == QStringLiteral("y")) {
            copyReference();
            return true;
        }
        if (text == QStringLiteral("t")) {
            sendSelectionToTerminal();
            return true;
        }
        if (text == QStringLiteral("o")) {
            openSelectedFile();
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}

// One card row's height, for a page step.
int BoardView::rowHeight() const
{
    const int at = board::stepRow(m_rows, -1, 1);
    if (at >= 0 && at < m_list->count())
        return qMax(16, m_list->visualItemRect(m_list->item(at)).height());
    return qMax(16, QFontMetrics(m_list->font()).height() + 10);
}

// ------------------------------------------------------------------- the cleanup run (19.9)

// The one button, in its three states. Clicking it while a run is going stops that run; clicking
// it otherwise starts a **preview**, never the real thing: a cleanup rewrites many of the owner's
// files, and `dry_run` produces the same plan while every write tool refuses. The Apply button in
// the result panel is the only way to a run that writes.
void BoardView::requestCleanup()
{
    if (cleanupRunning()) {
        send({{QStringLiteral("type"), QStringLiteral("cancel")}});
        showCleanupProgress(QStringLiteral("stopping"));
        return;
    }
    startCleanup(true);
}

void BoardView::startCleanup(bool dryRun)
{
    if (cleanupRunning())
        return;
    hideCleanupPanel();
    m_cleanupDry = dryRun;
    m_cleanupWrites = 0;
    m_cleanupCards = m_model.total();
    m_cleanupChangelog.clear();
    m_cleanupRequest = nextRequestId();
    // Held from the click, not from the worker's answer: the button has to say Stop at once, and
    // a card's ask has to be refused here rather than be sent and bounced.
    m_cleanupRun = QStringLiteral("starting");
    m_cleanupClock.start();
    updateCleanupButton();
    showCleanupProgress(QStringLiteral("starting"));
    send({{QStringLiteral("type"), QStringLiteral("board_cleanup")},
          {QStringLiteral("id"), m_cleanupRequest},
          {QStringLiteral("dry_run"), dryRun}});
}

void BoardView::endCleanup()
{
    m_cleanupRun.clear();
    m_cleanupRequest.clear();
    if (m_cleanupGuard)
        m_cleanupGuard->stop();
    // "A cleanup is running" stops being true here, so the line that said it on a card goes with
    // the run — but only on the card it was said about, and only if that card is still open.
    if (!m_busyCard.isEmpty()) {
        if (m_busyCard == m_detail->cardId())
            m_detail->showError(QString());
        m_busyCard.clear();
    }
    updateCleanupButton();
}

void BoardView::updateCleanupButton()
{
    if (!m_cleanup)
        return;
    const bool running = cleanupRunning();
    m_cleanup->setText(running ? QStringLiteral("Stop") : QStringLiteral("Clean up"));
    m_cleanup->setToolTip(running
        ? (m_cleanupDry
               ? QStringLiteral("Stop the preview. Nothing has been written either way.")
               : QStringLiteral("Stop the cleanup. What it has already written stays, and the "
                                "changelog says what that was."))
        : QStringLiteral("Have the agent tidy the board: merge or split sections and cards, "
                         "review statuses. The first run is a preview that writes nothing."));
    // A property, not a font: a stylesheet rule with a pseudo-state that changed the font would
    // paint one width and measure another (tests/buttonfit_test.cpp).
    m_cleanup->setProperty("running", running);
    m_cleanup->style()->unpolish(m_cleanup);
    m_cleanup->style()->polish(m_cleanup);
    layoutListTools();
}

// A cleanup takes minutes, so its progress line stays up instead of timing out like a move's
// notice. `step` is the tool it is on, the last write it made, or a word for a state.
void BoardView::showCleanupProgress(const QString &step)
{
    const qint64 seconds = m_cleanupClock.isValid() ? m_cleanupClock.elapsed() / 1000 : 0;
    QString text = m_cleanupDry ? QStringLiteral("Cleanup preview") : QStringLiteral("Cleanup");
    text += QStringLiteral(" · %1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    if (!step.isEmpty())
        text += QStringLiteral(" · ") + step;
    if (m_cleanupWrites > 0)
        text += QStringLiteral(" · %1 %2").arg(m_cleanupWrites)
                    .arg(m_cleanupDry ? QStringLiteral("proposed") : QStringLiteral("written"));
    if (m_cleanupDry)
        text += QStringLiteral(" · nothing is written");
    m_noticeText->setText(text);
    m_notice->setProperty("error", false);
    m_notice->style()->unpolish(m_notice);
    m_notice->style()->polish(m_notice);
    m_noticeUndo->setVisible(false);
    m_notice->show();
    placeNotice();
    m_noticeTimer->stop();      // it goes when the run does, not on a timer
}

void BoardView::hideCleanupPanel()
{
    if (m_cleanupPanel)
        m_cleanupPanel->hide();
}

// 19.9's events, kept away from any card thread. They carry `cleanup: true` and a `run_id` and
// never a `card_id`, so this runs before the card routing and swallows every one of them.
bool BoardView::handleCleanupEvent(const QString &type, const QJsonObject &event)
{
    if (type == QStringLiteral("board_cleanup_started")) {
        m_cleanupRun = event.value(QStringLiteral("run_id")).toString();
        if (m_cleanupRun.isEmpty())
            m_cleanupRun = QStringLiteral("running");
        m_cleanupDry = event.value(QStringLiteral("dry_run")).toBool();
        m_cleanupChangelog = event.value(QStringLiteral("changelog")).toString();
        m_cleanupCards = event.value(QStringLiteral("cards")).toInt(m_model.total());
        m_cleanupWrites = 0;
        if (!m_cleanupClock.isValid())
            m_cleanupClock.start();
        updateCleanupButton();
        showCleanupProgress(QStringLiteral("reading %1 cards").arg(m_cleanupCards));
        return true;
    }
    if (type == QStringLiteral("board_cleanup_summary")) {
        endCleanup();
        m_noticeTimer->stop();
        m_notice->hide();
        showCleanupSummary(event);
        return true;
    }
    if (!event.value(QStringLiteral("cleanup")).toBool())
        return false;
    // Everything below belongs to the run, and to nothing else.
    if (type == QStringLiteral("board_activity")) {
        ++m_cleanupWrites;
        const QString card = event.value(QStringLiteral("id")).toString();
        QString line = event.value(QStringLiteral("summary")).toString();
        if (!card.isEmpty())
            line = QStringLiteral("#%1 %2").arg(card, line);
        showCleanupProgress(line);
        return true;
    }
    if (type == QStringLiteral("tool_started")) {
        // One concise line rather than the bare tool name (protocol § 23): "reading agent.py",
        // "running git status". Without a `label` this falls back to the legacy preview, and
        // without that to the tool's own name, so an older worker says as much as it used to.
        const toollabel::Label label = toollabel::fromEvent(event);
        const QString line = label.runningLine().isEmpty() ? label.line() : label.runningLine();
        showCleanupProgress(line.isEmpty() ? event.value(QStringLiteral("tool")).toString() : line);
        return true;
    }
    if (type == QStringLiteral("tool_result")) {
        // Past tense with its stats once the call has landed: "read 4 cards · 120 lines". A call
        // that never happened says why on the same line, which is what line() already puts there.
        const toollabel::Label label = toollabel::fromEvent(event);
        if (!label.line().isEmpty())
            showCleanupProgress(label.line());
        return true;
    }
    if (type == QStringLiteral("status")) {
        showCleanupProgress(event.value(QStringLiteral("text")).toString());
        return true;
    }
    if (type == QStringLiteral("error")) {
        showCleanupProgress(QStringLiteral("failed: %1")
                                .arg(event.value(QStringLiteral("text")).toString()));
    }
    if (type == QStringLiteral("done") || type == QStringLiteral("error")
        || type == QStringLiteral("cancelled")) {
        // The summary is the last word (19.9) and ends the run. This only catches a worker that
        // died before sending one, which would otherwise leave the button on Stop for good.
        if (!m_cleanupGuard) {
            m_cleanupGuard = new QTimer(this);
            m_cleanupGuard->setSingleShot(true);
            m_cleanupGuard->setInterval(20000);
            connect(m_cleanupGuard, &QTimer::timeout, this, [this] {
                if (!cleanupRunning())
                    return;
                endCleanup();
                showNotice(QStringLiteral("The cleanup ended without a summary. Its changelog, if "
                                          "it wrote one, has what it did."), true);
            });
        }
        m_cleanupGuard->start();
    }
    // delta, answer, thinking, turn_summary and the rest of a turn's chatter: the run's prose is
    // in its report and its changelog, not streamed over the board.
    return true;
}

void BoardView::showCleanupSummary(const QJsonObject &summary)
{
    const QString outcome = summary.value(QStringLiteral("outcome")).toString();
    const bool dry = summary.value(QStringLiteral("dry_run")).toBool();
    const QJsonObject counts = summary.value(QStringLiteral("counts")).toObject();
    const QJsonArray changes = summary.value(QStringLiteral("changes")).toArray();
    const QJsonArray refusals = summary.value(QStringLiteral("refusals")).toArray();
    const int writes = counts.value(QStringLiteral("writes")).toInt();
    const int proposed = counts.value(QStringLiteral("proposed")).toInt();
    const double seconds = summary.value(QStringLiteral("seconds")).toDouble();
    m_cleanupChangelog = summary.value(QStringLiteral("changelog")).toString();

    // The heading says which of the two ran, and how it ended, in that order: whether anything
    // was written to the owner's files is the first thing to know.
    QString head = dry ? QStringLiteral("Cleanup preview — nothing was written")
                       : QStringLiteral("Cleanup — the board was rewritten");
    if (outcome == QStringLiteral("cancelled"))
        head += dry ? QStringLiteral(" · stopped") : QStringLiteral(" · stopped part way");
    else if (outcome == QStringLiteral("error"))
        head += QStringLiteral(" · it failed");
    QStringList facts;
    facts << QStringLiteral("%1 card%2 before, %3 after")
                 .arg(summary.value(QStringLiteral("cards_before")).toInt(m_cleanupCards))
                 .arg(summary.value(QStringLiteral("cards_before")).toInt(m_cleanupCards) == 1
                          ? QString() : QStringLiteral("s"))
                 .arg(summary.value(QStringLiteral("cards_after")).toInt(m_cleanupCards));
    facts << (dry ? QStringLiteral("%1 proposed").arg(proposed)
                  : QStringLiteral("%1 written").arg(writes));
    for (const QString &action : {QStringLiteral("merge"), QStringLiteral("split"),
                                  QStringLiteral("move"), QStringLiteral("update"),
                                  QStringLiteral("create"), QStringLiteral("comment"),
                                  QStringLiteral("sections")}) {
        const int n = counts.value(action).toInt();
        if (n > 0)
            facts << QStringLiteral("%1 %2").arg(n).arg(action);
    }
    if (seconds > 0)
        facts << QStringLiteral("%1:%2").arg(int(seconds) / 60)
                     .arg(int(seconds) % 60, 2, 10, QLatin1Char('0'));
    m_cleanupHead->setText(QStringLiteral("<b>%1</b><br>%2")
                               .arg(head.toHtmlEscaped(), facts.join(QStringLiteral(" · ")).toHtmlEscaped()));

    QString html;
    if (changes.isEmpty()) {
        html += QStringLiteral("<p>%1</p>")
                    .arg(dry ? QStringLiteral("It proposed no change.")
                             : QStringLiteral("It changed nothing. Doing less and saying why is a "
                                              "good outcome for this run."));
    } else {
        html += QStringLiteral("<p><b>%1</b></p><ul>")
                    .arg(dry ? QStringLiteral("What it would do") : QStringLiteral("What it did"));
        for (const QJsonValue &value : changes) {
            const QJsonObject change = value.toObject();
            const QString id = change.value(QStringLiteral("card_id")).toString();
            const QString action = change.value(QStringLiteral("action")).toString();
            const QString text = change.value(QStringLiteral("summary")).toString();
            const QString path = change.value(QStringLiteral("path")).toString();
            QString line = QStringLiteral("<b>%1</b>").arg(action.toHtmlEscaped());
            if (!id.isEmpty())
                line += QStringLiteral(" <a href=\"card:%1\">#%1</a>").arg(id.toHtmlEscaped());
            if (!text.isEmpty())
                line += QStringLiteral(" — %1").arg(text.toHtmlEscaped());
            if (!path.isEmpty())
                line += QStringLiteral(" · <a href=\"%1\">%2</a>")
                            .arg(path.toHtmlEscaped(), QFileInfo(path).fileName().toHtmlEscaped());
            html += QStringLiteral("<li>%1</li>").arg(line);
        }
        html += QStringLiteral("</ul>");
        if (summary.value(QStringLiteral("truncated")).toBool())
            html += QStringLiteral("<p>Only the first %1 are listed here; the changelog has every "
                                   "one.</p>").arg(changes.size());
    }
    if (!refusals.isEmpty()) {
        html += QStringLiteral("<p><b>Refused (%1)</b></p><ul>").arg(refusals.size());
        for (const QJsonValue &value : refusals) {
            const QJsonObject refusal = value.toObject();
            html += QStringLiteral("<li>%1 — %2</li>")
                        .arg(refusal.value(QStringLiteral("tool")).toString().toHtmlEscaped(),
                             refusal.value(QStringLiteral("error")).toString().toHtmlEscaped());
        }
        html += QStringLiteral("</ul>");
    }
    const QString report = summary.value(QStringLiteral("report")).toString();
    if (!report.isEmpty()) {
        html += QStringLiteral("<p><b>The agent's report</b></p>");
        const QStringList paragraphs = report.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts);
        for (const QString &paragraph : paragraphs)
            html += QStringLiteral("<p>%1</p>").arg(paragraph.trimmed().toHtmlEscaped());
    }
    m_cleanupBody->setHtml(html);
    m_cleanupBody->verticalScrollBar()->setValue(0);

    // Apply only follows a preview that found something, and only when it ran to the end: half a
    // plan is not a plan.
    m_cleanupApply->setVisible(dry && outcome == QStringLiteral("done") && !changes.isEmpty());
    m_cleanupLog->setVisible(!m_cleanupChangelog.isEmpty());
    m_cleanupLog->setToolTip(m_cleanupChangelog.isEmpty()
                                 ? QString()
                                 : QStringLiteral("Open %1 in a pane").arg(m_cleanupChangelog));
    m_cleanupPanel->setProperty("failed", outcome == QStringLiteral("error"));
    m_cleanupPanel->style()->unpolish(m_cleanupPanel);
    m_cleanupPanel->style()->polish(m_cleanupPanel);
    m_cleanupPanel->show();
    // The list keeps the keyboard: every control in the panel is NoFocus, so nothing moved.
}

}  // namespace relay

