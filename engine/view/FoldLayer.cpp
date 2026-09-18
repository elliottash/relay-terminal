// SPDX-License-Identifier: GPL-3.0-or-later
#include "FoldLayer.h"

#include <QTextBoundaryFinder>

#include <algorithm>

namespace relay {

QString FoldLine::text() const
{
    QString s;
    for (const FoldSpan &span : spans)
        s += span.text;
    return s;
}

namespace {

// East Asian Wide/Fullwidth and the pictographs terminals give two cells. The
// emulator cores decide this for real cells; fold text is the host's own, so
// the layer needs the same answer to wrap it onto the same grid.
bool wideCodepoint(char32_t c)
{
    return (c >= 0x1100 && c <= 0x115F)     // Hangul Jamo
        || (c >= 0x2E80 && c <= 0x303E)     // CJK radicals, Kangxi, punctuation
        || (c >= 0x3041 && c <= 0x33FF)     // kana, Hangul compat, CJK compat
        || (c >= 0x3400 && c <= 0x4DBF)     // CJK ext A
        || (c >= 0x4E00 && c <= 0x9FFF)     // CJK unified
        || (c >= 0xA000 && c <= 0xA4CF)     // Yi
        || (c >= 0xAC00 && c <= 0xD7A3)     // Hangul syllables
        || (c >= 0xF900 && c <= 0xFAFF)     // CJK compat ideographs
        || (c >= 0xFE10 && c <= 0xFE19)     // vertical forms
        || (c >= 0xFE30 && c <= 0xFE6F)     // CJK compat forms
        || (c >= 0xFF00 && c <= 0xFF60)     // fullwidth forms
        || (c >= 0xFFE0 && c <= 0xFFE6)
        || (c >= 0x1F300 && c <= 0x1F64F)   // pictographs and emoticons
        || (c >= 0x1F900 && c <= 0x1F9FF)
        || (c >= 0x20000 && c <= 0x3FFFD);  // CJK ext B..
}

} // namespace

int FoldLayer::clusterWidth(const QString &cluster)
{
    if (cluster.isEmpty())
        return 0;
    const QVector<uint> cps = cluster.toUcs4();
    for (uint cp : cps) {
        if (cp == 0xFE0F)
            return 2; // emoji presentation selector
        if (wideCodepoint(char32_t(cp)))
            return 2;
    }
    return 1;
}

FoldLayer::FoldLayer() = default;

void FoldLayer::setPrefix(const QString &uriPrefix) { m_prefix = uriPrefix; }

bool FoldLayer::isAnchorUri(const QString &uri) const
{
    return !m_prefix.isEmpty() && uri.startsWith(m_prefix);
}

int FoldLayer::indexOf(const QString &uri) const
{
    const auto it = m_index.constFind(uri);
    return it == m_index.constEnd() ? -1 : *it;
}

const FoldLayer::Fold *FoldLayer::fold(const QString &uri) const
{
    const int i = indexOf(uri);
    return i < 0 ? nullptr : &m_folds[size_t(i)];
}

// ---------------------------------------------------------------- layout

// Break every logical line into grapheme cells, then wrap the cells to the
// usable width. Every wrapped row carries the block indent, so a wrapped fold
// line reads as one block (a hanging indent); the copy path puts the cells back
// together without it.
void FoldLayer::layout(Fold *f) const
{
    f->cells.clear();
    f->rows.clear();
    f->cells.reserve(size_t(f->lines.size()));
    for (const FoldLine &line : f->lines) {
        std::vector<Cell> cells;
        for (const FoldSpan &span : line.spans) {
            if (span.text.isEmpty())
                continue;
            QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, span.text);
            int from = 0;
            while (from < span.text.size()) {
                finder.setPosition(from);
                int to = finder.toNextBoundary();
                if (to <= from)
                    to = from + 1;
                Cell c;
                c.text = span.text.mid(from, to - from);
                // A tab inside fold text would break the grid; the host's detail
                // lines are already expanded, so any that is left becomes a space.
                if (c.text == QLatin1String("\t"))
                    c.text = QStringLiteral(" ");
                if (c.text.size() == 1 && c.text.at(0) < QChar(0x20))
                    c.text = QStringLiteral(" ");
                c.width = clusterWidth(c.text);
                c.fg = span.fg;
                c.bg = span.bg;
                c.bold = span.bold;
                c.italic = span.italic;
                c.underline = span.underline;
                c.dim = span.dim;
                c.link = span.link;
                if (c.width > 0)
                    cells.push_back(c);
                from = to;
            }
        }
        f->cells.push_back(std::move(cells));
    }

    const int usable = std::max(1, m_columns - m_indent);
    for (int line = 0; line < int(f->cells.size()); ++line) {
        const std::vector<Cell> &cells = f->cells[size_t(line)];
        if (cells.empty()) {
            f->rows.push_back(Row{line, 0, 0});
            continue;
        }
        int first = 0;
        while (first < int(cells.size())) {
            int used = 0, n = 0;
            while (first + n < int(cells.size())) {
                const int w = cells[size_t(first + n)].width;
                if (n > 0 && used + w > usable)
                    break;
                used += w;
                ++n;
            }
            f->rows.push_back(Row{line, first, n});
            first += n;
        }
    }
}

bool FoldLayer::setGeometry(int columns, int indent)
{
    const int cols = std::max(1, columns);
    const int ind = std::max(0, std::min(indent, std::max(0, cols - 1)));
    if (cols == m_columns && ind == m_indent)
        return false;
    m_columns = cols;
    m_indent = ind;
    for (Fold &f : m_folds) {
        if (f.hasContent)
            layout(&f);
    }
    rebuildAnchors();
    return true;
}

// ---------------------------------------------------------------- content

void FoldLayer::setContent(const QString &uri, const QVector<FoldLine> &lines)
{
    int i = indexOf(uri);
    if (i < 0) {
        i = int(m_folds.size());
        m_folds.push_back(Fold{});
        m_folds.back().uri = uri;
        m_index.insert(uri, i);
    }
    Fold &f = m_folds[size_t(i)];
    f.lines = lines;
    f.hasContent = true;
    f.expanded = true;
    layout(&f);
    rebuildAnchors();
}

void FoldLayer::setExpanded(const QString &uri, bool expanded)
{
    int i = indexOf(uri);
    if (i < 0) {
        if (!expanded)
            return;
        i = int(m_folds.size());
        m_folds.push_back(Fold{});
        m_folds.back().uri = uri;
        m_index.insert(uri, i);
    }
    Fold &f = m_folds[size_t(i)];
    if (f.expanded == expanded)
        return;
    f.expanded = expanded;
    rebuildAnchors();
}

bool FoldLayer::expanded(const QString &uri) const
{
    const Fold *f = fold(uri);
    return f && f->expanded;
}

bool FoldLayer::known(const QString &uri) const { return indexOf(uri) >= 0; }

bool FoldLayer::hasContent(const QString &uri) const
{
    const Fold *f = fold(uri);
    return f && f->hasContent;
}

void FoldLayer::remove(const QString &uri)
{
    const int i = indexOf(uri);
    if (i < 0)
        return;
    m_folds.erase(m_folds.begin() + i);
    m_index.clear();
    for (int k = 0; k < int(m_folds.size()); ++k)
        m_index.insert(m_folds[size_t(k)].uri, k);
    rebuildAnchors();
}

void FoldLayer::clear()
{
    m_folds.clear();
    m_index.clear();
    rebuildAnchors();
}

QStringList FoldLayer::expandedUris() const
{
    QStringList out;
    for (const Anchor &a : m_anchors)
        out << m_folds[size_t(a.foldIndex)].uri;
    for (const Fold &f : m_folds) {
        if (f.expanded && (!f.resolved() || f.height() == 0) && !out.contains(f.uri))
            out << f.uri;
    }
    return out;
}

int FoldLayer::expandedCount() const
{
    int n = 0;
    for (const Fold &f : m_folds) {
        if (f.expanded)
            ++n;
    }
    return n;
}

// ---------------------------------------------------------------- anchoring

void FoldLayer::setAnchor(const QString &uri, int startRow, int endRow)
{
    const int i = indexOf(uri);
    if (i < 0)
        return;
    Fold &f = m_folds[size_t(i)];
    f.anchorStartRow = startRow;
    f.anchorRow = endRow;
    rebuildAnchors();
}

void FoldLayer::clearAnchors()
{
    for (Fold &f : m_folds)
        f.anchorRow = f.anchorStartRow = -1;
    rebuildAnchors();
}

void FoldLayer::retainAnchored(const QVector<QString> &seen)
{
    std::vector<Fold> kept;
    kept.reserve(m_folds.size());
    for (Fold &f : m_folds) {
        if (seen.contains(f.uri))
            kept.push_back(std::move(f));
    }
    m_folds = std::move(kept);
    m_index.clear();
    for (int k = 0; k < int(m_folds.size()); ++k)
        m_index.insert(m_folds[size_t(k)].uri, k);
    rebuildAnchors();
}

void FoldLayer::rebuildAnchors()
{
    m_anchors.clear();
    m_anchorStarts.clear();
    m_totalHeight = 0;
    for (int i = 0; i < int(m_folds.size()); ++i) {
        const Fold &f = m_folds[size_t(i)];
        if (f.anchorStartRow >= 0)
            m_anchorStarts.insert(f.anchorStartRow, i);
        if (!f.expanded || !f.resolved() || f.height() == 0)
            continue;
        m_anchors.push_back(Anchor{f.anchorRow, f.height(), i, 0});
    }
    std::sort(m_anchors.begin(), m_anchors.end(), [this](const Anchor &a, const Anchor &b) {
        if (a.row != b.row)
            return a.row < b.row;
        return m_folds[size_t(a.foldIndex)].uri < m_folds[size_t(b.foldIndex)].uri;
    });
    int prefix = 0;
    for (Anchor &a : m_anchors) {
        a.visualAnchor = a.row + prefix;
        prefix += a.height;
    }
    m_totalHeight = prefix;
}

// ---------------------------------------------------------------- mapping

int FoldLayer::visualOfReal(int realRow) const
{
    // Every fold anchored strictly above this row pushes it down.
    int shift = 0;
    for (const Anchor &a : m_anchors) {
        if (a.row < realRow)
            shift += a.height;
        else
            break;
    }
    return realRow + shift;
}

FoldLayer::VisualRow FoldLayer::at(int visualRow) const
{
    VisualRow out;
    if (m_anchors.empty()) {
        out.realRow = visualRow;
        return out;
    }
    // The last anchor whose own visual row is above `visualRow`.
    int lo = 0, hi = int(m_anchors.size());
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (m_anchors[size_t(mid)].visualAnchor < visualRow)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == 0) {
        out.realRow = visualRow;
        return out;
    }
    const Anchor &a = m_anchors[size_t(lo - 1)];
    const int offset = visualRow - a.visualAnchor;
    if (offset <= a.height) {
        out.fold = true;
        out.foldIndex = a.foldIndex;
        out.foldRow = offset - 1;
        return out;
    }
    out.realRow = a.row + (offset - a.height);
    return out;
}

int FoldLayer::foldAtAnchorStart(int realRow) const
{
    const auto it = m_anchorStarts.constFind(realRow);
    return it == m_anchorStarts.constEnd() ? -1 : *it;
}

int FoldLayer::foldVisualStart(int foldIndex) const
{
    for (const Anchor &a : m_anchors) {
        if (a.foldIndex == foldIndex)
            return a.visualAnchor + 1;
    }
    return -1;
}

// ---------------------------------------------------------------- text

QString FoldLayer::cellsText(int foldIndex, int lineIndex, int from, int to) const
{
    if (foldIndex < 0 || foldIndex >= int(m_folds.size()))
        return QString();
    const Fold &f = m_folds[size_t(foldIndex)];
    if (lineIndex < 0 || lineIndex >= int(f.cells.size()))
        return QString();
    const std::vector<Cell> &cells = f.cells[size_t(lineIndex)];
    QString s;
    for (int i = std::max(0, from); i < std::min(to, int(cells.size())); ++i)
        s += cells[size_t(i)].text;
    return s;
}

QString FoldLayer::rowText(int foldIndex, int rowIndex) const
{
    if (foldIndex < 0 || foldIndex >= int(m_folds.size()))
        return QString();
    const Fold &f = m_folds[size_t(foldIndex)];
    if (rowIndex < 0 || rowIndex >= int(f.rows.size()))
        return QString();
    const Row &r = f.rows[size_t(rowIndex)];
    return cellsText(foldIndex, r.line, r.first, r.first + r.count);
}

QString FoldLayer::lineText(int foldIndex, int lineIndex) const
{
    if (foldIndex < 0 || foldIndex >= int(m_folds.size()))
        return QString();
    const Fold &f = m_folds[size_t(foldIndex)];
    if (lineIndex < 0 || lineIndex >= int(f.cells.size()))
        return QString();
    return cellsText(foldIndex, lineIndex, 0, int(f.cells[size_t(lineIndex)].size()));
}

} // namespace relay
