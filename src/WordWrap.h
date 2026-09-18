// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace relay {

// Streaming word wrap for text Relay writes into the terminal itself (agent replies, notes).
//
// The terminal wraps at the last column whatever is there, so prose broke mid-word ("ses" /
// "sion", owner report 2026-09-18). This sits between the renderer and the terminal: it tracks the
// cursor column, holds back the word being streamed until the space or newline that ends it, and
// starts a new line before a word that would cross the right edge. A continuation line takes the
// hanging indent of its first line — the leading spaces plus a list or quote marker — so a wrapped
// bullet lines up under its text, the way Warp and a Markdown viewer lay it out.
//
// Escape sequences pass through with zero width (CSI and OSC, split across chunks or not). '\r'
// and '\n' return the column to 0; the output keeps '\n' for the caller to turn into "\r\n". A
// word longer than the line is left to the terminal to break. columns <= 0 turns wrapping off.
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
