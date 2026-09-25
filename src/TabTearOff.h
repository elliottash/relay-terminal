// TabTearOff.h — the geometry of dragging a tab out of its window (#W6ES).
//
// A tab label in the window's tab bar already drags: Qt's QTabBar owns the reorder inside
// the bar. What did not exist is everything past the tab row — grabbing a tab and pulling
// it into another window did nothing. RelayWindow::tabDrag watches the bar's mouse
// events and turns a drag that leaves the tab row into a tab move; these are the two
// decisions that watching has to make, kept here (style of PaneLayout.h) so they can be
// tested without a full window:
//
//   leavesTabRow  — has this press-on-a-tab become a tear-off, or is it still Qt's gesture?
//   dropWindow    — which window does a release land in, if any?
//
// Qt 5.15 / Qt 6.4, QtCore only.

#pragma once

#include <QPoint>
#include <QRect>
#include <utility>
#include <vector>

namespace relay::tabs {

// A press on a tab label becomes a tear-off once the cursor is past Qt's drag threshold AND
// outside the tab bar's own rect — `barGlobal` is the bar's global geometry, not the window's.
// The window cannot be the yardstick: the window being dragged to usually overlaps the source,
// and a maximized source can never be left at all, so a whole-window test made the gesture a
// no-op in exactly the layouts people drag tabs in (#W6ES). Every tabbed interface tears a tab
// out of its strip, and everything still inside the strip stays QTabBar's reorder.
inline bool leavesTabRow(const QPoint &pressGlobal, const QPoint &cursorGlobal, const QRect &barGlobal,
                         int startDragDistance) {
    return (cursorGlobal - pressGlobal).manhattanLength() > startDragDistance
        && !barGlobal.contains(cursorGlobal);
}

// Which window a torn-off tab lands in: the one whose geometry holds the release point.
// `windows` pairs each candidate window's global frameGeometry with an id; the source
// window's own id is never the answer, because the drag already left it. A release on empty
// space (or over another application's window) matches nothing and returns -1: the tab gets
// a window of its own. Where rectangles overlap, the first listed wins — list frontmost
// first.
inline int dropWindow(const std::vector<std::pair<QRect, int>> &windows, int sourceId,
                      const QPoint &cursorGlobal) {
    for (const auto &entry : windows)
        if (entry.second != sourceId && entry.first.contains(cursorGlobal)) return entry.second;
    return -1;
}

}  // namespace relay::tabs
