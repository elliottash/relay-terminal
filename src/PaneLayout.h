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
#include <QRect>
#include <QSize>
#include <Qt>

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

// The short window after "new pane" put a pane on the right, during which Left, Up or Down
// re-dock that pane to the named side instead (issue #78BN). Right keeps it where it is, and
// anything else — another key, a click, or two seconds passing — closes the window and behaves
// normally, so arrows typed a moment later still reach the shell or the prompt box.
//
// No widgets and no timers: the caller passes a monotonic clock in milliseconds, so the rules
// can be tested on their own (tests/panelayout_test.cpp).
class PlacementWindow {
public:
    // How long a new pane may still be placed with an arrow.
    static constexpr qint64 kTimeoutMs = 2000;

    enum class Action {
        None,     // the window was not open; the caller does nothing special
        Place,    // re-dock the new pane on `direction`; the caller consumes the key
        Dismiss,  // the window just closed; the caller does NOT consume the key
    };
    struct Response {
        Action action = Action::None;
        Direction direction = Direction::Right;
    };

    // Arm the window; `nowMs` is a monotonic millisecond clock.
    void arm(qint64 nowMs) { m_armed = true; m_since = nowMs; }
    void cancel() { m_armed = false; }
    bool armed(qint64 nowMs) const { return m_armed && nowMs - m_since < kTimeoutMs; }

    // A key press anywhere in the window. Modifier-only presses (Ctrl, Shift, Alt, Meta, AltGr)
    // keep the window open, because they are the first half of a shortcut, not "any other key".
    Response keyPress(int key, Qt::KeyboardModifiers modifiers, qint64 nowMs);
    // Any mouse press closes the window. Never consumes the click.
    Response mousePress(qint64 nowMs);

private:
    bool m_armed = false;
    qint64 m_since = 0;
};

}  // namespace relay::panes
