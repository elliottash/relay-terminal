// SPDX-License-Identifier: GPL-3.0-or-later
// relay::panes: the layout decisions behind Alt+arrow (focus a neighbour), Ctrl+Alt+arrow (move
// the focused pane) and dragging a pane's ⠿ grip onto another pane's edge.
//
// Geometry and splitter order only, with no terminal and no window, so the decisions can be
// tested on their own (tests/panelayout_test.cpp). RelayWindow in src/main.cpp is the only
// caller; it maps its leaf widgets onto these functions rather than repeating the rules.
// See issues/changes/needs_qa_llm/2026-09-17-pane-move-keys-and-drag-broken.md.
#pragma once

#include <QList>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QSize>

class QSplitter;
class QWidget;

namespace relay::panes {

enum class Direction { Left, Right, Up, Down };

// Left and Up run toward the start of a splitter, Right and Down toward its end.
bool towardStart(Direction direction);

// The orientation a move or a split in this direction happens along.
Qt::Orientation orientationFor(Direction direction);

// The pane on `direction`'s side of `from` that Alt+arrow should focus and Ctrl+Alt+arrow should
// move past: the nearest one, then the most aligned across the direction of travel. Returns an
// index into `candidates` (all in the same coordinate system as `from`), or -1 when that side is
// empty. `from` itself must not be in `candidates`.
int neighborIndex(const QRect &from, const QList<QRect> &candidates, Direction direction);

// Swap two panes that sit side by side in the splitter they share.
//
// QSplitter::insertWidget MOVES a child the splitter already owns, and numbers the target index
// as if that child had been taken out first, so inserting `current` at the neighbour's index is
// the whole swap in either direction. Re-inserting the neighbour afterwards to "finish" a move
// toward the end puts both back where they started, which is what made Ctrl+Alt+Right and
// Ctrl+Alt+Down do nothing. Pane sizes are kept.
void swapInSplitter(QSplitter *splitter, QWidget *current, QWidget *neighbor);

// The edge of a pane that a drop at `local` (a point inside a pane of `size`) belongs to: the
// nearest edge wins. Dropping just past the divider between two panes therefore names the edge
// they already share, and the dragged pane keeps its place.
Direction dropEdge(const QPoint &local, const QSize &size);

// Every splitter above `pane`, innermost first. Showing or hiding something inside a pane changes
// that pane's minimum size, and a splitter that cannot satisfy every minimum redistributes all of
// its children as soon as one of those minimums moves: that is how taking control of the terminal
// (Ctrl+H, which hides the prompt box) shrank a pane in a three-pane row to almost nothing.
QList<QPointer<QSplitter>> enclosingSplitters(QWidget *pane);

// Put sizes recorded from enclosingSplitters() back. A splitter that has gone away, or whose
// children changed in between so the sizes no longer describe it, is skipped.
void restoreSizes(const QList<QPointer<QSplitter>> &splitters, const QList<QList<int>> &sizes);

}  // namespace relay::panes
