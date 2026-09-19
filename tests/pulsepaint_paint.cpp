// SPDX-License-Identifier: GPL-3.0-or-later
// The painting half of tests/pulsepaint_test.cpp (card #V8KT, and the subagent badge of card
// #YMSR). PaneChrome.h lives in this translation unit on purpose: its include chain reaches
// src/Keymap.h, whose preset table is a raw string literal moc cannot parse (see
// tests/buttonfit_test.cpp for the same rule), so the moc'd test class must not see it. AUTOMOC
// never mocs a source without a Q_OBJECT, which this file is.
#include "pulsepaint_helpers.h"

#include "PaneChrome.h"

QImage tabAt(relay::panestatus::State state, relay::panestatus::State live, int phase, qreal dpr) {
    const QIcon icon = relay::chrome::tabIcon(true, state, false, relay::panestatus::Glyph::None, QColor(), dpr, live, phase);
    // Qt5's QIcon::pixmap takes a size in device pixels, so ask for the raster directly.
    return icon.pixmap(QSize(int(16 * dpr), int(16 * dpr))).toImage();
}

QImage headAt(relay::panestatus::State state, int phase, qreal dpr) {
    QImage img(QSize(16, 16) * dpr, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    QPainter p(&img);
    const relay::panestatus::Tokens t = relay::chrome::tokens();
    // What PaneStateGlyph paints: a 16x16 widget, the box inset half a pixel on every side.
    relay::chrome::paintStateGlyph(p, QRectF(0, 0, 16, 16).adjusted(0.5, 0.5, -0.5, -0.5), state,
                                   relay::panestatus::stateInk(state, t), t.background,
                                   relay::panestatus::pulseScale(phase));
    p.end();
    return img;
}

QImage badgeAt(int live, qreal dpr, bool remote) {
    const relay::panestatus::Tokens t = relay::chrome::tokens();
    const QColor ground = remote ? relay::panestatus::remoteStyle(t).fill : t.background;
    QFont font = QApplication::font();
    font.setWeight(QFont::DemiBold);   // PaneSubagentBadge paints its number in this weight
    // The widget's own sizeHint for one digit, whatever the count is, so 0 and 8 are the same box.
    const QSize size(7 + 12 + 4 + QFontMetrics(font).horizontalAdvance(QStringLiteral("8")) + 7, 18);
    QImage img(size * dpr, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(ground);
    QPainter p(&img);
    relay::chrome::paintSubagentBadge(p, QRectF(QPointF(0, 0), QSizeF(size)),
                                      relay::panestatus::subagentBadgeText(live),
                                      relay::panestatus::subagentBadgeStyle(ground, t),
                                      relay::chrome::paneRadius() > 0 ? 5 : 0, font);
    p.end();
    return img;
}

QImage badgeOffsetAt(int live, qreal dpr, int offset) {
    const relay::panestatus::Tokens t = relay::chrome::tokens();
    const QColor ground = t.background;
    QFont font = QApplication::font();
    font.setWeight(QFont::DemiBold);
    const QSize box(7 + 12 + 4 + QFontMetrics(font).horizontalAdvance(QStringLiteral("8")) + 7, 18);
    QImage img(QSize(box.width() + offset, box.height()) * dpr, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(ground);
    QPainter p(&img);
    relay::chrome::paintSubagentBadge(p, QRectF(QPointF(offset, 0), QSizeF(box)),
                                      relay::panestatus::subagentBadgeText(live),
                                      relay::panestatus::subagentBadgeStyle(ground, t),
                                      relay::chrome::paneRadius() > 0 ? 5 : 0, font);
    p.end();
    return img;
}

QColor headerGround() { return relay::chrome::tokens().background; }

QColor sshBandFill() { return relay::panestatus::remoteStyle(relay::chrome::tokens()).fill; }
