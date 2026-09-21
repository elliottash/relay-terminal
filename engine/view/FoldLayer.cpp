// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FoldLayer.h"

#include "core/CellTypes.h"

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

// The table itself lives beside FoldLine in the host-facing header: a host that
// caps its content in rendered rows has to wrap it exactly as this does
// (relay::wrapFoldLines), and two copies of the width rules would drift.
int FoldLayer::clusterWidth(const QString &cluster)
{
    return foldClusterWidth(cluster);
}

namespace {

// The ink of an SGR parameter list, as the packed colour the view resolves
// against the theme when it paints (#R2WQ). Only the ink matters here: a host
// that wants bold or italic sets those flags on the span itself, and the pane
// sets both for its prose. 0 = the default ink.
uint32_t sgrFg(const QString &sgr)
{
    if (sgr.isEmpty())
        return 0;
    uint32_t fg = 0;
    const QStringList params = sgr.split(QLatin1Char(';'));
    for (int i = 0; i < params.size(); ++i) {
        bool ok = false;
        const int p = params.at(i).toInt(&ok);
        if (!ok)
            continue;
        if (p == 39)
            fg = 0;
        else if (p >= 30 && p <= 37)
            fg = CellColor::indexed(uint8_t(p - 30));
        else if (p >= 90 && p <= 97)
            fg = CellColor::indexed(uint8_t(p - 90 + 8));
        else if (p == 38 && i + 1 < params.size()) {
            bool sub = false;
            const int mode = params.at(i + 1).toInt(&sub);
            if (mode == 5 && i + 2 < params.size()) {
                fg = CellColor::indexed(uint8_t(params.at(i + 2).toInt()));
                i += 2;
            } else if (mode == 2 && i + 4 < params.size()) {
                fg = CellColor::rgb(uint8_t(params.at(i + 2).toInt()), uint8_t(params.at(i + 3).toInt()),
                                    uint8_t(params.at(i + 4).toInt()));
                i += 4;
            }
        }
    }
    return fg;
}

}  // namespace

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

// Break every logical line into grapheme cells, then wrap them to the grid
// width with the one shared word-aware break rule (#R2WQ, src/WordWrap.h):
// rows end between words, never inside one, and a prose block's continuation
// rows hang under their line's marker or leading spaces. An insertion fold's
// rows all start at the block indent, so the block reads as one inset detail;
// the copy path puts the cells back together without it.
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
            const uint32_t sgrInk = sgrFg(span.sgr);
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
                c.reverse = span.reverse;
                c.link = span.link;
                c.fgPacked = sgrInk;
                if (c.width > 0)
                    cells.push_back(c);
                from = to;
            }
        }
        f->cells.push_back(std::move(cells));
    }

    for (int line = 0; line < int(f->cells.size()); ++line) {
        const std::vector<Cell> &cells = f->cells[size_t(line)];
        if (cells.empty()) {
            f->rows.push_back(Row{line, 0, 0, f->replacement ? 0 : m_indent});
            continue;
        }
        const int blockIndent = f->replacement ? 0 : m_indent;
        const int hang = f->replacement ? wrap::hangingIndent(f->lines[size_t(line)].text(), m_columns)
                                        : m_indent;
        QVector<int> widths(int(cells.size()));
        QVector<char> startsWord(int(cells.size()), 0), spaces(int(cells.size()), 0);
        for (int i = 0; i < int(cells.size()); ++i) {
            widths[i] = cells[size_t(i)].width;
            spaces[i] = cells[size_t(i)].text == QLatin1String(" ") ? 1 : 0;
        }
        startsWord[0] = 1;
        for (int i = 1; i < int(cells.size()); ++i)
            startsWord[i] = !spaces[i] && spaces[size_t(i - 1)] ? 1 : 0;
        const QVector<wrap::Row> wrapped =
            wrap::rows(widths, startsWord, spaces, m_columns, blockIndent, hang,
                       f->replacement ? 0 : m_indent);
        for (const wrap::Row &r : wrapped)
            f->rows.push_back(Row{line, r.first, r.count,
                                  f->replacement ? r.indent : m_indent});
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
    for (const Anchor &a : m_anchors) {
        if (!a.replacement)
            out << m_folds[size_t(a.foldIndex)].uri;
    }
    for (const Fold &f : m_folds) {
        if (f.replacement)
            continue;   // prose is not the host's to re-open
        if (f.expanded && (!f.resolved() || f.height() == 0) && !out.contains(f.uri))
            out << f.uri;
    }
    return out;
}

int FoldLayer::expandedCount() const
{
    int n = 0;
    for (const Fold &f : m_folds) {
        if (f.expanded && !f.replacement)
            ++n;
    }
    return n;
}

// ---------------------------------------------------------------- prose blocks

void FoldLayer::setProse(const QString &uri, const QVector<FoldLine> &lines, int printColumns)
{
    if (uri.isEmpty())
        return;
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
    f.expanded = true;   // drives the anchor machinery; prose is never toggled
    f.replacement = true;
    f.printColumns = printColumns;
    layout(&f);
    rebuildAnchors();
}

bool FoldLayer::isProseUri(const QString &uri)
{
    return uri.startsWith(QLatin1String(kProsePrefix));
}

bool FoldLayer::proseActive() const
{
    for (const Anchor &a : m_anchors) {
        if (a.replacement)
            return true;
    }
    return false;
}

bool FoldLayer::rowHidden(int realRow) const
{
    for (const Anchor &a : m_anchors) {
        if (a.replacement && realRow >= a.startRow && realRow <= a.row)
            return true;
    }
    return false;
}

// A replacement fold takes over only away from the width its rows were printed
// at: at that width the grid already holds exactly the rows the rule produces,
// so the layer stands aside and the printed bytes show, byte for byte.
bool FoldLayer::takenOver(const Fold &f) const
{
    return f.replacement && f.printColumns > 0 && m_columns != f.printColumns;
}

// ---------------------------------------------------------------- anchoring

void FoldLayer::setAnchorRows(const QString &uri, int startRow, int endRow)
{
    const int i = indexOf(uri);
    if (i < 0)
        return;
    Fold &f = m_folds[size_t(i)];
    f.anchorStartRow = startRow;
    f.anchorRow = endRow;
}

void FoldLayer::setAnchor(const QString &uri, int startRow, int endRow)
{
    setAnchorRows(uri, startRow, endRow);
    rebuildAnchors();
}

void FoldLayer::applyAnchors(const std::vector<AnchorRows> &anchors, const QSet<QString> &seen)
{
    for (const AnchorRows &a : anchors)
        setAnchorRows(a.uri, a.startRow, a.endRow);
    retainAnchored(seen);   // rebuilds the layout once, for the whole batch
}

void FoldLayer::clearAnchors()
{
    for (Fold &f : m_folds)
        f.anchorRow = f.anchorStartRow = -1;
    rebuildAnchors();
}

void FoldLayer::retainAnchored(const QSet<QString> &seen)
{
    std::vector<Fold> kept;
    kept.reserve(m_folds.size());
    // A QSet, not a list: with one fold per tool row and one per prose block
    // this ran once per block close over every fold there had ever been (#PPR4).
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
    ++m_rebuilds;
    m_anchors.clear();
    m_anchorStarts.clear();
    m_totalHeight = 0;
    for (int i = 0; i < int(m_folds.size()); ++i) {
        const Fold &f = m_folds[size_t(i)];
        if (f.replacement) {
            if (!f.hasContent || !f.resolved() || f.height() == 0 || !takenOver(f))
                continue;
            m_anchors.push_back(Anchor{f.anchorRow, f.anchorStartRow, f.height(),
                                       f.anchorRow - f.anchorStartRow + 1, true, i, 0});
            continue;
        }
        if (f.anchorStartRow >= 0)
            m_anchorStarts.insert(f.anchorStartRow, i);
        if (!f.expanded || !f.resolved() || f.height() == 0)
            continue;
        m_anchors.push_back(Anchor{f.anchorRow, f.anchorRow, f.height(), 0, false, i, 0});
    }
    // Blocks never overlap, so ordering by the first row each touches orders
    // the blocks as they sit in the grid.
    std::sort(m_anchors.begin(), m_anchors.end(), [this](const Anchor &a, const Anchor &b) {
        if (a.startRow != b.startRow)
            return a.startRow < b.startRow;
        return m_folds[size_t(a.foldIndex)].uri < m_folds[size_t(b.foldIndex)].uri;
    });
    int shift = 0;   // visual - real, for rows above each block
    for (Anchor &a : m_anchors) {
        a.visualStart = a.startRow + shift + (a.replacement ? 0 : 1);
        shift += a.height - a.hidden;
    }
    m_totalHeight = shift;
}

// ---------------------------------------------------------------- mapping

int FoldLayer::visualOfReal(int realRow) const
{
    // Every block above this row moves it: an insertion fold by its height, a
    // replacement fold by the difference between the rows it paints and the
    // rows it hides. A hidden row answers the block's first visual row.
    int shift = 0;
    for (const Anchor &a : m_anchors) {
        if (a.replacement) {
            if (realRow < a.startRow)
                break;
            if (realRow <= a.row)
                return a.visualStart;
            shift += a.height - a.hidden;
        } else {
            if (a.row >= realRow)
                break;
            shift += a.height;
        }
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
    // The last block that starts at or above `visualRow`.
    int lo = 0, hi = int(m_anchors.size());
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (m_anchors[size_t(mid)].visualStart <= visualRow)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == 0) {
        out.realRow = visualRow;
        return out;
    }
    const Anchor &a = m_anchors[size_t(lo - 1)];
    const int offset = visualRow - a.visualStart;
    if (offset < a.height) {
        out.fold = true;
        out.foldIndex = a.foldIndex;
        out.foldRow = offset;
        return out;
    }
    // The row after the block: the real row that follows the last one it
    // covers, wherever the blocks above put it.
    out.realRow = a.row + 1 + (offset - a.height);
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
            return a.visualStart;
    }
    return -1;
}

int FoldLayer::foldIndent(int foldIndex) const
{
    if (foldIndex < 0 || foldIndex >= int(m_folds.size()))
        return m_indent;
    return m_folds[size_t(foldIndex)].replacement ? 0 : m_indent;
}

int FoldLayer::rowStartCol(int foldIndex, int foldRow) const
{
    if (foldIndex < 0 || foldIndex >= int(m_folds.size()))
        return m_indent;
    const Fold &f = m_folds[size_t(foldIndex)];
    if (!f.replacement)
        return m_indent;
    if (foldRow < 0 || foldRow >= int(f.rows.size()))
        return 0;
    return f.rows[size_t(foldRow)].startCol;
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

bool FoldLayer::rowCellRange(int foldIndex, int foldRow, int fromCol, int toCol,
                             int *first, int *last) const
{
    if (first) *first = 0;
    if (last) *last = 0;
    if (foldIndex < 0 || foldIndex >= int(m_folds.size()))
        return false;
    const Fold &f = m_folds[size_t(foldIndex)];
    if (foldRow < 0 || foldRow >= int(f.rows.size()))
        return false;
    const Row &r = f.rows[size_t(foldRow)];
    if (r.line < 0 || r.line >= int(f.cells.size()))
        return false;
    const std::vector<Cell> &cells = f.cells[size_t(r.line)];
    const int end = std::min(r.first + r.count, int(cells.size()));
    int col = rowStartCol(foldIndex, foldRow);
    int lo = -1, hi = -1;
    for (int i = std::max(0, r.first); i < end; ++i) {
        if (col >= fromCol && col <= toCol) {
            if (lo < 0)
                lo = i;
            hi = i;
        }
        col += cells[size_t(i)].width;
        if (col > toCol)
            break;
    }
    if (lo < 0)
        return false;
    if (first) *first = lo;
    if (last) *last = hi + 1;
    return true;
}

int FoldLayer::foldHidingRow(int realRow) const
{
    for (const Anchor &a : m_anchors) {
        if (a.replacement && realRow >= a.startRow && realRow <= a.row)
            return a.foldIndex;
    }
    return -1;
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
