// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// The Relay mark, painted. One drawing of the app's own icon, so every surface that shows it shows
// the same shape: the pane header's live state glyph and the tab icons (src/PaneChrome.h), and the
// button at the left of the "Relaying – …" line that opens the Activity pane (PaneBusyLine,
// src/Pane.h, card #4X53). It lives in a header of its own because Pane.h is included before
// PaneChrome.h in the one translation unit and both need it; never copy the shape instead.

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QVector>

#include <algorithm>

namespace relay::chrome {

// The Relay mark, one ink: the cord chevron, the dash and the seated tip, redrawn from
// data/icons/org.relayterminal.Relay-symbolic.svg (16x16) into the glyph box. It is the live
// states' glyph (card #4E13: "a blue blinking relay icon (terminal program running) or purple
// blinking relay icon (agent working)") — the app saying its own name where work is happening.
// `ground` fills the ring's hole, as the other filled shapes do for their cut marks.
inline void paintRelayMark(QPainter &p, const QRectF &box, const QColor &ink, const QColor &ground) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const qreal u = std::min(box.width(), box.height()) / 16.0;
    auto at = [&](qreal x, qreal y) { return box.topLeft() + QPointF(x, y) * u; };
    // The cord: a > shape, stroked with round ends as the icon's own is.
    p.setPen(QPen(ink, 1.9 * u, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(QPolygonF(QVector<QPointF>{at(2.6, 3.6), at(6.4, 8.0), at(2.6, 12.4)}));
    // The dash.
    p.setPen(Qt::NoPen);
    p.setBrush(ink);
    p.drawRoundedRect(QRectF(at(7.2, 7.05), at(11.0, 8.95)), 0.95 * u, 0.95 * u);
    // The seated tip: a filled ring with its hole in the ground's colour.
    p.drawEllipse(at(12.9, 8.0), 2.35 * u, 2.35 * u);
    p.setBrush(ground);
    p.drawEllipse(at(12.9, 8.0), 1.15 * u, 1.15 * u);
    p.restore();
}

// The same mark inside a circle — the owner's "circled purple relay icon" (#4X53). The ring and
// the mark are the one ink the caller passes (the busy line's own state colour, so the button says
// whose work you would be looking into); the disc is that ink laid very thinly over `ground`, and
// the blend is what the mark's cut hole is filled with, so the seated tip reads on the disc. A
// pressed or hovered button says so by weight, never by a second colour.
inline void paintCircledRelayMark(QPainter &p, const QRectF &circle, const QColor &ink,
                                  const QColor &ground, bool hover = false, bool pressed = false) {
    const int alpha = pressed ? 64 : hover ? 40 : 22;
    auto mix = [](const QColor &over, const QColor &under, int a) {
        const qreal f = a / 255.0;
        return QColor(int(std::lround(over.red() * f + under.red() * (1 - f))),
                      int(std::lround(over.green() * f + under.green() * (1 - f))),
                      int(std::lround(over.blue() * f + under.blue() * (1 - f))));
    };
    const QColor disc = mix(ink, ground, alpha);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QColor ring = ink;
    ring.setAlpha(hover || pressed ? 235 : 170);
    p.setPen(QPen(ring, circle.width() >= 15 ? 1.2 : 1.0));
    p.setBrush(disc);
    // Inset by half the ring's width so the stroke lands inside the button's box.
    p.drawEllipse(circle.adjusted(0.6, 0.6, -0.6, -0.6));
    p.restore();
    // The mark in the circle's inscribed square, a touch smaller so it never touches the ring.
    const qreal side = circle.width() * 0.70;
    paintRelayMark(p, QRectF(circle.center() - QPointF(side, side) / 2, QSizeF(side, side)), ink, disc);
}

}  // namespace relay::chrome
