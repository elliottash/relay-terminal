// SPDX-License-Identifier: GPL-3.0-or-later
// Card #V8KT: a live pane's marks must not only exist, they must move. This renders the real
// tab icon and the header glyph (pulsepaint_paint.cpp) at every step of the pulse and fails
// when a step stops changing the painted pixels.
//
// Why a rendering test and not another pulseScale() assertion: the first evidence run of the
// card measured "0 differing pixels" between two screenshots while the marks were animating
// fine, because the pulse's steps repeat themselves (in card #4E13's blink, phases 0 and 2
// paint the same image, and so do 1 and 3; in the breath it replaced, 1 and 3 were both 0.90)
// and the two frames happened to land a whole period — or two steps — apart. So "two captures
// are identical" proves nothing about a periodic pulse; what does prove the pulse is visible
// is that every *adjacent* step changes the pixels, at tab size and header size, at 1x and 2x
// device pixel ratios. That is the property pinned here, measured on ink rather than trusted
// from geometry.
#include "pulsepaint_helpers.h"

#include <QTest>

#include <algorithm>

using namespace relay::panestatus;

namespace {
// Pixels whose channels move by `least` or more: a coarse but honest "a person would notice".
// The column range is in device pixels; the whole width by default.
int movedPixels(const QImage &a, const QImage &b, int least, int x0 = 0, int x1 = -1) {
    if (x1 < 0) x1 = a.width();
    int moved = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = x0; x < x1; ++x) {
            const QColor pa = a.pixelColor(x, y), pb = b.pixelColor(x, y);
            const int delta = std::max({qAbs(pa.red() - pb.red()), qAbs(pa.green() - pb.green()),
                                        qAbs(pa.blue() - pb.blue()), qAbs(pa.alpha() - pb.alpha())});
            if (delta >= least) ++moved;
        }
    return moved;
}

// One colour, the size of `img`: what "nothing was painted here" is compared against.
QImage flatLike(const QImage &img, const QColor &ground) {
    QImage flat(img.size(), QImage::Format_ARGB32_Premultiplied);
    flat.fill(ground);
    return flat;
}

// A step worth failing on: the marks are 16 px, so a step that moves fewer than this many
// pixels strongly is a step nobody sees. The measured values sit an order of magnitude above.
constexpr int kEnoughMovedPixels = 8;
}  // namespace

class PulsePaintTest final : public QObject {
    Q_OBJECT
private slots:
    // Every adjacent step of the blink changes what is painted, for each live state's own ink,
    // on the tab and in the pane header. (Two-steps-apart pairs do not, on purpose: the blink
    // alternates two levels, so the pairs 0-2 and 1-3 each paint the same image — see
    // pulseScale. Sampling a periodic pulse has to mind its period; that is the evidence
    // harness's job, not the pulse's.)
    void everyStepOfTheBlinkMoves() {
        for (qreal dpr : {1.0, 2.0})
            for (State state : {State::Running, State::Working, State::Subagents})
                for (int phase = 0; phase < 4; ++phase) {
                    const int next = (phase + 1) % 4;
                    const QByteArray where = QStringLiteral("%1 dpr=%2 step %3-%4")
                                                 .arg(stateName(state)).arg(dpr).arg(phase).arg(next).toUtf8();
                    QVERIFY2(movedPixels(tabAt(state, state, phase, dpr), tabAt(state, state, next, dpr), 8)
                                 >= kEnoughMovedPixels, where.constData());
                    QVERIFY2(movedPixels(headAt(state, phase, dpr), headAt(state, next, dpr), 8)
                                 >= kEnoughMovedPixels, where.constData());
                }
    }

    // Card #YMSR: the subagent badge in the pane header. Its first rule is "if applicable" — with
    // no subagents the badge is not there at all, not a chip with a zero in it — and where it is
    // there, its star and its number are really painted, at 1x and 2x, on the header's ground and
    // on the ssh band's fill (a remote session repaints the title row the badge sits in).
    void theSubagentBadgeIsPaintedOnlyWhenItApplies() {
        const QColor band = sshBandFill();
        for (qreal dpr : {1.0, 2.0}) {
            for (const QColor &ground : {headerGround(), band}) {
                const bool remote = ground == band;
                const QByteArray where = QStringLiteral("%1 dpr=%2")
                                             .arg(remote ? QStringLiteral("ssh band") : QStringLiteral("header"))
                                             .arg(dpr).toUtf8();
                // No subagents: every pixel is the ground the widget sits on.
                const QImage none = badgeAt(0, dpr, remote);
                QVERIFY2(movedPixels(none, flatLike(none, ground), 1) == 0, where.constData());
                // One subagent: the badge is a chip, and it is painted.
                const QImage one = badgeAt(1, dpr, remote);
                QVERIFY2(movedPixels(one, flatLike(one, ground), 8) >= 30 * dpr, where.constData());
                // Its star is inked, in the left cell of the box (x 7..19 logical).
                QVERIFY2(movedPixels(one, none, 8, 0, int(19 * dpr)) >= 10 * dpr, where.constData());
                // The number is the count, and nothing else about the badge: the same box with a
                // 3 in it and with an 8 differs only to the right of the star's cell.
                const QImage three = badgeAt(3, dpr, remote), eight = badgeAt(8, dpr, remote);
                QVERIFY2(movedPixels(three, eight, 8, 0, int(23 * dpr)) == 0, where.constData());
                QVERIFY2(movedPixels(three, eight, 8, int(23 * dpr)) >= 4 * dpr, where.constData());
            }
        }
    }

    // A news icon (here: a failed turn) carrying the live corner dot still moves with every
    // step — the dot is the one thing that says a sibling pane is busy.
    void everyStepOfTheCornerDotMoves() {
        for (qreal dpr : {1.0, 2.0})
            for (State live : {State::Running, State::Working})
                for (int phase = 0; phase < 4; ++phase) {
                    const int next = (phase + 1) % 4;
                    const QByteArray where = QStringLiteral("dot %1 dpr=%2 step %3-%4")
                                                 .arg(stateName(live)).arg(dpr).arg(phase).arg(next).toUtf8();
                    QVERIFY2(movedPixels(tabAt(State::Failed, live, phase, dpr),
                                         tabAt(State::Failed, live, next, dpr), 8) >= kEnoughMovedPixels,
                             where.constData());
                }
    }
};

QTEST_MAIN(PulsePaintTest)
#include "pulsepaint_test.moc"
