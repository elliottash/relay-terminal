// SPDX-License-Identifier: AGPL-3.0-or-later
// Emulator-neutral cell, line and input types shared by every VtCore
// implementation, the session, the view and the tests.
#pragma once

#include <QString>

#include <cstdint>
#include <string>
#include <vector>

namespace relay {

// Packed colour: high byte = kind, low 24 bits = palette index or 0xRRGGBB.
struct CellColor {
    enum Kind : uint8_t { Default = 0, Indexed = 1, Rgb = 2 };
    static uint32_t defaultColor() { return 0; }
    static uint32_t indexed(uint8_t i) { return (uint32_t(Indexed) << 24) | i; }
    static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) { return (uint32_t(Rgb) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b; }
    static Kind kind(uint32_t c) { return Kind(c >> 24); }
    static uint32_t value(uint32_t c) { return c & 0xFFFFFF; }
};

enum CellAttr : uint16_t {
    AttrBold = 1 << 0,
    AttrItalic = 1 << 1,
    AttrUnderline = 1 << 2,
    AttrDoubleUnderline = 1 << 3,
    AttrCurlyUnderline = 1 << 4,
    AttrBlink = 1 << 5,
    AttrReverse = 1 << 6,
    AttrConceal = 1 << 7,
    AttrStrike = 1 << 8,
    AttrFaint = 1 << 9,
    AttrCluster = 1 << 10, // ch is an offset into Line::clusters
};

constexpr char32_t kWideTail = 0xFFFFFFFF; // right half of a double-width cell

struct Cell {
    char32_t ch = 0;  // first codepoint; 0 = blank; kWideTail; or cluster offset (AttrCluster)
    uint32_t fg = 0;  // CellColor
    uint32_t bg = 0;  // CellColor
    uint32_t link = 0; // hyperlink id (VtCore::hyperlinkUri), 0 = none
    uint16_t attrs = 0;
    uint8_t width = 1; // 1 or 2 (0 only for kWideTail)
    uint8_t reserved = 0;

    bool isBlank() const { return ch == 0 && bg == 0 && (attrs & (AttrReverse | AttrUnderline | AttrStrike)) == 0 && link == 0; }
};

enum PromptMark : uint8_t {
    MarkPromptStart = 1 << 0,     // OSC 133;A
    MarkCommandStart = 1 << 1,    // OSC 133;B (end of prompt, user input starts)
    MarkOutputStart = 1 << 2,     // OSC 133;C
    MarkCommandFinished = 1 << 3, // OSC 133;D[;exit]
    // Row roles, set by the host with the private OSC 7772 ("shell" / "agent"): a line the user
    // typed, sent to that destination. A role is not a colour — the view paints the row's band and
    // ink from the scheme it has *now*, so a theme switch recolours every such row, scrollback
    // included, which no SGR colour written into the grid can do.
    MarkUserShell = 1 << 4,
    MarkUserAgent = 1 << 5,
};

struct Line {
    std::vector<Cell> cells;
    // Grapheme clusters: at offset o, clusters[o] = n, clusters[o+1..o+n] = codepoints.
    std::u32string clusters;
    bool continuation = false; // soft-wrapped continuation of the previous line
    uint8_t marks = 0;         // PromptMark bits
    uint16_t wrapColumns = 0;  // terminal width when the line was stored (cores that reflow)
    // Viewport decorations filled by VtCore::updateFrame (columns, inclusive).
    int16_t selectionStart = -1;
    int16_t selectionEnd = -1;
    struct Highlight {
        uint16_t start; // inclusive
        uint16_t end;   // inclusive
        bool current;
    };
    std::vector<Highlight> highlights; // search matches

    void clear()
    {
        cells.clear();
        clusters.clear();
        continuation = false;
        marks = 0;
        wrapColumns = 0;
        selectionStart = selectionEnd = -1;
        highlights.clear();
    }
    int columns() const { return int(cells.size()); }
    // Text of one cell ("" for wide tails, " " for blanks).
    QString cellText(const Cell &c) const;
    // Codepoints of one cell into out (appends); returns count.
    int cellCodepoints(const Cell &c, std::u32string *out) const;
    void appendCluster(Cell *c, const char32_t *cps, int n);
    // Text of columns [from, to) with trailing spaces trimmed.
    QString text(int from = 0, int to = -1) const;
    // Text of columns [from, to) exactly as it was printed: the trailing-space trim of text()
    // is not applied, so a row that soft-wrapped at a space still ends in one. Columns the
    // terminal never wrote to are still dropped from the end. This is what a selection copies
    // for a row the next row wraps out of (#8SBD); everything else wants text().
    QString untrimmedText(int from = 0, int to = -1) const;
};

enum class CursorShape : uint8_t { Block, Underline, Bar };

struct CursorState {
    int row = 0;
    int col = 0;
    bool visible = true;
    bool blink = false;
    CursorShape shape = CursorShape::Block;
};

enum class MouseTracking : uint8_t { None, Click, Drag, Move };

// Engine-neutral keys (subset of what terminals encode specially).
enum class Key : uint16_t {
    None,
    Enter, Tab, Backspace, Escape,
    Up, Down, Left, Right,
    Insert, Delete, Home, End, PageUp, PageDown,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
    Kp0, Kp1, Kp2, Kp3, Kp4, Kp5, Kp6, Kp7, Kp8, Kp9,
    KpMultiply, KpPlus, KpComma, KpMinus, KpPeriod, KpDivide, KpEnter, KpEqual,
};

enum Modifier : uint8_t { ModNone = 0, ModShift = 1, ModAlt = 2, ModCtrl = 4, ModSuper = 8 };

enum class MouseButton : uint8_t { None = 0, Left = 1, Middle = 2, Right = 3, WheelUp = 4, WheelDown = 5, WheelLeft = 6, WheelRight = 7 };

struct KeyInput {
    Key key = Key::None;      // special key, or None for text keys
    char32_t codepoint = 0;   // unshifted codepoint of a text key ('a' for Shift+A), 0 if none
    QString text;             // text the key produces with modifiers applied (may be empty)
    uint8_t modifiers = ModNone;
    bool release = false;
    bool repeat = false;
};

struct MouseInput {
    enum class Action : uint8_t { Press, Release, Motion };
    Action action = Action::Press;
    MouseButton button = MouseButton::None;
    int row = 0;       // viewport cell
    int col = 0;
    float x = 0;       // pixels relative to the cell grid origin (for SGR-pixel mode)
    float y = 0;
    uint8_t modifiers = ModNone;
};

enum class SelectionUnit : uint8_t { Cell, Word, Line };

// What the view needs to paint one frame.
struct ViewportFrame {
    int rows = 0;
    int columns = 0;
    std::vector<Line> lines;    // one per viewport row
    std::vector<uint8_t> dirty; // one per viewport row
    bool full = true;           // every row changed (scroll, resize, colours)
    // The vertical shift this frame can be replayed as instead of repainted: rows
    // [scrollTop, scrollBottom) of the *previous* frame moved up by `scrolledBy` rows (negative
    // moves them down), and every row the shift could not carry over is named in `dirty`.
    // `scrolledBy == 0` means there is nothing to replay and the frame reads exactly as it did
    // before — `full`, then `dirty`. When it is set, `full` may still be true, because the
    // desktop's own view repaints a scroll wholesale and always did; a consumer that ignores
    // these three fields is therefore still correct, only more expensive (#3H5T).
    int scrolledBy = 0;
    int scrollTop = 0;
    int scrollBottom = 0;
    CursorState cursor;         // viewport coordinates; visible=false when off-viewport
    bool cursorInViewport = true;
    int historyRows = 0;        // scrollback lines above the active screen
    int viewportTop = 0;        // first visible row counted from the top of the scrollback
    bool altScreen = false;
};

} // namespace relay
