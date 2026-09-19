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
#include <QString>
#include <Qt>

#include <optional>

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

// ----- the pane header: the order in which it gives way (owner, 2026-09-19) --------------------
//
// A pane header is the title on the left and the directory on the right, with the state glyph, the
// state's word, the subagent badge and the ssh / phone / usage chips between them (PaneChrome puts
// those at the front of the row). With everything on it wants about 470 px, and a pane in a
// three-pane row has far less. So the elements give way in one fixed order, and when the pane
// widens again they come back in exactly the reverse one:
//
//   1. the directory elides from the left down to its legible floor, and then goes — below that
//      floor "…/x" says nothing, so no path is better than a stub;
//   2. the title elides (ElideRight) down to its own floor;
//   3. the state word goes to its short form (`relay::panestatus::stateLabelShort`: "Running",
//      "Subagents") and then goes, leaving the glyph, which says it too;
//   4. the ssh chip is squeezed to its 150 px floor (eliding user@host in the middle), then drops
//      the `user@` and shows the host alone — below its floor, which is what rung 4 is for — and
//      below the host's own width elides the host with a whole ellipsis;
//   5. the usage chip collapses to CPU alone: no memory half and no separator;
//   6. nothing else gives. The subagent badge stays whole and the state glyph stays, always.
//
// Nothing is ever drawn as a partial glyph or cut mid-letter: each step either elides by whole
// glyphs or leaves its element out. Both labels are therefore elided by hand rather than left to
// the layout, which clips a squeezed QLabel mid-glyph — that is how a crowded header came to end
// in a stray half of a character instead of a path.
//
// The whole ladder is this one function, and it reads nothing but the header's width and the
// natural widths of the elements. In particular it never reads what an element is showing *now*:
// the chips are painted widgets whose sizeHint follows the form this function chose, so an answer
// that depended on the current form would feed itself, and the header would flicker between two
// rungs for ever. `Pane::updateHeader()` applies the answer, Qt lays the row out again, and asks
// again — the second answer has to be the first one. Tests: tests/panelayout_test.cpp.
inline constexpr int kTitleFloorPx = 80;       // never elide a title into less than this
inline constexpr int kDirectoryFloorPx = 56;   // below this "…/x" says nothing; show no path
inline constexpr int kSshFloorPx = 150;        // a chip still showing user@ is not squeezed below this

// What each element would take if it had the room, in pixels, as the row measures it now. 0 means
// the element is not in this header at all. The caller folds the row's spacing, its margins and
// the room the hover button row is kept clear of into `fixed`.
struct HeaderWants {
    int title = 0;            // the title's full text
    int directory = 0;        // the directory line's full text
    int stateWord = 0;        // the live state's word, long form ("Subagents working")
    int stateWordShort = 0;   // the same word, short form ("Subagents")
    int ssh = 0;              // the ssh chip showing "⇄ user@host"
    int sshHost = 0;          // the same chip showing the host alone
    int sshEllipsis = 0;      // the same chip with the host elided to one ellipsis: its hard floor
    int usage = 0;            // the usage chip, CPU and memory
    int usageCpu = 0;         // the same chip, CPU alone
    int glyph = 0;            // the state glyph, which never gives way
    int badge = 0;            // the subagent badge, which is whole or absent
    int chips = 0;            // the phone / sharing chip and anything else that never shrinks
    int fixed = 0;            // margins, the row's spacing, the button row's room
};

enum class WordForm { Full, Short, Hidden };
enum class SshForm { UserAndHost, HostOnly };
enum class UsageForm { CpuAndMemory, CpuOnly };

// What each element shows and how much room it is given. The px values are what the caller hands
// each widget as its width: their sum plus `HeaderWants::fixed` and the elements that cannot give
// way is at most `headerWidth`, unless `shortfall` says the header is narrower than what stays.
struct HeaderFit {
    int title = 0;            // px the title may elide (ElideRight) into; 0 only when there is none
    int directory = 0;        // px the directory may elide (ElideLeft) into; 0 means do not show it
    WordForm word = WordForm::Hidden;
    int wordPx = 0;           // px for the state word; 0 when it is not shown
    SshForm ssh = SshForm::UserAndHost;
    int sshPx = 0;            // px for the ssh chip, which elides its text into them; 0 when absent
    UsageForm usage = UsageForm::CpuAndMemory;
    int usagePx = 0;          // px for the usage chip; 0 when absent
    int shortfall = 0;        // px by which what cannot give way still exceeds the header
};

HeaderFit headerFit(int headerWidth, const HeaderWants &wants);

// The edge of a pane that a drop at `local` (a point inside a pane of `size`) belongs to: the
// nearest edge wins. Dropping just past the divider between two panes therefore names the edge
// they already share, and the dragged pane keeps its place.
Direction dropEdge(const QPoint &local, const QSize &size);

// Every splitter above `pane`, innermost first. Showing or hiding something inside a pane changes
// that pane's minimum size, and a splitter that cannot satisfy every minimum redistributes all of
// its children as soon as one of those minimums moves: that is how taking control of the terminal
// (Ctrl+H, which hides the prompt box) shrank a pane in a three-pane row to almost nothing.
QList<QPointer<QSplitter>> enclosingSplitters(QWidget *pane);

// ----- docking a pane beside a neighbour (owner, 2026-09-19) -----------------------------------
//
// Inserting a pane into a splitter that already runs that way used to give every child an equal
// share: a wide terminal next to a narrow file pane, and docking a third pane beside the terminal
// made all three the same width — an arrangement the person had made, undone by a keystroke. Only
// the anchor's share is divided now, half to it and half to the newcomer, and every other child
// keeps the size it had. The chord and the drag both dock through RelayWindow::insertBeside, so
// this is the arithmetic for both.
//
// `sizes` is QSplitter::sizes() as it is BEFORE the insertion and `anchorIndex` the anchor's place
// in it; the result has one more entry, in splitter order, summing to the same total and ready for
// setSizes() after the insert. The two halves are floor then ceil, as a fresh two-pane splitter
// divides itself, so which of them is the new pane does not change the numbers.
//
// An empty list means there is nothing to keep: no sizes, an index outside them, or an anchor with
// no room to give (a splitter that has not been laid out yet reads as zeros). The caller then
// falls back rather than writing a size of 0 for the pane it just docked.
QList<int> sizesAfterDock(const QList<int> &sizes, int anchorIndex);

// Put sizes recorded from enclosingSplitters() back. A splitter that has gone away, or whose
// children changed in between so the sizes no longer describe it, is skipped.
void restoreSizes(const QList<QPointer<QSplitter>> &splitters, const QList<QList<int>> &sizes);
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

// ----- "move left/right, then ↓ docks it beneath that neighbour" (card #Q7Y9) ------------------
//
// The chord has no window of its own: it borrows PlacementWindow's two-second clock. These are
// the two decisions in it that are worth deciding without a window (tests/panelayout_test.cpp).

// The move that carries `pane` toward `anchor` when the two sit side by side: Right when the
// anchor lies to the pane's right, Left when it lies to its left, nothing when they overlap.
// A pane dropped beneath the anchor it sat to the RIGHT of is docked there by Move-left; naming
// Move-right, which takes it further away, is what the drag hint used to do.
std::optional<Direction> moveToward(const QRect &pane, const QRect &anchor);

// True when a key press should leave the chord's window armed: a modifier held down on its own,
// or a key the keymap binds to one of the chord's own actions (`actionId`, empty when the key is
// bound to nothing). Everything else closes it — including Ctrl+C, Ctrl+D and Ctrl+L, which are
// shell keys and not "the first half of a shortcut", so a stale chord can never turn a much
// later Move-down into a dock.
bool chordKeyKeepsWindow(int key, const QString &actionId);

}  // namespace relay::panes
