// SPDX-License-Identifier: GPL-3.0-or-later
// relay::TerminalView: QPainter renderer and input for a TerminalSession.
#pragma once

#include "ColorScheme.h"
#include "FoldLayer.h"
#include "KeyMapper.h"
#include "OutputLinks.h"
#include "core/CellTypes.h"

#include <QElapsedTimer>
#include <QFont>
#include <QRawFont>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>

class QLabel;
class QLineEdit;

namespace relay {

class TerminalSession;
class VtCore;

class TerminalView : public QWidget {
    Q_OBJECT
public:
    explicit TerminalView(TerminalSession *session, QWidget *parent = nullptr);
    ~TerminalView() override;

    TerminalSession *session() const { return m_session; }

    // ---- appearance
    void setTerminalFont(const QFont &font);
    // Extra pixels between rows and the border around the grid, so engine panes match the
    // Konsole profile's LineSpacing and TerminalMargin.
    void setLineSpacing(int pixels);
    void setPadding(int pixels);
    QFont terminalFont() const { return m_baseFont; }
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void setColorScheme(const ColorScheme &scheme);
    const ColorScheme &colorScheme() const { return m_scheme; }
    void setEmojiFontFamily(const QString &family);
    int cellWidth() const { return m_cw; }
    int cellHeight() const { return m_ch; }
    QSize sizeForGrid(int rows, int cols) const;
    int rows() const { return m_rows; }
    int columns() const { return m_cols; }

    // ---- scrolling (host API: PageUp/PageDown from a composer)
    void scrollLines(int lines); // negative = back in history
    void scrollPages(int pages);
    void scrollToTop();
    void scrollToBottom();
    void scrollToRow(int row); // row from the top of the scrollback
    // The same, in the visual rows a scroll bar sees: real rows with the rows of
    // every expanded fold spliced in. Identical to scrollToRow() while no fold
    // is open, which is what scrollPositionChanged() reports too.
    void scrollToVisualRow(int row);
    bool scrollToPrompt(int direction);
    bool viewportAtBottom() const; // showing the newest output rather than sitting back in history

    // ---- clipboard and selection
    void copySelection();                 // to the clipboard
    void pasteClipboard();
    void pasteSelection();                // X11 primary selection (clipboard elsewhere)
    void pasteText(const QString &text);  // honours bracketed paste
    QString selectedText() const;
    void selectAll();
    void clearSelection();

    // ---- search
    void showSearchBar();
    void hideSearchBar();
    int find(const QString &text, bool backwards); // returns match count

    // ---- behaviour
    // Return true to let the host handle a key (the view ignores it).
    using ShortcutFilter = std::function<bool(const QKeyEvent *)>;
    void setShortcutFilter(ShortcutFilter filter) { m_shortcutFilter = std::move(filter); }
    void setCopyOnSelect(bool on) { m_copyOnSelect = on; }
    void setScrollToBottomOnKeystroke(bool on) { m_scrollOnKey = on; }
    void setVisualBell(bool on) { m_visualBell = on; }
    void setCursorBlink(bool on);
    // An unfocused view draws the cursor as a hollow outline, the way Konsole does. Relay's panes
    // never take focus (the prompt box owns the keys), so that outline would sit on the prompt
    // forever; they turn it off and the cursor appears only once the view really has the keyboard.
    void setUnfocusedCursorVisible(bool on);
    void setClipboardWriteAllowed(bool allowed); // OSC 52 (off by default)
    void setBuiltinShortcuts(bool on) { m_builtinShortcuts = on; }
    void setBuiltinContextMenu(bool on) { m_builtinContextMenu = on; }
    void setKeyMapperOptions(const KeyMapperOptions &o) { m_keyOptions = o; }

    // ---- links in the output (issues YZTK and GWXM)
    //
    // A link the view found: an OSC 8 hyperlink, a URL, a file or folder that exists, or a
    // `#K7Q2` reference to a Switchboard card the host's board knows.
    // The recognition and resolution rules live in src/OutputLinks.* and are tested there.
    struct Link {
        QString target;         // an absolute path, the URL as written, or relay://card/<id>
        QString text;           // the output text it was found as
        QString card;           // a card reference: its id, upper-cased; empty for every other link
        QString cardTitle;      // what the host's board calls that card, when it knows a title
        bool url = false;       // open in a browser rather than a Relay pane
        bool directory = false; // a folder: the explorer pane, not the preview
        int line = -1;
        int column = -1;
        bool valid() const { return !target.isEmpty(); }
    };

    // `#K7Q2` in the output is a card link only when the host's Switchboard index knows the id
    // (Switchboard design section 5); with no lookup set — a pane that has seen no board, or the
    // engine on its own — card references stay plain text.
    void setCardLookup(relay::links::CardLookup lookup);

    // The link under a point in the widget, if any (the context menu and the host use it).
    Link linkAtPoint(const QPoint &pos);
    // A plain left click opens a link; Ctrl+click always does. Hosts that use the first
    // click of an inactive pane to move the focus disarm it until the pane is active.
    void setPlainClickOpensLinks(bool on) { m_plainClickOpens = on; }

    // Step through every link in the scrollback and on the screen: -1 towards older
    // output, +1 towards newer, 0 re-reads the current one. The link is scrolled into
    // view, underlined and selected. Returns false when the output holds no link.
    bool stepLink(int delta, Link *link);
    void endLinkWalk();   // Esc, or any other input: drop the highlight
    bool linkWalkActive() const { return m_linkCursor.active(); }
    int linkWalkIndex() const { return m_linkCursor.index(); }
    int linkWalkCount() const { return m_linkCursor.count(); }

    // ---- folds: the detail of an agent tool call, unfolded inside the grid (#TK9C)
    //
    // Relay prints each tool call as one concise line wrapped in an OSC 8
    // hyperlink whose URI starts with the fold prefix ("relay://call/"). A
    // click on such a line unfolds its detail **in place, underneath it**, as a
    // block of virtual rows the view lays between the real ones; a second click
    // folds it away. The rows are real rows of the scroll bar's range and of
    // every scrolling gesture, they are selected, copied and searched in visual
    // order, and they survive a resize (the anchor is found again in the
    // reflowed scrollback) and disappear with their anchor when the scrollback
    // is trimmed. See docs/ENGINE.md, "Folds".
    //
    // OSC 8 URIs starting with this are fold anchors; empty turns folds off.
    void setFoldPrefix(const QString &uriPrefix);
    QString foldPrefix() const { return m_folds.prefix(); }
    // How far a fold block is indented, in cells (2..4, default 3).
    void setFoldIndent(int cells);
    // An anchor was clicked (or toggled with the keyboard) and the view has no
    // content for it: the host fetches the detail and calls setFoldContent().
    std::function<void(const QString &uri)> onFoldRequested;
    // Set a fold's content and expand it. Spans carry their own colours, so a
    // coloured diff is the host's to build.
    void setFoldContent(const QString &uri, const QVector<FoldLine> &lines);
    void setFoldExpanded(const QString &uri, bool expanded);
    bool foldExpanded(const QString &uri) const;
    void removeFold(const QString &uri);
    void clearFolds();
    QStringList expandedFolds() const; // visual order, oldest first
    // Toggle by URI: with content the view opens or shuts it itself, without it
    // asks the host through onFoldRequested. False when there is no such anchor.
    bool toggleFold(const QString &uri);
    // Toggle the anchor under a point in this view's coordinates.
    bool toggleFoldAt(const QPoint &pos);
    // Toggle the nearest anchor at or above the cursor row.
    bool toggleNearestFold();
    // The rows on screen in visual order: real rows and the rows of every open
    // fold interleaved exactly as they are painted, fold rows with their
    // indent. screenText() and scrollbackText() stay real rows only.
    QStringList visibleRowsText() const;

    // Resolve a Ctrl+click token to an absolute path (relative to the shell's
    // current directory) if it exists. Exposed for tests.
    static bool splitPathToken(const QString &token, QString *path, int *line, int *column);

    // The OSC 8 link, URL or existing path under a point in this view's coordinates, or an empty
    // string when there is none. `line` and `column` are set to -1 when the token carries none.
    // The host's right-click menu uses it for Open link / Copy link address / Open this file.

    QString debugDump();
    quint64 paintCount() const { return m_paints; }
    // The frame this view last pulled. Anything else that needs screen state — remote sharing,
    // for one — reads this instead of calling VtCore::updateFrame, which consumes the dirty
    // state and therefore tolerates exactly one consumer per session.
    const ViewportFrame &frame() const { return m_frame; }
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

signals:
    // OSC 8 URI, URL text, or an existing absolute path (line/column -1 if absent).
    void linkActivated(const QString &target, int line, int column);
    void scrollPositionChanged(int viewportTop, int historyRows, int rows);
    void gridSizeChanged(int rows, int columns);
    // A new frame() is available. Emitted after the view has taken it, before it repaints.
    void frameChanged();
    void bellRang();
    void dumpRequested();

protected:
    bool event(QEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void inputMethodEvent(QInputMethodEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    bool focusNextPrevChild(bool) override { return false; }

private:
    friend class TerminalAccessible;
    struct CellPos {
        int row;
        int col;
    };

    void updateMetrics();
    void applyGeometry();
    void scheduleGeometry();
    void scheduleFrame();
    void pullFrame();
    QRect cellRect(int row, int col, int width = 1) const;
    CellPos cellAt(const QPoint &p, bool clamp = true) const;
    QColor resolve(uint32_t packed, bool foreground) const;
    void paintRow(QPainter &p, int screenRow, const Line &line, int realRow);
    void paintFoldRow(QPainter &p, int screenRow, int foldIndex, int foldRow);
    void paintCursor(QPainter &p);
    QColor groundAt(int y) const;
    QColor foldBackground() const;
    QColor foldRule() const;
    // Folds are laid out and hit-tested only on the primary screen: vim and
    // less own the grid while they run, and the folds come back on return.
    bool foldsVisible() const { return m_folds.active() && !m_frame.altScreen; }

    // ---- folds
    // What sits on a screen row: a real row of the frame, or a fold's row.
    FoldLayer::VisualRow visualAt(int screenRow) const;
    // The frame line index a screen row shows, or -1 when it shows a fold row
    // or nothing. Identical to `screenRow` while no fold is open.
    int frameRowOf(int screenRow) const;
    // The same, but a screen row inside a fold answers with the nearest real
    // row above it, so a gesture that crosses a fold still has somewhere to go.
    int frameRowClamped(int screenRow) const;
    // The screen row a real (absolute) row is painted on; outside [0, rows).
    int screenRowOfReal(int realRow) const;
    int realRows() const { return m_frame.historyRows + m_frame.rows; }
    int visualTotal() const { return m_folds.visualTotal(realRows()); }
    int maxVisualTop() const { return std::max(0, visualTotal() - m_rows); }
    // Ask the core for every anchor row again (after a resize, a trim, a clear
    // or a new fold) and drop the folds whose anchors have left the scrollback.
    void resolveFoldAnchors();
    void invalidateFoldAnchors();
    // Put the core's viewport where the visual window needs it and clamp
    // m_visualTop. Called with the session lock held, right after updateFrame.
    void syncFoldViewport(VtCore &core, bool *frameChanged);
    void setVisualTop(int top);
    // The fold anchor under a screen cell (its URI), or empty.
    QString foldAnchorAt(const CellPos &c) const;

    // ---- selection across the boundary between real rows and fold rows
    //
    // The cores own the selection over real rows ("the core keeps it attached
    // to content") and they know nothing about fold rows, so when a fold is on
    // screen the view owns a selection in visual coordinates and delegates its
    // real-row part to the core: the core paints and yields the real text, the
    // view paints and yields the fold text, and copySelection() puts them
    // together in the order they are displayed. With no fold on screen none of
    // this runs and the core owns the selection outright, as before.
    struct FoldSelPos {
        bool fold = false;
        int realRow = 0;      // absolute scrollback row (fold == false)
        QString foldUri;      // fold == true
        int foldRow = 0;      // wrapped row inside the fold
        int col = 0;          // grid column
    };
    bool foldSelectionActive() const { return m_visualSelection; }
    FoldSelPos selPosAt(const CellPos &c) const;
    int visualRowOf(const FoldSelPos &p) const;
    bool selPosLess(const FoldSelPos &a, const FoldSelPos &b) const;
    void beginVisualSelection(const CellPos &c, SelectionUnit unit);
    void extendVisualSelection(const CellPos &c);
    void applyVisualSelection();   // mirror the real-row part onto the core
    void clearVisualSelection();
    // The selected grid columns of one fold row, or false when it has none.
    bool foldSelectionRange(int foldIndex, int foldRow, int *from, int *to) const;
    QString visualSelectedText() const;
    bool foldWordRange(const FoldSelPos &p, int *from, int *to) const;
    // A FoldSpan link under a screen cell, or an empty string.
    QString foldLinkAt(const CellPos &c, int *startCol, int *endCol) const;
    quint32 glyphFor(int variant, char32_t cp);
    bool handleBuiltinShortcut(QKeyEvent *e);
    void sendKey(const KeyInput &k);
    void afterUserInput();
    void updateHover(const QPoint &pos, Qt::KeyboardModifiers mods);
    bool linkAt(const CellPos &c, Link *link, int *startCol, int *endCol);
    // Every link in the scrollback and on the screen, oldest first, in absolute rows
    // (0 = the oldest scrollback line, the same coordinates as scrollToRow()).
    struct WalkLink {
        int row = 0;
        int col = 0;
        int endRow = 0;
        int endCol = 0;
        Link link;
    };
    void collectLinks();
    void showWalkLink(const WalkLink &walk);
    QString currentDirectory() const;
    bool mouseToProgram(Qt::KeyboardModifiers mods) const;
    void sendMouse(QMouseEvent *e, int action);
    void autoScrollTick();
    void updateSearchLabel(int count, int index);

    TerminalSession *m_session;
    ColorScheme m_scheme;
    ViewportFrame m_frame;
    bool m_forceFull = true;

    QFont m_baseFont;
    int m_zoom = 0;
    QFont m_fonts[4]; // regular, bold, italic, bold italic
    QRawFont m_raw[4];
    QFont m_emojiFont;
    std::unordered_map<uint64_t, quint32> m_glyphCache;
    int m_cw = 8;
    int m_ch = 16;
    int m_ascent = 12;
    int m_descent = 4;
    int m_padding = 2;
    int m_lineSpacing = 0;
    int m_rows = 24;
    int m_cols = 80;

    QTimer m_frameTimer;
    QTimer m_geometryTimer;   // the grid follows the size the view still has once the layout settles
    QElapsedTimer m_sinceFrame;
    quint64 m_bytesAtFrame = 0;
    quint64 m_paints = 0;

    int m_lastTop = -1;
    int m_lastHistory = -1;

    // Folds. m_visualTop is the authority while a fold is open: the core's own
    // viewport is then driven to cover the real rows the visual window needs.
    FoldLayer m_folds;
    int m_visualTop = 0;
    int m_paintedVisualTop = 0;
    bool m_followBottom = true;  // the view sits at the newest output
    bool m_foldAnchorsDirty = false;
    QElapsedTimer m_foldResolveAt;
    bool m_visualSelection = false;
    bool m_visualGesture = false;
    SelectionUnit m_visualSelUnit = SelectionUnit::Cell;
    FoldSelPos m_selAnchor;
    FoldSelPos m_selExtent;
    FoldSelPos m_selStart;
    FoldSelPos m_selEnd;

    bool m_focused = false;
    bool m_unfocusedCursor = true;
    bool m_blinkEnabled = true;
    bool m_blinkOn = true;
    QTimer m_blinkTimer;
    CursorState m_paintedCursor;
    bool m_paintedCursorInViewport = false;

    // Selection gesture
    bool m_selecting = false;
    bool m_selectionMoved = false;
    int m_clickCount = 0;
    QElapsedTimer m_lastClick;
    QPoint m_lastClickPos;
    QPoint m_lastMousePos;
    QTimer m_autoScroll;
    int m_mouseButtonsToProgram = 0;

    // Link hover
    int m_hoverRow = -1;
    int m_hoverStart = -1;
    int m_hoverEnd = -1;
    int m_hoverCellRow = -2;   // the cell the hover was computed for, so a move inside
    int m_hoverCellCol = -2;   // one cell costs nothing
    bool m_plainClickOpens = true;
    Link m_pressedLink;        // the link a plain left press landed on
    QString m_pressedFold;     // the fold anchor a plain left press landed on
    int m_pressedRow = -1;
    int m_pressedStart = -1;
    int m_pressedEnd = -1;

    // Keyboard walk over the links (Ctrl+Shift+L)
    std::vector<WalkLink> m_linkWalk;
    relay::links::Cursor m_linkCursor;
    relay::links::CardLookup m_cardLookup;

    QString m_preedit;
    bool m_flash = false;

    ShortcutFilter m_shortcutFilter;
    KeyMapperOptions m_keyOptions;
    bool m_copyOnSelect = true;
    bool m_scrollOnKey = true;
    bool m_visualBell = true;
    bool m_builtinShortcuts = true;
    bool m_builtinContextMenu = true;
    int m_wheelRemainder = 0;

    QWidget *m_searchBar = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_searchLabel = nullptr;
};

} // namespace relay
