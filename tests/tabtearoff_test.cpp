// SPDX-License-Identifier: AGPL-3.0-or-later
// Tab tear-off (#W6ES): the two decisions a tab-label drag past its tab row has to make —
// when a press on a tab label has left the tab row, and which window a release lands in.
// They live in TabTearOff.h so they can be checked here without a full window; the drag
// itself (watching the bar, highlighting the target, moving the page) is RelayWindow's,
// wired in RelayWindowCore.cpp's event filter.
//
// The window cases run against real QWidgets, because the regression this guards against is
// a coordinate mistake: the decision is made in global coordinates over the tab bar's
// mapToGlobal() rect, and a hand-rolled QRect hides exactly the kind of off-by-frame error
// that would make a tab tear off while it is still over its own tab row. The bar rect — not
// the window's frame — is the yardstick because the window a tab is dragged to usually
// overlaps its source, and a maximized source can never be left at all: measured against the
// frame, the gesture was a silent no-op in exactly those layouts (#W6ES).
#include "TabTearOff.h"

#include <QPoint>
#include <QRect>
#include <QTabBar>
#include <QTest>
#include <QWidget>

using relay::tabs::dropWindow;
using relay::tabs::leavesTabRow;

namespace {
constexpr int kDragDistance = 10;   // QApplication::startDragDistance() defaults to 10 px
}

class TabTearOffTests : public QObject {
    Q_OBJECT

private slots:

    void insideTheTabRowItsReorder() {
        const QRect bar(600, 400, 800, 36);   // the tab row of an 800x500 window at (600,400)
        const QPoint pressOnTab(1000, 418);
        // However far the tab travels along the row, inside the row it never tears off:
        // Qt's QTabBar owns that gesture (reorder), and a stray drag across the labels must
        // not rip the tab out of its page.
        QVERIFY(!leavesTabRow(pressOnTab, QPoint(1005, 423), bar, kDragDistance));
        QVERIFY(!leavesTabRow(pressOnTab, QPoint(1380, 410), bar, kDragDistance));  // far right
        QVERIFY(!leavesTabRow(pressOnTab, QPoint(610, 430), bar, kDragDistance));   // far left
    }

    void pastTheRowTearsEvenInsideTheWindow() {
        const QRect bar(600, 400, 800, 36);
        const QPoint pressOnTab(1000, 418);
        // Straight down out of the row and into the source window's own content: this is the
        // overlapping-windows case — the release can be over another window while the cursor
        // never once left this one — so the tear-off has to arm here (#W6ES).
        QVERIFY(leavesTabRow(pressOnTab, QPoint(1000, 460), bar, kDragDistance));
        QVERIFY(leavesTabRow(pressOnTab, QPoint(1380, 880), bar, kDragDistance));   // deep content
        QVERIFY(leavesTabRow(pressOnTab, QPoint(1000, 390), bar, kDragDistance));   // above the row
        // A pixel past the row is not yet a tear-off: Qt's own drag threshold still applies.
        QVERIFY(!leavesTabRow(pressOnTab, QPoint(1008, 418), bar, kDragDistance));  // 8 px: jitter
        QVERIFY(leavesTabRow(pressOnTab, QPoint(1000, 447), bar, kDragDistance));   // 29 px, 11 past
    }

    void onRealWidgets() {
        QWidget window;
        window.setGeometry(100, 100, 300, 200);
        QTabBar bar(&window);
        bar.setGeometry(0, 0, 300, 36);   // the row across the top of the window
        const QRect barGlobal(bar.mapToGlobal(QPoint(0, 0)), bar.size());   // what production passes
        const QPoint onTab(barGlobal.left() + 40, barGlobal.top() + 18);
        QVERIFY(!leavesTabRow(onTab, onTab + QPoint(120, 0), barGlobal, kDragDistance));  // along the row
        QVERIFY(leavesTabRow(onTab, window.frameGeometry().center(), barGlobal, kDragDistance));  // into content
        QVERIFY(leavesTabRow(onTab, barGlobal.topLeft() + QPoint(-30, -30), barGlobal, kDragDistance));
    }

    void dropWindowFindsTheWindowUnderTheCursor() {
        const QRect left(100, 100, 400, 300), right(600, 100, 400, 300), source(450, 250, 100, 80);
        const std::vector<std::pair<QRect, int>> windows{{left, 1}, {right, 2}, {source, 3}};
        QCOMPARE(dropWindow(windows, 3, QPoint(300, 200)), 1);   // over the left window
        QCOMPARE(dropWindow(windows, 3, QPoint(800, 200)), 2);   // over the right window
        QCOMPARE(dropWindow(windows, 3, QPoint(20, 900)), -1);   // empty desktop: a window of its own
        // The source is never the answer: (525, 290) is inside `source` and no other window.
        QCOMPARE(dropWindow(windows, 3, QPoint(525, 290)), -1);
        // Overlapping rectangles resolve to the first listed that is not the source: (470, 290)
        // is inside both `source` and `left`.
        const std::vector<std::pair<QRect, int>> stacked{{source, 3}, {left, 1}};
        QCOMPARE(dropWindow(stacked, 3, QPoint(470, 290)), 1);
    }
};

QTEST_MAIN(TabTearOffTests)
#include "tabtearoff_test.moc"
