// SPDX-License-Identifier: AGPL-3.0-or-later
// Tab tear-off (#W6ES): the two decisions a tab-label drag past its window has to make —
// when a press on a tab label has left its window, and which window a release lands in.
// They live in TabTearOff.h so they can be checked here without a full window; the drag
// itself (watching the bar, highlighting the target, moving the page) is RelayWindow's,
// wired in RelayWindowCore.cpp's event filter.
//
// The window cases run against real QWidgets, because the regression this guards against is
// a coordinate mistake: the decision is made in global coordinates over frameGeometry(), and
// a hand-rolled QRect hides exactly the kind of off-by-frame error that would make a tab tear
// off while it is still over its own title strip.
#include "TabTearOff.h"

#include <QPoint>
#include <QRect>
#include <QTest>
#include <QWidget>

using relay::tabs::dropWindow;
using relay::tabs::leavesWindow;

namespace {
constexpr int kDragDistance = 10;   // QApplication::startDragDistance() defaults to 10 px
}

class TabTearOffTests : public QObject {
    Q_OBJECT

private slots:

    void leavesWindowInsideTheWindowItsReorder() {
        const QRect window(600, 400, 800, 500);
        const QPoint pressOnTab(1000, 420);   // on the tab bar
        // However far the tab travels, inside the window it never tears off: Qt's QTabBar owns
        // that gesture (reorder), and a stray drag from the bar into the content must not rip
        // the tab out of its page.
        QVERIFY(!leavesWindow(pressOnTab, QPoint(1005, 425), window, kDragDistance));
        QVERIFY(!leavesWindow(pressOnTab, QPoint(1380, 880), window, kDragDistance));
        QVERIFY(!leavesWindow(pressOnTab, QPoint(610, 890), window, kDragDistance));   // far corner
        // The window's own title strip — above the bar, still inside the geometry — is inside.
        QVERIFY(!leavesWindow(pressOnTab, QPoint(1000, 405), window, kDragDistance));
    }

    void leavesWindowOutsideIt() {
        const QRect window(600, 400, 800, 500);
        const QPoint pressOnTab(1000, 420);
        QVERIFY(leavesWindow(pressOnTab, QPoint(400, 410), window, kDragDistance));    // left of it
        QVERIFY(leavesWindow(pressOnTab, QPoint(1000, 390), window, kDragDistance));   // above it
        QVERIFY(leavesWindow(pressOnTab, QPoint(1450, 900), window, kDragDistance));   // below it
        // A pixel past the edge is not yet a tear-off: Qt's own drag threshold still applies.
        QVERIFY(!leavesWindow(pressOnTab, QPoint(1008, 420), window, kDragDistance));  // far enough left, but only 8 px moved
        QVERIFY(leavesWindow(pressOnTab, QPoint(1000, 399), window, kDragDistance));   // 21 px moved, 1 px past the top
    }

    void leavesWindowOnRealWidgets() {
        QWidget a;
        a.setGeometry(100, 100, 300, 200);
        const QRect geometry = a.frameGeometry();   // what the production code passes
        const QPoint onTab(geometry.left() + 40, geometry.top() + 30);
        QVERIFY(!leavesWindow(onTab, a.geometry().center(), geometry, kDragDistance));
        QVERIFY(leavesWindow(onTab, geometry.center() + QPoint(geometry.width(), 0), geometry, kDragDistance));
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
