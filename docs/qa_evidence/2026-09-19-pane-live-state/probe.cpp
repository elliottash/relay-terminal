// SPDX-License-Identifier: GPL-3.0-or-later
// Evidence diagnosis for card #V8KT: does each step of the pulse actually change the painted
// glyph at tab/header size? Paints the real glyphs (relay::chrome::tabIcon for the tab,
// paintStateGlyph for the 16 px header glyph, exactly as PaneStateGlyph::paintEvent sizes it)
// at every phase and reports how many pixels differ between phase pairs, per device pixel
// ratio. Pairs (p, p+1) answer "is the step visible at all"; the pair (1, 3) answers "do the
// two 0.90 steps of pulseScale() paint identically" (the suspected cause of the relaying
// scene's 0-pixel evidence diff). "any" counts pixels whose channels differ at all, "vis"
// counts pixels differing by 16 or more in some channel — a coarse but honest stand-in for
// "a person would notice".
#include "PaneChrome.h"

#include <QApplication>
#include <QImage>
#include <QPainter>

#include <cstdio>

namespace ps = relay::panestatus;

static int g_any = 0, g_vis = 0;
static void compare(const QImage &a, const QImage &b) {
    g_any = g_vis = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
            const QColor pa = a.pixelColor(x, y), pb = b.pixelColor(x, y);
            const int d = std::max({std::abs(pa.red() - pb.red()), std::abs(pa.green() - pb.green()),
                                    std::abs(pa.blue() - pb.blue()), std::abs(pa.alpha() - pb.alpha())});
            if (d >= 1) ++g_any;
            if (d >= 16) ++g_vis;
        }
}

static QImage tabImage(ps::State state, ps::State live, int phase, qreal dpr) {
    const QIcon icon = relay::chrome::tabIcon(true, state, false, ps::Glyph::None, QColor(), dpr, live, phase);
    // Qt5 QIcon::pixmap takes a size in device pixels, so ask for the raster directly.
    return icon.pixmap(QSize(int(16 * dpr), int(16 * dpr))).toImage();
}

// What PaneStateGlyph paints: a 16x16 widget, the box inset half a pixel on every side.
static QImage headImage(ps::State state, int phase, qreal dpr) {
    QImage img(QSize(16, 16) * dpr, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    QPainter p(&img);
    const ps::Tokens t = relay::chrome::tokens();
    relay::chrome::paintStateGlyph(p, QRectF(0, 0, 16, 16).adjusted(0.5, 0.5, -0.5, -0.5), state,
                                   ps::stateInk(state, t), t.background, ps::pulseScale(phase));
    p.end();
    return img;
}

typedef QImage (*Shot)(ps::State, ps::State, int, qreal);

static void table(const char *what, Shot shot, ps::State state, ps::State live, qreal dpr) {
    const int phases[4] = {0, 1, 2, 3};
    printf("%-34s dpr=%.0f", what, dpr);
    for (int i = 0; i < 4; ++i) {
        compare(shot(state, live, phases[i], dpr), shot(state, live, phases[(i + 1) % 4], dpr));
        printf("   %d-%d: %4d/%3d", phases[i], phases[(i + 1) % 4], g_any, g_vis);
    }
    compare(shot(state, live, 1, dpr), shot(state, live, 3, dpr));
    printf("   1-3: %4d/%3d\n", g_any, g_vis);
}

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    for (qreal dpr : {1.0, 2.0}) {
        printf("== devicePixelRatio %.0f ==                                     (any/vis pixels, 16 px logical)\n", dpr);
        // The tab's own live icon breathing (state == live): the running and relaying scenes.
        table("tab: Running breathes", [](ps::State s, ps::State, int ph, qreal d) { return tabImage(s, s, ph, d); }, ps::State::Running, ps::State::Running, dpr);
        table("tab: Working breathes", [](ps::State s, ps::State, int ph, qreal d) { return tabImage(s, s, ph, d); }, ps::State::Working, ps::State::Working, dpr);
        table("tab: Subagents breathes", [](ps::State s, ps::State, int ph, qreal d) { return tabImage(s, s, ph, d); }, ps::State::Subagents, ps::State::Subagents, dpr);
        // A news icon carrying the breathing corner dot: the mix scene.
        table("tab: Failed + Running dot", [](ps::State, ps::State live, int ph, qreal d) { return tabImage(ps::State::Failed, live, ph, d); }, ps::State::Failed, ps::State::Running, dpr);
        table("tab: Failed + Working dot", [](ps::State, ps::State live, int ph, qreal d) { return tabImage(ps::State::Failed, live, ph, d); }, ps::State::Failed, ps::State::Working, dpr);
        // The pane header's glyph breathing.
        table("head: Running breathes", [](ps::State s, ps::State, int ph, qreal d) { return headImage(s, ph, d); }, ps::State::Running, ps::State::Running, dpr);
        table("head: Working breathes", [](ps::State s, ps::State, int ph, qreal d) { return headImage(s, ph, d); }, ps::State::Working, ps::State::Working, dpr);
        table("head: Subagents breathes", [](ps::State s, ps::State, int ph, qreal d) { return headImage(s, ph, d); }, ps::State::Subagents, ps::State::Subagents, dpr);
    }
    return 0;
}
