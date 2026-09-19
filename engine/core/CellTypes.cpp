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

QString Line::text(int from, int to) const
{
    if (to < 0 || to > int(cells.size()))
        to = int(cells.size());
    std::u32string cps;
    cps.reserve(size_t(std::max(0, to - from)));
    for (int i = std::max(0, from); i < to; ++i) {
        const Cell &c = cells[size_t(i)];
        if (c.ch == kWideTail)
            continue;
        if (c.ch == 0 && !(c.attrs & AttrCluster)) {
            cps.push_back(U' ');
            continue;
        }
        cellCodepoints(c, &cps);
    }
    while (!cps.empty() && cps.back() == U' ')
        cps.pop_back();
    return QString::fromUcs4(cps.data(), int(cps.size()));
}

} // namespace relay
