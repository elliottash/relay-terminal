// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::panes: the layout decisions behind Alt+arrow (focus a neighbour), Ctrl+Alt+arrow (move
// the focused pane) and dragging a pane's ⠿ grip onto another pane's edge.
//
// Geometry and splitter order only, with no terminal and no window, so the decisions can be
// tested on their own (tests/panelayout_test.cpp). RelayWindow in src/main.cpp is the only
// caller; it maps its leaf widgets onto these functions rather than repeating the rules.
// See issues/changes/needs_qa_llm/2026-09-17-pane-move-keys-and-drag-broken.md.
#pragma once

#include <QFontMetrics>
#include <QList>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <Qt>

#include <optional>

class QLayout;
class QSplitter;
class QWidget;

namespace relay::panes {

enum class Direction { Left, Right, Up, Down };

// Left and Up run toward the start of a splitter, Right and Down toward its end.
bool towardStart(Direction direction);

// The orientation a move or a split in this direction happens along.
Qt::Orientation orientationFor(Direction direction);

// Where a pane Relay opens *for* another one goes (card #QVGQ): Sessions & Projects, Settings, a
// file, Review, the Info pane, a fork — everything that docks beside the pane it was opened from,
// as opposed to a split, move or drag the person aimed. One rule for all of them: to the right,
// like the new-pane button (#803C), and beneath only when the anchor is narrower than two usable
// panes side by side, where a split to the right would leave two slivers.
inline constexpr int kDockBesideMinWidth = 600;
Qt::Orientation dockOrientation(int anchorWidth);

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
// subagent badge and the ssh / phone / usage chips between them (PaneChrome puts those at the
// front of the row). With everything on it wants about 360 px, and a pane in a three-pane row has
// far less. So the elements give way in one fixed order, and when the pane widens again they come
// back in exactly the reverse one:
//
//   1. the directory elides from the left down to its legible floor, and then goes — below that
//      floor "…/x" says nothing, so no path is better than a stub;
//   2. the title elides (ElideRight) down to its own floor;
//   3. the ssh chip is squeezed to its 150 px floor (eliding user@host in the middle), then drops
//      the `user@` and shows the host alone — below its floor, which is what rung 3 is for — and
//      below the host's own width elides the host with a whole ellipsis;
//   4. the usage chip collapses to CPU alone: no memory half and no separator;
//   5. nothing else gives. The subagent badge stays whole and the state glyph stays, always.
//
// One thing gives way outside the ladder (owner, 2026-09-20): a title that would elide may
// instead cross a second line — twoLineTitle() splits it at the spaces the granted width can
// hold, and the header grows downward by one line. The ladder itself stays width-only and never
// hears about the wrap, so its answer cannot chase the header's height.
//
// The live state's word used to be rung 3, between the title and the ssh chip. Card #0STR took
// the word off the header altogether (owner, 2026-09-19: "clean up the headers of tabs and panes.
// they are busy") — the glyph beside it blinks and its tooltip spells the state out, and the
// sentence lives on the busy line above the prompt box — so the rung is gone with it rather than
// left in as a form nothing ever asks for.
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

enum class SshForm { UserAndHost, HostOnly };
enum class UsageForm { CpuAndMemory, CpuOnly };

// What each element shows and how much room it is given. The px values are what the caller hands
// each widget as its width: their sum plus `HeaderWants::fixed` and the elements that cannot give
// way is at most `headerWidth`, unless `shortfall` says the header is narrower than what stays.
struct HeaderFit {
    int title = 0;            // px the title may elide (ElideRight) into; 0 only when there is none
    int directory = 0;        // px the directory may elide (ElideLeft) into; 0 means do not show it
    SshForm ssh = SshForm::UserAndHost;
    int sshPx = 0;            // px for the ssh chip, which elides its text into them; 0 when absent
    UsageForm usage = UsageForm::CpuAndMemory;
    int usagePx = 0;          // px for the usage chip; 0 when absent
    int shortfall = 0;        // px by which what cannot give way still exceeds the header
};

HeaderFit headerFit(int headerWidth, const HeaderWants &wants);

// The title split onto two lines that fit `px`, or an empty list when a split does not help.
// Greedy, at spaces: the first line takes as many whole words as the granted width holds, and the
// rest goes on the second, elided when it will not fit whole — so two lines always show at least
// as much of the title as the one elided line they replace. A title with nowhere to break (a
// path, one long word) or one that fits `px` whole returns empty and stays on its single line.
QStringList twoLineTitle(const QString &title, const QFontMetrics &metrics, int px);

// ----- the claims chip before the title (#0FBB) -------------------------------------------------
//
// The header names the Board cards this pane is working: the running turn's cards first (the
// one the prompt's `#id` or the Board's Run handed it, #C7PF), then the cards the pane's agent has
// claimed, newest claim first, each id once. The chip's text is the first of them, and the count
// of all of them in parentheses when there is more than one: "#K7Q2", "#K7Q2 (3)". One id says
// nothing about a count, so it carries none.
QStringList claimChipCards(const QStringList &turnCards, const QStringList &claimed);
QString claimChipText(const QStringList &cards);
// What a screen reader says for the chip: the count spelled out and the latest id, and that it
// opens the list — "Claimed cards: #K7Q2 and 2 more. Opens the list."
QString claimChipAccessibleName(const QStringList &cards);

// The edge of a pane that a drop at `local` (a point inside a pane of `size`) belongs to: the
// nearest edge wins. Dropping just past the divider between two panes therefore names the edge
// they already share, and the dragged pane keeps its place.
Direction dropEdge(const QPoint &local, const QSize &size);

// Every splitter above `pane`, innermost first. Showing or hiding something inside a pane changes
// that pane's minimum size, and a splitter that cannot satisfy every minimum redistributes all of
// its children as soon as one of those minimums moves: that is how taking control of the terminal
// (Ctrl+H, which hides the prompt box) shrank a pane in a three-pane row to almost nothing.
QList<QPointer<QSplitter>> enclosingSplitters(QWidget *pane);

// Every splitter inside `root`'s widget tree: `root` itself if it is one, then each splitter
// nested inside it, depth first in splitter-child order. `enclosingSplitters` walks up from one
// pane to the splitters around it; this walks down from a whole tab, so equalizing every entry
// tiles the page evenly at every level a drag could have left uneven, not just one splitter.
QList<QPointer<QSplitter>> splittersIn(QWidget *root);

// ----- how space moves when the panes in a tab change (card #QVGQ) -----------------------------
//
// Adding spreads, rearranging keeps, closing gives back in proportion:
//
//   * ADDING a pane — a new terminal (#EQM2), anything opened for a pane (Sessions, Settings, a
//     file, Review, Info, a fork: RelayWindow::dockBeside), Execute / Verify / Try it beside the
//     Switchboard, and the ← ↑ ↓ that finishes placing a new pane — puts it in with
//     sizesAfterDock, then equalizes the whole tab (sizesAfterEqualize, so a Switchboard keeps
//     its floor). RelayWindow::spreadAfterAdding is the one call that does it.
//   * REARRANGING — the move keys, dragging a pane, docking beneath a neighbour — keeps every
//     size the person set: only the anchor's share is divided (sizesAfterDock below), or one
//     equal share of the page for a pane moved past its edge (sizesAfterEdgeDock).
//   * CLOSING hands the freed space to the panes left in that splitter in proportion to their
//     sizes (QSplitter's own behaviour, measured: 596:298 → 797:399), so a hand-set ratio stays.
//     Restoring a closed pane puts back the sizes recorded when it closed.
//   * Alt+0 (pane.equalize) is the spread on demand.

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
//
// `anchorFloor` (card #BXCN) is a width the anchor is owed — a Switchboard pane's list/card split
// (relay::board::kCardSplitWidth). The newcomer still takes half the anchor's share, but never
// more than the anchor can spare and stay above its floor; the half it could not take from the
// anchor it takes from the panes beside it, proportionally, so docking beside the board narrows
// them and not the board's split. 0 — the default, and what every caller that is not Execute or
// Verify passes — divides the anchor's share alone and behaves exactly as before.
QList<int> sizesAfterDock(const QList<int> &sizes, int anchorIndex, int anchorFloor = 0);

// The sizes for Alt+0 (Ctrl+Alt+0 before 2026-09-20), "every splitter in this tab back to equal
// shares": equal shares,
// except that an entry whose floor exceeds its equal share is pinned at the floor and the
// remainder is divided equally among the rest — so "equalize panes" gives a Switchboard pane the
// width its list/card split needs (card #BXCN) and the other panes share what is left. `floors`
// is one entry per size, 0 for a pane owed nothing (and all-zero in a vertical splitter, where
// the floor is not a height). When the floors cannot be afforded — their sum eats the total —
// the answer is plain equal shares, today's behaviour, because the tidy grid is still better
// than a page the floors have squeezed out of shape. Same contract otherwise: the list has as
// many entries as it is given and sums to the same total, ready for setSizes().
QList<int> sizesAfterEqualize(const QList<int> &sizes, const QList<int> &floors);


// Put sizes recorded from enclosingSplitters() back. A splitter that has gone away, or whose
// children changed in between so the sizes no longer describe it, is skipped.
// True when `pane` already fills the page across the direction of travel — the page's full
// height for a left/right move, its full width for an up/down one — so moving it past that
// edge would only hand it what it already has alone. The caller asks once the neighbour search
// has come back empty, which is the other half of "nothing to change": the top pane of a stack
// reaches the page's right edge too, but it does not span the page, so a column of its own is
// still a move.
bool fillsTheEdge(const QRect &pane, const QRect &page, Direction direction);

// The sizes for a pane moved past the page's edge, at the start or the end of the root
// splitter it joins: the newcomer takes one equal share of the whole splitter — the page's
// top-level regions plus itself — and the panes already there keep their relative sizes, so a
// column pulled out of a two-wide page leaves three thirds, and a hand-set wide column stays
// proportionally wide. `sizesAfterDock`'s twin for the one dock that has no neighbour whose
// share could be halved: the pane leaves the page's edge, not a pane beside it. An empty
// answer means there is nothing to divide (a splitter not laid out yet), and the caller keeps
// whatever sizes Qt chose for the insert.
QList<int> sizesAfterEdgeDock(const QList<int> &sizes, bool atStart);

void restoreSizes(const QList<QPointer<QSplitter>> &splitters, const QList<QList<int>> &sizes);
// The short window after "new pane" put a pane on the right, during which Left, Up or Down
// re-dock that pane to the named side instead (issue #78BN). Right keeps it where it is, and
// anything else — another key, a click, or two seconds passing — closes the window and behaves
// normally, so arrows typed a moment later still reach the shell or the prompt box. Ctrl may
// still be held from the split key (card #JXWT): see `keyPress` for how far that goes.
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
    //
    // A bare arrow places the pane, and so does one with Ctrl still held (card #JXWT): the hand
    // that typed Ctrl+E has not let go yet, and Shift may ride along — Ctrl+Shift+E is the twin
    // a program cannot swallow, and it leaves both down. But only while the keymap leaves that
    // chord free: `boundAction` is the action the keymap binds to this exact key chord, empty
    // when none, and a bound shortcut keeps doing what it always does — Alt+Left focuses the
    // pane to the left, Ctrl+Alt+arrow moves one, and the konsole preset's Ctrl+Shift+Down
    // focuses below.
    Response keyPress(int key, Qt::KeyboardModifiers modifiers, qint64 nowMs, const QString &boundAction);
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


// ----- what a pane's minimum width is made of (card #SDXE) ------------------------------------
//
// A pane grows when its MINIMUM grows. The splitters are built with setChildrenCollapsible(false),
// so a splitter must satisfy every child's minimum size: the moment one child's minimum passes the
// width it has, the splitter widens that child and takes the pixels from its neighbours. A widget
// whose minimum follows the text it is showing therefore drags the dividers about while an agent
// works — the header's usage meter re-measures "cpu 7%" against "cpu 100%" two and a half times a
// second — and nothing on screen says why. That is the jiggle card #SDXE reports.
//
// The rule below is the one a QHBoxLayout applies to each of its children (Qt's qSmartMinSize):
// the size POLICY decides whether the size hint or the minimum size hint is the floor, and an
// explicit minimumWidth() replaces whatever that came to. It is worth having written down because
// the answer surprises: a QSizePolicy::Fixed widget cannot shrink, so its sizeHint() *is* its
// minimum and overriding minimumSizeHint() on it changes nothing at all. Every header widget whose
// hint follows its text must therefore either shrink (Maximum, Preferred, Ignored) with a
// content-free minimumSizeHint(), or carry an explicit minimumWidth().
//
// A hidden widget is not asked this question by its layout at all; that is the caller's test to
// make, so that this one is about the policy and nothing else.
int layoutMinimumWidth(const QWidget *widget);

// The minimum width every visible child of `row` contributes, as one line:
//   pane "agent" w=612 min=498 | paneStateGlyph=16 paneUsageChip=94 paneTitle=286 …
// Printed for every pane about once a second while RELAY_LAYOUT_LOG is set in the environment,
// which is how the numbers in docs/qa_evidence/2026-09-20-pane-width-jiggle were taken: run it
// before and after a change and a minimum that follows content shows up as a column that moves.
// `paneWidth` and `paneMinimum` are the pane's own, so the line says whether the header is what is
// holding the pane open.
QString headerMinimumsLine(const QString &pane, int paneWidth, int paneMinimum, const QLayout *row);

// True while RELAY_LAYOUT_LOG is set. Read once: a diagnostic that costs a getenv per pane per
// frame is one nobody leaves in.
bool layoutLogEnabled();

}  // namespace relay::panes
