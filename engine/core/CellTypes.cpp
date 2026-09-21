// SPDX-License-Identifier: AGPL-3.0-or-later
#include "CellTypes.h"

#include <algorithm>

namespace relay {

int Line::cellCodepoints(const Cell &c, std::u32string *out) const
{
    if (c.ch == kWideTail)
        return 0;
    if (c.attrs & AttrCluster) {
        const size_t off = size_t(c.ch);
        if (off >= clusters.size())
            return 0;
        const int n = int(clusters[off]);
        out->append(clusters, off + 1, size_t(n));
        return n;
    }
    if (c.ch == 0)
        return 0;
    out->push_back(c.ch);
    return 1;
}

QString Line::cellText(const Cell &c) const
{
    if (c.ch == kWideTail)
        return QString();
    if (c.ch == 0 && !(c.attrs & AttrCluster))
        return QStringLiteral(" ");
    std::u32string cps;
    cellCodepoints(c, &cps);
    return QString::fromUcs4(cps.data(), int(cps.size()));
}

void Line::appendCluster(Cell *c, const char32_t *cps, int n)
{
    if (n <= 0) {
        c->ch = 0;
        return;
    }
    if (n == 1) {
        c->ch = cps[0];
        c->attrs &= uint16_t(~AttrCluster);
        return;
    }
    c->ch = char32_t(clusters.size());
    c->attrs |= AttrCluster;
    clusters.push_back(char32_t(n));
    clusters.append(cps, size_t(n));
}

namespace {

// Codepoints of columns [from, to) of `l`, read exactly as the grid paints them: a column the
// terminal never wrote to reads as one space, and the right half of a double-width cell adds
// nothing of its own.
std::u32string rowCodepoints(const Line &l, int from, int to)
{
    std::u32string cps;
    cps.reserve(size_t(std::max(0, to - from)));
    for (int i = std::max(0, from); i < to; ++i) {
        const Cell &c = l.cells[size_t(i)];
        if (c.ch == kWideTail)
            continue;
        if (c.ch == 0 && !(c.attrs & AttrCluster)) {
            cps.push_back(U' ');
            continue;
        }
        l.cellCodepoints(c, &cps);
    }
    return cps;
}

} // namespace

QString Line::text(int from, int to) const
{
    if (to < 0 || to > int(cells.size()))
        to = int(cells.size());
    std::u32string cps = rowCodepoints(*this, from, to);
    while (!cps.empty() && cps.back() == U' ')
        cps.pop_back();
    return QString::fromUcs4(cps.data(), int(cps.size()));
}

// Copying a selection across a soft wrap joins the two rows with nothing, so whatever stands in
// this row's last column is all that separates its last word from the next row's first word.
// text()'s trim ate that character, which is how "foo " + "bar" came out of the clipboard as
// "foobar" (#8SBD). Columns nobody printed to are still dropped: a row that wrapped because a
// double-width character would not fit ends in a blank the terminal never wrote, and turning
// that into a space would break the wrapped word the other way.
QString Line::untrimmedText(int from, int to) const
{
    if (to < 0 || to > int(cells.size()))
        to = int(cells.size());
    from = std::max(0, from);
    while (to > from) {
        const Cell &c = cells[size_t(to - 1)];
        if (c.ch != 0 || (c.attrs & AttrCluster))
            break;
        --to;
    }
    const std::u32string cps = rowCodepoints(*this, from, to);
    return QString::fromUcs4(cps.data(), int(cps.size()));
}

} // namespace relay
