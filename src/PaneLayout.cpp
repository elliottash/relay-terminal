// SPDX-License-Identifier: GPL-3.0-or-later
#include "PaneLayout.h"

#include <QSplitter>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace relay::panes {

bool towardStart(Direction direction) { return direction == Direction::Left || direction == Direction::Up; }

Qt::Orientation orientationFor(Direction direction) {
    return direction == Direction::Left || direction == Direction::Right ? Qt::Horizontal : Qt::Vertical;
}

int neighborIndex(const QRect &from, const QList<QRect> &candidates, Direction direction) {
    // A few pixels of slack, so a pane that starts level with this one still counts as beside it.
    constexpr int slack = 4;
    int best = -1;
    double bestScore = 0;
    for (int i = 0; i < candidates.size(); ++i) {
        const QRect &to = candidates.at(i);
        double gap = 0, offset = 0;
        switch (direction) {
        case Direction::Right:
            if (to.left() < from.right() - slack) continue;
            gap = to.left() - from.right();
            offset = std::abs(to.center().y() - from.center().y());
            break;
        case Direction::Left:
            if (to.right() > from.left() + slack) continue;
            gap = from.left() - to.right();
            offset = std::abs(to.center().y() - from.center().y());
            break;
        case Direction::Down:
            if (to.top() < from.bottom() - slack) continue;
            gap = to.top() - from.bottom();
            offset = std::abs(to.center().x() - from.center().x());
            break;
        case Direction::Up:
            if (to.bottom() > from.top() + slack) continue;
            gap = from.top() - to.bottom();
            offset = std::abs(to.center().x() - from.center().x());
            break;
        }
        // The nearest pane wins; among equally near ones, the one most in line with this pane.
        const double score = std::max(0.0, gap) * 4 + offset;
        if (best < 0 || score < bestScore) { bestScore = score; best = i; }
    }
    return best;
}

void swapInSplitter(QSplitter *splitter, QWidget *current, QWidget *neighbor) {
    if (!splitter || !current || !neighbor) return;
    const int to = splitter->indexOf(neighbor);
    if (to < 0 || splitter->indexOf(current) < 0) return;
    const QList<int> sizes = splitter->sizes();
    splitter->insertWidget(to, current);
    splitter->setSizes(sizes);
}

Direction dropEdge(const QPoint &local, const QSize &size) {
    const double fx = double(local.x()) / std::max(1, size.width());
    const double fy = double(local.y()) / std::max(1, size.height());
    const double left = fx, right = 1 - fx, top = fy, bottom = 1 - fy;
    const double nearest = std::min({left, right, top, bottom});
    if (nearest == left) return Direction::Left;
    if (nearest == right) return Direction::Right;
    if (nearest == top) return Direction::Up;
    return Direction::Down;
}

}  // namespace relay::panes
