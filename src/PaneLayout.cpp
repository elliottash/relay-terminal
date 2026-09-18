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

PlacementWindow::Response PlacementWindow::keyPress(int key, Qt::KeyboardModifiers modifiers, qint64 nowMs) {
    if (!m_armed) return {};
    // A modifier held down on its own is not yet a keystroke.
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta
        || key == Qt::Key_AltGr || key == Qt::Key_unknown || key == 0)
        return {};
    if (!armed(nowMs)) { m_armed = false; return {Action::Dismiss, Direction::Right}; }
    const auto mods = modifiers & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    // Only a bare arrow places: Alt+Left already focuses the pane to the left, and a shortcut
    // should keep doing what it always does.
    if (mods == Qt::NoModifier) {
        switch (key) {
        case Qt::Key_Left: m_armed = false; return {Action::Place, Direction::Left};
        case Qt::Key_Up: m_armed = false; return {Action::Place, Direction::Up};
        case Qt::Key_Down: m_armed = false; return {Action::Place, Direction::Down};
        // Right means "yes, where it is". The window closes and the key is swallowed, so the
        // pane does not also scroll or move a cursor.
        case Qt::Key_Right: m_armed = false; return {Action::Place, Direction::Right};
        default: break;
        }
    }
    m_armed = false;
    return {Action::Dismiss, Direction::Right};
}

PlacementWindow::Response PlacementWindow::mousePress(qint64 nowMs) {
    Q_UNUSED(nowMs);
    if (!m_armed) return {};
    m_armed = false;
    return {Action::Dismiss, Direction::Right};
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

QList<QPointer<QSplitter>> enclosingSplitters(QWidget *pane) {
    QList<QPointer<QSplitter>> splitters;
    for (QWidget *widget = pane ? pane->parentWidget() : nullptr; widget; widget = widget->parentWidget())
        if (auto *splitter = qobject_cast<QSplitter *>(widget)) splitters.append(splitter);
    return splitters;
}

void restoreSizes(const QList<QPointer<QSplitter>> &splitters, const QList<QList<int>> &sizes) {
    for (int i = 0; i < splitters.size() && i < sizes.size(); ++i) {
        QSplitter *splitter = splitters.at(i);
        if (!splitter || splitter->count() != sizes.at(i).size()) continue;
        splitter->setSizes(sizes.at(i));
    }
}

}  // namespace relay::panes
