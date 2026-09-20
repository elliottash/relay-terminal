// SPDX-License-Identifier: AGPL-3.0-or-later
#include "BoardPane.h"
#include "CurrentTextComboBox.h"   // the model box, shared with the terminal pane (#BRD3)
#include "Projects.h"   // which folder of a project is its board: `switchboard/`, else `issues/`
#include "ToolLabel.h"

#include "BoardChat.h"   // the Switchboard page agent's panel (#8YQ9, protocol 19.18)

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
#include <QFocusEvent>
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
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
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
// The date columns at the right of a row (owner, 2026-09-19: "add a 'created' and 'updated'
// column"): the gap between the two and before the badges, and the room an `#ID` is owed when
// deciding whether the pane can carry the columns at all. That room is a constant rather than the
// card's own id width, so the header over the list can answer the same question the rows do — a
// header whose labels had gone while the cells were still drawn would be worse than no header.
constexpr int kDateGap = 8;
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
    // The pane holding the card (#R9G7): the link colour, because on the card page the same chip
    // *is* a link to that pane and the row should read as the same thing. Once the pane has gone
    // the chip is muted like the rest of the history.
    case board::Badge::Session:
        return {theme::Link, mix(theme::Link, theme::Surface, 0.55)};
    case board::Badge::SessionClosed:
        return {theme::TextMuted, theme::Border};
    case board::Badge::Thread:
        return {theme::TextMuted, QColor()};
    default:
        return {theme::TextMuted, theme::Border};
    }
}

// The flag at the head of a card row (#VKFV): an empty ring when the card carries no priority —
// unlit hardware, like the jack rings of an empty board — and a filled disc in the priority's
// own colour otherwise. The colours are the board's material tokens (Theme.h), so a theme
// switch recolours every flag and a light theme never paints white on cream.
void drawPriorityFlag(QPainter &painter, const QRect &box, int priority)
{
    const QColor ink = priority < 0 ? theme::BoardPriorityLow
                     : priority == 1 ? theme::BoardPriorityOne
                     : priority == 2 ? theme::BoardPriorityTwo
                     : priority >= 3 ? theme::BoardPriorityThree
                                     : QColor();
    const QPointF middle(box.center());
    const QRectF circle(QPointF(middle.x() - 4.5, middle.y() - 4.5), QSizeF(9, 9));
    if (ink.isValid()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        painter.drawEllipse(circle);
    } else {
        painter.setPen(QPen(theme::BoardMetalDim, 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(circle.adjusted(0.5, 0.5, -0.5, -0.5));
    }
}

// The same flag as a control of its own (#DPJB), for the card page: the detail's header draws it
// with the row's own function, and a click shifts it exactly as a click on the row's does — a left
// click raises, a right click lowers. `onStep` is wired to `BoardView::setCardPriority`, so the page
// and the row write through one path and neither can drift from the other (clamped −1…+3).
class PriorityFlagButton final : public QWidget {
public:
    explicit PriorityFlagButton(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("boardCardFlag"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(20, 20);           // the row's 15 px box, with the click's own room
        setPriority(0);
    }

    int priority() const { return m_priority; }

    void setPriority(int value)
    {
        m_priority = value;
        // The row's own words, so the tooltip reads the same wherever the flag is clicked.
        setToolTip(QStringLiteral("Priority %1 — left-click raises the flag, right-click lowers it")
                       .arg(value > 0 ? QStringLiteral("+%1").arg(value)
                                      : value < 0 ? QStringLiteral("−1")
                                                  : QStringLiteral("0")));
        update();
    }

    std::function<void(int step)> onStep;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        drawPriorityFlag(painter, rect(), m_priority);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        const bool left = event->button() == Qt::LeftButton;
        if ((left || event->button() == Qt::RightButton) && onStep) {
            onStep(left ? 1 : -1);
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

private:
    int m_priority = 0;
};

// The room the list keeps at its right: its frame plus the width a vertical scrollbar takes,
// whether or not one is showing.
int rowReserve(const QListWidget *list)
{
    return list->verticalScrollBar()->sizeHint().width() + 2 + 2 * list->frameWidth();
}

// The width a row's content is drawn in. The column header over the list measures with this too,
// so a header cell sits exactly over the column of cells below it.
int rowContentWidth(const QListWidget *list)
{
    return qMax(120, list->width() - rowReserve(list));
}

// One date column's width: "0000-00-00" in the row's own date font, so both columns are the same
// width, every row lines up with the next, and the header's label is measured over the same cell.
int dateColumnWidth(const QFont &font)
{
    return QFontMetrics(monoFont(font, 0.85)).horizontalAdvance(QStringLiteral("0000-00-00")) + 6;
}

// The room the `#ID` column takes: "#WWWW" in the id's own mono — the widest id the format
// allows — so every id lines up in one fixed column between the flag and the title (#VKFV).
int idColumnWidth(const QFont &font)
{
    return QFontMetrics(monoFont(font, 0.85)).horizontalAdvance(QStringLiteral("#WWWW")) + kIdGap;
}

// Whether a row `width` px wide can carry the two date columns at all: the flag, the `#ID`
// column and the title's floor are owed their room first, and only then does the table get its
// right-hand columns.
bool dateColumnsFit(const QFont &font, int width)
{
    const int keep = kRowPadX + kGlyphWidth + 4 + idColumnWidth(font) + kTitleMin + kDateGap
                     + 2 * dateColumnWidth(font) + kDateGap + kRowPadX;
    return width >= keep;
}

// Where everything on one card row goes, relative to the row's top-left corner. The badges that
// do not fit are already gone (board::fitBadges) and the title is elided into what is left, so a
// narrow pane loses decoration before it loses meaning.
struct CardShape {
    QRect priorityRect, idRect, titleRect, createdRect, updatedRect;
    QString title, created, updated;
    bool dates = false;                 // the two date columns are on this row
    QList<QPair<board::Badge, QRect>> badges;
    int height = 0;
};

// `sessionLive` says whether the pane the card was claimed by is still open (#R9G7): it only
// changes what the claim chip says, but it is measured with the rest of the badges, so it has to
// be known before the row is laid out.
CardShape cardShape(const board::Card &card, bool showStatus, const QFont &font, int width,
                    bool sessionLive = true)
{
    CardShape shape;
    const QFontMetrics metrics(font);
    const QFontMetrics badgeMetrics(smaller(font, 0.85));
    shape.height = qMax(24, metrics.height() + 10);

    // The flag column, then the `#ID` column, then the title (#VKFV): the id is a fixed column
    // of its own now, so the ids of every row line up whatever the titles say.
    int x = kRowPadX;
    shape.priorityRect = QRect(x, 0, kGlyphWidth, shape.height);
    x += kGlyphWidth + 4;
    shape.idRect = QRect(x, 0, idColumnWidth(font) - kIdGap, shape.height);
    x += idColumnWidth(font);

    // The table's right-hand columns first: the badges and the title are what give way to them,
    // and the badges that no longer fit are dropped by board::fitBadges — a narrow pane loses
    // decoration before it loses meaning.
    shape.dates = dateColumnsFit(font, width);
    int contentRight = width - kRowPadX;
    if (shape.dates) {
        const int column = dateColumnWidth(font);
        shape.updatedRect = QRect(contentRight - column, 0, column, shape.height);
        shape.createdRect = QRect(shape.updatedRect.left() - kDateGap - column, 0, column,
                                  shape.height);
        shape.created = board::dateCell(card.created);
        shape.updated = board::dateCell(card.updated);
        contentRight = shape.createdRect.left() - kDateGap;
    }
    const int available = qMax(40, contentRight - x);

    // No single badge may eat the row. A card whose `assignee` holds a sentence is a mistake in
    // the file, but the row still has to be readable, so a badge is capped at a quarter of the
    // width and elided inside its pill.
    const int cap = qMax(60, available / 4);
    QHash<QString, int> widths;
    QList<QPair<board::Badge, int>> measured;
    for (const board::Badge &badge : board::badges(card, showStatus, sessionLive)) {
        // The claim chip is exempt (#R9G7). The cap is there for a badge whose text came out of
        // the card file and could be a sentence; this one is the glyph, eight characters and at
        // most " closed", so it cannot eat the row however narrow the pane is — and elided to
        // "37fa10…" it names nobody, which is the whole of what it is for. It fits whole, or
        // fitBadges drops it and the tooltip still says who holds the card.
        const bool bounded = badge.kind == board::Badge::Session
                             || badge.kind == board::Badge::SessionClosed;
        const int full = badgeMetrics.horizontalAdvance(badge.text) + 12;
        const int w = bounded ? full : qMin(cap, full);
        widths.insert(badge.text, w);
        measured << qMakePair(badge, w);
    }
    // What the title is owed before a badge may have anything: the rest of the row is decoration
    // next to knowing which card this is.
    const int titleFloor = qBound(kTitleMin, available * 45 / 100, 280);
    const QList<board::Badge> kept =
        board::fitBadges(measured, qMax(0, available - titleFloor - 12), kBadgeGap);

    // Right to left from the row's right edge, prepending, so the order on screen ends up the
    // reading order board::badges returned.
    int right = contentRight;
    for (int i = int(kept.size()) - 1; i >= 0; --i) {
        const int w = widths.value(kept.at(i).text);
        const int h = badgeMetrics.height() + 2;
        right -= w;
        shape.badges.prepend(qMakePair(kept.at(i), QRect(right, (shape.height - h) / 2, w, h)));
        right -= kBadgeGap;
    }
    const int titleEnd = shape.badges.isEmpty() ? contentRight : right + kBadgeGap - 10;
    const int titleWidth = qMax(30, titleEnd - x);
    shape.title = metrics.elidedText(card.title.isEmpty() ? QStringLiteral("(untitled)")
                                                          : card.title,
                                     Qt::ElideRight, titleWidth);
    const int drawn = qMin(titleWidth, metrics.horizontalAdvance(shape.title));
    shape.titleRect = QRect(x, 0, drawn, shape.height);
    return shape;
}

// A section header: a chevron, the status name, its count, and a `+` that adds into it.
struct SectionShape {
    QRect chevronRect, titleRect, addRect, triageRect;
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
    // The triage ⚠ (#8YQ9) sits inside the `+`, at the row's end: every section can be checked,
    // including Done, so unlike the `+` its room is reserved on every header rather than only on
    // the ones that take new cards. Both are drawn on hover only — a header at rest is a name.
    int right = width - kRowPadX;
    if (canAdd) {
        shape.addRect = QRect(right - kAddWidth, top, kAddWidth, metrics.height());
        right -= kAddWidth;
    }
    shape.triageRect = QRect(right - kAddWidth, top, kAddWidth, metrics.height());
    right -= kAddWidth;
    shape.titleRect = QRect(x, top, qMax(20, right - x), metrics.height());
    return shape;
}

// The fold row that stands for a section's self-closed cards (#93WR): a chevron and one muted
// line, "3 closed by the agent". It is a row inside the section's cards rather than a header of
// its own, so the chevron sits where the card rows' `#ID` column starts and there is no rule
// above it — the eye reads it as the last of the cards, which is what it stands for.
struct FoldShape {
    QRect chevronRect, titleRect;
    int height = 0;
};

FoldShape foldShape(const QFont &font, int width)
{
    FoldShape shape;
    const QFontMetrics metrics(smaller(font, 0.85));
    shape.height = qMax(22, metrics.height() + 8);
    int x = kRowPadX + kGlyphWidth + 4;
    shape.chevronRect = QRect(x, 0, 12, shape.height);
    x += 14;
    shape.titleRect = QRect(x, 0, qMax(20, width - kRowPadX - x), shape.height);
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

    // Which cards have an agent turn running on them (protocol 19.16). Several can, and the one
    // on screen is not necessarily one of them, so the list is where you see the others working.
    std::function<QString(const QString &)> turnMode;

    // Whether the pane a card was claimed by is still open (#R9G7). Asked once per row as it is
    // measured and once as it is painted, so the list picks a closing pane up at its next repaint
    // rather than holding a stale answer; unset — a test — means every claim reads as live.
    std::function<bool(const QString &)> paneExists;
    bool sessionLive(const board::Card &card) const
    {
        return card.session.isEmpty() || !paneExists || paneExists(card.session);
    }

    // The row's width comes from the list with room for its scrollbar kept whether or not the
    // scrollbar is showing: measured at one width and painted at another, the elision would be
    // computed for a row wider than the one drawn.
    int rowWidth() const { return rowContentWidth(m_list); }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const board::Row *row = rowAt(index.row());
        const int width = rowWidth();
        if (!row)
            return QSize(width, 0);
        if (row->kind == board::Row::Section)
            return QSize(width, sectionShape(option.font, width, index.row() == 0, false).height);
        if (row->kind == board::Row::Fold)
            return QSize(width, foldShape(option.font, width).height);
        const board::Card *card = m_model->card(row->cardId);
        if (!card)
            return QSize(width, 0);
        return QSize(width, cardShape(*card, row->showStatus, option.font, width,
                                     sessionLive(*card)).height);
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
        else if (row->kind == board::Row::Fold)
            paintFold(painter, option, rect, *row);
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

    // The ⚠ of the header at `rowIndex` (#8YQ9), given that row's rect, or an empty rect. Every
    // section has one — a section that takes no new cards can still hold a malformed card.
    QRect triageRectOf(int rowIndex, const QRect &itemRect) const
    {
        const board::Row *row = rowAt(rowIndex);
        if (!row || row->kind != board::Row::Section)
            return QRect();
        return sectionShape(m_list->font(), rowWidth(), rowIndex == 0, adds(row->columnId))
            .triageRect.translated(itemRect.topLeft());
    }

    // The flag at the left of a card row (#VKFV), given that row's rect: the box the ring or the
    // disc is painted in, widened a little because it is a click target, not a mark.
    QRect priorityRectOf(int rowIndex, const QRect &itemRect) const
    {
        const board::Row *row = rowAt(rowIndex);
        if (!row || row->kind != board::Row::Card)
            return QRect();
        return cardShape(board::Card{}, false, m_list->font(), rowWidth())
            .priorityRect.translated(itemRect.topLeft());
    }

    // The label a click at `at` (viewport coordinates) lands on in the card row at `rowIndex`
    // (#3ZAP): labels are the one badge that copies rather than decorates. A null string when
    // the click is anywhere else in the row.
    QString labelBadgeAt(int rowIndex, const QRect &itemRect, const QPoint &at) const
    {
        const board::Row *row = rowAt(rowIndex);
        if (!row || row->kind != board::Row::Card)
            return QString();
        const board::Card *card = m_model->card(row->cardId);
        if (!card)
            return QString();
        const CardShape shape = cardShape(*card, row->showStatus, m_list->font(), rowWidth(),
                                          sessionLive(*card));
        for (const auto &placed : shape.badges)
            if (placed.first.kind == board::Badge::Label
                && placed.second.translated(itemRect.topLeft()).contains(at))
                return placed.first.text;
        return QString();
    }

    // Nothing is created straight into Done: a card gets there by being closed.
    std::function<bool(const QString &columnId)> adds = [](const QString &) { return true; };

private:
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
        // Triage (#8YQ9): the board's own `check()`, scoped to this section's cards. It is
        // deterministic and free — not an agent turn — so it is a click on the header rather
        // than a sentence to the page agent, and what it finds is clickable in its turn.
        if (hover) {
            painter->setFont(smaller(option.font, 0.95));
            painter->setPen(theme::TextMuted);
            painter->drawText(shape.triageRect.translated(origin), Qt::AlignCenter,
                              QStringLiteral("\u26A0"));
        }
    }

    // The self-closed fold row (#93WR): the same chevrons and the same muted text as a folded
    // section header, and the selection band of a card row, because unlike a header this row can
    // be stood on.
    void paintFold(QPainter *painter, const QStyleOptionViewItem &option, const QRect &rect,
                   const board::Row &row) const
    {
        const FoldShape shape = foldShape(option.font, rect.width());
        const QPoint origin = rect.topLeft();
        const bool selected = option.state & QStyle::State_Selected;
        const bool hover = option.state & QStyle::State_MouseOver;
        const bool focused = m_list->hasFocus();
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
        painter->setFont(smaller(option.font, 0.85));
        painter->setPen(selected || hover ? theme::Text : theme::TextMuted);
        painter->drawText(shape.chevronRect.translated(origin), Qt::AlignLeft | Qt::AlignVCenter,
                          row.collapsed ? QStringLiteral("▸") : QStringLiteral("▾"));
        const QFontMetrics metrics(smaller(option.font, 0.85));
        painter->drawText(shape.titleRect.translated(origin), Qt::AlignLeft | Qt::AlignVCenter,
                          metrics.elidedText(row.title, Qt::ElideRight, shape.titleRect.width()));
    }

    void paintCard(QPainter *painter, const QStyleOptionViewItem &option, const QRect &rect,
                   const board::Row &row) const
    {
        const board::Card *card = m_model->card(row.cardId);
        if (!card)
            return;
        const CardShape shape = cardShape(*card, row.showStatus, option.font, rect.width(),
                                          sessionLive(*card));
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
        // The flag column (#VKFV): an empty ring at 0, a yellow disc at −1 and white, pale green
        // then bright green at +1…+3 — clickable, left to raise and right to lower. A card being
        // planned or discussed right now wears the agent's mark over the flag instead, in the
        // agent's colour: the one place the board shows that #B is working while you read #A.
        const QString working = turnMode ? turnMode(row.cardId) : QString();
        if (working.isEmpty()) {
            drawPriorityFlag(*painter, shape.priorityRect.translated(origin), card->priority);
        } else {
            painter->setPen(theme::Agent);
            painter->drawText(shape.priorityRect.translated(origin), Qt::AlignCenter,
                              QStringLiteral("✦"));
        }

        // The `#ID` column before the title: mono and muted, one fixed column for every row.
        painter->setFont(monoFont(option.font, 0.85));
        painter->setPen(theme::TextMuted);
        painter->drawText(shape.idRect.translated(origin), Qt::AlignLeft | Qt::AlignVCenter,
                          card->reference());

        painter->setFont(option.font);
        painter->setPen(card->closed() ? theme::TextMuted : theme::Text);
        painter->drawText(shape.titleRect.translated(origin), Qt::AlignLeft | Qt::AlignVCenter,
                          shape.title);

        // The two date columns, in the same mono as the id: a column of dates reads as a column.
        // A cell the card has nothing to say in stays blank rather than wrong (board::dateCell).
        if (shape.dates) {
            painter->drawText(shape.createdRect.translated(origin),
                              Qt::AlignLeft | Qt::AlignVCenter, shape.created);
            painter->drawText(shape.updatedRect.translated(origin),
                              Qt::AlignLeft | Qt::AlignVCenter, shape.updated);
        }

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

// The header row over the list (owner, 2026-09-19: "change switchboard sorting from a sort
// button to adding header columns that you click on", then "add a 'created' and 'updated'
// column", then the flag column #VKFV): four cells — ⚑, Card, Created, Updated — each one a sort
// of the cards *inside* every section, with the arrow on the one that is on. A cell is placed
// with the same measurements the row delegate draws its columns with, so a label sits exactly
// over the cells it names; the two date cells and their labels go together when the pane is too
// narrow to carry them. The ⚑ is the one cell too narrow for an arrow: the accent colour alone
// says the priority sort is on.
class ColumnHeader final : public QWidget {
public:
    explicit ColumnHeader(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("boardColumnHeader"));
        m_layout = new QHBoxLayout(this);
        m_layout->setSpacing(kDateGap);
        const QList<board::SortColumn> columns{board::SortColumn::Priority, board::SortColumn::Card,
                                               board::SortColumn::Created,
                                               board::SortColumn::Updated};
        for (const board::SortColumn column : columns) {
            auto *cell = new QToolButton(this);
            cell->setObjectName(column == board::SortColumn::Priority
                                    ? QStringLiteral("boardHeaderPriority")
                                : column == board::SortColumn::Card
                                    ? QStringLiteral("boardHeaderCard")
                                : column == board::SortColumn::Created
                                    ? QStringLiteral("boardHeaderCreated")
                                    : QStringLiteral("boardHeaderUpdated"));
            cell->setToolButtonStyle(Qt::ToolButtonTextOnly);
            cell->setCursor(Qt::PointingHandCursor);
            cell->setFocusPolicy(Qt::NoFocus);      // the arrows stay with the list
            cell->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
            connect(cell, &QToolButton::clicked, this, [this, column] {
                if (onSort)
                    onSort(column);
            });
            m_cell[int(column)] = cell;
            // The ⚑ is placed by hand over the rows' flag column, left of the layout: a cell in
            // the layout would push the Card label off the titles' left edge, and the layout's
            // own spacing is the date columns', not the flag's.
            if (column == board::SortColumn::Priority)
                continue;
            m_layout->addWidget(cell);
            // The stretch sits between the card's own label and the two date columns, which are
            // the table's right-hand columns.
            if (column == board::SortColumn::Card)
                m_layout->addStretch(1);
        }
        layoutCells();
        updateCells();
    }

    // Which column was clicked: the pane turns it into the sort that is on.
    std::function<void(board::SortColumn)> onSort;

    // The list the cells line up with: its width decides where they sit and whether the two date
    // columns fit at all (the rows ask the same question of the same width, so a header whose
    // labels had gone while the cells were still drawn cannot happen).
    void setList(QListWidget *list)
    {
        m_list = list;
        layoutCells();
    }

    // The sort that is on, as the list's own (board::Sort): the arrow follows it.
    void setSort(board::Sort sort)
    {
        if (m_ready && sort == m_sort)
            return;
        m_sort = sort;
        m_ready = true;
        updateCells();
    }

protected:
    // The cells' places depend on the width and on the font, so both are re-read when either
    // changes — a theme switch can change the font under a pane that is not resized.
    void resizeEvent(QResizeEvent *) override { layoutCells(); }
    void changeEvent(QEvent *event) override
    {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::FontChange)
            layoutCells();
    }

private:
    // Where the cells sit and which of them are on the page: measured from the list, whose width
    // this header shares, keeping the same reserve at the right that the rows keep for the
    // scrollbar and the same left margin the rows' titles start at.
    void layoutCells()
    {
        const int frame = m_list ? m_list->frameWidth() : 0;
        const int reserve = m_list ? rowReserve(m_list) : 0;
        const int rowWidth = m_list ? rowContentWidth(m_list) : width();
        const QFont base = font();
        m_layout->setContentsMargins(frame + kRowPadX + kGlyphWidth + 4 + idColumnWidth(base), 3,
                                     qMax(0, reserve - frame) + kRowPadX, 3);
        const bool dates = dateColumnsFit(base, rowWidth);
        const int column = dateColumnWidth(base);
        for (int i = 0; i < 4; ++i) {
            QToolButton *cell = m_cell[i];
            if (cell == nullptr)
                continue;
            // The font is set here and never in the stylesheet, so sizeHint() measures what paints.
            QFont cellFont = monoFont(base, 0.85);
            cellFont.setLetterSpacing(QFont::PercentageSpacing, 106);
            cell->setFont(cellFont);
            const int cellHeight = QFontMetrics(cellFont).height() + 6;
            cell->setMinimumHeight(cellHeight);
            if (i == int(board::SortColumn::Priority)) {
                // Over the rows' flag column (#VKFV): the box a row's ring sits in, at the rows'
                // own left edge — placed by hand, vertically centred, because no layout holds it.
                cell->setGeometry(QRect(frame + kRowPadX, qMax(3, (height() - cellHeight) / 2),
                                        kGlyphWidth + 4, cellHeight));
                continue;
            }
            const bool date = i >= int(board::SortColumn::Created);
            if (date)
                cell->setFixedWidth(column);
            cell->setVisible(!date || dates);
        }
    }

    // What each cell says: its name, and its arrow when it is the sort that is on.
    void updateCells()
    {
        const int active = board::sortColumnIndex(m_sort);
        for (int i = 0; i < 4; ++i) {
            QToolButton *cell = m_cell[i];
            if (cell == nullptr)
                continue;
            const board::SortColumn column = board::SortColumn(i);
            QString text = board::columnTitle(column).toUpper();
            // The ⚑ cell is one glyph wide: no arrow fits beside it, and the accent colour the
            // active property turns on says the same thing.
            if (active == i && column != board::SortColumn::Priority)
                text += board::sortAscending(m_sort) ? QStringLiteral(" ▲") : QStringLiteral(" ▼");
            cell->setText(text);
            cell->setToolTip(cellTooltip(column, m_sort));
            cell->setProperty("active", active == i);
            cell->style()->unpolish(cell);
            cell->style()->polish(cell);
        }
    }

    // The tip: what the column is and the orders a click walks through, in the order it walks
    // through them — so the cycle back to the board's own drag order is written where it happens.
    static QString cellTooltip(board::SortColumn column, board::Sort current)
    {
        const board::Sort first = board::nextColumnSort(column, board::Sort::Manual);
        const board::Sort second = board::nextColumnSort(column, first);
        const QString what = column == board::SortColumn::Priority
                                 ? QStringLiteral("the cards' priority flag")
                             : column == board::SortColumn::Card
                                 ? QStringLiteral("the card's title")
                             : column == board::SortColumn::Created
                                 ? QStringLiteral("when the card was created")
                                 : QStringLiteral("when the card last changed");
        if (board::sortColumnIndex(current) == int(column))
            return QStringLiteral("Sorted by %1 — %2. Click again for %3, or once more for the "
                                  "board's own order.")
                .arg(what, board::sortTitle(current).toLower(),
                     board::sortTitle(board::nextColumnSort(column, current)).toLower());
        return QStringLiteral("Order the cards inside each section by %1: %2, then %3, then the "
                              "board's own order — the one drag and drop writes.")
            .arg(what, board::sortTitle(first).toLower(), board::sortTitle(second).toLower());
    }

    QHBoxLayout *m_layout = nullptr;
    QToolButton *m_cell[4] = {nullptr, nullptr, nullptr, nullptr};
    QListWidget *m_list = nullptr;
    board::Sort m_sort = board::Sort::Manual;
    bool m_ready = false;
};

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
    // The self-closed fold row (#93WR): a click shows its cards or puts them away. The row is not
    // a card — it never starts a drag and nothing can be dropped on it.
    std::function<void(const QString &columnId)> onToggleFold;
    // The ⚠ on a section header (#8YQ9): check that section's cards and show what it found.
    std::function<void(const QString &columnId)> onTriageSection;
    // A click on a row's flag: the card and the step, +1 for a left click and −1 for a right
    // one (#VKFV). The pane clamps at −1…+3 and writes it through `board_priority`.
    std::function<void(const QString &cardId, int step)> onPriority;
    // A click on a row's label badge copies its hashtag (#3ZAP) instead of selecting the row.
    std::function<void(const QString &label)> onCopyLabel;
    std::function<QRect(int rowIndex, const QRect &itemRect)> addRectOf, priorityRectOf,
        triageRectOf;
    // The label badge a point lands on, as text, or a null string (#3ZAP).
    std::function<QString(int rowIndex, const QRect &itemRect, const QPoint &at)> labelBadgeAt;
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

    // The label badge under a left-button event, or a null string (#3ZAP): shared by the
    // press, release and double-click guards, so a badge click is one gesture that copies and
    // never becomes a selection or an activation.
    QString labelBadgeUnder(const QMouseEvent *event) const
    {
        if (!labelBadgeAt || event->button() != Qt::LeftButton)
            return QString();
#if QT_VERSION_MAJOR >= 6
        const QPoint at = event->position().toPoint();
#else
        const QPoint at = event->pos();
#endif
        const QModelIndex index = indexAt(at);
        const board::Row *row = rowAt(index.row());
        if (!row || row->kind != board::Row::Card)
            return QString();
        return labelBadgeAt(index.row(), visualRect(index), at);
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
        // The flag at a card row's left end is a control (#VKFV), not a selection: a left click
        // raises that card's priority and a right click lowers it. Neither ever moves the
        // selection, so a burst of clicks walks one card's flag without losing the row the
        // keyboard is on.
        if (row && row->kind == board::Row::Card && priorityRectOf
            && (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)) {
            const QRect flag = priorityRectOf(index.row(), visualRect(index));
            if (flag.isValid() && flag.adjusted(-2, -2, 2, 2).contains(at)) {
                if (onPriority)
                    onPriority(row->cardId, event->button() == Qt::LeftButton ? 1 : -1);
                event->accept();
                return;
            }
        }
        // A label badge copies its hashtag (#3ZAP) rather than selecting the row; the row's
        // other badges still decorate, and a click between them selects as before.
        if (const QString label = labelBadgeUnder(event); !label.isEmpty()) {
            if (onCopyLabel)
                onCopyLabel(label);
            event->accept();
            return;
        }
        // The fold row (#93WR) toggles on a click, like a section header, and the pane stands the
        // selection on it afterwards so Enter and ←/→ carry on from there.
        if (row && row->kind == board::Row::Fold && event->button() == Qt::LeftButton) {
            if (onToggleFold)
                onToggleFold(row->columnId);
            event->accept();
            return;
        }
        if (row && row->kind == board::Row::Section && event->button() == Qt::LeftButton) {
            const QRect add = addRectOf ? addRectOf(index.row(), visualRect(index)) : QRect();
            const QRect triage = triageRectOf ? triageRectOf(index.row(), visualRect(index))
                                              : QRect();
            if (add.isValid() && add.adjusted(-5, -5, 5, 5).contains(at)) {
                if (onAddInSection)
                    onAddInSection(row->columnId);
            } else if (triage.isValid() && triage.adjusted(-5, -5, 5, 5).contains(at)) {
                if (onTriageSection)
                    onTriageSection(row->columnId);
            } else if (onToggleSection) {
                onToggleSection(row->columnId);
            }
            event->accept();
            return;
        }
        QListWidget::mousePressEvent(event);
    }

    // The release of a badge click must not reach the list either (#3ZAP): the press state Qt
    // keeps is stale from an earlier real press on that row, and it would hand the badge's
    // release to the row as a click — a badge is a control, not a selection.
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!labelBadgeUnder(event).isEmpty()) {
            event->accept();
            return;
        }
        QListWidget::mouseReleaseEvent(event);
    }

    // A second click on a badge is another copy, not an activation of the row (#3ZAP).
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        const QString label = labelBadgeUnder(event);
        if (!label.isEmpty()) {
            if (onCopyLabel)
                onCopyLabel(label);
            event->accept();
            return;
        }
        QListWidget::mouseDoubleClickEvent(event);
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

// Model reasoning is untrusted text: drop control characters except newline and tab, exactly as
// every other transcript surface does (src/AgentInternalsView.cpp's sanitize).
static QString sanitizeTrace(const QString &text)
{
    QString clean;
    clean.reserve(text.size());
    for (const QChar c : text) {
        const ushort u = c.unicode();
        if (u == '\n' || u == '\t' || (u >= 0x20 && u != 0x7f && !(u >= 0x80 && u < 0xa0)))
            clean += c;
    }
    return clean;
}

// The one question every delete path asks (card #CYM9): name the card, say exactly what goes,
// and say where recovery stands — Undo for 30 s, git after that only if it ever saw the card.
// Cancel is the default: a delete is never something the keyboard slipped into.
static bool confirmDeleteCard(const QString &id, const QString &title, QWidget *parent)
{
    QMessageBox confirm(QMessageBox::Warning, QStringLiteral("Delete card"),
                        QStringLiteral("Delete #%1 “%2”?").arg(id, title),
                        QMessageBox::Cancel, parent);
    confirm.setInformativeText(QStringLiteral(
        "The card file and its thread are removed from disk. Undo works for 30 seconds; "
        "git still has it if it was committed."));
    auto *remove = confirm.addButton(QStringLiteral("Delete"), QMessageBox::DestructiveRole);
    confirm.exec();
    return confirm.clickedButton() == remove;
}

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
        // The card's priority flag, at the head of the header exactly as at the head of a row
        // (#DPJB): the list's own control, so a card can be flagged from the page it is read on.
        m_flag = new PriorityFlagButton(this);
        m_flag->onStep = [this](int step) {
            if (onPriority)
                onPriority(step);
        };
        top->addWidget(m_flag);
        m_ref = new QLabel(this);
        m_ref->setObjectName(QStringLiteral("boardCardRef"));
        top->addWidget(m_ref);
        top->addStretch();
        m_toPrompt = textButton(QStringLiteral("#ID → prompt (t)"),
                                QStringLiteral("Insert this card's #ID in the terminal's prompt (t)"));
        m_openFile = textButton(QStringLiteral("Open file (o)"),
                                QStringLiteral("Open the card's Markdown file in a pane (o)"));
        top->addWidget(m_toPrompt);
        top->addWidget(m_openFile);
        m_close = new QToolButton(this);
        m_close->setObjectName(QStringLiteral("boardCardClose"));
        m_close->setText(QStringLiteral("×"));
        m_close->setToolTip(QStringLiteral("Close the card (Esc)"));
        m_close->setCursor(Qt::PointingHandCursor);
        top->addWidget(m_close);
        layout->addLayout(top);

        // The title, and at its right the pencil that edits it (owner, #VZ69: "there should be a
        // prominent pencil edit button, rather than the small 'edit' button at the top"). It sits
        // on the thing it edits rather than among the card's other tools, and it is outlined in the
        // accent, so it is the one control on the card that cannot be missed. Its key stays in the
        // label (#QG60), and the pencil says what the words mean before they are read.
        auto *titleRow = new QHBoxLayout;
        titleRow->setSpacing(8);
        m_title = new QLabel(this);
        m_title->setWordWrap(true);
        m_title->setObjectName(QStringLiteral("boardCardTitle"));
        m_title->setCursor(Qt::IBeamCursor);
        m_title->setToolTip(QStringLiteral("Click to edit the title (e)"));
        m_title->installEventFilter(this);
        titleRow->addWidget(m_title, 1);
        // The same line, as a field: editing swaps the two so the title never jumps.
        m_titleEdit = new QLineEdit(this);
        m_titleEdit->setObjectName(QStringLiteral("boardCardTitleEdit"));
        m_titleEdit->setPlaceholderText(QStringLiteral("Title — one line"));
        m_titleEdit->setToolTip(QStringLiteral("Enter saves, Esc cancels"));
        m_titleEdit->installEventFilter(this);
        m_titleEdit->hide();
        titleRow->addWidget(m_titleEdit, 1);
        m_edit = new QToolButton(this);
        m_edit->setObjectName(QStringLiteral("boardEditPencil"));
        m_edit->setText(QStringLiteral("✎ Edit (e)"));
        m_edit->setToolTip(QStringLiteral("Edit the title and the issue text (e)"));
        m_edit->setCursor(Qt::PointingHandCursor);
        m_edit->setFocusPolicy(Qt::NoFocus);
        titleRow->addWidget(m_edit, 0, Qt::AlignTop);
        // The owner's delete (#CYM9): the one action that takes a card off the board rather
        // than closing it, so it asks first and its Undo window is the only soft landing.
        m_delete = new QToolButton(this);
        m_delete->setObjectName(QStringLiteral("boardCardDelete"));
        m_delete->setText(QStringLiteral("⌫ Delete (Del)"));
        m_delete->setToolTip(QStringLiteral("Delete this card and its thread, after a confirm (Del)"));
        m_delete->setCursor(Qt::PointingHandCursor);
        m_delete->setFocusPolicy(Qt::NoFocus);
        titleRow->addWidget(m_delete, 0, Qt::AlignTop);
        layout->addLayout(titleRow);

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
        // While the agent is working on this card, a strip over the box says so and carries the
        // one control that stops it (owner, #VZ69: "'stop' button isnt intuitive, it should be
        // stop planning i guess, or there should be an X next to 'agent planning'"). It is both:
        // the line names the mode, and the button is an ✕ that says what it stops. Before this
        // the running mode's own button turned into a bare "Stop", which only worked while that
        // mode had a button at all — Discuss no longer does.
        m_busyStrip = new QWidget(reply);
        m_busyStrip->setObjectName(QStringLiteral("boardBusyStrip"));
        auto *busyRow = new QHBoxLayout(m_busyStrip);
        busyRow->setContentsMargins(0, 0, 0, 4);
        busyRow->setSpacing(6);
        m_busyLabel = new QLabel(m_busyStrip);
        m_busyLabel->setObjectName(QStringLiteral("boardBusyLabel"));
        busyRow->addWidget(m_busyLabel, 0);
        // What it is doing this second — "reading Pane.h", "step 4/256". A Plan turn is minutes
        // of silent tool calls before its first word, and a card that only said "thinking…" for
        // all of it read as stuck (owner, 2026-09-19: "the planning agent was getting stuck").
        // The cleanup strip has said this since 19.9; a card said nothing.
        m_busyWhat = new QLabel(m_busyStrip);
        m_busyWhat->setObjectName(QStringLiteral("boardBusyWhat"));
        m_busyWhat->setTextInteractionFlags(Qt::NoTextInteraction);
        busyRow->addWidget(m_busyWhat, 1);
        m_stop = new QToolButton(m_busyStrip);
        m_stop->setObjectName(QStringLiteral("boardStop"));
        m_stop->setCursor(Qt::PointingHandCursor);
        m_stop->setFocusPolicy(Qt::NoFocus);
        busyRow->addWidget(m_stop, 0);
        m_busyStrip->hide();
        replyLayout->addWidget(m_busyStrip);

        m_reply = new RichEditor(reply);
        m_reply->setObjectName(QStringLiteral("boardReplyEditor"));
        m_reply->setPlaceholders({QStringLiteral("Reply — Enter discusses, Ctrl+Enter plans, Ctrl+Shift+Enter only comments"),
                                  QStringLiteral("Reply — Enter discusses, Ctrl+Enter plans"),
                                  QStringLiteral("Reply to this card…"), QStringLiteral("Reply…")});
        m_reply->setAutoHeight(2, 8);
        replyLayout->addWidget(m_reply);
        auto *buttons = new QHBoxLayout;
        m_buttonRow = buttons;
        buttons->setSpacing(6);
        buttons->addStretch(1);
        // Discuss and Comment have no buttons (owner, #VZ69: "remove comment / discuss buttons. i
        // would say you just press enter in the prompt box to discuss / comment"). They are what
        // the box itself does — Enter discusses, Ctrl+Shift+Enter only comments — and the
        // placeholder above says so. What is left on the row is the three things that are *not*
        // typing into the box: Plan, and the two that leave the board.
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
        for (QPushButton *button : {m_plan, m_execute, m_verify}) {
            button->setFocusPolicy(Qt::NoFocus);   // Tab stays between the reply box and the card
            button->setProperty("fullLabel", button->text());   // what fitButtons() shortens from
        }
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
        connect(m_plan, &QPushButton::clicked, this, [this] {
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
        connect(m_stop, &QToolButton::clicked, this, [this] {
            if (m_busy && onCancel)
                onCancel();
        });
        connect(m_edit, &QToolButton::clicked, this, [this] {
            if (onEditHint)
                onEditHint();
            beginEdit(false);
        });
        connect(m_delete, &QToolButton::clicked, this, [this] {
            if (onDeleteHint)
                onDeleteHint();
            remove();
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
            const QUrl url(link);
            // The claim chip (#R9G7) is the thread's own `relay-pane:` anchor, so it goes to the
            // same handler: a click on the session reveals the pane that took the card.
            if (url.scheme() == QStringLiteral("relay-pane")) {
                if (onFocusPane)
                    onFocusPane(url.path());
                return;
            }
            if (url.scheme() == QStringLiteral("tag")) {   // the labels row copies (#3ZAP)
                if (onCopyTag)
                    onCopyTag(url.path().isEmpty() ? url.host() : url.path());
                return;
            }
            if (onOpenPath)
                onOpenPath(link);
        });
        connect(m_doc, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
            // The thread's pane links (#HKAP): relay-pane:<session token> reveals that pane —
            // before the external branch, or the desktop would be asked to open it. Any other
            // scheme is a real URL; a bare path opens like a link in the body.
            if (url.scheme() == QStringLiteral("relay-pane")) {
                if (onFocusPane)
                    onFocusPane(url.path());
                return;
            }
            // A label hashtag copies; a `#ID` that names a card zooms to it (#3ZAP).
            if (url.scheme() == QStringLiteral("tag")) {
                if (onCopyTag)
                    onCopyTag(url.path().isEmpty() ? url.host() : url.path());
                return;
            }
            if (url.scheme() == QStringLiteral("card")) {
                if (onOpenCard)
                    onOpenCard((url.path().isEmpty() ? url.host() : url.path()).toUpper());
                return;
            }
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

    // The Switchboard agent's model box on this page (#BRD3). The owner, 2026-09-20: "did we lose
    // the model picker in the switchboard agent … it's the one in the cards, it's different and
    // doesn't have the model picker." It was not lost — the list page's box was reparented into
    // the page agent's composer, where a card cannot see it — but a card's Discuss and Plan run on
    // the very same `switchboard` role, so the page that runs them is the last place the model
    // should be invisible. The view owns the box and fills it from the one state both boxes read
    // (BoardView::rebuildModelBox); the page only gives it its place on the strip and its share of
    // the row's width. Right-aligned under the reply box, left of the three buttons, which is the
    // shape of every other composer strip in Relay.
    void setModelBox(QComboBox *box)
    {
        if (box == nullptr || m_buttonRow == nullptr)
            return;
        m_modelBox = box;
        box->setParent(m_replyFrame);
        m_buttonRow->insertWidget(1, box);   // 0 is the stretch that right-aligns the row
        fitButtons();
    }

    // The box was refilled and its current row is a different length, so the row's arithmetic has
    // to be done again: a long model name is what takes the keys out of the buttons' labels.
    void refitButtons() { fitButtons(); }

    // `mode` is "discuss" or "plan" for an agent turn (protocol 19.10), empty for a plain comment.
    std::function<void(const QString &text, const QString &mode)> onReply;
    // Execute: hand the card to a terminal pane. `note` is what was in the reply box.
    std::function<void(const QString &note)> onExecute;
    // Verify (#T71W): hand the card to a terminal pane on the *recommended verifier*, which is a
    // different provider family from the one that implemented it. `note` is the reply box again.
    std::function<void(const QString &note)> onVerify;
    // A `relay-pane:` anchor in the thread names a pane by session token (#HKAP). The card's
    // own claim chip is the same anchor through the same handler (#R9G7).
    std::function<void(const QString &token)> onFocusPane;
    // Whether the pane a card was claimed by is still open (#R9G7): read as the card is shown,
    // so the page picks up a closed pane at its next re-read. Unset means live.
    std::function<bool(const QString &token)> paneExists;
    // A label hashtag was clicked — a badge in the list, the meta's labels, or a `#tag` in the
    // card's own words or the thread (#3ZAP): copy `#tag` and say so.
    std::function<void(const QString &tag)> onCopyTag;
    // A `#ID` in the card's own words or the thread names another card on this board: zoom to
    // it, the way the cleanup panel's `card:` anchors do (#3ZAP).
    std::function<void(const QString &id)> onOpenCard;
    // Whether a word after a `#` names a card on this board (#3ZAP): the board decides whether
    // `#K7Q2` is a card reference or a four-character label — shape alone cannot.
    std::function<bool(const QString &id)> hasCard;
    std::function<void(const QString &mode)> onModeHint;   // a mode button was clicked, not keyed
    std::function<void()> onClose, onCancel, onToPrompt, onEscape;
    std::function<void(const QString &what, const QString &value)> onMove;
    // One click on the card page's priority flag (#DPJB): +1 for a left click, −1 for a right
    // one, exactly as the row's flag reports it. The view clamps and writes `board_priority`.
    std::function<void(int step)> onPriority;
    // A path relative to the workspace (the card file) or to the card (a link in its body).
    std::function<void(const QString &path)> onOpenPath;
    // A `board_update` patch and the hash the edit started from, so the worker can refuse a
    // write over a file that changed meanwhile. The GUI never writes the file itself.
    std::function<void(const QJsonObject &patch, const QString &baseHash)> onEdit;
    std::function<void()> onEditHint;        // the Edit button was clicked, not the key
    // Delete (#CYM9): the trash button or the Del key was confirmed; the view sends board_delete.
    std::function<void()> onDelete;
    std::function<void()> onDeleteHint;      // the Delete button was clicked, not the key

    QString cardId() const { return m_id; }
    // The flag the view just wrote (#DPJB), shown at once so the disc turns under the click
    // rather than waiting for the worker's `board_changed`.
    void setPriority(int value) { m_flag->setPriority(value); }
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
    // Whether this card is in a verify lane at all (#3XZV): `needs-verification`, the
    // implementer's checklist being checked, or one of the QA lanes after it. That, and a `qa`
    // block, is what puts the verify line and its button on screen.
    bool inVerifyLane() const
    {
        return m_statusValue == QStringLiteral("needs-verification")
               || m_statusValue.startsWith(QStringLiteral("needs-qa"));
    }
    // Whether there is something to open: the worker found a verifier that is not the implementer
    // and is actually on this machine.
    bool hasVerifier() const { return !board::verifyRunner(m_qa).isEmpty(); }

    // Delete (#CYM9): hand the card to the view, which asks the one confirm every path asks
    // (asking here too would double-confirm: button -> onDelete -> deleteCard -> a second
    // dialog nobody answered). Not while a turn runs on the card — the worker would only
    // refuse it — and not mid-edit: what is on screen would be deleted underneath the person
    // typing into it.
    void remove()
    {
        if (m_id.isEmpty() || m_editing || !onDelete)
            return;
        if (m_busy) {
            showError(QStringLiteral("The agent is still answering on #%1. Stop it, or wait for "
                                     "it, before deleting the card.").arg(m_id));
            return;
        }
        onDelete();
    }

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
        if (!inVerifyLane()) {
            showError(QStringLiteral("#%1 has not landed for verification yet. Move it to Needs "
                                     "verification when it lands.").arg(m_id));
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
            m_thinking.clear();
            m_thinkingDone = false;
            m_thinkingMs = 0;
            m_sealed.clear();
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
        // `m_id` is set, so the flag is this card's; the row read the same key (#VKFV/#DPJB).
        m_flag->setPriority(front.value(QStringLiteral("priority")).toInt());
        m_ref->setText(QStringLiteral("#") + m_id);
        m_title->setText(title);
        const int status = m_status->findData(card.value(QStringLiteral("status")).toString());
        if (status >= 0)
            m_status->setCurrentIndex(status);
        const int tab = m_tab->findData(card.value(QStringLiteral("tab")).toString());
        if (tab >= 0)
            m_tab->setCurrentIndex(tab);
        m_tab->setVisible(m_tab->count() > 1);
        const QString sessionToken = front.value(QStringLiteral("session")).toString().trimmed();
        const QString meta = metaText(front, card.value(QStringLiteral("tasks")).toArray(), m_path,
                                      sessionToken.isEmpty() || !paneExists
                                          || paneExists(sessionToken));
        m_meta->setText(meta);
        m_meta->setVisible(!meta.isEmpty());
        m_qa = card.value(QStringLiteral("qa")).toObject();
        showVerify();
        m_openFile->setEnabled(!m_path.isEmpty());
        m_body = board::bodyWithoutTitle(card.value(QStringLiteral("body")).toString(), title);

        const int shownBefore = int(m_entries.size());
        m_entries.clear();
        const QJsonArray thread = card.value(QStringLiteral("thread")).toArray();
        for (const QJsonValue &value : thread)
            m_entries << value.toObject();
        m_threadTotal = qMax(card.value(QStringLiteral("thread_total")).toInt(), int(m_entries.size()));
        // The shown thread shrank (an older entry rolled off the window the worker sends): the
        // sealed blocks' anchors have shifted with it, so they go rather than land in the wrong
        // place. A list that only grew keeps every one where it sealed.
        if (int(m_entries.size()) < shownBefore)
            m_sealed.clear();
        render(sameCard ? Scroll::Keep : Scroll::Top);
        m_loading = false;
    }

    void appendEntry(const QJsonObject &entry)
    {
        // A thread entry is landing: the reasoning that streamed before it is sealed in place
        // above it (#9K5H), so the entry — an answer, or a question the agent asks mid-turn —
        // reads after the thinking it came from, exactly as the terminal's transcript orders
        // them. The live block starts empty for whatever the model thinks next.
        sealThinking();
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

    // A reasoning delta of the running turn (protocol 19.4). It streams into the thread above
    // the answer it precedes; the render is coalesced on the same 40 ms timer the answer uses,
    // so a long stream of deltas cannot re-render the card per chunk.
    void appendThinking(const QString &text)
    {
        if (m_thinking.size() < 200000)      // the terminal's own cap (src/Pane.h)
            m_thinking += text;
        if (!m_render->isActive())
            m_render->start();
    }

    // The block ended (thinking_done): its header settles to "thought for N s" — the same words
    // the terminal's fold uses — and the tail stays on screen for the rest of the turn.
    void finishThinking(qint64 ms)
    {
        m_thinkingDone = true;
        m_thinkingMs = ms;
        if (!m_render->isActive())
            m_render->start();
    }

    // Move the live trace into the sealed list, anchored to the number of entries on screen
    // when it was sealed, so a re-read that rebuilds `m_entries` from the file keeps drawing it
    // at the same place in the thread.
    void sealThinking()
    {
        if (!m_thinking.trimmed().isEmpty() || m_thinkingDone)
            m_sealed << LiveThinking{m_thinking, m_thinkingDone, m_thinkingMs, int(m_entries.size())};
        m_thinking.clear();
        m_thinkingDone = false;
        m_thinkingMs = 0;
        while (m_sealed.size() > 12)
            m_sealed.removeFirst();   // the thread itself only shows the last entries
    }

    // While a Discuss or a Plan runs, the strip over the reply box names it and stops it, and the
    // buttons that would start another turn wait (#VZ69).
    //
    // `streamed` and `progress` are how a card that was already running gets its turn back when
    // you come back to it: turns run per card now (protocol 19.16), so the board can be showing
    // #A while #B is planning, and #B's answer so far and its current step are held by the view
    // rather than lost when the card was closed.
    void setBusy(bool busy, const QString &mode = QString(), const QString &streamed = QString(),
                 const QString &progress = QString(), const QString &thinking = QString(),
                 bool thinkingDone = false, qint64 thinkingMs = 0)
    {
        m_busy = busy;
        m_busyMode = busy ? (mode.isEmpty() ? QStringLiteral("discuss") : mode) : QString();
        if (busy) {
            m_streaming = streamed;
            m_thinking = thinking;
            m_thinkingDone = thinkingDone;
            m_thinkingMs = thinkingMs;
        }
        setProgress(busy ? progress : QString());
        setModeTips();
        if (!busy)
            m_streaming.clear();
        // The trace is *not* cleared when the turn ends: a settled block stays where it sealed
        // until the next turn on this card starts, like the terminal's settled fold. A turn
        // that ended with no answer (stopped, failed) leaves its trace readable in place.
        render(busy ? Scroll::Bottom : Scroll::Keep);
    }

    // One line of what the turn is doing now. Elided rather than wrapped: the strip is one row
    // over the reply box, and a tool call's own line ("read 4 cards · 120 lines") is already short.
    void setProgress(const QString &line)
    {
        m_progress = line;
        if (m_busyWhat == nullptr)
            return;
        const int room = qMax(60, m_busyWhat->width());
        m_busyWhat->setText(QFontMetrics(m_busyWhat->font()).elidedText(line, Qt::ElideRight, room));
        m_busyWhat->setToolTip(line);
        m_busyWhat->setVisible(m_busy && !line.isEmpty());
    }

    // The answer so far, kept by the view while another card is on screen.
    QString streaming() const { return m_streaming; }
    QString progress() const { return m_progress; }

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
    // `selectIssue` is the card that was just created from the quick-add field: its `## Issue` is
    // the one line that was typed there, and that line is the *title*, not the issue (owner,
    // #VZ69). Offering it selected says so without throwing it away — the first keystroke
    // replaces it, and Ctrl+Enter or Esc leaves the card exactly as the field made it.
    void beginEdit(bool titleFirst, bool selectIssue = false)
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
            if (selectIssue)
                m_issueEdit->selectAll();
            else
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
                if (key->key() == Qt::Key_Delete) {   // #CYM9
                    remove();
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

    // What the reply row can do right now, and — while a turn is running — the strip over the
    // box that names the turn and stops it. Nothing on the row changes its label any more: a
    // button that turns into "Stop" was the thing the owner could not read (#VZ69).
    // The verb is the board's own, "Switchboarding", with a spaced dot before the status
    // (owner, 2026-09-19: 'it says "Switchboarding · [status]..."'), after the pane's
    // "Relaying · …" line.
    void setModeTips()
    {
        m_plan->setEnabled(!m_busy);
        m_plan->setToolTip(QStringLiteral("The agent reads the code and writes the card's plan; it "
                                          "changes no code and no other card. Anything typed goes "
                                          "with it (p, or Ctrl+Enter)"));
        m_delete->setEnabled(!m_busy);
        m_execute->setEnabled(!m_busy);
        m_execute->setToolTip(QStringLiteral("Hand the card to a new terminal pane beside the board: "
                                             "its agent builds it, and the card moves to In progress (x)"));
        m_verify->setEnabled(!m_busy && hasVerifier());
        const bool planning = m_busyMode == QStringLiteral("plan");
        m_busyLabel->setText(planning ? QStringLiteral("✦ Switchboarding · planning…")
                                      : QStringLiteral("✦ Switchboarding · discussing…"));
        m_stop->setText(planning ? QStringLiteral("✕ Stop planning")
                                 : QStringLiteral("✕ Stop discussing"));
        m_stop->setToolTip(planning
                               ? QStringLiteral("Stop the agent before it finishes the plan. "
                                                "Anything it has already written to the card stays.")
                               : QStringLiteral("Stop the agent's reply. Anything it has already "
                                                "written to the card stays."));
        m_busyStrip->setVisible(m_busy);
        if (m_busyWhat != nullptr)
            m_busyWhat->setVisible(m_busy && !m_progress.isEmpty());
        fitButtons();
    }

    // A reply-row label without its key: "Discuss (Enter)" -> "Discuss". The key does not vanish —
    // it is in the button's tooltip and in the pane's key legend either way.
    static QString labelWithoutKey(const QString &label)
    {
        const int at = label.lastIndexOf(QStringLiteral(" ("));
        return at > 0 && label.endsWith(QLatin1Char(')')) ? label.left(at) : label;
    }

    // The reply row carries each key in its label (#QG60: "Execute (x)"). Three of those do not
    // fit a narrow card — CardDetail's own minimum width lets the layout squeeze a button below its
    // size hint, and the label then paints cut off at both ends — so when the row is tight the keys
    // drop out of the labels first, the way a card row drops its decorative badges before its
    // meaning (board::fitBadges). Only while the card is actually on screen: a width nothing has
    // laid out yet says nothing about what fits.
    //
    // The model box shares the row (#BRD3), and it is the one thing on it that can give width
    // back: it is QSizePolicy::Maximum, so the layout shrinks it towards its "MM" minimum before
    // it touches anything else, while a QPushButton can only be clipped. So the box's *natural*
    // width is what the keys are measured against — the keys go first, and only then does the box
    // start to squeeze — and what has to fit at the far end is three short labels beside a box at
    // its minimum, which is still true at the ~350 px pane where the list is hidden and the card
    // has the whole width.
    void fitButtons()
    {
        if (!isVisible() || !m_replyFrame || m_replyFrame->isHidden())
            return;
        const QList<QPushButton *> row{m_plan, m_execute, m_verify};
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
        if (m_modelBox != nullptr && !m_modelBox->isHidden()) {
            wide += m_modelBox->sizeHint().width();
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
        const QString line = inVerifyLane() ? board::verifyLine(m_qa) : QString();
        const QString note = inVerifyLane() ? board::verifyNote(m_qa) : QString();
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
        m_verify->setVisible(inVerifyLane());
        m_verify->setEnabled(!m_busy && hasVerifier());
        const QString label = board::verifyLabel(m_qa);
        m_verify->setToolTip(hasVerifier()
                                 ? QStringLiteral("Hand the card to a new terminal pane on %1, from "
                                                  "a different provider family than the one that "
                                                  "implemented it; it runs the checklist (v)")
                                       .arg(label)
                                 : QStringLiteral("No verifier is available for this card: %1")
                                       .arg(!note.isEmpty() ? note
                                            : line.isEmpty() ? QStringLiteral("the board has not said who should check it")
                                                             : line));
        fitButtons();
    }

    // Two short lines of facts under the pickers, keys muted, the file a link that opens it.
    static QString metaText(const QJsonObject &front, const QJsonArray &tasks, const QString &path,
                            bool sessionLive = true)
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
        // Labels read as the hashtags they are and copy on a click (#3ZAP): the muted key
        // stays the meta's, the words take the pane's link colour.
        QStringList labelWords;
        const QJsonValue labelsValue = front.value(QLatin1String("labels"));
        if (labelsValue.isArray())
            for (const QJsonValue &entry : labelsValue.toArray())
                labelWords << entry.toString();
        else if (labelsValue.isString())
            labelWords << labelsValue.toString();
        labelWords.removeAll(QString());
        if (!labelWords.isEmpty()) {
            QStringList shown;
            for (const QString &label : std::as_const(labelWords))
                shown << QStringLiteral("<a href=\"tag:%1\" style=\"color:%2\">#%3</a>")
                                 .arg(QString::fromUtf8(QUrl::toPercentEncoding(label)),
                                      theme::Link.name(), label.toHtmlEscaped());
            parts << QStringLiteral("<span style=\"color:%1\">labels</span>&nbsp;%2")
                         .arg(theme::TextMuted.name(), shown.join(QStringLiteral(", ")));
        }
        add("assignee", QStringLiteral("assignee"));
        // Which pane claimed the card (#R9G7), beside who it is assigned to: the same chip the
        // row wears, and — while that pane is open — the same `relay-pane:` anchor the thread's
        // "Executing (xxxxxxxx)" entry carries, in the link colour, so a click reveals the pane.
        // A pane that has gone keeps its token, muted, with "closed" and no link: the claim is
        // still the record of who took the card.
        const QString session = front.value(QStringLiteral("session")).toString().trimmed();
        if (!session.isEmpty()) {
            const QString chip = board::sessionChip(session, sessionLive).toHtmlEscaped();
            parts << QStringLiteral("<span style=\"color:%1\">session</span>&nbsp;%2")
                         .arg(theme::TextMuted.name(),
                              sessionLive
                                  ? QStringLiteral("<a href=\"relay-pane:%1\" style=\"color:%2\">%3</a>")
                                        .arg(QString::fromUtf8(QUrl::toPercentEncoding(session)),
                                             theme::Link.name(), chip)
                                  : QStringLiteral("<span style=\"color:%1\">%2</span>")
                                        .arg(theme::TextMuted.name(), chip));
        }
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
        const int from = cursor.position();
        cursor.insertFragment(QTextDocumentFragment(&part));
        linkifyTags(cursor.document(), from, cursor.position());
    }

    // A `#tag` (#3ZAP): a label hashtag copies when clicked; a `#ID` that names a card on this
    // board zooms to it instead. Card ids are the four base32 characters the board coins, so
    // shape alone cannot tell a reference from a four-letter label — `hasCard` decides, and a
    // word that names no card is a label. A `#` inside a word (`well#known`) is not a tag.
    static const QRegularExpression &tagPattern()
    {
        static const QRegularExpression pattern(
            QStringLiteral("(?<![A-Za-z0-9_-])#[A-Za-z0-9][A-Za-z0-9_-]*"));
        return pattern;
    }

    // The anchor a tag wears, over whatever format the words around it carry.
    QTextCharFormat tagFormat(const QTextCharFormat &base, const QString &word) const
    {
        const bool ref = hasCard && hasCard(word);
        QTextCharFormat link = base;
        link.setAnchor(true);
        link.setAnchorHref(ref ? QStringLiteral("card:") + word.toUpper()
                               : QStringLiteral("tag:") + word);
        link.setFontUnderline(true);
        link.setForeground(theme::Link);
        return link;
    }

    // `text` inserted with every `#tag` in it an anchor (#3ZAP), so a plain thread line — an
    // event, a note, a hand-off — carries the same affordance the Markdown bodies get.
    void insertTagged(QTextCursor &cursor, const QString &text, const QTextCharFormat &format)
    {
        int at = 0;
        QRegularExpressionMatchIterator it = tagPattern().globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const QString found = match.captured(0);
            if (match.capturedStart() > at)
                cursor.insertText(text.mid(at, match.capturedStart() - at), format);
            cursor.insertText(found, tagFormat(format, found.mid(1)));
            at = match.capturedEnd();
        }
        if (at < text.size())
            cursor.insertText(text.mid(at), format);
    }

    // Overlay `#tag` anchors on a stretch of already-rendered Markdown (#3ZAP): the body after
    // setMarkdown(), or the range a thread comment's fragment landed in. What is already an
    // anchor — a Markdown link's label, a pane link — and what is code, fenced or inline, keeps
    // its meaning: `[#bug](http://x)` stays an http link and `#include` stays source.
    void linkifyTags(QTextDocument *doc, int from, int to)
    {
        for (QTextBlock block = doc->findBlock(from);
             block.isValid() && block.position() <= to; block = block.next()) {
            const QTextBlockFormat blockFormat = block.blockFormat();
            if (blockFormat.property(QTextFormat::BlockCodeLanguage).isValid()
                || blockFormat.property(QTextFormat::BlockCodeFence).isValid())
                continue;
            for (QTextBlock::iterator piece = block.begin(); !piece.atEnd(); ++piece) {
                const QTextFragment fragment = piece.fragment();
                if (!fragment.isValid())
                    continue;
                const QTextCharFormat base = fragment.charFormat();
                if (!base.anchorHref().isEmpty())
                    continue;
                if (base.fontFamilies().toStringList().contains(QStringLiteral("monospace"),
                                                               Qt::CaseInsensitive))
                    continue;   // a code span, not prose
                const QString text = fragment.text();
                QRegularExpressionMatchIterator it = tagPattern().globalMatch(text);
                while (it.hasNext()) {
                    const QRegularExpressionMatch match = it.next();
                    const int start = fragment.position() + match.capturedStart();
                    const int end = fragment.position() + match.capturedEnd();
                    if (start < from || end > to)
                        continue;
                    QTextCursor apply(doc);
                    apply.setPosition(start);
                    apply.setPosition(end, QTextCursor::KeepAnchor);
                    apply.setCharFormat(tagFormat(base, match.captured(0).mid(1)));
                }
            }
        }
    }

    // A hairline the width of the document: an empty block two pixels tall, painted in the border
    // ink. QTextDocument has no rule of its own that a theme can colour — Markdown's `---` takes
    // the palette's — and a row of box-drawing characters would wrap and be copied with the text.
    void insertRule(QTextCursor &cursor, int topMargin)
    {
        QTextBlockFormat rule;
        rule.setTopMargin(topMargin);
        rule.setBottomMargin(0);
        rule.setLineHeight(2, QTextBlockFormat::FixedHeight);
        rule.setBackground(theme::Border);
        QTextCharFormat hairline;
        hairline.setFontPointSize(1);
        cursor.insertBlock(rule, hairline);
    }

    void insertLine(QTextCursor &cursor, const QString &text, const QTextCharFormat &format,
                    int topMargin)
    {
        QTextBlockFormat block;
        block.setTopMargin(topMargin);
        block.setBottomMargin(2);
        cursor.insertBlock(block, format);
        insertTagged(cursor, text, format);
    }

    // One reasoning block: the text, whether it ended, how long it ran, and — for a sealed
    // block — how many entries were on screen when it sealed, so a re-read that rebuilds the
    // entry list draws it at the same place in the thread.
    struct LiveThinking {
        QString text;
        bool done = false;
        qint64 ms = 0;
        int after = 0;
    };

    // One reasoning block in the thread (#9K5H): the terminal fold's header ("thinking…" while
    // it streams, "thought for N s" when it ends) over the tail of the trace, muted and italic
    // like the placeholder it replaces. The end is the part being written and the part a reader
    // of a question needs, so the tail is what shows and the cut is named, as the fold names
    // its cut; the whole block is never written to the card file.
    void insertThinking(QTextCursor &cursor, const LiveThinking &block, qreal base, int topMargin = 8)
    {
        QTextCharFormat head;
        head.setForeground(theme::TextMuted);
        head.setFontWeight(QFont::DemiBold);
        head.setFontPointSize(qMax(theme::FloorPt, base * 0.9));
        const QString label = !block.done
            ? QStringLiteral("✦ thinking…")
            : (block.ms > 0
                   ? QStringLiteral("✦ thought for %1 s").arg((block.ms + 500) / 1000)
                   : QStringLiteral("✦ thinking stopped"));
        insertLine(cursor, label, head, topMargin);
        const QString body = sanitizeTrace(block.text).trimmed();
        if (body.isEmpty())
            return;
        constexpr int kTail = 4000;
        QTextCharFormat trace;
        trace.setForeground(theme::TextMuted);
        trace.setFontItalic(true);
        trace.setFontPointSize(qMax(theme::FloorPt, base * 0.9));
        if (body.size() > kTail)
            insertLine(cursor, QStringLiteral("… %1 more characters above").arg(body.size() - kTail),
                       trace, 2);
        const QString shown = body.size() > kTail ? body.right(kTail) : body;
        const QStringList lines = shown.split(QLatin1Char('\n'));
        for (int i = 0; i < lines.size(); ++i) {
            // A blank line between paragraphs, an empty block otherwise: reasoning text is
            // prose and the thread's own markdown blocks have air around them.
            insertLine(cursor, lines.at(i), trace, i == 0 ? 2 : 1);
        }
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
        linkifyTags(doc, 0, doc->characterCount());   // the card's own words first (#3ZAP)

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
        // The card's own words end here and the conversation about them begins, and the reader has
        // to see the seam (owner, #VZ69: "there should be a clearer dematcation between the issue
        // and the convo thread"). A heading in bolder ink was not it: the thread gets a rule the
        // width of the document and then its own ground under the heading, so the two halves of
        // the card are two surfaces rather than one column of text with a louder line in it.
        insertRule(cursor, 22);
        QTextBlockFormat band;
        band.setTopMargin(0);
        band.setBottomMargin(10);
        band.setLeftMargin(8);
        band.setBackground(mix(theme::Surface, theme::Text, 0.07));
        cursor.insertBlock(band, heading);
        cursor.insertText(threadTitle, heading);
        if (m_entries.isEmpty() && !m_busy)
            insertLine(cursor, QStringLiteral("No replies yet. Enter in the box below discusses the "
                                              "card with the agent, Ctrl+Enter has it Plan the work, "
                                              "and Ctrl+Shift+Enter leaves a comment for whoever "
                                              "picks it up."), muted, 4);

        const QDateTime now = QDateTime::currentDateTimeUtc();
        // Entries already drawn: a sealed block anchors to the count it sealed at, so it draws
        // before the first entry that landed under it — including one sealed over an empty
        // thread (after 0), which draws at the top (#9K5H).
        int drawn = 0;
        const auto drawSealed = [this, &cursor, base, &drawn] {
            for (int at = 0; at < m_sealed.size(); ++at)
                if (m_sealed.at(at).after == drawn)
                    insertThinking(cursor, m_sealed.at(at), base);
        };
        for (const QJsonObject &entry : std::as_const(m_entries)) {
            drawSealed();
            ++drawn;
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
            // An entry that handed the card to a pane links to it (#HKAP): its whole first
            // line — "Executing (xxxxxxxx) · …" — is an anchor on that pane's session token,
            // in the palette's link colour so it reads as clickable before it is clicked.
            // Anything after the first line (the owner's note on the hand-off) is ordinary
            // body text; entries without a token keep the Markdown path.
            const QString paneToken = attrs.value(QStringLiteral("pane_token")).toString();
            if (paneToken.isEmpty()) {
                cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
                insertMarkdown(cursor, board::threadMarkdown(text, kind), base);
            } else {
                QTextCharFormat link;
                link.setForeground(theme::Link);
                link.setAnchor(true);
                link.setAnchorHref(QStringLiteral("relay-pane:") + paneToken);
                const int split = text.indexOf(QLatin1Char('\n'));
                insertLine(cursor, split < 0 ? text : text.left(split), link, 2);
                if (split >= 0)
                    insertMarkdown(cursor, board::threadMarkdown(text.mid(split).trimmed(), kind), base);
            }
        }
        // A block sealed after the last entry on screen (or with none at all) still draws: it
        // ran after everything the thread shows so far.
        drawSealed();
        const bool liveTrace = !m_thinking.trimmed().isEmpty() || m_thinkingDone;
        if (m_busy || !m_streaming.isEmpty() || liveTrace) {
            QTextCharFormat who;
            who.setFontWeight(QFont::DemiBold);
            who.setForeground(theme::Agent);
            insertLine(cursor, QStringLiteral("✦ agent"), who, 14);
            if (const QString mode = board::modeTitle(m_busyMode); m_busy && !mode.isEmpty())
                cursor.insertText(QStringLiteral("  ") + mode, muted);
            cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
            if (liveTrace)
                insertThinking(cursor, LiveThinking{m_thinking, m_thinkingDone, m_thinkingMs, 0}, base, 2);
            if (m_streaming.isEmpty()) {
                if (!liveTrace) {
                    // No reasoning came from this worker (or the display is off): the old
                    // placeholder keeps the turn's presence in the thread.
                    QTextCharFormat thinking = muted;
                    thinking.setFontItalic(true);
                    cursor.insertText(QStringLiteral("thinking…"), thinking);
                }
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

    PriorityFlagButton *m_flag = nullptr;   // the card page's priority flag (#DPJB)
    QLabel *m_ref = nullptr, *m_title = nullptr, *m_meta = nullptr, *m_error = nullptr;
    QLabel *m_verifyLine = nullptr;   // the cross-provider QA recommendation (#T71W)
    QComboBox *m_status = nullptr, *m_tab = nullptr;
    QToolButton *m_close = nullptr, *m_toPrompt = nullptr, *m_openFile = nullptr, *m_edit = nullptr;
    QToolButton *m_delete = nullptr;
    QTextBrowser *m_doc = nullptr;
    RichEditor *m_reply = nullptr;
    QPushButton *m_plan = nullptr, *m_execute = nullptr, *m_verify = nullptr;
    // The strip under the reply box: the stretch that right-aligns it, the model box, then the
    // three buttons (#BRD3). The box is the view's — this page holds it only to place it and to
    // count its width in fitButtons().
    QHBoxLayout *m_buttonRow = nullptr;
    QComboBox *m_modelBox = nullptr;
    // The strip over the reply box while a turn runs: "✦ Switchboarding · planning…" and the ✕ that
    // stops it (#VZ69). Hidden the rest of the time.
    QWidget *m_busyStrip = nullptr;
    QLabel *m_busyWhat = nullptr;
    QLabel *m_busyLabel = nullptr;
    QToolButton *m_stop = nullptr;
    QFrame *m_replyFrame = nullptr, *m_editFrame = nullptr;
    QLineEdit *m_titleEdit = nullptr;
    QPlainTextEdit *m_issueEdit = nullptr;
    QPushButton *m_saveEdit = nullptr, *m_cancelEdit = nullptr;
    QTimer *m_render = nullptr;
    QList<QJsonObject> m_entries;
    QHash<QString, QString> m_drafts;
    QString m_body, m_streaming, m_id, m_path;
    // The turn's reasoning, in the thread (#9K5H). `m_thinking` is the block streaming now;
    // `m_sealed` holds the blocks that ran before a thread entry landed under them, anchored to
    // the entry count at which they sealed so a re-read draws them in the same place.
    QString m_thinking;
    bool m_thinkingDone = false;
    qint64 m_thinkingMs = 0;
    QList<LiveThinking> m_sealed;
    // The card as the worker last handed it over: the hash an edit is written against, and the
    // `## Issue` text an edit starts from and is compared with.
    QString m_hash, m_issue;
    QJsonObject m_front;
    QJsonObject m_qa;                 // the worker's verifier recommendation for this card (#T71W)
    QString m_statusValue;
    QStringList m_sections;           // the body's `## ` headings, for "has it a plan?"
    QString m_busyMode;               // "discuss" or "plan" while a turn runs
    QString m_progress;               // what this card's turn is doing now (19.16)
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
    // Clicking a problem hands it to the page agent as a **draft** (owner, 2026-09-19: "if there
    // are problems, when you click on them, it sends a fix note to the switchboard agent" — and
    // "draft you confirm"). Nothing is sent: the request lands in the composer with the keyboard
    // in it, so the owner reads it before pressing Enter. Opening the file stays on `o`, which is
    // why the tooltip says so.
    connect(m_problems, &QLabel::linkActivated, this, [this](const QString &path) {
        if (m_chat && !m_problemFix.isEmpty()) {
            m_chat->prefill(m_problemFix);
            return;
        }
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
    // The list's column header, between the tools and the rows: the sort lives here now (owner,
    // 2026-09-19). It measures against the list, so it is wired once the list is up.
    m_columnHeader = new ColumnHeader(m_listPane);
    m_columnHeader->onSort = [this](board::SortColumn column) {
        setSortOrder(board::sortId(board::nextColumnSort(column, m_model.sort())));
    };
    listLayout->addWidget(m_columnHeader);
    m_list = new RowList(&m_rows, m_listPane);
    m_columnHeader->setList(m_list);
    auto *delegate = new RowDelegate(&m_model, &m_rows, m_list);
    delegate->adds = [this](const QString &columnId) { return sectionTakesNewCards(columnId); };
    delegate->turnMode = [this](const QString &cardId) {
        return m_cardTurns.value(cardId).mode;
    };
    // Is the pane that claimed this card still open (#R9G7)? Asked as each row is drawn, so a
    // pane closed while the board is up reads as closed at the list's next repaint; the window
    // answers, and a view with no window behind it (a test) calls every claim live.
    delegate->paneExists = [this](const QString &token) {
        return !paneExists || paneExists(token);
    };
    m_list->setItemDelegate(delegate);
    m_list->addRectOf = [delegate](int rowIndex, const QRect &itemRect) {
        return delegate->addRectOf(rowIndex, itemRect);
    };
    m_list->priorityRectOf = [delegate](int rowIndex, const QRect &itemRect) {
        return delegate->priorityRectOf(rowIndex, itemRect);
    };
    m_list->labelBadgeAt = [delegate](int rowIndex, const QRect &itemRect, const QPoint &at) {
        return delegate->labelBadgeAt(rowIndex, itemRect, at);
    };
    m_list->onCopyLabel = [this](const QString &label) { copyTag(label); };
    m_list->onPriority = [this](const QString &card, int step) { setCardPriority(card, step); };
    m_list->onToggleSection = [this](const QString &columnId) { toggleSection(columnId); };
    // A click on the self-closed fold row (#93WR) does what Enter on it does, and says so once:
    // the row is a keyboard row like any other.
    m_list->onToggleFold = [this](const QString &columnId) {
        if (onHint)
            onHint(QStringLiteral("board.selfClosedFold"), QStringLiteral("Enter"));
        toggleSelfClosed(columnId);
    };
    m_list->onAddInSection = [this](const QString &columnId) {
        if (onHint)
            onHint(QStringLiteral("board.quickAdd"), QStringLiteral("n"));
        quickAddIn(columnId);
    };
    m_list->triageRectOf = [delegate](int rowIndex, const QRect &itemRect) {
        return delegate->triageRectOf(rowIndex, itemRect);
    };
    // The ⚠ on a section header (#8YQ9): the board's own check, scoped to that section's cards.
    m_list->onTriageSection = [this](const QString &columnId) { requestCheck(columnId); };
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
    syncColumnHeader();
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

    // The gear's page. A sibling of the splitter rather than a window over it (owner: new
    // surfaces are panes, not floating strips), shown in its place and gone again on Save or
    // Cancel — the pane keeps its size and its place in the window either way.
    m_sections = new board::SectionEditor(this);
    m_sections->hide();
    m_sections->onClose = [this] { closeSections(); };
    m_sections->onSave = [this](const QJsonObject &message) {
        QJsonObject request = message;
        request.insert(QStringLiteral("type"), QStringLiteral("board_sections"));
        request.insert(QStringLiteral("reason"), QStringLiteral("edited in the Switchboard"));
        send(request);
        closeSections();
    };
    // "Hide this board's folder" / "Show this board's folder" (protocol 19.17, #916B): the one
    // action that renames an existing board's folder. The worker answers `board_folder_changed`
    // (handled in handleEvent) or a refusal, which the notice line shows like any other error.
    m_sections->onFolder = [this](bool hidden) {
        send({{QStringLiteral("type"), QStringLiteral("board_folder")}, {QStringLiteral("hidden"), hidden}});
        closeSections();
    };
    layout->addWidget(m_sections, 1);

    // The page agent's panel (#8YQ9), under everything the list page shows. **Not** inside the
    // splitter: `rebuild()` hides that whole widget when the board has no cards, and a board with
    // no cards is precisely the board the survey has something to say about — put there, the
    // survey could never be seen on the only board that gets one.
    buildChatPanel(layout);

    // The card page's model box (#BRD3), built once both pages exist: the list page's box has
    // just been reparented into the composer above, and the card page it goes on was made with
    // the splitter. Both are filled from the one state here, so either page can name the model
    // before the worker has said a word.
    buildCardModelBox();
    rebuildModelBox();

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
    m_detail->onFocusPane = [this](const QString &token) {
        if (onFocusPane && !token.isEmpty())
            onFocusPane(token);
    };
    m_detail->paneExists = [this](const QString &token) {
        return !paneExists || paneExists(token);
    };
    m_detail->hasCard = [this](const QString &id) { return m_model.card(id) != nullptr; };
    m_detail->onCopyTag = [this](const QString &tag) { copyTag(tag); };
    m_detail->onOpenCard = [this](const QString &id) { openCard(id); };
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
            m_cardTurns.insert(card, CardTurn{mode, text, QString(), QString(), QString(), false, 0});
            m_detail->setBusy(true, mode);
            syncModelBoxEnabled();
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
    // `cancel` is the worker's own turn — here, a cleanup. A card turn runs on that card's own
    // agent (protocol 19.16), so stopping it names the card, and stopping #A leaves #B planning.
    m_detail->onCancel = [this] {
        const QString card = m_detail->cardId();
        if (card.isEmpty())
            return;
        send({{QStringLiteral("type"), QStringLiteral("board_cancel")},
              {QStringLiteral("card"), card}});
    };
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
    };
    m_detail->onMove = [this](const QString &what, const QString &value) {
        const QString card = m_detail->cardId();
        if (card.isEmpty() || value.isEmpty())
            return;
        const QString id = nextRequestId();
        m_pendingNotes.insert(id, what == QStringLiteral("tab")
                                      ? QStringLiteral("Moved #%1 to %2").arg(card, board::tabTitle(value))
                                      : QStringLiteral("Moved #%1 to %2").arg(card, board::statusTitle(value)));
        // A status pick is a move to a lane: it takes the card out of the manual section it may
        // have been parked in (#3XZV). A tab pick leaves the parking alone.
        QJsonObject move{{QStringLiteral("type"), QStringLiteral("board_move")},
                         {QStringLiteral("id"), id},
                         {QStringLiteral("card"), card},
                         {what, value},
                         {QStringLiteral("reason"), QStringLiteral("changed in the Switchboard")}};
        if (what == QStringLiteral("status"))
            move.insert(QStringLiteral("section"), QString());
        send(move);
    };
    // The card page's flag click (#DPJB) goes through the row's own function: one clamp, one
    // `board_priority`, one notice, one undo record for both places the flag can be clicked.
    m_detail->onPriority = [this](int step) { setCardPriority(m_detail->cardId(), step); };
    m_detail->onEdit = [this](const QJsonObject &patch, const QString &baseHash) {
        saveCardEdit(patch, baseHash);
    };
    m_detail->onDelete = [this] { deleteCard(m_detail->cardId()); };
    m_detail->onDeleteHint = [this] {
        if (onHint)
            onHint(QStringLiteral("board.delete"), QStringLiteral("Del"));
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
    m_filter->setPlaceholderText(QStringLiteral("Filter  —  any word in the card, label:bug, "
                                                "status:done, folder:changes, @agent, waiting:me"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setMinimumWidth(60);      // it gives way to the buttons rather than pushing them out
    m_listTools->addWidget(m_filter, 1);
    // There is no sort control in this row any more (owner, 2026-09-19: "change switchboard
    // sorting from a sort button to adding header columns that you click on"): the order is the
    // list's own column header, built with the list under these tools.
    m_add = new QToolButton(tools);
    m_add->setObjectName(QStringLiteral("boardAddButton"));
    m_add->setText(QStringLiteral("+  New card (n)"));
    m_add->setToolTip(QStringLiteral("New card in Inbox (n)"));
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
    // The Switchboard agent's model (#BRD3): the worker's own model box. It is built into this
    // row so it exists from the first draw, then buildChatPanel moves it into the page agent's
    // composer row (#8YQ9 t:6m, composer parity with the main panes); collapsed it is only as
    // wide as the model it names.
    m_modelBox = new CurrentTextComboBox(tools);
    m_modelBox->setObjectName(QStringLiteral("statusPicker"));
    m_modelBox->setAccessibleName(QStringLiteral("Switchboard agent model"));
    m_modelBox->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_modelBox->setFocusPolicy(Qt::TabFocus);
    m_listTools->addWidget(m_modelBox);
    connect(m_modelBox, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        pickModel(m_modelBox->itemData(index).toString());
    });
    // Not filled here: the card page's twin is built once the card page exists, and the two are
    // filled together (buildCardModelBox, called from the constructor below).
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

    // The label chips under them (#VKFV, owner: clean up annotates bug/feature/area labels and
    // "those should become a second set of filter next to the section list"): one chip per
    // label the board carries, wrapping the same way. Lower case, where the section boxes are
    // engraved upper case, so the two rows never read as one.
    m_labelChecks = new QWidget(tools);
    m_labelChecks->setObjectName(QStringLiteral("boardLabelChecks"));
    QSizePolicy labelPolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    labelPolicy.setHeightForWidth(true);
    m_labelChecks->setSizePolicy(labelPolicy);
    auto *labelFlow = new FlowLayout(m_labelChecks, 10, 3);
    labelFlow->setContentsMargins(0, 0, 0, 0);
    m_labelChecksLayout = labelFlow;
    toolsLayout->addWidget(m_labelChecks);

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
            openCard((url.path().isEmpty() ? url.host() : url.path()).toUpper());
            return;
        }
        if (onOpenFile && !url.path().isEmpty())
            onOpenFile(QDir(m_workspace).absoluteFilePath(url.path()));
    });
}

// --------------------------------------------------------- the page agent (#8YQ9, protocol 19.18)

// The conversation about the whole board, pinned under the list on the list page. It is a panel
// in the page, not a window over it and not a second pane (owner's standing rule: a new surface
// is a pane or in-pane, never a floating strip), and it is built only once — an open card hides
// the whole list page, panel and all, so a card is never looking at the board's conversation.
//
// Clean up comes down here with it (owner, 2026-09-19: "put the clean up button down there, or
// see if there should be other buttons"), and the other button that belongs beside an agent that
// reorganizes the board is **Check**: the board's own format check over every card, deterministic
// and free, whose findings pre-fill this composer with a fix request. The button keeps every bit
// of its cleanup machinery — BoardView still owns it, its text, its run and its Stop — so moving
// it is a reparent and nothing else.
void BoardView::buildChatPanel(QVBoxLayout *layout)
{
    m_chat = new BoardChatPanel(m_listPane);
    m_chat->onSend = [this](const QJsonObject &message) { send(message); };
    m_chat->nextRequestId = [this] { return nextRequestId(); };
    m_chat->onHint = [this](const QString &id, const QString &keys) {
        if (onHint)
            onHint(id, keys);
    };
    m_chat->onStatus = [this](const QString &text) {
        if (onStatus)
            onStatus(text);
    };
    m_chat->onOpenFile = [this](const QString &path) {
        if (onOpenFile && !path.isEmpty())
            onOpenFile(QDir(m_workspace).absoluteFilePath(path));
    };
    m_chat->onOpenCard = [this](const QString &id) {
        selectCard(id);
        m_selected = id;
        openSelected();
    };
    m_chat->onOpenOption = [this](const QString &section, const QString &row) {
        if (onOpenOption)
            onOpenOption(section, row);
    };
    m_chat->onOpenSession = [this](const QString &id) {
        if (onOpenSession)
            onOpenSession(id);
    };
    m_chat->setWorkspace(m_workspace);

    // Clean up leaves the filter row for the panel's button row, beside the Check the panel
    // builds for itself. Taken out of its old layout by hand rather than left for addWidget to
    // move it, which does that with a warning on the console.
    if (m_cleanup) {
        if (m_listTools)
            m_listTools->removeWidget(m_cleanup);
        if (m_toolsWrap)
            m_toolsWrap->removeWidget(m_cleanup);
        m_chat->addToolWidget(m_cleanup);
    }
    layout->addWidget(m_chat);
    // The model box (#BRD3) belongs in this composer row — composer parity with the main panes
    // (#8YQ9 t:6m) — left of the microphone, so Send stays last. Built with the list tools
    // above (the box must exist even before this panel is), and reparented here once it is.
    if (m_modelBox)
        m_chat->addComposerWidget(m_modelBox);
}

void BoardView::focusChat()
{
    if (m_chat)
        m_chat->focusComposer();
}

bool BoardView::chatRunning() const
{
    return m_chat && m_chat->running();
}

// `board_check`, scoped to a section (a header's warning mark) or to the whole board. The panel's
// own Check button sends the unscoped one itself; it is the same message, and both answers come
// back carrying this pane's request prefix, which is how `board_problems` below tells a check
// somebody asked for from the board's own refresh.
void BoardView::requestCheck(const QString &columnId)
{
    QJsonObject message{{QStringLiteral("type"), QStringLiteral("board_check")}};
    if (!columnId.isEmpty())
        message.insert(QStringLiteral("section"), columnId);
    send(message);
}

// 19.18's events, kept away from any card thread exactly as a cleanup's are: they carry
// `chat: true` and a `turn_id` and never a `card_id`.
bool BoardView::handleChatEvent(const QString &type, const QJsonObject &event)
{
    return m_chat && m_chat->handleEvent(type, event);
}

// The label chips follow the labels the board carries right now — every label on a card, plus
// any the board's own config names — rather than a hard-coded list. Rebuilt only when that set
// changes, so ticking one does not delete the chip under the pointer.
void BoardView::syncLabelChecks()
{
    QStringList labels = m_model.allLabels();
    for (const QJsonValue &value : m_config.value(QStringLiteral("labels")).toArray())
        if (!value.toString().isEmpty() && !labels.contains(value.toString()))
            labels << value.toString();
    labels.sort();
    if (labels == m_labelIds)
        return;
    m_labelIds = labels;
    // A label that went away while its chip was ticked simply stops filtering.
    m_labelPicked.intersect(QSet<QString>(labels.begin(), labels.end()));
    m_model.setLabelFilter(m_labelPicked);
    while (QLayoutItem *item = m_labelChecksLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const QString &label : labels) {
        auto *box = new QCheckBox(label, m_labelChecks);
        box->setObjectName(QStringLiteral("boardLabelCheck"));
        box->setChecked(m_labelPicked.contains(label));
        box->setCursor(Qt::PointingHandCursor);
        box->setFocusPolicy(Qt::NoFocus);
        box->setToolTip(QStringLiteral("Only cards labelled %1 — tick more to narrow, untick to "
                                       "broaden. Composes with the filter box and the section "
                                       "checkboxes.").arg(label));
        connect(box, &QCheckBox::toggled, this, [this, label](bool on) {
            if (on)
                m_labelPicked.insert(label);
            else
                m_labelPicked.remove(label);
            m_model.setLabelFilter(m_labelPicked);
            rebuild();
            // The selection may have been in what just went away: stand on the first card left.
            if (board::rowOfCard(m_rows, m_selected) < 0) {
                const int first = board::stepRow(m_rows, -1, 1);
                m_selected = first >= 0 ? m_rows.at(first).cardId : QString();
            }
        });
        m_labelChecksLayout->addWidget(box);
    }
    m_labelChecks->setVisible(!labels.isEmpty());
}

// The pane's hover buttons take their room out of whichever row is on top for good, and in a
// ~350 px pane that leaves the filter box nothing. Rather than let two QToolButtons elide to a
// pair of identical "…", they drop to a line of their own under the filter.
void BoardView::layoutListTools()
{
    if (!m_toolsWrap || !m_add)
        return;
    const int inset = m_head && m_head->isHidden() ? m_rightInset : 0;
    const int room = m_listPane->width() - 16 - inset;
    // The filter is owed a legible width before the button may sit beside it. Only "+ New card"
    // is in this row now — Clean up went down to the page agent's button row and the model box
    // into its composer row (#8YQ9) — so only its width is counted. Reserving theirs as well
    // wrapped the row a good 150 px earlier than it had to.
    const int need = m_count->sizeHint().width() + 150 + m_add->sizeHint().width() + 24;
    const bool wrap = room < need;
    if (wrap == m_toolsWrapped)
        return;
    m_toolsWrapped = wrap;
    QHBoxLayout *from = wrap ? m_listTools : m_toolsWrap;
    QHBoxLayout *to = wrap ? m_toolsWrap : m_listTools;
    from->removeWidget(m_add);
    to->addWidget(m_add);
    m_toolsWrapRow->setVisible(wrap);
}

// ------------------------------------------------------------------------- the model box (#BRD3)

// The rows, the current one and the tooltip are relay::helpermodel's (src/HelperModelBox.h): the
// Switchboard agent is the *tab's* helper now, and Options, Actions and Sessions carry a box over
// the same role and the same worker (#FEJQ §30.7). What is left here is this box — built with the
// filter row so it exists before the panel it ends up in does — and what a pick means to the view.

// The card page's own box (owner, 2026-09-20: "did we lose the model picker in the switchboard
// agent … it's the one in the cards, it's different and doesn't have the model picker"). An open
// card hides the whole list page — tools, composer and the box in it — so from a card there was no
// way to read or change the model, although Discuss and Plan are turns of that very agent.
//
// It is the same widget kind under the same object name as its twin, because it is the same
// control: the theme styles `QComboBox#statusPicker` and nothing else, and a box that looked
// different would read as a different setting. What tells them apart is the page they are on.
void BoardView::buildCardModelBox()
{
    if (!m_detail || m_cardModelBox)
        return;
    m_cardModelBox = new CurrentTextComboBox(m_detail);
    m_cardModelBox->setObjectName(QStringLiteral("statusPicker"));
    m_cardModelBox->setAccessibleName(QStringLiteral("Switchboard agent model"));
    // Maximum, so the reply strip takes its width back from the box rather than from the three
    // buttons when the card is narrow (CardDetail::fitButtons); TabFocus, so Tab reaches it from
    // the reply box and the buttons stay off the tab ring.
    m_cardModelBox->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_cardModelBox->setFocusPolicy(Qt::TabFocus);
    connect(m_cardModelBox, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        pickModel(m_cardModelBox->itemData(index).toString());
    });
    m_detail->setModelBox(m_cardModelBox);
}

// One pick, whichever box it came from. The gear is not a choice: put both boxes back on the live
// row at once. A real pick stays showing until the reconfigure's `configured` event redraws them
// on the new role.
void BoardView::pickModel(const QString &data)
{
    if (data == QStringLiteral("gear"))
        rebuildModelBox();
    if (onModelPick)
        onModelPick(data);
}

void BoardView::rebuildModelBox()
{
    // One state, filled into every box over it: the list page's and the card page's must agree
    // about which model the helper is on, the way the four panels' boxes do (#FEJQ).
    for (CurrentTextComboBox *box : {m_modelBox, m_cardModelBox}) {
        if (!box)
            continue;
        const QSignalBlocker block(box);
        m_modelTip = helpermodel::fill(box, m_modelBoxState);
        box->updateGeometry();   // the collapsed box's width follows the new current row
    }
    if (m_detail)
        m_detail->refitButtons();   // a longer model name takes the keys out of the card's labels
    syncModelBoxEnabled();
}

// A pick reconfigures the worker, and the worker refuses a configure mid-turn — so while any
// card's Discuss or Plan, or a cleanup, is running, the box waits rather than errors. Both boxes:
// the turn that blocks the configure is as likely to have been started from the card page.
void BoardView::syncModelBoxEnabled()
{
    const bool busy = !m_cardTurns.isEmpty() || cleanupRunning();
    for (CurrentTextComboBox *box : {m_modelBox, m_cardModelBox}) {
        if (!box)
            continue;
        box->setEnabled(!busy);
        box->setToolTip(m_modelTip + (busy ? helpermodel::busyNote() : QString()));
    }
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
    // The names as well as the ids: renaming a section in the gear leaves the ids exactly as they
    // were, and comparing those alone left a box reading "Ready to start" under a header that
    // already said "Up next".
    if (ids == m_checkIds && titles == m_checkTitles)
        return;
    m_checkIds = ids;
    m_checkTitles = titles;
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
    // After the sections themselves, because it is about the list of them rather than about any
    // one (owner, 2026-09-19: "put a gear after the list of switchboard sections").
    auto *gear = new QToolButton(m_checks);
    gear->setObjectName(QStringLiteral("boardSectionGear"));
    gear->setText(QStringLiteral("⚙"));
    gear->setAutoRaise(true);
    gear->setCursor(Qt::PointingHandCursor);
    gear->setFocusPolicy(Qt::NoFocus);
    gear->setToolTip(QStringLiteral("Add, remove, merge or rename the sections. They are a view of "
                                    "the statuses: no card moves and none of them changes."));
    connect(gear, &QToolButton::clicked, this, [this] { openSections(); });
    m_checksLayout->addWidget(gear);
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
    field->setToolTip(QStringLiteral("The card's title, kept verbatim. Enter creates the card and "
                                     "opens it with its issue ready to type"));
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
        QJsonObject create{{QStringLiteral("type"), QStringLiteral("board_create")},
                           {QStringLiteral("tab"), defaultCategory()},
                           {QStringLiteral("status"),
                            status.isEmpty() ? QStringLiteral("inbox") : status},
                           {QStringLiteral("card_type"), QStringLiteral("work")},
                           {QStringLiteral("text"), text}};
        if (status.isEmpty())
            create.insert(QStringLiteral("section"), m_quickAddColumn);   // a manual section
        send(create);
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

// One line for the strip over the reply box: "reading Pane.h", "read 4 cards · 120 lines",
// "Requesting model · step 4/256". The same `toollabel` the cleanup notice and the terminal
// pane's tool lines use, so a card turn is described in the words the rest of Relay uses.
static QString turnProgressLine(const QString &type, const QJsonObject &event)
{
    if (type == QStringLiteral("status"))
        return event.value(QStringLiteral("text")).toString();
    const toollabel::Label label = toollabel::fromEvent(event);
    const QString line = type == QStringLiteral("tool_started")
                             ? (label.runningLine().isEmpty() ? label.line() : label.runningLine())
                             : label.line();
    return line.isEmpty() ? event.value(QStringLiteral("tool")).toString() : line;
}

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
    // The board's folder was renamed at this pane's request (19.17): the event names the *new*
    // root, so it comes before the guard below, which would otherwise read it as another
    // project's. From here the worker speaks for the new folder; the watcher and the rows follow.
    if (type == QStringLiteral("board_folder_changed") && mine) {
        if (!root.isEmpty())
            m_root = root;
        showNotice(event.value(QStringLiteral("summary")).toString(), false);
        watchIssues();
        reload();
        return;
    }
    if (!root.isEmpty()) {
        if (!m_root.isEmpty() && root != m_root)
            return;
        if (m_root.isEmpty() && type == QStringLiteral("board"))
            m_root = root;
    }
    // The model box's events (#BRD3). They name no board and carry no `root` — they are about
    // the worker, not the cards — so they pass the guard above.
    // `configured`, `presets` and `model_roles` — the last sent after a configure in which a role
    // fell back (protocol 13), with the warning the box's tooltip carries. The holder takes all
    // three, so the board and the helper panels read them the same way (#FEJQ).
    if (m_modelBoxState.take(type, event)) {
        // Asked on every configure, not once: a stored or removed key reconfigures the workers
        // (RelayWindow::reconfigureBoardWorkers), and the answer is how the box hears of it.
        if (type == QStringLiteral("configured"))
            send({{QStringLiteral("type"), QStringLiteral("presets")}});
        if (m_chat && type == QStringLiteral("presets"))
            m_chat->setPresets(m_modelBoxState.presets);   // the microphone reads these (#8YQ9)
        rebuildModelBox();
        return;
    }
    if (type == QStringLiteral("board")) {
        m_open = true;
        // The page agent's conversation rides on this event (19.18), so a pane opened while the
        // agent is half way through an answer draws the panel — history, queue and all — from it.
        if (m_chat) {
            m_chat->setWorkspace(event.value(QStringLiteral("workspace")).toString().isEmpty()
                                     ? m_workspace
                                     : event.value(QStringLiteral("workspace")).toString());
            m_chat->setChatState(event.value(QStringLiteral("chat")).toObject());
        }
        m_config = event.value(QStringLiteral("config")).toObject();
        m_model.setConfig(m_config);
        m_model.reset(event.value(QStringLiteral("cards")).toArray());
        m_pendingDeletes.clear();
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
        // The config travels with every change, not only with the full `board` event: the gear
        // rewrites the section list without touching a card, so the sections would otherwise not
        // move until the pane was reopened. Before the early return below for the same reason.
        if (event.contains(QStringLiteral("config"))) {
            const QJsonObject config = event.value(QStringLiteral("config")).toObject();
            if (config != m_config) {
                m_config = config;
                m_model.setConfig(config);
                if (m_sectionsOpen)
                    m_sections->refresh(m_model);
                rebuild();
            }
        }
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
        if (!open.isEmpty() && detailOpen() && removed.contains(open)) {
            closeDetail();
            // This pane's own delete already put up its "Deleted #ID · Undo" toast (#CYM9); the
            // removal events that follow it must not talk over that. Someone else's delete —
            // another pane, an agent, a collaborator's pull — still says what happened.
            if (!deletedHere(open))
                showNotice(QStringLiteral("#%1 was removed from the board.").arg(open), true);
        }
        if (!open.isEmpty() && detailOpen()) {
            for (const QJsonValue &value : upserts)
                if (value.toObject().value(QStringLiteral("id")).toString() == open) {
                    send({{QStringLiteral("type"), QStringLiteral("board_card_get")},
                          {QStringLiteral("card"), open}});
                    break;
                }
        }
        // A card this pane deleted coming back (Undo, or a re-create) ends its marker: the next
        // removal of that id is news again, not the echo of our own write.
        for (const QJsonValue &value : upserts) {
            const QString back = value.toObject().value(QStringLiteral("id")).toString();
            if (!back.isEmpty())
                forgetDeleted(back);
        }
        return;
    }
    // The answer to one pane's own `board_card_get`: the worker echoes the requester's id, but the
    // window fans every event out to every board pane of the workspace, so without this check a
    // card opened in one tab opens in them all (#TTYB). Only the card *details* are scoped — the
    // broadcasts above (`board`, `board_changed`) still reach every pane, so the rows move together.
    if (type == QStringLiteral("board_card") && mine) {
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
        // Turns run per card (19.16), so the card you open may already be working: give it back
        // its strip, the answer so far and the step it is on. A card with no turn is idle, even
        // if the one you came from is still planning.
        if (const QString id = event.value(QStringLiteral("card_id")).toString();
            m_cardTurns.contains(id)) {
            const CardTurn &turn = m_cardTurns.value(id);
            m_detail->setBusy(true, turn.mode, turn.streamed, turn.progress, turn.thinking,
                              turn.thinkingDone, turn.thinkingMs);
        } else {
            m_detail->setBusy(false);
        }
        const bool wasOpen = detailOpen();
        m_detail->setVisible(true);
        if (!wasOpen)
            m_detailSized = false;
        updateDetailLayout();
        if (m_editOnOpen) {
            m_editOnOpen = false;
            const bool fresh = m_editOnOpenFresh;
            m_editOnOpenFresh = false;
            m_detail->beginEdit(false, fresh);
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
        const QJsonArray items = event.value(QStringLiteral("items")).toArray();
        // A check this pane asked for — the panel's Check button, or a section header's warning
        // mark — answers in the panel as a clickable list (#8YQ9). Every other `board_problems`
        // is the board's own refresh and belongs in the banner over the list, where it has always
        // been. A scoped check is never allowed to rewrite that banner: it leaves out the
        // problems that belong to no one section (the worker's rule), so it would under-report
        // the board.
        const QString section = event.value(QStringLiteral("section")).toString();
        if (mine) {
            if (m_chat)
                m_chat->showFindings(items, section);
            if (section.isEmpty())
                showProblems(items);
            return;
        }
        showProblems(items);
        return;
    }
    if (type == QStringLiteral("board_written") && mine) {
        const QString writeId = event.value(QStringLiteral("write_id")).toString();
        const QString kind = event.value(QStringLiteral("kind")).toString();
        const QString card = event.value(QStringLiteral("card_id")).toString();
        QString note = m_pendingNotes.take(requestId);
        // The delete has landed (#CYM9): from here the "Deleted #ID · Undo" toast owns the
        // card's removal events, until the card itself comes back or the pane reloads.
        m_pendingDeletes.remove(requestId);
        if (kind == QStringLiteral("board_create")) {
            note = QStringLiteral("Created #%1").arg(card);
            // The quick-add field took the title; the card itself takes the issue (owner, #VZ69:
            // "when you first press enter to add a new card, it should open the edit box, the
            // editable issue part … the first thing you enter in the top row thing makes the
            // title, not the issue content"). So the new card is the selection, it opens, and it
            // opens *editing*, with the cursor in the issue box — which is the same path `e`
            // takes, and it waits for the card to arrive the same way.
            m_selected = card;
            closeQuickAdd();
            m_editOnOpenFresh = true;
            editSelected();
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
    // The page agent's events (19.18) before everything else: they are tagged `chat: true` with a
    // turn id and no card id, and an open card's thread must never see one of them — the same
    // rule, and the same shape, as a cleanup's below.
    if (handleChatEvent(type, event))
        return;
    // A cleanup's events, and whole: they are tagged `cleanup: true` with a run id and no
    // card id (19.9), and an open card's thread must never see one of them.
    if (handleCleanupEvent(type, event))
        return;
    // Cards run in parallel with each other but not with a cleanup, and never two turns on one
    // card (19.16): say which of those it was, and put back whatever this pane had started.
    if (type == QStringLiteral("error")
        && event.value(QStringLiteral("code")).toString() == QStringLiteral("board_busy")) {
        const bool cleanupRuns = event.value(QStringLiteral("cleanup_running")).toBool();
        const QString busyCard = event.value(QStringLiteral("card_id")).toString();
        QStringList running;
        for (const QJsonValue &value : event.value(QStringLiteral("cards")).toArray())
            running << QStringLiteral("#") + value.toString();
        const QString what = cleanupRuns
            ? QStringLiteral("A cleanup is running on this board.")
            : (running.size() > 1
                   ? QStringLiteral("The agent is already working on %1.")
                         .arg(running.join(QStringLiteral(", ")))
                   : (busyCard.isEmpty()
                          ? QStringLiteral("The Switchboard agent is busy.")
                          : QStringLiteral("The agent is answering on #%1.").arg(busyCard)));
        if (!m_cleanupRequest.isEmpty() && requestId == m_cleanupRequest) {
            endCleanup();
            m_notice->hide();
            showNotice(what + QStringLiteral(" The cleanup did not start — nothing was written. "
                                             "Try again when it has finished."), true);
            return;
        }
        // The refused ask is this pane's own: the card it was typed on is the open one.
        const QString asked = m_detail->cardId();
        if (m_cardTurns.contains(asked)) {
            const QString unsent = m_cardTurns.take(asked).unsent;
            m_detail->setBusy(false);
            syncModelBoxEnabled();
            // The worker checks before it writes, so the question never reached the thread.
            if (cleanupRuns)
                m_busyCard = asked;
            m_detail->restoreReply(unsent);
            m_detail->showError(what + QStringLiteral(" Your message was not sent and is not in "
                                                      "the thread — it is back in the reply box. "
                                                      "Try again when that one has finished, or "
                                                      "stop it on its own card."));
            QTimer::singleShot(0, this, [this] { placeNotice(); });
            return;
        }
        showNotice(what, true);
        return;
    }
    // A board_ask turn (19.16): every event carries the card it belongs to, and the board may be
    // showing another one. The open card's view is updated; every card's turn is followed here,
    // so coming back to it finds the answer so far and what it is doing now.
    const QString card = event.value(QStringLiteral("card_id")).toString();
    if (!card.isEmpty() && m_cardTurns.contains(card)) {
        const bool here = card == m_detail->cardId();
        CardTurn &turn = m_cardTurns[card];
        // The reasoning trace of the running turn (19.4's thinking events, which the worker
        // tags with the card): streamed into the card's thread above the answer it precedes
        // and sealed in place when a thread entry lands under it, so a question the agent asks
        // mid-plan reads after the thinking it came from (#9K5H). "same as in the terminals"
        // (owner, 2026-09-19) — the same words the terminal fold uses, in the thread's flow.
        if (type == QStringLiteral("thinking_delta")) {
            const QString text = event.value(QStringLiteral("text")).toString();
            if (turn.thinking.size() < 200000)
                turn.thinking += text;
            if (here)
                m_detail->appendThinking(text);
            return;
        }
        if (type == QStringLiteral("thinking_done")) {
            turn.thinkingDone = true;
            turn.thinkingMs = event.value(QStringLiteral("elapsed_ms")).toVariant().toLongLong();
            if (here)
                m_detail->finishThinking(turn.thinkingMs);
            return;
        }
        if (type == QStringLiteral("delta")) {
            turn.streamed += event.value(QStringLiteral("text")).toString();
            if (here)
                m_detail->appendDelta(event.value(QStringLiteral("text")).toString());
            return;
        }
        // What the turn is doing this second. A Plan reads the repository for minutes before it
        // says a word, and a card that only said "thinking…" for all of it read as stuck
        // (owner, 2026-09-19). The cleanup's notice has drawn these lines since 19.9.
        if (type == QStringLiteral("status") || type == QStringLiteral("tool_started")
            || type == QStringLiteral("tool_result")) {
            const QString line = turnProgressLine(type, event);
            if (!line.isEmpty()) {
                turn.progress = line;
                if (here)
                    m_detail->setProgress(line);
            }
            return;
        }
        if (type == QStringLiteral("done") || type == QStringLiteral("error")
            || type == QStringLiteral("cancelled")) {
            const QString endedMode = turn.mode;   // read before the turn is dropped
            m_cardTurns.remove(card);
            syncModelBoxEnabled();
            m_list->viewport()->update();       // the row stops saying it is working
            if (here) {
                m_detail->setBusy(false);
                if (type == QStringLiteral("error"))
                    m_detail->showError(event.value(QStringLiteral("text")).toString());
            } else if (type == QStringLiteral("error")) {
                showNotice(QStringLiteral("#%1: %2").arg(card, event.value(QStringLiteral("text")).toString()),
                           true);
            }
            // The bell learns the turn ended even when the board is not the pane being looked at
            // (#NQP9); the view itself posts nothing and moves no focus.
            if (onTurnEnded) onTurnEnded(card, endedMode, type);
            return;
        }
    }
    // A write this pane asked for and the worker refused (a move into Needs QA without evidence,
    // a stale hash): say so where the card was dropped instead of in a status bar.
    if (type == QStringLiteral("error") && mine) {
        m_pendingNotes.remove(requestId);
        m_pendingDeletes.remove(requestId);
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
        // A question the agent could not take (no provider key, say): the question itself is
        // already in the thread, so say that under it rather than over the board. An error with
        // no card of its own belongs to the card this pane just asked on, which is the open one.
        if (const QString asked = m_detail->cardId(); m_cardTurns.contains(asked)) {
            m_cardTurns.remove(asked);
            syncModelBoxEnabled();
            m_list->viewport()->update();
            m_detail->setBusy(false);
            m_detail->showError(QStringLiteral("The Switchboard agent could not answer: %1 "
                                               "Your message is kept in the thread.").arg(text));
            return;
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
    if (errors == 0) {
        m_problemFix.clear();
        return;
    }
    m_problemFix = board::fixRequest(path, first, errors);
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
                           + QStringLiteral("\n\nClick to draft a fix for the Switchboard agent — "
                                            "it is put in its composer, not sent. `o` opens the "
                                            "file; python3 scripts/relay-board.py check lists "
                                            "every problem."));
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
    if (!m_model.filter().trimmed().isEmpty() || !m_model.labelFilter().isEmpty()) {
        // With a filter set — words or ticked label chips — what the rows say is the honest
        // number: it counts the closed cards a `status:done` search turns up, which "open"
        // never does.
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
        // The last widget in the row is the gear, not a section: qobject_cast skips it.
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
    // The first work-card folder, which is what quick add files into. `planning` is skipped:
    // until #X7NB it was tagged as a plan-card tab and so was never the default, and a card
    // quick-added into a folder named after the Planning stage would read as already planned.
    for (const board::Tab &tab : m_model.tabs())
        if (!tab.folder.isEmpty() && tab.type == QStringLiteral("work")
            && tab.id != QStringLiteral("planning"))
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

// Nothing is created straight into Done: a card gets there by being closed. A manual section
// takes them (#3XZV): a quick-add there is parked in it, its status still inbox.
bool BoardView::sectionTakesNewCards(const QString &columnId) const
{
    if (columnId == board::verifiedSection() || columnId == board::doneSection())
        return false;
    const QList<board::Column> sections = m_model.sections();
    for (const board::Column &section : sections) {
        if (section.id != columnId)
            continue;
        const QString landing = section.statuses.value(0);
        return !landing.isEmpty() ? (landing != QStringLiteral("done")
                                     && landing != QStringLiteral("dropped"))
                                  : true;   // collects nothing: a section filled by hand
    }
    return false;
}

void BoardView::selectRow(int index)
{
    if (index < 0 || index >= m_rows.size() || index >= m_list->count())
        return;
    QListWidgetItem *item = m_list->item(index);
    // Exactly one of the two is ever set (#93WR): a fold row is selectable but is not a card, so
    // standing on it empties the card selection and every card action is inert.
    m_selectedFold = m_rows.at(index).kind == board::Row::Fold ? m_rows.at(index).columnId
                                                              : QString();
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
    // A new pane starts as a compact overview: every section is a count that opens on demand.
    // Restored panes skip this seed and keep exactly the folds saved in their layout.
    if (!m_collapsedSeeded) {
        m_collapsedSeeded = true;
        for (const board::Column &section : m_model.sections())
            m_collapsed.insert(section.id);
    }
    m_rows = m_model.rows(m_collapsed, m_hidden, m_selfClosedOpen);

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
        // The self-closed fold row (#93WR): selectable, because Enter and ←/→ work on it, and
        // nothing else — it carries no card id, so it cannot be dragged, opened, moved or deleted,
        // and the handlers that read that id all step over it.
        if (row.kind == board::Row::Fold) {
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setToolTip(row.collapsed
                                 ? QStringLiteral("Cards the agent finished and closed itself, "
                                                  "without a verifier. Enter or → to show them.")
                                 : QStringLiteral("Cards the agent finished and closed itself, "
                                                  "without a verifier. Enter or ← to put them "
                                                  "away."));
            if (row.columnId == selectedFold()) {
                m_list->setCurrentItem(item);
                item->setSelected(true);
            }
            continue;
        }
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        item->setData(kCardRole, row.cardId);
        if (const board::Card *card = m_model.card(row.cardId)) {
            QString tip = QStringLiteral("%1 · %2\nPriority %3 — left-click raises the flag, "
                                         "right-click lowers it.\n%4")
                              .arg(card->reference(), card->title,
                                   card->priority > 0 ? QStringLiteral("+%1").arg(card->priority)
                                   : card->priority < 0 ? QStringLiteral("−1")
                                                        : QStringLiteral("0"),
                                   card->path);
            // The claim chip is eight characters of a session token (#R9G7), which says nothing
            // on its own: the tooltip is where the row says what they are and whether that pane
            // is still there. The chip itself is asked about here, as the row is filled, so a
            // pane closed while the board is up reads as closed at the next refill.
            if (!card->session.isEmpty()) {
                const QString chip = board::sessionChip(card->session, true);
                tip += (paneExists && !paneExists(card->session))
                           ? QStringLiteral("\nClaimed by the pane %1, which has closed").arg(chip)
                           : QStringLiteral("\nClaimed by the pane %1").arg(chip);
            }
            item->setToolTip(tip);
        }
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
    syncLabelChecks();     // and the chips which labels it keeps
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
    if (m_sectionsOpen) {        // the gear's page has the pane: a redraw must not push it off
        m_empty->hide();
        m_splitter->hide();
        m_keys->hide();
    }
    syncChatVisible();
}

// The page agent belongs to the list page: it is there whenever the board is, including a board
// with no cards yet, and it is away while a card or the sections page has the pane.
void BoardView::syncChatVisible()
{
    if (m_chat)
        m_chat->setVisible(m_open && !m_sectionsOpen && !detailOpen());
}

// The gear at the end of the section checkboxes: the section list itself, editable. It is a page
// in this pane, so the board stays where it is and Esc or Cancel comes straight back.
void BoardView::openSections()
{
    if (!m_open)
        return;
    m_sectionsOpen = true;
    m_sections->setModel(m_model);
    m_sections->setFolder(QFileInfo(m_root.isEmpty() ? projects::boardDirOf(m_workspace) : m_root).fileName());
    m_sections->show();
    m_sections->setFocus(Qt::OtherFocusReason);   // Esc closes the page from the moment it opens
    rebuild();
}

void BoardView::closeSections()
{
    if (!m_sectionsOpen)
        return;
    m_sectionsOpen = false;
    m_sections->hide();
    rebuild();
}

// A section folds and unfolds; which sections are folded is saved with the window's layout.
// A click on a column header (and a restored pane's saved sort): set the model, show it on the
// header, redraw. Safe on a board that has not opened yet — the rows are empty until the first
// `board` event, and the order is waiting for them.
void BoardView::setSortOrder(const QString &id)
{
    const board::Sort sort = board::sortFromId(id);
    if (sort == m_model.sort())
        return;
    m_model.setSort(sort);
    syncColumnHeader();
    rebuild();
    // The selection may have moved out from under the card: stand on it where it landed.
    const int at = board::rowOfCard(m_rows, m_selected);
    if (at >= 0)
        selectRow(at);
}

// The header shows what is on: the cell that wears the arrow. A restored pane opens with it
// right, because the sort rides the layout node and the header reads the model.
void BoardView::syncColumnHeader()
{
    if (m_columnHeader)
        m_columnHeader->setSort(m_model.sort());
}

// One click on a row's flag (#VKFV): raise it (left) or lower it (right), clamped at −1…+3.
// The row moves the moment it is clicked and the worker's `board_changed` settles it, exactly
// like a drag; a click at 0 that lands on 0 is still sent, so the notice and the undo record
// agree with what the file says.
void BoardView::setCardPriority(QString id, int step)
{
    const board::Card *card = m_model.card(id);
    if (!card)
        return;
    const int priority = qBound(-1, card->priority + step, 3);
    board::Card updated = *card;
    updated.priority = priority;
    m_model.upsert(updated);
    rebuild();
    // The page shows the click at once if it is the card on screen (#DPJB); the worker's
    // `board_changed` then confirms it, the way it settles the row.
    if (detailOpen() && m_detail->cardId() == id)
        m_detail->setPriority(priority);
    static const char *const names[5] = {"−1", "0", "+1", "+2", "+3"};
    const QString requestId = nextRequestId();
    m_pendingNotes.insert(requestId,
                          priority == 0
                              ? QStringLiteral("Cleared the flag on #%1").arg(id)
                              : QStringLiteral("Flagged #%1 at %2").arg(
                                    id, QString::fromLatin1(names[priority + 1])));
    send({{QStringLiteral("type"), QStringLiteral("board_priority")},
          {QStringLiteral("id"), requestId},
          {QStringLiteral("card"), id},
          {QStringLiteral("priority"), priority}});
}

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

// The self-closed fold row (#93WR): show this section's self-closed cards, or put them away.
// Folding while the selection is inside moves the selection to the fold row, which is where ←
// left off and where → carries on from.
void BoardView::toggleSelfClosed(QString columnId)
{
    if (columnId.isEmpty())
        return;
    const bool hadFocus = m_list->hasFocus();
    if (m_selfClosedOpen.contains(columnId))
        m_selfClosedOpen.remove(columnId);
    else
        m_selfClosedOpen.insert(columnId);
    rebuild();
    const int fold = board::rowOfFold(m_rows, columnId);
    if (board::rowOfCard(m_rows, m_selected) >= 0)
        return;               // the selected card is still on screen: nothing to move
    m_selected.clear();
    if (fold < 0) {
        m_selectedFold.clear();
        return;               // the row itself has gone (its section folded, or a filter)
    }
    m_selectedFold = columnId;
    if (hadFocus)
        selectRow(fold);
}

QJsonArray BoardView::openSelfClosed() const
{
    QStringList ids(m_selfClosedOpen.begin(), m_selfClosedOpen.end());
    ids.sort();
    return QJsonArray::fromStringList(ids);
}

void BoardView::setOpenSelfClosed(const QJsonArray &state)
{
    m_selfClosedOpen.clear();
    for (const QJsonValue &value : state)
        if (!value.toString().isEmpty())
            m_selfClosedOpen.insert(value.toString());
    if (m_open)
        rebuild();
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

// The label chips' ticked set, in the same shape and home in the layout node as the hidden
// sections (#VKFV). A label the board no longer carries is kept harmlessly: it filters nothing
// until a card wears it again.
QJsonArray BoardView::labelFilter() const
{
    QStringList labels(m_labelPicked.begin(), m_labelPicked.end());
    labels.sort();
    return QJsonArray::fromStringList(labels);
}

void BoardView::setLabelFilter(const QJsonArray &state)
{
    m_labelPicked.clear();
    for (const QJsonValue &value : state)
        if (!value.toString().isEmpty())
            m_labelPicked.insert(value.toString());
    m_model.setLabelFilter(m_labelPicked);
    // The chips exist only once the board's labels do; syncLabelChecks reads m_labelPicked when
    // it builds them, and this keeps any that are already up in step.
    if (m_labelChecksLayout) {
        for (int i = 0; i < m_labelIds.size() && i < m_labelChecksLayout->count(); ++i) {
            if (auto *box = qobject_cast<QCheckBox *>(m_labelChecksLayout->itemAt(i)->widget())) {
                const QSignalBlocker block(box);
                box->setChecked(m_labelPicked.contains(m_labelIds.at(i)));
            }
        }
    }
    if (m_open)
        rebuild();
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
        "&nbsp; <b>/</b> or <b>Esc</b> filter &nbsp; <b>a</b> ask the agent &nbsp; "
        "<b>t</b> #ID to prompt &nbsp; <b>y</b> copy &nbsp; "
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
    syncChatVisible();
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
    if (sameSection && m_model.sort() != board::Sort::Manual) {
        showNotice(QStringLiteral("The list is sorted by %1, so reordering is off. Click that "
                                  "column's header again for the board's own order.")
                       .arg(board::sortTitle(m_model.sort()).toLower()),
                   true);
        return;
    }
    QString note;
    if (!status.isEmpty() && !sameSection) {
        // Within its own section a card keeps its exact status (Needs QA stays LLM or human),
        // and a move to a status section takes it out of the manual section it may have been
        // parked in: an empty `section` is the unpark (#3XZV).
        message.insert(QStringLiteral("status"), status);
        message.insert(QStringLiteral("section"), QString());
        note = QStringLiteral("Moved #%1 to %2").arg(id, sectionTitle(columnId));
    } else if (status.isEmpty() && !sameSection) {
        // A manual section: the drop parks the card there and its status stays what it was.
        message.insert(QStringLiteral("section"), columnId);
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
    // Inbox by default (owner, #5G43): `n` and the toolbar button create in the board's intake
    // section, never in whatever section happens to hold the selection — with an in-progress card
    // selected, every new card was starting life in In progress. A section's own + button is the
    // way to add straight into that section.
    QString intake;
    for (const QString &id : columnIds()) {
        if (!sectionTakesNewCards(id))
            continue;
        if (intake.isEmpty())
            intake = id;      // a board reordered to have no Inbox: its first section
        if (m_model.dropStatus(id) == QStringLiteral("inbox")) {
            intake = id;
            break;
        }
    }
    quickAddIn(intake);
}

// The field sits over the list rather than inside a section: with one long list, a field at a
// section's head would be scrolled out of sight as often as not. It names the section it adds to,
// and it takes the card's *title*: Enter creates the card and opens it with the cursor in its
// issue box (#VZ69), so the one line here is never mistaken for the whole card. Esc, or leaving it
// empty, closes it.
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
    m_quickAdd->setPlaceholderText(QStringLiteral("Title of a new card in %1 — Enter opens it, Esc closes")
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
    // move on the list, but the open card stays until the edit is saved or dropped. Whatever was
    // queued for the card that is not going to open goes with it, rather than waiting to happen
    // to the next card that does.
    if (detailOpen() && m_detail->editing()) {
        m_editOnOpen = false;
        m_editOnOpenFresh = false;
        m_replyOnOpen = false;
        m_actionOnOpen.clear();
        return;
    }
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
        const bool fresh = m_editOnOpenFresh;
        m_editOnOpenFresh = false;
        m_detail->beginEdit(false, fresh);
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

// Execute (#XS6Q): the card goes to a terminal pane's agent. The window opens the pane
// (`onExecuteCard`), whose agent gets the card attached and the task text that tells it the
// board's conventions (board::executeTask), and the pane's session token comes back.
//
// With a token the board records the hand-off as a **claim** (#R9G7): one `board_claim` on the
// worker, which is the three writes Execute used to send by hand — assignee agent, the move to
// Executing, and the progress entry naming the pane — plus the `session` field in the card's
// front matter, so the Switchboard shows who has the card and another agent reading the board
// sees it is taken. Without a token the window could not open a pane, and the old three writes
// stand: the card still records the hand-off, it just has no pane to name.
//
// Nothing here is a model turn, so the Switchboard worker stays free for the next Discuss or Plan.
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
    // Read before the pane is opened: opening one spins the event loop, and the update below is
    // hash-checked against the version the card was read at.
    const QString baseHash = m_detail->hash();
    const QString status = m_detail->status();
    // The pane opens before anything is written, because what is written names it (#HKAP): the
    // claim, and the progress entry it makes, read "Claimed (<first 8 characters>) · …" and
    // draw as a link that reveals that pane.
    const QString paneToken = onExecuteCard(card, board::executeTask(card, m_detail->title(), m_detail->hasPlan(),
                                                                     m_detail->hasAcceptance(), note));
    if (!paneToken.isEmpty()) {
        const QString id = nextRequestId();
        m_pendingNotes.insert(id, QStringLiteral("Claimed #%1 · Execute").arg(card));
        send({{QStringLiteral("type"), QStringLiteral("board_claim")}, {QStringLiteral("id"), id},
              {QStringLiteral("card"), card}, {QStringLiteral("pane_token"), paneToken},
              {QStringLiteral("text"), note}});
        return;
    }
    // No pane: the writes Execute has always made, in this order on the worker's stdin — the
    // hash-checked update first, then the move (which re-reads the file), then the note.
    if (front.value(QStringLiteral("assignee")).toString() != QStringLiteral("agent"))
        send({{QStringLiteral("type"), QStringLiteral("board_update")}, {QStringLiteral("id"), nextRequestId()},
              {QStringLiteral("card"), card}, {QStringLiteral("base_hash"), baseHash},
              {QStringLiteral("patch"), QJsonObject{{QStringLiteral("fields"),
                                                     QJsonObject{{QStringLiteral("assignee"), QStringLiteral("agent")}}}}}});
    if (status != QStringLiteral("executing") && status != QStringLiteral("in-progress")) {
        const QString id = nextRequestId();
        m_pendingNotes.insert(id, QStringLiteral("Moved #%1 to Executing · Execute").arg(card));
        send({{QStringLiteral("type"), QStringLiteral("board_move")}, {QStringLiteral("id"), id},
              {QStringLiteral("card"), card}, {QStringLiteral("status"), QStringLiteral("executing")},
              {QStringLiteral("reason"), QStringLiteral("Execute: handed to a terminal pane")}});
    }
    // No pane to name, so no `pane_token` and nothing for the thread to link to: the plain
    // Execute wording, which is what the entry said before panes were linked at all.
    const QString entry =
            QStringLiteral("Execute · handed to a new terminal pane beside the Switchboard, whose "
                           "agent works on it and records its commits in `links.commits`.");
    send({{QStringLiteral("type"), QStringLiteral("board_comment")}, {QStringLiteral("card"), card},
          {QStringLiteral("kind"), QStringLiteral("progress")},
          {QStringLiteral("text"), entry + (note.isEmpty() ? QString()
                                                           : QStringLiteral("\n\n") + note)}});
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
    // The canonical signature is the `qa` block's; the card's own field is the fallback for a
    // worker that sends no `qa` (and then there is no runner either, so this is belt and braces).
    QString implementedBy = qa.value(QStringLiteral("implemented_by")).toString();
    if (implementedBy.isEmpty())
        implementedBy = m_detail->front().value(QStringLiteral("implemented_by")).toString();
    // The pane opens before the note is sent (#HKAP), because the note carries its session
    // token — the same treatment as Execute's entry: "Verifying (<first 8 characters>) · …"
    // links to the verifier's pane. Without a token, the plain Verify wording stays.
    const QString paneToken = onVerifyCard(card, runner,
                                           board::verifyTask(card, m_detail->title(), label, implementedBy,
                                                             m_detail->status(), note));
    QString text = paneToken.isEmpty()
            ? QStringLiteral("Verify · handed to a new terminal pane on %1").arg(label)
            : QStringLiteral("Verifying (%1) · handed to a new terminal pane on %2")
                      .arg(paneToken.left(8), label);
    if (!why.isEmpty())
        text += QStringLiteral(" · ") + why;
    if (!note.isEmpty())
        text += QStringLiteral("\n\n") + note;
    send({{QStringLiteral("type"), QStringLiteral("board_comment")}, {QStringLiteral("card"), card},
          {QStringLiteral("kind"), QStringLiteral("progress")},
          {QStringLiteral("pane_token"), paneToken},
          {QStringLiteral("text"), text}});
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
    m_selectedFold.clear();
    // A card the fold row is holding cannot be stood on until the row is open (#93WR), so a card
    // reached by id — a `#ID` in a card's or a thread's text, a link from the helper, the cleanup
    // panel's anchors — opens what holds it: its self-closed group, and the section around it.
    // Only for a self-closed card: every other card is where it always was.
    if (board::rowOfCard(m_rows, id) < 0) {
        const board::Card *card = m_model.card(id);
        const QString section = card ? m_model.sectionOf(*card) : QString();
        if (card && board::selfClosed(*card) && !section.isEmpty()) {
            bool opened = false;
            if (!m_selfClosedOpen.contains(section)) {
                m_selfClosedOpen.insert(section);
                opened = true;
            }
            if (m_collapsed.contains(section)) {
                m_collapsedSeeded = true;
                m_collapsed.remove(section);
                opened = true;
            }
            if (opened)
                rebuild();
        }
    }
    const int at = board::rowOfCard(m_rows, id);
    if (at >= 0)
        selectRow(at);
}

void BoardView::copyReference()
{
    copyTag(m_selected);
}

// A label hashtag was clicked (#3ZAP) — a row badge, the meta's labels, or a `#tag` in the
// card's own words or the thread: the copy-and-notice `copyReference` gives a card id.
void BoardView::copyTag(const QString &tag)
{
    if (tag.isEmpty())
        return;
    QApplication::clipboard()->setText(QStringLiteral("#") + tag);
    showNotice(QStringLiteral("Copied #%1").arg(tag), false);
}

// Zoom to a card by id (#3ZAP): a `#ID` reference in a card's text takes the same path the
// cleanup panel's `card:` anchors do.
void BoardView::openCard(const QString &id)
{
    if (id.isEmpty())
        return;
    selectCard(id);
    m_selected = id;
    openSelected();
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
    menu.addSeparator();
    // The `m` menu is where the owner looked for a way to close a card and found none (#CYM9):
    // delete sits at its foot, destructive and labelled so, below every move.
    QAction *remove = menu.addAction(QStringLiteral("Delete card…"));
    connect(remove, &QAction::triggered, this, [this] {
        if (onHint)
            onHint(QStringLiteral("board.delete"), QStringLiteral("Del"));
        deleteSelected();
    });
    // Under the selected row rather than wherever the mouse happens to be.
    QPoint at = QCursor::pos();
    if (QListWidgetItem *current = m_list->currentItem())
        at = m_list->viewport()->mapToGlobal(m_list->visualItemRect(current).bottomLeft());
    menu.exec(at);
}

void BoardView::deleteSelected()
{
    deleteCard(m_selected);
}

// The owner's delete (card #CYM9): confirm, then one `board_delete` message; the worker takes
// the file and its thread off disk and the write's Undo puts them back. The card is remembered
// as ours until the write lands, so the removal events that follow do not talk over the toast.
void BoardView::deleteCard(const QString &id)
{
    if (id.isEmpty())
        return;
    const board::Card *card = m_model.card(id);
    if (!confirmDeleteCard(id, card ? card->title : QString(), this))
        return;
    const QString requestId = nextRequestId();
    m_pendingNotes.insert(requestId, QStringLiteral("Deleted #%1").arg(id));
    m_pendingDeletes.insert(requestId, id);
    send({{QStringLiteral("type"), QStringLiteral("board_delete")},
          {QStringLiteral("id"), requestId},
          {QStringLiteral("card"), id},
          {QStringLiteral("reason"), QStringLiteral("deleted in the Switchboard")}});
}

bool BoardView::deletedHere(const QString &card) const
{
    return m_pendingDeletes.values().contains(card);
}

void BoardView::forgetDeleted(const QString &card)
{
    for (auto it = m_pendingDeletes.begin(); it != m_pendingDeletes.end();) {
        if (it.value() == card)
            it = m_pendingDeletes.erase(it);
        else
            ++it;
    }
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

// Left folds the section the selection is in — or, on the self-closed fold row or on one of its
// cards, puts those cards away first (#93WR). One key, innermost first: ← on a self-closed card
// lands on the fold row, ← again folds the whole section.
void BoardView::foldSelected()
{
    if (!selectedFold().isEmpty()) {
        const QString columnId = selectedFold();
        if (m_selfClosedOpen.contains(columnId))
            toggleSelfClosed(columnId);
        else
            toggleSection(columnId);
        return;
    }
    const int at = board::rowOfCard(m_rows, m_selected);
    if (at < 0)
        return;
    const QString columnId = m_rows.at(at).columnId;
    const board::Card *card = m_model.card(m_selected);
    if (card && board::selfClosed(*card) && m_selfClosedOpen.contains(columnId)) {
        toggleSelfClosed(columnId);
        return;
    }
    toggleSection(columnId);
}

// Right unfolds the folded section nearest the selection, above or below, and stands on its
// first card. Nearest, not "the next one down": right after Left the folded header is next to
// the selection, so the two keys undo each other, and at the end of the list Right opens Done
// rather than jumping back to whatever was folded at the top.
void BoardView::unfoldNearest()
{
    // On the self-closed fold row (#93WR), → is that row's own key: it shows its cards, and on an
    // open row it steps into the first of them, the way ← steps back out.
    if (!selectedFold().isEmpty()) {
        const QString columnId = selectedFold();
        if (!m_selfClosedOpen.contains(columnId))
            toggleSelfClosed(columnId);
        const int first = board::stepRow(m_rows, board::rowOfFold(m_rows, columnId), 1);
        if (first >= 0 && m_rows.at(first).kind == board::Row::Card)
            selectRow(first);
        return;
    }
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
    // `a` asks the page agent (#8YQ9): it puts the keyboard in the panel's composer, as `/` puts
    // it in the filter — one key for narrowing the list, one for asking about it. It is a bare
    // letter and not a chord on purpose: Ctrl+/ is already `help.shortcuts`, and the window's
    // event filter matches the keymap and accepts that key before the board ever sees it.
    if (text == QStringLiteral("a")) {
        if (detailOpen())
            closeDetail();      // the panel is the list page's
        focusChat();
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
    // Del deletes the open or the selected card (#CYM9), after the confirm the button asks.
    // Owner-only by construction: an agent has no way to send this message.
    if (mods == Qt::NoModifier && key->key() == Qt::Key_Delete
        && (detailOpen() || !m_selected.isEmpty())) {
        deleteCard(detailOpen() ? m_detail->cardId() : m_selected);
        return true;
    }
    if (key->key() == Qt::Key_Escape && detailOpen()) {
        closeDetail();
        return true;
    }
    // On the main page Esc is the way to the filter bar (#K9X6). An open card has gone back
    // above; an active filter comes off next — the same first Esc as inside the box — and with
    // nothing left to undo the bar itself takes the focus and selects what is in it. The list
    // reaches this through its own event filter, so the rule lives here once for the whole page.
    if (key->key() == Qt::Key_Escape) {
        if (!m_filter->text().isEmpty())
            m_filter->clear();
        else
            focusFilter();
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
    // Clicking into the filter is the mouse way there; Esc reaches the bar from anywhere on
    // the main page (#K9X6), so that is the hint. Keyboard focus (`/`, Esc itself, Tab) is
    // already the fast path and says nothing.
    if (object == m_filter && event->type() == QEvent::FocusIn
        && static_cast<QFocusEvent *>(event)->reason() == Qt::MouseFocusReason && onHint) {
        onHint(QStringLiteral("board.filter"), QStringLiteral("Esc"));
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
            // Enter on the self-closed fold row (#93WR) toggles it; a fold row is not a card and
            // has nothing to open.
            if (!selectedFold().isEmpty())
                toggleSelfClosed(selectedFold());
            else
                openSelected();
            return true;
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
    syncModelBoxEnabled();
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
    syncModelBoxEnabled();
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
