// SPDX-License-Identifier: GPL-3.0-or-later
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

// What a line's first word has to be for its continuation rows to indent past it: a bullet, a
// checkbox, a quote bar, or an ordinal ("1." / "2)"). MarkdownAnsi's markers, and the raw ones.
bool isMarker(const QString &word) {
    static const QString bullets = QStringLiteral("•◦▪▸‣☐☑▎-*+>");
    if (word.size() == 1 && bullets.contains(word.at(0))) return true;
    if (word.size() < 2 || word.size() > 4) return false;
    const QChar last = word.back();
    if (last != QLatin1Char('.') && last != QLatin1Char(')')) return false;
    for (int i = 0; i + 1 < word.size(); ++i)
        if (!word.at(i).isDigit()) return false;
    return true;
}

}  // namespace

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
    if (!m_overlong && isMarker(visible(m_word))) { m_lead = Lead::Marker; return; }
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
