// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MarkdownAnsi.h"

#include <QRegularExpression>
#include <QVector>

#include <algorithm>

namespace relay {

namespace {

const QString kEsc = QStringLiteral("\x1b[");
const QString kReset = QStringLiteral("\x1b[0m");
// Colours follow the inline inks in main.cpp: violet is the agent, amber is a tool.

QString sgr(const QString &params) { return kEsc + params + QLatin1Char('m'); }

int runLength(const QString &text, int from, QChar c) {
    int n = 0;
    while (from + n < text.size() && text.at(from + n) == c) ++n;
    return n;
}

// Leading spaces, then the rest of the line.
int indentOf(const QString &line) {
    int n = 0;
    while (n < line.size() && (line.at(n) == QLatin1Char(' ') || line.at(n) == QLatin1Char('\t'))) ++n;
    return n;
}

// "---", "* * *", "___": one marker character repeated, spaces allowed.
bool ruleShape(const QString &rest, int *count) {
    if (rest.isEmpty()) return false;
    const QChar c = rest.at(0);
    if (c != QLatin1Char('-') && c != QLatin1Char('*') && c != QLatin1Char('_')) return false;
    int n = 0;
    for (const QChar ch : rest) {
        if (ch == c) ++n;
        else if (ch != QLatin1Char(' ')) return false;
    }
    if (count) *count = n;
    return true;
}

QStringList tableCells(const QString &row) {
    QString text = row.trimmed();
    if (text.startsWith(QLatin1Char('|'))) text.remove(0, 1);
    if (text.endsWith(QLatin1Char('|')) && !text.endsWith(QStringLiteral("\\|"))) text.chop(1);
    QStringList cells;
    QString cell;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\\') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('|')) { cell += QLatin1Char('|'); ++i; }
        else if (text.at(i) == QLatin1Char('|')) { cells << cell.trimmed(); cell.clear(); }
        else cell += text.at(i);
    }
    cells << cell.trimmed();
    return cells;
}

// Card #CVHT (bolding main points): the closed keyword lists behind **Done:** / **Need:** /
// **Problem:**. A bold run is coloured when its *first* word — punctuation stripped, compared
// case-insensitively — is in one of these lists; anything else stays plain bold, as before.
const QStringList kDoneWords = {QStringLiteral("done"), QStringLiteral("finished"), QStringLiteral("complete"),
                                QStringLiteral("completed"), QStringLiteral("ready"), QStringLiteral("success"),
                                QStringLiteral("passed"), QStringLiteral("fixed"), QStringLiteral("works"),
                                QStringLiteral("working")};
const QStringList kProblemWords = {QStringLiteral("problem"), QStringLiteral("error"), QStringLiteral("failed"),
                                   QStringLiteral("failure"), QStringLiteral("broken"), QStringLiteral("blocked"),
                                   QStringLiteral("warning"), QStringLiteral("bug")};
const QStringList kNeedWords = {QStringLiteral("need"), QStringLiteral("needs"), QStringLiteral("question"),
                                QStringLiteral("waiting"), QStringLiteral("ask"), QStringLiteral("decision")};
// Enough held characters for the first word of a bold run and a little more; a stray unclosed `**`
// cannot stall the stream for longer than this.
constexpr int kBoldHoldMax = 32;

}  // namespace

MarkdownAnsi::MarkdownAnsi(const QString &baseSgr) { m_palette.base = baseSgr; }
MarkdownAnsi::MarkdownAnsi(const Palette &palette) : m_palette(palette) {}
void MarkdownAnsi::setPalette(const Palette &palette) { m_palette = palette; }

void MarkdownAnsi::reset() {
    m_pending.clear();
    m_table.clear();
    m_final = false;
    m_lineStarted = false;
    m_line = Line::Paragraph;
    m_inFence = false;
    m_fenceLen = 0;
    resetInline();
}

void MarkdownAnsi::resetInline() {
    m_bold = m_italic = m_strike = false;
    m_codeRun = 0;
    m_prev = QChar();
    m_boldHold.clear();
    m_boldRole = BoldRole::None;
}

int MarkdownAnsi::visibleWidth(const QString &rendered) {
    int width = 0;
    for (int i = 0; i < rendered.size(); ++i) {
        const QChar c = rendered.at(i);
        if (c == QChar(0x1b)) {
            while (i < rendered.size() && !rendered.at(i).isLetter()) ++i;   // through the final byte
            continue;
        }
        if (c.isLowSurrogate()) continue;
        ++width;
    }
    return width;
}

QString MarkdownAnsi::feed(const QString &text) {
    m_pending += text;
    return process();
}

QString MarkdownAnsi::finish() {
    m_final = true;
    QString out = process();
    flushBoldHold(out);   // an open bold run's held first word still deserves its colour
    if (!m_table.isEmpty()) out += renderTable();
    if (m_lineStarted) out += kReset;
    reset();
    return out;
}

QString MarkdownAnsi::lineBase() const {
    switch (m_line) {
    case Line::Heading1: return m_palette.heading + QStringLiteral(";4");
    case Line::Heading: return m_palette.heading;
    case Line::Quote: return m_palette.quote;
    case Line::Code: return m_palette.codeBlock;
    case Line::Paragraph: break;
    }
    return m_palette.base;
}

QString MarkdownAnsi::style() const {
    QString params = QStringLiteral("0;") + lineBase();
    if (m_bold) {
        params += QStringLiteral(";1");
        // Card #CVHT: once a bold run's first word is classified, its role colour holds for the
        // rest of the run (until the closing marker or the end of the line).
        if (m_boldRole == BoldRole::Done) params += QLatin1Char(';') + m_palette.done;
        else if (m_boldRole == BoldRole::Need) params += QLatin1Char(';') + m_palette.need;
        else if (m_boldRole == BoldRole::Problem) params += QLatin1Char(';') + m_palette.problem;
    }
    if (m_italic) params += QStringLiteral(";3");
    if (m_strike) params += QStringLiteral(";9");
    if (m_codeRun > 0) params += QLatin1Char(';') + m_palette.inlineCode;
    return sgr(params);
}

QString MarkdownAnsi::process() {
    QString out;
    for (;;) {
        if (!m_lineStarted) {
            if (!decideLine(out)) break;
            continue;
        }
        int i = 0;
        bool lineDone = false;
        while (i < m_pending.size()) {
            if (m_pending.at(i) == QLatin1Char('\n')) {
                flushBoldHold(out);   // a bold run left open at the line end still gets its colour
                out += kReset + QLatin1Char('\n');
                ++i;
                m_lineStarted = false;
                m_line = Line::Paragraph;
                resetInline();
                lineDone = true;
                break;
            }
            if (m_line == Line::Code) { out += m_pending.at(i++); continue; }
            if (!inlineStep(out, i)) break;   // held: needs more input
        }
        m_pending.remove(0, i);
        if (!lineDone) break;
    }
    return out;
}

// Classifies the line at the start of m_pending and emits its prefix. False when more input is
// needed to tell what the line is.
bool MarkdownAnsi::decideLine(QString &out) {
    if (m_pending.isEmpty()) return false;
    const int nl = m_pending.indexOf(QLatin1Char('\n'));
    const bool complete = nl >= 0 || m_final;
    const int end = nl >= 0 ? nl : m_pending.size();
    const int consumed = nl >= 0 ? nl + 1 : end;
    const QString newline = nl >= 0 ? QStringLiteral("\n") : QString();
    const QString line = m_pending.left(end);
    const int indent = indentOf(line);
    const QString rest = line.mid(indent);

    // A table is rendered once its last row is in: column widths depend on every row.
    if (!m_table.isEmpty()) {
        if (!complete && (rest.isEmpty() || rest.startsWith(QLatin1Char('|')))) return false;
        if (complete && rest.startsWith(QLatin1Char('|'))) {
            m_table << line;
            m_pending.remove(0, consumed);
            return true;
        }
        out += renderTable();
    }

    auto startLine = [&](Line kind, const QString &prefix, int skip) {
        m_line = kind;
        m_lineStarted = true;
        resetInline();
        out += prefix + style();
        m_pending.remove(0, skip);
    };

    if (m_inFence) {
        const bool couldClose = rest.isEmpty() || runLength(rest, 0, m_fenceChar) == rest.size();
        if (!complete && couldClose) return false;
        if (complete && runLength(rest, 0, m_fenceChar) >= m_fenceLen && rest.trimmed().size() == runLength(rest, 0, m_fenceChar)) {
            m_inFence = false;
            out += sgr(m_palette.dim) + line + kReset + newline;
            m_pending.remove(0, consumed);
            return true;
        }
        startLine(Line::Code, QString(), 0);
        return true;
    }

    if (rest.isEmpty()) {
        if (!complete) return false;
        out += line + newline;
        m_pending.remove(0, consumed);
        return true;
    }

    // Fences: ``` or ~~~, with an optional language.
    const QChar first = rest.at(0);
    if (first == QLatin1Char('`') || first == QLatin1Char('~')) {
        const int run = runLength(rest, 0, first);
        if (!complete && run == rest.size() && run < 3) return false;
        if (run >= 3 && (first == QLatin1Char('~') || !rest.mid(run).contains(QLatin1Char('`')))) {
            if (!complete) return false;
            m_inFence = true;
            m_fenceChar = first;
            m_fenceLen = run;
            out += sgr(m_palette.dim) + line + kReset + newline;
            m_pending.remove(0, consumed);
            return true;
        }
    }

    if (first == QLatin1Char('|')) {
        if (!complete) return false;
        m_table << line;
        m_pending.remove(0, consumed);
        return true;
    }

    int ruleCount = 0;
    if (ruleShape(rest, &ruleCount)) {
        if (!complete) return false;
        if (ruleCount >= 3) {
            out += sgr(m_palette.dim) + QString(40, QChar(0x2500)) + kReset + newline;
            m_pending.remove(0, consumed);
            return true;
        }
    }

    if (first == QLatin1Char('#')) {
        const int hashes = runLength(rest, 0, first);
        if (!complete && hashes == rest.size() && hashes <= 6) return false;
        if (hashes <= 6 && (hashes == rest.size() || rest.at(hashes) == QLatin1Char(' '))) {
            int skip = indent + hashes;
            while (skip < end && m_pending.at(skip) == QLatin1Char(' ')) ++skip;
            startLine(hashes == 1 ? Line::Heading1 : Line::Heading, line.left(indent), skip);
            return true;
        }
    }

    if (first == QLatin1Char('>')) {
        if (!complete && rest.size() == 1) return false;
        int skip = indent + 1;
        if (skip < end && m_pending.at(skip) == QLatin1Char(' ')) ++skip;
        startLine(Line::Quote, line.left(indent) + sgr(m_palette.marker) + QStringLiteral("▎ "), skip);
        return true;
    }

    if (first == QLatin1Char('-') || first == QLatin1Char('*') || first == QLatin1Char('+')) {
        if (!complete && rest.size() == 1) return false;
        if (rest.size() == 1 || rest.at(1) == QLatin1Char(' ')) {
            const QString after = rest.mid(2);
            static const QStringList boxes = {QStringLiteral("[ ] "), QStringLiteral("[x] "), QStringLiteral("[X] ")};
            if (!complete)
                for (const QString &box : boxes)
                    if (box.startsWith(after) && after.size() < box.size()) return false;
            const int level = indent / 2;
            QString glyph = level == 0 ? QStringLiteral("•") : level == 1 ? QStringLiteral("◦") : QStringLiteral("▪");
            int skip = indent + std::min<int>(2, rest.size());
            if (after.startsWith(boxes.at(0))) { glyph = QStringLiteral("☐"); skip += 4; }
            else if (after.startsWith(boxes.at(1)) || after.startsWith(boxes.at(2))) { glyph = QStringLiteral("☑"); skip += 4; }
            startLine(Line::Paragraph, line.left(indent) + sgr(m_palette.marker) + glyph + QLatin1Char(' '), skip);
            return true;
        }
    }

    if (first.isDigit()) {
        int digits = 0;
        while (digits < rest.size() && rest.at(digits).isDigit()) ++digits;
        if (!complete && digits == rest.size() && digits <= 9) return false;
        if (digits <= 9 && digits < rest.size() && (rest.at(digits) == QLatin1Char('.') || rest.at(digits) == QLatin1Char(')'))) {
            if (!complete && digits + 1 == rest.size()) return false;
            if (digits + 1 == rest.size() || rest.at(digits + 1) == QLatin1Char(' ')) {
                const int skip = indent + std::min<int>(digits + 2, rest.size());
                startLine(Line::Paragraph, line.left(indent) + sgr(m_palette.marker) + rest.left(digits + 1) + QLatin1Char(' '), skip);
                return true;
            }
        }
    }

    startLine(Line::Paragraph, line.left(indent), indent);
    return true;
}

// One step of inline Markdown at m_pending[i]. False when the text from i needs more input.
bool MarkdownAnsi::inlineStep(QString &out, int &i) {
    const QString &text = m_pending;
    const QChar c = text.at(i);
    const auto atEnd = [&](int j) { return j >= text.size(); };
    const auto next = [&](int j) { return atEnd(j) ? QChar(QLatin1Char(' ')) : text.at(j); };
    const auto isSpace = [](QChar ch) { return ch.isNull() || ch.isSpace(); };

    // Card #CVHT: a held first word is classified before any inline marker is handled, so the
    // markers of a bold run are never mistaken for its text.
    if (!m_boldHold.isEmpty()
        && (c == QLatin1Char('*') || c == QLatin1Char('_') || c == QLatin1Char('`')
            || c == QLatin1Char('~') || c == QLatin1Char('[') || c == QLatin1Char('\\')))
        flushBoldHold(out);

    if (c == QLatin1Char('\\') && m_codeRun == 0) {
        if (atEnd(i + 1) && !m_final) return false;
        const QChar escaped = next(i + 1);
        if (escaped.isPunct() || escaped.isSymbol()) { out += escaped; m_prev = escaped; i += 2; return true; }
        out += c; m_prev = c; ++i;
        return true;
    }

    if (c == QLatin1Char('`')) {
        const int run = runLength(text, i, c);
        if (atEnd(i + run) && !m_final) return false;
        if (m_codeRun == 0) m_codeRun = run;
        else if (run == m_codeRun) m_codeRun = 0;
        else { out += text.mid(i, run); i += run; return true; }
        out += style();
        m_prev = c; i += run;
        return true;
    }
    if (m_codeRun > 0) { out += c; m_prev = c; ++i; return true; }

    if (c == QLatin1Char('*') || c == QLatin1Char('_')) {
        const int run = runLength(text, i, c);
        if (atEnd(i + run) && !m_final) return false;
        const QChar after = next(i + run);
        bool canOpen = !isSpace(after), canClose = !isSpace(m_prev);
        if (c == QLatin1Char('_')) {   // snake_case is not emphasis
            canOpen = canOpen && !m_prev.isLetterOrNumber();
            canClose = canClose && !after.isLetterOrNumber();
        }
        int left = run > 3 ? 0 : run;
        bool changed = false;
        if (canClose && left > 0) {
            if (left >= 2 && m_bold) { m_bold = false; m_boldRole = BoldRole::None; left -= 2; changed = true; }
            if (left >= 1 && m_italic) { m_italic = false; left -= 1; changed = true; }
            if (left >= 2 && m_bold) { m_bold = false; m_boldRole = BoldRole::None; left -= 2; changed = true; }
        }
        if (!changed && canOpen && left > 0) {
            if (left >= 2) m_bold = true;
            if (left != 2) m_italic = true;
            left = 0;
            changed = true;
            if (m_bold) { m_boldRole = BoldRole::None; m_boldHold.clear(); }   // a fresh run: classify it
        }
        if (changed) out += style();
        if (!changed) left = run;
        if (left > 0) out += QString(left, c);
        m_prev = c; i += run;
        return true;
    }

    if (c == QLatin1Char('~')) {
        const int run = runLength(text, i, c);
        if (atEnd(i + run) && !m_final) return false;
        if (run == 2 && ((m_strike && !isSpace(m_prev)) || (!m_strike && !isSpace(next(i + run))))) {
            m_strike = !m_strike;
            out += style();
        } else {
            out += text.mid(i, run);
        }
        m_prev = c; i += run;
        return true;
    }

    if (c == QLatin1Char('[')) {
        // [text](url) on one line; held until it resolves, bounded so a stray '[' cannot stall.
        const int lineEnd = text.indexOf(QLatin1Char('\n'), i);
        const int limit = lineEnd < 0 ? text.size() : lineEnd;
        const int close = text.indexOf(QLatin1Char(']'), i);
        const bool open = lineEnd < 0 && !m_final && limit - i < 400;
        if ((close < 0 || close > limit) && open) return false;
        if (close >= 0 && close < limit) {
            if (close + 1 >= limit && open) return false;
            if (close + 1 < limit && text.at(close + 1) == QLatin1Char('(')) {
                const int paren = text.indexOf(QLatin1Char(')'), close + 2);
                if ((paren < 0 || paren > limit) && open) return false;
                if (paren >= 0 && paren < limit) {
                    const QString label = text.mid(i + 1, close - i - 1);
                    const QString url = text.mid(close + 2, paren - close - 2).trimmed();
                    out += sgr(QStringLiteral("0;") + m_palette.link) + label + style();
                    if (!url.isEmpty() && url != label) out += sgr(m_palette.dim) + QStringLiteral(" (") + url + QLatin1Char(')') + style();
                    m_prev = QLatin1Char(')');
                    i = paren + 1;
                    return true;
                }
            }
        }
    }

    // Card #CVHT: the first characters of a bold run are held (bounded) until its first word can
    // be classified, so a role colour can start at the run's very first character.
    if (m_bold && m_boldRole == BoldRole::None && m_boldHold.size() < kBoldHoldMax) {
        m_boldHold += c;
        m_prev = c;
        ++i;
        if (m_boldHold.size() >= kBoldHoldMax) flushBoldHold(out);
        return true;
    }

    out += c; m_prev = c; ++i;
    return true;
}

// Classifies the held first word of a bold run and emits it in that role's colour (plain bold when
// the word matches nothing). Card #CVHT.
void MarkdownAnsi::flushBoldHold(QString &out) {
    if (m_boldHold.isEmpty()) return;
    QString word;
    for (const QChar ch : m_boldHold) {
        if (ch.isSpace()) break;
        word += ch;
    }
    while (!word.isEmpty() && !word.at(0).isLetterOrNumber()) word.remove(0, 1);
    while (!word.isEmpty() && !word.back().isLetterOrNumber()) word.chop(1);
    word = word.toLower();
    m_boldRole = BoldRole::Plain;
    if (!word.isEmpty()) {
        if (kDoneWords.contains(word)) m_boldRole = BoldRole::Done;
        else if (kNeedWords.contains(word)) m_boldRole = BoldRole::Need;
        else if (kProblemWords.contains(word)) m_boldRole = BoldRole::Problem;
    }
    out += style() + m_boldHold;
    m_boldHold.clear();
}

QString MarkdownAnsi::renderInline(const QString &text, bool bold) const {
    // A table cell is rendered by a second instance, which needs the whole palette and not just
    // the base: inline code and links inside a cell are the same colours as anywhere else.
    Palette cell = m_palette;
    if (bold) cell.base += QStringLiteral(";1");
    MarkdownAnsi inner(cell);
    // Two statements, not `feed(text) + finish()`: the order in which `+` evaluates its operands
    // is unspecified, and g++ 15 on x86_64 runs finish() first, which flushes an empty renderer
    // and then feeds text nothing will ever emit. Every numeric table cell came out blank on that
    // toolchain while the same source was right here (aarch64, g++ 13), 2026-09-18.
    QString rendered = inner.feed(text);
    rendered += inner.finish();
    return rendered;
}

QString MarkdownAnsi::renderTable() {
    const QStringList rows = m_table;
    m_table.clear();
    static const QRegularExpression separatorCell(QStringLiteral("^:?-+:?$"));
    bool hasHeader = rows.size() >= 2;
    const QStringList separator = hasHeader ? tableCells(rows.at(1)) : QStringList();
    for (const QString &cell : separator) if (!separatorCell.match(cell).hasMatch()) hasHeader = false;

    QString out;
    if (!hasHeader) {   // not a table after all: plain lines
        for (const QString &row : rows) out += renderInline(row, false) + QLatin1Char('\n');
        return out;
    }

    QList<QStringList> cells;
    int columns = 0;
    for (int r = 0; r < rows.size(); ++r) {
        if (r == 1) continue;
        QStringList rendered;
        for (const QString &cell : tableCells(rows.at(r))) rendered << renderInline(cell, r == 0);
        columns = std::max(columns, int(rendered.size()));
        cells << rendered;
    }
    QVector<int> widths(columns, 0);
    QVector<QChar> align(columns, QLatin1Char('l'));
    for (int col = 0; col < columns && col < separator.size(); ++col) {
        const QString s = separator.at(col);
        if (s.startsWith(QLatin1Char(':')) && s.endsWith(QLatin1Char(':'))) align[col] = QLatin1Char('c');
        else if (s.endsWith(QLatin1Char(':'))) align[col] = QLatin1Char('r');
    }
    for (const QStringList &row : cells)
        for (int col = 0; col < row.size(); ++col) widths[col] = std::max(widths[col], visibleWidth(row.at(col)));

    const QString bar = sgr(m_palette.dim) + QStringLiteral(" │ ");
    for (int r = 0; r < cells.size(); ++r) {
        QString line;
        for (int col = 0; col < columns; ++col) {
            const QString cell = col < cells.at(r).size() ? cells.at(r).at(col) : QString();
            const int pad = widths.at(col) - visibleWidth(cell);
            const int before = align.at(col) == QLatin1Char('r') ? pad : align.at(col) == QLatin1Char('c') ? pad / 2 : 0;
            if (col > 0) line += bar;
            line += sgr(QStringLiteral("0;") + m_palette.base) + QString(before, QLatin1Char(' '));
            line += cell;
            line += sgr(QStringLiteral("0;") + m_palette.base) + QString(pad - before, QLatin1Char(' '));
        }
        out += line + kReset + QLatin1Char('\n');
        if (r == 0) {
            QStringList dashes;
            for (int col = 0; col < columns; ++col) dashes << QString(widths.at(col), QChar(0x2500));
            out += sgr(m_palette.dim) + dashes.join(QStringLiteral("─┼─")) + kReset + QLatin1Char('\n');
        }
    }
    return out;
}

}  // namespace relay
