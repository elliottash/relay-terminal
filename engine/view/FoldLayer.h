// SPDX-License-Identifier: GPL-3.0-or-later
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
#include <QString>
#include <QStringList>
#include <QVector>

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
        bool dim = false;
        QString link;
    };

    // One wrapped visual row of a fold: cells [first, first + count) of a
    // logical line.
    struct Row {
        int line = 0;
        int first = 0;
        int count = 0;
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
    // Drop every fold whose URI is not in `seen` (its anchor left the
    // scrollback) and forget the anchors of the ones that are not.
    void retainAnchored(const QVector<QString> &seen);

    // ---- the visual-row model
    // True when at least one expanded fold has a resolved anchor and a height,
    // i.e. when the view has to do any of this work at all. Everything else in
    // the view takes its old path when this is false.
    bool active() const { return !m_anchors.empty(); }
    int expandedCount() const;
    int visualRows() const { return m_totalHeight; } // rows contributed by the folds

    int visualTotal(int realRows) const { return realRows + m_totalHeight; }
    // The visual row a real row is painted on.
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
    // The first visual row of a fold's block (its anchor's visual row + 1), or -1.
    int foldVisualStart(int foldIndex) const;
    // The fold whose anchor run *starts* on this absolute real row, or -1. The
    // view overpaints the chevron there, whether the fold is open or shut.
    int foldAtAnchorStart(int realRow) const;

    // ---- access
    const std::vector<Fold> &folds() const { return m_folds; }
    int indexOf(const QString &uri) const;
    const Fold *fold(const QString &uri) const;
    // The text of one wrapped row (no indent), and of one logical line.
    QString rowText(int foldIndex, int rowIndex) const;
    QString lineText(int foldIndex, int lineIndex) const;
    // Cells [from, to) of a logical line as text.
    QString cellsText(int foldIndex, int lineIndex, int from, int to) const;

    // Grid columns one grapheme cluster occupies (1 or 2).
    static int clusterWidth(const QString &cluster);

private:
    void layout(Fold *f) const;
    void rebuildAnchors();

    struct Anchor {
        int row = 0;       // absolute real row the fold hangs under
        int height = 0;
        int foldIndex = 0;
        int visualAnchor = 0; // the anchor row's visual row
    };

    QString m_prefix;
    std::vector<Fold> m_folds;
    QHash<QString, int> m_index;
    std::vector<Anchor> m_anchors; // expanded, resolved, non-empty; sorted by row
    QHash<int, int> m_anchorStarts; // real row -> fold index, for every resolved fold
    int m_totalHeight = 0;
    int m_columns = 80;
    int m_indent = 3;
};

} // namespace relay
