// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The window's own title bar, since Relay draws one instead of taking the desktop's: ChromeButton
// paints the header glyphs (bell, gear, minimize, maximize, close, and the tab row's own buttons)
// so they all sit at one stroke weight, and NotificationsPopup is the list behind the bell.

#include "Theme.h"
#include "Notifications.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QToolButton>
#include <QScrollArea>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QPainter>
#include <QScreen>

#include <algorithm>
#include <functional>
#include <cmath>

// ----- window header (Relay draws its own title bar) ------------------------------------------
// The window has no OS title bar: the tab row is the title bar, with the Relay icon on the left
// and the bell, the actions button and minimize/maximize/close on the right. Dragging the empty
// part of the row moves the window, a double-click maximizes it, and a few pixels of padding
// around the window stay grabbable for resizing (see RelayWindow::edgesAt).
//
// "window/native_frame" (Actions › System title bar) gives the system decorations back for
// desktops where they work better; it applies to windows opened after the change.

// A header button. The glyphs are painted rather than typed: a text bell or gear lands in
// whatever font the desktop happens to have (often a colour emoji), and these have to sit at
// the same weight as the tab labels beside them.
class ChromeButton final : public QToolButton {
public:
    // Plus and TabClose are the tab row's own buttons; they are drawn here so the whole header
    // shares one stroke weight instead of mixing painted glyphs with the icon theme's bitmaps.
    enum class Glyph { Bell, Gear, Minimize, Maximize, Restore, Close, Plus, TabClose };

    explicit ChromeButton(Glyph glyph, QWidget *parent = nullptr, int size = kSize)
        : QToolButton(parent), m_glyph(glyph) {
        setObjectName(glyph == Glyph::Close ? QStringLiteral("windowCloseButton")
                    : glyph == Glyph::Plus ? QStringLiteral("newTabButton")
                    : glyph == Glyph::TabClose ? QStringLiteral("tabCloseButton")
                                               : QStringLiteral("windowChromeButton"));
        setAutoRaise(true);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::ArrowCursor);
        setFixedSize(size, size);
    }

    void setGlyph(Glyph glyph) { if (m_glyph == glyph) return; m_glyph = glyph; update(); }
    // Unseen notifications, drawn as a dot on the bell. 0 hides it.
    void setBadge(int count) { if (m_badge == count) return; m_badge = count; update(); }

    static constexpr int kSize = 26;

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool hovered = underMouse() && isEnabled();
        const bool closing = m_glyph == Glyph::Close;
        if (hovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(closing ? relay::theme::Error.darker(130) : relay::theme::SurfaceRaised);
            const qreal radius = std::min(width(), height()) * 5.0 / kSize;
            painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), radius, radius);
        }
        // A dimmed button keeps its space in the layout but shows nothing, so a row of tabs is
        // not a row of crosses; pointing at the tab (or selecting it) brings the cross back.
        if (m_dim && !hovered) return;
        QColor ink = hovered ? (closing ? QColor(Qt::white) : relay::theme::Text) : relay::theme::TextMuted;
        if (!isEnabled()) ink = relay::theme::TextMuted.darker(150);
        // Every glyph is drawn for a 26 px button and scaled from there, so a smaller button
        // (the tab's close cross) keeps the same proportions and the same apparent weight.
        const qreal unit = std::min(width(), height()) / qreal(kSize);
        painter.setPen(QPen(ink, 1.3 * std::max(0.85, unit), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        const QPointF centre(width() / 2.0, height() / 2.0);
        switch (m_glyph) {
        case Glyph::Bell: paintBell(painter, centre, unit); break;
        case Glyph::Gear: paintGear(painter, centre, unit); break;
        case Glyph::Minimize: painter.drawLine(centre + QPointF(-5, 3) * unit, centre + QPointF(5, 3) * unit); break;
        case Glyph::Maximize: painter.drawRect(QRectF(centre + QPointF(-4.5, -4.5) * unit, QSizeF(9 * unit, 9 * unit))); break;
        case Glyph::Restore:
            painter.drawRect(QRectF(centre + QPointF(-5.5, -2.5) * unit, QSizeF(8 * unit, 8 * unit)));
            painter.drawPolyline(QPolygonF({centre + QPointF(-2.5, -5.5) * unit, centre + QPointF(5.5, -5.5) * unit,
                                            centre + QPointF(5.5, 2.5) * unit}));
            break;
        case Glyph::Plus:
            painter.drawLine(centre + QPointF(-4.5, 0) * unit, centre + QPointF(4.5, 0) * unit);
            painter.drawLine(centre + QPointF(0, -4.5) * unit, centre + QPointF(0, 4.5) * unit);
            break;
        case Glyph::Close:
        case Glyph::TabClose: {
            const qreal arm = (m_glyph == Glyph::Close ? 4.5 : 3.6) * unit;
            painter.drawLine(centre + QPointF(-arm, -arm), centre + QPointF(arm, arm));
            painter.drawLine(centre + QPointF(arm, -arm), centre + QPointF(-arm, arm));
            break;
        }
        }
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent *event) override
#else
    void enterEvent(QEvent *event) override
#endif
    { QToolButton::enterEvent(event); update(); }
    void leaveEvent(QEvent *event) override { QToolButton::leaveEvent(event); update(); }

private:
    void paintBell(QPainter &painter, const QPointF &centre, qreal unit) const {
        painter.save();
        painter.translate(centre);
        painter.scale(unit, unit);
        painter.translate(-centre);
        const qreal x = centre.x(), y = centre.y();
        QPainterPath bell;
        bell.moveTo(x - 5.5, y + 2.5);
        bell.cubicTo(x - 4.2, y + 1.5, x - 4.0, y - 0.5, x - 4.0, y - 1.5);
        bell.cubicTo(x - 4.0, y - 4.6, x - 2.2, y - 6.0, x, y - 6.0);
        bell.cubicTo(x + 2.2, y - 6.0, x + 4.0, y - 4.6, x + 4.0, y - 1.5);
        bell.cubicTo(x + 4.0, y - 0.5, x + 4.2, y + 1.5, x + 5.5, y + 2.5);
        bell.closeSubpath();
        painter.drawPath(bell);
        painter.drawArc(QRectF(x - 2, y + 2.6, 4, 3.4), 200 * 16, 140 * 16);   // clapper
        painter.restore();
        if (m_badge <= 0) return;
        // Unread dot, top-right, over the bell's shoulder.
        painter.setPen(Qt::NoPen);
        painter.setBrush(relay::theme::Accent);
        const QRectF dot(width() - 11.0, 3.0, 8.0, 8.0);
        painter.drawEllipse(dot);
        if (m_badge > 1) {
            QFont small = font();
            small.setPixelSize(7);
            small.setBold(true);
            painter.setFont(small);
            painter.setPen(relay::theme::AccentText);
            painter.drawText(dot, Qt::AlignCenter, m_badge > 9 ? QStringLiteral("9+") : QString::number(m_badge));
        }
    }

    // A cog drawn as one outline: six teeth around a ring, rather than a circle with spokes
    // poking through it. At 26 px the spokes read as noise; the solid outline does not.
    void paintGear(QPainter &painter, const QPointF &centre, qreal unit) const {
        constexpr int teeth = 6;
        const qreal inner = 4.2 * unit, outer = 6.0 * unit;
        const qreal half = M_PI / teeth;          // half of one tooth-and-gap period
        QPolygonF cog;
        for (int i = 0; i < teeth; ++i) {
            const qreal base = i * 2 * half;
            const qreal angle[4] = {base - half * 0.60, base - half * 0.34, base + half * 0.34, base + half * 0.60};
            const qreal radius[4] = {inner, outer, outer, inner};
            for (int k = 0; k < 4; ++k) cog << centre + QPointF(std::cos(angle[k]), std::sin(angle[k])) * radius[k];
        }
        painter.drawPolygon(cog);
        painter.drawEllipse(centre, 2.1 * unit, 2.1 * unit);
    }

public:
    // The tab's cross stays hidden until the tab is hovered or current.
    void setDim(bool dim) { if (m_dim == dim) return; m_dim = dim; update(); }

private:
    Glyph m_glyph;
    int m_badge = 0;
    bool m_dim = false;
};

// The list behind the bell: newest first, one row per notification, click to go back to the pane
// that posted it. Opening it marks everything as seen (that is what the badge counts).
class NotificationsPopup final : public QFrame {
public:
    explicit NotificationsPopup(QWidget *parent) : QFrame(parent, Qt::Popup) {
        setObjectName(QStringLiteral("notificationsPopup"));
        setAttribute(Qt::WA_StyledBackground);
        setFixedWidth(380);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(6);
        auto *head = new QHBoxLayout;
        head->setContentsMargins(2, 0, 2, 0);
        auto *title = new QLabel(QStringLiteral("NOTIFICATIONS"));
        title->setObjectName(QStringLiteral("paletteTitle"));
        head->addWidget(title);
        head->addStretch(1);
        m_clear = new QToolButton;
        m_clear->setObjectName(QStringLiteral("popupTextButton"));
        m_clear->setText(QStringLiteral("Clear all"));
        m_clear->setFocusPolicy(Qt::NoFocus);
        connect(m_clear, &QToolButton::clicked, this, [] { relay::NotificationCenter::instance().clear(); });
        head->addWidget(m_clear);
        layout->addLayout(head);

        m_scroll = new QScrollArea;
        m_scroll->setObjectName(QStringLiteral("notificationsScroll"));
        m_scroll->setWidgetResizable(true);
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_rows = new QWidget;
        m_rowsLayout = new QVBoxLayout(m_rows);
        m_rowsLayout->setContentsMargins(0, 0, 0, 0);
        m_rowsLayout->setSpacing(4);
        m_rowsLayout->addStretch(1);
        m_scroll->setWidget(m_rows);
        layout->addWidget(m_scroll, 1);

        m_empty = new QLabel(QStringLiteral("Nothing yet.\nFinished agent turns, long commands, failures and\nprompts waiting for you show up here."));
        m_empty->setObjectName(QStringLiteral("muted"));
        m_empty->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_empty);

        connect(&relay::NotificationCenter::instance(), &relay::NotificationCenter::changed, this, [this] { if (isVisible()) rebuild(); });
    }

    // Called with the pane token of the clicked notification, when it has one.
    std::function<void(const QString &)> onOpenSource;

    void popUpUnder(QWidget *anchor) {
        rebuild();
        const QPoint below = anchor->mapToGlobal(QPoint(anchor->width(), anchor->height() + 6));
        QRect screen = anchor->screen() ? anchor->screen()->availableGeometry() : QRect(0, 0, 1280, 800);
        int x = std::max(screen.left() + 8, below.x() - width());
        x = std::min(x, screen.right() - width() - 8);
        move(x, std::min(below.y(), screen.bottom() - height() - 8));
        show();
        relay::NotificationCenter::instance().markAllSeen();
    }

private:
    void rebuild() {
        for (QWidget *row : std::as_const(m_widgets)) row->deleteLater();
        m_widgets.clear();
        const auto entries = relay::NotificationCenter::instance().entries();
        m_empty->setVisible(entries.isEmpty());
        m_scroll->setVisible(!entries.isEmpty());
        m_clear->setEnabled(!entries.isEmpty());
        for (const relay::Notification &note : entries) m_widgets.append(addRow(note));
        const int rows = std::min(6, int(entries.size()));
        m_scroll->setFixedHeight(entries.isEmpty() ? 0 : std::max(64, rows * 56));
        adjustSize();
    }

    QWidget *addRow(const relay::Notification &note) {
        auto *row = new QFrame;
        row->setObjectName(QStringLiteral("notificationRow"));
        row->setAttribute(Qt::WA_StyledBackground);
        row->setProperty("kind", note.kind);
        row->setCursor(note.source.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(8, 6, 6, 6);
        layout->setSpacing(8);
        auto *dot = new QLabel(QStringLiteral("●"));
        dot->setObjectName(QStringLiteral("notificationDot"));
        dot->setProperty("kind", note.kind);
        dot->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        layout->addWidget(dot);
        auto *text = new QVBoxLayout;
        text->setContentsMargins(0, 0, 0, 0);
        text->setSpacing(1);
        auto *titleRow = new QHBoxLayout;
        titleRow->setContentsMargins(0, 0, 0, 0);
        auto *title = new QLabel(note.title);
        title->setObjectName(QStringLiteral("notificationTitle"));
        titleRow->addWidget(title, 1);
        auto *when = new QLabel(relay::NotificationCenter::relativeTime(note.at));
        when->setObjectName(QStringLiteral("notificationTime"));
        titleRow->addWidget(when);
        text->addLayout(titleRow);
        if (!note.body.isEmpty()) {
            auto *body = new QLabel(note.body.left(240));
            body->setObjectName(QStringLiteral("notificationBody"));
            body->setWordWrap(true);
            text->addWidget(body);
        }
        layout->addLayout(text, 1);
        auto *dismiss = new QToolButton;
        dismiss->setObjectName(QStringLiteral("notificationDismiss"));
        dismiss->setText(QStringLiteral("✕"));
        dismiss->setFocusPolicy(Qt::NoFocus);
        dismiss->setToolTip(QStringLiteral("Dismiss"));
        const QString id = note.id;
        connect(dismiss, &QToolButton::clicked, this, [id] { relay::NotificationCenter::instance().remove(id); });
        layout->addWidget(dismiss, 0, Qt::AlignTop);
        row->installEventFilter(this);
        row->setProperty("relaySource", note.source);
        m_rowsLayout->insertWidget(m_rowsLayout->count() - 1, row);
        return row;
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::MouseButtonRelease) {
            const QString source = object->property("relaySource").toString();
            if (!source.isEmpty() && onOpenSource) { hide(); onOpenSource(source); return true; }
        }
        return QFrame::eventFilter(object, event);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape) { hide(); return; }
        QFrame::keyPressEvent(event);
    }

private:
    QScrollArea *m_scroll = nullptr;
    QWidget *m_rows = nullptr;
    QVBoxLayout *m_rowsLayout = nullptr;
    QLabel *m_empty = nullptr;
    QToolButton *m_clear = nullptr;
    QList<QWidget *> m_widgets;
};

