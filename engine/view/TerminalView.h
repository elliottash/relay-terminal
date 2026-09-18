// SPDX-License-Identifier: GPL-3.0-or-later
// relay::TerminalView: QPainter renderer and input for a TerminalSession.
#pragma once

#include "ColorScheme.h"
#include "KeyMapper.h"
#include "core/CellTypes.h"

#include <QElapsedTimer>
#include <QFont>
#include <QRawFont>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <unordered_map>

class QLabel;
class QLineEdit;

namespace relay {

class TerminalSession;

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
    bool scrollToPrompt(int direction);

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

    // Resolve a Ctrl+click token to an absolute path (relative to the shell's
    // current directory) if it exists. Exposed for tests.
    static bool splitPathToken(const QString &token, QString *path, int *line, int *column);

    // The OSC 8 link, URL or existing path under a point in this view's coordinates, or an empty
    // string when there is none. `line` and `column` are set to -1 when the token carries none.
    // The host's right-click menu uses it for Open link / Copy link address / Open this file.
    QString linkAtPoint(const QPoint &pos, int *line = nullptr, int *column = nullptr);

    QString debugDump();
    quint64 paintCount() const { return m_paints; }
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

signals:
    // OSC 8 URI, URL text, or an existing absolute path (line/column -1 if absent).
    void linkActivated(const QString &target, int line, int column);
    void scrollPositionChanged(int viewportTop, int historyRows, int rows);
    void gridSizeChanged(int rows, int columns);
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
    void paintRow(QPainter &p, int row);
    void paintCursor(QPainter &p);
    quint32 glyphFor(int variant, char32_t cp);
    bool handleBuiltinShortcut(QKeyEvent *e);
    void sendKey(const KeyInput &k);
    void afterUserInput();
    void updateHover(const QPoint &pos, Qt::KeyboardModifiers mods);
    bool linkAt(const CellPos &c, QString *target, int *line, int *col, int *startCol, int *endCol);
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
