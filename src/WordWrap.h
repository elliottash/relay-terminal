// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QString>
#include <QVector>

namespace relay {

// ---- the shared word-aware break rule (#R2WQ) ---------------------------------
//
// Where the rows of one logical line end, decided once and used everywhere
// Relay wraps its own text: by WordWrap for the bytes it prints into the grid,
// and by the engine's fold layer (and relay::wrapFoldLines) for the rows the
// view lays out itself — agent prose re-wrapped after a resize, and expanded
// tool-call detail. Before this existed the fold layer filled rows cluster by
// cluster, so detail broke mid-word at any width; the rule was WordWrap's but
// written a second time was the risk, so it lives here, next to the streaming
// wrapper that introduced it.
//
// A row breaks before a word that would cross the right edge; a continuation
// row takes the line's hanging indent (past a list/quote marker, else the
// leading spaces, dropped when it would eat past half the row); a word wider
// than a whole row is left to break at the edge, exactly as the terminal
// breaks WordWrap's overlong words; a space that would cross the edge is
// dropped rather than opening the next row with a blank.
namespace wrap {

// One row: cells [first, first + count) of the logical line. `count` can be
// zero (an empty line is one empty row). A dropped edge space belongs to no
// row, so the rows of one line are not strictly contiguous. `indent` is the
// columns the row starts at: firstIndent for the first row, hangIndent for
// every continuation row.
struct Row {
    int first = 0;
    int count = 0;
    int indent = 0;
};

// What a line's first word has to be for its continuation rows to hang past
// it: a bullet, a checkbox, a quote bar, or an ordinal ("1." / "2)").
// `word` is the line's first word as it will be shown, without escapes.
bool isMarker(const QString &word);

// The hanging indent a logical line's continuation rows take: past the marker
// if the first word is one (marker plus the space after it), else the line's
// leading spaces; dropped when it would eat past half the row. `lineText` is
// visible text, without escapes.
int hangingIndent(const QString &lineText, int columns);

// Breaks one logical line of laid-out cells into rows.
//
//   widths     one entry per cell: the grid columns it occupies (0 allowed)
//   startsWord cell i begins a word (i == 0, or the first non-space after a
//              run of spaces); spaces never start words
//   spaces     cell i is a blank
//   columns    the grid width; <= 0 returns the whole line as one row
//   firstIndent  columns before cell 0 of the first row
//   hangIndent   columns before cell 0 of a continuation row that starts a word
//   edgeIndent   columns before a continuation row that starts mid-word: the
//                terminal's own wrap of a word wider than a row starts at
//                column 0, which is what WordWrap's output shows; a block that
//                insets every row (an expanded fold) passes its indent instead
//
// All indents must be below `columns`. This is the whole-line form of the rule
// WordWrap applies word-by-word as it streams; the rows it returns are the rows
// the terminal shows for WordWrap's output, which is what makes a re-wrapped
// block agree with the bytes the pane printed.
QVector<Row> rows(const QVector<int> &widths, const QVector<char> &startsWord,
                  const QVector<char> &spaces, int columns, int firstIndent, int hangIndent,
                  int edgeIndent = 0);

}  // namespace wrap

// Streaming word wrap for text Relay writes into the terminal itself (agent replies, notes).
//
// The terminal wraps at the last column whatever is there, so prose broke mid-word ("ses" /
// "sion", owner report 2026-09-18). This sits between the renderer and the terminal: it tracks the
// cursor column, holds back the word being streamed until the space or newline that ends it, and
// starts a new line before a word that would cross the right edge. A continuation line takes the
// hanging indent of its first line — the leading spaces plus a list or quote marker — so a wrapped
// bullet lines up under its text, the way Warp and a Markdown viewer lay it out.
//
// Escape sequences pass through with zero width (CSI, OSC and the other ST-terminated strings —
// APC carries an inline image, #1MGS — split across chunks or not). '\r' and '\n' return the
// column to 0; the output keeps '\n' for the caller to turn into "\r\n". A word longer than the
// line is left to the terminal to break. columns <= 0 turns wrapping off.
class WordWrap {
public:
    void setColumns(int columns) { m_columns = columns; }
    int columns() const { return m_columns; }

    QString feed(const QString &text);
    // Emits the held word (wrapping it if needed). The column is kept: more text may follow.
    QString flush();
    // Forget everything: the next text starts at column 0 of a fresh line.
    void reset();
    bool holding() const { return !m_word.isEmpty(); }

    // Cells a character takes: 2 for East Asian wide and emoji, 0 for combining marks, else 1.
    static int cellWidth(uint codepoint);

private:
    void emitWord(QString &out);
    void newline(QString &out);
    void advance(int width);
    void endOfFirstWord();

    int m_columns = 0;
    int m_col = 0;          // cells filled on the current row, 0..m_columns
    int m_indent = 0;       // hanging indent for this line's continuation rows
    enum class Lead { Spaces, Marker, Done } m_lead = Lead::Spaces;   // reading the line's prefix
    QString m_word;         // the held word, escapes included
    int m_wordWidth = 0;
    bool m_overlong = false;   // streaming a word wider than the line: the terminal breaks it
    enum class Esc { None, Start, Csi, Osc, OscEsc } m_esc = Esc::None;
    QChar m_high;           // a high surrogate waiting for its pair
};

}  // namespace relay
