// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::TerminalView: QPainter renderer and input for a TerminalSession.
#pragma once

#include "ColorScheme.h"
#include "FoldLayer.h"
#include "FoldSearch.h"
#include "ImageCache.h"
#include "KeyMapper.h"
#include "OutputLinks.h"
#include "core/CellTypes.h"
#include "core/InlineImage.h"
#include "core/InlineMedia.h"

#include <QElapsedTimer>
#include <QFont>
#include <QHash>
#include <QPointer>
#include <QRawFont>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>

class QLabel;
class QLineEdit;
class QProcess;
class QMediaPlayer;
class QAudioOutput;
class QMovie;

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
    //
    // The find covers the real rows *and* the text of every open fold, as one
    // sequence in the order the rows are painted: the cores search their own
    // rows, the view searches the fold layer's, and the two are merged in
    // visual order (see view/FoldSearch.h). The host is told a count and an
    // index and never has to know that folds exist.
    void showSearchBar();
    void hideSearchBar();
    int find(const QString &text, bool backwards); // returns match count
    // Matches of the current needle: the core's, plus the ones inside open folds.
    int searchMatchCount() const;
    // The core's matches on rows a taken-over prose block hides; the count
    // subtracts them, because the fold counts that text itself (#RW9T).
    int hiddenCoreMatches() const;
    // The selected match, counted from the newest (0), or -1 when there is none.
    int searchIndex() const { return m_searchIndex; }
    // One match towards older content (backwards) or newer, wrapping. Returns
    // the count; *index, when given, gets the selected match's index.
    int searchStep(bool backwards, int *index = nullptr);

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
    // `#K7Q2` reference to a Board card the host's board knows.
    // The recognition and resolution rules live in src/OutputLinks.* and are tested there.
    // A wrapped screen row as one logical line: the text, for every UTF-16 unit
    // of it the screen cell the unit was printed in, and the first screen row of
    // the wrap (#9MYY). logicalRowAt() fills it; the file-local helpers in
    // TerminalView.cpp work on it, so it is public.
    struct LogicalRow {
        QString text;
        std::vector<std::pair<int, int>> cellOf;
        int firstRow = -1;
    };
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

    // `#K7Q2` in the output is a card link only when the host's Board index knows the id
    // (Board design section 5); with no lookup set — a pane that has seen no board, or the
    // engine on its own — card references stay plain text.
    void setCardLookup(relay::links::CardLookup lookup);

    // Which paths in the output exist, and where a relative one is relative to (card #S5SH).
    // Without one the view asks this machine (`links::systemProbe()`); a pane logged into
    // another host answers from what it has asked that host. `directory` overrides the pane's
    // own working directory when it returns something, which is how a remote `./build.log`
    // resolves against the remote folder. Neither may block: this is called from a mouse-move.
    void setLinkProbe(relay::links::Probe probe, std::function<QString()> directory = {});
    // The answers the host gave have changed (a batch came back): re-read the output.
    void linkProbeUpdated();

    // The link under a point in the widget, if any (the context menu and the host use it).
    Link linkAtPoint(const QPoint &pos);
    // A plain left click opens a link; Ctrl+click always does. Hosts that use the first
    // click of an inactive pane to move the focus disarm it until the pane is active.
    void setPlainClickOpensLinks(bool on) { m_plainClickOpens = on; }

    // ---- inline images (card #1MGS, core/InlineImage.h)
    // The file of the picture painted under a point, or empty (also for a picture whose file is
    // gone). A plain left click there opens it full size, as does Ctrl+click.
    QString imagePathAt(const QPoint &pos);
    // What opening a picture does; by default QDesktopServices opens the file. Tests replace it.
    void setImageOpener(std::function<void(const QString &path)> open) { m_imageOpener = std::move(open); }
    ImageCache &imageCache() { return m_images; }
    // Every path, URL and card reference that resolves wears ColorScheme::link at rest, not only
    // under the pointer (owner, 2026-09-19: "clickable things need to be understood from colors").
    // Only a cell whose foreground is plain — the default, or an achromatic colour: white, bright
    // white, a grey, the host's muted ink — is recoloured. A chromatic colour a program chose (`git
    // status` red, `ls` blue) already says something and is left alone. Never on the alternate
    // screen, which a full-screen program owns. On by default.
    void setLinksColouredAtRest(bool on);
    bool linksColouredAtRest() const { return m_linksAtRest; }

    // Step through every link in the scrollback and on the screen: -1 towards older
    // output, +1 towards newer, 0 re-reads the current one. The link is scrolled into
    // view, underlined and selected. Returns false when the output holds no link.
    bool stepLink(int delta, Link *link);
    void endLinkWalk();   // Esc, or any other input: drop the highlight
    bool linkWalkActive() const { return m_linkCursor.active(); }
    int linkWalkIndex() const { return m_linkCursor.index(); }
    int linkWalkCount() const { return m_linkCursor.count(); }

    // The same walk over the lines a host anchored itself (card #XPEB): every OSC 8 run whose URI
    // starts with one of `prefixes` — a Relay pane's tool-call, reasoning and "✦ N tool calls"
    // lines — one stop per URI, oldest first. The first step lands on the newest; -1 goes older,
    // +1 newer, 0 re-reads (and re-highlights) the current one. The line is scrolled into view,
    // underlined and selected, and `stop` gets its URI and its text. What a stop *does* is the
    // host's: toggleFold() for a fold anchor, its own link routing for anything else. False when
    // the output holds no such line.
    struct AnchorStop {
        QString uri;
        QString text;
    };
    bool stepAnchor(const QStringList &prefixes, int delta, AnchorStop *stop);
    void endAnchorWalk();
    bool anchorWalkActive() const { return m_anchorCursor.active(); }
    int anchorWalkIndex() const { return m_anchorCursor.index(); }
    int anchorWalkCount() const { return m_anchorCursor.count(); }
    // Every stop's URI, oldest first, while a walk runs (a host acting on several at once).
    QStringList anchorWalkUris() const;

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
    // A block of the host's own prose (#R2WQ): printed into the grid inside an
    // OSC 8 run with `uri` (kProsePrefix), re-wrapped from `lines` whenever the
    // grid is not at printColumns, and never interactive. See TerminalBackend.h.
    void setProseBlock(const QString &uri, const QVector<FoldLine> &lines, int printColumns);
    // Every prose block the view holds, as handed to setProseBlock(): what the
    // host saves beside its scrollback rows (#MTCS).
    QVector<ProseBlock> proseBlocks() const;
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
    // Rows painted, and filesystem resolutions of the pane's directory: both
    // are counters the performance tests assert on (#6W0Z).
    quint64 rowPaintCount() const { return m_rowPaints; }
    quint64 directoryResolveCount() const { return m_cwdResolves; }
    // The frame this view last pulled. Anything else that needs screen state — remote sharing,
    // for one — reads this instead of calling VtCore::updateFrame, which consumes the dirty
    // state and therefore tolerates exactly one consumer per session.
    const ViewportFrame &frame() const {
        return int(m_frame.lines.size()) > m_frame.rows ? m_baseFrame : m_frame;
    }
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

signals:
    // OSC 8 URI, URL text, or an existing absolute path (line/column -1 if absent).
    void linkActivated(const QString &target, int line, int column, Qt::KeyboardModifiers modifiers);
    // An Alt+drag finished with a non-empty selection (card #7BYT): the host adds it to its
    // prompt box. Plain and Shift drags only select; Ctrl+Alt+drag is the rectangle.
    void selectionActivated(const QString &text);
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
    // One re-wrapped row of a prose block (#R2WQ): painted like a grid row of
    // the host's own, not like a fold — no tint, no rule, column 0.
    void paintProseRow(QPainter &p, int screenRow, const FoldLayer::Fold &f, int foldRow);
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
    void keepFoldAnchorInPlace(int anchorRow, int screenRow);
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
    QString foldLinkAt(const CellPos &c, int *startCol, int *endCol, QVector<QRect> *segments = nullptr) const;
    quint32 glyphFor(int variant, char32_t cp);
    bool handleBuiltinShortcut(QKeyEvent *e);
    void sendKey(const KeyInput &k);
    void afterUserInput();
    void updateHover(const QPoint &pos, Qt::KeyboardModifiers mods);
    bool linkAt(const CellPos &c, Link *link, int *startCol, int *endCol, QVector<QRect> *segments = nullptr);
    // The OSC 8 URI behind a frame cell, by the link id the frame's cell
    // carries. Both cores intern one id per distinct URI when a frame is built
    // and never hand one id two URIs inside a frame (LibVtermCore's table is
    // append-only; GhosttyCore interns per pull), so the answer is memoised per
    // frame next to m_frameProse — hyperlinkAt() converts the whole row for
    // every hover probe otherwise (#9MYY).
    QString frameHyperlinkUri(uint32_t id, int frameRow, int col);
    bool linkHovered(int row, int col) const;
    // A markdown link's label (card #MDKN): its OSC 8 URI is the block's anchor with the target
    // the agent wrote as a fragment, and the target is resolved here through relay::links exactly
    // as a span of text is, so a label fills the same Link a scanned target does — the same kind,
    // the same line and column, the same card id — and opens by the same road. False when the
    // target resolves to nothing (a path that is not there), which leaves the label plain text.
    bool resolveLabelLink(const QString &uri, Link *link);
    // Every link in the scrollback and on the screen, oldest first, in absolute rows
    // (0 = the oldest scrollback line, the same coordinates as scrollToRow()).
    struct WalkLink {
        int row = 0;
        int col = 0;
        int endRow = 0;
        int endCol = 0;
        // A link inside a replacement block (#J4WK). The block hides the real
        // rows its text came from, so the link's place on screen is a fold row
        // and a fold column, and the selection that shows the walk is the
        // view's own, not the emulator's. One QRect per wrapped row the link
        // covers: x = first grid column, y = the fold's row index, width = its
        // columns. Empty for an ordinary link in the grid.
        QString foldUri;
        QVector<QRect> foldSpans;
        Link link;
    };
    void collectLinks();
    void collectAnchors(const QStringList &prefixes);
    // The links of one replacement block, scanned from the block's own logical
    // lines — the walk's half of what linkAt() does for the mouse (#J4WK).
    void collectFoldLinks(int foldIndex, const QString &cwd, const QString &home,
                          const relay::links::Probe &probe,
                          const relay::links::CardLookup &cardLookup);
    void showWalkLink(const WalkLink &walk);
    // The columns of one frame row that lie inside a link that resolves: *cols is sized to the
    // row and set where the link colour applies. Scans are cached per logical line and directory
    // (m_restLinks), so an unchanged screen costs no probe.
    void restLinkColumns(int frameRow, std::vector<char> *cols);
    std::vector<char> foldRestLinks(const std::vector<FoldLayer::Cell> &cells);
    // The directory the rows of *this frame* are scanned against, resolved once
    // per frame instead of once per painted row (#6W0Z).
    const QString &frameDirectory();
    // Whether an OSC 8 link id is a prose anchor, memoised for the frame.
    bool proseLink(uint32_t link, int frameRow, int col);

    // Inline images (#1MGS). One picture as the rows on screen show it: its top-left cell is
    // (top, col) in screen rows, which may be above the view, and its rows from firstRow to
    // lastRow are the ones found on screen.
    struct ImagePlacement {
        inlineimage::ImageRef ref;
        int col = 0;
        int top = 0;
        int firstRow = 0;
        int lastRow = 0;
    };
    // The image row a link id names, memoised for the frame; false for any other link.
    bool imageRefOf(uint32_t link, int frameRow, int col, inlineimage::ImageRef *ref);
    // The pictures with a row among screen rows [first, last], one entry per picture.
    void imagesOnRows(int first, int last, std::vector<ImagePlacement> *out);
    // Where a picture of `natural` pixels is painted: fitted into its cells, then narrowed to the
    // grid's right edge. Not clipped to the view.
    QRect imageRect(const ImagePlacement &image, QSize natural) const;
    void paintImages(QPainter &p, int firstRow, int lastRow);
    // The picture under a point; `missing` says its file could not be read (only the one-line
    // placeholder on its first row is then under the pointer).
    bool imageAt(const QPoint &pos, ImagePlacement *image, bool *missing);
    void openImage(const QString &path);
    // Media rows (#MDA7) use the same linked-cell placement as images.
    struct MediaPlacement {
        inlinemedia::MediaRef ref;
        int col = 0;
        int top = 0;
        int firstRow = 0;
        int lastRow = 0;
    };
    struct MediaInfo {
        QString kind;
        QString name;
        QString path;
        QString preview;
        QString url;
        QVector<qreal> waveform;
        qint64 durationMs = 0;
        int rows = 0;
        int columns = 0;
        QChar delimiter = QLatin1Char(',');
        QVector<QStringList> head;   // a table's first rows, header first, for its inline preview
        bool valid = false;
    };
    bool mediaRefOf(uint32_t link, int frameRow, int col, inlinemedia::MediaRef *ref);
    void mediaOnRows(int first, int last, std::vector<MediaPlacement> *out);
    MediaInfo mediaInfo(const QString &manifest);
    QRect mediaRect(const MediaPlacement &media) const;
    void paintMedia(QPainter &p, int firstRow, int lastRow);
    bool mediaAt(const QPoint &pos, MediaPlacement *media);
    void activateMedia(const MediaPlacement &media, const QPoint &pos);
    void openTable(const MediaInfo &info);
    void paintTablePreview(QPainter &p, const QRect &box, const MediaInfo &info, int lines);
    static constexpr int kTablePreviewRows = 15;   // data rows relay-show reserves at most
    void playAudio(const MediaInfo &info, qint64 fromMs);
    void stopAudio(bool preservePosition = false);
    qint64 audioPositionMs() const;
    void probeAudio(const QString &manifest, const QString &path);
    void renderMath(const QString &manifest);
    QString currentDirectory() const;
    bool mouseToProgram(Qt::KeyboardModifiers mods) const;
    void sendMouse(QMouseEvent *e, int action);
    void autoScrollTick();
    void updateSearchLabel(int count, int index);
    // The count on the search bar after the folds moved under a live search.
    void refreshSearchLabel();
    // Put a visual row on screen if it is not already, the way a core scrolls
    // its own match into view: half a screen above it.
    void ensureVisualRowVisible(int visualRow);

    TerminalSession *m_session;
    ColorScheme m_scheme;
    ViewportFrame m_frame;
    // Remote screen consumers keep the core's ordinary grid. Only the local
    // folded view may extend m_frame to cover compressed prose (#B7SP).
    ViewportFrame m_baseFrame;
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
    quint64 m_paints = 0;
    quint64 m_rowPaints = 0;

    int m_lastTop = -1;
    int m_lastHistory = -1;

    // Folds. m_visualTop is the authority while a fold is open: the core's own
    // viewport is then driven to cover the real rows the visual window needs.
    FoldLayer m_folds;
    // The find's fold half and the merge with the core's matches.
    FoldSearch m_foldSearch;
    int m_searchIndex = -1;
    int m_visualTop = 0;
    int m_paintedVisualTop = 0;
    // The visual window moved in this frame, so every screen row shows
    // something else and the dirty-row region cannot describe it.
    bool m_visualTopMoved = false;
    bool m_followBottom = true;  // the view sits at the newest output
    bool m_foldAnchorsDirty = false;
    // Content has moved under the anchors since the last resolve; nothing can
    // have moved without it, so the heartbeat has nothing to find (#PPR4).
    bool m_contentMoved = true;
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

    // Link hover: rectangles use screen cells, one per visible row segment.
    QVector<QRect> m_hoverSegments;
    int m_hoverRow = -1;
    int m_hoverStart = -1;
    int m_hoverEnd = -1;
    int m_hoverCellRow = -2;   // the cell the hover was computed for, so a move inside
    int m_hoverCellCol = -2;   // one cell costs nothing
    bool m_plainClickOpens = true;
    // Links at rest: the cache maps "<directory>\n<logical line>" to the [start, end] character
    // spans links::scan found. Dropped when the probe's answers change, after a few seconds (a file
    // the output named may have been created since), and when it grows past a bound.
    bool m_linksAtRest = true;
    QHash<QString, QVector<QPair<int, int>>> m_restLinks;
    QElapsedTimer m_restLinksAge;
    // Resolved once per frame and thrown away with it (#6W0Z): the pane's
    // directory, and which link ids are prose anchors. Both cost a syscall or
    // the core's mutex, and neither can change inside one frame.
    QString m_frameCwd;
    bool m_frameCwdValid = false;
    // Bumped by every pullFrame() that changed the screen (#9MYY). Anything
    // derived from the frame alone — the hover caches, the accessible text —
    // keys its one entry on this instead of diffing the screen.
    quint64 m_frameVersion = 0;
    // The hover's one-entry per-frame cache (#9MYY): the logical line the
    // pointer last crossed and its link scan. Sweeping the pointer along a link
    // used to rebuild the wrapped line and re-run links::scan for every cell;
    // now it is one build and one scan per frame. Dropped on a frame version
    // change and when the link probe or the card lookup changes.
    struct {
        quint64 version = 0;                     // the frame the entry was built for
        int firstRow = -1;                       // the logical line's first screen row
        int mode = -1;                           // links::Mode the scan ran in
        LogicalRow logical;
        std::vector<int> idxOfCell;              // per UTF-16 unit: which Found covers it, -1 if none
        QVector<links::Found> found;             // the scan's answers for that line
    } m_hover;
    // The frame's link ids answered once, cleared with the frame (see
    // frameHyperlinkUri).
    std::vector<std::pair<uint32_t, QString>> m_hoverUris;
    std::vector<std::pair<uint32_t, bool>> m_frameProse;
    // Inline images (#1MGS): which link ids of this frame are image rows (thrown away with the
    // frame, like m_frameProse), the parsed URIs across frames, and the decoded pictures.
    struct FrameImageLink {
        bool image = false;
        inlineimage::ImageRef ref;
    };
    std::unordered_map<uint32_t, FrameImageLink> m_frameImages;
    QHash<QString, inlineimage::ImageRef> m_imageUris;   // an empty path: not a well-formed image URI
    std::vector<ImagePlacement> m_imagePlacements;
    ImageCache m_images;
    QHash<QString, QMovie *> m_animatedImages;
    std::unordered_map<uint32_t, inlinemedia::MediaRef> m_frameMedia;
    QHash<QString, MediaInfo> m_mediaInfo;
    QSet<QString> m_audioProbes;
    QSet<QString> m_mathRenders;
    std::vector<MediaPlacement> m_mediaPlacements;
    QProcess *m_audioProcess = nullptr;
    QMediaPlayer *m_qtPlayer = nullptr;  // Qt 6 Multimedia, when built
    QAudioOutput *m_qtOutput = nullptr;
    bool m_qtAudioFailed = false;
    QTimer m_audioTimer;
    QString m_audioPath;
    qint64 m_audioPositionMs = 0;
    qint64 m_audioDurationMs = 0;
    QElapsedTimer m_audioClock;
    std::function<void(const QString &)> m_imageOpener;
    QString m_pressedImage;    // the picture a plain left press landed on
    bool m_hoverImage = false; // the pointer is over a picture (pointing hand)
    // How many times the directory was really resolved (tests: it is once per
    // frame, whatever the row count).
    mutable quint64 m_cwdResolves = 0;
    Link m_pressedLink;        // the link a plain left press landed on
    QString m_pressedFold;     // the fold anchor a plain left press landed on
    int m_pressedRow = -1;
    int m_pressedStart = -1;
    int m_pressedEnd = -1;

    // Keyboard walk over the links (Ctrl+Shift+L)
    std::vector<WalkLink> m_linkWalk;
    relay::links::Cursor m_linkCursor;
    // Keyboard walk over the host's anchored lines (Ctrl+J in Relay, card #XPEB)
    std::vector<WalkLink> m_anchorWalk;   // link.target holds the URI, link.text the line
    relay::links::Cursor m_anchorCursor;
    relay::links::CardLookup m_cardLookup;
    relay::links::Probe m_linkProbe;                  // #S5SH: the host's filesystem, not this one
    std::function<QString()> m_linkDirectory;

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
    int m_zoomWheelRemainder = 0;

    QWidget *m_searchBar = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_searchLabel = nullptr;
};

// The viewport as one accessibility text (#9MYY): every row's text joined with
// '\n', and for each row the offset of its first character in that join. The
// screen-reader queries answered this per call before — a full join for
// text()/characterCount(), then a split of it for characterRect() and
// offsetAtPoint() — and now read this one table instead. A free function over
// the frame so the join and the offsets are testable directly, without faking
// QAccessible::isActive() to get at them through the interface.
struct AccessibleText {
    QString text;                // the rows joined with '\n'
    std::vector<int> lineStart;  // per row: the offset of the row's first character
    int rowOf(int offset) const;    // binary search: the row an offset falls in
    int rowLength(int row) const;   // the characters of a row in text
};
AccessibleText accessibleText(const ViewportFrame &frame);

} // namespace relay
