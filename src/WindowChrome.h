// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// The window's own title bar, since Relay draws one instead of taking the desktop's: ChromeButton
// paints the header glyphs (bell, gear, the join plug, minimize, maximize, close, and the tab
// row's own buttons) so they all sit at one stroke weight, ChromeSeparator is the hairline that
// groups the header's right row, and NotificationsPopup is the list behind the bell.

#include "Theme.h"
#include "PaneChrome.h"   // relay::chrome::paintTypeGlyph: a tool pane's button wears its pane's glyph
#include "Notifications.h"

#include <QFontMetricsF>
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
// and, on the right, the bell; a hairline (ChromeSeparator); the tool-pane buttons ending in the
// Options gear; a hairline; the join plug; a hairline; minimize/maximize/close — the last
// hairline and the window buttons only when Relay draws the frame. Dragging the empty part of
// the row moves the window, a double-click maximizes it, and a few pixels of padding around the
// window stay grabbable for resizing (see RelayWindow::edgesAt).
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
    enum class Glyph { Bell, Gear, Minimize, Maximize, Restore, Close, Plus, TabClose, Connect };

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

    // A button that opens a tool pane (Actions, Sessions, the Switchboard) draws that pane's own
    // glyph, the one on its header band (src/PaneStatus.h), so the two are recognisably one thing.
    explicit ChromeButton(relay::panestatus::Glyph paneGlyph, QWidget *parent = nullptr, int size = kSize)
        : ChromeButton(Glyph::Gear, parent, size) { m_paneGlyph = paneGlyph; }

    void setGlyph(Glyph glyph) { if (m_glyph == glyph) return; m_glyph = glyph; update(); }

    // ----- lit while its pane is open (owner, 2026-09-18) --------------------------------------
    // "the sessions / actions / switchboard / options buttons at the top right should be
    // highlighted when they are open (using the header colors). click again to close those panes."
    // The pane type is the whole link: the button's ground and its glyph come from that type's own
    // header band (relay::panestatus::openButtonStyle), read at paint time, so a theme switch, a
    // retinted pane type or "appearance/pane_colours" needs nothing but a repaint. The window says
    // which buttons are lit (RelayWindow::syncChromeButtons).
    void setPaneType(const QString &paneType) { if (m_paneType == paneType) return; m_paneType = paneType; update(); }
    void setOpen(bool open) { if (m_open == open) return; m_open = open; update(); }
    bool isOpen() const { return m_open; }
    // Unseen notifications, drawn as a dot on the bell. 0 hides it.
    void setBadge(int count) { if (m_badge == count) return; m_badge = count; update(); }

    static constexpr int kSize = 26;

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool hovered = underMouse() && isEnabled();
        const bool closing = m_glyph == Glyph::Close;
        const qreal radius = std::min(width(), height()) * 5.0 / kSize;
        const QRectF body = QRectF(rect()).adjusted(1, 1, -1, -1);
        namespace ps = relay::panestatus;
        const ps::Tokens tokens = relay::chrome::tokens();
        const bool lit = m_open && !m_paneType.isEmpty();
        const ps::OpenButtonStyle open =
            lit ? ps::openButtonStyle(ps::typeStyle(m_paneType, relay::chrome::colourMode(), tokens), hovered)
                : ps::OpenButtonStyle{};
        // The ground the glyph and the focus ring end up on, for their contrast floors (§14).
        QColor ground = relay::theme::Background;
        if (lit) {
            painter.setPen(QPen(open.line, 1));
            painter.setBrush(open.fill);
            painter.drawRoundedRect(body, radius, radius);
            ground = open.fill;
        } else if (hovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(closing ? relay::theme::Error.darker(130) : relay::theme::SurfaceRaised);
            painter.drawRoundedRect(body, radius, radius);
            ground = closing ? relay::theme::Error.darker(130) : relay::theme::SurfaceRaised;
        }
        // The keyboard-focus ring, outside whatever ground the button ended up with. These buttons
        // are Qt::NoFocus on purpose — the title bar must never take Tab from the terminal — so it
        // shows only when something puts the focus here deliberately.
        if (hasFocus()) {
            painter.setPen(QPen(ps::focusRing(ground, tokens), 1.4));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(rect()).adjusted(0.7, 0.7, -0.7, -0.7), radius + 0.6, radius + 0.6);
        }
        // A dimmed button keeps its space in the layout but shows nothing, so a row of tabs is
        // not a row of crosses; pointing at the tab (or selecting it) brings the cross back.
        if (m_dim && !hovered) return;
        QColor ink = hovered ? (closing ? QColor(Qt::white) : relay::theme::Text) : relay::theme::TextMuted;
        if (lit) ink = open.ink;
        if (!isEnabled()) ink = relay::theme::TextMuted.darker(150);
        // Every glyph is drawn for a 26 px button and scaled from there, so a smaller button
        // (the tab's close cross) keeps the same proportions and the same apparent weight.
        const qreal unit = std::min(width(), height()) / qreal(kSize);
        painter.setPen(QPen(ink, 1.3 * std::max(0.85, unit), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        const QPointF centre(width() / 2.0, height() / 2.0);
        if (m_paneGlyph != relay::panestatus::Glyph::None) {
            // Drawn for a 14 px box in a 26 px button, like the other glyphs' ±6 px reach.
            const qreal side = 14 * unit;
            relay::chrome::paintTypeGlyph(painter, QRectF(centre.x() - side / 2, centre.y() - side / 2, side, side), m_paneGlyph, ink);
            return;
        }
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
        case Glyph::Connect:
            // A plug, the one on the app icon: two prongs, a body, and its cord.
            painter.drawRoundedRect(QRectF(centre + QPointF(-1.5, -3.5) * unit, QSizeF(5 * unit, 7 * unit)), 1.2 * unit, 1.2 * unit);
            painter.drawLine(centre + QPointF(-1.5, -1.8) * unit, centre + QPointF(-5.5, -1.8) * unit);
            painter.drawLine(centre + QPointF(-1.5, 1.8) * unit, centre + QPointF(-5.5, 1.8) * unit);
            painter.drawLine(centre + QPointF(3.5, 0) * unit, centre + QPointF(6, 0) * unit);
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
        // Unread dot, top-right, over the bell's shoulder. A count grows it into a pill wide
        // enough for its digits at the legibility floor; they were 7px in an 8px dot.
        painter.setPen(Qt::NoPen);
        painter.setBrush(relay::theme::Accent);
        if (m_badge <= 1) {
            painter.drawEllipse(QRectF(width() - 11.0, 3.0, 8.0, 8.0));
            return;
        }
        QFont small = font();
        small.setPointSizeF(relay::theme::FloorPt);
        small.setBold(true);
        const QString count = m_badge > 9 ? QStringLiteral("9+") : QString::number(m_badge);
        const QFontMetricsF metrics(small);
        const qreal h = std::ceil(metrics.height()) - 1, w = std::max(h, metrics.horizontalAdvance(count) + 6);
        const QRectF pill(width() - w - 1.0, 0.0, w, h);
        painter.drawRoundedRect(pill, h / 2, h / 2);
        painter.setFont(small);
        painter.setPen(relay::theme::AccentText);
        painter.drawText(pill, Qt::AlignCenter, count);
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
    relay::panestatus::Glyph m_paneGlyph = relay::panestatus::Glyph::None;
    QString m_paneType;       // the tool pane this button opens, when it opens one
    int m_badge = 0;
    bool m_dim = false;
    bool m_open = false;      // that pane is open in the window's current tab
};

// The hairline between the header's groups: after the bell, and either side of the join plug
// (owner, 2026-09-20). A painted line rather than a stylesheet rule, so it reads the theme at
// paint time the way ChromeButton does and a theme switch needs nothing but a repaint; the same
// 1px border token QToolBar::separator wears.
class ChromeSeparator final : public QWidget {
public:
    explicit ChromeSeparator(QWidget *parent = nullptr) : QWidget(parent) {
        // Not a control: a press on it falls through to the header and drags the window.
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFixedSize(1, ChromeButton::kSize);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        // On the half-pixel so a 1px pen covers exactly one device column, and inset top and
        // bottom so it reads as a tick between the glyphs rather than a full-height rule.
        painter.setPen(QPen(relay::theme::Border, 1));
        painter.drawLine(QPointF(0.5, 7.0), QPointF(0.5, height() - 7.0));
    }
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

