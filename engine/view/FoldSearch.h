// SPDX-License-Identifier: GPL-3.0-or-later
// relay::FoldSearch: find inside expanded folds, merged with the core's own
// matches into one sequence in visual order.
//
// The cores search real rows and know nothing about the view's fold layer, so
// with a fold open a find has two sources of matches:
//
//   * the core's, over real rows, stepped with VtCore::searchStep() and
//     reported back by VtCore::searchCurrentRow() (an absolute real row),
//   * this class's, over the text of every **expanded** fold, found in the
//     fold's *logical* lines so a match that straddles the block's wrap is
//     still one match.
//
// Both are ordered by the visual row they are painted on -- the coordinates
// FoldLayer maps -- so the user walks them in the order the eye reads them,
// and the index a step returns counts from the newest (0) across both kinds,
// exactly as a core's own does.
//
// ## Why the core is *parked*, never stepped and undone
//
// Neither core can enumerate its matches (libghostty-vt answers "how many" and
// "which one is selected", not "all of them"), and asking for them by stepping
// the core over every match would be O(matches) FFI calls per needle. So the
// merge keeps **one match of look-ahead**: it steps the core once, and while
// the fold matches that lie between the previous position and that core match
// have not been visited yet it leaves the core parked where it is and walks
// the fold matches on its own. Consuming the parked match is then free.
//
// That gives exactly one VtCore::searchStep() per core match visited -- a step
// is never made and then undone, which no core promises to be exact -- and the
// core's own cyclic order (…, i-1, i, i+1, … wrapping) is the same cyclic
// order the merged walk needs, in both directions, including the wrap. When
// the walk reverses direction the parked match ends up on the wrong side and
// one step in the new direction puts it back on the right one.
//
// While the current match is a fold match the core still paints its own parked
// match as "the selected one"; the view drops that flag (TerminalView::
// paintRow) so only one match anywhere is ever highlighted as current.
//
// Nothing here touches Qt's GUI classes, so the ordering and the indices are
// unit tested against a fake core (engine/tests/FoldSearchTest.cpp).
#pragma once

#include "FoldLayer.h"

#include <QString>

#include <functional>
#include <vector>

namespace relay {

class FoldSearch {
public:
    // One match inside a fold: cells [from, to) of one *logical* line, so a
    // match that straddles the block's wrap is a single match that happens to
    // be painted on two rows.
    struct Match {
        int fold = 0; // index into FoldLayer::folds()
        int line = 0; // logical line index
        int from = 0;
        int to = 0;
    };

    // Where a match sits for ordering: its visual row, and the cell offset
    // inside that row. Fold rows and real rows never share a visual row, so
    // the column only ever separates two matches of the same kind.
    struct Key {
        int row = -1;
        int col = 0;
    };

    // The core match the core is currently selected on.
    struct CorePos {
        bool valid = false;
        int row = 0;   // absolute real row (VtCore::searchCurrentRow())
        int index = 0; // counted from the newest (0), what searchStep() returns
    };

    // What the view lends this class of the core it cannot see.
    struct Core {
        int count = 0;
        // Step one match towards older (backwards) or newer content and report
        // where the core landed. Called at most once per step().
        std::function<CorePos(bool backwards)> step;
        // The absolute row of the core's selected match, or -1: re-read before
        // a parked match is reused, so a trimmed scrollback cannot leave the
        // merge comparing against a row that has moved.
        std::function<int()> currentRow;
    };

    struct Step {
        enum Kind { None, Core, Fold };
        Kind kind = None;
        int match = -1;     // kind == Fold: index into matches()
        int index = -1;     // counted from the newest (0), across both kinds
        int count = 0;      // core matches + fold matches
        int visualRow = -1; // the row to scroll into view, or -1
    };

    // One match's cells on one wrapped fold row, for painting.
    struct RowMatch {
        int from = 0; // cell index in the logical line, clipped to the row
        int to = 0;
        bool current = false;
    };

    // ---- the needle
    // Setting a different needle forgets the matches and the cursor. Empty
    // means no fold matches at all, which is how a view without a fold
    // behaves.
    void setNeedle(const QString &needle);
    const QString &needle() const { return m_needle; }
    // The folds, their content, their wrap or their anchors moved: recompute
    // on the next question, not now.
    void invalidate() { m_dirty = true; }
    void reset(); // forget the needle, the matches and the cursor

    // ---- matches
    int matchCount(const FoldLayer &layer) const;
    const std::vector<Match> &matches(const FoldLayer &layer) const;
    std::vector<RowMatch> rowMatches(const FoldLayer &layer, int foldIndex, int foldRow) const;

    // ---- the cursor
    void resetCursor();
    bool currentIsFold() const { return m_cur.valid && m_cur.kind == Step::Fold; }
    int index() const { return m_cur.valid ? m_cur.index : -1; }

    // Walk one match towards older content (backwards) or newer, wrapping.
    Step step(const FoldLayer &layer, bool backwards, const Core &core);

    // The visual position of a fold match; row < 0 when its fold is shut,
    // unanchored or empty.
    Key keyOf(const FoldLayer &layer, const Match &m) const;
    static bool keyLess(const Key &a, const Key &b)
    {
        return a.row != b.row ? a.row < b.row : a.col < b.col;
    }

private:
    void refresh(const FoldLayer &layer) const;
    int nextFoldMatch(const FoldLayer &layer, bool backwards) const;
    int foldMatchesNewerThan(const FoldLayer &layer, const Key &k) const;
    bool ahead(const Key &k, bool backwards) const;

    struct Cursor {
        bool valid = false;
        Step::Kind kind = Step::None;
        Key key;
        int match = -1;
        int index = -1;
    };

    QString m_needle;
    mutable std::vector<Match> m_matches; // visual order, oldest first
    mutable bool m_dirty = true;
    mutable Cursor m_cur;
    mutable CorePos m_parked;
};

} // namespace relay
