// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::FoldLayer: the GUI-free half of the terminal's fold layer.
//
// Relay prints each agent tool call into the terminal grid as one concise line
// wrapped in an OSC 8 hyperlink (`relay://call/<pane>/<turn>/<call>`). Clicking
// that line unfolds its detail *in place, underneath it, inside the terminal*.
//
// Neither emulator core lets the host insert rows into the scrollback
// (libghostty-vt owns its page list, libvterm's host ring is rewrapped on
// resize), so a fold is not content: it is a layer of **virtual rows** the view
// lays between the real rows it paints. This class owns everything about that
// layer which does not need a QPainter, a widget or a session:
//
//   * the folds themselves (content, expanded state, resolved anchor row),
//   * the layout: logical lines broken into grapheme cells and wrapped to the
//     grid width with a hanging indent,
//   * the mapping between **visual rows** (what the user scrolls through:
//     real rows and fold rows interleaved) and **real rows** (absolute
//     scrollback rows, 0 = the oldest line, the coordinates
//     VtCore::scrollViewportToRow() uses).
//
// Coordinates
//   real row     absolute scrollback row, 0 = oldest. realRows = history + rows.
//   visual row   real rows with every expanded fold's rows spliced in after the
//                fold's anchor row. visualTotal() >= realRows.
//
// A fold hangs under the **last** real row of the run of cells carrying its
// anchor URI (a long anchor line may soft-wrap over several rows), so
// `anchorRow` is that last row. Anchors are resolved by the view from
// VtCore::hyperlinkRuns() and re-resolved after resize, trimming and clearing;
// a fold whose URI is no longer in the scrollback is dropped.
//
// Nothing here touches Qt's GUI classes beyond QColor, so the maths is unit
// tested on its own (engine/tests/FoldLayerTest.cpp).
#pragma once

// relay::FoldSpan and relay::FoldLine -- what the host hands in -- live in the
// host-facing header, so a host only has to know that one.
#include "TerminalBackend.h"

#include <QColor>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <utility>
#include <vector>

namespace relay {

class FoldLayer {
public:
    // One grapheme cluster of fold text, laid out.
    struct Cell {
        QString text;
        int width = 1;      // 1 or 2 grid columns
        QColor fg;
        QColor bg;
        bool bold = false;
        bool italic = false;
        bool underline = false;
        bool strike = false;
        bool dim = false;
        bool reverse = false;
        QString link;
        uint32_t fgPacked = 0;  // an SGR ink as CellColor (prose spans); 0 = none
    };

    // One wrapped visual row of a fold: cells [first, first + count) of a
    // logical line, starting at column `startCol` (an insertion fold's block
    // indent; a prose row's own first-column or hanging indent). A dropped
    // edge space belongs to no row, so `first` of one row can sit past
    // `first + count` of the previous.
    struct Row {
        int line = 0;
        int first = 0;
        int count = 0;
        int startCol = 0;
    };

    struct Fold {
        QString uri;
        QVector<FoldLine> lines;              // as the host gave them
        std::vector<std::vector<Cell>> cells; // one vector per logical line
        std::vector<Row> rows;                // the wrap at the current width
        bool hasContent = false;
        bool expanded = false;
        int anchorRow = -1;                   // absolute row of the anchor run's last row
        int anchorStartRow = -1;              // absolute row where the run starts
        // A replacement fold (#R2WQ): the block's real rows [anchorStartRow,
        // anchorRow] are hidden and re-wrapped from `lines` whenever the grid
        // is not at printColumns, the width the pane printed them at. An
        // insertion fold (a tool call's detail) ignores all three.
        bool replacement = false;
        int printColumns = 0;
        bool resolved() const { return anchorRow >= 0; }
        int height() const { return int(rows.size()); }
    };

    FoldLayer();

    // ---- anchors and identity
    // OSC 8 URIs starting with this prefix are fold anchors. Empty = no fold
    // anchors at all, which is how a view without the feature behaves.
    void setPrefix(const QString &uriPrefix);
    QString prefix() const { return m_prefix; }
    bool isAnchorUri(const QString &uri) const;

    // ---- content
    void setContent(const QString &uri, const QVector<FoldLine> &lines); // sets content and expands
    void setExpanded(const QString &uri, bool expanded);
    bool expanded(const QString &uri) const;
    bool known(const QString &uri) const;
    bool hasContent(const QString &uri) const;
    void remove(const QString &uri);
    void clear();
    QStringList expandedUris() const; // visual order; folds with no anchor yet last

    // ---- prose blocks (#R2WQ)
    // A replacement fold: `uri` is the OSC 8 run that covers exactly the
    // block's rows in the grid (kProsePrefix), `lines` the block's logical
    // lines as the pane printed them, `printColumns` the width they were
    // wrapped at. While the grid is at that width the fold takes no rows and
    // hides nothing; at any other width it hides the run's real rows and
    // paints its own wrap of the lines. The URI is never interactive.
    void setProse(const QString &uri, const QVector<FoldLine> &lines, int printColumns);
    static bool isProseUri(const QString &uri);
    // Every replacement fold with content, in m_folds order (the order the pane
    // printed them in): what a host saves beside its scrollback rows and hands
    // back through setProse() after a restore (#MTCS). Unresolved anchors are
    // included: the block is worth saving even when the view has not yet found
    // its run, and a restored pane resolves it again from its replayed rows.
    QVector<ProseBlock> proseBlocks() const;
    // True while any replacement fold has taken its rows over.
    bool proseActive() const;
    // A real row hidden by a taken-over replacement fold (its text is the
    // fold's, so the view treats core matches there as the fold's matches).
    bool rowHidden(int realRow) const;
    // Those rows as [first, last] ranges of absolute real rows, in order. The
    // find asks the core how many of its matches lie in them and subtracts
    // that: the same text is counted again from the fold's own lines (#RW9T).
    std::vector<std::pair<int, int>> hiddenRowRanges() const;

    // ---- layout
    // columns: the grid width. indent: how far the block is pushed in (2..4).
    // Returns true when the wrap changed (the caller repaints and re-measures).
    bool setGeometry(int columns, int indent);
    int columns() const { return m_columns; }
    int indent() const { return m_indent; }

    // ---- anchoring (the view feeds this from VtCore::hyperlinkRuns())
    // Sets the absolute rows of the anchor run of `uri`; -1 forgets them.
    void setAnchor(const QString &uri, int startRow, int endRow);
    // Forget every anchor (after a resize, before the rows are resolved again).
    void clearAnchors();
    // One anchor run, as the view read it out of the grid.
    struct AnchorRows {
        QString uri;
        int startRow = -1;
        int endRow = -1;
    };
    // A whole resolve in one call: every anchor the view found, then the drop
    // of every fold whose URI is not in `seen` (its anchor left the
    // scrollback). The layout is rebuilt **once**, at the end — setAnchor() per
    // anchor rebuilt it per anchor, which with the linear `seen` scan made a
    // resolve cost O(folds^2) in a conversation that grows one fold per tool
    // row and one per prose block (#PPR4).
    void applyAnchors(const std::vector<AnchorRows> &anchors, const QSet<QString> &seen);
    // Drop every fold whose URI is not in `seen` (its anchor left the
    // scrollback) and forget the anchors of the ones that are not.
    void retainAnchored(const QSet<QString> &seen);

    // ---- the visual-row model
    // True when at least one expanded fold has a resolved anchor and a height,
    // i.e. when the view has to do any of this work at all. Everything else in
    // the view takes its old path when this is false.
    bool active() const { return !m_anchors.empty(); }
    int expandedCount() const;
    int visualRows() const { return m_totalHeight; } // net rows the folds add (may shrink)

    int visualTotal(int realRows) const { return realRows + m_totalHeight; }
    // The visual row a real row is painted on. A row hidden by a replacement
    // fold answers the block's first visual row.
    int visualOfReal(int realRow) const;

    struct VisualRow {
        bool fold = false;
        int realRow = 0;    // fold == false
        int foldIndex = -1; // fold == true: index into folds()
        int foldRow = 0;    // fold == true: index into folds()[foldIndex].rows
    };
    // The visual row `v`. Out-of-range rows come back as real rows, which the
    // view paints as blanks, exactly as it does past the end today.
    VisualRow at(int visualRow) const;
    // The first visual row of a fold's block, or -1.
    int foldVisualStart(int foldIndex) const;
    // The fold whose anchor run *starts* on this absolute real row, or -1. The
    // view overpaints the chevron there, whether the fold is open or shut.
    // Replacement folds never answer: their first cell is text, not a
    // placeholder.
    int foldAtAnchorStart(int realRow) const;
    // The columns a fold's rows start at: the block indent for a fold, 0 for
    // a prose block, which is laid out like a grid row of its own.
    int foldIndent(int foldIndex) const;
    // The column one wrapped row of a fold starts at: every row of an
    // insertion fold at the block indent, a prose row at its own first-column
    // or hanging indent.
    int rowStartCol(int foldIndex, int foldRow) const;

    // ---- access
    const std::vector<Fold> &folds() const { return m_folds; }
    int indexOf(const QString &uri) const;
    const Fold *fold(const QString &uri) const;
    // The text of one wrapped row (no indent), and of one logical line.
    QString rowText(int foldIndex, int rowIndex) const;
    QString lineText(int foldIndex, int lineIndex) const;
    // Cells [from, to) of a logical line as text.
    QString cellsText(int foldIndex, int lineIndex, int from, int to) const;
    // The cells of one wrapped row that grid columns [fromCol, toCol] cover, as
    // [*first, *last). A cell is one or two columns wide, so a column offset is
    // not a cell offset: adding one to the other reads the wrong graphemes as
    // soon as a wide character sits to the left (#C7WP). A cell counts as
    // covered when the column it *starts* at is in range, which is the rule
    // paintProseRow highlights by, so what is painted is what copies. Returns
    // false when the row has no cell in those columns (*first == *last).
    bool rowCellRange(int foldIndex, int foldRow, int fromCol, int toCol, int *first, int *last) const;
    // The fold whose replacement block has taken over this real row, or -1.
    int foldHidingRow(int realRow) const;

    // Grid columns one grapheme cluster occupies (1 or 2).
    static int clusterWidth(const QString &cluster);

    // How often the layout has been rebuilt. A resolve is one batch and costs
    // one rebuild; the tests assert on that rather than on a clock (#PPR4).
    quint64 rebuildCount() const { return m_rebuilds; }

private:
    void layout(Fold *f) const;
    void rebuildAnchors();
    // The anchor rows of one fold, without rebuilding the layout: applyAnchors()
    // sets every anchor of a resolve and rebuilds once.
    void setAnchorRows(const QString &uri, int startRow, int endRow);
    // A replacement fold has taken its rows over at the current width.
    bool takenOver(const Fold &f) const;

    struct Anchor {
        int row = 0;       // insertion: the row the fold hangs under;
                           // replacement: the last row it hides
        int startRow = 0;  // replacement: the first row it hides
        int height = 0;    // rows the block paints
        int hidden = 0;    // replacement: real rows it hides; insertion: 0
        bool replacement = false;
        int foldIndex = 0;
        int visualStart = 0; // the block's first visual row
    };

    QString m_prefix;
    std::vector<Fold> m_folds;
    QHash<QString, int> m_index;
    std::vector<Anchor> m_anchors; // expanded, resolved, non-empty; sorted by startRow
    QHash<int, int> m_anchorStarts; // real row -> fold index, for every resolved insertion fold
    int m_totalHeight = 0;
    quint64 m_rebuilds = 0;
    int m_columns = 80;
    int m_indent = kFoldIndent;
};

} // namespace relay
