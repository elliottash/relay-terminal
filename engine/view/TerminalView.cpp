// SPDX-License-Identifier: GPL-3.0-or-later
#include "TerminalView.h"

#include "BoxDrawing.h"
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
    emit gridSizeChanged(rows, cols);
    scheduleFrame();
}

void TerminalView::scheduleFrame()
{
    if (m_frameTimer.isActive())
        return;
    // Typing echo should appear within one frame; a flood repaints at ~30 fps so
    // painting (and the X server) never becomes the bottleneck.
    const quint64 bytes = m_session->bytesReceived();
    const bool flooding = bytes - m_bytesAtFrame > 512 * 1024;
    m_frameTimer.start(flooding ? 33 : 4);
}

void TerminalView::pullFrame()
{
    const bool force = m_forceFull;
    m_forceFull = false;
    m_bytesAtFrame = m_session->bytesReceived();
    const bool changed = m_session->withCore([&](VtCore &c) { return c.updateFrame(&m_frame, force); });
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

    if (m_frame.full || force) {
        update();
    } else {
        QRegion region;
        for (int r = 0; r < m_frame.rows && r < int(m_frame.dirty.size()); ++r) {
            if (m_frame.dirty[size_t(r)])
                region += QRect(0, m_padding + r * m_ch, width(), m_ch);
        }
        const bool cursorMoved = m_paintedCursor.row != m_frame.cursor.row || m_paintedCursor.col != m_frame.cursor.col
            || m_paintedCursor.visible != m_frame.cursor.visible || m_paintedCursor.shape != m_frame.cursor.shape
            || m_paintedCursorInViewport != m_frame.cursorInViewport;
        if (cursorMoved) {
            region += QRect(0, m_padding + m_paintedCursor.row * m_ch, width(), m_ch);
            region += QRect(0, m_padding + m_frame.cursor.row * m_ch, width(), m_ch);
        }
        if (!region.isEmpty())
            update(region);
    }
    if (m_frame.cursor.row != m_paintedCursor.row || m_frame.cursor.col != m_paintedCursor.col)
        m_blinkOn = true;
    m_paintedCursor = m_frame.cursor;
    m_paintedCursorInViewport = m_frame.cursorInViewport;

    if (m_frame.viewportTop != m_lastTop || m_frame.historyRows != m_lastHistory) {
        m_lastTop = m_frame.viewportTop;
        m_lastHistory = m_frame.historyRows;
        emit scrollPositionChanged(m_frame.viewportTop, m_frame.historyRows, m_frame.rows);
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
    p.fillRect(dirty, m_flash ? m_scheme.foreground : m_scheme.background);
    if (m_frame.lines.empty())
        return;
    const int firstRow = std::max(0, (dirty.top() - m_padding) / m_ch);
    const int lastRow = std::min(int(m_frame.lines.size()) - 1, (dirty.bottom() - m_padding) / m_ch);
    for (int row = firstRow; row <= lastRow; ++row)
        paintRow(p, row);
    paintCursor(p);
}

void TerminalView::paintRow(QPainter &p, int row)
{
    const Line &line = m_frame.lines[size_t(row)];
    const int cols = std::min<int>(int(line.cells.size()), m_frame.columns);
    const int y = m_padding + row * m_ch;
    const int baseline = y + m_ascent;

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
        if (c.attrs & AttrReverse) {
            std::swap(fg, bg);
            bgDefault = false;
        }
        if (line.selectionStart >= 0 && col >= line.selectionStart && col <= line.selectionEnd) {
            bg = m_scheme.selection;
            bgDefault = false;
        }
        for (const Line::Highlight &h : line.highlights) {
            if (col >= h.start && col <= h.end) {
                bg = h.current ? m_scheme.searchCurrent : m_scheme.searchMatch;
                fg = m_scheme.searchText;
                bgDefault = false;
            }
        }
        if (c.attrs & AttrFaint)
            fg.setAlphaF(0.6);
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
    std::u32string cps;
    for (int col = 0; col < cols; ++col) {
        const Cell &c = line.cells[size_t(col)];
        if (c.ch == kWideTail)
            continue;
        const int w = c.width == 2 ? 2 : 1;
        const CellColors cc = colorsFor(col);
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
}

void TerminalView::paintCursor(QPainter &p)
{
    const ViewportFrame &f = m_frame;
    if (!f.cursorInViewport || f.cursor.row < 0 || f.cursor.row >= int(f.lines.size()))
        return;
    const Line &line = f.lines[size_t(f.cursor.row)];
    const int col = std::max(0, std::min(f.cursor.col, f.columns - 1));
    const Cell cell = col < int(line.cells.size()) ? line.cells[size_t(col)] : Cell();
    const int w = cell.width == 2 ? 2 : 1;
    QRect r = cellRect(f.cursor.row, col, w);

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
    const QRect cursorRect = cellRect(m_frame.cursor.row, m_frame.cursor.col);
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
        Link link;
        int s = 0, en = 0;
        const bool onLink = linkAt(pos, &link, &s, &en);
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
        m_session->withCore([&](VtCore &c) { c.selectionBegin(pos.row, pos.col, unit, rect); });
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
        m_session->withCore([&](VtCore &c) { c.selectionExtend(pos.row, pos.col); });
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
    m_session->withCore([&](VtCore &c) {
        c.scrollViewport(up ? -1 : 1);
        c.selectionExtend(up ? 0 : m_rows - 1, pos.col);
    });
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
        m_session->withCore([](VtCore &c) { c.selectionClear(); });
        scheduleFrame();
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
    if (m_copyOnSelect && QApplication::clipboard()->supportsSelection()) {
        const QString text = selectedText();
        if (!text.isEmpty())
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
    if (link.valid()) {
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

QString TerminalView::currentDirectory() const
{
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

// The logical line the cell belongs to: soft-wrapped rows of the viewport joined into one
// string, with the (row, column) each UTF-16 unit came from.
namespace {
struct LogicalRow {
    QString text;
    std::vector<std::pair<int, int>> cellOf;
};
} // namespace

bool TerminalView::linkAt(const CellPos &c, Link *link, int *startCol, int *endCol)
{
    *link = Link();
    if (c.row < 0 || c.row >= int(m_frame.lines.size()))
        return false;
    const Line &l = m_frame.lines[size_t(c.row)];

    // An OSC 8 hyperlink: the program itself said what the text points at.
    const QString uri = m_session->withCore([&](VtCore &core) { return core.hyperlinkAt(c.row, c.col); });
    if (!uri.isEmpty() && c.col < int(l.cells.size())) {
        const uint32_t id = l.cells[size_t(c.col)].link;
        int s = c.col, e = c.col;
        while (s > 0 && l.cells[size_t(s - 1)].link == id && id)
            --s;
        while (e + 1 < int(l.cells.size()) && l.cells[size_t(e + 1)].link == id && id)
            ++e;
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
    int firstRow = c.row, lastRow = c.row;
    while (firstRow > 0 && m_frame.lines[size_t(firstRow)].continuation)
        --firstRow;
    while (lastRow + 1 < int(m_frame.lines.size()) && m_frame.lines[size_t(lastRow + 1)].continuation)
        ++lastRow;
    LogicalRow logical;
    for (int r = firstRow; r <= lastRow; ++r) {
        const Line &rowLine = m_frame.lines[size_t(r)];
        const int n = r < lastRow ? m_frame.columns : int(rowLine.cells.size());
        for (int i = 0; i < n; ++i) {
            const Cell cell = i < int(rowLine.cells.size()) ? rowLine.cells[size_t(i)] : Cell();
            if (cell.ch == kWideTail)
                continue;
            const QString text = rowLine.cellText(cell);
            for (int k = 0; k < text.size(); ++k)
                logical.cellOf.push_back({r, i});
            logical.text += text;
        }
    }
    int idx = -1;
    for (int i = 0; i < int(logical.cellOf.size()); ++i) {
        if (logical.cellOf[size_t(i)].first == c.row && logical.cellOf[size_t(i)].second == c.col) {
            idx = i;
            break;
        }
    }
    if (idx < 0 || idx >= logical.text.size())
        return false;
    for (const links::Found &found : links::scan(logical.text, currentDirectory(), QDir::homePath(), links::systemProbe())) {
        const int s = found.candidate.start;
        const int e = s + found.candidate.length - 1;
        if (idx < s || idx > e || e >= int(logical.cellOf.size()))
            continue;
        link->target = found.target.target;
        link->text = found.candidate.text;
        link->url = found.target.kind == links::Kind::Url;
        link->directory = found.target.directory;
        link->line = found.target.line;
        link->column = found.target.column;
        // The hover underline is per row: clip the span to the row under the pointer.
        *startCol = logical.cellOf[size_t(s)].first == c.row ? logical.cellOf[size_t(s)].second : 0;
        *endCol = logical.cellOf[size_t(e)].first == c.row ? logical.cellOf[size_t(e)].second : m_frame.columns - 1;
        return true;
    }
    return false;
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
    m_session->withCore([&](VtCore &core) {
        historyRows = core.historyRows();
        columns = std::max(1, core.columns());
        rows = core.historyText(kWalkScrollbackLines);
        const QString screen = core.screenText();
        rows += screen.split(QLatin1Char('\n'));
    });
    // historyText() returns the newest lines, so the first row it gave us sits this far
    // down the scrollback; screen row k follows at historyRows + k.
    const int firstRow = std::max(0, historyRows - int(rows.size()));
    const QString cwd = currentDirectory();
    const QString home = QDir::homePath();
    const links::Probe probe = links::systemProbe();
    for (int i = 0; i < rows.size() && int(m_linkWalk.size()) < kWalkMaxLinks;) {
        // Rows the emulator filled to the last column continue on the next row: a path
        // that wrapped is one logical line again.
        int last = i;
        QString text = rows[i];
        while (last + 1 < rows.size() && rows[last].size() >= columns) {
            ++last;
            text += rows[last];
        }
        for (const links::Found &found : links::scan(text, cwd, home, probe)) {
            WalkLink walk;
            const int s = found.candidate.start;
            const int e = s + found.candidate.length - 1;
            walk.row = firstRow + i + s / columns;
            walk.col = s % columns;
            walk.endRow = firstRow + i + e / columns;
            walk.endCol = e % columns;
            walk.link.target = found.target.target;
            walk.link.text = found.candidate.text;
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
        top = m_session->withCore([](VtCore &core) { return core.viewportTop(); });
    }
    const int row = walk.row - top;
    const int endRow = walk.endRow - top;
    m_session->withCore([&](VtCore &core) {
        core.selectionBegin(row, walk.col, SelectionUnit::Cell, false);
        core.selectionExtend(endRow, walk.endCol);
    });
    // The underline the mouse draws, for the row the link starts on.
    m_hoverRow = row >= 0 && row < m_rows ? row : -1;
    m_hoverStart = walk.col;
    m_hoverEnd = row == endRow ? walk.endCol : m_frame.columns - 1;
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
    // A right click on a path offers the two things a click cannot do (issue YZTK).
    const Link link = linkAtPoint(e->pos());
    if (link.valid()) {
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
    copy->setEnabled(m_session->withCore([](VtCore &c) { return c.hasSelection(); }));
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

void TerminalView::scrollLines(int lines)
{
    m_session->withCore([&](VtCore &c) { c.scrollViewport(lines); });
    scheduleFrame();
}

void TerminalView::scrollPages(int pages)
{
    scrollLines(pages * std::max(1, m_rows - 1));
}

void TerminalView::scrollToTop()
{
    m_session->withCore([](VtCore &c) { c.scrollViewportToTop(); });
    scheduleFrame();
}

void TerminalView::scrollToBottom()
{
    m_session->withCore([](VtCore &c) { c.scrollViewportToBottom(); });
    scheduleFrame();
}

void TerminalView::scrollToRow(int row)
{
    m_session->withCore([&](VtCore &c) { c.scrollViewportToRow(row); });
    scheduleFrame();
}

bool TerminalView::scrollToPrompt(int direction)
{
    const bool ok = m_session->withCore([&](VtCore &c) { return c.scrollToPrompt(direction); });
    scheduleFrame();
    return ok;
}

QString TerminalView::selectedText() const
{
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
    m_session->withCore([](VtCore &c) { c.selectAll(); });
    scheduleFrame();
}

void TerminalView::clearSelection()
{
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
            updateSearchLabel(m_session->withCore([](VtCore &c) { return c.searchMatchCount(); }),
                              m_session->withCore([](VtCore &c) { return c.searchStep(true); }));
            scheduleFrame();
        });
        connect(newer, &QToolButton::clicked, this, [this] {
            updateSearchLabel(m_session->withCore([](VtCore &c) { return c.searchMatchCount(); }),
                              m_session->withCore([](VtCore &c) { return c.searchStep(false); }));
            scheduleFrame();
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
    m_forceFull = true;
    scheduleFrame();
    setFocus();
}

void TerminalView::updateSearchLabel(int count, int index)
{
    if (!m_searchLabel)
        return;
    m_searchLabel->setText(count == 0 ? tr("no matches") : QStringLiteral("%1/%2").arg(index + 1).arg(count));
}

int TerminalView::find(const QString &text, bool backwards)
{
    int index = -1;
    const int count = m_session->withCore([&](VtCore &c) {
        const int n = c.searchSet(text);
        if (n > 0)
            index = c.searchStep(backwards);
        return n;
    });
    updateSearchLabel(count, index);
    m_forceFull = true;
    scheduleFrame();
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
        s += QStringLiteral("--- historyText(5) ---\n") + c.historyText(5).join(QLatin1Char('\n'));
        s += QStringLiteral("\n--- screenText() ---\n") + c.screenText() + QLatin1Char('\n');
    });
    return s;
}

} // namespace relay
