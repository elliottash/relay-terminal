// SPDX-License-Identifier: GPL-3.0-or-later
#include "FoldSearch.h"

#include <algorithm>

namespace relay {

namespace {

bool sameMatch(const FoldSearch::Match &a, const FoldSearch::Match &b)
{
    return a.fold == b.fold && a.line == b.line && a.from == b.from && a.to == b.to;
}

} // namespace

void FoldSearch::setNeedle(const QString &needle)
{
    if (needle == m_needle) {
        m_dirty = true;
        return;
    }
    m_needle = needle;
    m_matches.clear();
    m_dirty = true;
    resetCursor();
}

void FoldSearch::reset()
{
    m_needle.clear();
    m_matches.clear();
    m_dirty = true;
    resetCursor();
}

void FoldSearch::resetCursor()
{
    m_cur = Cursor();
    m_parked = CorePos();
}

// ---------------------------------------------------------------- matches

// Every occurrence of the needle in every expanded fold, in visual order.
//
// A fold's *logical* line is what is searched, not its wrapped rows: the copy
// path already gives a wrapped detail line back as one line, and a user who
// sees a word broken over the block's wrap still means one word. The text is
// built with a code-unit -> cell map, and the matching rule is the one both
// cores use on real rows (Qt's case-insensitive compare, non-overlapping).
void FoldSearch::refresh(const FoldLayer &layer) const
{
    if (!m_dirty)
        return;
    m_dirty = false;
    std::vector<Match> found;
    if (!m_needle.isEmpty()) {
        const std::vector<FoldLayer::Fold> &folds = layer.folds();
        for (int i = 0; i < int(folds.size()); ++i) {
            // A shut, unanchored or empty fold has no visual rows: its text is
            // not on screen and is not searched.
            if (layer.foldVisualStart(i) < 0)
                continue;
            const FoldLayer::Fold &f = folds[size_t(i)];
            for (int line = 0; line < int(f.cells.size()); ++line) {
                const std::vector<FoldLayer::Cell> &cells = f.cells[size_t(line)];
                QString text;
                std::vector<int> cellOf;
                for (int c = 0; c < int(cells.size()); ++c) {
                    const QString &s = cells[size_t(c)].text;
                    for (int k = 0; k < s.size(); ++k)
                        cellOf.push_back(c);
                    text += s;
                }
                int from = 0;
                while ((from = text.indexOf(m_needle, from, Qt::CaseInsensitive)) >= 0) {
                    const int last = from + m_needle.size() - 1;
                    if (last >= int(cellOf.size()))
                        break;
                    found.push_back(Match{i, line, cellOf[size_t(from)], cellOf[size_t(last)] + 1});
                    from += std::max(1, int(m_needle.size()));
                }
            }
        }
        // The folds themselves come in insertion order, so the whole list is
        // put into the order the rows are painted in.
        std::stable_sort(found.begin(), found.end(), [&](const Match &a, const Match &b) {
            return keyLess(keyOf(layer, a), keyOf(layer, b));
        });
    }
    if (found.size() != m_matches.size()
        || !std::equal(found.begin(), found.end(), m_matches.begin(), sameMatch)) {
        m_matches = std::move(found);
        // The set changed under us (a fold opened, its content arrived, the
        // scrollback trimmed one away): where we were no longer means
        // anything, so the next step starts from the newest or the oldest.
        m_cur = Cursor();
        m_parked = CorePos();
    }
}

const std::vector<FoldSearch::Match> &FoldSearch::matches(const FoldLayer &layer) const
{
    refresh(layer);
    return m_matches;
}

int FoldSearch::matchCount(const FoldLayer &layer) const
{
    refresh(layer);
    return int(m_matches.size());
}

FoldSearch::Key FoldSearch::keyOf(const FoldLayer &layer, const Match &m) const
{
    Key k;
    const std::vector<FoldLayer::Fold> &folds = layer.folds();
    if (m.fold < 0 || m.fold >= int(folds.size()))
        return k;
    const int start = layer.foldVisualStart(m.fold);
    if (start < 0)
        return k;
    const FoldLayer::Fold &f = folds[size_t(m.fold)];
    for (int r = 0; r < int(f.rows.size()); ++r) {
        const FoldLayer::Row &row = f.rows[size_t(r)];
        if (row.line != m.line || m.from < row.first || m.from >= row.first + std::max(1, row.count))
            continue;
        k.row = start + r;
        k.col = m.from - row.first;
        return k;
    }
    return k;
}

std::vector<FoldSearch::RowMatch> FoldSearch::rowMatches(const FoldLayer &layer, int foldIndex, int foldRow) const
{
    std::vector<RowMatch> out;
    refresh(layer);
    if (m_matches.empty())
        return out;
    const std::vector<FoldLayer::Fold> &folds = layer.folds();
    if (foldIndex < 0 || foldIndex >= int(folds.size()))
        return out;
    const FoldLayer::Fold &f = folds[size_t(foldIndex)];
    if (foldRow < 0 || foldRow >= int(f.rows.size()))
        return out;
    const FoldLayer::Row &row = f.rows[size_t(foldRow)];
    for (int i = 0; i < int(m_matches.size()); ++i) {
        const Match &m = m_matches[size_t(i)];
        if (m.fold != foldIndex || m.line != row.line)
            continue;
        const int from = std::max(m.from, row.first);
        const int to = std::min(m.to, row.first + row.count);
        if (from < to)
            out.push_back(RowMatch{from, to, m_cur.valid && m_cur.kind == Step::Fold && m_cur.match == i});
    }
    return out;
}

// ---------------------------------------------------------------- the walk

bool FoldSearch::ahead(const Key &k, bool backwards) const
{
    if (!m_cur.valid)
        return true; // nothing visited yet: the whole sequence lies ahead
    return backwards ? keyLess(k, m_cur.key) : keyLess(m_cur.key, k);
}

// The fold match one step towards older (backwards) or newer content, wrapping
// round the ends. -1 when there is none at all.
int FoldSearch::nextFoldMatch(const FoldLayer &layer, bool backwards) const
{
    const int n = int(m_matches.size());
    if (n == 0)
        return -1;
    if (!m_cur.valid)
        return backwards ? n - 1 : 0;
    if (backwards) {
        for (int i = n - 1; i >= 0; --i) {
            if (keyLess(keyOf(layer, m_matches[size_t(i)]), m_cur.key))
                return i;
        }
        return n - 1; // wrapped: the newest
    }
    for (int i = 0; i < n; ++i) {
        if (keyLess(m_cur.key, keyOf(layer, m_matches[size_t(i)])))
            return i;
    }
    return 0; // wrapped: the oldest
}

int FoldSearch::foldMatchesNewerThan(const FoldLayer &layer, const Key &k) const
{
    int n = 0;
    for (const Match &m : m_matches) {
        if (keyLess(k, keyOf(layer, m)))
            ++n;
    }
    return n;
}

FoldSearch::Step FoldSearch::step(const FoldLayer &layer, bool backwards, const Core &core)
{
    refresh(layer);
    Step out;
    out.count = std::max(0, core.count) + int(m_matches.size());
    if (out.count == 0) {
        resetCursor();
        return out;
    }

    // Nothing visited yet, but the core already sits on a match -- a fold was
    // toggled under a live search, say, which forgets the cursor but not the
    // core's own. Continue from there, so the match the core is stepped to
    // below really is the next one in the direction of travel and the index
    // arithmetic stays exact.
    if (!m_cur.valid && core.count > 0 && core.currentRow) {
        const int row = core.currentRow();
        if (row >= 0) {
            m_cur.valid = true;
            m_cur.kind = Step::Core;
            m_cur.key = Key{layer.visualOfReal(row), 0};
        }
    }

    // ---- the core's candidate: the match it is parked on when that still
    // lies ahead in the direction of travel, otherwise one step further.
    if (core.count > 0 && core.step) {
        if (m_parked.valid && core.currentRow) {
            const int row = core.currentRow();
            if (row < 0)
                m_parked.valid = false;
            else
                m_parked.row = row; // the scrollback may have trimmed under it
        }
        if (!m_parked.valid || !ahead(Key{layer.visualOfReal(m_parked.row), 0}, backwards))
            m_parked = core.step(backwards);
    } else {
        m_parked = CorePos();
    }
    const bool haveCore = m_parked.valid;
    const Key coreKey = haveCore ? Key{layer.visualOfReal(m_parked.row), 0} : Key();

    // ---- the folds' candidate
    const int f = nextFoldMatch(layer, backwards);
    const bool haveFold = f >= 0;
    const Key foldKey = haveFold ? keyOf(layer, m_matches[size_t(f)]) : Key();

    if (!haveCore && !haveFold) {
        resetCursor();
        return out;
    }

    // ---- which of the two comes first. One that has not wrapped round the
    // end of the sequence always wins; between two of the same kind it is the
    // nearer one, which going backwards is the newer of the two.
    bool takeFold;
    if (!haveCore) {
        takeFold = true;
    } else if (!haveFold || foldKey.row < 0) {
        takeFold = false;
    } else {
        const bool foldWrapped = !ahead(foldKey, backwards);
        const bool coreWrapped = !ahead(coreKey, backwards);
        if (foldWrapped != coreWrapped)
            takeFold = !foldWrapped;
        else
            takeFold = backwards ? keyLess(coreKey, foldKey) : keyLess(foldKey, coreKey);
    }

    // ---- the index, counted from the newest across both kinds.
    //
    // `m_parked.index` is how many core matches are newer than the parked one,
    // so for a fold match it is also how many core matches are newer than the
    // fold match -- plus the parked one itself when the walk left it on the
    // newer side (which is where a forwards walk parks it).
    if (takeFold) {
        const int coreNewer = haveCore ? m_parked.index + (keyLess(foldKey, coreKey) ? 1 : 0) : 0;
        out.kind = Step::Fold;
        out.match = f;
        out.index = foldMatchesNewerThan(layer, foldKey) + coreNewer;
        out.visualRow = foldKey.row;
        m_cur = Cursor{true, Step::Fold, foldKey, f, out.index};
    } else {
        out.kind = Step::Core;
        out.index = m_parked.index + foldMatchesNewerThan(layer, coreKey);
        out.visualRow = coreKey.row;
        m_cur = Cursor{true, Step::Core, coreKey, -1, out.index};
    }
    return out;
}

} // namespace relay
