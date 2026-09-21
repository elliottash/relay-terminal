// SPDX-License-Identifier: AGPL-3.0-or-later
#include "LibVtermCore.h"

#include <QUrl>

extern "C" {
#include <vterm.h>
}

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

namespace relay {
namespace {

const uint32_t kVtermTail = 0xFFFFFFFF;

// Keep palette references in history and serialized scrollback: resolving them here
// freezes a row at the theme that was active when it left the screen.
inline uint32_t packedColor(const VTermColor &c)
{
    if (VTERM_COLOR_IS_INDEXED(&c))
        return CellColor::indexed(c.indexed.idx);
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
    // #3H5T: the vertical shift seen since the last frame, so a phone can move its rows instead
    // of being sent all of them — a streamed reply was 166 whole-screen snapshots of 7.7 kB
    // against 86 diffs of 375 B. `scrollBy` is rows moved up inside [scrollTop, scrollBottom);
    // zero means nothing describable moved. `pushedLine` is what used to set `allDirty` from
    // sb_pushline: it still makes the frame full, but it is kept apart so updateFrame() can tell
    // "full because output scrolled" from "full because the whole screen really changed".
    int scrollBy = 0;
    int scrollTop = 0;
    int scrollBottom = 0;
    bool pushedLine = false;
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

    // hyperlinkRuns() answers the scrollback half of its walk from here (#PPR4).
    // A line in the ring never changes once it is pushed, so the rows walked
    // for one prefix only have to be walked again when the ring itself is
    // rewrapped, cleared or reset — `ringEpoch` counts exactly those. Runs are
    // held in **push ids**, which trimming does not renumber, and turned into
    // the absolute rows the interface promises when the answer is built.
    struct CachedRun {
        QString uri;
        uint32_t link = 0; // the OSC 8 id the run was opened with
        qint64 startId = 0;
        int startCol = 0;
        qint64 endId = 0;
        int endCol = 0;
    };
    struct RunCache {
        QString prefix;
        std::vector<CachedRun> runs;
        qint64 walkedTo = 0;  // push id the ring has been walked up to (exclusive)
        uint32_t openId = 0;  // the run still open at walkedTo - 1, if any
        quint64 epoch = 0;
        bool valid = false;
    };
    std::vector<RunCache> runCaches;
    quint64 ringEpoch = 0;

    // What the work-counting tests read (#6W0Z, #PPR4): cells converted into
    // stored scrollback lines, and rows visited by a hyperlink walk. Both are
    // counted per line, never per cell, so nothing is added to the inner loops.
    quint64 storedCells = 0;
    mutable quint64 linkRowsWalked = 0;   // the walks themselves are const

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
            c->fg = packedColor(vc.fg);
        }
        if (!VTERM_COLOR_IS_DEFAULT_BG(&vc.bg)) {
            c->bg = packedColor(vc.bg);
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

    // A blank cell as the stored-line rule sees it, read straight off
    // libvterm's own cell so a line can be measured *before* it is converted:
    // no character, no background of its own, no attribute and no hyperlink.
    // The foreground is deliberately not part of it — a space keeps no ink —
    // which is exactly what `isBlank() && attrs == 0` decided in pushLine's
    // pop_back loop, cell by cell, after paying to convert all of them (#6W0Z).
    static bool blankSourceCell(const VTermScreenCell &vc)
    {
        if (vc.chars[0] != 0 || vc.hyperlink != 0 || !VTERM_COLOR_IS_DEFAULT_BG(&vc.bg))
            return false;
        return !vc.attrs.bold && !vc.attrs.italic && !vc.attrs.blink && !vc.attrs.reverse
            && !vc.attrs.conceal && !vc.attrs.strike && !vc.attrs.underline;
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
            // Where the line really ends, found before anything is converted: a
            // 99-character line in a 280-column grid used to pay the colour
            // conversion and the cluster walk for 181 cells that were then
            // popped off again (#6W0Z). Same cells kept, same cells dropped —
            // blankSourceCell() is the pop_back loop's test, read off the
            // source cell — so a styled blank (a background, reverse video)
            // stays, as it always did.
            int kept = cols;
            while (kept > 0 && blankSourceCell(cells[kept - 1]))
                --kept;
            // The slot comes from the ring and clear() keeps its capacity, so a
            // short line landing where a wide one was would hold the wide one's
            // allocation for as long as it is in the scrollback: 2.8 kB a line
            // at 132 columns against the ~1.7 kB the cells themselves need.
            // Let that block go rather than carry it; a slot that keeps seeing
            // lines of the same length keeps its allocation and allocates
            // nothing.
            if (l->cells.capacity() > size_t(kept) + 16)
                std::vector<Cell>().swap(l->cells);
            l->cells.resize(size_t(kept));
            d->storedCells += quint64(kept);
            for (int i = 0; i < kept; ++i)
                d->convertCell(cells[i], l, &l->cells[size_t(i)]);
            l->continuation = info->continuation;
            l->marks = uint8_t(info->relay_marks);
            l->wrapColumns = uint16_t(cols);
        }
        ++d->pushed;
        if (d->scrollOffset > 0 && !d->resizing)
            d->scrollOffset = std::min<int>(d->scrollOffset + 1, int(d->count));
        // The frame is still full — the viewport moved over the ring and the desktop's own view
        // repaints it whole, as it always has. `pushedLine` rather than `allDirty` only so that
        // updateFrame() can still recognise the frame as a plain scroll (#3H5T).
        d->pushedLine = true;
        return 1;
    }

    // Rows [fromId, toId) of one prefix's hyperlink walk, appended to `out`
    // with the rule the whole-scrollback walk uses: the same link id on the
    // same or the next row continues the run, anything else starts one.
    // `openId` carries that state in and out, so the walk can be split into
    // "the rows pushed since last time" and "the screen" and still produce
    // exactly what one pass over both would have.
    void walkLinkRows(const QString &prefix, qint64 fromId, qint64 toId, std::vector<CachedRun> *out,
                      uint32_t *openId) const
    {
        Line tmp;
        for (qint64 id = fromId; id < toId; ++id) {
            ++linkRowsWalked;
            const Line *l = peekLine(id, &tmp);
            if (!l) {
                *openId = 0;
                continue;
            }
            bool onThisRow = false;
            for (int col = 0; col < int(l->cells.size()); ++col) {
                const uint32_t link = l->cells[size_t(col)].link;
                if (link == 0 || link >= linkUris.size() || !linkUris[link].startsWith(prefix))
                    continue;
                if (*openId == link && !out->empty() && (out->back().endId == id || out->back().endId == id - 1)) {
                    out->back().endId = id;
                    out->back().endCol = col;
                } else {
                    out->push_back(CachedRun{linkUris[link], link, id, col, id, col});
                    *openId = link;
                }
                onThisRow = true;
            }
            if (!onThisRow)
                *openId = 0;
        }
    }

    // The screen's own rows, read for their hyperlink ids alone: no colour
    // conversion, no cluster walk and no Line copy, all of which
    // peekLine()->readScreenRow() would do per row, and which is most of what
    // this walk costs once the ring half of it is cached.
    void walkLinkScreen(const QString &prefix, std::vector<CachedRun> *out, uint32_t *openId) const
    {
        VTermScreenCell vc;
        for (int row = 0; row < rowsN; ++row) {
            ++linkRowsWalked;
            const qint64 id = pushed + row;
            bool onThisRow = false;
            for (int col = 0; col < colsN; ++col) {
                if (!vterm_screen_get_cell(screen, VTermPos{row, col}, &vc))
                    break;
                const uint32_t link = vc.hyperlink;
                if (link == 0 || link >= linkUris.size() || !linkUris[link].startsWith(prefix))
                    continue;
                if (*openId == link && !out->empty() && (out->back().endId == id || out->back().endId == id - 1)) {
                    out->back().endId = id;
                    out->back().endCol = col;
                } else {
                    out->push_back(CachedRun{linkUris[link], link, id, col, id, col});
                    *openId = link;
                }
                onThisRow = true;
            }
            if (!onThisRow)
                *openId = 0;
        }
    }

    // The cache for one prefix, emptied when the ring it describes was rewrapped,
    // trimmed away or cleared.
    RunCache &runCacheFor(const QString &prefix)
    {
        for (RunCache &c : runCaches) {
            if (c.prefix == prefix) {
                if (!c.valid || c.epoch != ringEpoch || c.walkedTo > pushed) {
                    c.runs.clear();
                    c.walkedTo = firstId();
                    c.openId = 0;
                    c.epoch = ringEpoch;
                    c.valid = true;
                }
                return c;
            }
        }
        // Two prefixes are in use (fold anchors and prose blocks, #R2WQ); a
        // third would be a new kind of anchor, not a new caller per frame.
        if (runCaches.size() >= 8)
            runCaches.erase(runCaches.begin());
        runCaches.push_back(RunCache{prefix, {}, firstId(), 0, ringEpoch, true});
        return runCaches.back();
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
        } else if (CellColor::kind(c.fg) == CellColor::Indexed) {
            vterm_color_indexed(&o->fg, uint8_t(CellColor::value(c.fg)));
        }
        if (CellColor::kind(c.bg) == CellColor::Rgb) {
            const uint32_t v = CellColor::value(c.bg);
            vterm_color_rgb(&o->bg, uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v));
        } else if (CellColor::kind(c.bg) == CellColor::Indexed) {
            vterm_color_indexed(&o->bg, uint8_t(CellColor::value(c.bg)));
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
        info->relay_marks = l.marks & 0xFF;
        --d->count;
        --d->pushed;
        ++d->ringEpoch;
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
        ++d->ringEpoch;
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

    // Fold one moverect into this frame's scroll description (#3H5T). True when it was a plain
    // vertical shift of whole rows that fits what is already recorded; false for a partial-width
    // move, a second region in the same frame, or a shift that turns its region over completely —
    // none of which is worth describing, and all of which the caller damages instead.
    bool noteScroll(VTermRect dest, VTermRect src)
    {
        if (dest.start_col != 0 || src.start_col != 0 || dest.end_col != colsN || src.end_col != colsN)
            return false;
        if (dest.end_row - dest.start_row != src.end_row - src.start_row)
            return false;
        const int by = src.start_row - dest.start_row;   // rows the content moved up
        if (by == 0)
            return true;                                 // nothing moved; nothing to damage either
        const int top = std::min(dest.start_row, src.start_row);
        const int bottom = std::max(dest.end_row, src.end_row);
        if (top < 0 || bottom > rowsN || bottom - top <= 0)
            return false;
        if (scrollBy != 0 && (top != scrollTop || bottom != scrollBottom))
            return false;
        if (std::abs(scrollBy + by) >= bottom - top)
            return false;
        scrollTop = top;
        scrollBottom = bottom;
        scrollBy += by;
        shiftDirty(top, bottom, by);
        return true;
    }

    // `dirty` is in viewport rows, so a shift has to move it with the content: a row damaged
    // before the scroll is still damaged where it lands, and the rows the shift vacated are new.
    void shiftDirty(int top, int bottom, int by)
    {
        top = std::max(0, top);
        bottom = std::min(bottom, int(dirty.size()));
        if (bottom <= top)
            return;
        const int n = std::abs(by);
        if (n >= bottom - top) {
            std::fill(dirty.begin() + top, dirty.begin() + bottom, uint8_t(1));
            return;
        }
        if (by > 0) {
            std::move(dirty.begin() + top + n, dirty.begin() + bottom, dirty.begin() + top);
            std::fill(dirty.begin() + bottom - n, dirty.begin() + bottom, uint8_t(1));
        } else {
            std::move_backward(dirty.begin() + top, dirty.begin() + bottom - n, dirty.begin() + bottom);
            std::fill(dirty.begin() + top, dirty.begin() + top + n, uint8_t(1));
        }
    }

    // ---------------------------------------------------------------- libvterm callbacks
    static int onDamage(VTermRect r, void *user)
    {
        auto *d = static_cast<Impl *>(user);
        for (int row = std::max(0, r.start_row); row < r.end_row && row < int(d->dirty.size()); ++row)
            d->dirty[size_t(row)] = 1;
        return 1;
    }
    static int onMoveRect(VTermRect dest, VTermRect src, void *user)
    {
        auto *d = static_cast<Impl *>(user);
        // #3H5T: libvterm reports a scroll as one moverect (VTERM_DAMAGE_SCROLL merges the cell
        // damage around it), so the rows that moved can be described to a remote client rather
        // than resent. Anything that does not fit that description is damaged the way it always
        // was; if a shift had already been folded into `dirty` this frame, the bookkeeping can no
        // longer say which rows are current, so the whole screen goes.
        if (d->noteScroll(dest, src))
            return 1;
        if (d->scrollBy != 0) {
            d->scrollBy = 0;
            d->allDirty = true;
        }
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
        case 7772: {
            // Relay's row role (CellTypes.h, MarkUserShell / MarkUserAgent): the cursor's line is
            // one the user typed. Anything else in the body is ignored.
            const PromptMark role = body == "shell" ? MarkUserShell : body == "agent" ? MarkUserAgent : PromptMark(0);
            if (role)
                vterm_state_relay_mark_cursor_line(state, role);
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
        case 7772:   // Relay's row role
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
    // Both halves of a resize rewrite the ring (rewrap, and libvterm popping
    // lines back onto the screen), so every cached hyperlink walk goes.
    ++d->ringEpoch;
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
    ++d->ringEpoch;
    (void)first;
    d->scrollOffset = std::min<int>(d->scrollOffset, int(d->count));
    d->allDirty = true;
}

bool LibVtermCore::atGround() const
{
    return vterm_relay_parser_at_ground(d->vt);
}

quint64 LibVtermCore::storedCells() const { return d->storedCells; }
quint64 LibVtermCore::linkRowsWalked() const { return d->linkRowsWalked; }

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
    if (!force && !sizeChanged && !d->allDirty && !d->decorDirty && !d->pushedLine && !anyRow)
        return false;
    const bool full = force || sizeChanged || d->allDirty || d->decorDirty || d->pushedLine
        || d->scrollOffset > 0;
    // #3H5T: the frame is a plain scroll when the only thing that moved is one shift — output
    // pushing lines off the top, or an application scrolling a margin. Then `dirty` names exactly
    // the rows the shift could not carry over, and a remote client can move the rest itself. A
    // repaint the viewport did not ask for (force, a resize, a colour or decoration change, or a
    // viewport sitting back in the scrollback, where a push moves nothing) is not that.
    const bool scrolled = d->scrollBy != 0 && !force && !sizeChanged && !d->allDirty
        && !d->decorDirty && d->scrollOffset == 0;
    frame->rows = d->rowsN;
    frame->columns = d->colsN;
    frame->lines.resize(size_t(d->rowsN));
    frame->dirty.assign(size_t(d->rowsN), 0);
    frame->full = full;
    frame->scrolledBy = scrolled ? d->scrollBy : 0;
    frame->scrollTop = scrolled ? d->scrollTop : 0;
    frame->scrollBottom = scrolled ? d->scrollBottom : 0;
    const qint64 top = d->topVisibleId();
    for (int row = 0; row < d->rowsN; ++row) {
        if (!full && !d->dirty[size_t(row)])
            continue;
        // Every row is still filled when the frame is full, so nothing downstream sees less than
        // it did; `dirty` keeps telling the truth about which of them actually changed, which is
        // what makes the scroll description usable.
        frame->dirty[size_t(row)] = uint8_t(scrolled ? d->dirty[size_t(row)] : 1);
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
    d->pushedLine = false;
    d->scrollBy = 0;
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

// Styled scrollback. The ring already holds converted Lines — the same type and
// the same cells updateFrame() hands the view, colours resolved to RGB and
// clusters interned — so this is a clamp and a copy, with nothing read from
// libvterm and nothing written back: the viewport, the dirty state, the
// selection and the search are all untouched.
int LibVtermCore::historyLines(int fromRow, int count, std::vector<Line> *out) const
{
    if (out)
        out->clear();
    const int total = int(d->count);
    const int from = std::max(0, std::min(fromRow, total));
    const int want = std::max(0, std::min(count, total - from));
    if (!out)
        return from;
    out->reserve(size_t(want));
    for (int i = 0; i < want; ++i) {
        // The ring never carries viewport decorations (decorate() only ever
        // writes them onto a frame's copy), but say so rather than assume it.
        Line line = d->ringAt(size_t(from + i));
        line.selectionStart = line.selectionEnd = -1;
        line.highlights.clear();
        out->push_back(std::move(line));
    }
    return from;
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

// The id is the one the caller already read off the cell, so this is a table
// lookup — no row to convert, no libvterm call (#6W0Z).
QString LibVtermCore::hyperlinkUri(uint32_t id, int, int) const
{
    return id && id < d->linkUris.size() ? d->linkUris[id] : QString();
}

// Every run of cells whose OSC 8 URI starts with `prefix`, in absolute
// scrollback rows. The ring holds the link id per cell, so this is a walk over
// the scrollback and the screen with no libvterm call per cell — about 20 ms
// for a 100 000-line history, which is why the interface tells callers to run
// it on resize, trimming and clearing rather than per frame.
//
// Relay's fold layer asks for it far more often than that (once per agent
// block, twice — one prefix for tool anchors, one for prose), and the cost grew
// with the conversation: 65 ms of GUI CPU a turn at turn 25 against 86 ms at
// turn 225 (#PPR4). So the scrollback half of the walk is cached: a pushed line
// never changes, so only the rows pushed since the last call are walked, and
// only the screen — which does change — is walked every time. The answer is
// identical to the full walk's, the cache is dropped whenever the ring is
// rewrapped, cleared or reset (Impl::ringEpoch), and trimming is handled by
// holding the runs in push ids rather than rows.
std::vector<VtCore::HyperlinkRun> LibVtermCore::hyperlinkRuns(const QString &prefix) const
{
    std::vector<HyperlinkRun> out;
    if (prefix.isEmpty())
        return out;
    Impl::RunCache &cache = d->runCacheFor(prefix);
    const qint64 first = d->firstId();
    // Rows pushed *and* trimmed between two calls were never walked and no
    // longer exist; every row still in the ring has been walked or is about to
    // be.
    if (cache.walkedTo < first) {
        cache.walkedTo = first;
        cache.openId = 0;
    }
    d->walkLinkRows(prefix, cache.walkedTo, d->pushed, &cache.runs, &cache.openId);
    cache.walkedTo = d->pushed;

    // What the trim took: a run entirely gone, and — where it cut into one —
    // the first surviving row and column, which is where a full walk starting
    // at the oldest line would have opened it.
    size_t gone = 0;
    while (gone < cache.runs.size() && cache.runs[gone].endId < first)
        ++gone;
    if (gone > 0)
        cache.runs.erase(cache.runs.begin(), cache.runs.begin() + long(gone));
    for (Impl::CachedRun &r : cache.runs) {
        if (r.startId >= first)
            break;
        r.startId = first;
        r.startCol = 0;
        Line tmp;
        if (const Line *l = d->peekLine(first, &tmp)) {
            for (int col = 0; col < int(l->cells.size()); ++col) {
                if (l->cells[size_t(col)].link == r.link) {
                    r.startCol = col;
                    break;
                }
            }
        }
    }

    // The screen's rows are rewritten under us, so they are walked every time,
    // continuing whatever run the last ring row left open.
    std::vector<Impl::CachedRun> all = cache.runs;
    uint32_t openId = cache.openId;
    d->walkLinkScreen(prefix, &all, &openId);

    out.reserve(all.size());
    for (const Impl::CachedRun &r : all) {
        HyperlinkRun h;
        h.uri = r.uri;
        h.startRow = int(r.startId - first);
        h.startCol = r.startCol;
        h.endRow = int(r.endId - first);
        h.endCol = r.endCol;
        out.push_back(h);
    }
    return out;
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
        // #8SBD: a row the next row soft-wrapped out of is joined with nothing, so the copied
        // text keeps exactly what was printed at the wrap — the space prose wrapped at, and
        // nothing at all when the wrap fell mid-token, which is what keeps a wrapped URL or path
        // whole. That means reading such a row *without* text()'s trailing-space trim; the last
        // row of the selection still gets it, so a selection never ends in padding.
        bool wrapped = false;
        if (id < d->selEnd.line && !d->selRect) {
            Line tmp2;
            const Line *next = d->peekLine(id + 1, &tmp2);
            wrapped = next && next->continuation;
        }
        out += wrapped ? l->untrimmedText(from, to) : l->text(from, to);
        if (id < d->selEnd.line && !wrapped)
            out += QLatin1Char('\n');
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

int LibVtermCore::searchCurrentRow() const
{
    if (d->current < 0 || d->current >= int(d->matches.size()))
        return -1;
    return int(d->matches[size_t(d->current)].line - d->firstId());
}

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
    ++d->ringEpoch;
}

void LibVtermCore::reset()
{
    vterm_screen_relay_set_hyperlink(d->screen, 0);
    vterm_screen_reset(d->screen, 1);
    ++d->ringEpoch;
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
