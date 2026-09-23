// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AnsiSerializer.h"

#include <QStringList>

namespace relay {
namespace {

struct SgrState {
    uint32_t fg = CellColor::defaultColor();
    uint32_t bg = CellColor::defaultColor();
    bool bold = false;
    bool italic = false;
    bool faint = false;
    bool underline = false;
    bool doubleUnderline = false;
    bool curlyUnderline = false;
    bool blink = false;
    bool reverse = false;
    bool conceal = false;
    bool strike = false;

    bool operator==(const SgrState &other) const {
        return fg == other.fg && bg == other.bg && bold == other.bold && italic == other.italic
               && faint == other.faint && underline == other.underline
               && doubleUnderline == other.doubleUnderline && curlyUnderline == other.curlyUnderline
               && blink == other.blink && reverse == other.reverse && conceal == other.conceal
               && strike == other.strike;
    }
    bool operator!=(const SgrState &other) const { return !(*this == other); }

    bool isDefault() const {
        return fg == CellColor::defaultColor() && bg == CellColor::defaultColor() && !bold && !italic
               && !faint && !underline && !doubleUnderline && !curlyUnderline && !blink && !reverse
               && !conceal && !strike;
    }
};

SgrState stateForCell(const Cell &cell)
{
    SgrState s;
    s.fg = cell.fg;
    s.bg = cell.bg;
    s.bold = (cell.attrs & AttrBold) != 0;
    s.italic = (cell.attrs & AttrItalic) != 0;
    s.faint = (cell.attrs & AttrFaint) != 0;
    s.underline = (cell.attrs & AttrUnderline) != 0;
    s.doubleUnderline = (cell.attrs & AttrDoubleUnderline) != 0;
    s.curlyUnderline = (cell.attrs & AttrCurlyUnderline) != 0;
    s.blink = (cell.attrs & AttrBlink) != 0;
    s.reverse = (cell.attrs & AttrReverse) != 0;
    s.conceal = (cell.attrs & AttrConceal) != 0;
    s.strike = (cell.attrs & AttrStrike) != 0;
    return s;
}

void appendColour(QString *out, uint32_t colour, bool foreground)
{
    const int base = foreground ? 38 : 48;
    const CellColor::Kind kind = CellColor::kind(colour);
    const uint32_t value = CellColor::value(colour);
    if (kind == CellColor::Indexed) {
        out->append(QString::number(base));
        out->append(QLatin1Char(';'));
        out->append(QLatin1Char('5'));
        out->append(QLatin1Char(';'));
        out->append(QString::number(value));
    } else if (kind == CellColor::Rgb) {
        out->append(QString::number(base));
        out->append(QLatin1Char(';'));
        out->append(QLatin1Char('2'));
        out->append(QLatin1Char(';'));
        out->append(QString::number((value >> 16) & 0xFF));
        out->append(QLatin1Char(';'));
        out->append(QString::number((value >> 8) & 0xFF));
        out->append(QLatin1Char(';'));
        out->append(QString::number(value & 0xFF));
    }
}

// The parameter list for `state` assuming the terminal starts from default attributes.
// If state is default, returns "0".
QString sgrParams(const SgrState &state)
{
    if (state.isDefault()) return QStringLiteral("0");

    QString params;
    auto sep = [&params]() {
        if (!params.isEmpty()) params.append(QLatin1Char(';'));
    };

    if (state.bold) { sep(); params.append(QLatin1Char('1')); }
    if (state.faint) { sep(); params.append(QLatin1Char('2')); }
    if (state.italic) { sep(); params.append(QLatin1Char('3')); }
    if (state.underline) { sep(); params.append(QLatin1Char('4')); }
    if (state.doubleUnderline) { sep(); params.append(QLatin1String("21")); }
    if (state.curlyUnderline) { sep(); params.append(QLatin1String("4:3")); }
    if (state.blink) { sep(); params.append(QLatin1Char('5')); }
    if (state.reverse) { sep(); params.append(QLatin1Char('7')); }
    if (state.conceal) { sep(); params.append(QLatin1Char('8')); }
    if (state.strike) { sep(); params.append(QLatin1Char('9')); }

    if (state.fg != CellColor::defaultColor()) {
        sep();
        appendColour(&params, state.fg, true);
    }
    if (state.bg != CellColor::defaultColor()) {
        sep();
        appendColour(&params, state.bg, false);
    }
    return params;
}

}  // namespace

QString lineToAnsi(const Line &line, const std::function<QString(uint32_t, int)> &linkUri)
{
    QString out;
    SgrState current;
    bool emittedSgr = false;
    QString activeLink;

    for (size_t i = 0; i < line.cells.size(); ++i) {
        const Cell &cell = line.cells[i];
        if (cell.ch == kWideTail) continue;

        const QString uri = linkUri && cell.link ? linkUri(cell.link, int(i)) : QString();
        if (uri != activeLink) {
            out += QStringLiteral("\x1b]8;;") + uri + QStringLiteral("\x1b\\");
            activeLink = uri;
        }
        const SgrState next = stateForCell(cell);
        if (next != current) {
            out.append(QLatin1Char('\x1b'));
            out.append(QLatin1Char('['));
            out.append(sgrParams(next));
            out.append(QLatin1Char('m'));
            current = next;
            emittedSgr = true;
        }

        out.append(line.cellText(cell));
    }

    // Trim trailing blanks exactly the way Line::text() does, so a plain line and its
    // ANSI cousin end at the same column.
    int end = out.size();
    while (end > 0 && out.at(end - 1) == QLatin1Char(' ')) --end;
    if (end < out.size()) out.resize(end);

    if (!activeLink.isEmpty()) out += QStringLiteral("\x1b]8;;\x1b\\");
    if (emittedSgr && !out.isEmpty() && !current.isDefault()) {
        out.append(QLatin1String("\x1b[0m"));
    }
    return out;
}

QStringList linesToAnsi(const std::vector<Line> &lines)
{
    QStringList out;
    out.reserve(int(lines.size()));
    for (const Line &line : lines) out.append(lineToAnsi(line));
    return out;
}

} // namespace relay
