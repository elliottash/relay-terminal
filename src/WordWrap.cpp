// SPDX-License-Identifier: AGPL-3.0-or-later
#include "WordWrap.h"

namespace relay {

namespace {

const QChar kEsc(0x1b);

// The word as the terminal will show it: escape sequences removed.
QString visible(const QString &word) {
    QString text;
    for (int i = 0; i < word.size(); ++i) {
        if (word.at(i) != kEsc) { text += word.at(i); continue; }
        if (i + 1 < word.size() && word.at(i + 1) == QLatin1Char(']')) {        // OSC: to BEL or ST
            for (i += 2; i < word.size(); ++i) {
                if (word.at(i) == QChar(0x07)) break;
                if (word.at(i) == kEsc) { ++i; break; }
            }
        } else if (i + 1 < word.size() && word.at(i + 1) == QLatin1Char('[')) {  // CSI: to the final byte
            for (i += 2; i < word.size() && !(word.at(i).unicode() >= 0x40 && word.at(i).unicode() <= 0x7e); ++i) {}
        } else {
            ++i;
        }
    }
    return text;
}

}  // namespace

// What a line's first word has to be for its continuation rows to indent past it: a bullet, a
// checkbox, a quote bar, or an ordinal ("1." / "2)"). MarkdownAnsi's markers, and the raw ones.
bool wrap::isMarker(const QString &word) {
    static const QString bullets = QStringLiteral("•◦▪▸‣☐☑▎-*+>");
    if (word.size() == 1 && bullets.contains(word.at(0))) return true;
    if (word.size() < 2 || word.size() > 4) return false;
    const QChar last = word.back();
    if (last != QLatin1Char('.') && last != QLatin1Char(')')) return false;
    for (int i = 0; i + 1 < word.size(); ++i)
        if (!word.at(i).isDigit()) return false;
    return true;
}

// The hanging indent of a logical line, from its visible text: past the marker when the first
// word is one, else the leading spaces, and never past half the row. This is WordWrap::
// endOfFirstWord()'s decision, stated for a whole line — the marker case counts the marker and
// the space after it, because the wrapper sets the indent at the space that follows a marker.
int wrap::hangingIndent(const QString &lineText, int columns) {
    int lead = 0;
    while (lead < lineText.size() && lineText.at(lead) == QLatin1Char(' ')) ++lead;
    int end = lead;
    while (end < lineText.size() && lineText.at(end) != QLatin1Char(' ')) ++end;
    const QString firstWord = lineText.mid(lead, end - lead);
    int indent = lead;
    if (!firstWord.isEmpty() && isMarker(firstWord)) {
        int wordWidth = 0;
        uint high = 0;
        for (int i = 0; i < firstWord.size(); ++i) {
            const uint u = firstWord.at(i).unicode();
            if (QChar(firstWord.at(i)).isHighSurrogate()) { high = u; continue; }
            wordWidth += WordWrap::cellWidth(high && QChar(firstWord.at(i)).isLowSurrogate()
                                                 ? QChar::surrogateToUcs4(high, u) : u);
            high = 0;
        }
        indent = lead + wordWidth + 1;   // the marker, and the space after it
    }
    if (columns > 0 && indent > columns / 2) indent = 0;
    return indent;
}

// The whole-line form of the rule emitWord() applies as it streams: fill a row
// until the next word would cross the edge, then break before that word on a
// row that starts at the hanging indent. A word that does not fit on any row
// is broken at the edge exactly where the terminal would break it, and a space
// that would cross the edge is dropped.
QVector<wrap::Row> wrap::rows(const QVector<int> &widths, const QVector<char> &startsWord,
                              const QVector<char> &spaces, int columns, int firstIndent,
                              int hangIndent, int edgeIndent) {
    QVector<Row> out;
    const int n = widths.size();
    if (n == 0 || columns <= 0) {
        out.push_back(Row{0, n, firstIndent});
        return out;
    }
    // Each word's total width, so the break decision sees the whole word, not its first cell.
    QVector<int> wordWidth(n, 0);
    for (int i = 0; i < n; ++i) {
        if (!startsWord[size_t(i)] && i > 0) continue;
        int w = 0, j = i;
        while (j < n && !spaces[size_t(j)]) { w += widths[size_t(j)]; ++j; }
        for (int k = i; k < j; ++k) wordWidth[k] = w;
        i = j > i ? j - 1 : i;
    }
    int first = 0, used = firstIndent;
    int indent = firstIndent;   // the columns the row being filled starts at
    int i = 0;
    while (i < n) {
        const int w = widths[size_t(i)];
        const bool cellFits = used + w <= columns;
        // The word decision sees the whole word: a row ends *before* a word
        // that would cross the edge, not at the cell where it runs out. A cell
        // inside a word only breaks at the edge, which is where the terminal
        // breaks a word wider than a row — and such a continuation row starts
        // at edgeIndent, the terminal's own column 0 for prose.
        const bool wordFits = !startsWord[size_t(i)] || used + wordWidth[size_t(i)] <= columns;
        if (used > indent && spaces[size_t(i)] && !cellFits) {
            // An edge space opens no row: drop it — and the whole run of spaces
            // that follows, which in the byte stream all sit at the edge. The
            // row ends here; the next word starts the next row, and the
            // dropped cells must not sit inside the range of either row.
            out.push_back(Row{first, i - first, indent});
            while (i < n && spaces[size_t(i)])
                ++i;
            first = i;
            indent = hangIndent;
            used = hangIndent;
            continue;
        }
        if ((used > indent && !cellFits) || (used > indent && startsWord[size_t(i)] && !wordFits)) {
            out.push_back(Row{first, i - first, indent});
            first = i;
            // A row that starts a word hangs; a row that continues a word
            // broken at the edge starts where the terminal's own wrap leaves
            // the cursor.
            indent = startsWord[size_t(i)] ? hangIndent : edgeIndent;
            used = indent;
            continue;   // the cell is laid out again, at the head of the new row
        }
        used += w;
        ++i;
    }
    // A line that ended on a dropped edge space has already emitted its last
    // row; only an empty line (or an all-dropped one, which cannot happen)
    // still needs the one empty row.
    if (first < n || out.empty())
        out.push_back(Row{first, n - first, indent});
    return out;
}

int WordWrap::cellWidth(uint cp) {
    if (cp < 0x300) return 1;
    if ((cp >= 0x300 && cp <= 0x36f) || (cp >= 0x200b && cp <= 0x200f) || (cp >= 0xfe00 && cp <= 0xfe0f)
        || cp == 0x20d7 || (cp >= 0x1ab0 && cp <= 0x1aff) || (cp >= 0x1dc0 && cp <= 0x1dff) || (cp >= 0x20d0 && cp <= 0x20ff))
        return 0;
    if ((cp >= 0x1100 && cp <= 0x115f) || (cp >= 0x2e80 && cp <= 0x303e) || (cp >= 0x3041 && cp <= 0x33ff)
        || (cp >= 0x3400 && cp <= 0x4dbf) || (cp >= 0x4e00 && cp <= 0x9fff) || (cp >= 0xa000 && cp <= 0xa4cf)
        || (cp >= 0xac00 && cp <= 0xd7a3) || (cp >= 0xf900 && cp <= 0xfaff) || (cp >= 0xfe30 && cp <= 0xfe4f)
        || (cp >= 0xff00 && cp <= 0xff60) || (cp >= 0xffe0 && cp <= 0xffe6) || (cp >= 0x1f300 && cp <= 0x1f64f)
        || (cp >= 0x1f900 && cp <= 0x1f9ff) || (cp >= 0x1fa70 && cp <= 0x1faff) || (cp >= 0x20000 && cp <= 0x3fffd))
        return 2;
    return 1;
}

void WordWrap::reset() {
    m_col = 0;
    m_indent = 0;
    m_lead = Lead::Spaces;
    m_word.clear();
    m_wordWidth = 0;
    m_overlong = false;
    m_esc = Esc::None;
    m_high = QChar();
}

// The terminal's own bookkeeping: a character that does not fit goes to the next row.
void WordWrap::advance(int width) {
    if (m_columns > 0 && m_col + width > m_columns) m_col = 0;
    m_col += width;
}

void WordWrap::newline(QString &out) {
    out += QLatin1Char('\n');
    m_col = 0;
    m_indent = 0;
    m_lead = Lead::Spaces;
}

// A line's first word settles its hanging indent: past the marker if it is one (set by the space
// after it), else the leading spaces. More than half the line is no indent at all.
void WordWrap::endOfFirstWord() {
    if (m_lead != Lead::Spaces) return;
    if (!m_overlong && wrap::isMarker(visible(m_word))) { m_lead = Lead::Marker; return; }
    m_lead = Lead::Done;
    m_indent = m_col;
    if (m_columns > 0 && m_indent > m_columns / 2) m_indent = 0;
}

void WordWrap::emitWord(QString &out) {
    if (m_word.isEmpty()) return;
    endOfFirstWord();
    const bool wraps = m_columns > 0 && m_lead != Lead::Spaces && m_col > m_indent && m_col + m_wordWidth > m_columns;
    if (wraps) {
        out += QLatin1Char('\n');
        // Cursor-forward, not spaces: the indent stays empty cells, so an underline or a copy
        // does not pick it up.
        if (m_indent > 0) out += QStringLiteral("\x1b[%1C").arg(m_indent);
        m_col = m_indent;
    }
    out += m_word;
    if (m_columns > 0 && m_col + m_wordWidth > m_columns) m_col = (m_col + m_wordWidth - 1) % m_columns + 1;
    else m_col += m_wordWidth;
    m_word.clear();
    m_wordWidth = 0;
}

QString WordWrap::flush() {
    QString out;
    emitWord(out);
    if (!m_high.isNull()) { out += m_high; m_high = QChar(); }
    return out;
}

QString WordWrap::feed(const QString &text) {
    QString out;
    for (const QChar c : text) {
        QString &target = m_word.isEmpty() ? out : m_word;
        if (m_esc != Esc::None) {
            target += c;
            const ushort u = c.unicode();
            switch (m_esc) {
            case Esc::Start: m_esc = u == '[' ? Esc::Csi : u == ']' ? Esc::Osc : Esc::None; break;
            case Esc::Csi: if (u >= 0x40 && u <= 0x7e) m_esc = Esc::None; break;
            case Esc::Osc: if (u == 0x07) m_esc = Esc::None; else if (c == kEsc) m_esc = Esc::OscEsc; break;
            case Esc::OscEsc: m_esc = u == '\\' ? Esc::None : Esc::Osc; break;
            case Esc::None: break;
            }
            continue;
        }
        if (c == kEsc) { target += c; m_esc = Esc::Start; continue; }
        if (c == QLatin1Char('\n')) { emitWord(out); m_overlong = false; newline(out); continue; }
        if (c == QLatin1Char('\r')) { emitWord(out); m_overlong = false; out += c; m_col = 0; continue; }
        if (c == QLatin1Char(' ') || c == QLatin1Char('\t')) {
            emitWord(out);
            m_overlong = false;
            if (m_lead == Lead::Marker) {
                out += c;
                advance(1);
                m_lead = Lead::Done;
                m_indent = m_columns > 0 && m_col > m_columns / 2 ? 0 : m_col;
                continue;
            }
            // A space at the right edge would open the next row with a blank: the word after it
            // starts that row instead.
            if (m_columns > 0 && m_col >= m_columns && m_lead != Lead::Spaces) continue;
            out += c;
            advance(c == QLatin1Char('\t') ? 8 - m_col % 8 : 1);
            continue;
        }

        uint cp = c.unicode();
        if (c.isHighSurrogate()) { m_high = c; continue; }
        QString unit(c);
        if (c.isLowSurrogate() && !m_high.isNull()) {
            cp = QChar::surrogateToUcs4(m_high, c);
            unit.prepend(m_high);
            m_high = QChar();
        }
        const int width = cellWidth(cp);
        if (m_overlong) { out += unit; advance(width); continue; }
        m_word += unit;
        m_wordWidth += width;
        // Wider than any row can hold: start it on a row of its own and let the terminal break it.
        if (m_columns > 0 && m_wordWidth > m_columns - m_indent) {
            const QString word = m_word;
            const int wordWidth = m_wordWidth;
            m_word.clear();
            m_wordWidth = 0;
            if (m_lead == Lead::Spaces) { m_lead = Lead::Done; m_indent = 0; }
            else if (m_col > m_indent) {
                out += QLatin1Char('\n');
                if (m_indent > 0) out += QStringLiteral("\x1b[%1C").arg(m_indent);
                m_col = m_indent;
            }
            out += word;
            for (int i = 0; i < wordWidth; ++i) advance(1);
            m_overlong = true;
        }
    }
    return out;
}

}  // namespace relay
