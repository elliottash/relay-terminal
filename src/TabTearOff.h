// TabTearOff.h — the geometry of dragging a tab out of its window (#W6ES).
//
// A tab label in the window's tab bar already drags: Qt's QTabBar owns the reorder inside
// the bar. What did not exist is everything past the window edge — grabbing a tab and
// pulling it into another window did nothing. RelayWindow::tabDrag watches the bar's mouse
// events and turns a drag that leaves its window into a tab move; these are the two
// decisions that watching has to make, kept here (style of PaneLayout.h) so they can be
// tested without a full window:
//
//   leavesWindow  — has this press-on-a-tab become a tear-off, or is it still Qt's gesture?
//   dropWindow    — which window does a release land in, if any?
//
// Qt 5.15 / Qt 6.4, QtCore only.

#pragma once

#include <QPoint>
#include <QRect>
#include <utility>
#include <vector>

namespace relay::tabs {

// A press on a tab label leaves its window only once the cursor is past Qt's drag threshold
// AND outside the window's own geometry — `windowGlobal` is the source window's
// frameGeometry(): on a native frame the title bar belongs to the window, and a stray drag
// from the tab bar into the title strip must not tear the tab out. Everything inside the
// window, however far from the bar, is QTabBar's reorder (or nothing) and stays that way.
inline bool leavesWindow(const QPoint &pressGlobal, const QPoint &cursorGlobal, const QRect &windowGlobal,
                         int startDragDistance) {
    return (cursorGlobal - pressGlobal).manhattanLength() > startDragDistance
        && !windowGlobal.contains(cursorGlobal);
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
