// SPDX-License-Identifier: AGPL-3.0-or-later
// VtCore over libghostty-vt (MIT, https://github.com/ghostty-org/ghostty).
//
// Every libghostty-vt call in Relay lives in this file. The C API is marked
// unstable upstream; the build pins a commit (engine/scripts/build-libghostty-vt.sh)
// and this adapter is the only code that has to follow API changes.
#include "GhosttyCore.h"

#include "SequenceScanner.h"

#include <QUrl>

// Qt defines `emit` as a macro; libghostty-vt uses it as a struct member name.
#ifdef emit
#undef emit
#endif
#include <ghostty/vt.h>

#include <algorithm>
#include <map>
#include <unordered_map>

namespace relay {
namespace {

// ghostty_terminal_set() takes callbacks as `const void *`.
template<typename F>
inline const void *fn(F f)
{
    return reinterpret_cast<const void *>(f);
}

inline uint32_t packRgb(GhosttyColorRgb c)
{
    return CellColor::rgb(c.r, c.g, c.b);
}

inline GhosttyColorRgb toRgb(uint32_t rgb)
{
    return GhosttyColorRgb{uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb)};
}

inline GhosttyMods toMods(uint8_t m)
{
    GhosttyMods out = 0;
    if (m & ModShift)
        out |= GHOSTTY_MODS_SHIFT;
    if (m & ModCtrl)
        out |= GHOSTTY_MODS_CTRL;
    if (m & ModAlt)
        out |= GHOSTTY_MODS_ALT;
    if (m & ModSuper)
        out |= GHOSTTY_MODS_SUPER;
    return out;
}

GhosttyKey specialKey(Key k)
{
    switch (k) {
    case Key::Enter: return GHOSTTY_KEY_ENTER;
    case Key::Tab: return GHOSTTY_KEY_TAB;
    case Key::Backspace: return GHOSTTY_KEY_BACKSPACE;
    case Key::Escape: return GHOSTTY_KEY_ESCAPE;
    case Key::Up: return GHOSTTY_KEY_ARROW_UP;
    case Key::Down: return GHOSTTY_KEY_ARROW_DOWN;
    case Key::Left: return GHOSTTY_KEY_ARROW_LEFT;
    case Key::Right: return GHOSTTY_KEY_ARROW_RIGHT;
    case Key::Insert: return GHOSTTY_KEY_INSERT;
    case Key::Delete: return GHOSTTY_KEY_DELETE;
    case Key::Home: return GHOSTTY_KEY_HOME;
    case Key::End: return GHOSTTY_KEY_END;
    case Key::PageUp: return GHOSTTY_KEY_PAGE_UP;
    case Key::PageDown: return GHOSTTY_KEY_PAGE_DOWN;
    case Key::Kp0: return GHOSTTY_KEY_NUMPAD_0;
    case Key::Kp1: return GHOSTTY_KEY_NUMPAD_1;
    case Key::Kp2: return GHOSTTY_KEY_NUMPAD_2;
    case Key::Kp3: return GHOSTTY_KEY_NUMPAD_3;
    case Key::Kp4: return GHOSTTY_KEY_NUMPAD_4;
    case Key::Kp5: return GHOSTTY_KEY_NUMPAD_5;
    case Key::Kp6: return GHOSTTY_KEY_NUMPAD_6;
    case Key::Kp7: return GHOSTTY_KEY_NUMPAD_7;
    case Key::Kp8: return GHOSTTY_KEY_NUMPAD_8;
    case Key::Kp9: return GHOSTTY_KEY_NUMPAD_9;
    case Key::KpMultiply: return GHOSTTY_KEY_NUMPAD_MULTIPLY;
    case Key::KpPlus: return GHOSTTY_KEY_NUMPAD_ADD;
    case Key::KpComma: return GHOSTTY_KEY_NUMPAD_COMMA;
    case Key::KpMinus: return GHOSTTY_KEY_NUMPAD_SUBTRACT;
    case Key::KpPeriod: return GHOSTTY_KEY_NUMPAD_DECIMAL;
    case Key::KpDivide: return GHOSTTY_KEY_NUMPAD_DIVIDE;
    case Key::KpEnter: return GHOSTTY_KEY_NUMPAD_ENTER;
    case Key::KpEqual: return GHOSTTY_KEY_NUMPAD_EQUAL;
    default: break;
    }
    if (k >= Key::F1 && k <= Key::F24)
        return GhosttyKey(GHOSTTY_KEY_F1 + (int(k) - int(Key::F1)));
    return GHOSTTY_KEY_UNIDENTIFIED;
}

GhosttyKey textKey(char32_t cp)
{
    if (cp >= 'A' && cp <= 'Z')
        cp = cp - 'A' + 'a';
    if (cp >= 'a' && cp <= 'z')
        return GhosttyKey(GHOSTTY_KEY_A + int(cp - 'a'));
    if (cp >= '0' && cp <= '9')
        return GhosttyKey(GHOSTTY_KEY_DIGIT_0 + int(cp - '0'));
    switch (cp) {
    case ' ': return GHOSTTY_KEY_SPACE;
    case '`': return GHOSTTY_KEY_BACKQUOTE;
    case '\\': return GHOSTTY_KEY_BACKSLASH;
    case '[': return GHOSTTY_KEY_BRACKET_LEFT;
    case ']': return GHOSTTY_KEY_BRACKET_RIGHT;
    case ',': return GHOSTTY_KEY_COMMA;
    case '=': return GHOSTTY_KEY_EQUAL;
    case '-': return GHOSTTY_KEY_MINUS;
    case '.': return GHOSTTY_KEY_PERIOD;
    case '\'': return GHOSTTY_KEY_QUOTE;
    case ';': return GHOSTTY_KEY_SEMICOLON;
    case '/': return GHOSTTY_KEY_SLASH;
    default: return GHOSTTY_KEY_UNIDENTIFIED;
    }
}

bool modeValue(GhosttyTerminal t, GhosttyMode mode)
{
    GhosttyTerminalModeConfig cfg{mode, false};
    if (ghostty_terminal_get(t, GHOSTTY_TERMINAL_DATA_MODE, &cfg) != GHOSTTY_SUCCESS)
        return false;
    return cfg.value;
}

QString fromGhosttyString(const GhosttyString &s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.ptr), int(s.len));
}

// Standard xterm 256-colour palette entries 16..255.
GhosttyColorRgb xtermColor(int i)
{
    if (i >= 16 && i < 232) {
        static const uint8_t steps[6] = {0, 95, 135, 175, 215, 255};
        const int n = i - 16;
        return GhosttyColorRgb{steps[(n / 36) % 6], steps[(n / 6) % 6], steps[n % 6]};
    }
    const uint8_t v = uint8_t(8 + 10 * (i - 232));
    return GhosttyColorRgb{v, v, v};
}

} // namespace

struct GhosttyCore::Impl {
    GhosttyCore *q = nullptr;
    GhosttyTerminal t = nullptr;
    GhosttyRenderState rs = nullptr;
    GhosttyRenderStateRowIterator rows = nullptr;
    GhosttyRenderStateRowCells cells = nullptr;
    GhosttyKeyEncoder keyEncoder = nullptr;
    GhosttyKeyEvent keyEvent = nullptr;
    GhosttyMouseEncoder mouseEncoder = nullptr;
    GhosttyMouseEvent mouseEvent = nullptr;
    GhosttySearch search = nullptr;
    GhosttyTrackedGridRef selAnchor = nullptr;

    int rowsN = 24;
    int colsN = 80;
    int cellW = 8;
    int cellH = 16;
    int scrollbackLines = 10000;
    bool alt = false;
    bool clipboardAllowed = false;
    bool mouseButtonDown = false;
    int searchTotal = 0;
    CursorShape lastCursorShape = CursorShape::Block;
    SelectionUnit selUnit = SelectionUnit::Cell;
    bool selRect = false;
    bool selAnchorOnAlt = false; // grid refs are only valid on the screen they came from
    QString title;
    SequenceScanner scanner;

    // Row roles (OSC 7772;shell/agent, CellTypes.h): libghostty-vt has no
    // storage for them, so every marked row is held as a tracked grid ref —
    // the same mechanism the selection anchor uses — with its bits. The ref
    // follows the row through scrolling, reflow and scrollback trimming, which
    // is what the libvterm fork's relay_marks get from living in the line.
    struct RoleMark {
        GhosttyTrackedGridRef ref = nullptr;
        uint8_t bits = 0;
        bool onAlt = false;
    };
    // Mutable only so the const historyLines() can prune dead refs; a prune
    // touches no state the terminal observes.
    mutable std::vector<RoleMark> roleMarks;

    std::unordered_map<std::string, uint32_t> linkIds;
    std::vector<QString> linkUris{QString()};

    // ---- callbacks
    static void onWritePty(GhosttyTerminal, void *ud, const uint8_t *data, size_t len)
    {
        auto *d = static_cast<Impl *>(ud);
        if (d->q->events.reply)
            d->q->events.reply(reinterpret_cast<const char *>(data), len);
    }
    static void onBell(GhosttyTerminal, void *ud)
    {
        auto *d = static_cast<Impl *>(ud);
        if (d->q->events.bell)
            d->q->events.bell();
    }
    static void onTitle(GhosttyTerminal t, void *ud)
    {
        auto *d = static_cast<Impl *>(ud);
        GhosttyString s{nullptr, 0};
        if (ghostty_terminal_get(t, GHOSTTY_TERMINAL_DATA_TITLE, &s) == GHOSTTY_SUCCESS)
            d->title = fromGhosttyString(s);
        if (d->q->events.titleChanged)
            d->q->events.titleChanged(d->title);
    }
    static void onPwd(GhosttyTerminal t, void *ud)
    {
        auto *d = static_cast<Impl *>(ud);
        GhosttyString s{nullptr, 0};
        if (ghostty_terminal_get(t, GHOSTTY_TERMINAL_DATA_PWD, &s) != GHOSTTY_SUCCESS)
            return;
        const QString raw = fromGhosttyString(s);
        QString path = raw, host;
        if (raw.startsWith(QLatin1String("file:")) || raw.startsWith(QLatin1String("kitty-shell-cwd:"))) {
            const QUrl url(raw);
            path = url.path();
            host = url.host();
        }
        if (d->q->events.cwdChanged)
            d->q->events.cwdChanged(path, host);
    }
    static void onClipboardWrite(GhosttyTerminal, void *ud, const GhosttyClipboardWrite *w)
    {
        auto *d = static_cast<Impl *>(ud);
        GhosttyClipboardWriteReply reply = GHOSTTY_INIT_SIZED(GhosttyClipboardWriteReply);
        reply.result = GHOSTTY_CLIPBOARD_WRITE_RESULT_DENIED;
        if (d->clipboardAllowed && d->q->events.clipboardWrite) {
            QByteArray data;
            for (size_t i = 0; i < w->contents_len; ++i) {
                const QByteArray mime(reinterpret_cast<const char *>(w->contents[i].mime.ptr), int(w->contents[i].mime.len));
                if (mime.startsWith("text/")) {
                    data = QByteArray(reinterpret_cast<const char *>(w->contents[i].data.ptr), int(w->contents[i].data.len));
                    break;
                }
            }
            const QString target = w->location == GHOSTTY_CLIPBOARD_LOCATION_STANDARD ? QStringLiteral("clipboard")
                                                                                       : QStringLiteral("primary");
            d->q->events.clipboardWrite(target, data);
            reply.result = GHOSTTY_CLIPBOARD_WRITE_RESULT_SUCCESS;
        }
        w->reply(w, &reply);
    }
    static void onNotification(GhosttyTerminal, void *ud, const GhosttyTerminalDesktopNotification *n)
    {
        auto *d = static_cast<Impl *>(ud);
        if (d->q->events.notification)
            d->q->events.notification(fromGhosttyString(n->title), fromGhosttyString(n->body));
    }
    static bool onDeviceAttributes(GhosttyTerminal, void *, GhosttyDeviceAttributes *out)
    {
        // Same answer as xterm's VT220 emulation with ANSI colour (what most TUIs expect).
        out->primary.conformance_level = 62;
        out->primary.features[0] = 1;  // 132 columns
        out->primary.features[1] = 22; // ANSI colour
        out->primary.num_features = 2;
        out->secondary.device_type = 1;
        out->secondary.firmware_version = 10;
        out->secondary.rom_cartridge = 0;
        out->tertiary.unit_id = 0;
        return true;
    }
    static bool onSize(GhosttyTerminal, void *ud, GhosttySizeReportSize *out)
    {
        auto *d = static_cast<Impl *>(ud);
        out->rows = uint16_t(d->rowsN);
        out->columns = uint16_t(d->colsN);
        out->cell_width = uint32_t(d->cellW);
        out->cell_height = uint32_t(d->cellH);
        return true;
    }
    static GhosttyString onXtversion(GhosttyTerminal, void *)
    {
        static const char v[] = "Relay(libghostty-vt)";
        return GhosttyString{reinterpret_cast<const uint8_t *>(v), sizeof v - 1};
    }
    static bool onColorScheme(GhosttyTerminal t, void *, GhosttyColorScheme *out)
    {
        GhosttyColorRgb bg{0, 0, 0};
        ghostty_terminal_get(t, GHOSTTY_TERMINAL_DATA_COLOR_BACKGROUND, &bg);
        *out = ghostty_color_perceived_luminance(&bg) > 0.5 ? GHOSTTY_COLOR_SCHEME_LIGHT : GHOSTTY_COLOR_SCHEME_DARK;
        return true;
    }

    void applyScrollbackLimit()
    {
        // Line limits prune slowly in libghostty-vt (measured 83 MB/s vs 700 MB/s
        // with a byte limit), so express the line count as a byte budget.
        // ~10 bytes per cell was measured for plain text (8 MB -> 8 691 rows x 100).
        const size_t bytes = size_t(std::max(0, scrollbackLines)) * size_t(std::max(colsN, 1)) * 10u + 256u * 1024u;
        ghostty_terminal_set(t, GHOSTTY_TERMINAL_OPT_SCROLLBACK_MAX_BYTES, &bytes);
        ghostty_terminal_set(t, GHOSTTY_TERMINAL_OPT_SCROLLBACK_MAX_LINES, nullptr);
    }

    uint32_t internLink(const std::string &uri)
    {
        auto it = linkIds.find(uri);
        if (it != linkIds.end())
            return it->second;
        if (linkUris.size() > 65536) {
            linkIds.clear();
            linkUris.assign(1, QString());
        }
        const uint32_t id = uint32_t(linkUris.size());
        linkUris.push_back(QString::fromStdString(uri));
        linkIds.emplace(uri, id);
        return id;
    }

    bool gridRef(GhosttyPointTag tag, int x, int y, GhosttyGridRef *ref) const
    {
        GhosttyPoint p;
        p.tag = tag;
        p.value.coordinate.x = uint16_t(std::max(0, x));
        p.value.coordinate.y = uint32_t(std::max(0, y));
        *ref = GHOSTTY_INIT_SIZED(GhosttyGridRef);
        return ghostty_terminal_grid_ref(t, p, ref) == GHOSTTY_SUCCESS;
    }

    // The cursor's row, tagged with a role the moment OSC 7772 goes through:
    // libghostty-vt ignores the sequence, but the scanner split feed() at its
    // end, so the cursor still sits on the row the role belongs to. SCREEN y
    // counts the scrollback, the cursor's Y does not; never on the alternate
    // screen, which a full-screen program owns (as with the selection anchor).
    void trackRowRole(PromptMark role)
    {
        if (!role || alt)
            return;
        uint16_t x = 0, y = 0;
        ghostty_terminal_get(t, GHOSTTY_TERMINAL_DATA_CURSOR_X, &x);
        ghostty_terminal_get(t, GHOSTTY_TERMINAL_DATA_CURSOR_Y, &y);
        GhosttyPoint p;
        p.tag = GHOSTTY_POINT_TAG_SCREEN;
        p.value.coordinate.x = x;
        p.value.coordinate.y = uint32_t(q->historyRows() + y);
        GhosttyTrackedGridRef ref = nullptr;
        ghostty_terminal_grid_ref_track(t, p, &ref);
        if (ref)
            roleMarks.push_back({ref, uint8_t(role), false});
    }

    void freeRoleMarks()
    {
        for (RoleMark &m : roleMarks)
            ghostty_tracked_grid_ref_free(m.ref);
        roleMarks.clear();
    }

    // A row erased end to end keeps nothing the host marked it for: the role
    // describes the text, and once that is gone the band would be worn by
    // whatever prints on the row next — `/new` clears the screen, so the new
    // conversation opened in the colour of the command that used to be there.
    // The rows are SCREEN coordinates, inclusive; the libvterm fork does the
    // same to relay_marks in its erase() (third_party/libvterm/src/state.c).
    void dropRoleRows(int firstRow, int lastRow)
    {
        if (lastRow < firstRow)
            return;
        for (size_t i = 0; i < roleMarks.size();) {
            RoleMark &m = roleMarks[i];
            GhosttyPointCoordinate at{0, 0};
            if (m.onAlt == alt && ghostty_tracked_grid_ref_has_value(m.ref)
                && ghostty_tracked_grid_ref_point(m.ref, GHOSTTY_POINT_TAG_SCREEN, &at) == GHOSTTY_SUCCESS
                && int(at.y) >= firstRow && int(at.y) <= lastRow) {
                ghostty_tracked_grid_ref_free(m.ref);
                roleMarks.erase(roleMarks.begin() + long(i));
                continue;
            }
            ++i;
        }
    }

    // CSI J (display) and CSI K (line), once libghostty-vt has applied them —
    // neither moves the cursor, so it still names the row the erase was
    // measured from. Only the rows wiped from end to end lose their role: an
    // erase that stops at the cursor leaves the text before it standing, and
    // that text is what the role was about. `CSI 3 J` drops scrollback, whose
    // refs then report no value and are pruned by roleMarkMap().
    void eraseRoleRows(char selector, int param)
    {
        if (alt || roleMarks.empty())
            return;
        uint16_t y = 0;
        ghostty_terminal_get(t, GHOSTTY_TERMINAL_DATA_CURSOR_Y, &y);
        const int top = q->historyRows();       // SCREEN row of the viewport's first line
        const int cursor = top + int(y);
        const int bottom = top + rowsN - 1;
        if (selector == 'K') {
            if (param == 2)
                dropRoleRows(cursor, cursor);
            return;
        }
        if (param == 0)
            dropRoleRows(cursor + 1, bottom);
        else if (param == 1)
            dropRoleRows(top, cursor - 1);
        else if (param == 2)
            dropRoleRows(top, bottom);
    }

    // Role bits by absolute row (SCREEN coordinates: 0 = oldest scrollback
    // line, the space historyRows(), historyLines() and viewportTop() use).
    // Refs whose row left the retained scrollback report no value and are
    // dropped, so the vector only ever holds live marks; a ref from the other
    // screen is skipped, not dropped — it becomes addressable again when that
    // screen is back.
    std::map<int, uint8_t> roleMarkMap() const
    {
        std::map<int, uint8_t> out;
        for (size_t i = 0; i < roleMarks.size();) {
            const RoleMark &m = roleMarks[i];
            if (m.onAlt != alt) {
                ++i;
                continue;
            }
            GhosttyPointCoordinate at{0, 0};
            if (ghostty_tracked_grid_ref_has_value(m.ref)
                && ghostty_tracked_grid_ref_point(m.ref, GHOSTTY_POINT_TAG_SCREEN, &at) == GHOSTTY_SUCCESS) {
                out[int(at.y)] |= m.bits;
                ++i;
                continue;
            }
            if (ghostty_tracked_grid_ref_has_value(m.ref)) {
                ++i; // addressable, just not in SCREEN coordinates right now
                continue;
            }
            ghostty_tracked_grid_ref_free(m.ref);
            roleMarks.erase(roleMarks.begin() + long(i));
        }
        return out;
    }

    QString formatSelection(const GhosttySelection *sel, bool unwrap) const
    {
        GhosttyTerminalSelectionFormatOptions opts = GHOSTTY_INIT_SIZED(GhosttyTerminalSelectionFormatOptions);
        opts.emit = GHOSTTY_FORMATTER_FORMAT_PLAIN;
        opts.unwrap = unwrap;
        opts.trim = true;
        opts.selection = sel;
        uint8_t *ptr = nullptr;
        size_t len = 0;
        if (ghostty_terminal_selection_format_alloc(t, nullptr, opts, &ptr, &len) != GHOSTTY_SUCCESS || !ptr)
            return QString();
        const QString s = QString::fromUtf8(reinterpret_cast<const char *>(ptr), int(len));
        ghostty_free(nullptr, ptr, len);
        return s;
    }

    QStringList rangeText(GhosttyPointTag tag, int y0, int y1) const
    {
        // Lines [y0, y1] of the given coordinate space, one entry per row.
        QStringList out;
        if (y1 < y0)
            return out;
        GhosttySelection sel = GHOSTTY_INIT_SIZED(GhosttySelection);
        if (!gridRef(tag, 0, y0, &sel.start) || !gridRef(tag, colsN - 1, y1, &sel.end))
            return out;
        out = formatSelection(&sel, false).split(QLatin1Char('\n'));
        while (out.size() < y1 - y0 + 1)
            out << QString();
        while (out.size() > y1 - y0 + 1)
            out.removeLast();
        for (QString &l : out) {
            while (l.endsWith(QLatin1Char(' ')))
                l.chop(1);
        }
        return out;
    }

    void checkAltScreen()
    {
        GhosttyTerminalScreen screen = GHOSTTY_TERMINAL_SCREEN_PRIMARY;
        ghostty_terminal_get(t, GHOSTTY_TERMINAL_DATA_ACTIVE_SCREEN, &screen);
        const bool nowAlt = screen == GHOSTTY_TERMINAL_SCREEN_ALTERNATE;
        if (nowAlt != alt) {
            alt = nowAlt;
            // A selection anchor from the other screen must not reach the
            // selection API (refs must belong to the active screen; unchecked).
            if (selAnchor && selAnchorOnAlt != alt) {
                ghostty_tracked_grid_ref_free(selAnchor);
                selAnchor = nullptr;
                setSelection(nullptr);
            }
            if (q->events.altScreenChanged)
                q->events.altScreenChanged(alt);
        }
    }

    // Hyperlink URI of a grid ref, growing the buffer when needed.
    static bool hyperlinkUri(const GhosttyGridRef &ref, std::string *out)
    {
        char small[1024];
        size_t len = 0;
        GhosttyResult r = ghostty_grid_ref_hyperlink_uri(&ref, reinterpret_cast<uint8_t *>(small), sizeof small, &len);
        if (r == GHOSTTY_SUCCESS) {
            out->assign(small, len);
            return true;
        }
        if (r != GHOSTTY_OUT_OF_SPACE || len == 0 || len > (1u << 20))
            return false;
        out->resize(len);
        if (ghostty_grid_ref_hyperlink_uri(&ref, reinterpret_cast<uint8_t *>(&(*out)[0]), len, &len) != GHOSTTY_SUCCESS)
            return false;
        out->resize(len);
        return true;
    }

    GhosttyTerminalScrollbar scrollbar() const
    {
        GhosttyTerminalScrollbar sb{0, 0, 0};
        ghostty_terminal_get(t, GHOSTTY_TERMINAL_DATA_SCROLLBAR, &sb);
        return sb;
    }

    void setSelection(const GhosttySelection *sel)
    {
        ghostty_terminal_set(t, GHOSTTY_TERMINAL_OPT_SELECTION, sel);
    }
};

GhosttyCore::GhosttyCore(int rows, int cols)
    : d(new Impl)
{
    d->q = this;
    d->rowsN = std::max(1, rows);
    d->colsN = std::max(1, cols);
    ghostty_terminal_new(nullptr, &d->t, uint16_t(d->colsN), uint16_t(d->rowsN));
    ghostty_render_state_new(nullptr, &d->rs);
    ghostty_render_state_row_iterator_new(nullptr, &d->rows);
    ghostty_render_state_row_cells_new(nullptr, &d->cells);
    ghostty_key_encoder_new(nullptr, &d->keyEncoder);
    ghostty_key_event_new(nullptr, &d->keyEvent);
    ghostty_mouse_encoder_new(nullptr, &d->mouseEncoder);
    ghostty_mouse_event_new(nullptr, &d->mouseEvent);

    GhosttyTerminal t = d->t;
    ghostty_terminal_set(t, GHOSTTY_TERMINAL_OPT_USERDATA, d.get());
    ghostty_terminal_set(t, GHOSTTY_TERMINAL_OPT_WRITE_PTY, fn(&Impl::onWritePty));
    ghostty_terminal_set(t, GHOSTTY_TERMINAL_OPT_BELL, fn(&Impl::onBell));
    ghostty_terminal_set(t, GHOSTTY_TERMINAL_OPT_TITLE_CHANGED, fn(&Impl::onTitle));
    ghostty_terminal_set(t, GHOSTTY_TERMINAL_OPT_PWD_CHANGED, fn(&Impl::onPwd));
    ghostty_terminal_set(t, GHOSTTY_TERMINAL_OPT_CLIPBOARD_WRITE, fn(&Impl::onClipboardWrite));
    ghostty_terminal_set(t, GHOSTTY_TERMINAL_OPT_DESKTOP_NOTIFICATION, fn(&Impl::onNotification));
    ghostty_terminal_set(t, GHOSTTY_TERMINAL_OPT_DEVICE_ATTRIBUTES, fn(&Impl::onDeviceAttributes));
    ghostty_terminal_set(t, GHOSTTY_TERMINAL_OPT_SIZE, fn(&Impl::onSize));
    ghostty_terminal_set(t, GHOSTTY_TERMINAL_OPT_XTVERSION, fn(&Impl::onXtversion));
    ghostty_terminal_set(t, GHOSTTY_TERMINAL_OPT_COLOR_SCHEME, fn(&Impl::onColorScheme));
    // Grapheme clustering (DEC mode 2027) on by default, as in Ghostty: emoji
    // modifiers, ZWJ sequences and flags occupy one cell cluster.
    GhosttyTerminalModeConfig graphemes{GHOSTTY_MODE_GRAPHEME_CLUSTER, true};
    ghostty_terminal_set(t, GHOSTTY_TERMINAL_OPT_MODE_DEFAULT, &graphemes);
    // Clipboard reads (OSC 52 "?") stay unanswered: programs must not read the
    // user's clipboard without consent.
    d->applyScrollbackLimit();
}

GhosttyCore::~GhosttyCore()
{
    if (d->search)
        ghostty_search_free(d->search);
    if (d->selAnchor)
        ghostty_tracked_grid_ref_free(d->selAnchor);
    d->freeRoleMarks();
    ghostty_mouse_event_free(d->mouseEvent);
    ghostty_mouse_encoder_free(d->mouseEncoder);
    ghostty_key_event_free(d->keyEvent);
    ghostty_key_encoder_free(d->keyEncoder);
    ghostty_render_state_row_cells_free(d->cells);
    ghostty_render_state_row_iterator_free(d->rows);
    ghostty_render_state_free(d->rs);
    ghostty_terminal_free(d->t);
}

void GhosttyCore::feed(const char *data, size_t len)
{
    size_t off = 0;
    SequenceScanner::Hit hit;
    while (off < len && d->scanner.next(data, len, off, &hit)) {
        ghostty_terminal_vt_write(d->t, reinterpret_cast<const uint8_t *>(data + off), hit.end - off);
        off = hit.end;
        if (hit.kind == SequenceScanner::Hit::AltScreen) {
            d->checkAltScreen();
        } else if (hit.kind == SequenceScanner::Hit::RowRole) {
            d->trackRowRole(hit.role);
        } else if (hit.kind == SequenceScanner::Hit::Erase) {
            d->eraseRoleRows(hit.mark, hit.eraseParam);
        } else if (events.promptMark) {
            uint16_t y = 0;
            ghostty_terminal_get(d->t, GHOSTTY_TERMINAL_DATA_CURSOR_Y, &y);
            const PromptMark kind = hit.mark == 'A' ? MarkPromptStart
                : hit.mark == 'B'                   ? MarkCommandStart
                : hit.mark == 'C'                   ? MarkOutputStart
                                                    : MarkCommandFinished;
            events.promptMark(kind, int(y), hit.exitCode);
        }
    }
    if (off < len)
        ghostty_terminal_vt_write(d->t, reinterpret_cast<const uint8_t *>(data + off), len - off);
    d->checkAltScreen();
}

void GhosttyCore::resize(int rows, int cols, int cellWidthPx, int cellHeightPx)
{
    d->rowsN = std::max(1, rows);
    d->colsN = std::max(1, cols);
    d->cellW = std::max(1, cellWidthPx);
    d->cellH = std::max(1, cellHeightPx);
    ghostty_terminal_resize(d->t, uint16_t(d->colsN), uint16_t(d->rowsN), uint32_t(d->cellW), uint32_t(d->cellH));
    d->applyScrollbackLimit();
}

int GhosttyCore::rows() const { return d->rowsN; }
int GhosttyCore::columns() const { return d->colsN; }

void GhosttyCore::setScrollbackLines(int lines)
{
    d->scrollbackLines = std::max(0, lines);
    d->applyScrollbackLimit();
}

bool GhosttyCore::atGround() const
{
    bool ground = true;
    ghostty_terminal_get(d->t, GHOSTTY_TERMINAL_DATA_VT_GROUND, &ground);
    return ground;
}

void GhosttyCore::setCellPixelSize(int w, int h, int, int)
{
    d->cellW = std::max(1, w);
    d->cellH = std::max(1, h);
}

bool GhosttyCore::updateFrame(ViewportFrame *frame, bool force)
{
    if (ghostty_render_state_update(d->rs, d->t) != GHOSTTY_SUCCESS)
        return false;
    GhosttyRenderStateDirty dirty = GHOSTTY_RENDER_STATE_DIRTY_FALSE;
    ghostty_render_state_get(d->rs, GHOSTTY_RENDER_STATE_DATA_DIRTY, &dirty);
    if (dirty == GHOSTTY_RENDER_STATE_DIRTY_FALSE && !force && d->searchTotal == 0)
        return false;

    uint16_t cols = 0, rows = 0;
    ghostty_render_state_get(d->rs, GHOSTTY_RENDER_STATE_DATA_COLS, &cols);
    ghostty_render_state_get(d->rs, GHOSTTY_RENDER_STATE_DATA_ROWS, &rows);
    const bool sizeChanged = frame->rows != rows || frame->columns != cols;
    const bool full = force || sizeChanged || dirty == GHOSTTY_RENDER_STATE_DIRTY_FULL;
    frame->rows = rows;
    frame->columns = cols;
    frame->lines.resize(rows);
    frame->dirty.assign(rows, full ? 1 : 0);
    frame->full = full;
    // This core does not describe a scroll yet, and the frame is reused between calls, so the
    // fields are cleared rather than left holding another frame's shift (#3H5T). Ghostty's
    // dirty-tracking has the information (GHOSTTY_RENDER_STATE_DIRTY_*); wiring it up needs a
    // machine that builds this core, which spark is not.
    frame->scrolledBy = 0;
    frame->scrollTop = 0;
    frame->scrollBottom = 0;

    GhosttyRenderStateColors colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
    ghostty_render_state_get(d->rs, GHOSTTY_RENDER_STATE_DATA_COLORS, &colors);

    // Search highlights for the viewport (only while a search is active).
    std::vector<std::pair<GhosttyPointCoordinate, GhosttyPointCoordinate>> matches;
    GhosttyPointCoordinate curStart{0, 0}, curEnd{0, 0};
    bool haveCurrent = false;
    if (d->search && d->searchTotal > 0) {
        ghostty_search_feed(d->search);
        std::vector<GhosttySelection> storage(256, GHOSTTY_INIT_SIZED(GhosttySelection));
        GhosttySelectionBuffer buf{storage.data(), storage.size(), 0};
        GhosttyResult r = ghostty_search_get(d->search, GHOSTTY_SEARCH_DATA_VIEWPORT_MATCHES, &buf);
        if (r == GHOSTTY_OUT_OF_SPACE && buf.len > storage.size()) {
            storage.assign(buf.len, GHOSTTY_INIT_SIZED(GhosttySelection));
            buf = GhosttySelectionBuffer{storage.data(), storage.size(), 0};
            r = ghostty_search_get(d->search, GHOSTTY_SEARCH_DATA_VIEWPORT_MATCHES, &buf);
        }
        if (r == GHOSTTY_SUCCESS) {
            for (size_t i = 0; i < buf.len; ++i) {
                GhosttyPointCoordinate s, e;
                if (ghostty_terminal_point_from_grid_ref(d->t, &storage[i].start, GHOSTTY_POINT_TAG_VIEWPORT, &s) == GHOSTTY_SUCCESS
                    && ghostty_terminal_point_from_grid_ref(d->t, &storage[i].end, GHOSTTY_POINT_TAG_VIEWPORT, &e) == GHOSTTY_SUCCESS)
                    matches.push_back({s, e});
            }
        }
        GhosttySelection cur = GHOSTTY_INIT_SIZED(GhosttySelection);
        if (ghostty_search_get(d->search, GHOSTTY_SEARCH_DATA_SELECTED_MATCH, &cur) == GHOSTTY_SUCCESS
            && ghostty_terminal_point_from_grid_ref(d->t, &cur.start, GHOSTTY_POINT_TAG_VIEWPORT, &curStart) == GHOSTTY_SUCCESS
            && ghostty_terminal_point_from_grid_ref(d->t, &cur.end, GHOSTTY_POINT_TAG_VIEWPORT, &curEnd) == GHOSTTY_SUCCESS)
            haveCurrent = true;
        // Highlights move with scrolling; repaint every row that has or had one.
        std::fill(frame->dirty.begin(), frame->dirty.end(), 1);
        frame->full = true;
    }

    if (ghostty_render_state_get(d->rs, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &d->rows) != GHOSTTY_SUCCESS)
        return false;

    // Row roles by absolute row, and the viewport's offset in that space, so a
    // marked row keeps its band wherever the viewport is (and after it has
    // scrolled into history).
    const std::map<int, uint8_t> roles = d->roleMarkMap();
    const int roleTop = int(d->scrollbar().offset);

    static const GhosttyRenderStateRowCellsData cellKeys[] = {
        GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW,
        GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN,
        GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_HAS_STYLING,
    };
    std::u32string cps;
    int y = -1;
    while (ghostty_render_state_row_iterator_next(d->rows)) {
        ++y;
        if (y >= rows)
            break;
        bool rowDirty = false;
        ghostty_render_state_row_get(d->rows, GHOSTTY_RENDER_STATE_ROW_DATA_DIRTY, &rowDirty);
        if (!(frame->dirty[size_t(y)] || rowDirty))
            continue;
        frame->dirty[size_t(y)] = 1;
        Line &line = frame->lines[size_t(y)];
        line.clear();
        line.cells.resize(cols);

        GhosttyRow raw = 0;
        if (ghostty_render_state_row_get(d->rows, GHOSTTY_RENDER_STATE_ROW_DATA_RAW, &raw) == GHOSTTY_SUCCESS) {
            bool cont = false;
            ghostty_row_get(raw, GHOSTTY_ROW_DATA_WRAP_CONTINUATION, &cont);
            line.continuation = cont;
            GhosttyRowSemanticPrompt sp = GHOSTTY_ROW_SEMANTIC_NONE;
            ghostty_row_get(raw, GHOSTTY_ROW_DATA_SEMANTIC_PROMPT, &sp);
            if (sp == GHOSTTY_ROW_SEMANTIC_PROMPT)
                line.marks |= MarkPromptStart;
        }
        if (!roles.empty()) {
            const auto it = roles.find(roleTop + y);
            if (it != roles.end())
                line.marks |= it->second;
        }
        GhosttyRenderStateRowSelection rsel = GHOSTTY_INIT_SIZED(GhosttyRenderStateRowSelection);
        if (ghostty_render_state_row_get(d->rows, GHOSTTY_RENDER_STATE_ROW_DATA_SELECTION, &rsel) == GHOSTTY_SUCCESS) {
            line.selectionStart = int16_t(rsel.start_x);
            line.selectionEnd = int16_t(rsel.end_x);
        }
        for (const auto &m : matches) {
            if (int(m.first.y) > y || int(m.second.y) < y)
                continue;
            const uint16_t s = int(m.first.y) == y ? m.first.x : 0;
            const uint16_t e = int(m.second.y) == y ? m.second.x : uint16_t(cols - 1);
            const bool current = haveCurrent && curStart.x == m.first.x && curStart.y == m.first.y && curEnd.x == m.second.x
                && curEnd.y == m.second.y;
            line.highlights.push_back({s, e, current});
        }

        if (ghostty_render_state_row_get(d->rows, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &d->cells) != GHOSTTY_SUCCESS)
            continue;
        int x = -1;
        while (ghostty_render_state_row_cells_next(d->cells)) {
            ++x;
            if (x >= cols)
                break;
            Cell &c = line.cells[size_t(x)];
            GhosttyCell rawCell = 0;
            uint32_t glen = 0;
            bool styled = false;
            void *values[] = {&rawCell, &glen, &styled};
            ghostty_render_state_row_cells_get_multi(d->cells, 3, cellKeys, values, nullptr);

            GhosttyCellWide wide = GHOSTTY_CELL_WIDE_NARROW;
            ghostty_cell_get(rawCell, GHOSTTY_CELL_DATA_WIDE, &wide);
            if (wide == GHOSTTY_CELL_WIDE_SPACER_TAIL) {
                c.ch = kWideTail;
                c.width = 0;
            } else {
                c.width = wide == GHOSTTY_CELL_WIDE_WIDE ? 2 : 1;
                if (glen == 1) {
                    uint32_t cp = 0;
                    ghostty_render_state_row_cells_get(d->cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, &cp);
                    c.ch = cp;
                } else if (glen > 1) {
                    cps.resize(glen);
                    ghostty_render_state_row_cells_get(d->cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, cps.data());
                    line.appendCluster(&c, cps.data(), int(glen));
                }
            }

            GhosttyColorRgb rgb{0, 0, 0};
            if (ghostty_render_state_row_cells_get(d->cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &rgb) == GHOSTTY_SUCCESS)
                c.bg = packRgb(rgb);
            if (styled) {
                GhosttyStyle st = GHOSTTY_INIT_SIZED(GhosttyStyle);
                ghostty_render_state_row_cells_get(d->cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &st);
                if (st.fg_color.tag == GHOSTTY_STYLE_COLOR_PALETTE) {
                    int idx = st.fg_color.value.palette;
                    if (st.bold && idx < 8)
                        idx += 8; // bold is bright, as in Konsole/xterm defaults
                    c.fg = packRgb(colors.palette[idx]);
                } else if (st.fg_color.tag == GHOSTTY_STYLE_COLOR_RGB) {
                    c.fg = packRgb(st.fg_color.value.rgb);
                }
                uint16_t a = 0;
                if (st.bold) a |= AttrBold;
                if (st.italic) a |= AttrItalic;
                if (st.faint) a |= AttrFaint;
                if (st.blink) a |= AttrBlink;
                if (st.inverse) a |= AttrReverse;
                if (st.invisible) a |= AttrConceal;
                if (st.strikethrough) a |= AttrStrike;
                if (st.underline == GHOSTTY_SGR_UNDERLINE_DOUBLE) a |= AttrDoubleUnderline;
                else if (st.underline == GHOSTTY_SGR_UNDERLINE_CURLY) a |= AttrCurlyUnderline;
                else if (st.underline != GHOSTTY_SGR_UNDERLINE_NONE) a |= AttrUnderline;
                c.attrs |= a;
            }
            bool hasLink = false;
            ghostty_cell_get(rawCell, GHOSTTY_CELL_DATA_HAS_HYPERLINK, &hasLink);
            if (hasLink) {
                GhosttyGridRef ref;
                std::string uri;
                if (d->gridRef(GHOSTTY_POINT_TAG_VIEWPORT, x, y, &ref) && Impl::hyperlinkUri(ref, &uri))
                    c.link = d->internLink(uri);
            }
        }
    }

    GhosttyRenderStateCursor cur = GHOSTTY_INIT_SIZED(GhosttyRenderStateCursor);
    ghostty_render_state_get(d->rs, GHOSTTY_RENDER_STATE_DATA_CURSOR, &cur);
    frame->cursorInViewport = cur.viewport_has_value;
    frame->cursor.visible = cur.visible && cur.viewport_has_value;
    frame->cursor.blink = cur.blinking;
    frame->cursor.row = cur.viewport_has_value ? cur.viewport_y : 0;
    frame->cursor.col = cur.viewport_has_value ? cur.viewport_x : 0;
    switch (cur.visual_style) {
    case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BAR: frame->cursor.shape = CursorShape::Bar; break;
    case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_UNDERLINE: frame->cursor.shape = CursorShape::Underline; break;
    default: frame->cursor.shape = CursorShape::Block; break;
    }
    d->lastCursorShape = frame->cursor.shape;

    const GhosttyTerminalScrollbar sb = d->scrollbar();
    frame->historyRows = int(sb.total > sb.len ? sb.total - sb.len : 0);
    frame->viewportTop = int(sb.offset);
    frame->altScreen = d->alt;

    ghostty_render_state_clean(d->rs);
    return true;
}

QString GhosttyCore::screenText() const
{
    return d->rangeText(GHOSTTY_POINT_TAG_ACTIVE, 0, d->rowsN - 1).join(QLatin1Char('\n'));
}

QStringList GhosttyCore::historyText(int maxLines) const
{
    const int h = historyRows();
    if (h <= 0 || maxLines <= 0)
        return {};
    return d->rangeText(GHOSTTY_POINT_TAG_HISTORY, std::max(0, h - maxLines), h - 1);
}

// Styled scrollback, in the same Line the viewport frame carries.
//
// The render state only ever describes the viewport, so history is read
// through grid refs (GHOSTTY_POINT_TAG_HISTORY, y = 0 the oldest line, the
// coordinates historyText() uses): one ref per cell, giving the cell, its
// grapheme cluster, its style and its OSC 8 URI. That is an FFI call or three
// per cell, which is why this is a paging call — at most a couple of hundred
// rows — and never a per-frame one.
//
// Nothing here writes: no scroll, no render-state update, no selection. The
// only state that moves is the link-id table, exactly as updateFrame() grows
// it, so an id in a history row means the same URI as one on the screen.
int GhosttyCore::historyLines(int fromRow, int count, std::vector<Line> *out) const
{
    if (out)
        out->clear();
    const int total = historyRows();
    const int from = std::max(0, std::min(fromRow, total));
    const int want = std::max(0, std::min(count, total - from));
    if (!out || want == 0)
        return from;

    // The live palette, OSC 4 overrides included — the same table the render
    // state resolves the screen's indexed colours with, so a line keeps its
    // colour when it scrolls off the screen into history.
    GhosttyColorRgb palette[256];
    const bool havePalette =
        ghostty_terminal_get(d->t, GHOSTTY_TERMINAL_DATA_COLOR_PALETTE, palette) == GHOSTTY_SUCCESS;
    const auto paletteColor = [&](int index) -> uint32_t {
        // Without the table there is nothing to resolve against; send the index
        // itself rather than a made-up colour (CellColor carries both kinds).
        return havePalette ? packRgb(palette[index & 0xFF]) : CellColor::indexed(uint8_t(index));
    };

    out->resize(size_t(want));
    // Row roles travel with the line into history (OSC 7772, CellTypes.h).
    const std::map<int, uint8_t> roles = d->roleMarkMap();
    std::vector<uint32_t> cps;
    std::string uri;
    for (int i = 0; i < want; ++i) {
        Line &line = (*out)[size_t(i)];
        const int y = from + i;
        const auto it = roles.find(y);
        if (it != roles.end())
            line.marks |= it->second;
        GhosttyGridRef rowRef;
        if (!d->gridRef(GHOSTTY_POINT_TAG_HISTORY, 0, y, &rowRef))
            continue;
        GhosttyRow raw = 0;
        if (ghostty_grid_ref_row(&rowRef, &raw) == GHOSTTY_SUCCESS) {
            bool cont = false;
            ghostty_row_get(raw, GHOSTTY_ROW_DATA_WRAP_CONTINUATION, &cont);
            line.continuation = cont;
            GhosttyRowSemanticPrompt sp = GHOSTTY_ROW_SEMANTIC_NONE;
            ghostty_row_get(raw, GHOSTTY_ROW_DATA_SEMANTIC_PROMPT, &sp);
            if (sp == GHOSTTY_ROW_SEMANTIC_PROMPT)
                line.marks |= MarkPromptStart;
        }
        line.wrapColumns = uint16_t(d->colsN);
        line.cells.resize(size_t(d->colsN));
        for (int x = 0; x < d->colsN; ++x) {
            GhosttyGridRef ref = rowRef;
            if (x > 0 && !d->gridRef(GHOSTTY_POINT_TAG_HISTORY, x, y, &ref))
                break;
            GhosttyCell rawCell = 0;
            if (ghostty_grid_ref_cell(&ref, &rawCell) != GHOSTTY_SUCCESS)
                break;
            Cell &c = line.cells[size_t(x)];

            GhosttyCellWide wide = GHOSTTY_CELL_WIDE_NARROW;
            ghostty_cell_get(rawCell, GHOSTTY_CELL_DATA_WIDE, &wide);
            if (wide == GHOSTTY_CELL_WIDE_SPACER_TAIL) {
                c.ch = kWideTail;
                c.width = 0;
            } else {
                c.width = wide == GHOSTTY_CELL_WIDE_WIDE ? 2 : 1;
                size_t glen = 0;
                cps.resize(8);
                GhosttyResult r = ghostty_grid_ref_graphemes(&ref, cps.data(), cps.size(), &glen);
                if (r == GHOSTTY_OUT_OF_SPACE && glen > 0 && glen < (1u << 16)) {
                    cps.resize(glen);
                    r = ghostty_grid_ref_graphemes(&ref, cps.data(), cps.size(), &glen);
                }
                if (r == GHOSTTY_SUCCESS && glen == 1) {
                    c.ch = cps[0];
                } else if (r == GHOSTTY_SUCCESS && glen > 1) {
                    std::u32string cluster(cps.begin(), cps.begin() + long(glen));
                    line.appendCluster(&c, cluster.data(), int(cluster.size()));
                }
            }

            GhosttyStyle st = GHOSTTY_INIT_SIZED(GhosttyStyle);
            bool styled = false;
            ghostty_cell_get(rawCell, GHOSTTY_CELL_DATA_HAS_STYLING, &styled);
            if (styled && ghostty_grid_ref_style(&ref, &st) == GHOSTTY_SUCCESS) {
                if (st.fg_color.tag == GHOSTTY_STYLE_COLOR_PALETTE) {
                    int index = st.fg_color.value.palette;
                    if (st.bold && index < 8)
                        index += 8; // bold is bright, as updateFrame() resolves it
                    c.fg = paletteColor(index);
                } else if (st.fg_color.tag == GHOSTTY_STYLE_COLOR_RGB) {
                    c.fg = packRgb(st.fg_color.value.rgb);
                }
                uint16_t a = 0;
                if (st.bold) a |= AttrBold;
                if (st.italic) a |= AttrItalic;
                if (st.faint) a |= AttrFaint;
                if (st.blink) a |= AttrBlink;
                if (st.inverse) a |= AttrReverse;
                if (st.invisible) a |= AttrConceal;
                if (st.strikethrough) a |= AttrStrike;
                if (st.underline == GHOSTTY_SGR_UNDERLINE_DOUBLE) a |= AttrDoubleUnderline;
                else if (st.underline == GHOSTTY_SGR_UNDERLINE_CURLY) a |= AttrCurlyUnderline;
                else if (st.underline != GHOSTTY_SGR_UNDERLINE_NONE) a |= AttrUnderline;
                c.attrs |= a;
            }
            // The background has the three sources the render state flattens:
            // a text-less cell carrying a colour, or the style's own.
            GhosttyCellContentTag content = GHOSTTY_CELL_CONTENT_CODEPOINT;
            ghostty_cell_get(rawCell, GHOSTTY_CELL_DATA_CONTENT_TAG, &content);
            if (content == GHOSTTY_CELL_CONTENT_BG_COLOR_RGB) {
                GhosttyColorRgb rgb{0, 0, 0};
                if (ghostty_cell_get(rawCell, GHOSTTY_CELL_DATA_COLOR_RGB, &rgb) == GHOSTTY_SUCCESS)
                    c.bg = packRgb(rgb);
            } else if (content == GHOSTTY_CELL_CONTENT_BG_COLOR_PALETTE) {
                GhosttyColorPaletteIndex index = 0;
                if (ghostty_cell_get(rawCell, GHOSTTY_CELL_DATA_COLOR_PALETTE, &index) == GHOSTTY_SUCCESS)
                    c.bg = paletteColor(index);
            } else if (st.bg_color.tag == GHOSTTY_STYLE_COLOR_RGB) {
                c.bg = packRgb(st.bg_color.value.rgb);
            } else if (st.bg_color.tag == GHOSTTY_STYLE_COLOR_PALETTE) {
                c.bg = paletteColor(st.bg_color.value.palette);
            }

            bool hasLink = false;
            ghostty_cell_get(rawCell, GHOSTTY_CELL_DATA_HAS_HYPERLINK, &hasLink);
            if (hasLink && Impl::hyperlinkUri(ref, &uri))
                c.link = d->internLink(uri);
        }
        // Trailing blanks are noise on the wire; the libvterm core's scrollback
        // is trimmed the same way, so both cores hand the serializer the same
        // shape and a row may be shorter than columns().
        while (!line.cells.empty() && line.cells.back().isBlank() && line.cells.back().attrs == 0)
            line.cells.pop_back();
    }
    return from;
}

bool GhosttyCore::altScreen() const { return d->alt; }

MouseTracking GhosttyCore::mouseTracking() const
{
    if (modeValue(d->t, GHOSTTY_MODE_ANY_MOUSE))
        return MouseTracking::Move;
    if (modeValue(d->t, GHOSTTY_MODE_BUTTON_MOUSE))
        return MouseTracking::Drag;
    if (modeValue(d->t, GHOSTTY_MODE_NORMAL_MOUSE) || modeValue(d->t, GHOSTTY_MODE_X10_MOUSE))
        return MouseTracking::Click;
    return MouseTracking::None;
}

bool GhosttyCore::mouseSgrPixels() const { return modeValue(d->t, GHOSTTY_MODE_SGR_PIXELS_MOUSE); }
bool GhosttyCore::bracketedPaste() const { return modeValue(d->t, GHOSTTY_MODE_BRACKETED_PASTE); }
QString GhosttyCore::title() const { return d->title; }

CursorState GhosttyCore::activeCursor() const
{
    CursorState c;
    uint16_t x = 0, y = 0;
    bool visible = true;
    ghostty_terminal_get(d->t, GHOSTTY_TERMINAL_DATA_CURSOR_X, &x);
    ghostty_terminal_get(d->t, GHOSTTY_TERMINAL_DATA_CURSOR_Y, &y);
    ghostty_terminal_get(d->t, GHOSTTY_TERMINAL_DATA_CURSOR_VISIBLE, &visible);
    c.row = y;
    c.col = x;
    c.visible = visible;
    c.shape = d->lastCursorShape; // DATA_CURSOR_STYLE is the SGR style; the shape comes from the render state
    return c;
}

int GhosttyCore::historyRows() const
{
    const GhosttyTerminalScrollbar sb = d->scrollbar();
    return int(sb.total > sb.len ? sb.total - sb.len : 0);
}

int GhosttyCore::viewportTop() const { return int(d->scrollbar().offset); }

bool GhosttyCore::viewportAtBottom() const
{
    bool active = true;
    ghostty_terminal_get(d->t, GHOSTTY_TERMINAL_DATA_VIEWPORT_ACTIVE, &active);
    return active;
}

void GhosttyCore::scrollViewport(int deltaRows)
{
    GhosttyTerminalScrollViewport s;
    s.tag = GHOSTTY_SCROLL_VIEWPORT_DELTA;
    s.value.delta = deltaRows;
    ghostty_terminal_scroll_viewport(d->t, s);
}

void GhosttyCore::scrollViewportToTop()
{
    GhosttyTerminalScrollViewport s;
    s.tag = GHOSTTY_SCROLL_VIEWPORT_TOP;
    s.value.delta = 0;
    ghostty_terminal_scroll_viewport(d->t, s);
}

void GhosttyCore::scrollViewportToBottom()
{
    GhosttyTerminalScrollViewport s;
    s.tag = GHOSTTY_SCROLL_VIEWPORT_BOTTOM;
    s.value.delta = 0;
    ghostty_terminal_scroll_viewport(d->t, s);
}

void GhosttyCore::scrollViewportToRow(int row)
{
    GhosttyTerminalScrollViewport s;
    s.tag = GHOSTTY_SCROLL_VIEWPORT_ROW;
    s.value.row = size_t(std::max(0, row));
    ghostty_terminal_scroll_viewport(d->t, s);
}

bool GhosttyCore::scrollToPrompt(int direction)
{
    const int total = historyRows() + d->rowsN;
    const int top = viewportTop();
    for (int y = top + (direction < 0 ? -1 : 1); y >= 0 && y < total; y += direction < 0 ? -1 : 1) {
        GhosttyGridRef ref;
        if (!d->gridRef(GHOSTTY_POINT_TAG_SCREEN, 0, y, &ref))
            continue;
        GhosttyRow row = 0;
        if (ghostty_grid_ref_row(&ref, &row) != GHOSTTY_SUCCESS)
            continue;
        GhosttyRowSemanticPrompt sp = GHOSTTY_ROW_SEMANTIC_NONE;
        ghostty_row_get(row, GHOSTTY_ROW_DATA_SEMANTIC_PROMPT, &sp);
        if (sp == GHOSTTY_ROW_SEMANTIC_PROMPT) {
            scrollViewportToRow(y);
            return true;
        }
    }
    return false;
}

QString GhosttyCore::hyperlinkAt(int row, int col) const
{
    GhosttyGridRef ref;
    std::string uri;
    if (!d->gridRef(GHOSTTY_POINT_TAG_VIEWPORT, col, row, &ref) || !Impl::hyperlinkUri(ref, &uri))
        return QString();
    return QString::fromStdString(uri);
}

// Every run of cells whose OSC 8 URI starts with `prefix`, in absolute
// scrollback rows (GHOSTTY_POINT_TAG_SCREEN's coordinates, the ones
// scrollToPrompt() and scrollViewportToRow() use).
//
// libghostty-vt answers a hyperlink per grid ref, so a scan of every cell of
// the whole scrollback would be one FFI call per cell. This walks **column 0**
// of each row instead and only then walks right to the end of the run, which is
// two calls per row: Relay's own anchor lines carry their hyperlink from the
// first column (the view overpaints the chevron there), so that is where they
// are found. An anchor that starts further right is not seen on this core.
std::vector<VtCore::HyperlinkRun> GhosttyCore::hyperlinkRuns(const QString &prefix) const
{
    std::vector<HyperlinkRun> out;
    if (prefix.isEmpty())
        return out;
    const std::string pre = prefix.toStdString();
    const int total = historyRows() + d->rowsN;
    std::string uri, prev;
    for (int y = 0; y < total; ++y) {
        GhosttyGridRef ref;
        if (!d->gridRef(GHOSTTY_POINT_TAG_SCREEN, 0, y, &ref) || !Impl::hyperlinkUri(ref, &uri)
            || uri.size() < pre.size() || uri.compare(0, pre.size(), pre) != 0) {
            prev.clear();
            continue;
        }
        // The same URI on the row right above continues the run (a soft-wrapped
        // anchor line); anything else starts a new one.
        if (!out.empty() && uri == prev && out.back().endRow == y - 1) {
            out.back().endRow = y;
        } else {
            HyperlinkRun r;
            r.uri = QString::fromStdString(uri);
            r.startRow = r.endRow = y;
            r.startCol = 0;
            out.push_back(r);
        }
        prev = uri;
        // How far right the run reaches on this row (for the anchor's extent).
        std::string cell;
        int endCol = 0;
        for (int x = 1; x < d->colsN; ++x) {
            GhosttyGridRef cref;
            if (!d->gridRef(GHOSTTY_POINT_TAG_SCREEN, x, y, &cref) || !Impl::hyperlinkUri(cref, &cell) || cell != uri)
                break;
            endCol = x;
        }
        out.back().endCol = endCol;
    }
    return out;
}

void GhosttyCore::selectionBegin(int row, int col, SelectionUnit unit, bool rectangle)
{
    d->selUnit = unit;
    d->selRect = rectangle;
    GhosttyPoint p;
    p.tag = GHOSTTY_POINT_TAG_VIEWPORT;
    p.value.coordinate.x = uint16_t(std::max(0, col));
    p.value.coordinate.y = uint32_t(std::max(0, row));
    if (d->selAnchor)
        ghostty_tracked_grid_ref_free(d->selAnchor);
    d->selAnchor = nullptr;
    ghostty_terminal_grid_ref_track(d->t, p, &d->selAnchor);
    d->selAnchorOnAlt = d->alt;
    if (unit == SelectionUnit::Cell) {
        d->setSelection(nullptr);
        return;
    }
    selectionExtend(row, col);
}

void GhosttyCore::selectionExtend(int row, int col)
{
    if (!d->selAnchor || !ghostty_tracked_grid_ref_has_value(d->selAnchor) || d->selAnchorOnAlt != d->alt)
        return;
    GhosttyGridRef anchor = GHOSTTY_INIT_SIZED(GhosttyGridRef);
    if (ghostty_tracked_grid_ref_snapshot(d->selAnchor, &anchor) != GHOSTTY_SUCCESS)
        return;
    GhosttyGridRef here;
    if (!d->gridRef(GHOSTTY_POINT_TAG_VIEWPORT, std::min(col, d->colsN - 1), std::min(row, d->rowsN - 1), &here))
        return;

    GhosttySelection sel = GHOSTTY_INIT_SIZED(GhosttySelection);
    if (d->selUnit == SelectionUnit::Word) {
        // Nearest word from the click towards the pointer and from the pointer
        // back towards the click; the selection spans both (upstream recipe).
        static const uint32_t boundaries[] = {' ', '\t', '"', '\'', '`', '(', ')', '[', ']', '{', '}', '<', '>', '|', ',', ';', 0x2502};
        GhosttyTerminalSelectWordBetweenOptions o = GHOSTTY_INIT_SIZED(GhosttyTerminalSelectWordBetweenOptions);
        o.boundary_codepoints = boundaries;
        o.boundary_codepoints_len = sizeof boundaries / sizeof boundaries[0];
        GhosttySelection fromAnchor = GHOSTTY_INIT_SIZED(GhosttySelection);
        GhosttySelection fromHere = GHOSTTY_INIT_SIZED(GhosttySelection);
        o.start = anchor;
        o.end = here;
        const bool haveA = ghostty_terminal_select_word_between(d->t, &o, &fromAnchor) == GHOSTTY_SUCCESS;
        o.start = here;
        o.end = anchor;
        const bool haveH = ghostty_terminal_select_word_between(d->t, &o, &fromHere) == GHOSTTY_SUCCESS;
        if (!haveA && !haveH)
            return;
        if (haveA && haveH) {
            GhosttyPointCoordinate pa, ph;
            ghostty_terminal_point_from_grid_ref(d->t, &fromAnchor.start, GHOSTTY_POINT_TAG_SCREEN, &pa);
            ghostty_terminal_point_from_grid_ref(d->t, &fromHere.start, GHOSTTY_POINT_TAG_SCREEN, &ph);
            const bool forward = pa.y < ph.y || (pa.y == ph.y && pa.x <= ph.x);
            sel.start = forward ? fromAnchor.start : fromHere.start;
            sel.end = forward ? fromHere.end : fromAnchor.end;
        } else {
            sel = haveA ? fromAnchor : fromHere;
        }
    } else if (d->selUnit == SelectionUnit::Line) {
        GhosttySelection a = GHOSTTY_INIT_SIZED(GhosttySelection);
        GhosttySelection b = GHOSTTY_INIT_SIZED(GhosttySelection);
        GhosttyTerminalSelectLineOptions o = GHOSTTY_INIT_SIZED(GhosttyTerminalSelectLineOptions);
        o.ref = anchor;
        if (ghostty_terminal_select_line(d->t, &o, &a) != GHOSTTY_SUCCESS)
            return;
        o.ref = here;
        if (ghostty_terminal_select_line(d->t, &o, &b) != GHOSTTY_SUCCESS)
            return;
        GhosttyPointCoordinate pa, pb;
        ghostty_terminal_point_from_grid_ref(d->t, &a.start, GHOSTTY_POINT_TAG_SCREEN, &pa);
        ghostty_terminal_point_from_grid_ref(d->t, &b.start, GHOSTTY_POINT_TAG_SCREEN, &pb);
        const bool forward = pa.y < pb.y || (pa.y == pb.y && pa.x <= pb.x);
        sel.start = forward ? a.start : b.start;
        sel.end = forward ? b.end : a.end;
    } else {
        sel.start = anchor;
        sel.end = here;
        sel.rectangle = d->selRect;
    }
    d->setSelection(&sel);
}

void GhosttyCore::selectionClear()
{
    d->setSelection(nullptr);
    if (d->selAnchor) {
        ghostty_tracked_grid_ref_free(d->selAnchor);
        d->selAnchor = nullptr;
    }
}

bool GhosttyCore::hasSelection() const
{
    GhosttySelection sel = GHOSTTY_INIT_SIZED(GhosttySelection);
    return ghostty_terminal_get(d->t, GHOSTTY_TERMINAL_DATA_SELECTION, &sel) == GHOSTTY_SUCCESS;
}

QString GhosttyCore::selectedText() const
{
    GhosttySelection sel = GHOSTTY_INIT_SIZED(GhosttySelection);
    if (ghostty_terminal_get(d->t, GHOSTTY_TERMINAL_DATA_SELECTION, &sel) != GHOSTTY_SUCCESS)
        return QString();
    return d->formatSelection(&sel, true);
}

void GhosttyCore::selectAll()
{
    GhosttySelection sel = GHOSTTY_INIT_SIZED(GhosttySelection);
    if (ghostty_terminal_select_all(d->t, &sel) == GHOSTTY_SUCCESS)
        d->setSelection(&sel);
}

int GhosttyCore::searchSet(const QString &needle)
{
    if (needle.isEmpty()) {
        if (d->search) {
            ghostty_search_free(d->search);
            d->search = nullptr;
        }
        d->searchTotal = 0;
        return 0;
    }
    if (!d->search)
        ghostty_search_new(nullptr, &d->search, d->t);
    const QByteArray utf8 = needle.toUtf8();
    GhosttyString s{reinterpret_cast<const uint8_t *>(utf8.constData()), size_t(utf8.size())};
    ghostty_search_set(d->search, GHOSTTY_SEARCH_OPT_NEEDLE, &s);
    ghostty_search_run(d->search);
    size_t total = 0;
    ghostty_search_get(d->search, GHOSTTY_SEARCH_DATA_TOTAL_MATCHES, &total);
    d->searchTotal = int(total);
    return d->searchTotal;
}

int GhosttyCore::searchStep(bool backwards)
{
    if (!d->search)
        return -1;
    ghostty_search_run(d->search);
    if (ghostty_search_set(d->search, backwards ? GHOSTTY_SEARCH_OPT_SELECT_NEXT : GHOSTTY_SEARCH_OPT_SELECT_PREV, nullptr)
        != GHOSTTY_SUCCESS)
        return -1;
    size_t total = 0, idx = 0;
    ghostty_search_get(d->search, GHOSTTY_SEARCH_DATA_TOTAL_MATCHES, &total);
    d->searchTotal = int(total);
    if (ghostty_search_get(d->search, GHOSTTY_SEARCH_DATA_SELECTED_INDEX, &idx) != GHOSTTY_SUCCESS)
        return -1;
    return int(idx);
}

int GhosttyCore::searchMatchCount() const { return d->searchTotal; }

int GhosttyCore::searchCurrentRow() const
{
    if (!d->search || d->searchTotal == 0)
        return -1;
    GhosttySelection cur = GHOSTTY_INIT_SIZED(GhosttySelection);
    GhosttyPointCoordinate start{0, 0};
    if (ghostty_search_get(d->search, GHOSTTY_SEARCH_DATA_SELECTED_MATCH, &cur) != GHOSTTY_SUCCESS
        || ghostty_terminal_point_from_grid_ref(d->t, &cur.start, GHOSTTY_POINT_TAG_SCREEN, &start) != GHOSTTY_SUCCESS)
        return -1;
    return int(start.y);
}

void GhosttyCore::sendKey(const KeyInput &key)
{
    GhosttyKey gk = key.key != Key::None ? specialKey(key.key) : textKey(key.codepoint);
    ghostty_key_event_set_action(d->keyEvent, key.release ? GHOSTTY_KEY_ACTION_RELEASE
                                     : key.repeat        ? GHOSTTY_KEY_ACTION_REPEAT
                                                         : GHOSTTY_KEY_ACTION_PRESS);
    ghostty_key_event_set_key(d->keyEvent, gk);
    ghostty_key_event_set_mods(d->keyEvent, toMods(key.modifiers));
    // Shift is consumed when it produced the text (e.g. 'A' from Shift+a).
    GhosttyMods consumed = 0;
    if ((key.modifiers & ModShift) && !key.text.isEmpty() && key.key == Key::None)
        consumed |= GHOSTTY_MODS_SHIFT;
    ghostty_key_event_set_consumed_mods(d->keyEvent, consumed);
    // Qt reports Ctrl+C as text "\x03"; the encoder wants the printable text only.
    QByteArray utf8 = key.text.toUtf8();
    if (!utf8.isEmpty() && (uchar(utf8[0]) < 0x20 || uchar(utf8[0]) == 0x7f))
        utf8.clear();
    ghostty_key_event_set_utf8(d->keyEvent, utf8.constData(), size_t(utf8.size()));
    uint32_t unshifted = key.codepoint;
    if (unshifted >= 'A' && unshifted <= 'Z')
        unshifted = unshifted - 'A' + 'a';
    ghostty_key_event_set_unshifted_codepoint(d->keyEvent, unshifted);
    ghostty_key_event_set_composing(d->keyEvent, false);

    ghostty_key_encoder_setopt_from_terminal(d->keyEncoder, d->t);
    char buf[128];
    size_t written = 0;
    if (ghostty_key_encoder_encode(d->keyEncoder, d->keyEvent, buf, sizeof buf, &written) == GHOSTTY_SUCCESS && written > 0) {
        if (events.reply)
            events.reply(buf, written);
    } else if (gk == GHOSTTY_KEY_UNIDENTIFIED && !utf8.isEmpty() && !key.release) {
        // Keys the encoder does not know (non-Latin layouts): send the text.
        if (key.modifiers & ModAlt) {
            if (events.reply)
                events.reply("\x1b", 1);
        }
        if (events.reply)
            events.reply(utf8.constData(), size_t(utf8.size()));
    }
}

void GhosttyCore::sendText(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    if (!utf8.isEmpty() && events.reply)
        events.reply(utf8.constData(), size_t(utf8.size()));
}

void GhosttyCore::sendMouse(const MouseInput &m)
{
    ghostty_mouse_encoder_setopt_from_terminal(d->mouseEncoder, d->t);
    GhosttyMouseEncoderSize size = GHOSTTY_INIT_SIZED(GhosttyMouseEncoderSize);
    size.screen_width = uint32_t(d->colsN * d->cellW);
    size.screen_height = uint32_t(d->rowsN * d->cellH);
    size.cell_width = uint32_t(d->cellW);
    size.cell_height = uint32_t(d->cellH);
    ghostty_mouse_encoder_setopt(d->mouseEncoder, GHOSTTY_MOUSE_ENCODER_OPT_SIZE, &size);

    if (m.action == MouseInput::Action::Press && m.button >= MouseButton::Left && m.button <= MouseButton::Right)
        d->mouseButtonDown = true;
    else if (m.action == MouseInput::Action::Release)
        d->mouseButtonDown = false;
    bool anyPressed = d->mouseButtonDown;
    ghostty_mouse_encoder_setopt(d->mouseEncoder, GHOSTTY_MOUSE_ENCODER_OPT_ANY_BUTTON_PRESSED, &anyPressed);

    ghostty_mouse_event_set_action(d->mouseEvent, m.action == MouseInput::Action::Press ? GHOSTTY_MOUSE_ACTION_PRESS
                                       : m.action == MouseInput::Action::Release     ? GHOSTTY_MOUSE_ACTION_RELEASE
                                                                                     : GHOSTTY_MOUSE_ACTION_MOTION);
    GhosttyMouseButton b = GHOSTTY_MOUSE_BUTTON_UNKNOWN;
    switch (m.button) {
    case MouseButton::Left: b = GHOSTTY_MOUSE_BUTTON_LEFT; break;
    case MouseButton::Middle: b = GHOSTTY_MOUSE_BUTTON_MIDDLE; break;
    case MouseButton::Right: b = GHOSTTY_MOUSE_BUTTON_RIGHT; break;
    case MouseButton::WheelUp: b = GHOSTTY_MOUSE_BUTTON_FOUR; break;
    case MouseButton::WheelDown: b = GHOSTTY_MOUSE_BUTTON_FIVE; break;
    case MouseButton::WheelLeft: b = GHOSTTY_MOUSE_BUTTON_SIX; break;
    case MouseButton::WheelRight: b = GHOSTTY_MOUSE_BUTTON_SEVEN; break;
    case MouseButton::None: break;
    }
    if (b == GHOSTTY_MOUSE_BUTTON_UNKNOWN)
        ghostty_mouse_event_clear_button(d->mouseEvent);
    else
        ghostty_mouse_event_set_button(d->mouseEvent, b);
    ghostty_mouse_event_set_mods(d->mouseEvent, toMods(m.modifiers));
    // Positions are pixels in the grid; use the cell centre when only cells are known.
    GhosttyMousePosition pos{m.x, m.y};
    if (m.x <= 0 && m.y <= 0)
        pos = GhosttyMousePosition{float(m.col * d->cellW + d->cellW / 2), float(m.row * d->cellH + d->cellH / 2)};
    ghostty_mouse_event_set_position(d->mouseEvent, pos);

    char buf[64];
    size_t written = 0;
    if (ghostty_mouse_encoder_encode(d->mouseEncoder, d->mouseEvent, buf, sizeof buf, &written) == GHOSTTY_SUCCESS && written > 0
        && events.reply)
        events.reply(buf, written);
}

void GhosttyCore::paste(const QString &text)
{
    QByteArray data = text.toUtf8();
    const bool bracketed = modeValue(d->t, GHOSTTY_MODE_BRACKETED_PASTE);
    QByteArray out(data.size() + 16, Qt::Uninitialized);
    size_t written = 0;
    GhosttyResult r = ghostty_paste_encode(data.data(), size_t(data.size()), bracketed, out.data(), size_t(out.size()), &written);
    if (r == GHOSTTY_OUT_OF_SPACE) {
        data = text.toUtf8();
        out.resize(int(written));
        r = ghostty_paste_encode(data.data(), size_t(data.size()), bracketed, out.data(), size_t(out.size()), &written);
    }
    if (r == GHOSTTY_SUCCESS && written > 0 && events.reply)
        events.reply(out.constData(), written);
}

void GhosttyCore::focusChanged(bool focused)
{
    if (!modeValue(d->t, GHOSTTY_MODE_FOCUS_EVENT))
        return;
    char buf[8];
    size_t written = 0;
    if (ghostty_focus_encode(focused ? GHOSTTY_FOCUS_GAINED : GHOSTTY_FOCUS_LOST, buf, sizeof buf, &written) == GHOSTTY_SUCCESS
        && events.reply)
        events.reply(buf, written);
}

void GhosttyCore::clearScrollback()
{
    // Out of band (not ED 3 through the parser, which may be mid-sequence):
    // a zero byte budget erases retained history, then restore the budget.
    const size_t zero = 0;
    ghostty_terminal_set(d->t, GHOSTTY_TERMINAL_OPT_SCROLLBACK_MAX_BYTES, &zero);
    d->applyScrollbackLimit();
}

void GhosttyCore::reset()
{
    ghostty_terminal_reset(d->t);
    d->scanner.reset();
    d->freeRoleMarks(); // the reset cleared the screens the refs point into
    d->checkAltScreen();
}

void GhosttyCore::setColors(uint32_t fg, uint32_t bg, const uint32_t *palette16)
{
    GhosttyColorRgb f = toRgb(fg), b = toRgb(bg);
    ghostty_terminal_set(d->t, GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND, &f);
    ghostty_terminal_set(d->t, GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND, &b);
    if (palette16) {
        GhosttyColorRgb pal[256];
        for (int i = 0; i < 256; ++i)
            pal[i] = i < 16 ? toRgb(palette16[i]) : xtermColor(i);
        ghostty_terminal_set(d->t, GHOSTTY_TERMINAL_OPT_COLOR_PALETTE, pal);
    }
}

void GhosttyCore::setClipboardWriteAllowed(bool allowed) { d->clipboardAllowed = allowed; }

} // namespace relay
