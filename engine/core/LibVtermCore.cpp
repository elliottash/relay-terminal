// SPDX-License-Identifier: GPL-3.0-or-later
#include "LibVtermCore.h"

#include <QUrl>

extern "C" {
#include <vterm.h>
}

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>

namespace relay {
namespace {

const uint32_t kVtermTail = 0xFFFFFFFF;

inline uint32_t rgbOf(const VTermColor &c)
{
    return CellColor::rgb(c.rgb.red, c.rgb.green, c.rgb.blue);
}

VTermModifier toVtermMods(uint8_t m)
{
    int out = VTERM_MOD_NONE;
    if (m & ModShift)
        out |= VTERM_MOD_SHIFT;
    if (m & ModAlt)
        out |= VTERM_MOD_ALT;
    if (m & ModCtrl)
        out |= VTERM_MOD_CTRL;
    return VTermModifier(out);
}

VTermKey toVtermKey(Key k)
{
    switch (k) {
    case Key::Enter: return VTERM_KEY_ENTER;
    case Key::Tab: return VTERM_KEY_TAB;
    case Key::Backspace: return VTERM_KEY_BACKSPACE;
    case Key::Escape: return VTERM_KEY_ESCAPE;
    case Key::Up: return VTERM_KEY_UP;
    case Key::Down: return VTERM_KEY_DOWN;
    case Key::Left: return VTERM_KEY_LEFT;
    case Key::Right: return VTERM_KEY_RIGHT;
    case Key::Insert: return VTERM_KEY_INS;
    case Key::Delete: return VTERM_KEY_DEL;
    case Key::Home: return VTERM_KEY_HOME;
    case Key::End: return VTERM_KEY_END;
    case Key::PageUp: return VTERM_KEY_PAGEUP;
    case Key::PageDown: return VTERM_KEY_PAGEDOWN;
    case Key::Kp0: return VTERM_KEY_KP_0;
    case Key::Kp1: return VTERM_KEY_KP_1;
    case Key::Kp2: return VTERM_KEY_KP_2;
    case Key::Kp3: return VTERM_KEY_KP_3;
    case Key::Kp4: return VTERM_KEY_KP_4;
    case Key::Kp5: return VTERM_KEY_KP_5;
    case Key::Kp6: return VTERM_KEY_KP_6;
    case Key::Kp7: return VTERM_KEY_KP_7;
    case Key::Kp8: return VTERM_KEY_KP_8;
    case Key::Kp9: return VTERM_KEY_KP_9;
    case Key::KpMultiply: return VTERM_KEY_KP_MULT;
    case Key::KpPlus: return VTERM_KEY_KP_PLUS;
    case Key::KpComma: return VTERM_KEY_KP_COMMA;
    case Key::KpMinus: return VTERM_KEY_KP_MINUS;
    case Key::KpPeriod: return VTERM_KEY_KP_PERIOD;
    case Key::KpDivide: return VTERM_KEY_KP_DIVIDE;
    case Key::KpEnter: return VTERM_KEY_KP_ENTER;
    case Key::KpEqual: return VTERM_KEY_KP_EQUAL;
    default: break;
    }
    if (k >= Key::F1 && k <= Key::F24)
        return VTermKey(VTERM_KEY_FUNCTION(1 + int(k) - int(Key::F1)));
    return VTERM_KEY_NONE;
}

bool isWordBoundary(char32_t c)
{
    switch (c) {
    case 0: case U' ': case U'\t': case U'"': case U'\'': case U'`': case U'(': case U')': case U'[': case U']':
    case U'{': case U'}': case U'<': case U'>': case U'|': case U',': case U';': case 0x2502:
        return true;
    default:
        return false;
    }
}

char32_t firstCodepoint(const Line &l, const Cell &c)
{
    if (c.ch == kWideTail)
        return U'x';
    if (c.attrs & AttrCluster) {
        std::u32string cps;
        l.cellCodepoints(c, &cps);
        return cps.empty() ? 0 : cps[0];
    }
    return c.ch;
}

QString hexColor(uint32_t rgb)
{
    const auto h = [](int v) { return QStringLiteral("%1").arg(v * 257, 4, 16, QLatin1Char('0')); };
    return QStringLiteral("rgb:%1/%2/%3").arg(h(int(rgb >> 16) & 255), h(int(rgb >> 8) & 255), h(int(rgb) & 255));
}

} // namespace

struct LibVtermCore::Impl {
    LibVtermCore *q = nullptr;
    VTerm *vt = nullptr;
    VTermState *state = nullptr;
    VTermScreen *screen = nullptr;
    int rowsN = 24;
    int colsN = 80;

    // Scrollback ring (oldest at head).
    std::vector<Line> ring;
    size_t head = 0;
    size_t count = 0;
    int limit = 10000;
    qint64 pushed = 0; // line id of screen row 0
    int scrollOffset = 0;

    std::vector<uint8_t> dirty;
    bool allDirty = true;
    bool decorDirty = false;
    quint64 changeCounter = 0;

    CursorState cursor;
    bool alt = false;
    int mouseMode = VTERM_PROP_MOUSE_NONE;
    QString title;
    QByteArray titleBuf;
    QByteArray oscBuf;
    std::unordered_map<std::string, uint32_t> linkIds;
    std::vector<QString> linkUris{QString()};
    bool clipboardAllowed = false;
    QByteArray clipBuf;
    char selectionBuf[8192];
    bool reflow = true;
    uint32_t defFg = 0xd8d8d8;
    uint32_t defBg = 0x1c1e24;
    Line scratch;

    bool resizing = false;
    size_t minCountDuringResize = 0;

    // Selection (line ids / columns, inclusive).
    struct Pos {
        qint64 line;
        int col;
    };
    bool selActive = false;
    SelectionUnit selUnit = SelectionUnit::Cell;
    bool selRect = false;
    Pos selAnchor{0, 0};
    Pos selExtent{0, 0};
    Pos selStart{0, 0};
    Pos selEnd{0, 0};

    // Search.
    struct Match {
        qint64 line;
        int start;
        int end;
    };
    QString needle;
    std::vector<Match> matches;
    int current = -1;
    quint64 matchesAt = 0;

    static bool posLess(const Pos &a, const Pos &b) { return a.line < b.line || (a.line == b.line && a.col < b.col); }

    qint64 firstId() const { return pushed - qint64(count); }
    qint64 topVisibleId() const { return pushed - scrollOffset; }
    Line &ringAt(size_t i) { return ring[(head + i) % ring.size()]; }
    const Line &ringAt(size_t i) const { return ring[(head + i) % ring.size()]; }

    // ---------------------------------------------------------------- cells
    void convertCell(const VTermScreenCell &vc, Line *line, Cell *c) const
    {
        *c = Cell();
        if (vc.chars[0] == kVtermTail) {
            c->ch = kWideTail;
            c->width = 0;
        } else {
            int n = 0;
            while (n < VTERM_MAX_CHARS_PER_CELL && vc.chars[n])
                ++n;
            if (n <= 1) {
                c->ch = n ? vc.chars[0] : 0;
            } else {
                char32_t cps[VTERM_MAX_CHARS_PER_CELL];
                for (int i = 0; i < n; ++i)
                    cps[i] = vc.chars[i];
                line->appendCluster(c, cps, n);
            }
            c->width = uint8_t(vc.width > 0 ? vc.width : 1);
        }
        if (!VTERM_COLOR_IS_DEFAULT_FG(&vc.fg)) {
            VTermColor col = vc.fg;
            vterm_screen_convert_color_to_rgb(screen, &col);
            c->fg = rgbOf(col);
        }
        if (!VTERM_COLOR_IS_DEFAULT_BG(&vc.bg)) {
            VTermColor col = vc.bg;
            vterm_screen_convert_color_to_rgb(screen, &col);
            c->bg = rgbOf(col);
        }
        uint16_t a = 0;
        if (vc.attrs.bold) a |= AttrBold;
        if (vc.attrs.italic) a |= AttrItalic;
        if (vc.attrs.blink) a |= AttrBlink;
        if (vc.attrs.reverse) a |= AttrReverse;
        if (vc.attrs.conceal) a |= AttrConceal;
        if (vc.attrs.strike) a |= AttrStrike;
        if (vc.attrs.underline == VTERM_UNDERLINE_DOUBLE) a |= AttrDoubleUnderline;
        else if (vc.attrs.underline == VTERM_UNDERLINE_CURLY) a |= AttrCurlyUnderline;
        else if (vc.attrs.underline) a |= AttrUnderline;
        c->attrs |= a;
        c->link = vc.hyperlink;
    }

    void readScreenRow(int row, Line *out) const
    {
        out->clear();
        out->cells.resize(size_t(colsN));
        VTermScreenCell vc;
        for (int col = 0; col < colsN; ++col) {
            if (!vterm_screen_get_cell(screen, VTermPos{row, col}, &vc))
                break;
            convertCell(vc, out, &out->cells[size_t(col)]);
        }
        const VTermLineInfo *info = vterm_state_get_lineinfo(state, row);
        out->continuation = info->continuation;
        out->marks = uint8_t(info->relay_marks);
        out->wrapColumns = uint16_t(colsN);
    }

    bool lineAt(qint64 id, Line *out) const
    {
        if (id >= pushed) {
            const int row = int(id - pushed);
            if (row >= rowsN)
                return false;
            readScreenRow(row, out);
            return true;
        }
        const qint64 idx = id - firstId();
        if (idx < 0)
            return false;
        *out = ringAt(size_t(idx));
        out->selectionStart = out->selectionEnd = -1;
        out->highlights.clear();
        return true;
    }

    // Cheaper than lineAt for scrollback: no copy.
    const Line *peekLine(qint64 id, Line *scratchLine) const
    {
        if (id >= pushed) {
            if (id - pushed >= rowsN)
                return nullptr;
            readScreenRow(int(id - pushed), scratchLine);
            return scratchLine;
        }
        const qint64 idx = id - firstId();
        if (idx < 0)
            return nullptr;
        return &ringAt(size_t(idx));
    }

    // ---------------------------------------------------------------- scrollback
    Line *pushSlot()
    {
        if (limit <= 0)
            return nullptr;
        if (count < ring.size()) {
            const size_t i = (head + count) % ring.size();
            ++count;
            return &ring[i];
        }
        if (ring.size() < size_t(limit)) {
            if (head != 0) {
                std::rotate(ring.begin(), ring.begin() + long(head), ring.end());
                head = 0;
            }
            ring.emplace_back();
            ++count;
            return &ring.back();
        }
        Line *l = &ring[head];
        head = (head + 1) % ring.size();
        return l;
    }

    static int pushLine(int cols, const VTermScreenCell *cells, const VTermLineInfo *info, void *user)
    {
        auto *d = static_cast<Impl *>(user);
        Line *l = d->pushSlot();
        if (l) {
            l->clear();
            l->cells.resize(size_t(cols));
            for (int i = 0; i < cols; ++i)
                d->convertCell(cells[i], l, &l->cells[size_t(i)]);
            while (!l->cells.empty() && l->cells.back().isBlank() && l->cells.back().attrs == 0)
                l->cells.pop_back();
            l->continuation = info->continuation;
            l->marks = uint8_t(info->relay_marks);
            l->wrapColumns = uint16_t(cols);
        }
        ++d->pushed;
        if (d->scrollOffset > 0 && !d->resizing)
            d->scrollOffset = std::min<int>(d->scrollOffset + 1, int(d->count));
        d->allDirty = true;
        return 1;
    }

    void fillVtermCell(const Line &l, const Cell &c, VTermScreenCell *o, bool lastColumn) const
    {
        std::memset(o, 0, sizeof *o);
        VTermColor fg, bg;
        vterm_state_get_default_colors(state, &fg, &bg);
        fg.type |= VTERM_COLOR_DEFAULT_FG;
        bg.type |= VTERM_COLOR_DEFAULT_BG;
        o->fg = fg;
        o->bg = bg;
        o->width = 1;
        if (c.ch == kWideTail) {
            o->chars[0] = kVtermTail;
        } else {
            std::u32string cps;
            l.cellCodepoints(c, &cps);
            for (size_t i = 0; i < cps.size() && i < VTERM_MAX_CHARS_PER_CELL; ++i)
                o->chars[i] = cps[i];
            o->width = (c.width == 2 && !lastColumn) ? 2 : 1;
            if (c.width == 2 && lastColumn)
                o->chars[0] = 0;
        }
        if (CellColor::kind(c.fg) == CellColor::Rgb) {
            const uint32_t v = CellColor::value(c.fg);
            vterm_color_rgb(&o->fg, uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v));
        }
        if (CellColor::kind(c.bg) == CellColor::Rgb) {
            const uint32_t v = CellColor::value(c.bg);
            vterm_color_rgb(&o->bg, uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v));
        }
        o->attrs.bold = (c.attrs & AttrBold) != 0;
        o->attrs.italic = (c.attrs & AttrItalic) != 0;
        o->attrs.blink = (c.attrs & AttrBlink) != 0;
        o->attrs.reverse = (c.attrs & AttrReverse) != 0;
        o->attrs.conceal = (c.attrs & AttrConceal) != 0;
        o->attrs.strike = (c.attrs & AttrStrike) != 0;
        o->attrs.underline = (c.attrs & AttrDoubleUnderline) ? VTERM_UNDERLINE_DOUBLE
            : (c.attrs & AttrCurlyUnderline)                 ? VTERM_UNDERLINE_CURLY
            : (c.attrs & AttrUnderline)                      ? VTERM_UNDERLINE_SINGLE
                                                             : 0;
        o->hyperlink = c.link;
    }

    static int popLine(int cols, VTermScreenCell *cells, VTermLineInfo *info, void *user)
    {
        auto *d = static_cast<Impl *>(user);
        if (d->count == 0)
            return 0;
        const Line &l = d->ringAt(d->count - 1);
        const Cell blank;
        for (int i = 0; i < cols; ++i) {
            const Cell &c = i < int(l.cells.size()) ? l.cells[size_t(i)] : blank;
            d->fillVtermCell(l, c, &cells[i], i == cols - 1);
        }
        info->continuation = l.continuation;
        info->relay_marks = l.marks & 0xF;
        --d->count;
        --d->pushed;
        if (d->resizing)
            d->minCountDuringResize = std::min(d->minCountDuringResize, d->count);
        d->scrollOffset = std::min<int>(d->scrollOffset, int(d->count));
        d->allDirty = true;
        return 1;
    }

    static int sbClear(void *user)
    {
        auto *d = static_cast<Impl *>(user);
        d->count = 0;
        d->head = 0;
        d->scrollOffset = 0;
        d->allDirty = true;
        return 1;
    }

    // Rewrap scrollback lines [fromIndex, count) (extended back to the start of
    // their logical line) to newCols.
    void rewrap(size_t fromIndex, int newCols)
    {
        if (count == 0 || newCols <= 0)
            return;
        std::vector<Line> lines;
        lines.reserve(count);
        for (size_t i = 0; i < count; ++i)
            lines.push_back(std::move(ringAt(i)));
        size_t s = std::min(fromIndex, count);
        while (s > 0 && s < count && lines[s].continuation)
            --s;
        std::vector<Line> out;
        out.reserve(count + 16);
        for (size_t i = 0; i < s; ++i)
            out.push_back(std::move(lines[i]));

        std::u32string cps;
        size_t i = s;
        while (i < count) {
            size_t j = i + 1;
            while (j < count && lines[j].continuation)
                ++j;
            Line cur;
            cur.marks = lines[i].marks;
            cur.continuation = lines[i].continuation; // only true when its head left the scrollback
            cur.wrapColumns = uint16_t(newCols);
            int col = 0;
            for (size_t k = i; k < j; ++k) {
                const Line &src = lines[k];
                const int srcCols = k + 1 < j ? std::max<int>(int(src.cells.size()), src.wrapColumns) : int(src.cells.size());
                for (int idx = 0; idx < srcCols; ++idx) {
                    const Cell c = idx < int(src.cells.size()) ? src.cells[size_t(idx)] : Cell();
                    if (c.ch == kWideTail)
                        continue;
                    const int w = c.width == 2 ? 2 : 1;
                    if (col + w > newCols) {
                        out.push_back(std::move(cur));
                        cur = Line();
                        cur.continuation = true;
                        cur.wrapColumns = uint16_t(newCols);
                        col = 0;
                    }
                    Cell nc = c;
                    if (c.attrs & AttrCluster) {
                        cps.clear();
                        src.cellCodepoints(c, &cps);
                        nc.attrs &= uint16_t(~AttrCluster);
                        cur.appendCluster(&nc, cps.data(), int(cps.size()));
                    }
                    cur.cells.push_back(nc);
                    if (w == 2) {
                        Cell tail = c;
                        tail.ch = kWideTail;
                        tail.width = 0;
                        tail.attrs &= uint16_t(~AttrCluster);
                        cur.cells.push_back(tail);
                    }
                    col += w;
                }
            }
            while (!cur.cells.empty() && cur.cells.back().isBlank() && cur.cells.back().attrs == 0)
                cur.cells.pop_back();
            out.push_back(std::move(cur));
            i = j;
        }
        if (limit > 0 && out.size() > size_t(limit))
            out.erase(out.begin(), out.begin() + long(out.size() - size_t(limit)));
        const qint64 first = firstId();
        ring = std::move(out);
        head = 0;
        count = ring.size();
        pushed = first + qint64(count);
    }

    // ---------------------------------------------------------------- libvterm callbacks
    static int onDamage(VTermRect r, void *user)
    {
        auto *d = static_cast<Impl *>(user);
        for (int row = std::max(0, r.start_row); row < r.end_row && row < int(d->dirty.size()); ++row)
            d->dirty[size_t(row)] = 1;
        return 1;
    }
    static int onMoveRect(VTermRect dest, VTermRect, void *user)
    {
        return onDamage(dest, user);
    }
    static int onMoveCursor(VTermPos pos, VTermPos old, int visible, void *user)
    {
        auto *d = static_cast<Impl *>(user);
        d->cursor.row = pos.row;
        d->cursor.col = pos.col;
        d->cursor.visible = visible;
        if (old.row >= 0 && old.row < int(d->dirty.size()))
            d->dirty[size_t(old.row)] = 1;
        if (pos.row >= 0 && pos.row < int(d->dirty.size()))
            d->dirty[size_t(pos.row)] = 1;
        return 1;
    }
    static int onTermProp(VTermProp prop, VTermValue *val, void *user)
    {
        auto *d = static_cast<Impl *>(user);
        switch (prop) {
        case VTERM_PROP_CURSORVISIBLE:
            d->cursor.visible = val->boolean;
            break;
        case VTERM_PROP_CURSORBLINK:
            d->cursor.blink = val->boolean;
            break;
        case VTERM_PROP_CURSORSHAPE:
            d->cursor.shape = val->number == VTERM_PROP_CURSORSHAPE_UNDERLINE ? CursorShape::Underline
                : val->number == VTERM_PROP_CURSORSHAPE_BAR_LEFT             ? CursorShape::Bar
                                                                             : CursorShape::Block;
            break;
        case VTERM_PROP_ALTSCREEN:
            if (bool(val->boolean) != d->alt) {
                d->alt = val->boolean;
                d->scrollOffset = 0;
                d->allDirty = true;
                if (d->q->events.altScreenChanged)
                    d->q->events.altScreenChanged(d->alt);
            }
            break;
        case VTERM_PROP_MOUSE:
            d->mouseMode = val->number;
            break;
        case VTERM_PROP_TITLE:
            if (val->string.initial)
                d->titleBuf.clear();
            if (d->titleBuf.size() < 4096) // bytes; decoded once complete (UTF-8 may split across fragments)
                d->titleBuf.append(val->string.str, int(val->string.len));
            if (val->string.final) {
                d->title = QString::fromUtf8(d->titleBuf);
                if (d->q->events.titleChanged)
                    d->q->events.titleChanged(d->title);
            }
            break;
        default:
            break;
        }
        return 1;
    }
    static int onBell(void *user)
    {
        auto *d = static_cast<Impl *>(user);
        if (d->q->events.bell)
            d->q->events.bell();
        return 1;
    }
    static void onOutput(const char *s, size_t len, void *user)
    {
        auto *d = static_cast<Impl *>(user);
        if (d->q->events.reply)
            d->q->events.reply(s, len);
    }

    uint32_t internLink(const QByteArray &uri)
    {
        const std::string key(uri.constData(), size_t(uri.size()));
        auto it = linkIds.find(key);
        if (it != linkIds.end())
            return it->second;
        // Ids are never reused (cells in scrollback keep them); once the 24-bit
        // space is exhausted, new links are simply not clickable.
        if (linkUris.size() >= 0xFFFFFF)
            return 0;
        const uint32_t id = uint32_t(linkUris.size());
        linkUris.push_back(QString::fromUtf8(uri));
        linkIds.emplace(key, id);
        return id;
    }

    void handleOsc(int command, const QByteArray &body)
    {
        switch (command) {
        case 7: {
            const QUrl url(QString::fromUtf8(body));
            if (q->events.cwdChanged)
                q->events.cwdChanged(url.isValid() && !url.scheme().isEmpty() ? url.path() : QString::fromUtf8(body), url.host());
            break;
        }
        case 8: {
            const int semi = body.indexOf(';');
            const QByteArray uri = semi >= 0 ? body.mid(semi + 1) : QByteArray();
            vterm_screen_relay_set_hyperlink(screen, uri.isEmpty() ? 0 : internLink(uri));
            break;
        }
        case 9:
            if (!body.startsWith("4;") && q->events.notification)
                q->events.notification(QString(), QString::fromUtf8(body));
            break;
        case 777: {
            const QList<QByteArray> parts = body.split(';');
            if (parts.size() >= 2 && parts[0] == "notify" && q->events.notification)
                q->events.notification(QString::fromUtf8(parts.value(1)), QString::fromUtf8(parts.mid(2).join(';')));
            break;
        }
        case 10:
        case 11:
        case 12:
            if (body == "?") {
                const uint32_t rgb = command == 11 ? defBg : defFg;
                const QByteArray reply = "\x1b]" + QByteArray::number(command) + ';' + hexColor(rgb).toLatin1() + "\x1b\\";
                if (q->events.reply)
                    q->events.reply(reply.constData(), size_t(reply.size()));
            }
            break;
        case 133: {
            if (body.isEmpty())
                break;
            const char kind = body[0];
            const PromptMark mark = kind == 'A' ? MarkPromptStart
                : kind == 'B'                   ? MarkCommandStart
                : kind == 'C'                   ? MarkOutputStart
                : kind == 'D'                   ? MarkCommandFinished
                                                : PromptMark(0);
            if (!mark)
                break;
            vterm_state_relay_mark_cursor_line(state, mark);
            int exitCode = -1;
            if (kind == 'D' && body.size() > 2 && body[1] == ';') {
                bool ok = false;
                const int v = body.mid(2).split(';').value(0).toInt(&ok);
                if (ok)
                    exitCode = v;
            }
            VTermPos pos;
            vterm_state_get_cursorpos(state, &pos);
            if (q->events.promptMark)
                q->events.promptMark(mark, pos.row, exitCode);
            break;
        }
        default:
            break;
        }
    }

    static int onOsc(int command, VTermStringFragment frag, void *user)
    {
        auto *d = static_cast<Impl *>(user);
        switch (command) {
        case 7: case 8: case 9: case 10: case 11: case 12: case 133: case 777:
            break;
        default:
            return 0;
        }
        if (frag.initial)
            d->oscBuf.clear();
        if (d->oscBuf.size() < 65536)
            d->oscBuf.append(frag.str, int(frag.len));
        if (frag.final)
            d->handleOsc(command, d->oscBuf);
        return 1;
    }

    static int onSelectionSet(VTermSelectionMask mask, VTermStringFragment frag, void *user)
    {
        auto *d = static_cast<Impl *>(user);
        if (frag.initial)
            d->clipBuf.clear();
        if (d->clipBuf.size() < 1 << 20)
            d->clipBuf.append(frag.str, int(frag.len));
        if (frag.final && d->clipboardAllowed && d->q->events.clipboardWrite)
            d->q->events.clipboardWrite((mask & VTERM_SELECTION_CLIPBOARD) ? QStringLiteral("clipboard") : QStringLiteral("primary"),
                                        d->clipBuf);
        return 1;
    }
    static int onSelectionQuery(VTermSelectionMask, void *)
    {
        return 1; // never reveal the clipboard to programs
    }

    // ---------------------------------------------------------------- selection helpers
    Pos viewportPos(int row, int col) const
    {
        return Pos{topVisibleId() + std::max(0, std::min(row, rowsN - 1)), std::max(0, std::min(col, colsN - 1))};
    }

    int wordEdge(qint64 id, int col, int dir) const
    {
        Line tmp;
        const Line *l = peekLine(id, &tmp);
        if (!l)
            return col;
        const int n = int(l->cells.size());
        auto boundaryAt = [&](int c) {
            if (c < 0 || c >= n)
                return true;
            return isWordBoundary(firstCodepoint(*l, l->cells[size_t(c)]));
        };
        if (boundaryAt(col))
            return col;
        while (!boundaryAt(col + dir))
            col += dir;
        return col;
    }

    void normalizeSelection()
    {
        Pos a = selAnchor, b = selExtent;
        if (posLess(b, a))
            std::swap(a, b);
        if (selUnit == SelectionUnit::Word) {
            a.col = wordEdge(a.line, a.col, -1);
            b.col = wordEdge(b.line, b.col, +1);
        } else if (selUnit == SelectionUnit::Line) {
            Line tmp;
            while (a.line > firstId()) {
                const Line *l = peekLine(a.line, &tmp);
                if (!l || !l->continuation)
                    break;
                --a.line;
            }
            while (true) {
                const Line *l = peekLine(b.line + 1, &tmp);
                if (!l || !l->continuation)
                    break;
                ++b.line;
            }
            a.col = 0;
            b.col = colsN - 1;
        }
        selStart = a;
        selEnd = b;
        decorDirty = true;
    }

    void decorate(qint64 id, Line *line) const
    {
        if (selActive && id >= selStart.line && id <= selEnd.line) {
            if (selRect) {
                line->selectionStart = int16_t(std::min(selStart.col, selEnd.col));
                line->selectionEnd = int16_t(std::max(selStart.col, selEnd.col));
            } else {
                line->selectionStart = int16_t(id == selStart.line ? selStart.col : 0);
                line->selectionEnd = int16_t(id == selEnd.line ? selEnd.col : colsN - 1);
            }
        }
        if (!matches.empty()) {
            auto it = std::lower_bound(matches.begin(), matches.end(), id, [](const Match &m, qint64 v) { return m.line < v; });
            for (; it != matches.end() && it->line == id; ++it) {
                const bool cur = current >= 0 && &matches[size_t(current)] == &*it;
                line->highlights.push_back({uint16_t(it->start), uint16_t(it->end), cur});
            }
        }
    }

    void computeMatches()
    {
        matches.clear();
        current = -1;
        matchesAt = changeCounter;
        if (needle.isEmpty())
            return;
        const Qt::CaseSensitivity cs = Qt::CaseInsensitive; // same as libghostty-vt (ASCII case-insensitive)
        Line tmp;
        std::u32string cps;
        const qint64 end = pushed + rowsN;
        for (qint64 id = firstId(); id < end; ++id) {
            const Line *l = peekLine(id, &tmp);
            if (!l)
                continue;
            // Text with a code-unit -> column map.
            QString text;
            std::vector<int> colOf;
            for (int c = 0; c < int(l->cells.size()); ++c) {
                const Cell &cell = l->cells[size_t(c)];
                if (cell.ch == kWideTail)
                    continue;
                const QString s = l->cellText(cell);
                for (int k = 0; k < s.size(); ++k)
                    colOf.push_back(c);
                text += s;
            }
            int from = 0;
            while ((from = text.indexOf(needle, from, cs)) >= 0) {
                const int last = from + needle.size() - 1;
                const Cell &lastCell = l->cells[size_t(colOf[size_t(last)])];
                matches.push_back({id, colOf[size_t(from)], colOf[size_t(last)] + (lastCell.width == 2 ? 1 : 0)});
                from += std::max(1, int(needle.size()));
            }
        }
    }

    void scrollLineIntoView(qint64 id)
    {
        const qint64 top = topVisibleId();
        if (id >= top && id < top + rowsN)
            return;
        // Put the line in the middle of the viewport.
        qint64 newTop = id - rowsN / 2;
        newTop = std::max(firstId(), std::min(newTop, pushed));
        scrollOffset = int(pushed - newTop);
        allDirty = true;
    }
};

LibVtermCore::LibVtermCore(int rows, int cols)
    : d(new Impl)
{
    d->q = this;
    d->rowsN = std::max(1, rows);
    d->colsN = std::max(1, cols);
    d->dirty.assign(size_t(d->rowsN), 1);
    d->vt = vterm_new(d->rowsN, d->colsN);
    vterm_set_utf8(d->vt, 1);
    d->state = vterm_obtain_state(d->vt);
    d->screen = vterm_obtain_screen(d->vt);

    static const VTermScreenCallbacks screenCbs = {
        &Impl::onDamage, &Impl::onMoveRect, &Impl::onMoveCursor, &Impl::onTermProp, &Impl::onBell,
        nullptr, // resize
        nullptr, // sb_pushline (replaced by sb_pushline4)
        nullptr, // sb_popline (replaced by sb_popline4)
        &Impl::sbClear,
    };
    vterm_screen_set_callbacks(d->screen, &screenCbs, d.get());
    static const VTermScreenRelayCallbacks relayCbs = {&Impl::pushLine, &Impl::popLine};
    vterm_screen_relay_set_callbacks(d->screen, &relayCbs);
    static const VTermStateFallbacks fallbacks = {nullptr, nullptr, &Impl::onOsc, nullptr, nullptr, nullptr, nullptr};
    vterm_screen_set_unrecognised_fallbacks(d->screen, &fallbacks, d.get());
    static const VTermSelectionCallbacks selCbs = {&Impl::onSelectionSet, &Impl::onSelectionQuery};
    vterm_state_set_selection_callbacks(d->state, &selCbs, d.get(), d->selectionBuf, sizeof d->selectionBuf);
    vterm_output_set_callback(d->vt, &Impl::onOutput, d.get());

    vterm_screen_set_damage_merge(d->screen, VTERM_DAMAGE_SCROLL);
    vterm_screen_enable_altscreen(d->screen, 1);
    vterm_screen_enable_reflow(d->screen, true);
    vterm_state_relay_set_grapheme_clusters(d->state, 1);
    vterm_state_set_bold_highbright(d->state, 1);
    setColors(d->defFg, d->defBg, nullptr);
    vterm_screen_reset(d->screen, 1);
}

LibVtermCore::~LibVtermCore()
{
    vterm_free(d->vt);
}

void LibVtermCore::feed(const char *data, size_t len)
{
    vterm_input_write(d->vt, data, len);
    vterm_screen_flush_damage(d->screen);
    ++d->changeCounter;
}

void LibVtermCore::resize(int rows, int cols, int, int)
{
    rows = std::max(1, rows);
    cols = std::max(2, cols);
    if (rows == d->rowsN && cols == d->colsN)
        return;
    const bool colsChanged = cols != d->colsN;
    if (d->reflow && colsChanged)
        d->rewrap(0, cols);
    d->resizing = true;
    d->minCountDuringResize = d->count;
    const size_t before = d->count;
    vterm_set_size(d->vt, rows, cols);
    d->resizing = false;
    d->rowsN = rows;
    d->colsN = cols;
    if (d->reflow && colsChanged && d->count > std::min(before, d->minCountDuringResize))
        d->rewrap(std::min(before, d->minCountDuringResize), cols);
    vterm_screen_flush_damage(d->screen);
    VTermPos pos;
    vterm_state_get_cursorpos(d->state, &pos);
    d->cursor.row = pos.row;
    d->cursor.col = pos.col;
    d->dirty.assign(size_t(rows), 1);
    d->allDirty = true;
    d->scrollOffset = 0;
    d->selActive = false;
    d->matches.clear();
    d->current = -1;
    ++d->changeCounter;
}

int LibVtermCore::rows() const { return d->rowsN; }
int LibVtermCore::columns() const { return d->colsN; }

void LibVtermCore::setScrollbackLines(int lines)
{
    lines = std::max(0, lines);
    if (lines == d->limit)
        return;
    std::vector<Line> keep;
    const size_t n = std::min(d->count, size_t(lines));
    keep.reserve(n);
    for (size_t i = d->count - n; i < d->count; ++i)
        keep.push_back(std::move(d->ringAt(i)));
    const qint64 first = d->pushed - qint64(n);
    d->ring = std::move(keep);
    d->head = 0;
    d->count = d->ring.size();
    d->limit = lines;
    (void)first;
    d->scrollOffset = std::min<int>(d->scrollOffset, int(d->count));
    d->allDirty = true;
}

bool LibVtermCore::atGround() const
{
    return vterm_relay_parser_at_ground(d->vt);
}

void LibVtermCore::setReflow(bool enabled)
{
    d->reflow = enabled;
    vterm_screen_enable_reflow(d->screen, enabled);
}

void LibVtermCore::setGraphemeClusters(bool enabled)
{
    vterm_state_relay_set_grapheme_clusters(d->state, enabled ? 1 : 0);
}

bool LibVtermCore::updateFrame(ViewportFrame *frame, bool force)
{
    const bool sizeChanged = frame->rows != d->rowsN || frame->columns != d->colsN;
    const bool anyRow = std::find(d->dirty.begin(), d->dirty.end(), 1) != d->dirty.end();
    if (!force && !sizeChanged && !d->allDirty && !d->decorDirty && !anyRow)
        return false;
    const bool full = force || sizeChanged || d->allDirty || d->decorDirty || d->scrollOffset > 0;
    frame->rows = d->rowsN;
    frame->columns = d->colsN;
    frame->lines.resize(size_t(d->rowsN));
    frame->dirty.assign(size_t(d->rowsN), 0);
    frame->full = full;
    const qint64 top = d->topVisibleId();
    for (int row = 0; row < d->rowsN; ++row) {
        if (!full && !d->dirty[size_t(row)])
            continue;
        frame->dirty[size_t(row)] = 1;
        Line &line = frame->lines[size_t(row)];
        const qint64 id = top + row;
        if (!d->lineAt(id, &line))
            line.clear();
        if (int(line.cells.size()) < d->colsN)
            line.cells.resize(size_t(d->colsN));
        d->decorate(id, &line);
    }
    frame->cursor = d->cursor;
    const int cursorRow = d->cursor.row + d->scrollOffset;
    frame->cursorInViewport = cursorRow < d->rowsN;
    frame->cursor.row = cursorRow;
    frame->cursor.visible = d->cursor.visible && frame->cursorInViewport;
    frame->historyRows = int(d->count);
    frame->viewportTop = int(d->count) - d->scrollOffset;
    frame->altScreen = d->alt;
    std::fill(d->dirty.begin(), d->dirty.end(), 0);
    d->allDirty = false;
    d->decorDirty = false;
    return true;
}

QString LibVtermCore::screenText() const
{
    QStringList lines;
    Line l;
    for (int r = 0; r < d->rowsN; ++r) {
        d->readScreenRow(r, &l);
        lines << l.text();
    }
    return lines.join(QLatin1Char('\n'));
}

QStringList LibVtermCore::historyText(int maxLines) const
{
    QStringList out;
    const size_t n = std::min(d->count, size_t(std::max(0, maxLines)));
    for (size_t i = d->count - n; i < d->count; ++i)
        out << d->ringAt(i).text();
    return out;
}

bool LibVtermCore::altScreen() const { return d->alt; }

MouseTracking LibVtermCore::mouseTracking() const
{
    switch (d->mouseMode) {
    case VTERM_PROP_MOUSE_CLICK: return MouseTracking::Click;
    case VTERM_PROP_MOUSE_DRAG: return MouseTracking::Drag;
    case VTERM_PROP_MOUSE_MOVE: return MouseTracking::Move;
    default: return MouseTracking::None;
    }
}

bool LibVtermCore::bracketedPaste() const { return vterm_state_relay_get_bracketpaste(d->state); }
QString LibVtermCore::title() const { return d->title; }
CursorState LibVtermCore::activeCursor() const { return d->cursor; }

int LibVtermCore::historyRows() const { return int(d->count); }
int LibVtermCore::viewportTop() const { return int(d->count) - d->scrollOffset; }
bool LibVtermCore::viewportAtBottom() const { return d->scrollOffset == 0; }

void LibVtermCore::scrollViewport(int deltaRows)
{
    if (d->alt)
        return;
    const int off = std::max(0, std::min(d->scrollOffset - deltaRows, int(d->count)));
    if (off != d->scrollOffset) {
        d->scrollOffset = off;
        d->allDirty = true;
    }
}

void LibVtermCore::scrollViewportToTop() { scrollViewport(-int(d->count)); }
void LibVtermCore::scrollViewportToBottom() { scrollViewport(d->scrollOffset); }
void LibVtermCore::scrollViewportToRow(int row) { scrollViewport(row - viewportTop()); }

bool LibVtermCore::scrollToPrompt(int direction)
{
    Line tmp;
    const qint64 top = d->topVisibleId();
    const qint64 end = d->pushed + d->rowsN;
    for (qint64 id = top + (direction < 0 ? -1 : 1); id >= d->firstId() && id < end; id += direction < 0 ? -1 : 1) {
        const Line *l = d->peekLine(id, &tmp);
        if (l && (l->marks & MarkPromptStart)) {
            d->scrollOffset = int(std::max<qint64>(0, std::min<qint64>(d->pushed - id, qint64(d->count))));
            d->allDirty = true;
            return true;
        }
    }
    return false;
}

QString LibVtermCore::hyperlinkAt(int row, int col) const
{
    Line tmp;
    const Line *l = d->peekLine(d->topVisibleId() + row, &tmp);
    if (!l || col < 0 || col >= int(l->cells.size()))
        return QString();
    const uint32_t id = l->cells[size_t(col)].link;
    return id < d->linkUris.size() ? d->linkUris[id] : QString();
}

void LibVtermCore::selectionBegin(int row, int col, SelectionUnit unit, bool rectangle)
{
    d->selUnit = unit;
    d->selRect = rectangle && unit == SelectionUnit::Cell;
    d->selAnchor = d->selExtent = d->viewportPos(row, col);
    d->selActive = unit != SelectionUnit::Cell;
    d->normalizeSelection();
}

void LibVtermCore::selectionExtend(int row, int col)
{
    d->selExtent = d->viewportPos(row, col);
    d->selActive = true;
    d->normalizeSelection();
}

void LibVtermCore::selectionClear()
{
    if (d->selActive)
        d->decorDirty = true;
    d->selActive = false;
}

bool LibVtermCore::hasSelection() const { return d->selActive; }

QString LibVtermCore::selectedText() const
{
    if (!d->selActive)
        return QString();
    QString out;
    Line tmp;
    for (qint64 id = d->selStart.line; id <= d->selEnd.line; ++id) {
        const Line *l = d->peekLine(id, &tmp);
        if (!l)
            continue;
        int from = id == d->selStart.line ? d->selStart.col : 0;
        int to = id == d->selEnd.line ? d->selEnd.col + 1 : d->colsN;
        if (d->selRect) {
            from = std::min(d->selStart.col, d->selEnd.col);
            to = std::max(d->selStart.col, d->selEnd.col) + 1;
        }
        out += l->text(from, to);
        if (id < d->selEnd.line) {
            Line tmp2;
            const Line *next = d->peekLine(id + 1, &tmp2);
            if (d->selRect || !next || !next->continuation)
                out += QLatin1Char('\n');
        }
    }
    return out;
}

void LibVtermCore::selectAll()
{
    d->selUnit = SelectionUnit::Cell;
    d->selRect = false;
    d->selAnchor = Impl::Pos{d->firstId(), 0};
    d->selExtent = Impl::Pos{d->pushed + d->rowsN - 1, d->colsN - 1};
    d->selActive = true;
    d->normalizeSelection();
}

int LibVtermCore::searchSet(const QString &needle)
{
    d->needle = needle;
    d->computeMatches();
    d->decorDirty = true;
    return int(d->matches.size());
}

int LibVtermCore::searchStep(bool backwards)
{
    if (d->needle.isEmpty())
        return -1;
    if (d->matchesAt != d->changeCounter) {
        const int prev = d->current;
        d->computeMatches();
        d->current = prev >= int(d->matches.size()) ? -1 : prev;
    }
    if (d->matches.empty())
        return -1;
    const int n = int(d->matches.size());
    if (d->current < 0)
        d->current = backwards ? n - 1 : 0;
    else
        d->current = (d->current + (backwards ? n - 1 : 1)) % n;
    d->scrollLineIntoView(d->matches[size_t(d->current)].line);
    d->decorDirty = true;
    return n - 1 - d->current; // newest match is index 0
}

int LibVtermCore::searchMatchCount() const { return int(d->matches.size()); }

void LibVtermCore::sendKey(const KeyInput &key)
{
    if (key.release)
        return;
    const VTermModifier mods = toVtermMods(key.modifiers);
    if (key.key != Key::None) {
        const VTermKey vk = toVtermKey(key.key);
        if (vk != VTERM_KEY_NONE)
            vterm_keyboard_key(d->vt, vk, mods);
        return;
    }
    if (key.modifiers & ModCtrl) {
        char32_t c = key.codepoint;
        if (c >= 'A' && c <= 'Z')
            c = c - 'A' + 'a';
        if (c == 0 && !key.text.isEmpty())
            c = key.text.toUcs4().value(0);
        if (c)
            vterm_keyboard_unichar(d->vt, c, VTermModifier(mods & ~VTERM_MOD_SHIFT));
        return;
    }
    if (key.modifiers & ModAlt) {
        char32_t c = key.text.isEmpty() ? key.codepoint : char32_t(key.text.toUcs4().value(0));
        if (c)
            vterm_keyboard_unichar(d->vt, c, VTERM_MOD_ALT);
        return;
    }
    for (uint c : key.text.toUcs4())
        vterm_keyboard_unichar(d->vt, c, VTERM_MOD_NONE);
}

void LibVtermCore::sendText(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    if (!utf8.isEmpty() && events.reply)
        events.reply(utf8.constData(), size_t(utf8.size()));
}

void LibVtermCore::sendMouse(const MouseInput &m)
{
    const VTermModifier mods = toVtermMods(m.modifiers);
    vterm_mouse_move(d->vt, m.row, m.col, mods);
    if (m.action == MouseInput::Action::Motion)
        return;
    int button = 0;
    switch (m.button) {
    case MouseButton::Left: button = 1; break;
    case MouseButton::Middle: button = 2; break;
    case MouseButton::Right: button = 3; break;
    case MouseButton::WheelUp: button = 4; break;
    case MouseButton::WheelDown: button = 5; break;
    default: return;
    }
    if (button >= 4 && m.action == MouseInput::Action::Release)
        return;
    vterm_mouse_button(d->vt, button, m.action == MouseInput::Action::Press, mods);
}

void LibVtermCore::paste(const QString &text)
{
    QString t = text;
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\r"));
    t.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    // Like libghostty-vt's paste encoder: control characters other than tab and
    // CR become spaces, so a paste can neither end the bracket (ESC[201~) nor
    // smuggle other sequences or signals.
    for (QChar &ch : t) {
        const ushort u = ch.unicode();
        if ((u < 0x20 && u != '\t' && u != '\r') || u == 0x7f || (u >= 0x80 && u < 0xa0))
            ch = QLatin1Char(' ');
    }
    const QByteArray utf8 = t.toUtf8();
    vterm_keyboard_start_paste(d->vt);
    if (events.reply)
        events.reply(utf8.constData(), size_t(utf8.size()));
    vterm_keyboard_end_paste(d->vt);
}

void LibVtermCore::focusChanged(bool focused)
{
    if (focused)
        vterm_state_focus_in(d->state);
    else
        vterm_state_focus_out(d->state);
}

void LibVtermCore::clearScrollback()
{
    d->count = 0;
    d->head = 0;
    d->scrollOffset = 0;
    d->allDirty = true;
}

void LibVtermCore::reset()
{
    vterm_screen_relay_set_hyperlink(d->screen, 0);
    vterm_screen_reset(d->screen, 1);
    d->title.clear();
    if (d->alt) {
        d->alt = false;
        if (events.altScreenChanged)
            events.altScreenChanged(false);
    }
    d->allDirty = true;
}

void LibVtermCore::setColors(uint32_t fg, uint32_t bg, const uint32_t *palette16)
{
    d->defFg = fg;
    d->defBg = bg;
    VTermColor f, b;
    vterm_color_rgb(&f, uint8_t(fg >> 16), uint8_t(fg >> 8), uint8_t(fg));
    vterm_color_rgb(&b, uint8_t(bg >> 16), uint8_t(bg >> 8), uint8_t(bg));
    vterm_screen_set_default_colors(d->screen, &f, &b);
    if (palette16) {
        for (int i = 0; i < 16; ++i) {
            VTermColor c;
            vterm_color_rgb(&c, uint8_t(palette16[i] >> 16), uint8_t(palette16[i] >> 8), uint8_t(palette16[i]));
            vterm_state_set_palette_color(d->state, i, &c);
        }
    }
    d->allDirty = true;
}

void LibVtermCore::setClipboardWriteAllowed(bool allowed) { d->clipboardAllowed = allowed; }

} // namespace relay
