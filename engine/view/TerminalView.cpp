// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TerminalView.h"

#include "BoxDrawing.h"
#include "FaintInk.h"
#include "LabelLinks.h"
#include "session/TerminalSession.h"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QHBoxLayout>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>

namespace relay {

namespace {

// Frame pacing (scheduleFrame). A frame that follows another within
// kStreamingGapMs is part of a stream and is held to one display frame; the
// first one after a quieter moment goes out in kEchoFrameMs, which is what
// keystroke echo rides on. The gap is longer than the streaming interval, so a
// steady stream stays a stream.
constexpr int kEchoFrameMs = 4;
constexpr int kStreamingFrameMs = 16;
constexpr int kStreamingGapMs = 50;

bool isRegionalIndicator(char32_t c)
{
    return c >= 0x1F1E6 && c <= 0x1F1FF;
}

bool wantsEmojiFont(const std::u32string &cps, int width)
{
    if (cps.empty())
        return false;
    const char32_t base = cps[0];
    for (char32_t c : cps) {
        if (c == 0xFE0F || isRegionalIndicator(c))
            return true; // emoji presentation selector (also keycaps like #️⃣), flags
        if ((c == 0x200D || (c >= 0x1F3FB && c <= 0x1F3FF)) && base >= 0x2190)
            return true; // ZWJ sequences and skin tones on pictographs
    }
    return width == 2 && ((base >= 0x1F000 && base <= 0x1FAFF) || (base >= 0x2600 && base <= 0x27BF) || (base >= 0x2B00 && base <= 0x2BFF));
}

QString ucs4ToString(const std::u32string &cps)
{
    return QString::fromUcs4(cps.data(), int(cps.size()));
}

// A colour `amount` of the way from `from` to `to` (both opaque).
QColor mix(const QColor &from, const QColor &to, double amount)
{
    return QColor(int(from.red() + (to.red() - from.red()) * amount),
                  int(from.green() + (to.green() - from.green()) * amount),
                  int(from.blue() + (to.blue() - from.blue()) * amount));
}

QString defaultEmojiFamily()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral("Apple Color Emoji");
#elif defined(Q_OS_WIN)
    return QStringLiteral("Segoe UI Emoji");
#else
    return QStringLiteral("Noto Color Emoji");
#endif
}

// A highlight worth putting on PRIMARY when the button is released: at least three letters or
// digits (#C9VT, the owner's rule), so an accidental one- or two-character drag does not take
// the selection buffer. The same rule is `relay::copyOnSelectWorthCopying` in
// src/CopyOnSelect.h, which every read-only pane surface uses; the engine cannot include a
// header from src/, so it is written out here and the two must stay in step.
bool copyOnSelectWorthCopying(const QString &text)
{
    int worthwhile = 0;
    for (const QChar &ch : text) {
        if (ch.isLetterOrNumber() && ++worthwhile >= 3)
            return true;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------- accessibility

class TerminalAccessible : public QAccessibleWidget, public QAccessibleTextInterface {
public:
    explicit TerminalAccessible(TerminalView *view)
        : QAccessibleWidget(view, QAccessible::Terminal)
    {
    }

    void *interface_cast(QAccessible::InterfaceType t) override
    {
        if (t == QAccessible::TextInterface)
            return static_cast<QAccessibleTextInterface *>(this);
        return QAccessibleWidget::interface_cast(t);
    }

    QString text(QAccessible::Text t) const override
    {
        if (t == QAccessible::Value)
            return allText();
        return QAccessibleWidget::text(t);
    }

    // QAccessibleTextInterface over the visible viewport.
    void selection(int, int *start, int *end) const override { *start = *end = 0; }
    int selectionCount() const override { return 0; }
    void addSelection(int, int) override {}
    void removeSelection(int) override {}
    void setSelection(int, int, int) override {}
    int cursorPosition() const override
    {
        const TerminalView *v = view();
        const ViewportFrame &f = v->m_frame;
        if (!f.cursorInViewport)
            return 0;
        int offset = 0;
        for (int r = 0; r < f.cursor.row && r < int(f.lines.size()); ++r)
            offset += f.lines[size_t(r)].text().size() + 1;
        return offset + f.cursor.col;
    }
    void setCursorPosition(int) override {}
    QString text(int startOffset, int endOffset) const override { return allText().mid(startOffset, endOffset - startOffset); }
    int characterCount() const override { return allText().size(); }
    QRect characterRect(int offset) const override
    {
        const TerminalView *v = view();
        const QStringList lines = allText().split(QLatin1Char('\n'));
        int row = 0;
        while (row < lines.size() && offset > lines[row].size()) {
            offset -= lines[row].size() + 1;
            ++row;
        }
        const QRect local = v->cellRect(row, offset);
        return QRect(v->mapToGlobal(local.topLeft()), local.size());
    }
    int offsetAtPoint(const QPoint &point) const override
    {
        const TerminalView *v = view();
        const TerminalView::CellPos c = v->cellAt(v->mapFromGlobal(point));
        const QStringList lines = allText().split(QLatin1Char('\n'));
        int offset = 0;
        for (int r = 0; r < c.row && r < lines.size(); ++r)
            offset += lines[r].size() + 1;
        return offset + std::min(c.col, int(lines.value(c.row).size()));
    }
    void scrollToSubstring(int, int) override {}
    QString attributes(int offset, int *startOffset, int *endOffset) const override
    {
        *startOffset = offset;
        *endOffset = offset + 1;
        return QString();
    }

private:
    TerminalView *view() const { return static_cast<TerminalView *>(widget()); }
    QString allText() const
    {
        QStringList out;
        for (const Line &l : view()->m_frame.lines)
            out << l.text();
        return out.join(QLatin1Char('\n'));
    }
};

static QAccessibleInterface *terminalAccessibleFactory(const QString &className, QObject *object)
{
    if (className == QLatin1String("relay::TerminalView") && object && object->isWidgetType())
        return new TerminalAccessible(static_cast<TerminalView *>(object));
    return nullptr;
}

// ---------------------------------------------------------------- construction

TerminalView::TerminalView(TerminalSession *session, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
{
    static bool factoryInstalled = false;
    if (!factoryInstalled) {
        QAccessible::installFactory(terminalAccessibleFactory);
        factoryInstalled = true;
    }

    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_InputMethodEnabled);
    setMouseTracking(true);
    setCursor(Qt::IBeamCursor);

    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (QFontDatabase().families().contains(QStringLiteral("DejaVu Sans Mono")))
        f.setFamily(QStringLiteral("DejaVu Sans Mono"));
    f.setStyleHint(QFont::Monospace);
    f.setPointSize(std::max(10, f.pointSize()));
    f.setKerning(false);
    m_baseFont = f;
    m_emojiFont = QFont(defaultEmojiFamily());
    updateMetrics();

    m_frameTimer.setSingleShot(true);
    connect(&m_frameTimer, &QTimer::timeout, this, &TerminalView::pullFrame);
    m_geometryTimer.setSingleShot(true);
    connect(&m_geometryTimer, &QTimer::timeout, this, &TerminalView::applyGeometry);
    m_blinkTimer.setInterval(600);
    connect(&m_blinkTimer, &QTimer::timeout, this, [this] {
        m_blinkOn = !m_blinkOn;
        if (m_frame.cursor.blink && m_frame.cursorInViewport)
            update(cellRect(m_frame.cursor.row, m_frame.cursor.col, 2));
    });
    m_autoScroll.setInterval(50);
    connect(&m_autoScroll, &QTimer::timeout, this, &TerminalView::autoScrollTick);

    connect(m_session, &TerminalSession::contentChanged, this, &TerminalView::scheduleFrame);
    connect(m_session, &TerminalSession::altScreenChanged, this, [this] {
        m_forceFull = true;
        scheduleFrame();
    });
    connect(m_session, &TerminalSession::bell, this, [this] {
        emit bellRang();
        if (!m_visualBell || m_flash)
            return;
        m_flash = true;
        update();
        QTimer::singleShot(80, this, [this] {
            m_flash = false;
            update();
        });
    });
    connect(m_session, &TerminalSession::clipboardWriteRequested, this, [](const QString &target, const QByteArray &data) {
        const QClipboard::Mode mode = target == QLatin1String("clipboard") || !QApplication::clipboard()->supportsSelection()
            ? QClipboard::Clipboard
            : QClipboard::Selection;
        QApplication::clipboard()->setText(QString::fromUtf8(data), mode);
    });

    setColorScheme(m_scheme);
    m_sinceFrame.start();
    m_lastClick.start();
    m_foldResolveAt.start();
}

TerminalView::~TerminalView() = default;

// ---------------------------------------------------------------- appearance

void TerminalView::updateMetrics()
{
    QFont base = m_baseFont;
    if (m_zoom != 0) {
        if (base.pointSizeF() > 0)
            base.setPointSizeF(std::max(4.0, base.pointSizeF() + m_zoom));
        else
            base.setPixelSize(std::max(6, base.pixelSize() + m_zoom));
    }
    for (int i = 0; i < 4; ++i) {
        m_fonts[i] = base;
        m_fonts[i].setBold(i & 1);
        m_fonts[i].setItalic(i & 2);
        m_raw[i] = QRawFont::fromFont(m_fonts[i]);
    }
    const QFontMetricsF fm(m_fonts[0]);
    m_cw = std::max(1, qCeil(fm.horizontalAdvance(QLatin1Char('M')) - 0.01));
    m_ch = std::max(1, qCeil(fm.height() - 0.01) + m_lineSpacing);
    // Half the extra spacing goes above the glyphs, as Konsole does, so text sits centred.
    m_ascent = qCeil(fm.ascent() - 0.01) + (m_lineSpacing + 1) / 2;
    m_descent = std::max(1, m_ch - m_ascent);
    m_emojiFont.setPixelSize(std::max(6, int(m_ch * 0.88)));
    m_glyphCache.clear();
}

void TerminalView::setTerminalFont(const QFont &font)
{
    m_baseFont = font;
    m_baseFont.setKerning(false);
    m_zoom = 0;
    updateMetrics();
    applyGeometry();
    m_forceFull = true;
    update();
}

void TerminalView::setLineSpacing(int pixels)
{
    const int clamped = std::max(0, std::min(16, pixels));
    if (clamped == m_lineSpacing)
        return;
    m_lineSpacing = clamped;
    updateMetrics();
    applyGeometry();
    m_forceFull = true;
    update();
}

void TerminalView::setPadding(int pixels)
{
    const int clamped = std::max(0, std::min(64, pixels));
    if (clamped == m_padding)
        return;
    m_padding = clamped;
    applyGeometry();
    m_forceFull = true;
    update();
}

void TerminalView::zoomIn()
{
    ++m_zoom;
    updateMetrics();
    applyGeometry();
    update();
}

void TerminalView::zoomOut()
{
    --m_zoom;
    updateMetrics();
    applyGeometry();
    update();
}

void TerminalView::resetZoom()
{
    m_zoom = 0;
    updateMetrics();
    applyGeometry();
    update();
}

void TerminalView::setColorScheme(const ColorScheme &scheme)
{
    m_scheme = scheme;
    uint32_t palette[16];
    for (int i = 0; i < 16; ++i)
        palette[i] = scheme.palette[size_t(i)] & 0xFFFFFF;
    const uint32_t fg = scheme.rgb(scheme.foreground), bg = scheme.rgb(scheme.background);
    m_session->withCore([&](VtCore &c) { c.setColors(fg, bg, palette); });
    m_forceFull = true;
    scheduleFrame();
}

void TerminalView::setEmojiFontFamily(const QString &family)
{
    m_emojiFont.setFamily(family);
    update();
}

void TerminalView::setUnfocusedCursorVisible(bool on)
{
    if (m_unfocusedCursor == on)
        return;
    m_unfocusedCursor = on;
    if (!m_focused)
        update();
}

void TerminalView::setCursorBlink(bool on)
{
    m_blinkEnabled = on;
    m_blinkOn = true;
    if (!on)
        m_blinkTimer.stop();
    else if (m_focused)
        m_blinkTimer.start();
}

void TerminalView::setClipboardWriteAllowed(bool allowed)
{
    m_session->setClipboardWriteAllowed(allowed);
}

QSize TerminalView::sizeForGrid(int rows, int cols) const
{
    return QSize(cols * m_cw + 2 * m_padding, rows * m_ch + 2 * m_padding);
}

QRect TerminalView::cellRect(int row, int col, int width) const
{
    return QRect(m_padding + col * m_cw, m_padding + row * m_ch, width * m_cw, m_ch);
}

TerminalView::CellPos TerminalView::cellAt(const QPoint &p, bool clamp) const
{
    int row = (p.y() - m_padding) >= 0 ? (p.y() - m_padding) / m_ch : -1;
    int col = (p.x() - m_padding) >= 0 ? (p.x() - m_padding) / m_cw : 0;
    if (clamp) {
        row = std::max(0, std::min(row, m_rows - 1));
        col = std::max(0, std::min(col, m_cols - 1));
    }
    return CellPos{row, col};
}

// ---------------------------------------------------------------- geometry and frames

void TerminalView::resizeEvent(QResizeEvent *)
{
    scheduleGeometry();
    if (m_searchBar)
        m_searchBar->move(width() - m_searchBar->width() - 8, 4);
}

void TerminalView::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    // The view may have been resized while it was hidden, so ask for the grid again.
    scheduleGeometry();
}

// Moving a pane to another split, tab or window hides it, reparents it and shows it again within
// one turn of the event loop, and Qt hands the view several sizes on the way (0 wide, then the
// parentless 100x30, then the real one). Following those would resize the emulator down to one
// row and a couple of columns, which reflows the screen and loses everything that was on it, so
// the grid follows the size the view still has once the layout has settled.
void TerminalView::scheduleGeometry()
{
    if (!m_geometryTimer.isActive())
        m_geometryTimer.start(0);
}

void TerminalView::applyGeometry()
{
    // A hidden or parentless view has no size worth following; showEvent() asks again.
    if (!isVisible())
        return;
    const int cols = std::max(2, (width() - 2 * m_padding) / m_cw);
    const int rows = std::max(1, (height() - 2 * m_padding) / m_ch);
    if (cols == m_cols && rows == m_rows && m_session->rows() == rows && m_session->columns() == cols)
        return;
    m_rows = rows;
    m_cols = cols;
    m_session->resize(rows, cols, m_cw, m_ch);
    m_forceFull = true;
    // Both cores rewrap the scrollback on a resize, so every fold has to be
    // told its new width and then find its anchor row again.
    m_folds.setGeometry(cols, m_folds.indent());
    m_foldSearch.invalidate(); // the block rewrapped: its matches sit on other rows
    if (!m_folds.folds().empty())
        m_foldAnchorsDirty = true;
    emit gridSizeChanged(rows, cols);
    scheduleFrame();
}

void TerminalView::scheduleFrame()
{
    // Typing echo should appear within one frame; output that keeps coming
    // repaints at display rate, so painting (and the X server) never becomes
    // the bottleneck.
    //
    // The pace is taken from the clock, not from the byte count. "More than
    // 512 KiB since the last frame" needed over 128 MiB/s at a 4 ms interval,
    // and the core delivers 26-37, so the cap never engaged and the view
    // painted ~150 times a second during a flood — a whole core spent on frames
    // no 60 Hz display shows (#6W0Z). It is also a latch: the oftener it
    // frames, the less each frame accumulates. Here the *first* frame after a
    // quiet moment still goes out in 4 ms, which is what keystroke echo rides
    // on (10.6 ms p50, measured end to end), and only a frame that follows
    // another one closely is held to one display frame.
    const bool streaming = m_sinceFrame.isValid() && m_sinceFrame.elapsed() < kStreamingGapMs;
    const int wanted = streaming ? kStreamingFrameMs : kEchoFrameMs;
    // A slower frame may already be pending -- the fold layer's anchor
    // heartbeat uses this same timer -- and must not hold up this one.
    if (m_frameTimer.isActive() && m_frameTimer.interval() <= wanted)
        return;
    m_frameTimer.start(wanted);
}

void TerminalView::pullFrame()
{
    const bool force = m_forceFull;
    m_forceFull = false;
    m_sinceFrame.restart();
    // Both of these are resolved at most once per frame, lazily, by the rows
    // that need them: the pane's directory (a readlink and a stat of
    // /proc/<pid>/cwd, plus the core's own mutex) and the URI behind a link id.
    // restLinkColumns() asked for both per painted row, which was most of the
    // GUI thread's syscalls during output (#6W0Z). Neither can change within
    // one frame; a `cd` shows up on the next one, 4-16 ms later.
    m_frameCwdValid = false;
    m_frameProse.clear();
    // Anchors are re-read before the frame, so the rows the fold layer works
    // with belong to the same content the frame will show. The heartbeat only
    // has something to find when content has moved under the anchors since the
    // last walk — output, trimming — so an idle pane with a fold open resolves
    // nothing (#PPR4).
    if (m_foldAnchorsDirty || (m_folds.active() && m_contentMoved && m_foldResolveAt.elapsed() > 250))
        resolveFoldAnchors();
    bool changed = false;
    m_session->withCore([&](VtCore &c) {
        changed = c.updateFrame(&m_frame, force);
        syncFoldViewport(c, &changed);
    });
    m_contentMoved = m_contentMoved || changed;
    // While a fold is open the anchors are re-read on a slow heartbeat, which
    // is what notices the scrollback trimming its oldest lines away underneath
    // them. Nothing runs when no fold is open.
    if (m_folds.active() && !m_frameTimer.isActive())
        m_frameTimer.start(300);
    if (!changed)
        return;
    emit frameChanged();

    // A link underline belongs to the content it was computed for.
    if (m_hoverRow >= 0 && !m_linkCursor.active()
        && (m_frame.full || (m_hoverRow < int(m_frame.dirty.size()) && m_frame.dirty[size_t(m_hoverRow)]))) {
        m_hoverRow = m_hoverStart = m_hoverEnd = -1;
        m_hoverCellRow = m_hoverCellCol = -2;
        setCursor(Qt::IBeamCursor);
    }

    const bool folds = foldsVisible();
    if (m_frame.full || force || m_visualTopMoved) {
        // Everything on screen moved: the frame is a full one, something asked
        // for a full paint, or the visual window scrolled under the fold layer.
        update();
    } else {
        // A dirty row still only costs its own row, with a fold layer or
        // without one: the layer splices whole blocks in at fixed places, so
        // the row a real row is painted on is a lookup (#6W0Z). Anything that
        // *moves* a block — a fold opening or shutting, its content, a resize,
        // a scroll — sets m_frame.full or m_forceFull and takes the branch
        // above, so the mapping used here is the one the last paint used.
        // Before this, one anchored fold anywhere made an agent pane repaint
        // every content row for every frame of streaming output.
        const auto rowRect = [this](int screenRow) {
            return QRect(0, m_padding + screenRow * m_ch, width(), m_ch);
        };
        const auto screenOf = [this, folds](int frameRow) {
            return folds ? screenRowOfReal(m_frame.viewportTop + frameRow) : frameRow;
        };
        QRegion region;
        for (int r = 0; r < m_frame.rows && r < int(m_frame.dirty.size()); ++r) {
            if (!m_frame.dirty[size_t(r)])
                continue;
            const int screenRow = screenOf(r);
            if (screenRow >= 0 && screenRow < m_rows)
                region += rowRect(screenRow);
        }
        const bool cursorMoved = m_paintedCursor.row != m_frame.cursor.row || m_paintedCursor.col != m_frame.cursor.col
            || m_paintedCursor.visible != m_frame.cursor.visible || m_paintedCursor.shape != m_frame.cursor.shape
            || m_paintedCursorInViewport != m_frame.cursorInViewport;
        if (cursorMoved) {
            for (const int screenRow : {screenOf(m_paintedCursor.row), screenOf(m_frame.cursor.row)}) {
                if (screenRow >= 0 && screenRow < m_rows)
                    region += rowRect(screenRow);
            }
        }
        if (!region.isEmpty())
            update(region);
    }
    m_visualTopMoved = false;
    if (m_frame.cursor.row != m_paintedCursor.row || m_frame.cursor.col != m_paintedCursor.col)
        m_blinkOn = true;
    m_paintedCursor = m_frame.cursor;
    m_paintedCursorInViewport = m_frame.cursorInViewport;

    // The scroll bar counts visual rows: real rows with the rows of every open
    // fold spliced in. With no fold open these are the core's own numbers.
    const int top = foldsVisible() ? m_visualTop : m_frame.viewportTop;
    const int range = foldsVisible() ? maxVisualTop() : m_frame.historyRows;
    if (top != m_lastTop || range != m_lastHistory) {
        m_lastTop = top;
        m_lastHistory = range;
        emit scrollPositionChanged(top, range, foldsVisible() ? m_rows : m_frame.rows);
    }
    if (QAccessible::isActive()) {
        QAccessibleEvent ev(this, QAccessible::VisibleDataChanged);
        QAccessible::updateAccessibility(&ev);
    }
}

// ---------------------------------------------------------------- painting

QColor TerminalView::resolve(uint32_t packed, bool foreground) const
{
    switch (CellColor::kind(packed)) {
    case CellColor::Rgb: {
        const uint32_t v = CellColor::value(packed);
        return QColor(int(v >> 16) & 255, int(v >> 8) & 255, int(v) & 255);
    }
    case CellColor::Indexed:
        return m_scheme.indexed(int(CellColor::value(packed)));
    default:
        return foreground ? m_scheme.foreground : m_scheme.background;
    }
}

quint32 TerminalView::glyphFor(int variant, char32_t cp)
{
    const uint64_t key = (uint64_t(variant) << 32) | cp;
    auto it = m_glyphCache.find(key);
    if (it != m_glyphCache.end())
        return it->second;
    quint32 glyph = 0;
    if (m_raw[variant].supportsCharacter(uint(cp))) {
        const QString s = QString::fromUcs4(&cp, 1);
        const QVector<quint32> g = m_raw[variant].glyphIndexesForString(s);
        glyph = g.size() == 1 ? g[0] : 0;
    }
    m_glyphCache.emplace(key, glyph);
    return glyph;
}

void TerminalView::paintEvent(QPaintEvent *e)
{
    ++m_paints;
    QPainter p(this);
    const QRect dirty = e->rect();
    if (!m_flash && m_scheme.backgroundEnd.isValid()) {
        // The gradient spans the whole view, not the dirty rect, so a partial repaint lands on
        // exactly the colour the full paint put there.
        QLinearGradient ground(0, 0, 0, height());
        ground.setColorAt(0, m_scheme.background);
        ground.setColorAt(1, m_scheme.backgroundEnd);
        p.fillRect(dirty, ground);
    } else {
        p.fillRect(dirty, m_flash ? m_scheme.foreground : m_scheme.background);
    }
    if (m_frame.lines.empty())
        return;
    const int firstRow = std::max(0, (dirty.top() - m_padding) / m_ch);
    const bool folds = foldsVisible();
    const int lastRow = std::min(folds ? m_rows - 1 : int(m_frame.lines.size()) - 1, (dirty.bottom() - m_padding) / m_ch);
    for (int row = firstRow; row <= lastRow; ++row) {
        if (!folds) {
            paintRow(p, row, m_frame.lines[size_t(row)], m_frame.viewportTop + row);
            continue;
        }
        const FoldLayer::VisualRow v = m_folds.at(m_visualTop + row);
        if (v.fold) {
            paintFoldRow(p, row, v.foldIndex, v.foldRow);
            continue;
        }
        const int frameRow = v.realRow - m_frame.viewportTop;
        if (frameRow >= 0 && frameRow < int(m_frame.lines.size()))
            paintRow(p, row, m_frame.lines[size_t(frameRow)], v.realRow);
    }
    paintCursor(p);
}

namespace {
// An ink that carries no meaning of its own: the default foreground, white, bright white, a grey,
// the host's muted ink, a light theme's near-black "bright white". Either a small channel spread
// (a dark warm grey such as IBM Beige's ANSI 15, #14120d, has HSV saturation 0.35 on a spread of
// 7) or a low HSV saturation (Gruvbox's beige foreground, spread 57, saturation 0.24). Every ANSI
// 1-6 and 9-14 in the shipped themes has a spread over 80 and a saturation over 0.45.
bool plainInk(const QColor &c)
{
    const int spread = std::max({c.red(), c.green(), c.blue()}) - std::min({c.red(), c.green(), c.blue()});
    return spread <= 40 || c.hsvSaturationF() < 0.3;
}
} // namespace

void TerminalView::paintRow(QPainter &p, int row, const Line &line, int realRow)
{
    ++m_rowPaints;
    const int cols = std::min<int>(int(line.cells.size()), m_frame.columns);
    const int y = m_padding + row * m_ch;
    const int baseline = y + m_ascent;
    // The core still calls its own parked match "the selected one" while the
    // find sits on a match inside a fold; only one match anywhere is current,
    // so here it is drawn as an ordinary one.
    const bool foldOwnsCurrent = foldsVisible() && m_foldSearch.currentIsFold();

    // A row the host marked as typed by the user wears its role's band and ink from the scheme in
    // force now (ColorScheme.h); the shell's own prompt row wears the prompt band. The band runs
    // the full width of the grid, not just under the cells the row happens to hold.
    const bool roleAgent = line.marks & MarkUserAgent, roleShell = !roleAgent && (line.marks & MarkUserShell);
    const QColor roleBand = roleAgent ? m_scheme.userAgentBand : roleShell ? m_scheme.userShellBand : QColor();
    const QColor roleInk = roleAgent ? m_scheme.userAgentInk : roleShell ? m_scheme.userShellInk : QColor();
    const bool roleRow = roleAgent || roleShell;
    const QColor rowBand = roleBand.isValid() ? roleBand
        : (!roleRow && m_scheme.promptBand.isValid() && (line.marks & MarkPromptStart)) ? m_scheme.promptBand : QColor();
    if (rowBand.isValid())
        p.fillRect(QRect(m_padding, y, m_cols * m_cw, m_ch), rowBand);

    struct CellColors {
        QColor fg;
        QColor bg;
        bool bgIsDefault;
    };
    auto colorsFor = [&](int col) -> CellColors {
        int lead = col;
        if (line.cells[size_t(col)].ch == kWideTail && col > 0)
            lead = col - 1;
        const Cell &c = line.cells[size_t(lead)];
        QColor fg = resolve(c.fg, true);
        QColor bg = resolve(c.bg, false);
        bool bgDefault = CellColor::kind(c.bg) == CellColor::Default;
        if (roleInk.isValid() && CellColor::kind(c.fg) == CellColor::Default)
            fg = roleInk;
        if (c.attrs & AttrReverse) {
            std::swap(fg, bg);
            bgDefault = false;
        }
        // On a banded row, a cell that brought no background of its own sits on the band.
        if (bgDefault && rowBand.isValid()) {
            bg = rowBand;
            bgDefault = false;
        }
        if (line.selectionStart >= 0 && col >= line.selectionStart && col <= line.selectionEnd) {
            bg = m_scheme.selection;
            bgDefault = false;
        }
        for (const Line::Highlight &h : line.highlights) {
            if (col >= h.start && col <= h.end) {
                bg = h.current && !foldOwnsCurrent ? m_scheme.searchCurrent : m_scheme.searchMatch;
                fg = m_scheme.searchText;
                bgDefault = false;
            }
        }
        // Faint (SGR 2) is a real colour, not 60% alpha: faded toward the ground this cell is
        // actually drawn on, and no further than 4.5:1 on it (view/FaintInk.h, #LG7T).
        if (c.attrs & AttrFaint)
            fg = faintInk(fg, bgDefault && bg == m_scheme.background ? groundAt(y) : bg);
        return {fg, bg, bgDefault && bg == m_scheme.background};
    };

    // Backgrounds, merged into runs.
    int runStart = -1;
    QColor runColor;
    for (int col = 0; col <= cols; ++col) {
        QColor bg;
        bool none = true;
        if (col < cols) {
            const CellColors cc = colorsFor(col);
            none = cc.bgIsDefault;
            bg = cc.bg;
        }
        if (runStart >= 0 && (none || bg != runColor)) {
            p.fillRect(QRect(m_padding + runStart * m_cw, y, (col - runStart) * m_cw, m_ch), runColor);
            runStart = -1;
        }
        if (!none && runStart < 0) {
            runStart = col;
            runColor = bg;
        }
    }

    // Text: glyph runs per (font variant, colour); fallbacks drawn per cell.
    struct Batch {
        int variant;
        QRgb color;
        QColor qcolor;
        QVector<quint32> glyphs;
        QVector<QPointF> positions;
    };
    std::vector<Batch> batches;
    auto batchFor = [&](int variant, const QColor &color) -> Batch & {
        for (Batch &b : batches) {
            if (b.variant == variant && b.color == color.rgba())
                return b;
        }
        batches.push_back({variant, color.rgba(), color, {}, {}});
        return batches.back();
    };
    const bool hoverRow = row == m_hoverRow;
    // Links at rest (setLinksColouredAtRest): the columns of this row inside a path, URL or card
    // reference that resolves. Empty when the option is off, on the alternate screen, or when the
    // row holds none.
    std::vector<char> restLink;
    if (m_linksAtRest && !m_frame.altScreen && !roleRow)   // a role row's ink is the role's
        restLinkColumns(realRow - m_frame.viewportTop, &restLink);
    const auto highlighted = [&line](int col) {
        for (const Line::Highlight &h : line.highlights)
            if (col >= h.start && col <= h.end) return true;
        return false;
    };
    std::u32string cps;
    for (int col = 0; col < cols; ++col) {
        const Cell &c = line.cells[size_t(col)];
        if (c.ch == kWideTail)
            continue;
        const int w = c.width == 2 ? 2 : 1;
        CellColors cc = colorsFor(col);
        // The link colour, on a cell whose ink is plain (plainInk: the default foreground or any
        // achromatic one — the agent's prose is bright white, a tool line is the host's grey) and
        // that nothing else claims: a find match keeps its ink, a chromatic colour a program chose
        // keeps its meaning.
        if (col < int(restLink.size()) && restLink[size_t(col)] && !(c.attrs & AttrReverse) && !highlighted(col)
            && (CellColor::kind(c.fg) == CellColor::Default || plainInk(cc.fg)))
            cc.fg = m_scheme.link;
        const int x = m_padding + col * m_cw;
        const int variant = ((c.attrs & AttrBold) ? 1 : 0) | ((c.attrs & AttrItalic) ? 2 : 0);

        // Decorations.
        const bool linkHover = hoverRow && col >= m_hoverStart && col <= m_hoverEnd;
        if ((c.attrs & (AttrUnderline | AttrDoubleUnderline | AttrCurlyUnderline)) || linkHover) {
            const int uy = baseline + std::max(1, m_descent / 3);
            const QColor uc = linkHover ? m_scheme.link : cc.fg;
            if (c.attrs & AttrCurlyUnderline) {
                QPainterPath path;
                path.moveTo(x, uy);
                for (int k = 0; k < w * m_cw; k += 2)
                    path.lineTo(x + k + 2, uy + ((k / 2) % 2 ? -1 : 1));
                p.save();
                p.setPen(QPen(uc, 1));
                p.drawPath(path);
                p.restore();
            } else {
                p.fillRect(QRect(x, uy, w * m_cw, 1), uc);
                if (c.attrs & AttrDoubleUnderline)
                    p.fillRect(QRect(x, uy + 2, w * m_cw, 1), uc);
            }
        }
        if (c.attrs & AttrStrike)
            p.fillRect(QRect(x, y + m_ch / 2, w * m_cw, 1), cc.fg);

        if ((c.ch == 0 && !(c.attrs & AttrCluster)) || (c.attrs & AttrConceal))
            continue;

        cps.clear();
        line.cellCodepoints(c, &cps);
        if (cps.empty())
            continue;
        const char32_t base = cps[0];
        if (cps.size() == 1 && base >= 0x2500 && base <= 0x259F && drawBoxCharacter(p, base, cellRect(row, col, 1), cc.fg))
            continue;
        if (wantsEmojiFont(cps, w)) {
            p.save();
            const QRect r = cellRect(row, col, w);
            p.setClipRect(r);
            p.setFont(m_emojiFont);
            p.setPen(cc.fg);
            p.drawText(r, Qt::AlignCenter, ucs4ToString(cps));
            p.restore();
            continue;
        }
        if (cps.size() == 1) {
            const quint32 glyph = glyphFor(variant, base);
            if (glyph) {
                Batch &b = batchFor(variant, cc.fg);
                b.glyphs.push_back(glyph);
                b.positions.push_back(QPointF(x, baseline));
                continue;
            }
        }
        // Fallback: combining sequences and characters missing from the
        // primary font (CJK, symbols). Qt picks a fallback font; clip to the cell.
        p.save();
        p.setClipRect(cellRect(row, col, w));
        p.setFont(m_fonts[variant]);
        p.setPen(cc.fg);
        p.drawText(QPointF(x, baseline), ucs4ToString(cps));
        p.restore();
    }
    for (const Batch &b : batches) {
        QGlyphRun run;
        run.setRawFont(m_raw[b.variant]);
        run.setGlyphIndexes(b.glyphs);
        run.setPositions(b.positions);
        p.setPen(b.qcolor);
        p.drawGlyphRun(QPointF(0, 0), run);
    }

    // A fold anchor says whether its block is open, in its own first cell: the
    // host prints a placeholder there and the view overpaints the chevron.
    if (!m_frame.altScreen && !m_folds.prefix().isEmpty() && cols > 0) {
        const int foldIndex = m_folds.foldAtAnchorStart(realRow);
        if (foldIndex >= 0) {
            const QRect r = cellRect(row, 0, 1);
            const CellColors cc = colorsFor(0);
            p.save();
            p.setClipRect(r);
            p.fillRect(r, cc.bgIsDefault ? groundAt(y) : cc.bg);
            p.setFont(m_fonts[0]);
            p.setPen(cc.fg);
            p.drawText(r, Qt::AlignCenter,
                       m_folds.folds()[size_t(foldIndex)].expanded ? QStringLiteral("▾") : QStringLiteral("▸"));
            p.restore();
        }
    }
}

// The colour the background gradient (if any) has at this pixel row, so an
// overpainted cell lands on exactly what the full paint put there.
QColor TerminalView::groundAt(int y) const
{
    if (!m_scheme.backgroundEnd.isValid() || height() <= 0)
        return m_scheme.background;
    return mix(m_scheme.background, m_scheme.backgroundEnd, std::min(1.0, std::max(0.0, double(y) / height())));
}

QColor TerminalView::foldBackground() const
{
    if (m_scheme.foldBackground.isValid())
        return m_scheme.foldBackground;
    return mix(m_scheme.background, m_scheme.foreground, 0.07);
}

QColor TerminalView::foldRule() const
{
    if (m_scheme.foldRule.isValid())
        return m_scheme.foldRule;
    return mix(m_scheme.background, m_scheme.foreground, 0.38);
}

// One wrapped row of an open fold: the block's tint across the width, a rule
// down its left edge, and the row's cells in the terminal's own grid and font
// starting at the indent. Spans bring their own colours (a diff's red and
// green), so the host decides what the detail looks like. A prose block's row
// is painted by paintProseRow instead, as a row of the grid it replaces.
void TerminalView::paintFoldRow(QPainter &p, int screenRow, int foldIndex, int foldRow)
{
    const std::vector<FoldLayer::Fold> &folds = m_folds.folds();
    if (foldIndex < 0 || foldIndex >= int(folds.size()))
        return;
    const FoldLayer::Fold &f = folds[size_t(foldIndex)];
    if (foldRow < 0 || foldRow >= int(f.rows.size()))
        return;
    if (f.replacement) {
        paintProseRow(p, screenRow, f, foldRow);
        return;
    }
    const FoldLayer::Row &row = f.rows[size_t(foldRow)];
    const int y = m_padding + screenRow * m_ch;
    const int baseline = y + m_ascent;
    const int indent = m_folds.indent();

    p.fillRect(QRect(m_padding, y, m_cols * m_cw, m_ch), foldBackground());
    const int ruleX = m_padding + std::max(0, indent - 2) * m_cw + m_cw / 2;
    p.fillRect(QRect(ruleX, y, std::max(1, m_cw / 8), m_ch), foldRule());

    const std::vector<FoldLayer::Cell> &cells = f.cells[size_t(row.line)];
    struct Batch {
        int variant;
        QRgb color;
        QColor qcolor;
        QVector<quint32> glyphs;
        QVector<QPointF> positions;
    };
    std::vector<Batch> batches;
    auto batchFor = [&](int variant, const QColor &color) -> Batch & {
        for (Batch &b : batches) {
            if (b.variant == variant && b.color == color.rgba())
                return b;
        }
        batches.push_back({variant, color.rgba(), color, {}, {}});
        return batches.back();
    };

    int selFrom = 0, selTo = -1;
    const bool selected = foldSelectionRange(foldIndex, foldRow, &selFrom, &selTo);
    // Find matches inside the block, highlighted exactly as the core's are in
    // the real rows. A match that straddles the block's wrap is one match, so
    // it comes back clipped to this row.
    const std::vector<FoldSearch::RowMatch> hits = m_foldSearch.rowMatches(m_folds, foldIndex, foldRow);

    int col = indent;
    for (int i = row.first; i < row.first + row.count && i < int(cells.size()); ++i) {
        const FoldLayer::Cell &c = cells[size_t(i)];
        if (col + c.width > m_cols)
            break;
        const int x = m_padding + col * m_cw;
        QColor fg = c.fg.isValid() ? c.fg : m_scheme.foreground;
        bool hit = false, hitCurrent = false;
        for (const FoldSearch::RowMatch &h : hits) {
            if (i >= h.from && i < h.to) {
                hit = true;
                hitCurrent = hitCurrent || h.current;
            }
        }
        if (c.bg.isValid())
            p.fillRect(QRect(x, y, c.width * m_cw, m_ch), c.bg);
        if (selected && col >= selFrom && col <= selTo)
            p.fillRect(QRect(x, y, c.width * m_cw, m_ch), m_scheme.selection);
        if (hit)
            p.fillRect(QRect(x, y, c.width * m_cw, m_ch), hitCurrent ? m_scheme.searchCurrent : m_scheme.searchMatch);
        // A dim span, against whatever was just painted under it — the block's band, a diff's
        // tint, the selection — and never below 4.5:1 on it (view/FaintInk.h, #LG7T).
        if (c.dim) {
            const QColor under = hit ? (hitCurrent ? m_scheme.searchCurrent : m_scheme.searchMatch)
                : (selected && col >= selFrom && col <= selTo) ? m_scheme.selection
                : c.bg.isValid()                               ? c.bg
                                                               : foldBackground();
            fg = faintInk(fg, under);
        }
        const bool hovered = screenRow == m_hoverRow && col >= m_hoverStart && col <= m_hoverEnd;
        if (c.underline || !c.link.isEmpty() || hovered) {
            const int uy = baseline + std::max(1, m_descent / 3);
            p.fillRect(QRect(x, uy, c.width * m_cw, 1), hovered || !c.link.isEmpty() ? m_scheme.link : fg);
        }
        if (!c.link.isEmpty())
            fg = m_scheme.link;
        if (hit)
            fg = m_scheme.searchText;
        const int variant = (c.bold ? 1 : 0) | (c.italic ? 2 : 0);
        const std::u32string cps = c.text.toStdU32String();
        if (!cps.empty() && cps[0] != U' ') {
            if (cps.size() == 1 && cps[0] >= 0x2500 && cps[0] <= 0x259F
                && drawBoxCharacter(p, cps[0], QRect(x, y, m_cw, m_ch), fg)) {
                // drawn as a rectangle, like the real grid's box characters
            } else if (cps.size() == 1 && glyphFor(variant, cps[0])) {
                Batch &b = batchFor(variant, fg);
                b.glyphs.push_back(glyphFor(variant, cps[0]));
                b.positions.push_back(QPointF(x, baseline));
            } else {
                // Clusters, emoji and anything missing from the primary font go
                // through the same fallback path the real rows use.
                p.save();
                p.setClipRect(QRect(x, y, c.width * m_cw, m_ch));
                p.setFont(wantsEmojiFont(cps, c.width) ? m_emojiFont : m_fonts[variant]);
                p.setPen(fg);
                if (wantsEmojiFont(cps, c.width))
                    p.drawText(QRect(x, y, c.width * m_cw, m_ch), Qt::AlignCenter, c.text);
                else
                    p.drawText(QPointF(x, baseline), c.text);
                p.restore();
            }
        }
        col += c.width;
    }
    for (const Batch &b : batches) {
        QGlyphRun run;
        run.setRawFont(m_raw[b.variant]);
        run.setGlyphIndexes(b.glyphs);
        run.setPositions(b.positions);
        p.setPen(b.qcolor);
        p.drawGlyphRun(QPointF(0, 0), run);
    }
}

// One row of a re-wrapped prose block (#R2WQ): painted as a row of the grid it
// replaces, in the terminal's own font and colours resolved from the theme at
// paint time — so a block the pane printed bold magenta headings in keeps them
// after a resize, and a theme switch recolours it like any grid row. A line the
// user typed keeps its role's band and ink (OSC 7772's promise, kept by the
// layer instead of the core while the block is taken over).
void TerminalView::paintProseRow(QPainter &p, int screenRow, const FoldLayer::Fold &f, int foldRow)
{
    if (foldRow < 0 || foldRow >= int(f.rows.size()))
        return;
    const FoldLayer::Row &row = f.rows[size_t(foldRow)];
    if (row.line < 0 || row.line >= int(f.cells.size()))
        return;
    const std::vector<FoldLayer::Cell> &cells = f.cells[size_t(row.line)];
    const int y = m_padding + screenRow * m_ch;
    const int baseline = y + m_ascent;

    const quint8 role = row.line < f.lines.size() ? f.lines[size_t(row.line)].role : 0;
    const bool roleAgent = role & kFoldRoleAgent, roleShell = !roleAgent && (role & kFoldRoleShell);
    const QColor roleBand = roleAgent ? m_scheme.userAgentBand : roleShell ? m_scheme.userShellBand : QColor();
    const QColor roleInk = roleAgent ? m_scheme.userAgentInk : roleShell ? m_scheme.userShellInk : QColor();
    if (roleBand.isValid())
        p.fillRect(QRect(m_padding, y, m_cols * m_cw, m_ch), roleBand);

    struct Batch {
        int variant;
        QRgb color;
        QColor qcolor;
        QVector<quint32> glyphs;
        QVector<QPointF> positions;
    };
    std::vector<Batch> batches;
    auto batchFor = [&](int variant, const QColor &color) -> Batch & {
        for (Batch &b : batches) {
            if (b.variant == variant && b.color == color.rgba())
                return b;
        }
        batches.push_back({variant, color.rgba(), color, {}, {}});
        return batches.back();
    };

    int selFrom = 0, selTo = -1;
    const int foldIndex = m_folds.indexOf(f.uri);
    const bool selected = foldIndex >= 0 && foldSelectionRange(foldIndex, foldRow, &selFrom, &selTo);
    const std::vector<FoldSearch::RowMatch> hits =
        foldIndex >= 0 ? m_foldSearch.rowMatches(m_folds, foldIndex, foldRow)
                       : std::vector<FoldSearch::RowMatch>();

    int col = row.startCol;
    for (int i = row.first; i < row.first + row.count && i < int(cells.size()); ++i) {
        const FoldLayer::Cell &c = cells[size_t(i)];
        if (col + c.width > m_cols)
            break;
        const int x = m_padding + col * m_cw;
        QColor fg = c.fg.isValid() ? c.fg
                  : c.fgPacked ? resolve(c.fgPacked, true)
                               : m_scheme.foreground;
        if (roleInk.isValid() && !c.fg.isValid() && !c.fgPacked)
            fg = roleInk;
        bool hit = false, hitCurrent = false;
        for (const FoldSearch::RowMatch &h : hits) {
            if (i >= h.from && i < h.to) {
                hit = true;
                hitCurrent = hitCurrent || h.current;
            }
        }
        if (c.bg.isValid())
            p.fillRect(QRect(x, y, c.width * m_cw, m_ch), c.bg);
        else if (roleBand.isValid())
            p.fillRect(QRect(x, y, c.width * m_cw, m_ch), roleBand);
        if (selected && col >= selFrom && col <= selTo)
            p.fillRect(QRect(x, y, c.width * m_cw, m_ch), m_scheme.selection);
        if (hit)
            p.fillRect(QRect(x, y, c.width * m_cw, m_ch), hitCurrent ? m_scheme.searchCurrent : m_scheme.searchMatch);
        if (c.dim) {
            const QColor under = hit ? (hitCurrent ? m_scheme.searchCurrent : m_scheme.searchMatch)
                : (selected && col >= selFrom && col <= selTo) ? m_scheme.selection
                : c.bg.isValid()                               ? c.bg
                : roleBand.isValid()                            ? roleBand
                                                               : groundAt(y);
            fg = faintInk(fg, under);
        }
        const bool hovered = screenRow == m_hoverRow && col >= m_hoverStart && col <= m_hoverEnd;
        if (c.underline || !c.link.isEmpty() || hovered) {
            const int uy = baseline + std::max(1, m_descent / 3);
            p.fillRect(QRect(x, uy, c.width * m_cw, 1), hovered || !c.link.isEmpty() ? m_scheme.link : fg);
        }
        if (!c.link.isEmpty())
            fg = m_scheme.link;
        if (hit)
            fg = m_scheme.searchText;
        const int variant = (c.bold ? 1 : 0) | (c.italic ? 2 : 0);
        const std::u32string cps = c.text.toStdU32String();
        if (!cps.empty() && cps[0] != U' ') {
            if (cps.size() == 1 && cps[0] >= 0x2500 && cps[0] <= 0x259F
                && drawBoxCharacter(p, cps[0], QRect(x, y, m_cw, m_ch), fg)) {
                // drawn as a rectangle, like the real grid's box characters
            } else if (cps.size() == 1 && glyphFor(variant, cps[0])) {
                Batch &b = batchFor(variant, fg);
                b.glyphs.push_back(glyphFor(variant, cps[0]));
                b.positions.push_back(QPointF(x, baseline));
            } else {
                p.save();
                p.setClipRect(QRect(x, y, c.width * m_cw, m_ch));
                p.setFont(wantsEmojiFont(cps, c.width) ? m_emojiFont : m_fonts[variant]);
                p.setPen(fg);
                if (wantsEmojiFont(cps, c.width))
                    p.drawText(QRect(x, y, c.width * m_cw, m_ch), Qt::AlignCenter, c.text);
                else
                    p.drawText(QPointF(x, baseline), c.text);
                p.restore();
            }
        }
        col += c.width;
    }
    for (const Batch &b : batches) {
        QGlyphRun run;
        run.setRawFont(m_raw[size_t(b.variant)]);
        run.setGlyphIndexes(b.glyphs);
        run.setPositions(b.positions);
        p.setPen(b.qcolor);
        p.drawGlyphRun(QPointF(0, 0), run);
    }
}

void TerminalView::paintCursor(QPainter &p)
{
    const ViewportFrame &f = m_frame;
    if (!f.cursorInViewport || f.cursor.row < 0 || f.cursor.row >= int(f.lines.size()))
        return;
    int screenRow = f.cursor.row;
    if (foldsVisible()) {
        screenRow = screenRowOfReal(f.viewportTop + f.cursor.row);
        if (screenRow < 0 || screenRow >= m_rows)
            return;
    }
    const Line &line = f.lines[size_t(f.cursor.row)];
    const int col = std::max(0, std::min(f.cursor.col, f.columns - 1));
    const Cell cell = col < int(line.cells.size()) ? line.cells[size_t(col)] : Cell();
    const int w = cell.width == 2 ? 2 : 1;
    QRect r = cellRect(screenRow, col, w);

    if (!m_preedit.isEmpty()) {
        const QFontMetrics fm(m_fonts[0]);
        const QRect pr(r.left(), r.top(), fm.horizontalAdvance(m_preedit) + 2, m_ch);
        p.fillRect(pr, m_scheme.background);
        p.setFont(m_fonts[0]);
        p.setPen(m_scheme.foreground);
        p.drawText(QPointF(pr.left(), pr.top() + m_ascent), m_preedit);
        p.fillRect(QRect(pr.left(), pr.bottom() - 1, pr.width(), 1), m_scheme.foreground);
        return;
    }
    if (!f.cursor.visible)
        return;
    if (!m_focused) {
        if (!m_unfocusedCursor)
            return;
        p.save();
        p.setPen(m_scheme.cursor);
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.adjusted(0, 0, -1, -1));
        p.restore();
        return;
    }
    if (f.cursor.blink && m_blinkEnabled && !m_blinkOn)
        return;
    switch (f.cursor.shape) {
    case CursorShape::Underline:
        p.fillRect(QRect(r.left(), r.bottom() - 1, r.width(), 2), m_scheme.cursor);
        return;
    case CursorShape::Bar:
        p.fillRect(QRect(r.left(), r.top(), 2, r.height()), m_scheme.cursor);
        return;
    case CursorShape::Block:
        break;
    }
    p.fillRect(r, m_scheme.cursor);
    if ((cell.ch != 0 || (cell.attrs & AttrCluster)) && cell.ch != kWideTail) {
        p.save();
        p.setClipRect(r);
        const int variant = ((cell.attrs & AttrBold) ? 1 : 0) | ((cell.attrs & AttrItalic) ? 2 : 0);
        std::u32string cps;
        line.cellCodepoints(cell, &cps);
        p.setFont(wantsEmojiFont(cps, w) ? m_emojiFont : m_fonts[variant]);
        p.setPen(m_scheme.cursorText);
        if (wantsEmojiFont(cps, w))
            p.drawText(r, Qt::AlignCenter, ucs4ToString(cps));
        else
            p.drawText(QPointF(r.left(), r.top() + m_ascent), ucs4ToString(cps));
        p.restore();
    }
}

// ---------------------------------------------------------------- keyboard

bool TerminalView::event(QEvent *e)
{
    if (e->type() == QEvent::ShortcutOverride) {
        auto *ke = static_cast<QKeyEvent *>(e);
        if (m_shortcutFilter && m_shortcutFilter(ke))
            return QWidget::event(e); // not accepted: the host shortcut fires
        // Super/Cmd combinations stay application shortcuts (Cmd+Q, Cmd+W),
        // except the view's own Cmd+C/V/F/... on macOS handled in keyPressEvent.
        if (mapModifiers(ke->modifiers(), m_keyOptions) & ModSuper) {
#if defined(Q_OS_MACOS)
            switch (ke->key()) {
            case Qt::Key_C: case Qt::Key_V: case Qt::Key_F: case Qt::Key_A:
            case Qt::Key_Plus: case Qt::Key_Equal: case Qt::Key_Minus: case Qt::Key_0:
                e->accept();
                return true;
            default:
                break;
            }
#endif
            return QWidget::event(e);
        }
        e->accept(); // the terminal owns every other key while focused
        return true;
    }
    return QWidget::event(e);
}

bool TerminalView::handleBuiltinShortcut(QKeyEvent *e)
{
    const Qt::KeyboardModifiers m = e->modifiers() & ~Qt::KeypadModifier;
#if defined(Q_OS_MACOS)
    const bool cmd = m == Qt::ControlModifier || m == (Qt::ControlModifier | Qt::ShiftModifier); // Command
#else
    const bool cmd = m == (Qt::ControlModifier | Qt::ShiftModifier);
#endif
    if (cmd) {
        switch (e->key()) {
        case Qt::Key_C: copySelection(); return true;
        case Qt::Key_V: pasteClipboard(); return true;
        case Qt::Key_F: showSearchBar(); return true;
        case Qt::Key_A: selectAll(); return true;
        case Qt::Key_Plus: case Qt::Key_Equal: zoomIn(); return true;
        case Qt::Key_Minus: case Qt::Key_Underscore: zoomOut(); return true;
        case Qt::Key_0: case Qt::Key_ParenRight: resetZoom(); return true;
        case Qt::Key_PageUp: scrollToPrompt(-1); return true;
        case Qt::Key_PageDown: scrollToPrompt(1); return true;
        case Qt::Key_Up: scrollLines(-1); return true;
        case Qt::Key_Down: scrollLines(1); return true;
        case Qt::Key_Home: scrollToTop(); return true;
        case Qt::Key_End: scrollToBottom(); return true;
        case Qt::Key_D: emit dumpRequested(); return true;
        // Open or shut the nearest tool-call line at or above the cursor (#TK9C).
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (toggleNearestFold())
                return true;
            break;
        default: break;
        }
    }
    if (m == Qt::ShiftModifier && !m_session->altScreen()) {
        switch (e->key()) {
        case Qt::Key_PageUp: scrollPages(-1); return true;
        case Qt::Key_PageDown: scrollPages(1); return true;
        case Qt::Key_Home: scrollToTop(); return true;
        case Qt::Key_End: scrollToBottom(); return true;
        default: break;
        }
    }
    if (m == Qt::ShiftModifier && e->key() == Qt::Key_Insert) {
        pasteSelection();
        return true;
    }
    return false;
}

void TerminalView::keyPressEvent(QKeyEvent *e)
{
    if (m_shortcutFilter && m_shortcutFilter(e)) {
        e->ignore();
        return;
    }
    if (m_builtinShortcuts && handleBuiltinShortcut(e))
        return;
    KeyInput k;
    if (!mapKeyEvent(e, &k, m_keyOptions)) {
        QWidget::keyPressEvent(e);
        return;
    }
    sendKey(k);
}

void TerminalView::keyReleaseEvent(QKeyEvent *e)
{
    // Release events matter only for the kitty keyboard protocol's event
    // reporting; the core drops them otherwise.
    KeyInput k;
    if (!e->isAutoRepeat() && mapKeyEvent(e, &k, m_keyOptions) && k.key != Key::None) {
        k.release = true;
        m_session->withCore([&](VtCore &c) { c.sendKey(k); });
    }
    QWidget::keyReleaseEvent(e);
}

void TerminalView::sendKey(const KeyInput &k)
{
    m_session->withCore([&](VtCore &c) { c.sendKey(k); });
    afterUserInput();
}

void TerminalView::afterUserInput()
{
    m_blinkOn = true;
    if (m_scrollOnKey) {
        const bool atBottom = m_session->withCore([](VtCore &c) { return c.viewportAtBottom(); });
        if (!atBottom)
            scrollToBottom();
    }
}

void TerminalView::inputMethodEvent(QInputMethodEvent *e)
{
    if (!e->commitString().isEmpty()) {
        const QString commit = e->commitString();
        m_session->withCore([&](VtCore &c) { c.sendText(commit); });
        afterUserInput();
    }
    m_preedit = e->preeditString();
    if (m_frame.cursorInViewport)
        update(QRect(0, m_padding + m_frame.cursor.row * m_ch, width(), m_ch));
    e->accept();
}

QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    // An open fold above the cursor moves its row on screen, and the input
    // method has to be told where the caret really is.
    const int cursorScreenRow = foldsVisible()
        ? std::max(0, std::min(screenRowOfReal(m_frame.viewportTop + m_frame.cursor.row), m_rows - 1))
        : m_frame.cursor.row;
    const QRect cursorRect = cellRect(cursorScreenRow, m_frame.cursor.col);
    switch (query) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImCursorRectangle:
    case Qt::ImAnchorRectangle:
        return cursorRect;
    case Qt::ImFont:
        return m_fonts[0];
    case Qt::ImHints:
        return int(Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase | Qt::ImhMultiLine);
    case Qt::ImCursorPosition:
    case Qt::ImAnchorPosition:
        return m_frame.cursor.col;
    case Qt::ImSurroundingText:
        if (m_frame.cursorInViewport && m_frame.cursor.row < int(m_frame.lines.size()))
            return m_frame.lines[size_t(m_frame.cursor.row)].text();
        return QString();
    case Qt::ImCurrentSelection:
        return QString();
    case Qt::ImMaximumTextLength:
        return QVariant();
    default:
        return QWidget::inputMethodQuery(query);
    }
}

// ---------------------------------------------------------------- mouse

bool TerminalView::mouseToProgram(Qt::KeyboardModifiers mods) const
{
    if (mods & Qt::ShiftModifier)
        return false; // Shift always selects, as in xterm/Konsole
    return m_session->withCore([](VtCore &c) { return c.mouseTracking() != MouseTracking::None; });
}

void TerminalView::sendMouse(QMouseEvent *e, int action)
{
    MouseInput m;
    m.action = MouseInput::Action(action);
    switch (e->button() != Qt::NoButton ? e->button() : (e->buttons() & Qt::LeftButton ? Qt::LeftButton
                                                         : e->buttons() & Qt::MiddleButton ? Qt::MiddleButton
                                                         : e->buttons() & Qt::RightButton ? Qt::RightButton
                                                                                          : Qt::NoButton)) {
    case Qt::LeftButton: m.button = MouseButton::Left; break;
    case Qt::MiddleButton: m.button = MouseButton::Middle; break;
    case Qt::RightButton: m.button = MouseButton::Right; break;
    default: m.button = MouseButton::None; break;
    }
    const CellPos c = cellAt(e->pos());
    m.row = c.row;
    m.col = c.col;
    m.x = float(std::max(0, e->pos().x() - m_padding));
    m.y = float(std::max(0, e->pos().y() - m_padding));
    m.modifiers = mapModifiers(e->modifiers(), m_keyOptions);
    m_session->withCore([&](VtCore &core) { core.sendMouse(m); });
}

void TerminalView::mousePressEvent(QMouseEvent *e)
{
    // A host that gave the view Qt::NoFocus keeps the keyboard elsewhere (Relay's prompt box):
    // clicking still selects, scrolls and follows links, but it does not grab the focus.
    if (focusPolicy() != Qt::NoFocus)
        setFocus(Qt::MouseFocusReason);
    if (mouseToProgram(e->modifiers())) {
        sendMouse(e, int(MouseInput::Action::Press));
        return;
    }
    const CellPos pos = cellAt(e->pos());
    if (e->button() == Qt::LeftButton) {
        endLinkWalk();
        m_pressedLink = Link();
        m_pressedFold.clear();
        // A fold anchor answers the click itself, before the "open this link"
        // path an OSC 8 URI would otherwise take -- with Ctrl too.
        const QString foldUri = foldAnchorAt(pos);
        if (!foldUri.isEmpty()) {
            if (e->modifiers() & Qt::ControlModifier) {
                toggleFold(foldUri);
                return;
            }
            if (e->modifiers() == Qt::NoModifier) {
                m_pressedFold = foldUri;
                m_pressedRow = pos.row;
            }
        }
        Link link;
        int s = 0, en = 0;
        const bool onLink = foldUri.isEmpty() && linkAt(pos, &link, &s, &en);
        if (onLink && (e->modifiers() & Qt::ControlModifier)) {
            emit linkActivated(link.target, link.line, link.column);
            return;
        }
        // A plain click opens on release, so that dragging from inside a path still
        // selects text (issue YZTK).
        if (onLink && m_plainClickOpens && e->modifiers() == Qt::NoModifier) {
            m_pressedLink = link;
            m_pressedRow = pos.row;
            m_pressedStart = s;
            m_pressedEnd = en;
        }
        const bool near = (e->pos() - m_lastClickPos).manhattanLength() < 6;
        if (m_lastClick.elapsed() < QApplication::doubleClickInterval() && near)
            m_clickCount = m_clickCount % 3 + 1;
        else
            m_clickCount = 1;
        m_lastClick.restart();
        m_lastClickPos = e->pos();
        const SelectionUnit unit = m_clickCount == 3 ? SelectionUnit::Line
            : m_clickCount == 2                      ? SelectionUnit::Word
                                                     : SelectionUnit::Cell;
        const bool rect = e->modifiers() & Qt::AltModifier;
        m_visualGesture = foldsVisible() && !rect;
        if (m_visualGesture) {
            beginVisualSelection(pos, unit);
        } else {
            clearVisualSelection();
            const int frameRow = frameRowClamped(pos.row);
            m_session->withCore([&](VtCore &c) { c.selectionBegin(frameRow, pos.col, unit, rect); });
        }
        m_selecting = true;
        m_selectionMoved = unit != SelectionUnit::Cell;
        scheduleFrame();
    } else if (e->button() == Qt::MiddleButton) {
        pasteSelection();
    }
}

void TerminalView::mouseDoubleClickEvent(QMouseEvent *e)
{
    // Normally a press at the same spot precedes this; if not (synthetic
    // events), make the double click count as the second click.
    if ((e->pos() - m_lastClickPos).manhattanLength() >= 6 || m_lastClick.elapsed() >= QApplication::doubleClickInterval()) {
        m_clickCount = 1;
        m_lastClickPos = e->pos();
        m_lastClick.restart();
    }
    mousePressEvent(e);
}

void TerminalView::mouseMoveEvent(QMouseEvent *e)
{
    m_lastMousePos = e->pos();
    if (!m_selecting && mouseToProgram(e->modifiers())) {
        const MouseTracking t = m_session->withCore([](VtCore &c) { return c.mouseTracking(); });
        if (t == MouseTracking::Move || (t == MouseTracking::Drag && e->buttons() != Qt::NoButton))
            sendMouse(e, int(MouseInput::Action::Motion));
        return;
    }
    if (m_selecting && (e->buttons() & Qt::LeftButton)) {
        const CellPos pos = cellAt(e->pos());
        if (m_visualGesture && foldsVisible()) {
            extendVisualSelection(pos);
        } else {
            const int frameRow = frameRowClamped(pos.row);
            m_session->withCore([&](VtCore &c) { c.selectionExtend(frameRow, pos.col); });
        }
        m_selectionMoved = true;
        if (e->pos().y() < m_padding || e->pos().y() >= height() - m_padding)
            m_autoScroll.start();
        else
            m_autoScroll.stop();
        scheduleFrame();
        return;
    }
    updateHover(e->pos(), e->modifiers());
}

void TerminalView::autoScrollTick()
{
    if (!m_selecting) {
        m_autoScroll.stop();
        return;
    }
    const bool up = m_lastMousePos.y() < m_padding;
    const CellPos pos = cellAt(m_lastMousePos);
    scrollLines(up ? -1 : 1);
    const int frameRow = frameRowClamped(up ? 0 : m_rows - 1);
    m_session->withCore([&](VtCore &c) { c.selectionExtend(frameRow, pos.col); });
    scheduleFrame();
}

void TerminalView::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_selecting && mouseToProgram(e->modifiers())) {
        sendMouse(e, int(MouseInput::Action::Release));
        return;
    }
    if (!m_selecting)
        return;
    m_selecting = false;
    m_autoScroll.stop();
    if (!m_selectionMoved) {
        clearVisualSelection();
        m_session->withCore([](VtCore &c) { c.selectionClear(); });
        scheduleFrame();
        // A plain click on an anchor that neither dragged nor left it toggles
        // the fold: the view opens or shuts it, or asks the host for the detail.
        if (!m_pressedFold.isEmpty() && e->button() == Qt::LeftButton) {
            const QString uri = m_pressedFold;
            m_pressedFold.clear();
            if (cellAt(e->pos()).row == m_pressedRow)
                toggleFold(uri);
            return;
        }
        // A click that neither dragged nor left the link follows it.
        if (m_pressedLink.valid() && e->button() == Qt::LeftButton) {
            const CellPos pos = cellAt(e->pos());
            const Link link = m_pressedLink;
            m_pressedLink = Link();
            if (pos.row == m_pressedRow && pos.col >= m_pressedStart && pos.col <= m_pressedEnd)
                emit linkActivated(link.target, link.line, link.column);
        }
        return;
    }
    m_pressedLink = Link();
    m_pressedFold.clear();
    if (m_copyOnSelect && QApplication::clipboard()->supportsSelection()) {
        const QString text = selectedText();
        if (copyOnSelectWorthCopying(text))
            QApplication::clipboard()->setText(text, QClipboard::Selection);
    }
}

void TerminalView::wheelEvent(QWheelEvent *e)
{
    m_wheelRemainder += e->angleDelta().y();
    const int steps = m_wheelRemainder / 120;
    m_wheelRemainder -= steps * 120;
    if (steps == 0)
        return;
    const Qt::KeyboardModifiers mods = e->modifiers();
    if (mouseToProgram(mods)) {
        const CellPos c = cellAt(e->position().toPoint());
        for (int i = 0; i < std::abs(steps); ++i) {
            MouseInput m;
            m.action = MouseInput::Action::Press;
            m.button = steps > 0 ? MouseButton::WheelUp : MouseButton::WheelDown;
            m.row = c.row;
            m.col = c.col;
            m.modifiers = mapModifiers(mods, m_keyOptions);
            m_session->withCore([&](VtCore &core) { core.sendMouse(m); });
        }
        return;
    }
    if (m_session->altScreen()) {
        // Alternate scroll: full-screen programs without mouse reporting get arrow keys.
        KeyInput k;
        k.key = steps > 0 ? Key::Up : Key::Down;
        for (int i = 0; i < 3 * std::abs(steps); ++i)
            m_session->withCore([&](VtCore &c) { c.sendKey(k); });
        return;
    }
    scrollLines(-3 * steps);
}

void TerminalView::updateHover(const QPoint &pos, Qt::KeyboardModifiers)
{
    // Hovering a path underlines it and shows where it points, with or without Ctrl
    // (issue YZTK). The scan is per cell, so moving inside one cell costs nothing.
    if (m_linkCursor.active())
        return; // the keyboard walk owns the underline until it ends
    const bool inside = rect().contains(pos);
    const CellPos c = inside ? cellAt(pos) : CellPos{-1, -1};
    if (c.row == m_hoverCellRow && c.col == m_hoverCellCol)
        return;
    m_hoverCellRow = c.row;
    m_hoverCellCol = c.col;
    int newRow = -1, newStart = -1, newEnd = -1;
    Link link;
    if (inside) {
        int s = -1, en = -1;
        if (linkAt(c, &link, &s, &en)) {
            newRow = c.row;
            newStart = s;
            newEnd = en;
        }
    }
    QString tip;
    if (!link.card.isEmpty()) {
        // A card reference says which card it opens, not the relay://card/<id> behind it.
        tip = QStringLiteral("#") + link.card;
        if (!link.cardTitle.isEmpty())
            tip += QStringLiteral(" · ") + link.cardTitle;
    } else if (link.valid()) {
        tip = link.target;
        if (link.line > 0)
            tip += QLatin1Char(':') + QString::number(link.line);
        if (link.directory)
            tip = tr("%1 (folder)").arg(tip);
    }
    if (tip != toolTip())
        setToolTip(tip);
    if (newRow == m_hoverRow && newStart == m_hoverStart && newEnd == m_hoverEnd)
        return;
    if (m_hoverRow >= 0)
        update(QRect(0, m_padding + m_hoverRow * m_ch, width(), m_ch));
    m_hoverRow = newRow;
    m_hoverStart = newStart;
    m_hoverEnd = newEnd;
    if (m_hoverRow >= 0)
        update(QRect(0, m_padding + m_hoverRow * m_ch, width(), m_ch));
    setCursor(m_hoverRow >= 0 ? Qt::PointingHandCursor : Qt::IBeamCursor);
}

// The directory this frame's rows are scanned against. One resolution per
// frame: currentDirectory() takes the core's mutex — the same one the pty
// thread holds while it feeds each chunk — and then makes a filesystem round
// trip, and restLinkColumns() wanted it for every painted row, which was 91 %
// of the GUI thread's `statx` during output and ~4 400 lock acquisitions a
// second (#6W0Z). Invalidated at the top of every pullFrame(), and by
// linkProbeUpdated() when the host changes what a path resolves against.
const QString &TerminalView::frameDirectory()
{
    if (!m_frameCwdValid) {
        m_frameCwd = currentDirectory();
        m_frameCwdValid = true;
    }
    return m_frameCwd;
}

QString TerminalView::currentDirectory() const
{
    ++m_cwdResolves;
    // A pane logged into another machine resolves the output's relative paths against the folder
    // the remote shell is in, not the one this process happens to be in (#S5SH).
    if (m_linkDirectory) {
        const QString remote = m_linkDirectory();
        if (!remote.isEmpty())
            return remote;
    }
    QString dir = m_session->currentDirectory();
    if (!dir.isEmpty() && QFileInfo(dir).isDir())
        return dir;
#if defined(Q_OS_LINUX)
    qint64 pid = m_session->foregroundPid();
    if (pid <= 0)
        pid = m_session->shellPid();
    if (pid > 0) {
        const QString target = QFileInfo(QStringLiteral("/proc/%1/cwd").arg(pid)).symLinkTarget();
        if (!target.isEmpty())
            return target;
    }
#endif
    return QDir::currentPath();
}

bool TerminalView::splitPathToken(const QString &token, QString *path, int *line, int *column)
{
    return links::splitLocation(token, path, line, column);
}

void TerminalView::setCardLookup(links::CardLookup lookup)
{
    m_cardLookup = std::move(lookup);
    // The hover is cached per cell and the walk's list is built once: both were scanned without
    // the board, so a pane that has just learnt its cards re-reads them on the next move.
    m_hoverCellRow = m_hoverCellCol = -2;
    endLinkWalk();
}

void TerminalView::setLinkProbe(links::Probe probe, std::function<QString()> directory)
{
    m_linkProbe = std::move(probe);
    m_linkDirectory = std::move(directory);
    linkProbeUpdated();
}

void TerminalView::linkProbeUpdated()
{
    // What is underlined was worked out with the old answers: drop the per-cell hover cache and
    // the walk's list so the next move or scan asks again. A walk the user is in the middle of
    // keeps its place — answers arrive while they are stepping through it.
    m_hoverCellRow = m_hoverCellCol = -2;
    m_restLinks.clear();
    m_frameCwdValid = false;   // the host may have changed what paths resolve against
    m_forceFull = true;
    update();
    if (!m_linkCursor.active())
        endLinkWalk();
    // And read the cell the pointer is already on again, so a path the host has just vouched for
    // underlines itself under a motionless pointer rather than waiting for the next wobble.
    if (underMouse())
        updateHover(mapFromGlobal(QCursor::pos()), QApplication::keyboardModifiers());
}

// The logical line the cell belongs to: soft-wrapped rows of the viewport joined into one
// string, with the (row, column) each UTF-16 unit came from.
namespace {
struct LogicalRow {
    QString text;
    std::vector<std::pair<int, int>> cellOf;
};

// The logical line frame row `row` belongs to — its soft-wrapped rows joined — and, for each
// character of it, the (frame row, column) it came from. Long URLs and paths wrap at the edge.
void logicalRowAt(const ViewportFrame &frame, int row, LogicalRow *out)
{
    int firstRow = row, lastRow = row;
    while (firstRow > 0 && frame.lines[size_t(firstRow)].continuation)
        --firstRow;
    while (lastRow + 1 < int(frame.lines.size()) && frame.lines[size_t(lastRow + 1)].continuation)
        ++lastRow;
    for (int r = firstRow; r <= lastRow; ++r) {
        const Line &rowLine = frame.lines[size_t(r)];
        const int n = r < lastRow ? frame.columns : int(rowLine.cells.size());
        for (int i = 0; i < n; ++i) {
            const Cell cell = i < int(rowLine.cells.size()) ? rowLine.cells[size_t(i)] : Cell();
            if (cell.ch == kWideTail)
                continue;
            const QString text = rowLine.cellText(cell);
            for (int k = 0; k < text.size(); ++k)
                out->cellOf.push_back({r, i});
            out->text += text;
        }
    }
}
} // namespace

// A markdown link's label, resolved (card #MDKN). The URI on the cell is the block's own anchor
// with the target the agent wrote after `#l=`; what that target *is* — a card, a row of Options, a
// saved conversation, a file at a line, a web page — is the same question `relay::links` answers
// about a span of text, and it is answered the same way here so that a label and the `(target)`
// printed beside it open identically, right down to the context menu the host builds from `card`
// and `url`.
bool TerminalView::resolveLabelLink(const QString &uri, Link *link)
{
    const QString raw = relay::labellink::targetOf(uri);
    if (raw.isEmpty())
        return false;
    // A web link first, and without the scanner: `candidates()` splits a URL at a bracket or a
    // trailing quote, and an agent writes those in a markdown target where they are not
    // punctuation. Everything else is spelled the way the transcript spells it.
    if (raw.startsWith(QLatin1String("http://")) || raw.startsWith(QLatin1String("https://"))
        || raw.startsWith(QLatin1String("mailto:"))) {
        link->target = raw;
        link->text = raw;
        link->url = true;
        return true;
    }
    for (const links::Found &found : links::scan(raw, currentDirectory(), QDir::homePath(),
                                                 m_linkProbe ? m_linkProbe : links::systemProbe(),
                                                 m_cardLookup, links::Mode::Prose)) {
        // The whole target, not a word inside it: `[x](notes about src/a.c)` is not a link.
        if (found.candidate.start != 0 || found.candidate.length != raw.size())
            continue;
        link->target = found.target.target;
        link->text = raw;
        link->card = found.target.kind == links::Kind::Card ? found.candidate.path : QString();
        link->cardTitle = found.target.label;
        link->url = found.target.kind == links::Kind::Url;
        link->directory = found.target.directory;
        link->line = found.target.line;
        link->column = found.target.column;
        return true;
    }
    return false;
}

bool TerminalView::linkAt(const CellPos &c, Link *link, int *startCol, int *endCol)
{
    *link = Link();
    // A screen row may be one of a fold's own rows; those carry the FoldSpan
    // links the host put there, not the emulator's cells.
    {
        int foldStart = 0, foldEnd = 0;
        const QString target = foldLinkAt(c, &foldStart, &foldEnd);
        // A re-wrapped prose block and a markdown fold are both rows of FoldSpans, and a link's
        // label there carries the same URI its cells would carry in the grid (#MDKN).
        if (relay::labellink::isLabelUri(target)) {
            if (!resolveLabelLink(target, link))
                return false;
            *startCol = foldStart;
            *endCol = foldEnd;
            return true;
        }
        if (!target.isEmpty()) {
            link->text = target;
            const QString local = target.startsWith(QLatin1String("file://")) ? QUrl(target).toLocalFile() : target;
            if (!local.isEmpty() && QFileInfo::exists(local)) {
                link->target = local;
                link->directory = QFileInfo(local).isDir();
            } else {
                link->target = target;
                link->url = true;
            }
            *startCol = foldStart;
            *endCol = foldEnd;
            return true;
        }
    }
    const int row = frameRowOf(c.row);
    if (row < 0 || row >= int(m_frame.lines.size()))
        return false;
    const Line &l = m_frame.lines[size_t(row)];

    // An OSC 8 hyperlink: the program itself said what the text points at.
    // A prose anchor (#R2WQ) is not a link — it only names the block the view
    // re-wraps — so the row reads as ordinary text and the scan below still
    // finds the paths and URLs inside it. A prose URI that carries a `#l=`
    // fragment is the exception: that is a markdown link's label, and the
    // fragment is what it opens (#MDKN).
    const QString uri = m_session->withCore([&](VtCore &core) { return core.hyperlinkAt(row, c.col); });
    const bool labelHere = relay::labellink::isLabelUri(uri) && c.col < int(l.cells.size());
    // The cells of the run under the pointer, whichever kind it is.
    const auto runOf = [&](int *from, int *to) {
        const uint32_t id = l.cells[size_t(c.col)].link;
        *from = c.col;
        *to = c.col;
        while (*from > 0 && l.cells[size_t(*from - 1)].link == id && id)
            --*from;
        while (*to + 1 < int(l.cells.size()) && l.cells[size_t(*to + 1)].link == id && id)
            ++*to;
    };
    if (labelHere) {
        int s = 0, e = 0;
        runOf(&s, &e);
        if (resolveLabelLink(uri, link)) {
            *startCol = s;
            *endCol = e;
            return true;
        }
        // A target that resolves to nothing — a path that is not there — leaves the label the
        // plain text it was before this existed, and the scan below still reads the row.
    }
    if (!uri.isEmpty() && !labelHere && !FoldLayer::isProseUri(uri) && c.col < int(l.cells.size())) {
        int s = 0, e = 0;
        runOf(&s, &e);
        link->text = uri;
        // file:// hyperlinks are local paths, so they open in a Relay pane like any other.
        const QString local = uri.startsWith(QLatin1String("file://")) ? QUrl(uri).toLocalFile() : QString();
        if (!local.isEmpty() && QFileInfo::exists(local)) {
            link->target = local;
            link->directory = QFileInfo(local).isDir();
        } else {
            link->target = uri;
            link->url = true;
        }
        *startCol = s;
        *endCol = e;
        return true;
    }

    // Plain text, joined across the soft-wrapped rows of the viewport (long URLs and
    // paths wrap at the terminal edge).
    LogicalRow logical;
    logicalRowAt(m_frame, row, &logical);
    int idx = -1;
    for (int i = 0; i < int(logical.cellOf.size()); ++i) {
        if (logical.cellOf[size_t(i)].first == row && logical.cellOf[size_t(i)].second == c.col) {
            idx = i;
            break;
        }
    }
    if (idx < 0 || idx >= logical.text.size())
        return false;
    // The row under the pointer is Relay-printed prose when its cells carry the block's
    // relay://prose/ anchor (#R2WQ; `uri` is empty at an unlinked cell, and a non-prose
    // hyperlink returned above). Prose scans in Prose mode (#SFZC): a bare folder word is
    // plain text there, a folder links only with its slash.
    const links::Mode mode = FoldLayer::isProseUri(uri) ? links::Mode::Prose : links::Mode::Program;
    for (const links::Found &found : links::scan(logical.text, currentDirectory(), QDir::homePath(),
                                                 m_linkProbe ? m_linkProbe : links::systemProbe(), m_cardLookup,
                                                 mode)) {
        const int s = found.candidate.start;
        const int e = s + found.candidate.length - 1;
        if (idx < s || idx > e || e >= int(logical.cellOf.size()))
            continue;
        link->target = found.target.target;
        link->text = found.candidate.text;
        link->card = found.target.kind == links::Kind::Card ? found.candidate.path : QString();
        link->cardTitle = found.target.label;
        link->url = found.target.kind == links::Kind::Url;
        link->directory = found.target.directory;
        link->line = found.target.line;
        link->column = found.target.column;
        // The hover underline is per row: clip the span to the row under the pointer.
        *startCol = logical.cellOf[size_t(s)].first == row ? logical.cellOf[size_t(s)].second : 0;
        *endCol = logical.cellOf[size_t(e)].first == row ? logical.cellOf[size_t(e)].second : m_frame.columns - 1;
        return true;
    }
    return false;
}

void TerminalView::setLinksColouredAtRest(bool on)
{
    if (m_linksAtRest == on)
        return;
    m_linksAtRest = on;
    m_restLinks.clear();
    m_forceFull = true;
    update();
}

// Is this link id a prose anchor (#R2WQ)? Memoised for the frame: the rows of
// one block all carry the same id, so a screenful of prose costs one lookup.
bool TerminalView::proseLink(uint32_t link, int frameRow, int col)
{
    for (const std::pair<uint32_t, bool> &known : m_frameProse) {
        if (known.first == link)
            return known.second;
    }
    const bool prose = FoldLayer::isProseUri(
        m_session->withCore([&](VtCore &core) { return core.hyperlinkUri(link, frameRow, col); }));
    m_frameProse.push_back({link, prose});
    return prose;
}

void TerminalView::restLinkColumns(int frameRow, std::vector<char> *cols)
{
    cols->clear();
    if (frameRow < 0 || frameRow >= int(m_frame.lines.size()))
        return;
    const Line &line = m_frame.lines[size_t(frameRow)];
    if (line.cells.empty())
        return;
    LogicalRow logical;
    logicalRowAt(m_frame, frameRow, &logical);
    if (logical.text.trimmed().isEmpty())
        return;
    // No cheap pre-filter on the text: a bare name (`docs`, `relay-terminal`) is a candidate the
    // scanner resolves against the directory, exactly as the pointer would, and an `ls` of
    // extension-less folders is the commonest link-bearing line there is. The cache keeps the
    // probe cost to one scan per changed line.
    if (m_restLinks.size() > 4096 || (m_restLinksAge.isValid() && m_restLinksAge.elapsed() > 5000))
        m_restLinks.clear();
    if (m_restLinks.isEmpty())
        m_restLinksAge.start();
    // #SFZC: a Relay prose row — every cell of the block's rows carries its relay://prose/
    // anchor (#R2WQ) — is scanned in Prose mode, where a bare folder word stays plain text.
    // Program rows keep today's rule: an `ls` of extension-less folders is the commonest
    // link-bearing line there is.
    bool prose = false;
    for (int col = 0; col < int(line.cells.size()); ++col) {
        const uint32_t link = line.cells[size_t(col)].link;
        if (!link)
            continue;
        // The id is already here, on the cell: ask for its URI rather than for
        // the row's, which converted every cell of the row to hand back this
        // same id (#6W0Z). An id's URI cannot change, so one lookup per link id
        // per frame answers every row of a prose block.
        prose = proseLink(link, frameRow, col);
        break;
    }
    const QString &cwd = frameDirectory();
    // The mode is part of the key: the same text is a link on a program row and plain on a
    // prose one, and the cache must not carry the answer of one to the other.
    const QString key = (prose ? QStringLiteral("prose\n") : QStringLiteral("out\n")) + cwd + QLatin1Char('\n')
                        + logical.text;
    auto it = m_restLinks.constFind(key);
    if (it == m_restLinks.constEnd()) {
        QVector<QPair<int, int>> spans;
        for (const links::Found &found : links::scan(logical.text, cwd, QDir::homePath(),
                                                     m_linkProbe ? m_linkProbe : links::systemProbe(), m_cardLookup,
                                                     prose ? links::Mode::Prose : links::Mode::Program))
            spans.append({found.candidate.start, found.candidate.start + found.candidate.length - 1});
        it = m_restLinks.insert(key, spans);
    }
    if (it->isEmpty())
        return;
    cols->assign(line.cells.size(), 0);
    for (const QPair<int, int> &span : *it) {
        for (int i = span.first; i <= span.second && i < int(logical.cellOf.size()); ++i) {
            const auto &[r, col] = logical.cellOf[size_t(i)];
            if (r != frameRow || col < 0 || col >= int(cols->size()))
                continue;
            (*cols)[size_t(col)] = 1;
            // A wide character's tail cell too, so the run has no gap.
            if (col + 1 < int(line.cells.size()) && line.cells[size_t(col + 1)].ch == kWideTail)
                (*cols)[size_t(col + 1)] = 1;
        }
    }
}

TerminalView::Link TerminalView::linkAtPoint(const QPoint &pos)
{
    Link link;
    int start = -1, end = -1;
    linkAt(cellAt(pos), &link, &start, &end);
    return link;
}

// ---------------------------------------------------------------- keyboard link walk

// Enough to cover a long build log without scanning a full 100 000-line scrollback on
// every Ctrl+Shift+L, and a cap on how many links one walk holds.
static constexpr int kWalkScrollbackLines = 2000;
static constexpr int kWalkMaxLinks = 500;

void TerminalView::collectLinks()
{
    m_linkWalk.clear();
    QStringList rows;
    int historyRows = 0, columns = 80;
    std::vector<VtCore::HyperlinkRun> proseRuns;
    m_session->withCore([&](VtCore &core) {
        historyRows = core.historyRows();
        columns = std::max(1, core.columns());
        rows = core.historyText(kWalkScrollbackLines);
        const QString screen = core.screenText();
        rows += screen.split(QLatin1Char('\n'));
        // The rows Relay printed as prose blocks (#R2WQ): they scan in Prose mode (#SFZC),
        // where a bare folder word is not a link. One walk per keypress, not per frame,
        // which is the pace hyperlinkRuns() asks for.
        proseRuns = core.hyperlinkRuns(QString::fromLatin1(kProsePrefix));
    });
    auto rowIsProse = [&proseRuns](int row) {
        for (const VtCore::HyperlinkRun &run : proseRuns)
            if (row >= run.startRow && row <= run.endRow)
                return true;
        return false;
    };
    // historyText() returns the newest lines, so the first row it gave us sits this far
    // down the scrollback; screen row k follows at historyRows + k.
    const int firstRow = std::max(0, historyRows - int(rows.size()));
    const QString cwd = currentDirectory();
    const QString home = QDir::homePath();
    const links::Probe probe = m_linkProbe ? m_linkProbe : links::systemProbe();
    const links::CardLookup cardLookup = m_cardLookup;
    for (int i = 0; i < rows.size() && int(m_linkWalk.size()) < kWalkMaxLinks;) {
        // Rows the emulator filled to the last column continue on the next row: a path
        // that wrapped is one logical line again.
        int last = i;
        QString text = rows[i];
        while (last + 1 < rows.size() && rows[last].size() >= columns) {
            ++last;
            text += rows[last];
        }
        // A logical line any of whose rows sits inside a prose block scans as prose; the
        // block's rows all carry the anchor, so the soft-wrapped tail of a prose line does
        // too. Program output around it keeps the terminal rule.
        bool prose = false;
        for (int r = i; r <= last && !prose; ++r)
            prose = rowIsProse(firstRow + r);
        for (const links::Found &found : links::scan(text, cwd, home, probe, cardLookup,
                                                     prose ? links::Mode::Prose : links::Mode::Program)) {
            WalkLink walk;
            const int s = found.candidate.start;
            const int e = s + found.candidate.length - 1;
            walk.row = firstRow + i + s / columns;
            walk.col = s % columns;
            walk.endRow = firstRow + i + e / columns;
            walk.endCol = e % columns;
            walk.link.target = found.target.target;
            walk.link.text = found.candidate.text;
            walk.link.card = found.target.kind == links::Kind::Card ? found.candidate.path : QString();
            walk.link.cardTitle = found.target.label;
            walk.link.url = found.target.kind == links::Kind::Url;
            walk.link.directory = found.target.directory;
            walk.link.line = found.target.line;
            walk.link.column = found.target.column;
            m_linkWalk.push_back(walk);
            if (int(m_linkWalk.size()) >= kWalkMaxLinks)
                break;
        }
        i = last + 1;
    }
}

void TerminalView::showWalkLink(const WalkLink &walk)
{
    // Put the link in the viewport, a third of the way down when it has to scroll.
    int top = m_session->withCore([](VtCore &core) { return core.viewportTop(); });
    if (walk.row < top || walk.endRow > top + m_rows - 1) {
        scrollToRow(std::max(0, walk.row - m_rows / 3));
        if (foldsVisible()) {
            m_forceFull = true;
            pullFrame(); // settle the core's viewport before rows are counted off it
        }
        top = m_session->withCore([](VtCore &core) { return core.viewportTop(); });
    }
    const int row = walk.row - top;
    const int endRow = walk.endRow - top;
    m_session->withCore([&](VtCore &core) {
        core.selectionBegin(row, walk.col, SelectionUnit::Cell, false);
        core.selectionExtend(endRow, walk.endCol);
    });
    // The underline the mouse draws, for the row the link starts on (a screen
    // row: an open fold above it may have pushed it down).
    const int screenRow = screenRowOfReal(walk.row);
    m_hoverRow = screenRow >= 0 && screenRow < m_rows ? screenRow : -1;
    m_hoverStart = walk.col;
    m_hoverEnd = walk.row == walk.endRow ? walk.endCol : m_frame.columns - 1;
    m_hoverCellRow = m_hoverCellCol = -2;
    m_forceFull = true;
    scheduleFrame();
}

bool TerminalView::stepLink(int delta, Link *link)
{
    // The list is built when the walk starts and kept while it lasts, so repeated presses
    // move through the same links even as the program keeps printing.
    if (!m_linkCursor.active())
        collectLinks();
    m_linkCursor.setCount(int(m_linkWalk.size()));
    const int index = m_linkCursor.step(delta);
    if (index < 0 || index >= int(m_linkWalk.size())) {
        endLinkWalk();
        return false;
    }
    const WalkLink &walk = m_linkWalk[size_t(index)];
    showWalkLink(walk);
    if (link)
        *link = walk.link;
    return true;
}

void TerminalView::endLinkWalk()
{
    if (!m_linkCursor.active() && m_linkWalk.empty())
        return;
    m_linkCursor.cancel();
    m_linkWalk.clear();
    m_session->withCore([](VtCore &core) { core.selectionClear(); });
    m_hoverRow = m_hoverStart = m_hoverEnd = -1;
    m_hoverCellRow = m_hoverCellCol = -2;
    m_forceFull = true;
    scheduleFrame();
}

// ---------------------------------------------------------------- folds
//
// A fold is a block of virtual rows the view lays under the row its OSC 8
// anchor ends on. Neither core can be asked to hold those rows, so everything
// about them lives here and in FoldLayer: the view keeps `m_visualTop` (the
// first visual row on screen) and drives the core's own viewport to whatever
// covers the real rows that window needs. With no fold open the two are the
// same number and every path below is the one that was here before.

FoldLayer::VisualRow TerminalView::visualAt(int screenRow) const
{
    if (!foldsVisible()) {
        FoldLayer::VisualRow v;
        v.realRow = m_frame.viewportTop + screenRow;
        return v;
    }
    return m_folds.at(m_visualTop + screenRow);
}

int TerminalView::frameRowOf(int screenRow) const
{
    if (!foldsVisible())
        return screenRow;
    const FoldLayer::VisualRow v = m_folds.at(m_visualTop + screenRow);
    if (v.fold)
        return -1;
    const int frameRow = v.realRow - m_frame.viewportTop;
    return frameRow >= 0 && frameRow < int(m_frame.lines.size()) ? frameRow : -1;
}

int TerminalView::frameRowClamped(int screenRow) const
{
    if (!foldsVisible())
        return std::max(0, std::min(screenRow, std::max(0, m_frame.rows - 1)));
    for (int r = screenRow; r >= 0; --r) {
        const int frameRow = frameRowOf(r);
        if (frameRow >= 0)
            return frameRow;
    }
    for (int r = screenRow + 1; r < m_rows; ++r) {
        const int frameRow = frameRowOf(r);
        if (frameRow >= 0)
            return frameRow;
    }
    return 0;
}

int TerminalView::screenRowOfReal(int realRow) const
{
    if (!foldsVisible())
        return realRow - m_frame.viewportTop;
    return m_folds.visualOfReal(realRow) - m_visualTop;
}

// Ask the core where every fold anchor is now. Called when the grid was
// resized (both cores reflow), when a fold was added or toggled, and on a
// 250 ms throttle while any fold is open, which is what catches the scrollback
// trimming its oldest lines away underneath us. A fold that once had an anchor
// and no longer does is dropped; one that never had an anchor yet is kept,
// because the host may set a call's content before its line is printed.
void TerminalView::resolveFoldAnchors()
{
    m_foldAnchorsDirty = false;
    m_contentMoved = false;
    m_foldResolveAt.restart();
    const QString prefix = m_folds.prefix();
    if (m_folds.folds().empty())
        return;
    // Two anchor families live in the grid: fold anchors (relay://call/) and
    // prose anchors (relay://prose/, #R2WQ), each its own OSC 8 run. One walk
    // per prefix, only for the prefixes something is waiting on.
    bool anyProse = false;
    for (const FoldLayer::Fold &f : m_folds.folds()) {
        if (f.replacement) {
            anyProse = true;
            break;
        }
    }
    std::vector<VtCore::HyperlinkRun> runs;
    if (!prefix.isEmpty())
        runs = m_session->withCore([&](VtCore &c) { return c.hyperlinkRuns(prefix); });
    if (anyProse) {
        const std::vector<VtCore::HyperlinkRun> prose =
            m_session->withCore([](VtCore &c) {
                return c.hyperlinkRuns(QString::fromLatin1(kProsePrefix));
            });
        runs.insert(runs.end(), prose.begin(), prose.end());
    }

    // One batch: the anchors, then the folds to keep, then a single rebuild of
    // the layout (#PPR4 — this used to rebuild once per anchor and test
    // membership against a list).
    QSet<QString> seen;
    std::vector<FoldLayer::AnchorRows> anchors;
    anchors.reserve(runs.size());
    const int before = m_folds.visualRows();
    // A markdown link's label inside a prose block carries the block's own URI with the target as
    // a fragment (#MDKN), which breaks the block's run into pieces — the cells before the label,
    // the label, the cells after it — and a label can be the whole of the block's first or last
    // grid row. The pieces are one anchor: the block's rows are their union, and without the union
    // the layer would hide the wrong rows on a resize (the last piece wins otherwise).
    QHash<QString, int> at;
    for (const VtCore::HyperlinkRun &r : runs) {
        const QString uri = relay::labellink::anchorOf(r.uri);
        if (!m_folds.known(uri))
            continue;
        const auto it = at.constFind(uri);
        if (it == at.constEnd()) {
            at.insert(uri, int(anchors.size()));
            anchors.push_back(FoldLayer::AnchorRows{uri, r.startRow, r.endRow});
            seen.insert(uri);
            continue;
        }
        FoldLayer::AnchorRows &have = anchors[size_t(*it)];
        have.startRow = std::min(have.startRow, r.startRow);
        have.endRow = std::max(have.endRow, r.endRow);
    }
    for (const FoldLayer::Fold &f : m_folds.folds()) {
        if (f.anchorStartRow < 0)
            seen.insert(f.uri); // never anchored: the line may still be on its way
    }
    m_folds.applyAnchors(anchors, seen);
    // The anchors, and with them the visual rows every fold match sits on, may
    // have moved (output, trimming, a dropped fold). Recomputing is lazy and
    // costs nothing while no needle is set; the match list itself usually comes
    // back identical, which keeps the walk where it was.
    m_foldSearch.invalidate();
    if (m_folds.visualRows() != before) {
        m_forceFull = true;
        refreshSearchLabel();
        update();
    }
}

void TerminalView::invalidateFoldAnchors()
{
    m_foldAnchorsDirty = true;
    m_forceFull = true;
    scheduleFrame();
}

// Clamp the visual window and put the core's viewport wherever it has to be for
// the frame to contain every real row that window shows. Runs with the session
// lock held, right after updateFrame(), so at most one extra frame copy is
// needed when the window moved.
void TerminalView::syncFoldViewport(VtCore &core, bool *changed)
{
    if (!foldsVisible()) {
        m_visualTop = m_frame.viewportTop;
        m_followBottom = core.viewportAtBottom();
        m_paintedVisualTop = m_visualTop;
        return;
    }
    const int maxTop = maxVisualTop();
    const int wanted = m_followBottom ? maxTop : std::max(0, std::min(m_visualTop, maxTop));
    m_visualTop = wanted;

    // The core's viewport is `rows` real rows wide and the window can never
    // show more than that, so covering its first real row covers them all.
    int first = -1;
    for (int i = 0; i < m_rows; ++i) {
        const FoldLayer::VisualRow v = m_folds.at(m_visualTop + i);
        if (!v.fold) {
            first = v.realRow;
            break;
        }
    }
    if (first < 0) {
        // The whole window sits inside one fold: keep the core on its anchor.
        const FoldLayer::VisualRow v = m_folds.at(m_visualTop);
        first = v.fold ? m_folds.folds()[size_t(v.foldIndex)].anchorRow : m_visualTop;
    }
    const int want = std::max(0, std::min(first, m_frame.historyRows));
    if (want != m_frame.viewportTop) {
        core.scrollViewportToRow(want);
        core.updateFrame(&m_frame, true);
        *changed = true;
    }
    if (m_visualTop != m_paintedVisualTop) {
        *changed = true;
        // Every screen row now shows something else: this frame repaints whole.
        m_visualTopMoved = true;
    }
    m_paintedVisualTop = m_visualTop;
}

// Toggling a fold leaves the line that was clicked where it was on screen --
// the block grows downwards under it -- unless that would push the cursor row
// off the bottom, in which case the older rows go up instead and the prompt
// stays in sight, which is what a terminal sitting at the bottom does.
void TerminalView::keepFoldAnchorInPlace(int anchorRow, int screenRow)
{
    if (anchorRow < 0 || screenRow < 0 || screenRow >= m_rows)
        return;
    int top = m_folds.visualOfReal(anchorRow) - screenRow;
    if (m_frame.cursorInViewport) {
        const int cursorVisual = m_folds.visualOfReal(m_frame.viewportTop + m_frame.cursor.row);
        top = std::max(top, cursorVisual - m_rows + 1);
    }
    const int maxTop = maxVisualTop();
    m_visualTop = std::max(0, std::min(top, maxTop));
    m_followBottom = m_visualTop >= maxTop;
}

void TerminalView::setVisualTop(int top)
{
    const int maxTop = maxVisualTop();
    m_visualTop = std::max(0, std::min(top, maxTop));
    m_followBottom = m_visualTop >= maxTop;
    m_forceFull = true;
    scheduleFrame();
}

QString TerminalView::foldAnchorAt(const CellPos &c) const
{
    if (!m_frame.altScreen && !m_folds.prefix().isEmpty()) {
        const int frameRow = frameRowOf(c.row);
        if (frameRow >= 0) {
            const QString uri = m_session->withCore([&](VtCore &core) { return core.hyperlinkAt(frameRow, c.col); });
            if (m_folds.isAnchorUri(uri))
                return uri;
        }
    }
    return QString();
}

void TerminalView::setFoldPrefix(const QString &uriPrefix)
{
    if (m_folds.prefix() == uriPrefix)
        return;
    m_folds.setPrefix(uriPrefix);
    m_folds.setGeometry(m_cols, m_folds.indent());
    if (!m_foldResolveAt.isValid())
        m_foldResolveAt.start();
    invalidateFoldAnchors();
}

void TerminalView::setFoldIndent(int cells)
{
    if (m_folds.setGeometry(m_cols, std::max(2, std::min(4, cells)))) {
        m_foldSearch.invalidate();
        m_forceFull = true;
        scheduleFrame();
    }
}

// A prose block (#R2WQ): laid out like a fold but painted as grid rows, and
// only while the pane is not at the width the block was printed at.
void TerminalView::setProseBlock(const QString &uri, const QVector<FoldLine> &lines, int printColumns)
{
    if (uri.isEmpty())
        return;
    const int anchor = m_folds.known(uri) ? m_folds.fold(uri)->anchorRow : -1;
    const int keep = anchor >= 0 ? screenRowOfReal(anchor) : -1;
    m_folds.setGeometry(m_cols, m_folds.indent());
    m_folds.setProse(uri, lines, printColumns);
    if (!m_foldResolveAt.isValid())
        m_foldResolveAt.start();
    keepFoldAnchorInPlace(anchor, keep);
    m_foldSearch.invalidate();
    refreshSearchLabel();
    invalidateFoldAnchors();
}

void TerminalView::setFoldContent(const QString &uri, const QVector<FoldLine> &lines)
{
    if (uri.isEmpty())
        return;
    const int anchor = m_folds.known(uri) ? m_folds.fold(uri)->anchorRow : -1;
    const int keep = anchor >= 0 ? screenRowOfReal(anchor) : -1;
    m_folds.setGeometry(m_cols, m_folds.indent());
    m_folds.setContent(uri, lines);
    if (!m_foldResolveAt.isValid())
        m_foldResolveAt.start();
    // A fold whose anchor is already known opens without waiting for the walk.
    keepFoldAnchorInPlace(anchor, keep);
    m_foldSearch.invalidate();
    refreshSearchLabel();
    invalidateFoldAnchors();
}

void TerminalView::setFoldExpanded(const QString &uri, bool expanded)
{
    if (uri.isEmpty() || m_folds.expanded(uri) == expanded)
        return;
    const int anchor = m_folds.known(uri) ? m_folds.fold(uri)->anchorRow : -1;
    const int keep = anchor >= 0 ? screenRowOfReal(anchor) : -1;
    m_folds.setExpanded(uri, expanded);
    keepFoldAnchorInPlace(anchor, keep);
    // A shut fold is not searched, so the count changes with the chevron.
    m_foldSearch.invalidate();
    refreshSearchLabel();
    m_forceFull = true;
    scheduleFrame();
}

bool TerminalView::foldExpanded(const QString &uri) const { return m_folds.expanded(uri); }

void TerminalView::removeFold(const QString &uri)
{
    if (!m_folds.known(uri))
        return;
    m_folds.remove(uri);
    m_foldSearch.invalidate();
    refreshSearchLabel();
    m_forceFull = true;
    scheduleFrame();
}

void TerminalView::clearFolds()
{
    if (m_folds.folds().empty())
        return;
    m_folds.clear();
    m_foldSearch.invalidate();
    refreshSearchLabel();
    m_forceFull = true;
    scheduleFrame();
}

QStringList TerminalView::expandedFolds() const { return m_folds.expandedUris(); }

bool TerminalView::toggleFold(const QString &uri)
{
    if (uri.isEmpty() || !m_folds.isAnchorUri(uri))
        return false;
    if (m_folds.hasContent(uri)) {
        setFoldExpanded(uri, !m_folds.expanded(uri));
        return true;
    }
    // No content yet: the host fetches it and calls setFoldContent(), which
    // expands the block.
    if (onFoldRequested) {
        onFoldRequested(uri);
        return true;
    }
    return false;
}

bool TerminalView::toggleFoldAt(const QPoint &pos)
{
    const QString uri = foldAnchorAt(cellAt(pos));
    return !uri.isEmpty() && toggleFold(uri);
}

// ---------------------------------------------------------------- fold selection

TerminalView::FoldSelPos TerminalView::selPosAt(const CellPos &c) const
{
    FoldSelPos p;
    const FoldLayer::VisualRow v = visualAt(c.row);
    p.col = c.col;
    if (v.fold) {
        p.fold = true;
        p.foldUri = m_folds.folds()[size_t(v.foldIndex)].uri;
        p.foldRow = v.foldRow;
    } else {
        p.realRow = v.realRow;
    }
    return p;
}

int TerminalView::visualRowOf(const FoldSelPos &p) const
{
    if (!p.fold)
        return m_folds.visualOfReal(p.realRow);
    const int index = m_folds.indexOf(p.foldUri);
    const int start = index < 0 ? -1 : m_folds.foldVisualStart(index);
    return start < 0 ? -1 : start + p.foldRow;
}

bool TerminalView::selPosLess(const FoldSelPos &a, const FoldSelPos &b) const
{
    const int ra = visualRowOf(a), rb = visualRowOf(b);
    return ra != rb ? ra < rb : a.col < b.col;
}

void TerminalView::beginVisualSelection(const CellPos &c, SelectionUnit unit)
{
    m_visualSelUnit = unit;
    m_selAnchor = m_selExtent = selPosAt(c);
    m_visualSelection = unit != SelectionUnit::Cell;
    applyVisualSelection();
}

void TerminalView::extendVisualSelection(const CellPos &c)
{
    m_selExtent = selPosAt(c);
    m_visualSelection = true;
    applyVisualSelection();
}

void TerminalView::clearVisualSelection()
{
    m_visualSelection = false;
    m_selStart = m_selEnd = FoldSelPos();
}

// Order the two ends, widen them for a double or triple click, and hand the
// real-row part to the core so it paints and owns exactly what it did before.
void TerminalView::applyVisualSelection()
{
    m_selStart = m_selAnchor;
    m_selEnd = m_selExtent;
    if (selPosLess(m_selEnd, m_selStart))
        std::swap(m_selStart, m_selEnd);
    if (m_visualSelUnit == SelectionUnit::Line) {
        m_selStart.col = 0;
        m_selEnd.col = m_cols - 1;
    } else if (m_visualSelUnit == SelectionUnit::Word && m_selStart.fold && m_selEnd.fold
               && m_selStart.foldUri == m_selEnd.foldUri && m_selStart.foldRow == m_selEnd.foldRow) {
        int from = 0, to = 0;
        if (foldWordRange(m_selStart, &from, &to)) {
            m_selStart.col = from;
            m_selEnd.col = to;
        }
    }

    // The real rows the selection covers: from its first real position to its
    // last. A fold's rows sit under its anchor, so a selection that starts
    // inside a fold starts, in real terms, on the row after that anchor.
    bool haveReal = false;
    int startRow = 0, startCol = 0, endRow = 0, endCol = 0;
    const FoldLayer::Fold *startFold = m_selStart.fold ? m_folds.fold(m_selStart.foldUri) : nullptr;
    const FoldLayer::Fold *endFold = m_selEnd.fold ? m_folds.fold(m_selEnd.foldUri) : nullptr;
    startRow = startFold ? startFold->anchorRow + 1 : m_selStart.realRow;
    startCol = startFold ? 0 : m_selStart.col;
    endRow = endFold ? endFold->anchorRow : m_selEnd.realRow;
    endCol = endFold ? m_cols - 1 : m_selEnd.col;
    haveReal = endRow >= startRow;

    m_session->withCore([&](VtCore &c) {
        if (!haveReal) {
            c.selectionClear();
            return;
        }
        const int top = m_frame.viewportTop;
        const int rows = std::max(1, m_frame.rows);
        c.selectionBegin(std::max(0, std::min(startRow - top, rows - 1)), startCol,
                         m_visualSelUnit == SelectionUnit::Word && !m_selStart.fold ? SelectionUnit::Word : SelectionUnit::Cell,
                         false);
        c.selectionExtend(std::max(0, std::min(endRow - top, rows - 1)), endCol);
    });
    scheduleFrame();
}

bool TerminalView::foldSelectionRange(int foldIndex, int foldRow, int *from, int *to) const
{
    if (!m_visualSelection)
        return false;
    const int start = m_folds.foldVisualStart(foldIndex);
    if (start < 0)
        return false;
    const int v = start + foldRow;
    const int sv = visualRowOf(m_selStart), ev = visualRowOf(m_selEnd);
    if (sv < 0 || ev < 0 || v < sv || v > ev)
        return false;
    *from = v == sv ? m_selStart.col : 0;
    *to = v == ev ? m_selEnd.col : m_cols - 1;
    return *to >= *from;
}

// The selection as text, in the order it is displayed: the real rows the core
// owns and the rows of every fold in between, spliced at the boundaries. A
// wrapped fold line copies as its one logical line, without the indent.
//
// Limitation: a real segment that reaches above the visible window is read back
// through the core's own selection, so its soft-wrapped rows join as they
// always did; the segments between two folds are on screen by construction.
QString TerminalView::visualSelectedText() const
{
    const int sv = visualRowOf(m_selStart), ev = visualRowOf(m_selEnd);
    if (sv < 0 || ev < 0)
        return QString();
    QStringList parts;
    int v = sv;
    while (v <= ev) {
        const FoldLayer::VisualRow r = m_folds.at(v);
        if (r.fold) {
            const FoldLayer::Fold &f = m_folds.folds()[size_t(r.foldIndex)];
            const FoldLayer::Row &fr = f.rows[size_t(r.foldRow)];
            // Collect every wrapped row of this logical line that the
            // selection covers, so the line comes back whole.
            QString line;
            const int lineIndex = fr.line;
            while (v <= ev) {
                const FoldLayer::VisualRow rr = m_folds.at(v);
                if (!rr.fold || rr.foldIndex != r.foldIndex)
                    break;
                const FoldLayer::Row &row = f.rows[size_t(rr.foldRow)];
                if (row.line != lineIndex)
                    break;
                int from = 0, to = m_cols - 1;
                foldSelectionRange(r.foldIndex, rr.foldRow, &from, &to);
                // Grid columns back to cell indices: the row starts at its own
                // first column (a prose continuation row hangs, a fold indents).
                const int indent = m_folds.rowStartCol(r.foldIndex, rr.foldRow);
                const int firstCell = row.first + std::max(0, from - indent);
                const int lastCell = row.first + std::min(row.count, std::max(0, to - indent + 1));
                line += m_folds.cellsText(r.foldIndex, lineIndex, firstCell, lastCell);
                ++v;
            }
            parts << line;
            continue;
        }
        // A run of real rows: hand it to the core in one go.
        const int firstReal = r.realRow;
        int lastReal = r.realRow;
        int firstCol = v == sv ? m_selStart.col : 0;
        int lastCol = m_cols - 1;
        while (v + 1 <= ev) {
            const FoldLayer::VisualRow next = m_folds.at(v + 1);
            if (next.fold)
                break;
            ++v;
            lastReal = next.realRow;
        }
        if (v == ev)
            lastCol = m_selEnd.col;
        ++v;
        const int top = m_frame.viewportTop;
        const int rows = std::max(1, m_frame.rows);
        parts << m_session->withCore([&](VtCore &c) {
            c.selectionBegin(std::max(0, std::min(firstReal - top, rows - 1)), firstCol, SelectionUnit::Cell, false);
            c.selectionExtend(std::max(0, std::min(lastReal - top, rows - 1)), lastCol);
            return c.selectedText();
        });
    }
    // Put the core's selection back the way the painting needs it.
    const_cast<TerminalView *>(this)->applyVisualSelection();
    return parts.join(QLatin1Char('\n'));
}

// The grid columns of the word under a position inside a fold, the same way a
// double click picks a word out of a real row.
bool TerminalView::foldWordRange(const FoldSelPos &p, int *from, int *to) const
{
    const int index = m_folds.indexOf(p.foldUri);
    if (index < 0)
        return false;
    const FoldLayer::Fold &f = m_folds.folds()[size_t(index)];
    if (p.foldRow < 0 || p.foldRow >= int(f.rows.size()))
        return false;
    const FoldLayer::Row &row = f.rows[size_t(p.foldRow)];
    const std::vector<FoldLayer::Cell> &cells = f.cells[size_t(row.line)];
    auto wordy = [](const QString &s) {
        if (s.isEmpty())
            return false;
        const QChar ch = s.at(0);
        return ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-') || ch == QLatin1Char('.')
            || ch == QLatin1Char('/');
    };
    int col = m_folds.indent(), hit = -1;
    for (int i = row.first; i < row.first + row.count && i < int(cells.size()); ++i) {
        if (p.col >= col && p.col < col + cells[size_t(i)].width) {
            hit = i;
            break;
        }
        col += cells[size_t(i)].width;
    }
    if (hit < 0 || !wordy(cells[size_t(hit)].text))
        return false;
    int start = hit, end = hit;
    while (start > row.first && wordy(cells[size_t(start - 1)].text))
        --start;
    while (end + 1 < row.first + row.count && end + 1 < int(cells.size()) && wordy(cells[size_t(end + 1)].text))
        ++end;
    int x = m_folds.indent();
    for (int k = row.first; k < start; ++k)
        x += cells[size_t(k)].width;
    *from = x;
    for (int k = start; k <= end; ++k)
        x += cells[size_t(k)].width;
    *to = x - 1;
    return true;
}

// A FoldSpan link under a screen cell: the host said this run of the detail
// points somewhere, and it opens through the normal link path.
QString TerminalView::foldLinkAt(const CellPos &c, int *startCol, int *endCol) const
{
    if (!foldsVisible())
        return QString();
    const FoldLayer::VisualRow v = m_folds.at(m_visualTop + c.row);
    if (!v.fold)
        return QString();
    const FoldLayer::Fold &f = m_folds.folds()[size_t(v.foldIndex)];
    if (v.foldRow < 0 || v.foldRow >= int(f.rows.size()))
        return QString();
    const FoldLayer::Row &row = f.rows[size_t(v.foldRow)];
    const std::vector<FoldLayer::Cell> &cells = f.cells[size_t(row.line)];
    int col = m_folds.rowStartCol(v.foldIndex, v.foldRow);
    for (int i = row.first; i < row.first + row.count && i < int(cells.size()); ++i) {
        const FoldLayer::Cell &cell = cells[size_t(i)];
        if (c.col >= col && c.col < col + cell.width && !cell.link.isEmpty()) {
            int from = i, to = i;
            while (from > row.first && cells[size_t(from - 1)].link == cell.link)
                --from;
            while (to + 1 < row.first + row.count && to + 1 < int(cells.size())
                   && cells[size_t(to + 1)].link == cell.link)
                ++to;
            int x = m_folds.rowStartCol(v.foldIndex, v.foldRow);
            for (int k = row.first; k < from; ++k)
                x += cells[size_t(k)].width;
            *startCol = x;
            for (int k = from; k <= to; ++k)
                x += cells[size_t(k)].width;
            *endCol = x - 1;
            return cell.link;
        }
        col += cell.width;
    }
    return QString();
}

QStringList TerminalView::visibleRowsText() const
{
    QStringList out;
    for (int i = 0; i < m_rows; ++i) {
        const FoldLayer::VisualRow v = visualAt(i);
        if (v.fold) {
            out << QString(m_folds.rowStartCol(v.foldIndex, v.foldRow), QLatin1Char(' '))
                + m_folds.rowText(v.foldIndex, v.foldRow);
            continue;
        }
        const int frameRow = v.realRow - m_frame.viewportTop;
        out << (frameRow >= 0 && frameRow < int(m_frame.lines.size()) ? m_frame.lines[size_t(frameRow)].text() : QString());
    }
    return out;
}

bool TerminalView::toggleNearestFold()
{
    if (m_folds.prefix().isEmpty() || m_frame.altScreen)
        return false;
    const int cursorRow = m_frame.cursorInViewport ? m_frame.cursor.row : m_frame.rows - 1;
    for (int r = cursorRow; r >= 0; --r) {
        const QString uri = m_session->withCore([&](VtCore &core) { return core.hyperlinkAt(r, 0); });
        if (m_folds.isAnchorUri(uri))
            return toggleFold(uri);
    }
    return false;
}

void TerminalView::contextMenuEvent(QContextMenuEvent *e)
{
    if (!m_builtinContextMenu || mouseToProgram(e->modifiers())) {
        e->ignore();
        return;
    }
    // Heap-allocated and non-blocking: a nested exec() loop could delete this
    // view (e.g. the shell exits and the host closes the pane) under a stack menu.
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    // A right click on a path offers the two things a click cannot do (issue YZTK); on a card
    // reference, the card. (Relay's own panes replace this menu — src/main.cpp's
    // showTerminalMenu() — so this is what the engine offers on its own.)
    const Link link = linkAtPoint(e->pos());
    if (!link.card.isEmpty()) {
        const QString reference = QStringLiteral("#") + link.card;
        menu->addAction(tr("Open %1").arg(reference), this,
                        [this, link] { emit linkActivated(link.target, -1, -1); });
        menu->addAction(tr("Copy %1").arg(reference), this,
                        [reference] { QApplication::clipboard()->setText(reference); });
        menu->addSeparator();
    } else if (QString section, row; link.valid()
               && (links::optionOf(link.target, &section, &row) || !links::sessionIdOf(link.target).isEmpty())) {
        // `option:sec/row` and `session:<id>` (#AGNT step 8) are neither files nor URLs: reading
        // one as a path offered "Open <last segment>", "Open in the system editor" and "Copy
        // path" for a setting. The click itself is the only thing that works here, so it is the
        // only thing offered; the host resolves it (Pane::openOutputTarget).
        menu->addAction(tr("Open"), this, [this, link] { emit linkActivated(link.target, -1, -1); });
        menu->addSeparator();
    } else if (link.valid()) {
        const QString name = link.url ? link.target : QFileInfo(link.target).fileName();
        menu->addAction(tr("Open %1").arg(name), this,
                        [this, link] { emit linkActivated(link.target, link.line, link.column); });
        if (!link.url)
            menu->addAction(tr("Open in the system editor"), this,
                            [link] { QDesktopServices::openUrl(QUrl::fromLocalFile(link.target)); });
        menu->addAction(link.url ? tr("Copy link") : tr("Copy path"), this,
                        [link] { QApplication::clipboard()->setText(link.target); });
        menu->addSeparator();
    }
    QAction *copy = menu->addAction(tr("Copy"), this, &TerminalView::copySelection);
    copy->setEnabled(m_visualSelection || m_session->withCore([](VtCore &c) { return c.hasSelection(); }));
    menu->addAction(tr("Paste"), this, &TerminalView::pasteClipboard);
    menu->addAction(tr("Select All"), this, &TerminalView::selectAll);
    menu->addSeparator();
    menu->addAction(tr("Find..."), this, &TerminalView::showSearchBar);
    menu->addAction(tr("Clear Scrollback"), this, [this] {
        m_session->withCore([](VtCore &c) { c.clearScrollback(); });
        m_forceFull = true;
        scheduleFrame();
    });
    menu->popup(e->globalPos());
}

// ---------------------------------------------------------------- focus

void TerminalView::focusInEvent(QFocusEvent *)
{
    m_focused = true;
    m_blinkOn = true;
    if (m_blinkEnabled)
        m_blinkTimer.start();
    m_session->withCore([](VtCore &c) { c.focusChanged(true); });
    update(QRect(0, m_padding + m_frame.cursor.row * m_ch, width(), m_ch));
}

void TerminalView::focusOutEvent(QFocusEvent *)
{
    m_focused = false;
    m_blinkTimer.stop();
    m_session->withCore([](VtCore &c) { c.focusChanged(false); });
    update(QRect(0, m_padding + m_frame.cursor.row * m_ch, width(), m_ch));
    updateHover(QPoint(-1, -1), Qt::NoModifier);
}

// ---------------------------------------------------------------- host API

// Scrolling counts visual rows: a 500-line fold scrolls line by line like any
// other output. With no fold open these all go straight to the core, as before.
void TerminalView::scrollLines(int lines)
{
    if (!foldsVisible()) {
        m_session->withCore([&](VtCore &c) { c.scrollViewport(lines); });
        scheduleFrame();
        return;
    }
    setVisualTop(m_visualTop + lines);
}

void TerminalView::scrollPages(int pages)
{
    scrollLines(pages * std::max(1, m_rows - 1));
}

void TerminalView::scrollToTop()
{
    m_session->withCore([](VtCore &c) { c.scrollViewportToTop(); });
    if (foldsVisible())
        setVisualTop(0);
    else
        scheduleFrame();
}

void TerminalView::scrollToBottom()
{
    m_session->withCore([](VtCore &c) { c.scrollViewportToBottom(); });
    m_followBottom = true;
    if (foldsVisible())
        setVisualTop(maxVisualTop());
    else
        scheduleFrame();
}

void TerminalView::scrollToRow(int row)
{
    if (foldsVisible()) {
        setVisualTop(m_folds.visualOfReal(row));
        return;
    }
    m_session->withCore([&](VtCore &c) { c.scrollViewportToRow(row); });
    scheduleFrame();
}

void TerminalView::scrollToVisualRow(int row)
{
    if (foldsVisible()) {
        setVisualTop(row);
        return;
    }
    scrollToRow(row);
}

bool TerminalView::viewportAtBottom() const
{
    if (foldsVisible())
        return m_followBottom;
    return m_session->withCore([](VtCore &c) { return c.viewportAtBottom(); });
}

bool TerminalView::scrollToPrompt(int direction)
{
    int top = -1;
    const bool ok = m_session->withCore([&](VtCore &c) {
        const bool found = c.scrollToPrompt(direction);
        top = c.viewportTop();
        return found;
    });
    if (ok && foldsVisible())
        setVisualTop(m_folds.visualOfReal(top));
    else
        scheduleFrame();
    return ok;
}

QString TerminalView::selectedText() const
{
    // A selection that touches an open fold is the view's, in visual order.
    if (m_visualSelection && foldsVisible())
        return visualSelectedText();
    return m_session->withCore([](VtCore &c) { return c.selectedText(); });
}

void TerminalView::copySelection()
{
    const QString text = selectedText();
    if (!text.isEmpty())
        QApplication::clipboard()->setText(text, QClipboard::Clipboard);
}

void TerminalView::pasteText(const QString &text)
{
    if (text.isEmpty())
        return;
    m_session->withCore([&](VtCore &c) { c.paste(text); });
    afterUserInput();
}

void TerminalView::pasteClipboard()
{
    pasteText(QApplication::clipboard()->text(QClipboard::Clipboard));
}

void TerminalView::pasteSelection()
{
    const QClipboard *cb = QApplication::clipboard();
    pasteText(cb->text(cb->supportsSelection() ? QClipboard::Selection : QClipboard::Clipboard));
}

void TerminalView::selectAll()
{
    if (foldsVisible()) {
        // Everything, in visual order: the whole scrollback and every open
        // fold's rows with it.
        m_visualSelUnit = SelectionUnit::Cell;
        m_selAnchor = FoldSelPos();
        m_selAnchor.realRow = 0;
        m_selExtent = FoldSelPos();
        m_selExtent.realRow = std::max(0, realRows() - 1);
        m_selExtent.col = m_cols - 1;
        m_visualSelection = true;
        applyVisualSelection();
        return;
    }
    m_session->withCore([](VtCore &c) { c.selectAll(); });
    scheduleFrame();
}

void TerminalView::clearSelection()
{
    clearVisualSelection();
    m_session->withCore([](VtCore &c) { c.selectionClear(); });
    scheduleFrame();
}

void TerminalView::showSearchBar()
{
    if (!m_searchBar) {
        m_searchBar = new QWidget(this);
        m_searchBar->setAutoFillBackground(true);
        auto *layout = new QHBoxLayout(m_searchBar);
        layout->setContentsMargins(6, 3, 6, 3);
        m_searchEdit = new QLineEdit(m_searchBar);
        m_searchEdit->setPlaceholderText(tr("Find in scrollback"));
        m_searchEdit->setMinimumWidth(180);
        m_searchLabel = new QLabel(m_searchBar);
        auto *older = new QToolButton(m_searchBar);
        older->setArrowType(Qt::UpArrow);
        older->setToolTip(tr("Older match (Enter)"));
        auto *newer = new QToolButton(m_searchBar);
        newer->setArrowType(Qt::DownArrow);
        newer->setToolTip(tr("Newer match (Shift+Enter)"));
        auto *close = new QToolButton(m_searchBar);
        close->setText(QStringLiteral("x"));
        layout->addWidget(m_searchEdit);
        layout->addWidget(m_searchLabel);
        layout->addWidget(older);
        layout->addWidget(newer);
        layout->addWidget(close);
        connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &t) { find(t, true); });
        connect(older, &QToolButton::clicked, this, [this] {
            int index = -1;
            const int count = searchStep(true, &index);
            updateSearchLabel(count, index);
        });
        connect(newer, &QToolButton::clicked, this, [this] {
            int index = -1;
            const int count = searchStep(false, &index);
            updateSearchLabel(count, index);
        });
        connect(close, &QToolButton::clicked, this, &TerminalView::hideSearchBar);
        connect(m_searchEdit, &QLineEdit::returnPressed, this, [this, older, newer] {
            if (QApplication::keyboardModifiers() & Qt::ShiftModifier)
                newer->click();
            else
                older->click();
        });
        auto *esc = new QAction(m_searchEdit);
        esc->setShortcut(Qt::Key_Escape);
        esc->setShortcutContext(Qt::WidgetShortcut);
        connect(esc, &QAction::triggered, this, &TerminalView::hideSearchBar);
        m_searchEdit->addAction(esc);
        m_searchBar->adjustSize();
    }
    m_searchBar->move(width() - m_searchBar->width() - 8, 4);
    m_searchBar->show();
    m_searchBar->raise();
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

void TerminalView::hideSearchBar()
{
    if (!m_searchBar)
        return;
    m_searchBar->hide();
    m_session->withCore([](VtCore &c) { c.searchSet(QString()); });
    m_foldSearch.reset();
    m_searchIndex = -1;
    m_forceFull = true;
    scheduleFrame();
    setFocus();
}

void TerminalView::updateSearchLabel(int count, int index)
{
    if (!m_searchLabel)
        return;
    if (count == 0)
        m_searchLabel->setText(tr("no matches"));
    else if (index < 0) // the matches moved under the walk: a count, no place in it
        m_searchLabel->setText(QString::number(count));
    else
        m_searchLabel->setText(QStringLiteral("%1/%2").arg(index + 1).arg(count));
}

// A fold opened, shut, gained content or lost its anchor while a find was
// live: the count on the bar follows, without moving the selected match.
void TerminalView::refreshSearchLabel()
{
    if (!m_searchLabel || !m_searchBar || m_searchBar->isHidden() || m_foldSearch.needle().isEmpty())
        return;
    const int count = searchMatchCount();
    if (foldsVisible())
        m_searchIndex = m_foldSearch.index();
    updateSearchLabel(count, count == 0 ? -1 : m_searchIndex);
}

int TerminalView::searchMatchCount() const
{
    const int core = m_session->withCore([](VtCore &c) { return c.searchMatchCount(); });
    return foldsVisible() ? core + m_foldSearch.matchCount(m_folds) : core;
}

void TerminalView::ensureVisualRowVisible(int visualRow)
{
    if (visualRow < 0)
        return;
    if (!foldsVisible()) {
        scrollToRow(visualRow);
        return;
    }
    if (visualRow >= m_visualTop && visualRow < m_visualTop + m_rows)
        return;
    scrollToVisualRow(std::max(0, visualRow - m_rows / 2));
}

// One step of the find, over the real rows and the open folds' rows as one
// sequence in visual order.
//
// With no fold open (or a full-screen program on the grid) this is the core's
// own searchStep(), untouched. Otherwise FoldSearch merges the two kinds of
// match: it steps the core at most once per call and leaves it parked on that
// match while the fold matches in between are walked, so a core step is never
// made and then undone. See view/FoldSearch.h.
int TerminalView::searchStep(bool backwards, int *index)
{
    int selected = -1;
    int count = 0;
    if (!foldsVisible()) {
        m_foldSearch.resetCursor();
        count = m_session->withCore([&](VtCore &c) {
            const int n = c.searchMatchCount();
            if (n > 0)
                selected = c.searchStep(backwards);
            return n;
        });
    } else {
        FoldSearch::Core core;
        core.count = m_session->withCore([](VtCore &c) { return c.searchMatchCount(); });
        core.step = [this](bool back) {
            FoldSearch::CorePos p;
            // A row a replacement fold hides is painted as the fold's own row,
            // so the core's match there is the fold's match: step past it. The
            // bound is the whole cycle, for the needle that matches nowhere
            // visible at all.
            int guard = 0;
            m_session->withCore([&](VtCore &c) {
                const int bound = c.searchMatchCount() + 1;
                do {
                    p.index = c.searchStep(back);
                    p.row = c.searchCurrentRow();
                    ++guard;
                } while (p.index >= 0 && p.row >= 0 && m_folds.rowHidden(p.row) && guard < bound);
            });
            p.valid = p.index >= 0 && p.row >= 0;
            return p;
        };
        core.currentRow = [this] { return m_session->withCore([](VtCore &c) { return c.searchCurrentRow(); }); };
        const FoldSearch::Step s = m_foldSearch.step(m_folds, backwards, core);
        selected = s.index;
        // The core may have recomputed its matches while stepping.
        count = m_session->withCore([](VtCore &c) { return c.searchMatchCount(); })
                + m_foldSearch.matchCount(m_folds);
        ensureVisualRowVisible(s.visualRow);
    }
    m_searchIndex = selected;
    if (index)
        *index = selected;
    m_forceFull = true;
    scheduleFrame();
    return count;
}

int TerminalView::find(const QString &text, bool backwards)
{
    m_foldSearch.setNeedle(text);
    m_session->withCore([&](VtCore &c) { c.searchSet(text); });
    int index = -1;
    const int count = searchStep(backwards, &index);
    updateSearchLabel(count, index);
    return count;
}

QString TerminalView::debugDump()
{
    QString s;
    const qint64 fgpid = m_session->foregroundPid();
    const QString cwd = m_session->currentDirectory();
    m_session->withCore([&](VtCore &c) {
        const CursorState cur = c.activeCursor();
        s += QStringLiteral("core=%1 rows=%2 cols=%3 altScreen=%4 history=%5 viewportTop=%6 cursor=%7,%8 fgpid=%9 bytes=%10 paints=%11\n")
                 .arg(QString::fromLatin1(c.name()))
                 .arg(c.rows())
                 .arg(c.columns())
                 .arg(c.altScreen())
                 .arg(c.historyRows())
                 .arg(c.viewportTop())
                 .arg(cur.row)
                 .arg(cur.col)
                 .arg(fgpid)
                 .arg(m_session->bytesReceived())
                 .arg(m_paints);
        s += QStringLiteral("title=%1 cwd=%2\n").arg(c.title(), cwd.isEmpty() ? QStringLiteral("-") : cwd);
    });
    if (!m_folds.prefix().isEmpty()) {
        s += QStringLiteral("folds prefix=%1 known=%2 expanded=%3 visualRows=%4 visualTop=%5 atBottom=%6\n")
                 .arg(m_folds.prefix())
                 .arg(m_folds.folds().size())
                 .arg(m_folds.expandedCount())
                 .arg(m_folds.visualRows())
                 .arg(m_visualTop)
                 .arg(m_followBottom);
        for (const FoldLayer::Fold &f : m_folds.folds())
            s += QStringLiteral("  %1 rows=%2 anchor=%3..%4 %5\n")
                     .arg(f.uri)
                     .arg(f.height())
                     .arg(f.anchorStartRow)
                     .arg(f.anchorRow)
                     .arg(f.expanded ? QStringLiteral("open") : QStringLiteral("shut"));
    }
    m_session->withCore([&](VtCore &c) {
        s += QStringLiteral("--- historyText(5) ---\n") + c.historyText(5).join(QLatin1Char('\n'));
        s += QStringLiteral("\n--- screenText() ---\n") + c.screenText() + QLatin1Char('\n');
    });
    return s;
}

} // namespace relay
