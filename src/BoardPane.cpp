// SPDX-License-Identifier: GPL-3.0-or-later
#include "BoardPane.h"

#include <QApplication>
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
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
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

#include "RichEditor.h"
#include "Theme.h"

namespace relay {
namespace {

constexpr int kCardRole = Qt::UserRole;          // the card id on a list row
// CardList has no Q_OBJECT (it declares no signals of its own), so its column travels as a
// dynamic property rather than through qobject_cast.
const char *const kColumnProperty = "relayBoardColumn";

// Card geometry (owner review, 2026-09-17: the columns used to be bare text rows with no card
// edge, no spacing and the chips run into the title). Each card is a raised, rounded box with
// the title on top and a footer of badges; the numbers keep a 6 px gutter between two cards.
constexpr int kCardGap = 3;          // above and below every card, so two cards are 6 px apart
constexpr int kCardPadX = 10, kCardPadY = 8;
constexpr int kTitleLines = 3;
// Below this width the open card takes the whole pane instead of squeezing the columns.
constexpr int kStackedWidth = 900;

QString columnIdOf(const QWidget *widget)
{
    return widget ? widget->property(kColumnProperty).toString() : QString();
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

QFont smaller(const QFont &base, qreal factor)
{
    QFont font(base);
    if (base.pointSizeF() > 0)
        font.setPointSizeF(base.pointSizeF() * factor);
    else
        font.setPixelSize(qMax(8, int(base.pixelSize() * factor)));
    return font;
}

QFont monoFont(const QFont &base, qreal factor)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (base.pointSizeF() > 0)
        font.setPointSizeF(base.pointSizeF() * factor);
    return font;
}

// Up to `maxLines` lines of `text` wrapped at `width`; the last one is elided.
QStringList wrapLines(const QString &text, const QFont &font, int width, int maxLines)
{
    QStringList lines;
    const QFontMetrics metrics(font);
    QTextLayout layout(text, font);
    layout.beginLayout();
    for (;;) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(width);
        if (lines.size() == maxLines - 1) {
            lines << metrics.elidedText(text.mid(line.textStart()).simplified(), Qt::ElideRight, width);
            break;
        }
        lines << text.mid(line.textStart(), line.textLength()).trimmed();
    }
    layout.endLayout();
    return lines;
}

// Where everything on one card goes, relative to the row's top-left corner. Computed the same
// way for sizeHint() and paint(), so a card is always exactly as tall as what it draws.
struct CardShape {
    QStringList title;
    QRect titleRect, idRect;
    QList<QPair<board::Badge, QRect>> badges;
    int height = 0;
};

CardShape shapeOf(const board::Card &card, bool showStatus, const QFont &font, int width)
{
    CardShape shape;
    const int inner = qMax(40, width - 2 * kCardPadX);
    const QFontMetrics titleMetrics(font);
    shape.title = wrapLines(card.title.isEmpty() ? QStringLiteral("(untitled)") : card.title, font,
                            inner, kTitleLines);
    int y = kCardGap + kCardPadY;
    shape.titleRect = QRect(kCardPadX, y, inner, int(shape.title.size()) * titleMetrics.lineSpacing());
    y = shape.titleRect.bottom() + 1 + 6;

    const QFont badgeFont = smaller(font, 0.85);
    const QFontMetrics badgeMetrics(badgeFont);
    const int rowHeight = badgeMetrics.height() + 4;
    const QFontMetrics idMetrics(monoFont(font, 0.85));
    int x = kCardPadX;
    shape.idRect = QRect(x, y, idMetrics.horizontalAdvance(card.reference()), rowHeight);
    x = shape.idRect.right() + 1 + 8;
    for (const board::Badge &badge : board::badges(card, showStatus)) {
        const int w = qMin(inner, badgeMetrics.horizontalAdvance(badge.text) + 12);
        if (x + w > kCardPadX + inner) {
            x = kCardPadX;
            y += rowHeight + 4;
        }
        shape.badges << qMakePair(badge, QRect(x, y, w, rowHeight));
        x += w + 4;
    }
    shape.height = y + rowHeight + kCardPadY + kCardGap;
    return shape;
}

// Paints a list row as a card. It reads the card from the model by id, so a row holds nothing
// but the id and a refill never copies card data into the view.
class CardDelegate final : public QStyledItemDelegate {
public:
    CardDelegate(const board::Model *model, QListWidget *list)
        : QStyledItemDelegate(list), m_model(model), m_list(list)
    {
    }

    // Columns that collect several statuses (Waiting, Needs QA) name the exact one on the card;
    // `quiet` is the one not worth a badge (Done in the Done column).
    void setStatusBadges(bool on, const QString &quiet)
    {
        m_statusBadges = on;
        m_quietStatus = quiet;
    }

    // The card width comes from the list, with room for its scrollbar kept whether or not the
    // scrollbar is showing. From the viewport instead, a column that grew a scrollbar would be
    // painted narrower than it was measured (QListView re-measures on a resize of the list, not
    // of its viewport), and the cards overlapped or left gaps.
    int cardWidth() const
    {
        const int reserve = m_list->verticalScrollBar()->sizeHint().width() + 2;
        return qMax(60, m_list->width() - 2 * m_list->frameWidth() - reserve);
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const board::Card *card = m_model->card(index.data(kCardRole).toString());
        const int width = cardWidth();
        if (!card)
            return QSize(width, 0);
        return QSize(width, shapeOf(*card, showStatus(*card), option.font, width).height);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const board::Card *card = m_model->card(index.data(kCardRole).toString());
        if (!card)
            return;
        QRect row = option.rect;
        row.setWidth(qMin(row.width(), cardWidth()));
        const CardShape shape = shapeOf(*card, showStatus(*card), option.font, row.width());
        const bool selected = option.state & QStyle::State_Selected;
        const bool hover = option.state & QStyle::State_MouseOver;
        const bool focused = m_list->hasFocus();

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF box = QRectF(row).adjusted(0.5, kCardGap + 0.5, -0.5, -kCardGap - 0.5);
        QColor fill = theme::SurfaceRaised;
        if (hover || selected)
            fill = mix(theme::SurfaceRaised, theme::Text, 0.04);
        QColor edge = theme::Border;
        if (selected)
            edge = focused ? theme::Accent : theme::BorderStrong;
        else if (hover)
            edge = theme::BorderStrong;
        painter->setPen(QPen(edge, selected && focused ? 1.5 : 1.0));
        painter->setBrush(fill);
        painter->drawRoundedRect(box, 6, 6);

        const QPoint origin = row.topLeft();
        painter->setFont(option.font);
        painter->setPen(theme::Text);
        const int lineHeight = QFontMetrics(option.font).lineSpacing();
        for (int i = 0; i < shape.title.size(); ++i)
            painter->drawText(QRect(shape.titleRect.left() + origin.x(),
                                    shape.titleRect.top() + origin.y() + i * lineHeight,
                                    shape.titleRect.width(), lineHeight),
                              Qt::AlignLeft | Qt::AlignVCenter, shape.title.at(i));

        painter->setFont(monoFont(option.font, 0.85));
        painter->setPen(theme::TextMuted);
        painter->drawText(shape.idRect.translated(origin), Qt::AlignLeft | Qt::AlignVCenter,
                          card->reference());

        painter->setFont(smaller(option.font, 0.85));
        for (const auto &placed : shape.badges) {
            const QRectF pill = QRectF(placed.second.translated(origin)).adjusted(0.5, 0.5, -0.5, -0.5);
            QColor ink = theme::TextMuted, border = theme::Border;
            switch (placed.first.kind) {
            case board::Badge::Agent:
                ink = theme::Agent;
                border = mix(theme::Agent, theme::SurfaceRaised, 0.55);
                break;
            case board::Badge::Waiting:
                ink = theme::Warning;
                border = mix(theme::Warning, theme::SurfaceRaised, 0.55);
                break;
            case board::Badge::TasksDone:
                ink = theme::Success;
                break;
            case board::Badge::Status:
            case board::Badge::Assignee:
                ink = theme::Text;
                break;
            default:
                break;
            }
            painter->setPen(QPen(border, 1.0));
            painter->setBrush(theme::Surface);
            painter->drawRoundedRect(pill, 4, 4);
            painter->setPen(ink);
            painter->drawText(pill, Qt::AlignCenter,
                              QFontMetrics(painter->font())
                                  .elidedText(placed.first.text, Qt::ElideRight, int(pill.width()) - 8));
        }
        painter->restore();
    }

private:
    bool showStatus(const board::Card &card) const
    {
        return m_statusBadges && card.status != m_quietStatus;
    }

    const board::Model *m_model;
    QListWidget *m_list;
    bool m_statusBadges = false;
    QString m_quietStatus;
};

// A column list that reports a drop instead of moving the row itself: the card only moves once
// the worker has written the file and sent board_changed back. While a card is dragged over it,
// the list tints itself and draws a line where the card would land.
class CardList final : public QListWidget {
public:
    explicit CardList(const QString &columnId, QWidget *parent = nullptr)
        : QListWidget(parent), m_columnId(columnId)
    {
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::MoveAction);
        setDropIndicatorShown(false);    // drawn by paintEvent below, between the cards
        setSelectionMode(QAbstractItemView::SingleSelection);
        setUniformItemSizes(false);
        setResizeMode(QListView::Adjust);           // cards re-wrap when the column resizes
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);   // cards are tall; no jumps
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setFrameShape(QFrame::NoFrame);
        setMouseTracking(true);
        viewport()->setAttribute(Qt::WA_Hover);
        setObjectName(QStringLiteral("boardColumn"));
        setProperty(kColumnProperty, columnId);
    }

    QString columnId() const { return m_columnId; }
    QString placeholder;             // drawn when the column has no cards
    // (card id, id it goes before, id it goes after)
    std::function<void(const QString &, const QString &, const QString &)> onDropped;
    std::function<void(bool)> onDragging;   // a drag from this list started (true) or ended
    static QString dragging;

    QStringList ids() const
    {
        QStringList out;
        for (int i = 0; i < count(); ++i)
            out << item(i)->data(kCardRole).toString();
        return out;
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QListWidget::resizeEvent(event);
        if (event->size().width() != event->oldSize().width())
            scheduleDelayedItemsLayout();   // the cards re-wrap to the new width
    }

    void showEvent(QShowEvent *event) override
    {
        QListWidget::showEvent(event);
        scheduleDelayedItemsLayout();       // measured while hidden, at whatever width it had
    }

    // Our own drag, not QListWidget's: the default image is the row painted on a transparent
    // pixmap, which without a compositor is a black box. This one is the card on the board's
    // background. The payload is the text `#ID`, so dropping a card on the prompt box types it.
    void startDrag(Qt::DropActions) override
    {
        QListWidgetItem *item = currentItem();
        if (!item)
            return;
        dragging = item->data(kCardRole).toString();
        const QRect rect = visualItemRect(item);
        const qreal ratio = devicePixelRatioF();
        QPixmap pixmap(rect.size() * ratio);
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(theme::Background);
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
        drag->setHotSpot(viewport()->mapFromGlobal(QCursor::pos()) - rect.topLeft());
        if (onDragging)
            onDragging(true);
        drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);   // until dropped or cancelled
        dragging.clear();
        m_dropActive = false;
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
        const int row = insertionRow(event->position().toPoint());
#else
        const int row = insertionRow(event->pos());
#endif
        if (row != m_dropRow) {
            m_dropRow = row;
            viewport()->update();
        }
    }

    void dragLeaveEvent(QDragLeaveEvent *event) override
    {
        QListWidget::dragLeaveEvent(event);
        m_dropActive = false;
        m_dropRow = -1;
        viewport()->update();
    }

    void dropEvent(QDropEvent *event) override
    {
        const QString card = dragging;
        dragging.clear();
        m_dropActive = false;
        viewport()->update();
        if (card.isEmpty()) {
            event->ignore();
            return;
        }
#if QT_VERSION_MAJOR >= 6
        const int row = insertionRow(event->position().toPoint());
#else
        const int row = insertionRow(event->pos());
#endif
        m_dropRow = -1;
        const QStringList order = ids();
        const int from = order.indexOf(card);
        const int slot = from >= 0 && from < row ? row - 1 : row;
        // The model is the file on disk; nothing moves in the view until the worker says so.
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
        if (from >= 0 && slot == from)
            return;   // dropped where it already was: no write, no thread entry
        const auto [before, after] = board::placement(order, card, slot);
        if (onDropped)
            onDropped(card, before, after);
    }

    void paintEvent(QPaintEvent *event) override
    {
        QListWidget::paintEvent(event);
        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing);
        if (count() == 0 && !placeholder.isEmpty()) {
            painter.setPen(theme::TextMuted);
            painter.setFont(smaller(font(), 0.9));
            painter.drawText(viewport()->rect().adjusted(8, 14, -8, 0),
                             Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, placeholder);
        }
        if (!m_dropActive)
            return;
        painter.setPen(QPen(alpha(theme::Accent, 110), 1.0));
        painter.setBrush(alpha(theme::Accent, 14));
        painter.drawRoundedRect(QRectF(viewport()->rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
        int y = kCardGap;
        if (count() > 0) {
            const int row = qBound(0, m_dropRow, count());
            y = row < count() ? visualItemRect(item(row)).top()
                              : visualItemRect(item(count() - 1)).bottom() + 1;
        }
        painter.setPen(QPen(theme::Accent, 2.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(6, y), QPointF(viewport()->width() - 6, y));
    }

private:
    // The row a card dropped at `pos` goes in front of: the first card whose middle is below it.
    int insertionRow(const QPoint &pos) const
    {
        for (int i = 0; i < count(); ++i)
            if (pos.y() < visualItemRect(item(i)).center().y())
                return i;
        return count();
    }

    QString m_columnId;
    bool m_dropActive = false;
    int m_dropRow = -1;
};

QString CardList::dragging;

}  // namespace

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
        m_toPrompt = textButton(QStringLiteral("#ID → prompt"),
                                QStringLiteral("Insert this card's #ID in the terminal's prompt (t)"));
        m_openFile = textButton(QStringLiteral("Open file"),
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

        m_title = new QLabel(this);
        m_title->setWordWrap(true);
        m_title->setObjectName(QStringLiteral("boardCardTitle"));
        layout->addWidget(m_title);

        auto *pickers = new QHBoxLayout;
        pickers->setSpacing(6);
        m_status = new QComboBox(this);
        m_status->setObjectName(QStringLiteral("boardPicker"));
        m_status->setToolTip(QStringLiteral("Status: the column the card sits in"));
        m_tab = new QComboBox(this);
        m_tab->setObjectName(QStringLiteral("boardPicker"));
        m_tab->setToolTip(QStringLiteral("Tab: the category folder the card lives in"));
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

        m_doc = new QTextBrowser(this);
        m_doc->setObjectName(QStringLiteral("boardCardDocument"));
        m_doc->setOpenLinks(false);      // a relative link would otherwise replace the card
        m_doc->document()->setDocumentMargin(12);
        layout->addWidget(m_doc, 1);

        m_error = new QLabel(this);
        m_error->setObjectName(QStringLiteral("boardCardError"));
        m_error->setWordWrap(true);
        m_error->hide();
        layout->addWidget(m_error);

        auto *reply = new QFrame(this);
        reply->setObjectName(QStringLiteral("boardReply"));
        auto *replyLayout = new QVBoxLayout(reply);
        replyLayout->setContentsMargins(8, 6, 8, 6);
        replyLayout->setSpacing(4);
        m_reply = new RichEditor(reply);
        m_reply->setObjectName(QStringLiteral("boardReplyEditor"));
        m_reply->setPlaceholders({QStringLiteral("Reply — Enter asks the agent, Ctrl+Shift+Enter only comments"),
                                  QStringLiteral("Reply — Enter asks the agent"),
                                  QStringLiteral("Reply to this card…"), QStringLiteral("Reply…")});
        m_reply->setAutoHeight(2, 8);
        replyLayout->addWidget(m_reply);
        auto *buttons = new QHBoxLayout;
        buttons->setSpacing(6);
        buttons->addStretch(1);
        m_comment = new QPushButton(QStringLiteral("Comment"), reply);
        m_comment->setObjectName(QStringLiteral("boardReplyButton"));
        m_comment->setToolTip(QStringLiteral("Append to the thread without calling a model (Ctrl+Shift+Enter)"));
        m_ask = new QPushButton(QStringLiteral("Ask the agent"), reply);
        m_ask->setObjectName(QStringLiteral("primary"));
        m_ask->setToolTip(QStringLiteral("The Switchboard agent answers in this thread (Enter)"));
        buttons->addWidget(m_comment);
        buttons->addWidget(m_ask);
        replyLayout->addLayout(buttons);
        layout->addWidget(reply);

        m_render = new QTimer(this);
        m_render->setSingleShot(true);
        m_render->setInterval(40);   // a streamed answer re-renders at most 25 times a second
        connect(m_render, &QTimer::timeout, this, [this] { render(Scroll::Follow); });

        connect(m_ask, &QPushButton::clicked, this, [this] {
            if (m_busy) {
                if (onCancel)
                    onCancel();
                return;
            }
            submit(true);
        });
        connect(m_comment, &QPushButton::clicked, this, [this] { submit(false); });
        connect(m_close, &QToolButton::clicked, this, [this] { if (onClose) onClose(); });
        connect(m_toPrompt, &QToolButton::clicked, this, [this] { if (onToPrompt) onToPrompt(); });
        connect(m_openFile, &QToolButton::clicked, this, [this] { if (onOpenPath) onOpenPath(m_path); });
        // Ctrl+Shift+Enter is the composer's "terminal, never the model" chord; here it means
        // the same thing: a note on the thread with no model call.
        m_reply->onSubmit = [this](const QString &route) { submit(route != QStringLiteral("shell")); };
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

    std::function<void(const QString &text, bool ask)> onReply;
    std::function<void()> onClose, onCancel, onToPrompt, onEscape;
    std::function<void(const QString &what, const QString &value)> onMove;
    // A path relative to the workspace (the card file) or to the card (a link in its body).
    std::function<void(const QString &path)> onOpenPath;

    QString cardId() const { return m_id; }
    QString path() const { return m_path; }

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
        }
        m_id = id;
        m_path = card.value(QStringLiteral("path")).toString();
        const QJsonObject front = card.value(QStringLiteral("front")).toObject();
        const QString title = card.value(QStringLiteral("title")).toString();
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

    void setBusy(bool busy)
    {
        m_busy = busy;
        m_ask->setText(busy ? QStringLiteral("Stop") : QStringLiteral("Ask the agent"));
        m_ask->setToolTip(busy ? QStringLiteral("Stop the Switchboard agent's answer")
                               : QStringLiteral("The Switchboard agent answers in this thread (Enter)"));
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
    bool replyHasFocus() const { return m_reply->hasFocus(); }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == m_reply && event->type() == QEvent::KeyPress
            && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            if (onEscape)
                onEscape();
            return true;
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

    void submit(bool ask)
    {
        const QString text = m_reply->toPlainText().trimmed();
        if (text.isEmpty() || !onReply || (ask && m_busy))
            return;
        m_reply->remember(text);
        m_reply->clear();
        m_error->hide();
        onReply(text, ask);
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
        muted.setFontPointSize(base * 0.9);
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
            insertLine(cursor, QStringLiteral("No replies yet. Ask the agent about this card, or "
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
            if (!model.isEmpty())
                extra << model;
            if (!kind.isEmpty() && kind != QStringLiteral("comment"))
                extra << kind;
            if (!age.isEmpty())
                extra << age;
            if (!extra.isEmpty())
                cursor.insertText(QStringLiteral("  ") + extra.join(QStringLiteral(" · ")), muted);
            cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
            insertMarkdown(cursor, text, base);
        }
        if (m_busy || !m_streaming.isEmpty()) {
            QTextCharFormat who;
            who.setFontWeight(QFont::DemiBold);
            who.setForeground(theme::Agent);
            insertLine(cursor, QStringLiteral("✦ agent"), who, 14);
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
    QComboBox *m_status = nullptr, *m_tab = nullptr;
    QToolButton *m_close = nullptr, *m_toPrompt = nullptr, *m_openFile = nullptr;
    QTextBrowser *m_doc = nullptr;
    RichEditor *m_reply = nullptr;
    QPushButton *m_ask = nullptr, *m_comment = nullptr;
    QTimer *m_render = nullptr;
    QList<QJsonObject> m_entries;
    QHash<QString, QString> m_drafts;
    QString m_body, m_streaming, m_id, m_path;
    int m_threadTotal = 0;
    bool m_loading = false, m_busy = false;
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
    // Two rows of chrome: the tabs get the whole width (seven of them do not fit beside a
    // filter field in a half-width pane), then the filter and the one button that adds.
    auto *head = new QWidget(this);
    head->setObjectName(QStringLiteral("boardHead"));
    auto *headLayout = new QVBoxLayout(head);
    headLayout->setContentsMargins(8, 4, 8, 6);
    headLayout->setSpacing(6);
    m_tabs = new QTabBar(head);
    m_tabs->setObjectName(QStringLiteral("boardTabs"));
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(false);
    m_tabs->setElideMode(Qt::ElideNone);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setFocusPolicy(Qt::NoFocus);
    m_tabs->setAcceptDrops(true);          // drop a card on a tab: move it to that category
    m_tabs->installEventFilter(this);
    m_tabRow = new QHBoxLayout;
    m_tabRow->setContentsMargins(0, 0, 0, 0);
    m_tabRow->addWidget(m_tabs, 1);
    headLayout->addLayout(m_tabRow);

    auto *tools = new QHBoxLayout;
    tools->setSpacing(6);
    m_filter = new QLineEdit(head);
    m_filter->setObjectName(QStringLiteral("boardFilter"));
    m_filter->setPlaceholderText(QStringLiteral("Filter  —  words, label:voice, status:ready, @agent, waiting:me"));
    m_filter->setClearButtonEnabled(true);
    tools->addWidget(m_filter, 1);
    auto *add = new QToolButton(head);
    add->setObjectName(QStringLiteral("boardAddButton"));
    add->setText(QStringLiteral("+  New card"));
    add->setToolTip(QStringLiteral("New card in the first column (n)"));
    add->setCursor(Qt::PointingHandCursor);
    add->setFocusPolicy(Qt::NoFocus);
    tools->addWidget(add);
    headLayout->addLayout(tools);
    layout->addWidget(head);

    m_problems = new QLabel(this);
    m_problems->setMinimumWidth(1);   // one line, clipped in a narrow pane; the tooltip has it all
    m_problems->setObjectName(QStringLiteral("boardProblems"));
    m_problems->setTextFormat(Qt::RichText);
    m_problems->hide();
    connect(m_problems, &QLabel::linkActivated, this, [this](const QString &path) {
        if (onOpenFile && !path.isEmpty())
            onOpenFile(QDir(m_workspace).absoluteFilePath(path));
    });
    auto *problemsRow = new QHBoxLayout;
    problemsRow->setContentsMargins(8, 0, 8, 6);
    problemsRow->addWidget(m_problems);
    layout->addLayout(problemsRow);

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
    m_noticeUndo->setText(QStringLiteral("Undo"));
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
    m_scroll = new QScrollArea(m_splitter);
    m_scroll->setObjectName(QStringLiteral("boardScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   // each column scrolls itself
    m_columns = new QWidget(m_scroll);
    m_columns->setObjectName(QStringLiteral("boardColumns"));
    auto *columns = new QHBoxLayout(m_columns);
    columns->setContentsMargins(8, 0, 8, 6);
    columns->setSpacing(8);
    m_scroll->setWidget(m_columns);
    m_splitter->addWidget(m_scroll);

    m_detail = new CardDetail(m_splitter);
    m_detail->hide();
    m_splitter->addWidget(m_detail);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->hide();                 // until the first `board` event: the loading line instead
    layout->addWidget(m_splitter, 1);

    m_empty = new QLabel(QStringLiteral("Loading the Switchboard…"), this);
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

    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (m_buildingTabs || index < 0 || index >= m_model.tabs().size())
            return;
        m_tab = m_model.tabs().at(index).id;
        const bool hadFocus = isAncestorOf(QApplication::focusWidget());
        rebuild();
        if (hadFocus)
            focusInput();
    });
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_model.setFilter(text);
        rebuild();
    });
    connect(add, &QToolButton::clicked, this, [this] {
        if (onHint)
            onHint(QStringLiteral("board.quickAdd"), QStringLiteral("n"));
        quickAdd();
    });
    m_filter->installEventFilter(this);

    updateDetailLayout();   // the key line's first text
    m_detail->onClose = [this] { closeDetail(); };
    m_detail->onEscape = [this] {
        // Esc in the reply box goes back to the cards; a second Esc there closes the card. With
        // the columns hidden (a narrow pane) there is nothing to go back to but the board.
        auto *list = listOfCard(m_detail->cardId());
        if (list && !m_scroll->isHidden())
            list->setFocus();
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
    m_detail->onReply = [this](const QString &text, bool ask) {
        const QString card = m_detail->cardId();
        if (card.isEmpty())
            return;
        if (ask) {
            m_askCard = card;
            m_detail->setBusy(true);
            send({{QStringLiteral("type"), QStringLiteral("board_ask")},
                  {QStringLiteral("card"), card}, {QStringLiteral("text"), text}});
        } else {
            send({{QStringLiteral("type"), QStringLiteral("board_comment")},
                  {QStringLiteral("card"), card}, {QStringLiteral("text"), text},
                  {QStringLiteral("kind"), QStringLiteral("note")}});
        }
    };
    m_detail->onCancel = [this] { send({{QStringLiteral("type"), QStringLiteral("cancel")}}); };
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
}

// Watch `issues/` and its folders. A card write anywhere (this window, a pane agent, a
// collaborator's merge) becomes one debounced board_refresh, which the worker answers with the
// rows that actually changed.
void BoardView::watchIssues()
{
    const QString root = m_workspace + QStringLiteral("/issues");
    if (!QFileInfo::exists(root))
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

QString BoardView::title() const
{
    const int total = m_model.total();
    return total > 0 ? QStringLiteral("Switchboard · %1").arg(total) : QStringLiteral("Switchboard");
}

QString BoardView::currentTab() const
{
    return m_tab;
}

void BoardView::setCurrentTab(const QString &tabId)
{
    const QList<board::Tab> tabs = m_model.tabs();
    if (tabs.isEmpty()) {
        m_tab = tabId;   // restored before the first `board` event; used once the tabs exist
        return;
    }
    for (int i = 0; i < tabs.size(); ++i) {
        if (tabs.at(i).id != tabId)
            continue;
        m_tab = tabId;
        if (m_tabs->currentIndex() != i)
            m_tabs->setCurrentIndex(i);
        else
            rebuild();
        return;
    }
}

// Scroll the columns so this one is on screen. Deferred: right after the columns come back from
// behind an open card their geometry is stale, and revealing then scrolls somewhere arbitrary.
void BoardView::revealColumn(QWidget *column)
{
    QPointer<QWidget> guard(column);
    QTimer::singleShot(0, this, [this, guard] {
        if (guard && guard->isVisible())
            m_scroll->ensureWidgetVisible(guard, 24, 0);
    });
}

void BoardView::setHeaderRightInset(int pixels)
{
    m_tabRow->setContentsMargins(0, 0, pixels, 0);
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
    // From the size hint, not the geometry: on a resize the layout has not placed m_keys yet.
    const int bottom = height() - (m_keys->isVisible() ? m_keys->sizeHint().height() : 0) - 10;
    m_notice->move((this->width() - m_notice->width()) / 2, bottom - m_notice->height());
    m_notice->raise();
}

// ------------------------------------------------------------------------- events

void BoardView::handleEvent(const QJsonObject &event)
{
    const QString type = event.value(QStringLiteral("event")).toString();
    const QString requestId = event.value(QStringLiteral("id")).toString();
    const bool mine = !requestId.isEmpty() && requestId.startsWith(m_requestPrefix);
    if (type == QStringLiteral("board")) {
        m_open = true;
        m_model.setConfig(event.value(QStringLiteral("config")).toObject());
        m_model.reset(event.value(QStringLiteral("cards")).toArray());
        if ((m_tab.isEmpty() || !m_model.tab(m_tab)) && !m_model.tabs().isEmpty())
            m_tab = m_model.tabs().first().id;
        m_builtTab.clear();   // the columns may have changed with the config
        const bool hadFocus = hasFocus();   // opened with Ctrl+Shift+S before the cards arrived
        rebuildTabs();
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
        const QList<board::Column> columns = m_model.columnsFor(m_tab);
        for (const board::Column &column : columns)
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
        if (m_replyOnOpen) {
            m_replyOnOpen = false;
            m_detail->focusReply();
        } else if (m_scroll->isHidden() && !m_detail->isAncestorOf(QApplication::focusWidget())) {
            m_detail->focusDocument();   // the columns it came from are hidden in a narrow pane
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
        if (!m_askCard.isEmpty()) {
            // A question the agent could not take (no provider key, say): the question itself is
            // already in the thread, so say that under it rather than over the board.
            const bool here = m_askCard == m_detail->cardId();
            m_detail->setBusy(false);
            m_askCard.clear();
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

void BoardView::rebuildTabs()
{
    // The caller rebuilds the columns once afterwards; currentChanged must not do it again.
    m_buildingTabs = true;
    while (m_tabs->count() > 0)
        m_tabs->removeTab(0);
    const QList<board::Tab> tabs = m_model.tabs();
    for (int i = 0; i < tabs.size(); ++i) {
        m_tabs->addTab(tabs.at(i).title);
        if (tabs.at(i).id == m_tab)
            m_tabs->setCurrentIndex(i);
    }
    m_buildingTabs = false;
    updateTabCounts();
}

void BoardView::updateTabCounts()
{
    const QList<board::Tab> tabs = m_model.tabs();
    for (int i = 0; i < tabs.size() && i < m_tabs->count(); ++i) {
        const int count = m_model.count(tabs.at(i).id);
        m_tabs->setTabText(i, count > 0 ? QStringLiteral("%1  %2").arg(tabs.at(i).title).arg(count)
                                        : tabs.at(i).title);
    }
}

QStringList BoardView::columnIds() const
{
    QStringList out;
    const QList<board::Column> columns = m_model.columnsFor(m_tab);
    for (const board::Column &column : columns)
        out << column.id;
    return out;
}

QListWidget *BoardView::listFor(const QString &columnId) const
{
    return m_lists.value(columnId);
}

QListWidget *BoardView::listOfCard(const QString &id) const
{
    if (id.isEmpty())
        return nullptr;
    for (auto it = m_lists.begin(); it != m_lists.end(); ++it)
        for (int i = 0; i < it.value()->count(); ++i)
            if (it.value()->item(i)->data(kCardRole).toString() == id)
                return it.value();
    return nullptr;
}

QWidget *BoardView::buildColumn(const board::Column &column)
{
    auto *frame = new QFrame(m_columns);
    frame->setObjectName(QStringLiteral("boardColumnFrame"));
    frame->setMinimumWidth(236);
    frame->setMaximumWidth(320);
    frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *layout = new QVBoxLayout(frame);
    // Less on the right: the list keeps its scrollbar's width free there (CardDelegate::cardWidth).
    layout->setContentsMargins(8, 6, 2, 4);
    layout->setSpacing(4);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(2, 0, 6, 2);
    header->setSpacing(6);
    auto *title = new QLabel(column.title.toUpper(), frame);
    title->setObjectName(QStringLiteral("boardColumnHeader"));
    header->addWidget(title);
    auto *count = new QLabel(frame);
    count->setObjectName(QStringLiteral("boardColumnCount"));
    header->addWidget(count);
    header->addStretch();
    auto *add = new QToolButton(frame);
    add->setObjectName(QStringLiteral("boardColumnAdd"));
    add->setText(QStringLiteral("+"));
    add->setToolTip(QStringLiteral("New card in %1").arg(column.title));
    add->setCursor(Qt::PointingHandCursor);
    add->setFocusPolicy(Qt::NoFocus);
    // Nothing is created straight into Done: a card gets there by being closed.
    const QString landing = m_model.dropStatus(m_tab, column.id);
    QSizePolicy keepRoom = add->sizePolicy();
    keepRoom.setRetainSizeWhenHidden(true);   // every header the same height, button or not
    add->setSizePolicy(keepRoom);
    add->setVisible(landing != QStringLiteral("done") && landing != QStringLiteral("dropped"));
    header->addWidget(add);
    layout->addLayout(header);
    connect(add, &QToolButton::clicked, this, [this, id = column.id] {
        if (onHint)
            onHint(QStringLiteral("board.quickAdd"), QStringLiteral("n"));
        quickAddIn(id);
    });

    auto *list = new CardList(column.id, frame);
    auto *delegate = new CardDelegate(&m_model, list);
    delegate->setStatusBadges(column.statuses.size() > 1,
                              column.id == QStringLiteral("done") ? QStringLiteral("done") : QString());
    list->setItemDelegate(delegate);
    list->onDropped = [this, id = column.id](const QString &card, const QString &before,
                                             const QString &after) {
        if (onHint)
            onHint(QStringLiteral("board.drag"), QStringLiteral("m"));
        m_selected = card;
        moveCard(card, id, before, after);
    };
    list->onDragging = [this](bool on) {
        m_dragActive = on;
        if (on) {
            m_dragScroll->start();
            return;
        }
        m_dragScroll->stop();
        if (m_rebuildPending) {
            // Not from inside startDrag(): its list may be the one a rebuild replaces.
            QTimer::singleShot(0, this, [this] {
                if (m_rebuildPending && !m_dragActive)
                    rebuild();
            });
        }
    };
    connect(list, &QListWidget::itemSelectionChanged, this, [this, list] {
        auto *item = list->currentItem();
        if (!item || !item->isSelected())
            return;
        m_selected = item->data(kCardRole).toString();
        // Only one card is selected on the whole board.
        for (QListWidget *other : std::as_const(m_lists))
            if (other != list && other->currentItem()) {
                const QSignalBlocker block(other);
                other->clearSelection();
                other->setCurrentItem(nullptr);
                other->viewport()->update();
            }
        if (detailOpen())
            m_follow->start();
    });
    // A click opens the card, the way a card board does; a drag never counts as a click.
    connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (QApplication::keyboardModifiers() != Qt::NoModifier)
            return;
        m_selected = item->data(kCardRole).toString();
        openSelected();
    });
    connect(list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        m_selected = item->data(kCardRole).toString();
        openSelected();
    });
    list->installEventFilter(this);
    layout->addWidget(list, 1);
    m_lists.insert(column.id, list);
    m_counts.insert(column.id, count);
    fillList(list, column);
    return frame;
}

void BoardView::fillList(QListWidget *list, const board::Column &column)
{
    const QList<board::Card> cards = m_model.cards(m_tab, column.id);
    if (auto *count = m_counts.value(column.id))
        count->setText(QString::number(cards.size()));
    auto *cardList = static_cast<CardList *>(list);
    const board::Tab *tab = m_model.tab(m_tab);
    const bool filtered = !m_model.filter().trimmed().isEmpty();
    const bool firstColumn = columnIds().value(0) == column.id;
    if (filtered)
        cardList->placeholder = QStringLiteral("No matches");
    else if (column.id == QStringLiteral("done") && tab && !tab->isFilter())
        cardList->placeholder = QStringLiteral("Drop a card here to close it");
    else if (m_model.count(m_tab) == 0)
        // A tab with nothing in it says so once, not "No cards" in every column.
        cardList->placeholder = firstColumn && tab
                                    ? QStringLiteral("Nothing in %1 yet.\nPress n to add a card.").arg(tab->title)
                                    : QString();
    else
        cardList->placeholder = QStringLiteral("No cards");

    // Refilled in place: the scroll position and the selection survive, and nothing flashes.
    const int scroll = list->verticalScrollBar()->value();
    const QSignalBlocker block(list);
    list->setUpdatesEnabled(false);
    list->clear();
    for (const board::Card &card : cards) {
        auto *item = new QListWidgetItem(list);
        item->setData(kCardRole, card.id);
        item->setToolTip(QStringLiteral("%1 · %2\n%3").arg(card.reference(), card.title, card.path));
        if (card.id == m_selected) {
            list->setCurrentItem(item);
            item->setSelected(true);
        }
    }
    list->doItemsLayout();
    list->verticalScrollBar()->setValue(scroll);
    list->setUpdatesEnabled(true);
    list->viewport()->update();
}

void BoardView::refill()
{
    QWidget *focus = QApplication::focusWidget();
    bool listHadFocus = false;
    for (QListWidget *list : std::as_const(m_lists))
        listHadFocus = listHadFocus || list == focus;
    const QList<board::Column> columns = m_model.columnsFor(m_tab);
    for (const board::Column &column : columns)
        if (auto *list = listFor(column.id))
            fillList(list, column);
    // A card moved with the keyboard keeps the focus: it follows the card to its new column.
    if (listHadFocus) {
        if (auto *list = listOfCard(m_selected)) {
            list->setFocus();
            if (list->currentItem())
                list->scrollToItem(list->currentItem());
            revealColumn(list->parentWidget());
        }
    }
}

void BoardView::rebuild()
{
    if (m_dragActive) {
        m_rebuildPending = true;
        return;
    }
    m_rebuildPending = false;
    const QList<board::Column> columns = m_model.columnsFor(m_tab);
    QStringList ids;
    for (const board::Column &column : columns)
        ids << column.id;
    if (!m_lists.isEmpty() && m_tab == m_builtTab && ids == m_builtColumns) {
        refill();
    } else {
        m_lists.clear();
        m_counts.clear();
        m_quickAdd.clear();
        if (auto *old = m_columns->layout()) {
            QLayoutItem *item = nullptr;
            while ((item = old->takeAt(0)) != nullptr) {
                if (item->widget()) {
                    item->widget()->hide();
                    item->widget()->deleteLater();
                }
                delete item;
            }
        }
        auto *layout = qobject_cast<QHBoxLayout *>(m_columns->layout());
        if (!layout)
            return;
        for (const board::Column &column : columns)
            layout->addWidget(buildColumn(column));
        layout->addStretch();
        m_builtTab = m_tab;
        m_builtColumns = ids;
        m_scroll->horizontalScrollBar()->setValue(0);
    }
    updateTabCounts();

    const bool empty = m_model.total() == 0;
    if (!m_open)
        m_empty->setText(QStringLiteral("Loading the Switchboard…"));
    else
        m_empty->setText(QStringLiteral("No cards yet.\n\nPress n or click “+ New card” to add the first one."));
    m_empty->setVisible(!m_open || empty);
    m_splitter->setVisible(m_open && !empty);
    m_keys->setVisible(m_open && !empty);
}

void BoardView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateDetailLayout();
    placeNotice();
}

// Wide enough, the open card sits beside the columns; in a narrow pane it takes the whole pane
// (the columns come back when it closes) instead of squeezing the board to a sliver.
void BoardView::updateDetailLayout()
{
    // The key line says what the keys do in what is on screen: the board, or a card that has
    // the pane to itself.
    static const QString boardKeys = QStringLiteral(
        "<b>Enter</b> open &nbsp; <b>n</b> new &nbsp; <b>m</b> move &nbsp; <b>Alt+Shift+Arrows</b> shift "
        "&nbsp; <b>/</b> filter &nbsp; <b>t</b> #ID to prompt &nbsp; <b>y</b> copy &nbsp; <b>o</b> file "
        "&nbsp; <b>Ctrl+Z</b> undo");
    static const QString cardKeys = QStringLiteral(
        "<b>Esc</b> back to the board &nbsp; <b>Tab</b> reply &nbsp; <b>Enter</b> asks the agent &nbsp; "
        "<b>Ctrl+Shift+Enter</b> comments only &nbsp; <b>Shift+Enter</b> new line");
    const bool stacked = width() < kStackedWidth;
    const QString keys = detailOpen() && stacked ? cardKeys : boardKeys;
    if (m_keys->text() != keys)
        m_keys->setText(keys);
    if (!detailOpen()) {
        m_scroll->setVisible(true);
        return;
    }
    const bool wasStacked = m_scroll->isHidden();
    m_scroll->setVisible(!stacked);
    if (stacked)
        return;
    if (!m_detailSized || wasStacked) {
        const int total = qMax(1, m_splitter->width());
        const int detail = qBound(360, total * 45 / 100, total - 260);
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
    const QString requestId = nextRequestId();
    QJsonObject message{{QStringLiteral("type"), QStringLiteral("board_move")},
                        {QStringLiteral("id"), requestId},
                        {QStringLiteral("card"), id},
                        {QStringLiteral("reason"), QStringLiteral("moved in the Switchboard")}};
    const QString status = m_model.dropStatus(m_tab, columnId);
    const board::Tab *tab = m_model.tab(m_tab);
    const board::Card *card = m_model.card(id);
    const bool sameColumn = card && m_model.columnOf(m_tab, *card) == columnId;
    QString note;
    if (tab && tab->isFilter() && !tab->filter.contains(QStringLiteral("done"))) {
        message.insert(QStringLiteral("tab"), columnId);   // Deferred groups by category
        note = QStringLiteral("Moved #%1 to %2").arg(id, board::tabTitle(columnId));
    } else if (!status.isEmpty() && !sameColumn) {
        // Within its own column a card keeps its exact status (Needs QA stays LLM or human).
        message.insert(QStringLiteral("status"), status);
        note = QStringLiteral("Moved #%1 to %2").arg(id, board::statusTitle(columnId));
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
    const QStringList columns = columnIds();
    if (columns.isEmpty())
        return;
    QString columnId = columns.first();
    for (const QString &id : columns)
        if (auto *list = listFor(id); list && list->hasFocus())
            columnId = id;
    quickAddIn(columnId);
}

// A field at the top of the column. Enter adds the card and keeps the field open for the next
// one (a burst of ideas is the common case); Esc, or leaving it empty, closes it.
void BoardView::quickAddIn(const QString &columnId)
{
    if (m_model.total() == 0 && !m_open)
        return;
    if (m_model.total() == 0) {
        // The empty board hides the columns; show them so the field has somewhere to go.
        m_empty->hide();
        m_splitter->show();
    }
    auto *list = listFor(columnId);
    if (!list)
        return;
    if (m_quickAdd) {
        if (m_quickAddColumn == columnId) {
            m_quickAdd->setFocus();
            return;
        }
        m_quickAdd->parentWidget()->deleteLater();
    }
    // In a row of its own with the list's scrollbar room on the right, so it lines up with the
    // cards under it. The row goes when the field does.
    auto *row = new QWidget(list->parentWidget());
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, list->verticalScrollBar()->sizeHint().width() + 2, 0);
    auto *field = new QLineEdit(row);
    rowLayout->addWidget(field);
    field->setObjectName(QStringLiteral("boardQuickAdd"));
    const board::Column *column = nullptr;
    const QList<board::Column> columns = m_model.columnsFor(m_tab);
    for (const board::Column &candidate : columns)
        if (candidate.id == columnId)
            column = &candidate;
    field->setPlaceholderText(QStringLiteral("New card in %1 — Enter adds, Esc closes")
                                  .arg(column ? column->title : QStringLiteral("this column")));
    field->setToolTip(QStringLiteral("The text is kept verbatim as the card's request"));
    if (auto *layout = qobject_cast<QVBoxLayout *>(list->parentWidget()->layout()))
        layout->insertWidget(1, row);
    revealColumn(list->parentWidget());
    field->setFocus();
    field->installEventFilter(this);
    m_quickAdd = field;
    m_quickAddColumn = columnId;
    const QString status = m_model.dropStatus(m_tab, columnId);
    const board::Tab *tab = m_model.tab(m_tab);
    const QString tabId = tab && !tab->folder.isEmpty() ? tab->id : QStringLiteral("features");
    const QString cardType = tab ? tab->type : QStringLiteral("work");
    connect(field, &QLineEdit::returnPressed, this, [this, field, status, tabId, cardType] {
        const QString text = field->text().trimmed();
        if (text.isEmpty()) {
            field->parentWidget()->deleteLater();
            focusInput();
            return;
        }
        send({{QStringLiteral("type"), QStringLiteral("board_create")},
              {QStringLiteral("tab"), tabId},
              {QStringLiteral("status"), status.isEmpty() ? QStringLiteral("inbox") : status},
              {QStringLiteral("card_type"), cardType},
              {QStringLiteral("text"), text}});
        field->clear();
    });
    connect(field, &QLineEdit::editingFinished, this, [field] {
        if (field->text().trimmed().isEmpty() && !field->hasFocus())
            field->parentWidget()->deleteLater();
    });
}

void BoardView::openSelected()
{
    if (m_selected.isEmpty())
        return;
    if (detailOpen() && m_detail->cardId() == m_selected) {
        updateDetailLayout();
        return;
    }
    send({{QStringLiteral("type"), QStringLiteral("board_card_get")},
          {QStringLiteral("card"), m_selected}});
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
    m_filter->setFocus();
    m_filter->selectAll();
}

void BoardView::selectCard(const QString &id)
{
    m_selected = id;
    if (auto *list = listOfCard(id)) {
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->data(kCardRole).toString() == id) {
                list->setCurrentItem(list->item(i));
                list->item(i)->setSelected(true);
            }
        list->setFocus();
        revealColumn(list->parentWidget());
    }
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
    const QList<board::Column> columns = m_model.columnsFor(m_tab);
    const board::Card *card = m_model.card(m_selected);
    const QString here = card ? m_model.columnOf(m_tab, *card) : QString();
    int index = 1;
    for (const board::Column &column : columns) {
        QAction *action = menu.addAction(QStringLiteral("&%1  %2").arg(index++).arg(column.title));
        action->setEnabled(column.id != here);
        const QString id = column.id;
        connect(action, &QAction::triggered, this, [this, id] { moveCard(m_selected, id, {}, {}); });
    }
    menu.addSeparator();
    for (const board::Tab &tab : m_model.tabs()) {
        if (tab.folder.isEmpty() || tab.id == m_tab)
            continue;
        QAction *action = menu.addAction(QStringLiteral("Move to %1").arg(tab.title));
        const QString id = tab.id;
        connect(action, &QAction::triggered, this, [this, id] { moveToTab(m_selected, id); });
    }
    // Under the selected card rather than wherever the mouse happens to be.
    QPoint at = QCursor::pos();
    if (auto *list = listOfCard(m_selected); list && list->currentItem())
        at = list->viewport()->mapToGlobal(list->visualItemRect(list->currentItem()).bottomLeft());
    menu.exec(at);
}

void BoardView::focusInput()
{
    if (listOfCard(m_selected)) {
        selectCard(m_selected);
        return;
    }
    const QStringList columns = columnIds();
    for (const QString &id : columns) {
        if (auto *list = listFor(id); list && list->count() > 0) {
            list->setFocus();
            list->setCurrentRow(0);
            list->item(0)->setSelected(true);
            m_selected = list->item(0)->data(kCardRole).toString();
            return;
        }
    }
    // No card to stand on (an empty board or a filter with no match): the view itself takes the
    // keys, so `n` still adds and `/` still filters.
    setFocus();
}

void BoardView::step(int columns, int rows)
{
    const QStringList ids = columnIds();
    int at = -1;
    for (int i = 0; i < ids.size(); ++i)
        if (auto *list = listFor(ids.at(i)); list && list->hasFocus())
            at = i;
    if (at < 0)
        return;
    if (columns != 0) {
        const QListWidget *from = listFor(ids.at(at));
        // Stay at the same height on screen, not the same index: columns hold different cards.
        const int y = from && from->currentItem() ? from->visualItemRect(from->currentItem()).center().y()
                                                  : 0;
        for (int i = at + columns; i >= 0 && i < ids.size(); i += columns) {
            auto *list = listFor(ids.at(i));
            if (!list || list->count() == 0)
                continue;
            QListWidgetItem *item = list->itemAt(QPoint(10, y));
            if (!item)
                item = y > 0 ? list->item(list->count() - 1) : list->item(0);
            list->setFocus();
            list->setCurrentItem(item);
            item->setSelected(true);
            revealColumn(list->parentWidget());
            return;
        }
        return;
    }
    auto *list = listFor(ids.at(at));
    if (list && list->count() > 0)
        list->setCurrentRow(qBound(0, list->currentRow() + rows, list->count() - 1));
}

// Alt+Shift+Up/Down: one place up or down inside the column.
void BoardView::reorder(QListWidget *list, int delta)
{
    auto *cards = static_cast<CardList *>(list);
    const QStringList order = cards->ids();
    const int from = order.indexOf(m_selected);
    const int slot = from + delta;
    if (from < 0 || slot < 0 || slot >= order.size())
        return;
    const auto [before, after] = board::placement(order, m_selected, slot);
    moveCard(m_selected, cards->columnId(), before, after);
}

// Keys that work anywhere in the pane that is not a text field: the list, the view itself.
bool BoardView::handleBoardKey(QKeyEvent *key)
{
    const auto mods = key->modifiers() & ~Qt::KeypadModifier;
    if (mods == Qt::ControlModifier
        && (key->key() == Qt::Key_PageUp || key->key() == Qt::Key_PageDown)) {
        const int step = key->key() == Qt::Key_PageDown ? 1 : -1;
        const int next = (m_tabs->currentIndex() + step + m_tabs->count()) % qMax(1, m_tabs->count());
        m_tabs->setCurrentIndex(next);
        return true;
    }
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

// While a card is dragged near the left or right edge of the columns, or the top or bottom of a
// column, scroll that way: otherwise a column off screen can never be dropped on.
void BoardView::autoScrollDuringDrag()
{
    const QPoint global = QCursor::pos();
    QWidget *viewport = m_scroll->viewport();
    const QPoint local = viewport->mapFromGlobal(global);
    if (local.y() >= 0 && local.y() < viewport->height()) {
        QScrollBar *bar = m_scroll->horizontalScrollBar();
        if (local.x() >= 0 && local.x() < 48)
            bar->setValue(bar->value() - 14);
        else if (local.x() > viewport->width() - 48 && local.x() <= viewport->width())
            bar->setValue(bar->value() + 14);
    }
    for (QListWidget *list : std::as_const(m_lists)) {
        const QPoint at = list->viewport()->mapFromGlobal(global);
        if (!list->viewport()->rect().contains(at))
            continue;
        QScrollBar *bar = list->verticalScrollBar();
        if (at.y() < 32)
            bar->setValue(bar->value() - 12);
        else if (at.y() > list->viewport()->height() - 32)
            bar->setValue(bar->value() + 12);
    }
}

bool BoardView::eventFilter(QObject *object, QEvent *event)
{
    // A card dropped on a tab moves to that category (design 4.2).
    if (object == m_tabs) {
        const QEvent::Type type = event->type();
        if (type == QEvent::DragEnter || type == QEvent::DragMove || type == QEvent::Drop) {
            auto *drop = static_cast<QDropEvent *>(event);
#if QT_VERSION_MAJOR >= 6
            const int index = m_tabs->tabAt(drop->position().toPoint());
#else
            const int index = m_tabs->tabAt(drop->pos());
#endif
            const QList<board::Tab> tabs = m_model.tabs();
            const board::Card *card = m_model.card(CardList::dragging);
            const bool target = card && index >= 0 && index < tabs.size()
                                && !tabs.at(index).folder.isEmpty() && tabs.at(index).id != card->tab
                                && tabs.at(index).type == card->type;
            if (type == QEvent::DragEnter) {
                // Accept the enter even off a tab, so moves across the bar keep arriving.
                if (card) drop->acceptProposedAction(); else drop->ignore();
                return true;
            }
            if (!target) {
                drop->ignore();
                return true;
            }
            drop->acceptProposedAction();
            if (type == QEvent::DragMove) {
                showNotice(QStringLiteral("Drop to move %1 to %2").arg(card->reference(),
                                                                       tabs.at(index).title), false);
            } else {
                const QString id = card->id;
                drop->setDropAction(Qt::IgnoreAction);
                moveToTab(id, tabs.at(index).id);
            }
            return true;
        }
        return QWidget::eventFilter(object, event);
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
            m_quickAdd->parentWidget()->deleteLater();
            focusInput();
            return true;
        }
        return QWidget::eventFilter(object, event);
    }
    auto *list = qobject_cast<QListWidget *>(object);
    if (!list)
        return QWidget::eventFilter(object, event);

    const auto mods = key->modifiers() & ~Qt::KeypadModifier;
    if (mods == (Qt::AltModifier | Qt::ShiftModifier)) {
        const QStringList ids = columnIds();
        const int at = ids.indexOf(columnIdOf(list));
        if (key->key() == Qt::Key_Left && at > 0) {
            moveCard(m_selected, ids.at(at - 1), {}, {});
            return true;
        }
        if (key->key() == Qt::Key_Right && at >= 0 && at + 1 < ids.size()) {
            moveCard(m_selected, ids.at(at + 1), {}, {});
            return true;
        }
        if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) {
            reorder(list, key->key() == Qt::Key_Up ? -1 : 1);
            return true;
        }
    }
    if (handleBoardKey(key))
        return true;
    if (mods == Qt::NoModifier) {
        switch (key->key()) {
        case Qt::Key_Left:
            step(-1, 0);
            return true;
        case Qt::Key_Right:
            step(1, 0);
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

}  // namespace relay
