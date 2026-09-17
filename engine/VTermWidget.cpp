// SPDX-License-Identifier: GPL-3.0-or-later
#include "VTermWidget.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QWheelEvent>

#include <algorithm>
#include <cstring>

namespace relay {

namespace {
const uint32_t kContinuation = 0xFFFFFFFF; // libvterm marks the right half of a wide cell this way
inline QString ucs4(uint32_t c)
{
    const char32_t cc = char32_t(c);
    return QString::fromUcs4(&cc, 1);
}
const QColor kDefaultFg(0xd8, 0xd8, 0xd8);
const QColor kDefaultBg(0x1c, 0x1e, 0x24);
const QColor kSelectionBg(0x3a, 0x5a, 0x8c);

bool posLess(qint64 l1, int c1, qint64 l2, int c2)
{
    return l1 < l2 || (l1 == l2 && c1 < c2);
}
} // namespace

VTermWidget::VTermWidget(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_InputMethodEnabled);
    setMouseTracking(false);
    setCursor(Qt::IBeamCursor);

    m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_font.setFamily(QStringLiteral("DejaVu Sans Mono"));
    m_font.setStyleHint(QFont::Monospace);
    m_font.setPointSize(11);
    m_font.setKerning(false);
    updateCellMetrics();

    m_vt = vterm_new(m_rows, m_cols);
    vterm_set_utf8(m_vt, 1);
    m_state = vterm_obtain_state(m_vt);
    m_screen = vterm_obtain_screen(m_vt);

    VTermColor fg, bg;
    vterm_color_rgb(&fg, kDefaultFg.red(), kDefaultFg.green(), kDefaultFg.blue());
    vterm_color_rgb(&bg, kDefaultBg.red(), kDefaultBg.green(), kDefaultBg.blue());
    vterm_state_set_default_colors(m_state, &fg, &bg);
    vterm_state_set_bold_highbright(m_state, 1);

    static const VTermScreenCallbacks screenCbs = {
        &VTermWidget::cbDamage,
        nullptr, // moverect: return-0 semantics -> libvterm damages instead
        &VTermWidget::cbMoveCursor,
        &VTermWidget::cbSetTermProp,
        &VTermWidget::cbBell,
        nullptr, // resize
        &VTermWidget::cbSbPushLine,
        &VTermWidget::cbSbPopLine,
        &VTermWidget::cbSbClear,
    };
    vterm_screen_set_callbacks(m_screen, &screenCbs, this);
    static const VTermStateFallbacks fallbacks = {
        nullptr, nullptr, &VTermWidget::cbOsc, nullptr, nullptr, nullptr, nullptr,
    };
    vterm_screen_set_unrecognised_fallbacks(m_screen, &fallbacks, this);
    vterm_output_set_callback(m_vt, &VTermWidget::cbOutput, this);
    vterm_screen_set_damage_merge(m_screen, VTERM_DAMAGE_ROW);
    vterm_screen_enable_altscreen(m_screen, 1);
    vterm_screen_enable_reflow(m_screen, true);
    vterm_screen_reset(m_screen, 1);

    m_repaintTimer.setSingleShot(true);
    m_repaintTimer.setInterval(8);
    connect(&m_repaintTimer, &QTimer::timeout, this, [this] {
        if (!m_pendingDamage.isEmpty())
            update(m_pendingDamage);
        m_pendingDamage = QRegion();
    });

    resize(m_cols * m_cw, m_rows * m_ch);
}

VTermWidget::~VTermWidget()
{
    m_pty.reset();
    vterm_free(m_vt);
}

// ---------------------------------------------------------------- process

bool VTermWidget::startProgram(const QString &program, const QStringList &args, const QString &workingDirectory,
                               const QStringList &extraEnvironment)
{
    m_pty = Pty::create();
    m_pty->onOutput = [this](const char *d, qint64 n) { onPtyOutput(d, n); };
    m_pty->onFinished = [this](int code) {
        if (onFinished)
            onFinished(code);
    };
    Pty::StartOptions o;
    o.program = program;
    o.arguments = args;
    o.workingDirectory = workingDirectory;
    o.extraEnvironment = QStringList{QStringLiteral("TERM=xterm-256color"), QStringLiteral("COLORTERM=truecolor")}
        + extraEnvironment;
    o.rows = m_rows;
    o.cols = m_cols;
    return m_pty->start(o);
}

void VTermWidget::sendInput(const QByteArray &bytes)
{
    if (m_pty)
        m_pty->write(bytes.constData(), bytes.size());
}

void VTermWidget::sendText(const QString &text, bool asPaste)
{
    if (asPaste) {
        QString t = text;
        t.replace(QStringLiteral("\r\n"), QStringLiteral("\r"));
        t.replace(QLatin1Char('\n'), QLatin1Char('\r'));
        vterm_keyboard_start_paste(m_vt); // emits ESC[200~ only if the app enabled mode 2004
        sendInput(t.toUtf8());
        vterm_keyboard_end_paste(m_vt);
    } else {
        sendInput(text.toUtf8());
    }
}

qint64 VTermWidget::shellPid() const { return m_pty ? m_pty->childPid() : -1; }
qint64 VTermWidget::foregroundProcessId() const { return m_pty ? m_pty->foregroundPid() : -1; }

int VTermWidget::capabilities() const
{
    return ScreenText | Scrollback | AltScreenState | LinkClicks | Osc8Links;
}

void VTermWidget::onPtyOutput(const char *data, qint64 len)
{
    m_bytes += quint64(len);
    vterm_input_write(m_vt, data, size_t(len));
    vterm_screen_flush_damage(m_screen);
}

void VTermWidget::cbOutput(const char *s, size_t len, void *user)
{
    auto *self = static_cast<VTermWidget *>(user);
    if (self->m_pty)
        self->m_pty->write(s, qint64(len));
}

// ---------------------------------------------------------------- vterm callbacks

int VTermWidget::cbDamage(VTermRect r, void *user)
{
    auto *self = static_cast<VTermWidget *>(user);
    if (self->m_scrollOffset > 0) {
        self->scheduleRepaint(self->rect());
        return 1;
    }
    self->scheduleRepaint(QRect(r.start_col * self->m_cw, r.start_row * self->m_ch,
                                (r.end_col - r.start_col) * self->m_cw, (r.end_row - r.start_row) * self->m_ch));
    return 1;
}

int VTermWidget::cbMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
    auto *self = static_cast<VTermWidget *>(user);
    self->m_cursor = pos;
    self->m_cursorVisible = visible;
    self->scheduleRepaint(self->cellRect(oldpos.row, oldpos.col, 2));
    self->scheduleRepaint(self->cellRect(pos.row, pos.col, 2));
    return 1;
}

int VTermWidget::cbSetTermProp(VTermProp prop, VTermValue *val, void *user)
{
    auto *self = static_cast<VTermWidget *>(user);
    switch (prop) {
    case VTERM_PROP_CURSORVISIBLE:
        self->m_cursorVisible = val->boolean;
        self->scheduleRepaint(self->cellRect(self->m_cursor.row, self->m_cursor.col));
        break;
    case VTERM_PROP_CURSORSHAPE:
        self->m_cursorShape = val->number;
        break;
    case VTERM_PROP_ALTSCREEN:
        self->m_altScreen = val->boolean;
        self->m_scrollOffset = 0;
        self->scheduleRepaint(self->rect());
        break;
    case VTERM_PROP_MOUSE:
        self->m_mouseMode = val->number;
        break;
    case VTERM_PROP_TITLE:
        if (val->string.initial)
            self->m_titleText.clear();
        self->m_titleText += QString::fromUtf8(val->string.str, int(val->string.len));
        if (val->string.final) {
            if (self->onTitleChanged)
                self->onTitleChanged(self->m_titleText);
            self->window()->setWindowTitle(self->m_titleText);
        }
        break;
    default:
        break;
    }
    return 1;
}

int VTermWidget::cbBell(void *) { return 1; }

int VTermWidget::cbSbPushLine(int cols, const VTermScreenCell *cells, void *user)
{
    auto *self = static_cast<VTermWidget *>(user);
    self->m_scrollback.emplace_back(cells, cells + cols);
    if (int(self->m_scrollback.size()) > self->m_sbLimit)
        self->m_scrollback.pop_front();
    ++self->m_pushed;
    if (self->m_scrollOffset > 0)
        self->m_scrollOffset = std::min<int>(self->m_scrollOffset + 1, int(self->m_scrollback.size()));
    return 1;
}

int VTermWidget::cbSbPopLine(int cols, VTermScreenCell *cells, void *user)
{
    auto *self = static_cast<VTermWidget *>(user);
    if (self->m_scrollback.empty())
        return 0;
    const Line &l = self->m_scrollback.back();
    VTermScreenCell blank{};
    blank.width = 1;
    vterm_state_get_default_colors(self->m_state, &blank.fg, &blank.bg);
    blank.fg.type |= VTERM_COLOR_DEFAULT_FG;
    blank.bg.type |= VTERM_COLOR_DEFAULT_BG;
    for (int i = 0; i < cols; ++i)
        cells[i] = i < int(l.size()) ? l[size_t(i)] : blank;
    self->m_scrollback.pop_back();
    --self->m_pushed;
    self->m_scrollOffset = std::min<int>(self->m_scrollOffset, int(self->m_scrollback.size()));
    return 1;
}

int VTermWidget::cbSbClear(void *user)
{
    auto *self = static_cast<VTermWidget *>(user);
    self->m_scrollback.clear();
    self->m_scrollOffset = 0;
    self->scheduleRepaint(self->rect());
    return 1;
}

int VTermWidget::cbOsc(int command, VTermStringFragment frag, void *user)
{
    auto *self = static_cast<VTermWidget *>(user);
    if (command != 8)
        return 0;
    if (frag.initial)
        self->m_oscBuf.clear();
    self->m_oscBuf.append(frag.str, int(frag.len));
    if (!frag.final)
        return 1;
    // OSC 8 ; params ; URI ST  (empty URI closes the link)
    const int semi = self->m_oscBuf.indexOf(';');
    const QString uri = semi >= 0 ? QString::fromUtf8(self->m_oscBuf.mid(semi + 1)) : QString();
    VTermPos cur;
    vterm_state_get_cursorpos(self->m_state, &cur);
    const CellPos here{self->screenLineId(cur.row), cur.col};
    if (self->m_linkOpen) {
        if (posLess(self->m_linkStart.line, self->m_linkStart.col, here.line, here.col))
            self->m_links.push_back({self->m_linkStart.line, self->m_linkStart.col, here.line, here.col, self->m_linkUri});
        if (self->m_links.size() > 2000)
            self->m_links.erase(self->m_links.begin(), self->m_links.begin() + 1000);
        self->m_linkOpen = false;
    }
    if (!uri.isEmpty()) {
        self->m_linkOpen = true;
        self->m_linkStart = here;
        self->m_linkUri = uri;
    }
    self->scheduleRepaint(self->rect());
    return 1;
}

// ---------------------------------------------------------------- geometry

void VTermWidget::updateCellMetrics()
{
    QFontMetrics fm(m_font);
    m_cw = std::max(1, fm.horizontalAdvance(QLatin1Char('M')));
    m_ch = std::max(1, fm.height());
    m_ascent = fm.ascent();
    m_boldFont = m_font;
    m_boldFont.setBold(true);
    m_italicFont = m_font;
    m_italicFont.setItalic(true);
    m_boldItalicFont = m_boldFont;
    m_boldItalicFont.setItalic(true);
}

QRect VTermWidget::cellRect(int row, int col, int width) const
{
    return QRect(col * m_cw, row * m_ch, width * m_cw, m_ch);
}

void VTermWidget::scheduleRepaint(const QRect &r)
{
    m_pendingDamage += r;
    if (!m_repaintTimer.isActive())
        m_repaintTimer.start();
}

void VTermWidget::resizeEvent(QResizeEvent *)
{
    const int cols = std::max(2, width() / m_cw);
    const int rows = std::max(1, height() / m_ch);
    if (cols != m_cols || rows != m_rows)
        resizeTerminal(rows, cols);
}

void VTermWidget::resizeTerminal(int rows, int columns)
{
    m_rows = rows;
    m_cols = columns;
    vterm_set_size(m_vt, rows, columns);
    vterm_screen_flush_damage(m_screen);
    if (m_pty)
        m_pty->resize(rows, columns);
    update();
}

// ---------------------------------------------------------------- text access

bool VTermWidget::cellAt(qint64 lineId, int col, VTermScreenCell *out) const
{
    if (col < 0 || col >= m_cols)
        return false;
    if (lineId >= m_pushed) {
        const int row = int(lineId - m_pushed);
        if (row >= m_rows)
            return false;
        return vterm_screen_get_cell(m_screen, VTermPos{row, col}, out) != 0;
    }
    const qint64 idx = lineId - firstLineId();
    if (idx < 0)
        return false;
    const Line &l = m_scrollback[size_t(idx)];
    if (col < int(l.size())) {
        *out = l[size_t(col)];
    } else {
        std::memset(out, 0, sizeof *out);
        out->width = 1;
        vterm_state_get_default_colors(m_state, &out->fg, &out->bg);
        out->fg.type |= VTERM_COLOR_DEFAULT_FG;
        out->bg.type |= VTERM_COLOR_DEFAULT_BG;
    }
    return true;
}

QString VTermWidget::lineText(qint64 lineId, int fromCol, int toCol) const
{
    if (toCol < 0 || toCol > m_cols)
        toCol = m_cols;
    QString s;
    VTermScreenCell cell;
    for (int col = fromCol; col < toCol; ++col) {
        if (!cellAt(lineId, col, &cell))
            break;
        if (cell.chars[0] == kContinuation)
            continue;
        if (cell.chars[0] == 0) {
            s += QLatin1Char(' ');
            continue;
        }
        for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i)
            s += ucs4(cell.chars[i]);
    }
    while (s.endsWith(QLatin1Char(' ')))
        s.chop(1);
    return s;
}

QString VTermWidget::screenText() const
{
    QStringList lines;
    for (int r = 0; r < m_rows; ++r)
        lines << lineText(screenLineId(r));
    return lines.join(QLatin1Char('\n'));
}

QStringList VTermWidget::scrollbackText(int maxLines) const
{
    QStringList out;
    const qint64 first = std::max(firstLineId(), m_pushed - qint64(maxLines));
    for (qint64 id = first; id < m_pushed; ++id)
        out << lineText(id);
    return out;
}

QString VTermWidget::debugDump() const
{
    QString s;
    s += QStringLiteral("rows=%1 cols=%2 altScreen=%3 scrollbackLines=%4 cursor=%5,%6 fgpid=%7 bytes=%8 paints=%9\n")
             .arg(m_rows).arg(m_cols).arg(m_altScreen).arg(m_scrollback.size())
             .arg(m_cursor.row).arg(m_cursor.col).arg(foregroundProcessId()).arg(m_bytes).arg(m_paints);
    s += QStringLiteral("links=%1\n").arg(m_links.size());
    for (const Link &l : m_links)
        s += QStringLiteral("  link %1:%2-%3:%4 %5\n").arg(l.startLine).arg(l.startCol).arg(l.endLine).arg(l.endCol).arg(l.uri);
    s += QStringLiteral("--- scrollbackText(5) ---\n") + scrollbackText(5).join(QLatin1Char('\n'));
    s += QStringLiteral("\n--- screenText() ---\n") + screenText() + QLatin1Char('\n');
    return s;
}

// ---------------------------------------------------------------- painting

QColor VTermWidget::toQColor(VTermColor c, bool fg) const
{
    if (fg && VTERM_COLOR_IS_DEFAULT_FG(&c))
        return kDefaultFg;
    if (!fg && VTERM_COLOR_IS_DEFAULT_BG(&c))
        return kDefaultBg;
    vterm_screen_convert_color_to_rgb(m_screen, &c);
    return QColor(c.rgb.red, c.rgb.green, c.rgb.blue);
}

void VTermWidget::paintEvent(QPaintEvent *ev)
{
    ++m_paints;
    QPainter p(this);
    const QRect dirty = ev->rect();
    p.fillRect(dirty, kDefaultBg);

    const int firstRow = std::max(0, dirty.top() / m_ch);
    const int lastRow = std::min(m_rows - 1, dirty.bottom() / m_ch);
    const qint64 top = topVisibleLineId();

    struct Run {
        QString text;
        int col = 0;
        QColor fg;
        const QFont *font = nullptr;
        int underline = 0;
        bool strike = false;
    } run;
    auto flush = [&](int row) {
        if (run.text.isEmpty())
            return;
        p.setFont(*run.font);
        p.setPen(run.fg);
        const int x = run.col * m_cw;
        const int y = row * m_ch;
        p.drawText(x, y + m_ascent, run.text);
        const int w = run.text.size() * m_cw;
        if (run.underline)
            p.drawLine(x, y + m_ascent + 2, x + w - 1, y + m_ascent + 2);
        if (run.underline == VTERM_UNDERLINE_DOUBLE)
            p.drawLine(x, y + m_ascent + 4, x + w - 1, y + m_ascent + 4);
        if (run.strike)
            p.drawLine(x, y + m_ch / 2, x + w - 1, y + m_ch / 2);
        run.text.clear();
    };

    VTermScreenCell cell;
    for (int row = firstRow; row <= lastRow; ++row) {
        const qint64 lineId = top + row;
        for (int col = 0; col < m_cols;) {
            if (!cellAt(lineId, col, &cell)) {
                ++col;
                continue;
            }
            if (cell.chars[0] == kContinuation) {
                ++col;
                continue;
            }
            const int w = std::max(1, int(cell.width));
            QColor fg = toQColor(cell.fg, true);
            QColor bg = toQColor(cell.bg, false);
            if (cell.attrs.reverse)
                std::swap(fg, bg);
            if (m_hasSelection && isSelected(lineId, col))
                bg = kSelectionBg;
            const bool isLink = linkAt(lineId, col) != nullptr;
            if (bg != kDefaultBg)
                p.fillRect(cellRect(row, col, w), bg);
            const QFont *font = cell.attrs.bold ? (cell.attrs.italic ? &m_boldItalicFont : &m_boldFont)
                                                : (cell.attrs.italic ? &m_italicFont : &m_font);
            const int underline = isLink ? VTERM_UNDERLINE_SINGLE : int(cell.attrs.underline);
            const uint32_t ch = cell.chars[0];
            const bool simple = w == 1 && ch >= 0x20 && ch < 0x7f && cell.chars[1] == 0;
            if (simple && !cell.attrs.conceal) {
                if (!run.text.isEmpty()
                    && (run.fg != fg || run.font != font || run.underline != underline
                        || run.strike != bool(cell.attrs.strike) || run.col + run.text.size() != col))
                    flush(row);
                if (run.text.isEmpty()) {
                    run.col = col;
                    run.fg = fg;
                    run.font = font;
                    run.underline = underline;
                    run.strike = cell.attrs.strike;
                }
                run.text += QChar(char16_t(ch));
            } else if (ch != 0 && !cell.attrs.conceal) {
                flush(row);
                QString s;
                for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i)
                    s += ucs4(cell.chars[i]);
                p.setFont(*font);
                p.setPen(fg);
                // Clip to the cell box so fallback glyphs with odd advances stay in their columns.
                p.save();
                p.setClipRect(cellRect(row, col, w));
                p.drawText(col * m_cw, row * m_ch + m_ascent, s);
                p.restore();
                if (underline)
                    p.drawLine(col * m_cw, row * m_ch + m_ascent + 2, (col + w) * m_cw - 1, row * m_ch + m_ascent + 2);
            } else {
                flush(row);
            }
            col += w;
        }
        flush(row);
    }

    // Cursor
    if (m_scrollOffset == 0 && m_cursorVisible && m_cursor.row >= firstRow && m_cursor.row <= lastRow) {
        VTermScreenCell cc;
        const bool ok = vterm_screen_get_cell(m_screen, m_cursor, &cc);
        const int w = ok ? std::max(1, int(cc.width)) : 1;
        const QRect r = cellRect(m_cursor.row, m_cursor.col, w);
        const QColor cursorColor(0xe0, 0xe0, 0xe0);
        if (!m_hasFocus) {
            p.setPen(cursorColor);
            p.setBrush(Qt::NoBrush);
            p.drawRect(r.adjusted(0, 0, -1, -1));
        } else if (m_cursorShape == VTERM_PROP_CURSORSHAPE_UNDERLINE) {
            p.fillRect(QRect(r.left(), r.bottom() - 1, r.width(), 2), cursorColor);
        } else if (m_cursorShape == VTERM_PROP_CURSORSHAPE_BAR_LEFT) {
            p.fillRect(QRect(r.left(), r.top(), 2, r.height()), cursorColor);
        } else {
            p.fillRect(r, cursorColor);
            if (ok && cc.chars[0] && cc.chars[0] != kContinuation) {
                QString s;
                for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cc.chars[i]; ++i)
                    s += ucs4(cc.chars[i]);
                p.setFont(cc.attrs.bold ? m_boldFont : m_font);
                p.setPen(kDefaultBg);
                p.drawText(r.left(), r.top() + m_ascent, s);
            }
        }
        if (!m_preedit.isEmpty()) {
            const QRect pr(r.left(), r.top(), QFontMetrics(m_font).horizontalAdvance(m_preedit) + 2, m_ch);
            p.fillRect(pr, QColor(0x44, 0x44, 0x22));
            p.setPen(Qt::white);
            p.setFont(m_font);
            p.drawText(pr, Qt::AlignLeft | Qt::AlignVCenter, m_preedit);
        }
    }
    // Scroll indicator
    if (m_scrollOffset > 0) {
        const QString label = QStringLiteral("scrollback -%1").arg(m_scrollOffset);
        p.setFont(m_font);
        const QRect lr(width() - (label.size() + 2) * m_cw, 0, (label.size() + 2) * m_cw, m_ch);
        p.fillRect(lr, QColor(0x55, 0x44, 0x00));
        p.setPen(Qt::white);
        p.drawText(lr, Qt::AlignCenter, label);
    }
}

// ---------------------------------------------------------------- keyboard

VTermModifier VTermWidget::modifiers(Qt::KeyboardModifiers m) const
{
    int mod = VTERM_MOD_NONE;
    if (m & Qt::ShiftModifier)
        mod |= VTERM_MOD_SHIFT;
    if (m & Qt::AltModifier)
        mod |= VTERM_MOD_ALT;
    if (m & Qt::ControlModifier)
        mod |= VTERM_MOD_CTRL;
    return VTermModifier(mod);
}

bool VTermWidget::event(QEvent *e)
{
    if (e->type() == QEvent::ShortcutOverride) {
        e->accept(); // the terminal owns every key while focused
        return true;
    }
    return QWidget::event(e);
}

void VTermWidget::setScrollOffset(int off)
{
    off = std::max(0, std::min(off, int(m_scrollback.size())));
    if (off == m_scrollOffset)
        return;
    m_scrollOffset = off;
    update();
}

void VTermWidget::keyPressEvent(QKeyEvent *e)
{
    const Qt::KeyboardModifiers qm = e->modifiers();
    const bool ctrlShift = (qm & Qt::ControlModifier) && (qm & Qt::ShiftModifier);
    if (ctrlShift && e->key() == Qt::Key_C) {
        copySelection(true);
        return;
    }
    if (ctrlShift && e->key() == Qt::Key_V) {
        pasteFrom(true);
        return;
    }
    if (ctrlShift && e->key() == Qt::Key_D) {
        emit dumpRequested();
        return;
    }
    if ((qm & Qt::ShiftModifier) && !(qm & Qt::ControlModifier) && !m_altScreen) {
        const int page = std::max(1, m_rows - 1);
        switch (e->key()) {
        case Qt::Key_PageUp: setScrollOffset(m_scrollOffset + page); return;
        case Qt::Key_PageDown: setScrollOffset(m_scrollOffset - page); return;
        case Qt::Key_Home: setScrollOffset(int(m_scrollback.size())); return;
        case Qt::Key_End: setScrollOffset(0); return;
        default: break;
        }
    }

    VTermModifier mod = modifiers(qm);
    VTermKey key = VTERM_KEY_NONE;
    switch (e->key()) {
    case Qt::Key_Return: key = VTERM_KEY_ENTER; break;
    case Qt::Key_Enter: key = (qm & Qt::KeypadModifier) ? VTERM_KEY_KP_ENTER : VTERM_KEY_ENTER; break;
    case Qt::Key_Tab: key = VTERM_KEY_TAB; break;
    case Qt::Key_Backtab: key = VTERM_KEY_TAB; mod = VTermModifier(mod | VTERM_MOD_SHIFT); break;
    case Qt::Key_Backspace: key = VTERM_KEY_BACKSPACE; break;
    case Qt::Key_Escape: key = VTERM_KEY_ESCAPE; break;
    case Qt::Key_Up: key = VTERM_KEY_UP; break;
    case Qt::Key_Down: key = VTERM_KEY_DOWN; break;
    case Qt::Key_Left: key = VTERM_KEY_LEFT; break;
    case Qt::Key_Right: key = VTERM_KEY_RIGHT; break;
    case Qt::Key_Insert: key = VTERM_KEY_INS; break;
    case Qt::Key_Delete: key = VTERM_KEY_DEL; break;
    case Qt::Key_Home: key = VTERM_KEY_HOME; break;
    case Qt::Key_End: key = VTERM_KEY_END; break;
    case Qt::Key_PageUp: key = VTERM_KEY_PAGEUP; break;
    case Qt::Key_PageDown: key = VTERM_KEY_PAGEDOWN; break;
    default:
        if (e->key() >= Qt::Key_F1 && e->key() <= Qt::Key_F35)
            key = VTermKey(VTERM_KEY_FUNCTION(e->key() - Qt::Key_F1 + 1));
        break;
    }

    if (key != VTERM_KEY_NONE) {
        vterm_keyboard_key(m_vt, key, mod);
    } else if (qm & (Qt::ControlModifier | Qt::AltModifier)) {
        uint32_t c = 0;
        const int k = e->key();
        if (k >= Qt::Key_A && k <= Qt::Key_Z)
            c = uint32_t('a' + (k - Qt::Key_A));
        else if (!e->text().isEmpty() && !(qm & Qt::ControlModifier))
            c = e->text().toUcs4().value(0);
        else if (k >= 0x20 && k < 0x7f)
            c = uint32_t(k);
        if (c == 0)
            return QWidget::keyPressEvent(e);
        vterm_keyboard_unichar(m_vt, c, VTermModifier(mod & ~VTERM_MOD_SHIFT));
    } else if (!e->text().isEmpty()) {
        for (uint c : e->text().toUcs4())
            vterm_keyboard_unichar(m_vt, c, VTERM_MOD_NONE);
    } else {
        return QWidget::keyPressEvent(e);
    }
    setScrollOffset(0);
}

void VTermWidget::inputMethodEvent(QInputMethodEvent *e)
{
    m_preedit = e->preeditString();
    if (!e->commitString().isEmpty()) {
        for (uint c : e->commitString().toUcs4())
            vterm_keyboard_unichar(m_vt, c, VTERM_MOD_NONE);
    }
    update(cellRect(m_cursor.row, 0, m_cols));
    e->accept();
}

// ---------------------------------------------------------------- mouse / selection

VTermWidget::CellPos VTermWidget::posAt(const QPoint &pt) const
{
    const int row = std::max(0, std::min(m_rows - 1, pt.y() / m_ch));
    const int col = std::max(0, std::min(m_cols - 1, pt.x() / m_cw));
    return CellPos{topVisibleLineId() + row, col};
}

bool VTermWidget::isSelected(qint64 line, int col) const
{
    CellPos a = m_selAnchor, b = m_selEnd;
    if (posLess(b.line, b.col, a.line, a.col))
        std::swap(a, b);
    return !posLess(line, col, a.line, a.col) && !posLess(b.line, b.col, line, col);
}

QString VTermWidget::selectedText() const
{
    CellPos a = m_selAnchor, b = m_selEnd;
    if (posLess(b.line, b.col, a.line, a.col))
        std::swap(a, b);
    QStringList out;
    for (qint64 l = a.line; l <= b.line; ++l)
        out << lineText(l, l == a.line ? a.col : 0, l == b.line ? b.col + 1 : -1);
    return out.join(QLatin1Char('\n'));
}

void VTermWidget::copySelection(bool clipboard)
{
    if (!m_hasSelection)
        return;
    QApplication::clipboard()->setText(selectedText(), clipboard ? QClipboard::Clipboard : QClipboard::Selection);
}

void VTermWidget::pasteFrom(bool clipboard)
{
    const QString t = QApplication::clipboard()->text(clipboard ? QClipboard::Clipboard : QClipboard::Selection);
    if (!t.isEmpty())
        sendText(t, true);
}

const VTermWidget::Link *VTermWidget::linkAt(qint64 line, int col) const
{
    for (const Link &l : m_links) {
        if (!posLess(line, col, l.startLine, l.startCol) && posLess(line, col, l.endLine, l.endCol))
            return &l;
    }
    return nullptr;
}

QString VTermWidget::currentDirectory() const
{
    // Linux only. macOS: proc_pidinfo(PROC_PIDVNODEPATHINFO). Windows: rely on OSC 7.
    qint64 pid = foregroundProcessId();
    if (pid <= 0)
        pid = shellPid();
    const QString target = QFileInfo(QStringLiteral("/proc/%1/cwd").arg(pid)).symLinkTarget();
    return target.isEmpty() ? QDir::currentPath() : target;
}

bool VTermWidget::activateAt(const CellPos &pos)
{
    if (const Link *l = linkAt(pos.line, pos.col)) {
        emit linkActivated(l->uri);
        if (onLinkActivated)
            onLinkActivated(l->uri);
        return true;
    }
    // Token under the pointer: build text with a column->index map (wide chars).
    QString text;
    std::vector<int> colToIndex(size_t(m_cols), -1);
    VTermScreenCell cell;
    for (int col = 0; col < m_cols; ++col) {
        if (!cellAt(pos.line, col, &cell))
            break;
        if (cell.chars[0] == kContinuation) {
            colToIndex[size_t(col)] = std::max(0, int(text.size()) - 1);
            continue;
        }
        colToIndex[size_t(col)] = int(text.size());
        if (cell.chars[0] == 0)
            text += QLatin1Char(' ');
        else
            for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i)
                text += ucs4(cell.chars[i]);
    }
    int idx = colToIndex[size_t(pos.col)];
    if (idx < 0 || idx >= text.size() || text[idx].isSpace())
        return false;
    int s = idx, e = idx;
    while (s > 0 && !text[s - 1].isSpace())
        --s;
    while (e + 1 < text.size() && !text[e + 1].isSpace())
        ++e;
    QString token = text.mid(s, e - s + 1);
    static const QString strip = QStringLiteral("\"'`()[]{}<>,;");
    while (!token.isEmpty() && strip.contains(token.front()))
        token.remove(0, 1);
    while (!token.isEmpty() && (strip.contains(token.back()) || token.back() == QLatin1Char('.') || token.back() == QLatin1Char(':')))
        token.chop(1);
    if (token.startsWith(QLatin1String("http://")) || token.startsWith(QLatin1String("https://"))) {
        emit linkActivated(token);
        if (onLinkActivated)
            onLinkActivated(token);
        return true;
    }
    // Strip grep/compiler style ":line[:col]" suffixes.
    static const QRegularExpression lineSuffix(QStringLiteral(":\\d+(:\\d+)?$"));
    QString candidate = token;
    if (token.startsWith(QLatin1String("~/")))
        candidate = QDir::homePath() + token.mid(1);
    for (int attempt = 0; attempt < 2; ++attempt) {
        QFileInfo fi(QDir(currentDirectory()), candidate);
        if (!candidate.isEmpty() && fi.exists()) {
            const QString path = fi.absoluteFilePath();
            emit pathActivated(path);
            if (onPathActivated)
                onPathActivated(path);
            return true;
        }
        candidate.remove(lineSuffix);
    }
    return false;
}

void VTermWidget::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    const CellPos pos = posAt(e->pos());
    if (m_mouseMode != VTERM_PROP_MOUSE_NONE && !(e->modifiers() & Qt::ShiftModifier)) {
        const int row = int(pos.line - topVisibleLineId());
        const int button = e->button() == Qt::LeftButton ? 1 : e->button() == Qt::MiddleButton ? 2 : 3;
        vterm_mouse_move(m_vt, row, pos.col, modifiers(e->modifiers()));
        vterm_mouse_button(m_vt, button, true, modifiers(e->modifiers()));
        return;
    }
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier)) {
        activateAt(pos);
        return;
    }
    if (e->button() == Qt::MiddleButton) {
        pasteFrom(false);
        return;
    }
    if (e->button() == Qt::LeftButton) {
        m_selecting = true;
        m_hasSelection = false;
        m_selAnchor = m_selEnd = pos;
        update();
    }
}

void VTermWidget::mouseMoveEvent(QMouseEvent *e)
{
    const CellPos pos = posAt(e->pos());
    if (m_mouseMode != VTERM_PROP_MOUSE_NONE && !(e->modifiers() & Qt::ShiftModifier)) {
        vterm_mouse_move(m_vt, int(pos.line - topVisibleLineId()), pos.col, modifiers(e->modifiers()));
        return;
    }
    if (!m_selecting)
        return;
    if (e->pos().y() < 0)
        setScrollOffset(m_scrollOffset + 1);
    else if (e->pos().y() > height())
        setScrollOffset(m_scrollOffset - 1);
    m_selEnd = pos;
    m_hasSelection = true;
    update();
}

void VTermWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_mouseMode != VTERM_PROP_MOUSE_NONE && !(e->modifiers() & Qt::ShiftModifier)) {
        const CellPos pos = posAt(e->pos());
        const int button = e->button() == Qt::LeftButton ? 1 : e->button() == Qt::MiddleButton ? 2 : 3;
        vterm_mouse_move(m_vt, int(pos.line - topVisibleLineId()), pos.col, modifiers(e->modifiers()));
        vterm_mouse_button(m_vt, button, false, modifiers(e->modifiers()));
        return;
    }
    if (m_selecting && m_hasSelection)
        copySelection(false); // X11 primary selection
    m_selecting = false;
}

void VTermWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (m_mouseMode != VTERM_PROP_MOUSE_NONE && !(e->modifiers() & Qt::ShiftModifier))
        return mousePressEvent(e);
    const CellPos pos = posAt(e->pos());
    const QString t = lineText(pos.line);
    // Word selection (ASCII-column approximation).
    auto isWord = [&](int c) { return c >= 0 && c < t.size() && !t[c].isSpace(); };
    if (!isWord(pos.col))
        return;
    int s = pos.col, en = pos.col;
    while (isWord(s - 1))
        --s;
    while (isWord(en + 1))
        ++en;
    m_selAnchor = {pos.line, s};
    m_selEnd = {pos.line, en};
    m_hasSelection = true;
    m_selecting = false;
    copySelection(false);
    update();
}

void VTermWidget::wheelEvent(QWheelEvent *e)
{
    const int steps = e->angleDelta().y() / 40; // ~3 lines per notch
    if (m_mouseMode != VTERM_PROP_MOUSE_NONE) {
        const CellPos pos = posAt(e->position().toPoint());
        vterm_mouse_move(m_vt, int(pos.line - topVisibleLineId()), pos.col, modifiers(e->modifiers()));
        vterm_mouse_button(m_vt, steps > 0 ? 4 : 5, true, modifiers(e->modifiers()));
        return;
    }
    if (m_altScreen) {
        // Like most terminals: wheel becomes arrow keys in full-screen apps without mouse mode.
        for (int i = 0; i < std::abs(steps); ++i)
            vterm_keyboard_key(m_vt, steps > 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN, VTERM_MOD_NONE);
        return;
    }
    setScrollOffset(m_scrollOffset + steps);
}

void VTermWidget::focusInEvent(QFocusEvent *)
{
    m_hasFocus = true;
    vterm_state_focus_in(m_state);
    update(cellRect(m_cursor.row, m_cursor.col, 2));
}

void VTermWidget::focusOutEvent(QFocusEvent *)
{
    m_hasFocus = false;
    vterm_state_focus_out(m_state);
    update(cellRect(m_cursor.row, m_cursor.col, 2));
}

} // namespace relay
