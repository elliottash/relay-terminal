// SPDX-License-Identifier: GPL-3.0-or-later
#include "RemotePane.h"

#include "CopyOnSelect.h"
#include "Hints.h"
#include "Theme.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QSysInfo>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace relay {

namespace remoteview {

namespace {

// xterm's 6x6x6 cube and grey ramp, for palette entries 16..255 (app/screen.js PALETTE).
QColor xterm(int index)
{
    static const QVector<QColor> base = [] {
        QVector<QColor> colors;
        for (const char *hex : {"#000000", "#cd0000", "#00cd00", "#cdcd00", "#1e90ff", "#cd00cd",
                                "#00cdcd", "#e5e5e5", "#4c4c4c", "#ff0000", "#00ff00", "#ffff00",
                                "#4682b4", "#ff00ff", "#00ffff", "#ffffff"})
            colors.append(QColor(QLatin1String(hex)));
        const int levels[6] = {0, 95, 135, 175, 215, 255};
        for (int r = 0; r < 6; ++r)
            for (int g = 0; g < 6; ++g)
                for (int b = 0; b < 6; ++b) colors.append(QColor(levels[r], levels[g], levels[b]));
        for (int grey = 0; grey < 24; ++grey) {
            const int value = 8 + grey * 10;
            colors.append(QColor(value, value, value));
        }
        return colors;
    }();
    return index >= 0 && index < base.size() ? base.at(index) : QColor();
}

QString str(const QJsonValue &value) { return value.isString() ? value.toString() : QString(); }

} // namespace

Segs segsOf(const QJsonArray &runs)
{
    Segs segs;
    segs.reserve(runs.size());
    for (const QJsonValue &value : runs) {
        const QJsonArray run = value.toArray();
        if (run.isEmpty() || !run.at(0).isString()) continue;
        Seg seg;
        seg.text = run.at(0).toString();
        seg.fg = quint32(qint64(run.at(1).toDouble()));
        seg.bg = quint32(qint64(run.at(2).toDouble()));
        seg.attrs = run.at(3).toInt();
        segs.append(seg);
    }
    return segs;
}

int columnsOf(char32_t cp)
{
    if (cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0x2060 || cp == 0xFEFF) return 0;
    if (cp >= 0xFE00 && cp <= 0xFE0F) return 0;   // variation selectors ride on their glyph
    const auto category = QChar::category(cp);
    if (category == QChar::Mark_NonSpacing || category == QChar::Mark_Enclosing) return 0;
    const bool wide = (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E)
        || (cp >= 0x3041 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF)
        || (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF)
        || (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF)
        || (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60)
        || (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F)
        || (cp >= 0x1F900 && cp <= 0x1F9FF) || (cp >= 0x20000 && cp <= 0x3FFFD);
    return wide ? 2 : 1;
}

QVector<Cell> cellsOf(const Segs &segs, int cols, int cursorCol)
{
    QVector<Cell> cells;
    cells.reserve(cols);
    for (const Seg &seg : segs) {
        const bool conceal = seg.attrs & Conceal;
        for (char32_t cp : seg.text.toUcs4()) {
            const int width = columnsOf(cp);
            if (width == 0) {
                if (!cells.isEmpty() && !conceal) cells.last().text += QString::fromUcs4(&cp, 1);
                continue;
            }
            Cell cell;
            cell.text = conceal ? QStringLiteral(" ") : QString::fromUcs4(&cp, 1);
            cell.fg = seg.fg;
            cell.bg = seg.bg;
            cell.attrs = seg.attrs;
            cells.append(cell);
            if (width == 2) {
                Cell tail = cell;
                tail.text.clear();
                tail.tail = true;
                cells.append(tail);
            }
        }
    }
    const int want = std::max(cols, cursorCol + 1);
    while (cells.size() < want) cells.append(Cell{QStringLiteral(" "), 0, 0, 0, false, false});
    if (cells.size() > want && cols > 0) cells.resize(std::max(cols, cursorCol + 1));
    if (cursorCol >= 0 && cursorCol < cells.size()) {
        // On the tail of a wide glyph the cursor belongs to the glyph.
        int at = cursorCol;
        if (cells[at].tail && at > 0) --at;
        cells[at].cursor = true;
    }
    return cells;
}

QColor colorOf(quint32 packed, const QVector<QColor> &ansi16)
{
    const quint32 kind = (packed >> 24) & 0xff;
    const quint32 value = packed & 0xffffff;
    if (kind == 1) {
        const int index = int(value & 0xff);
        if (index < 16 && ansi16.size() == 16 && ansi16.at(index).isValid()) return ansi16.at(index);
        return xterm(index);
    }
    if (kind == 2) return QColor(QRgb(0xff000000u | value));
    return QColor();
}

QByteArray keyBytes(int key, Qt::KeyboardModifiers modifiers, const QString &text)
{
    const bool ctrl = modifiers & Qt::ControlModifier;
    const bool alt = modifiers & Qt::AltModifier;
    const bool shift = modifiers & Qt::ShiftModifier;
    if (modifiers & Qt::MetaModifier) return {};
    QByteArray named;
    switch (key) {
    case Qt::Key_Return: case Qt::Key_Enter: named = "\r"; break;
    case Qt::Key_Tab: named = "\t"; break;
    case Qt::Key_Backtab: named = "\x1b[Z"; break;
    case Qt::Key_Escape: named = "\x1b"; break;
    case Qt::Key_Backspace: named = "\x7f"; break;
    case Qt::Key_Delete: named = "\x1b[3~"; break;
    case Qt::Key_Insert: named = "\x1b[2~"; break;
    case Qt::Key_Up: named = "\x1b[A"; break;
    case Qt::Key_Down: named = "\x1b[B"; break;
    case Qt::Key_Right: named = "\x1b[C"; break;
    case Qt::Key_Left: named = "\x1b[D"; break;
    case Qt::Key_Home: named = "\x1b[H"; break;
    case Qt::Key_End: named = "\x1b[F"; break;
    case Qt::Key_PageUp: named = "\x1b[5~"; break;
    case Qt::Key_PageDown: named = "\x1b[6~"; break;
    case Qt::Key_F1: named = "\x1bOP"; break;
    case Qt::Key_F2: named = "\x1bOQ"; break;
    case Qt::Key_F3: named = "\x1bOR"; break;
    case Qt::Key_F4: named = "\x1bOS"; break;
    case Qt::Key_F5: named = "\x1b[15~"; break;
    case Qt::Key_F6: named = "\x1b[17~"; break;
    case Qt::Key_F7: named = "\x1b[18~"; break;
    case Qt::Key_F8: named = "\x1b[19~"; break;
    case Qt::Key_F9: named = "\x1b[20~"; break;
    case Qt::Key_F10: named = "\x1b[21~"; break;
    case Qt::Key_F11: named = "\x1b[23~"; break;
    case Qt::Key_F12: named = "\x1b[24~"; break;
    default: break;
    }
    if (!named.isEmpty()) return alt ? QByteArray("\x1b") + named : named;
    if (ctrl) {
        // Ctrl+A..Ctrl+_ are the character minus 64; Ctrl+Space is NUL.
        if (key == Qt::Key_Space) return QByteArray(1, '\0');
        int code = -1;
        if (key >= Qt::Key_A && key <= Qt::Key_Z) code = key - Qt::Key_A + 'A';
        else if (key == Qt::Key_At) code = '@';
        else if (key == Qt::Key_BracketLeft) code = '[';
        else if (key == Qt::Key_Backslash) code = '\\';
        else if (key == Qt::Key_BracketRight) code = ']';
        else if (key == Qt::Key_AsciiCircum) code = '^';
        else if (key == Qt::Key_Underscore) code = '_';
        if (code < 0 || shift) return {};   // Ctrl+Shift is the app's, never the program's
        const QByteArray byte(1, char(code - 64));
        return alt ? QByteArray("\x1b") + byte : byte;
    }
    if (text.isEmpty() || text.at(0).category() == QChar::Other_Control) return {};
    const QByteArray bytes = text.toUtf8();
    return alt ? QByteArray("\x1b") + bytes : bytes;
}

// ----- ScreenModel ---------------------------------------------------------------------------------

ScreenModel::Applied ScreenModel::apply(const QJsonObject &frame)
{
    Applied applied;
    const QString kind = str(frame.value(QStringLiteral("t")));
    if (kind == QLatin1String("screen_snapshot")) {
        const int rows = frame.value(QStringLiteral("rows")).toInt(m_rows);
        const int cols = frame.value(QStringLiteral("cols")).toInt(m_cols);
        const bool alt = frame.value(QStringLiteral("alt")).toBool();
        // A snapshot is usually a full repaint and says nothing about what is above. Only a change
        // of geometry or of screen breaks the join with the history held (screen.js, apply()).
        applied.broke = m_hasFrame && (rows != m_rows || cols != m_cols || alt != m_alt);
        m_rows = std::max(1, rows);
        m_cols = std::max(1, cols);
        m_alt = alt;
        m_lines.clear();
        if (applied.broke) resetHistory();
        applied.snapshot = true;
        m_hasFrame = true;
    } else if (kind != QLatin1String("screen_diff")) {
        return applied;
    }
    for (const QJsonValue &value : frame.value(QStringLiteral("lines")).toArray()) {
        const QJsonObject line = value.toObject();
        const int row = line.value(QStringLiteral("row")).toInt(-1);
        if (row < 0) continue;
        m_lines.insert(row, segsOf(line.value(QStringLiteral("segs")).toArray()));
        if (!applied.snapshot) applied.rows.append(row);
    }
    const QJsonObject cursor = frame.value(QStringLiteral("cursor")).toObject();
    if (!cursor.isEmpty()) {
        m_cursor.row = cursor.value(QStringLiteral("row")).toInt(m_cursor.row);
        m_cursor.col = cursor.value(QStringLiteral("col")).toInt(m_cursor.col);
        m_cursor.visible = cursor.value(QStringLiteral("visible")).toBool(true);
    }
    const QJsonValue base = frame.value(QStringLiteral("base"));
    if (base.isDouble()) m_liveBase = qint64(base.toDouble());
    checkSeam();
    if (applied.snapshot)
        for (int row = 0; row < m_rows; ++row) applied.rows.append(row);
    return applied;
}

void ScreenModel::resetHistory()
{
    m_history.clear();
    m_historyTop = -1;
    m_more = true;
    m_pending = false;
}

qint64 ScreenModel::historyBottom() const
{
    return m_history.isEmpty() ? -1 : m_history.last().row + 1;
}

qint64 ScreenModel::gapRows() const
{
    const qint64 bottom = historyBottom();
    if (bottom < 0 || m_liveBase < 0) return 0;
    return std::max<qint64>(0, m_liveBase - bottom);
}

// The scrollback shrank under what is held — a clear, a reset, the alternate screen.
void ScreenModel::checkSeam()
{
    const qint64 bottom = historyBottom();
    if (bottom < 0 || m_liveBase < 0) return;
    if (m_liveBase < bottom) resetHistory();
}

bool ScreenModel::nearSeam(int firstVisible, int visible) const
{
    const qint64 gap = gapRows();
    if (gap == 0) return false;
    if (gap <= kPage * 2) return true;
    const int seam = int(m_history.size());
    return seam - (firstVisible + visible) <= kPrefetch;
}

ScreenModel::Request ScreenModel::nextRequest(int firstVisible, int visible)
{
    Request request;
    if (m_pending || !m_hasFrame) return request;
    // The seam first: a hole in the middle of the column is worse than a page short of the top.
    if (nearSeam(firstVisible, visible)) {
        const qint64 bottom = historyBottom();
        const int want = int(std::min<qint64>(gapRows(), kPage));
        m_pending = true;
        return {true, bottom + want, want};
    }
    if (!m_more) return request;
    if (m_historyTop >= 0 && firstVisible > kPrefetch) return request;
    // The first page ends at `base`, not at the newest row: the desktop's own view may be sitting
    // back in its scrollback (section 6.5).
    const qint64 before = m_historyTop >= 0 ? m_historyTop : m_liveBase;
    if (before == 0) { m_more = false; return request; }
    m_pending = true;
    return {true, before, kPage};
}

ScreenModel::Inserted ScreenModel::applyHistory(const QJsonObject &reply)
{
    m_pending = false;
    QVector<HistoryRow> lines;
    for (const QJsonValue &value : reply.value(QStringLiteral("lines")).toArray()) {
        const QJsonObject line = value.toObject();
        const QJsonValue row = line.value(QStringLiteral("row"));
        if (!row.isDouble()) continue;
        lines.append({qint64(row.toDouble()), segsOf(line.value(QStringLiteral("segs")).toArray())});
    }
    std::sort(lines.begin(), lines.end(), [](const HistoryRow &a, const HistoryRow &b) { return a.row < b.row; });
    Inserted inserted;
    const qint64 bottom = historyBottom();
    auto insertTop = [&](QVector<HistoryRow> fresh) {
        inserted.top = true;
        if (fresh.isEmpty()) {
            if (m_historyTop < 0 || m_historyTop <= 0) m_more = false;
            return;
        }
        if (m_historyTop >= 0 && fresh.last().row + 1 != m_historyTop) {
            // A hole: a scrollback ring evicted underneath us. Start again from this page.
            inserted.removed = int(m_history.size());
            m_history.clear();
        }
        m_history = fresh + m_history;
        inserted.added = int(fresh.size());
        m_historyTop = m_history.first().row;
        m_more = m_historyTop > 0;
    };
    if (bottom < 0) {
        insertTop(lines);
    } else if (!lines.isEmpty() && lines.first().row >= bottom) {
        // Rows that left the live screen while somebody read further up: below the buffer, above
        // the live block, which is where the program put them.
        QVector<HistoryRow> fresh;
        for (const HistoryRow &line : lines) if (line.row >= bottom) fresh.append(line);
        if (!fresh.isEmpty() && fresh.first().row == bottom) {
            m_history += fresh;
            inserted.added = int(fresh.size());
        }
    } else {
        QVector<HistoryRow> fresh;
        for (const HistoryRow &line : lines) if (line.row < m_historyTop) fresh.append(line);
        insertTop(fresh);
    }
    checkSeam();
    return inserted;
}

int ScreenModel::trim(int firstVisible)
{
    const int excess = int(m_history.size()) - kMax;
    if (excess <= 0) return 0;
    const int room = firstVisible - kPrefetch;
    const int drop = std::min(excess, room);
    if (drop <= 0) return 0;
    m_history.remove(0, drop);
    m_historyTop = m_history.isEmpty() ? -1 : m_history.first().row;
    m_more = m_historyTop < 0 || m_historyTop > 0;
    return drop;
}

QVector<Cell> ScreenModel::cellsAt(int index) const
{
    if (index < 0) return {};
    if (index < m_history.size()) return cellsOf(m_history.at(index).segs, m_cols);
    const int row = index - int(m_history.size());
    if (row >= m_rows) return {};
    const bool cursorHere = m_cursor.visible && m_cursor.row == row;
    return cellsOf(m_lines.value(row), m_cols, cursorHere ? m_cursor.col : -1);
}

namespace {
const QStringList &actionOrder()
{
    static const QStringList order{QStringLiteral("edit"), QStringLiteral("steer"), QStringLiteral("send_now"),
                                   QStringLiteral("to_queue"), QStringLiteral("up"), QStringLiteral("down"),
                                   QStringLiteral("remove")};
    return order;
}
} // namespace

QStringList offeredActions(const QJsonObject &row)
{
    QStringList listed;
    for (const QJsonValue &value : row.value(QStringLiteral("actions")).toArray())
        if (value.isString()) listed.append(value.toString());
    QStringList out;
    for (const QString &action : actionOrder())
        if (listed.contains(action)) out.append(action);
    return out;
}

// The desktop's own words for each action (the queue strip's hint line and row tooltips), as the
// phone's sheet uses them (app/pane.js ACTION_WORDS).
QString actionWords(const QString &action, const QString &kind)
{
    if (action == QLatin1String("remove")) return kind == QLatin1String("steer") ? QStringLiteral("Withdraw") : QStringLiteral("Remove");
    if (action == QLatin1String("edit")) return QStringLiteral("Edit");
    if (action == QLatin1String("to_queue")) return QStringLiteral("Back to the queue");
    if (action == QLatin1String("send_now")) return QStringLiteral("Send now");
    if (action == QLatin1String("steer")) return QStringLiteral("At the next tool call");
    if (action == QLatin1String("up")) return QStringLiteral("Move up");
    if (action == QLatin1String("down")) return QStringLiteral("Move down");
    return QString();
}

} // namespace remoteview

using namespace remoteview;

namespace {

// The sidecar lives beside the backend: <data>/remote in an install, the source tree otherwise —
// the same search src/RemoteShare.cpp makes for remote/gui_host.py.
QString viewerRoot()
{
    const QStringList candidates{
        QString::fromLocal8Bit(qgetenv("RELAY_REMOTE_DIR")),
#ifdef RELAY_SOURCE_DIR
        QStringLiteral(RELAY_SOURCE_DIR),
#endif
#ifdef RELAY_DATA_DIR
        QStringLiteral(RELAY_DATA_DIR),
#endif
    };
    for (const QString &candidate : candidates) {
        if (candidate.isEmpty()) continue;
        if (QFileInfo::exists(candidate + QStringLiteral("/remote/viewer.py"))) return candidate;
    }
    return QString();
}

// The terminal font from data/theme/terminal.conf, as src/EngineBackend.cpp reads it.
QFont terminalFont(int *lineSpacing, int *margin)
{
    QFont font(QStringLiteral("Hack"), 11);
    font.setStyleHint(QFont::Monospace);
    if (!QFontDatabase().families().contains(QStringLiteral("Hack")))
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    *lineSpacing = 2;
    *margin = 8;
    const QString dir = theme::themeDataDir();
    if (!dir.isEmpty()) {
        const QSettings profile(dir + QStringLiteral("/terminal.conf"), QSettings::IniFormat);
        QFont configured;
        const QString spec = profile.value(QStringLiteral("Appearance/Font")).toString();
        if (!spec.isEmpty() && configured.fromString(spec)) font = configured;
        *lineSpacing = profile.value(QStringLiteral("Appearance/LineSpacing"), 2).toInt();
        *margin = std::min(16, profile.value(QStringLiteral("Appearance/TerminalMargin"), 8).toInt());
    }
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setKerning(false);
    return font;
}

QString messageId()
{
    QByteArray bytes(9, '\0');
    for (char &byte : bytes) byte = char(QRandomGenerator::global()->bounded(256));
    return QString::fromLatin1(bytes.toBase64());
}

QLabel *plainLabel(const QString &objectName, QWidget *parent = nullptr)
{
    auto *label = new QLabel(parent);
    label->setObjectName(objectName);
    label->setTextFormat(Qt::PlainText);   // every string from the wire is plain text
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    return label;
}

void setPlain(QLabel *label, const QString &text)
{
    label->setText(text);
    label->setVisible(!text.isEmpty());
}

// "&" in a menu entry is a mnemonic to Qt; a desktop's label is shown as it was written.
QString menuText(QString text) { return text.replace(QLatin1Char('&'), QStringLiteral("&&")); }

} // namespace

// ----- RemoteScreen ------------------------------------------------------------------------------

RemoteScreen::RemoteScreen(QWidget *parent) : QAbstractScrollArea(parent)
{
    setObjectName(QStringLiteral("remoteScreen"));
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setCursor(Qt::IBeamCursor);
    viewport()->setAutoFillBackground(false);
    setAttribute(Qt::WA_InputMethodEnabled, false);
    m_baseFont = terminalFont(&m_lineSpacing, &m_margin);
    applyTheme();
    connect(theme::notifier(), &theme::Notifier::themeChanged, this, &RemoteScreen::applyTheme);
}

QSize RemoteScreen::sizeHint() const { return {640, 360}; }

void RemoteScreen::applyTheme()
{
    const theme::ThemeSpec &spec = theme::active();
    m_bg = spec.terminalBackground.isValid() ? spec.terminalBackground : theme::Background;
    m_fg = spec.terminalForeground.isValid() ? spec.terminalForeground : theme::Text;
    m_cursor = spec.terminalCursor.isValid() ? spec.terminalCursor : m_fg;
    m_ansi = spec.ansi.size() == 16 ? spec.ansi : QVector<QColor>();
    fit();
    viewport()->update();
}

// The desktop owns the size. The terminal font is used as configured, and only made smaller when
// the desktop's columns would not fit — never larger, and never a resize request back. Never below
// the app's legibility floor either (theme::FloorPt): a desktop pane wider than that still leaves
// is scrolled sideways rather than drawn in type nobody can read.
void RemoteScreen::fit()
{
    QFont font = m_baseFont;
    QFontMetricsF metrics(font);
    qreal cell = metrics.horizontalAdvance(QLatin1Char('M'));
    const int cols = m_model.cols();
    const qreal available = viewport()->width() - 2 * m_margin;
    if (available > 0 && cols > 0 && cell * cols > available) {
        const qreal points = font.pointSizeF() > 0 ? font.pointSizeF() : 11.0;
        font.setPointSizeF(std::max<qreal>(theme::FloorPt, points * available / (cell * cols) - 0.05));
        metrics = QFontMetricsF(font);
        cell = metrics.horizontalAdvance(QLatin1Char('M'));
    }
    m_font = font;
    m_cellW = cell;
    m_cellH = metrics.lineSpacing() + m_lineSpacing;
    m_ascent = metrics.ascent() + m_lineSpacing / 2.0;
}

int RemoteScreen::visibleRows() const
{
    return std::max(1, int((viewport()->height() - 2 * m_margin) / m_cellH));
}

int RemoteScreen::firstVisible() const { return verticalScrollBar()->value(); }

bool RemoteScreen::atBottom() const { return verticalScrollBar()->value() >= verticalScrollBar()->maximum(); }

void RemoteScreen::updateRange(bool keepBottom)
{
    QScrollBar *bar = verticalScrollBar();
    const int visible = visibleRows();
    const QSignalBlocker block(bar);
    bar->setRange(0, std::max(0, m_model.columnRows() - visible));
    bar->setPageStep(visible);
    bar->setSingleStep(3);
    if (keepBottom) bar->setValue(bar->maximum());
    QScrollBar *across = horizontalScrollBar();
    const int width = int(std::ceil(2 * m_margin + m_model.cols() * m_cellW));
    across->setRange(0, std::max(0, width - viewport()->width()));
    across->setPageStep(viewport()->width());
    across->setSingleStep(int(m_cellW * 4));
}

void RemoteScreen::setBehind(bool behind)
{
    if (behind == m_behind) return;
    m_behind = behind;
    emit behindChanged(behind);
}

void RemoteScreen::setHistoryEnabled(bool on)
{
    m_historyEnabled = on;
    requestIfNeeded();
}

void RemoteScreen::apply(const QJsonObject &frame)
{
    const bool wasAtBottom = atBottom();
    const ScreenModel::Applied applied = m_model.apply(frame);
    if (applied.snapshot) fit();
    updateRange(wasAtBottom);
    if (wasAtBottom) setBehind(false);
    else if (!applied.rows.isEmpty() && !applied.snapshot) setBehind(true);
    viewport()->update();
    requestIfNeeded();
}

void RemoteScreen::applyHistory(const QJsonObject &reply)
{
    const bool wasAtBottom = atBottom();
    const int value = verticalScrollBar()->value();
    const ScreenModel::Inserted inserted = m_model.applyHistory(reply);
    if (inserted.top) {
        // Everything above the viewport grew by exactly this much: the reader stays on their line.
        updateRange(false);
        verticalScrollBar()->setValue(value + inserted.added - inserted.removed);
    } else {
        updateRange(wasAtBottom);
    }
    if (const int trimmed = m_model.trim(firstVisible())) {
        const int now = verticalScrollBar()->value();
        updateRange(false);
        verticalScrollBar()->setValue(now - trimmed);
    }
    viewport()->update();
    requestIfNeeded();
}

void RemoteScreen::historyFailed()
{
    m_model.setPending(false);
    m_model.setMore(false);
}

void RemoteScreen::requestIfNeeded()
{
    if (!m_historyEnabled) return;
    const ScreenModel::Request request = m_model.nextRequest(firstVisible(), visibleRows());
    if (request.want) emit historyWanted(request.before, request.count);
}

void RemoteScreen::toLive()
{
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    setBehind(false);
    requestIfNeeded();
}

void RemoteScreen::scrollContentsBy(int, int)
{
    viewport()->update();
    if (atBottom()) setBehind(false);
    requestIfNeeded();
}

void RemoteScreen::resizeEvent(QResizeEvent *event)
{
    const bool wasAtBottom = atBottom();
    QAbstractScrollArea::resizeEvent(event);
    fit();
    updateRange(wasAtBottom);
}

void RemoteScreen::paintEvent(QPaintEvent *)
{
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), m_bg);
    if (!m_model.hasFrame()) {
        painter.setPen(theme::TextMuted);
        painter.drawText(viewport()->rect(), Qt::AlignCenter, QStringLiteral("Waiting for the desktop's screen…"));
        return;
    }
    QFont bold = m_font, italic = m_font, boldItalic = m_font;
    bold.setBold(true);
    italic.setItalic(true);
    boldItalic.setBold(true);
    boldItalic.setItalic(true);
    const QPoint selA = std::min(m_selStart, m_selEnd, [](const QPoint &a, const QPoint &b) {
        return a.y() < b.y() || (a.y() == b.y() && a.x() < b.x()); });
    const QPoint selB = (selA == m_selStart) ? m_selEnd : m_selStart;
    const bool hasSelection = m_selStart.x() >= 0 && m_selStart != m_selEnd;
    QColor selection = theme::Accent;
    selection.setAlpha(110);
    const int first = firstVisible();
    const int count = visibleRows() + 1;
    const qreal descent = QFontMetricsF(m_font).descent();
    painter.translate(-horizontalScrollBar()->value(), 0);
    for (int i = 0; i < count; ++i) {
        const int index = first + i;
        if (index >= m_model.columnRows()) break;
        const qreal y = m_margin + i * m_cellH;
        const QVector<Cell> cells = m_model.cellsAt(index);
        for (int col = 0; col < cells.size(); ++col) {
            const Cell &cell = cells.at(col);
            if (cell.tail) continue;
            const int span = (col + 1 < cells.size() && cells.at(col + 1).tail) ? 2 : 1;
            QColor fg = colorOf(cell.fg, m_ansi);
            QColor bg = colorOf(cell.bg, m_ansi);
            if (cell.attrs & Reverse) {
                const QColor swap = fg.isValid() ? fg : m_fg;
                fg = bg.isValid() ? bg : m_bg;
                bg = swap;
            }
            if (!fg.isValid()) fg = m_fg;
            const QRectF box(m_margin + col * m_cellW, y, span * m_cellW, m_cellH);
            const bool focused = hasFocus();
            if (cell.cursor && focused) {
                bg = m_cursor;
                fg = m_bg;
            }
            if (bg.isValid()) painter.fillRect(box, bg);
            if (hasSelection) {
                const QPoint at(col, index);
                const bool after = at.y() > selA.y() || (at.y() == selA.y() && at.x() >= selA.x());
                const bool before = at.y() < selB.y() || (at.y() == selB.y() && at.x() < selB.x());
                if (after && before) painter.fillRect(box, selection);
            }
            if (cell.cursor && !focused) {
                painter.setPen(m_cursor);
                painter.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));
            }
            if (cell.text.isEmpty() || cell.text == QLatin1String(" ")) continue;
            if (cell.attrs & Faint) fg.setAlphaF(0.65);
            const bool isBold = cell.attrs & Bold, isItalic = cell.attrs & Italic;
            painter.setFont(isBold && isItalic ? boldItalic : isBold ? bold : isItalic ? italic : m_font);
            painter.setPen(fg);
            painter.drawText(QPointF(box.left(), y + m_ascent), cell.text);
            if (cell.attrs & (Underline | DoubleUnderline | CurlyUnderline)) {
                const qreal line = y + m_ascent + std::max<qreal>(1.0, descent / 2);
                painter.drawLine(QPointF(box.left(), line), QPointF(box.right(), line));
            }
            if (cell.attrs & Strike) {
                const qreal line = y + m_ascent * 0.65;
                painter.drawLine(QPointF(box.left(), line), QPointF(box.right(), line));
            }
        }
    }
}

QPoint RemoteScreen::cellAt(const QPoint &pos) const
{
    const int x = pos.x() + horizontalScrollBar()->value();
    const int col = std::clamp(int((x - m_margin) / m_cellW), 0, std::max(0, m_model.cols()));
    const int row = std::clamp(firstVisible() + int((pos.y() - m_margin) / m_cellH), 0,
                               std::max(0, m_model.columnRows() - 1));
    return {col, row};
}

void RemoteScreen::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::LeftButton) {
        m_selStart = m_selEnd = cellAt(event->pos());
        m_selecting = true;
        viewport()->update();
    } else if (event->button() == Qt::MiddleButton && m_driving) {
        const QString text = QGuiApplication::clipboard()->text(QClipboard::Selection);
        if (!text.isEmpty()) emit paste(text);
    }
}

void RemoteScreen::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_selecting) return;
    m_selEnd = cellAt(event->pos());
    viewport()->update();
}

void RemoteScreen::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_selecting || event->button() != Qt::LeftButton) return;
    m_selecting = false;
    m_selEnd = cellAt(event->pos());
    if (m_selEnd == m_selStart) {
        m_selStart = m_selEnd = {-1, -1};
    } else {
        const QString text = selectedText();
        QClipboard *clipboard = QGuiApplication::clipboard();
        if (clipboard->supportsSelection()) clipboard->setText(text, QClipboard::Selection);
        // Copy on highlight, the terminal's own setting (Options › Terminal).
        if (QSettings().value(QStringLiteral("terminal/copy_on_select"), false).toBool())
            clipboard->setText(text, QClipboard::Clipboard);
    }
    viewport()->update();
}

QString RemoteScreen::selectedText() const
{
    if (m_selStart.x() < 0 || m_selStart == m_selEnd) return QString();
    auto less = [](const QPoint &a, const QPoint &b) { return a.y() < b.y() || (a.y() == b.y() && a.x() < b.x()); };
    const QPoint a = less(m_selStart, m_selEnd) ? m_selStart : m_selEnd;
    const QPoint b = less(m_selStart, m_selEnd) ? m_selEnd : m_selStart;
    QStringList lines;
    for (int row = a.y(); row <= b.y(); ++row) {
        const QVector<Cell> cells = m_model.cellsAt(row);
        const int from = row == a.y() ? a.x() : 0;
        const int to = row == b.y() ? b.x() : int(cells.size());
        QString line;
        for (int col = from; col < to && col < cells.size(); ++col)
            if (!cells.at(col).tail) line += cells.at(col).text;
        while (line.endsWith(QLatin1Char(' '))) line.chop(1);
        lines.append(line);
    }
    return lines.join(QLatin1Char('\n'));
}

void RemoteScreen::keyPressEvent(QKeyEvent *event)
{
    const Qt::KeyboardModifiers mods = event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    if (mods == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_C) {
        const QString text = selectedText();
        if (!text.isEmpty()) QGuiApplication::clipboard()->setText(text);
        event->accept();
        return;
    }
    if (mods == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_V) {
        if (m_driving) {
            const QString text = QGuiApplication::clipboard()->text();
            if (!text.isEmpty()) { toLive(); emit paste(text); }
        } else {
            emit typedWhileWatching();
        }
        event->accept();
        return;
    }
    if (mods == Qt::ShiftModifier && (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown)) {
        verticalScrollBar()->triggerAction(event->key() == Qt::Key_PageUp ? QAbstractSlider::SliderPageStepSub
                                                                          : QAbstractSlider::SliderPageStepAdd);
        event->accept();
        return;
    }
    const QByteArray bytes = keyBytes(event->key(), event->modifiers(), event->text());
    if (bytes.isEmpty()) { QAbstractScrollArea::keyPressEvent(event); return; }
    event->accept();
    if (!m_driving) { emit typedWhileWatching(); return; }
    toLive();
    emit keys(bytes);
}

// ----- RemotePane ---------------------------------------------------------------------------------

RemotePane::RemotePane(const QString &paneId, const QString &title, const QString &desktop, Sink sink,
                       QWidget *parent)
    : QWidget(parent), m_pane(paneId), m_title(title), m_desktop(desktop), m_sink(std::move(sink))
{
    setObjectName(QStringLiteral("remotePane"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Who has the keyboard, above the terminal it is about.
    m_driveBar = new QFrame;
    m_driveBar->setObjectName(QStringLiteral("remoteDriveBar"));
    auto *drive = new QHBoxLayout(m_driveBar);
    drive->setContentsMargins(8, 3, 8, 3);
    drive->setSpacing(8);
    m_driveLabel = plainLabel(QStringLiteral("remoteDriveLabel"));
    m_presenceLabel = plainLabel(QStringLiteral("remotePresence"));
    m_live = new QPushButton(QStringLiteral("↓ New output"));
    m_live->setObjectName(QStringLiteral("remoteLive"));
    m_live->hide();
    m_take = new QPushButton(QStringLiteral("Take the keyboard"));
    m_take->setObjectName(QStringLiteral("remoteTake"));
    m_take->setToolTip(QStringLiteral("Type into the desktop's terminal. The desktop takes it back with any key."));
    m_release = new QPushButton(QStringLiteral("Give it back"));
    m_release->setObjectName(QStringLiteral("remoteRelease"));
    drive->addWidget(m_driveLabel);
    drive->addWidget(m_presenceLabel);
    drive->addStretch(1);
    drive->addWidget(m_live);
    drive->addWidget(m_take);
    drive->addWidget(m_release);
    layout->addWidget(m_driveBar);

    m_screen = new RemoteScreen;
    layout->addWidget(m_screen, 1);
    m_note = plainLabel(QStringLiteral("remoteNote"));
    m_note->setWordWrap(true);
    m_note->hide();
    m_note->setContentsMargins(10, 4, 10, 4);
    layout->addWidget(m_note);

    // The reasoning bubble.
    m_thinking = new QFrame;
    m_thinking->setObjectName(QStringLiteral("remoteThinking"));
    auto *thinking = new QVBoxLayout(m_thinking);
    thinking->setContentsMargins(10, 4, 6, 4);
    thinking->setSpacing(2);
    auto *thinkingHead = new QHBoxLayout;
    m_thinkingHeader = plainLabel(QStringLiteral("remoteThinkingHeader"));
    m_thinkingToggle = new QToolButton;
    m_thinkingToggle->setText(QStringLiteral("▴"));
    m_thinkingToggle->setToolTip(QStringLiteral("Show more reasoning"));
    m_thinkingToggle->setAutoRaise(true);
    m_thinkingClose = new QToolButton;
    m_thinkingClose->setText(QStringLiteral("×"));
    m_thinkingClose->setToolTip(QStringLiteral("Hide reasoning"));
    m_thinkingClose->setAutoRaise(true);
    thinkingHead->addWidget(m_thinkingHeader, 1);
    thinkingHead->addWidget(m_thinkingToggle);
    thinkingHead->addWidget(m_thinkingClose);
    thinking->addLayout(thinkingHead);
    m_thinkingTail = new QPlainTextEdit;
    m_thinkingTail->setObjectName(QStringLiteral("remoteThinkingTail"));
    m_thinkingTail->setReadOnly(true);
    m_thinkingTail->setFrameShape(QFrame::NoFrame);
    m_thinkingTail->setFocusPolicy(Qt::ClickFocus);
    relay::installCopyOnSelect(m_thinkingTail);
    thinking->addWidget(m_thinkingTail);
    m_thinking->hide();
    layout->addWidget(m_thinking);

    // The queue strip.
    m_queue = new QFrame;
    m_queue->setObjectName(QStringLiteral("remoteQueue"));
    auto *queue = new QVBoxLayout(m_queue);
    queue->setContentsMargins(10, 4, 6, 4);
    queue->setSpacing(2);
    auto *queueHead = new QHBoxLayout;
    m_queueTitle = plainLabel(QStringLiteral("remoteQueueTitle"));
    m_queueHint = plainLabel(QStringLiteral("remoteQueueHint"));
    queueHead->addWidget(m_queueTitle);
    queueHead->addWidget(m_queueHint, 1);
    queue->addLayout(queueHead);
    m_queueReason = plainLabel(QStringLiteral("remoteQueueReason"));
    m_queueReason->setWordWrap(true);
    queue->addWidget(m_queueReason);
    m_running = plainLabel(QStringLiteral("remoteRunning"));
    queue->addWidget(m_running);
    m_rows = new QListWidget;
    m_rows->setObjectName(QStringLiteral("remoteRows"));
    m_rows->setFrameShape(QFrame::NoFrame);
    m_rows->setContextMenuPolicy(Qt::CustomContextMenu);
    m_rows->setSelectionMode(QAbstractItemView::SingleSelection);
    m_rows->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_rows->installEventFilter(this);
    queue->addWidget(m_rows);
    m_queue->hide();
    layout->addWidget(m_queue);

    // The prompt box and its strip.
    m_composer = new QFrame;
    m_composer->setObjectName(QStringLiteral("remoteComposer"));
    auto *composer = new QVBoxLayout(m_composer);
    composer->setContentsMargins(8, 6, 8, 6);
    composer->setSpacing(4);
    auto *line = new QHBoxLayout;
    m_box = new QPlainTextEdit;
    m_box->setObjectName(QStringLiteral("remotePrompt"));
    m_box->setTabChangesFocus(true);
    m_box->setFrameShape(QFrame::NoFrame);
    m_box->installEventFilter(this);
    m_mode = plainLabel(QStringLiteral("remoteMode"));
    line->addWidget(m_box, 1);
    line->addWidget(m_mode, 0, Qt::AlignTop);
    composer->addLayout(line);
    auto *strip = new QHBoxLayout;
    strip->setSpacing(10);
    m_folder = plainLabel(QStringLiteral("remoteFolder"));
    m_sessions = new QToolButton;
    m_sessions->setObjectName(QStringLiteral("remoteSessions"));
    m_sessions->setPopupMode(QToolButton::InstantPopup);
    m_sessions->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_sessions->setToolTip(QStringLiteral("Conversations"));
    m_sessionsMenu = new QMenu(m_sessions);
    m_sessions->setMenu(m_sessionsMenu);
    m_clock = plainLabel(QStringLiteral("remoteClock"));
    m_context = plainLabel(QStringLiteral("remoteContext"));
    m_allowance = plainLabel(QStringLiteral("remoteAllowance"));
    m_model = new QToolButton;
    m_model->setObjectName(QStringLiteral("remoteModel"));
    m_model->setPopupMode(QToolButton::InstantPopup);
    m_model->setToolTip(QStringLiteral("Model"));
    m_modelMenu = new QMenu(m_model);
    m_send = new QPushButton(QStringLiteral("Send"));
    m_send->setObjectName(QStringLiteral("remoteSend"));
    m_sendMenu = new QToolButton;
    m_sendMenu->setText(QStringLiteral("▾"));
    m_sendMenu->setToolTip(QStringLiteral("When to send"));
    m_sendMenu->setPopupMode(QToolButton::InstantPopup);
    auto *whenMenu = new QMenu(m_sendMenu);
    for (const auto &[when, words] : {std::pair<const char *, const char *>{"queue", "After this turn"},
                                      {"steer", "At the next tool call"}, {"now", "Now"}}) {
        const QString w = QString::fromLatin1(when);
        whenMenu->addAction(QString::fromUtf8(words), this, [this, w] {
            sendPrompt(w);
            if (w == QLatin1String("now")) hint(QStringLiteral("remote.compose.now.mouse"), QStringLiteral("Next time: Ctrl+Enter"));
            else if (w == QLatin1String("steer"))
                hint(QStringLiteral("remote.compose.steer.mouse"), QStringLiteral("Next time: Enter, then Enter again on the empty prompt box"));
        });
    }
    m_sendMenu->setMenu(whenMenu);
    strip->addWidget(m_folder);
    strip->addWidget(m_sessions);
    strip->addStretch(1);
    strip->addWidget(m_clock);
    strip->addWidget(m_context);
    strip->addWidget(m_allowance);
    strip->addWidget(m_model);
    strip->addWidget(m_send);
    strip->addWidget(m_sendMenu);
    composer->addLayout(strip);
    layout->addWidget(m_composer);

    // ---- wiring ----
    connect(m_screen, &RemoteScreen::keys, this, [this](const QByteArray &bytes) {
        send({{"t", "keys"}, {"bytes", QString::fromLatin1(bytes.toBase64())}});
    });
    connect(m_screen, &RemoteScreen::paste, this, [this](const QString &text) {
        send({{"t", "paste"}, {"text", text}});
    });
    connect(m_screen, &RemoteScreen::typedWhileWatching, this, [this] {
        const QString owner = m_desktop.isEmpty() ? QStringLiteral("the owner") : m_desktop;
        if (m_ended)
            return;
        if (guest())
            showNote(m_role != QLatin1String("editor") ? QStringLiteral("Watching %1's pane: a viewer cannot type into it.").arg(owner)
                     : m_asking ? QStringLiteral("Waiting for %1 to let you type…").arg(owner)
                     : QStringLiteral("“Ask to type” above the terminal first; %1 decides.").arg(owner));
        else if (m_capability != QLatin1String("full"))
            showNote(QStringLiteral("This device is paired for %1 only: it cannot type into the terminal.")
                         .arg(m_capability == QLatin1String("agent") ? QStringLiteral("the agent") : QStringLiteral("viewing")));
        else
            showNote(QStringLiteral("Watching. “Take the keyboard” above the terminal to type into it."));
    });
    connect(m_screen, &RemoteScreen::historyWanted, this, [this](qint64 before, int count) {
        m_historyRequest = QStringLiteral("h%1").arg(++m_historySeq);
        QJsonObject message{{"t", "history_get"}, {"count", count}, {"id", m_historyRequest}};
        if (before >= 0) message.insert(QStringLiteral("before_row"), double(before));
        send(message);
    });
    connect(m_screen, &RemoteScreen::behindChanged, m_live, &QWidget::setVisible);
    connect(m_live, &QPushButton::clicked, m_screen, [this] { m_screen->toLive(); m_screen->setFocus(); });
    connect(m_take, &QPushButton::clicked, this, &RemotePane::requestControl);
    connect(m_release, &QPushButton::clicked, this, &RemotePane::releaseControl);
    connect(m_thinkingToggle, &QToolButton::clicked, this, [this] {
        m_thinkingExpanded = !m_thinkingExpanded;
        renderThinking();
    });
    connect(m_thinkingClose, &QToolButton::clicked, this, [this] {
        m_thinkingHidden = true;
        m_thinking->hide();
        m_box->setFocus();
    });
    connect(m_thinkingTail, &QPlainTextEdit::selectionChanged, this, [this] {
        if (m_hasPendingTail && !m_thinkingTail->textCursor().hasSelection()) setTail(m_pendingTail);
    });
    connect(m_rows, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        if (QListWidgetItem *item = m_rows->itemAt(pos))
            rowMenu(item->data(Qt::UserRole).toString(), m_rows->viewport()->mapToGlobal(pos));
    });
    connect(m_box, &QPlainTextEdit::textChanged, this, [this] {
        const QFontMetrics metrics(m_box->font());
        const int lines = std::clamp(int(m_box->document()->size().height()), 1, 6);
        m_box->setFixedHeight(lines * metrics.lineSpacing() + 10);
        m_send->setEnabled(!m_box->toPlainText().trimmed().isEmpty());
    });
    connect(m_send, &QPushButton::clicked, this, [this] { sendPrompt(); m_box->setFocus(); });
    connect(m_modelMenu, &QMenu::aboutToShow, this, [this] {
        m_modelMenu->clear();
        const QJsonArray choices = m_state.value(QStringLiteral("model")).toObject().value(QStringLiteral("choices")).toArray();
        for (int i = 0; i < choices.size(); ++i) {
            const QJsonObject choice = choices.at(i).toObject();
            QAction *action = m_modelMenu->addAction(menuText(str(choice.value(QStringLiteral("label")))), this,
                                                     [this, i] { pickModel(i); });
            action->setCheckable(true);
            action->setChecked(choice.value(QStringLiteral("current")).toBool());
        }
    });
    connect(m_sessionsMenu, &QMenu::aboutToShow, this, [this] {
        m_sessionsMenu->clear();
        const QStringList labels = conversationMenuLabels();
        if (labels.isEmpty()) m_sessionsMenu->addAction(QStringLiteral("No other conversations"))->setEnabled(false);
        const QJsonObject sessions = m_state.value(QStringLiteral("sessions")).toObject();
        const bool canNew = sessions.value(QStringLiteral("can_new")).toBool();
        const bool canOpen = sessions.value(QStringLiteral("can_open")).toBool();
        const QJsonArray rows = sessions.value(QStringLiteral("rows")).toArray();
        for (int i = 0; i < labels.size(); ++i) {
            QAction *action = m_sessionsMenu->addAction(menuText(labels.at(i)), this, [this, i] { triggerConversation(i); });
            const int rowIndex = canNew ? i - 1 : i;
            if (rowIndex >= 0) {
                const QJsonObject row = rows.at(rowIndex).toObject();
                const bool current = row.value(QStringLiteral("current")).toBool();
                action->setCheckable(current);
                action->setChecked(current);
                action->setEnabled(canOpen && !current);
            }
            if (canNew && i == 0 && labels.size() > 1) m_sessionsMenu->addSeparator();
        }
    });

    auto restyle = [this] {
        const QString ground = theme::Surface.name(), raised = theme::SurfaceRaised.name(),
                      border = theme::Border.name(), muted = theme::TextMuted.name(), text = theme::Text.name(),
                      agent = theme::Agent.name();
        setStyleSheet(QStringLiteral(
            "#remoteDriveBar { background: %1; border-bottom: 1px solid %3; }"
            "#remoteDriveLabel, #remotePresence, #remoteQueueHint, #remoteFolder, #remoteClock, #remoteContext, #remoteMode, #remoteRunning { color: %4; }"
            "#remoteNote { color: %5; background: %2; }"
            "#remoteThinking { background: %2; border-top: 1px solid %3; }"
            "#remoteThinkingHeader { color: %6; }"
            "#remoteThinkingTail { background: transparent; color: %4; }"
            "#remoteQueue { background: %1; border-top: 1px solid %3; }"
            "#remoteQueueTitle { color: %4; font-weight: 600; }"
            "#remoteRows { background: transparent; color: %5; }"
            "#remoteComposer { background: %1; border-top: 1px solid %3; }"
            "#remotePrompt { background: %2; color: %5; border-radius: 4px; padding: 2px; }")
            .arg(ground, raised, border, muted, text, agent));
    };
    restyle();
    connect(theme::notifier(), &theme::Notifier::themeChanged, this, restyle);

    m_send->setEnabled(false);
    m_sendMenu->hide();
    m_composer->setVisible(true);
    m_box->setPlaceholderText(QStringLiteral("Ask or run…"));
    emit m_box->textChanged();
    updateDriveUi();
    setFocusProxy(m_box);
    // Nothing is asked for here: whoever opens the pane focuses it (the viewer's `open` is
    // pane_focus then pane_state_get, and again after every reconnect), and the snapshot follows.
}

RemotePane::~RemotePane()
{
    if (m_driving) send({{"t", "control_release"}});
    if (onClosed) onClosed();
}

RemotePane *RemotePane::openFromViewer(const QString &paneId, const QString &title, const QString &desktop)
{
    return openFromViewer(paneId, title, desktop, RemoteViewer::instance());
}

RemotePane *RemotePane::openFromViewer(const QString &paneId, const QString &title, const QString &desktop,
                                       RemoteViewer &viewer)
{
    // Both viewers live as long as the process, so the lambdas below may hold on to this one.
    RemoteViewer *source = &viewer;
    viewer.send({{"t", "open"}, {"pane", paneId}});
    viewer.paneOpened(paneId);
    auto *pane = new RemotePane(paneId, title, desktop, [source](const QJsonObject &message) {
        source->sendToDesktop(message);
    });
    pane->setCapability(viewer.isGuest() ? QStringLiteral("guest") : viewer.capability(), viewer.features());
    if (viewer.isGuest()) {
        pane->setRole(viewer.role());
        pane->setParticipant(viewer.participant());
    }
    pane->setMyDevice(viewer.device());
    connect(&viewer, &RemoteViewer::message, pane, &RemotePane::handle);
    connect(&viewer, &RemoteViewer::welcome, pane, [pane, source](const QString &capability, const QStringList &features) {
        pane->setMyDevice(source->device());
        pane->setCapability(source->isGuest() ? QStringLiteral("guest") : capability, features);
        if (source->isGuest()) {
            pane->setParticipant(source->participant());
            pane->setRole(source->role());
        }
    });
    connect(&viewer, &RemoteViewer::status, pane, &RemotePane::setConnection);
    // The viewer's own sentences ("Not connected to the desktop; that was not sent.") belong
    // where the thing that was not sent was typed.
    connect(&viewer, &RemoteViewer::failed, pane, [pane](const QString &message) { pane->showNote(message); });
    if (viewer.isGuest()) connect(&viewer, &RemoteViewer::ended, pane, &RemotePane::markEnded);
    pane->onClosed = [source, paneId] { source->paneClosed(paneId); };
    return pane;
}

void RemotePane::send(QJsonObject message)
{
    if (m_ended) return;   // an ended pane is a record of what was on it, and says nothing
    message.insert(QStringLiteral("pane"), m_pane);
    if (m_sink) m_sink(message);
}

void RemotePane::setCapability(const QString &capability, const QStringList &features)
{
    if (!capability.isEmpty()) m_capability = capability;
    m_features = features;
    m_screen->setHistoryEnabled(!m_ended && (features.isEmpty() || features.contains(QStringLiteral("history"))));
    applyGuestUi();
    updateDriveUi();
}

void RemotePane::setRole(const QString &role)
{
    if (role != QLatin1String("viewer") && role != QLatin1String("editor")) return;
    m_role = role;
    if (m_role != QLatin1String("editor")) {
        m_asking = false;
        // A demoted holder has already lost the keyboard on the desktop (section 10.3).
        m_driving = false;
    }
    applyGuestUi();
    updateDriveUi();
}

// What a guest sees of the pane: the screen, its scrollback, who has the keyboard and — for an
// editor — asking to type and a prompt box. None of the pane_state strip: a guest is never sent
// one, so the owner's model, conversations and queue are not theirs to see or to change.
void RemotePane::applyGuestUi()
{
    if (!guest()) return;
    const bool editor = m_role == QLatin1String("editor");
    m_composer->setVisible(editor && !m_ended);
    for (QWidget *widget : std::initializer_list<QWidget *>{m_mode, m_folder, m_sessions, m_clock, m_context, m_allowance, m_model, m_sendMenu, m_queue, m_thinking})
        widget->hide();
    m_box->setVisible(true);
    m_send->setVisible(true);
    const QString owner = m_desktop.isEmpty() ? QStringLiteral("the owner") : m_desktop;
    m_box->setPlaceholderText(m_paused ? QStringLiteral("Paused by %1…").arg(owner) : QStringLiteral("Ask %1's agent…").arg(owner));
    m_box->setEnabled(!m_paused);
    m_send->setEnabled(!m_paused && !m_box->toPlainText().trimmed().isEmpty());
}

void RemotePane::markEnded(const QString &message)
{
    if (m_ended) return;
    m_ended = true;
    m_endedMessage = message.isEmpty() ? QStringLiteral("Your access to this pane ended.") : message;
    m_driving = false;
    m_asking = false;
    m_claiming = false;
    m_screen->setDriving(false);
    m_screen->setHistoryEnabled(false);
    m_composer->hide();
    m_queue->hide();
    m_thinking->hide();
    updateDriveUi();
    showNote(m_endedMessage, 0);
}

void RemotePane::setConnection(const QString &state, const QString &message)
{
    if (m_ended) return;   // the reason it ended stays on screen; nothing will reconnect it
    const bool was = m_connected;
    m_connected = state == QLatin1String("connected");
    if (m_connected) {
        if (!was && m_everConnected) {
            // Back after a drop. The viewer resumes the streams and focuses the pane again, which
            // brings a fresh snapshot and state; there is nothing to ask for here.
            showNote(QStringLiteral("Connected to %1 again.").arg(m_desktop.isEmpty() ? QStringLiteral("the desktop") : m_desktop), 3000);
        }
        m_everConnected = true;
        return;
    }
    m_everConnected = m_everConnected || was;
    if (state == QLatin1String("reconnecting") || state == QLatin1String("connecting"))
        showNote(QStringLiteral("Reconnecting to %1…").arg(m_desktop.isEmpty() ? QStringLiteral("the desktop") : m_desktop), 0);
    else if (state == QLatin1String("offline"))
        showNote(message.isEmpty() ? QStringLiteral("Offline: the desktop cannot be reached.") : QStringLiteral("Offline: ") + message, 0);
    if (m_driving) {
        m_driving = false;
        updateDriveUi();
    }
}

void RemotePane::handle(const QJsonObject &message)
{
    if (m_ended) return;
    const QString kind = str(message.value(QStringLiteral("t")));
    const QJsonValue paneValue = message.value(QStringLiteral("pane"));
    const bool mine = paneValue.isString() && paneValue.toString() == m_pane;
    if (guest()) onGuestMessage(kind, message, mine);
    if (kind == QLatin1String("screen_snapshot") || kind == QLatin1String("screen_diff")) {
        if (mine) m_screen->apply(message);
    } else if (kind == QLatin1String("history")) {
        if (!mine) return;
        const QString id = str(message.value(QStringLiteral("id")));
        if (!id.isEmpty() && id != m_historyRequest) return;   // a page we stopped waiting for
        m_screen->applyHistory(message);
    } else if (kind == QLatin1String("pane_state")) {
        // Never sent to a guest (GUEST_SERVER_TYPES); one that arrived would draw the owner's strip.
        if (mine && !guest()) updateState(message);
    } else if (kind == QLatin1String("queue_edit_text")) {
        if (!mine || guest()) return;
        m_box->setPlainText(str(message.value(QStringLiteral("text"))) + m_typedAhead);
        m_typedAhead.clear();
        m_box->setFocus();
        m_box->moveCursor(QTextCursor::End);
    } else if (kind == QLatin1String("control")) {
        if (mine) onControl(message);
    } else if (kind == QLatin1String("participants")) {
        if (!mine) return;
        m_presence = message.value(QStringLiteral("items")).toArray();
        updateDriveUi();
    } else if (kind == QLatin1String("panes")) {
        for (const QJsonValue &value : message.value(QStringLiteral("items")).toArray()) {
            const QJsonObject item = value.toObject();
            if (str(item.value(QStringLiteral("id"))) != m_pane) continue;
            const QString title = str(item.value(QStringLiteral("title")));
            if (!title.isEmpty() && title != m_title) {
                m_title = title;
                if (onTitleChanged) onTitleChanged();
            }
        }
    } else if (kind == QLatin1String("paired")) {
        setMyDevice(str(message.value(QStringLiteral("device_id"))));
    } else if (kind == QLatin1String("welcome")) {
        const QJsonObject desktop = message.value(QStringLiteral("desktop")).toObject();
        if (!str(desktop.value(QStringLiteral("name"))).isEmpty()) m_desktop = str(desktop.value(QStringLiteral("name")));
        QStringList features;
        for (const QJsonValue &value : message.value(QStringLiteral("features")).toArray()) features.append(value.toString());
        setCapability(str(message.value(QStringLiteral("capability"))), features);
    } else if (kind == QLatin1String("agent")) {
        if (!mine) return;
        // The desktop's word for what just happened to this device ("You have the keyboard.").
        const QJsonObject event = message.value(QStringLiteral("event")).toObject();
        if (str(event.value(QStringLiteral("event"))) == QLatin1String("status")) {
            const QString text = str(event.value(QStringLiteral("text")));
            if (!text.isEmpty()) showNote(text, 3000);
        }
    } else if (kind == QLatin1String("error")) {
        const QString id = str(message.value(QStringLiteral("id")));
        if (!id.isEmpty() && id == m_historyRequest) { m_screen->historyFailed(); return; }
        if (paneValue.isString() && !mine) return;
        const QString code = str(message.value(QStringLiteral("code")));
        if (code == QLatin1String("not_driving") && m_driving) {
            m_driving = false;
            updateDriveUi();
        }
        const QString text = str(message.value(QStringLiteral("message")));
        if (!text.isEmpty()) showNote(QStringLiteral("The desktop said: ") + text);
    }
}

// ---- a guest's messages (section 10), as app/guest.js follows them --------------------------------

void RemotePane::onGuestMessage(const QString &kind, const QJsonObject &message, bool mine)
{
    const QString owner = m_desktop.isEmpty() ? QStringLiteral("the owner") : m_desktop;
    if (kind == QLatin1String("welcome")) {
        // A participant's welcome: {participant, role, panes, expires}, and no capability.
        const QString participant = str(message.value(QStringLiteral("participant")));
        if (!participant.isEmpty()) m_participant = participant;
        setRole(str(message.value(QStringLiteral("role"))));
    } else if (kind == QLatin1String("participants")) {
        // The `you` row is this guest's own record, and where a live role change arrives. A role
        // belongs to the person, not the pane, so every pane follows whichever pane it came for.
        for (const QJsonValue &value : message.value(QStringLiteral("items")).toArray()) {
            const QJsonObject item = value.toObject();
            if (!item.value(QStringLiteral("you")).toBool()) continue;
            const QString id = str(item.value(QStringLiteral("id")));
            if (!id.isEmpty()) m_participant = id;
            const QString role = str(item.value(QStringLiteral("role")));
            if (!role.isEmpty() && role != m_role && (role == QLatin1String("viewer") || role == QLatin1String("editor"))) {
                setRole(role);
                if (mine)
                    showNote(role == QLatin1String("editor")
                                 ? QStringLiteral("%1 made you an editor: you can ask to type, and ask their agent.").arg(owner)
                                 : QStringLiteral("%1 made you a viewer: you can watch this pane.").arg(owner), 8000);
            }
        }
    } else if (kind == QLatin1String("control_pending")) {
        if (!mine) return;
        m_asking = true;
        updateDriveUi();
        // The hub drops an unanswered request after a minute without saying so; the waiting ends
        // by itself rather than sitting there for ever.
        const int serial = ++m_askSerial;
        QTimer::singleShot(62000, this, [this, serial, owner] {
            if (serial != m_askSerial || !m_asking || m_driving) return;
            m_asking = false;
            updateDriveUi();
            showNote(QStringLiteral("Nobody answered. You can ask %1 to type again.").arg(owner));
        });
    } else if (kind == QLatin1String("prompt_pending")) {
        if (mine) showNote(QStringLiteral("Waiting for %1 to approve your prompt.").arg(owner), 6000);
    } else if (kind == QLatin1String("prompt_decided")) {
        if (!mine) return;
        if (message.value(QStringLiteral("approved")).toBool()) {
            showNote(QStringLiteral("%1 approved your prompt · it went to their agent.").arg(owner));
            return;
        }
        const QString reason = str(message.value(QStringLiteral("reason")));
        showNote(reason == QLatin1String("lapsed") ? QStringLiteral("Nobody answered your prompt in ten minutes. You can send it again.")
                 : reason == QLatin1String("paused") ? QStringLiteral("%1 paused guests before your prompt could run.").arg(owner)
                 : reason == QLatin1String("removed") ? QStringLiteral("Your access to this pane ended before your prompt ran.")
                 : QStringLiteral("%1 declined your prompt.").arg(owner), 8000);
    } else if (kind == QLatin1String("share_state")) {
        // No pane (or "") is the whole share.
        const QString pane = str(message.value(QStringLiteral("pane")));
        if (!pane.isEmpty() && pane != m_pane) return;
        const bool paused = message.value(QStringLiteral("paused")).toBool();
        const bool was = m_paused;
        m_paused = paused;
        m_pauseReason = str(message.value(QStringLiteral("reason")));
        if (paused) m_asking = false;
        applyGuestUi();
        updateDriveUi();
        if (paused)
            showNote(m_pauseReason == QLatin1String("away")
                         ? QStringLiteral("%1 is away: guests can act again when they are back.").arg(owner)
                         : QStringLiteral("%1 paused guests: you can watch, not type or ask.").arg(owner), 0);
        else if (was)
            showNote(QStringLiteral("%1 let guests act again.").arg(owner), 3000);
    }
}

// ---- control (section 10.3), as app/app.js onControl() ------------------------------------------

void RemotePane::onControl(const QJsonObject &message)
{
    m_holder = str(message.value(QStringLiteral("holder")));
    if (m_holder.isEmpty()) m_holder = QStringLiteral("owner");
    m_holderName = str(message.value(QStringLiteral("name")));
    m_holderDevice = str(message.value(QStringLiteral("device")));
    if (guest()) {
        // To a guest the holder is `owner`, `agent` or `participant:<id>`, and never a device.
        // The answer to this guest's own request carries a reason; the broadcast does not.
        const QString owner = m_desktop.isEmpty() ? QStringLiteral("the owner") : m_desktop;
        const QString reason = str(message.value(QStringLiteral("reason")));
        // The guest viewer says `driving` itself; the holder names this participant otherwise.
        const QJsonValue driving = message.value(QStringLiteral("driving"));
        const bool held = driving.isBool() ? driving.toBool()
                                           : !m_participant.isEmpty() && m_holder == QStringLiteral("participant:") + m_participant;
        const bool was = m_driving, asked = m_asking;
        m_driving = held && m_role == QLatin1String("editor");
        m_asking = false;
        ++m_askSerial;
        updateDriveUi();
        if (m_driving && !was) {
            showNote(QStringLiteral("What you type now goes to the program on %1's screen.").arg(owner));
            m_screen->toLive();
            m_screen->setFocus(Qt::OtherFocusReason);
        } else if (asked && !m_driving && !reason.isEmpty()) {
            showNote(reason == QLatin1String("lapsed") ? QStringLiteral("Nobody answered. You can ask to type again.")
                                                       : QStringLiteral("%1 said no for now. You can ask again.").arg(owner));
        } else if (was && !m_driving) {
            showNote(m_holder == QLatin1String("owner") ? QStringLiteral("%1 took the keyboard back.").arg(m_holderName.isEmpty() ? owner : m_holderName)
                     : m_holder == QLatin1String("agent") ? QStringLiteral("Their agent is driving this pane now.")
                     : QStringLiteral("%1 is driving this pane now.").arg(m_holderName.isEmpty() ? QStringLiteral("Somebody else") : m_holderName));
        }
        return;
    }
    // Which of the owner's devices is this one? The viewer says (`welcome.device`); a pane on a
    // sink that does not learns it from the handoff that answers its own control_request.
    if (m_claiming && m_myDevice.isEmpty() && m_holder == QLatin1String("owner") && !m_holderDevice.isEmpty())
        m_myDevice = m_holderDevice;
    m_claiming = false;
    const bool held = m_holder == QLatin1String("owner") && !m_holderDevice.isEmpty() && m_holderDevice == m_myDevice;
    const bool lost = m_driving && !held;
    m_driving = held;
    updateDriveUi();
    if (lost) {
        showNote(m_holder == QLatin1String("owner") && m_holderDevice.isEmpty()
                     ? QStringLiteral("The desktop took the keyboard back.")
                     : QStringLiteral("%1 has the keyboard now.").arg(m_holderName.isEmpty() ? QStringLiteral("Someone else") : m_holderName));
    }
}

void RemotePane::requestControl()
{
    if (guest()) {
        // A participant's control_request is a request, not a grant: the keyboard is theirs only
        // when the owner's `control` names them.
        if (m_role != QLatin1String("editor") || m_ended || m_paused || m_driving || m_asking) return;
        send({{"t", "control_request"}});
        m_asking = true;
        updateDriveUi();
        return;
    }
    if (m_capability != QLatin1String("full")) return;
    send({{"t", "control_request"}});
    // The desktop's `control` is what makes it true, and it follows at once; the bar must not
    // look dead until it arrives (app.js does the same).
    m_claiming = true;
    m_driving = true;
    updateDriveUi();
    m_screen->toLive();
    m_screen->setFocus(Qt::OtherFocusReason);
}

void RemotePane::releaseControl()
{
    if (!m_driving) return;
    send({{"t", "control_release"}});
    m_driving = false;
    updateDriveUi();
}

void RemotePane::updateDriveUi()
{
    const bool full = m_capability == QLatin1String("full");
    m_screen->setDriving(m_driving);
    m_release->setVisible(m_driving);
    const QString desktop = m_desktop.isEmpty() ? QStringLiteral("the desktop") : m_desktop;
    QString words;
    if (guest()) {
        // One line that says whose pane this is and what this guest may do on it.
        const bool editor = m_role == QLatin1String("editor");
        const QString owner = m_desktop.isEmpty() ? QStringLiteral("the owner") : m_desktop;
        m_take->setText(QStringLiteral("Ask to type"));
        m_take->setToolTip(QStringLiteral("Ask %1 for the keyboard. They can take it back at any time.").arg(owner));
        m_take->setVisible(editor && !m_ended && !m_driving && !m_asking && !m_paused && m_connected);
        if (m_ended) words = m_endedMessage;
        else if (m_driving) words = QStringLiteral("You have the keyboard · keys go to %1's pane").arg(owner);
        else if (m_paused) words = QStringLiteral("Paused by %1 · watching %1's pane").arg(owner);
        else if (m_asking) words = QStringLiteral("Asked %1 to let you type…").arg(owner);
        else if (m_holder == QLatin1String("agent")) words = QStringLiteral("Watching %1's pane · their agent has the keyboard").arg(owner);
        else if (m_holder.startsWith(QLatin1String("participant:")))
            words = QStringLiteral("Watching %1's pane · %2 has the keyboard").arg(owner, m_holderName.isEmpty() ? QStringLiteral("another guest") : m_holderName);
        else if (editor) words = QStringLiteral("%1's pane · You can ask to type; your prompts wait for %1").arg(owner);
        else words = QStringLiteral("Watching %1's pane").arg(owner);
    } else {
        m_take->setVisible(full && !m_driving && m_connected);
        if (m_driving) {
            words = QStringLiteral("You have the keyboard · keys go to %1").arg(desktop);
        } else if (m_holder == QLatin1String("agent")) {
            words = QStringLiteral("The agent has the keyboard");
        } else if (m_holder.startsWith(QLatin1String("participant:"))) {
            words = QStringLiteral("%1 has the keyboard").arg(m_holderName.isEmpty() ? QStringLiteral("A guest") : m_holderName);
        } else if (!m_holderDevice.isEmpty() && m_holderDevice != m_myDevice) {
            words = QStringLiteral("Another of your devices has the keyboard");
        } else {
            words = full ? QStringLiteral("Watching") : m_capability == QLatin1String("agent")
                    ? QStringLiteral("Watching · you may talk to the agent") : QStringLiteral("Watching · view only");
        }
    }
    m_driveLabel->setText(words);
    QStringList presence;
    for (const QJsonValue &value : m_presence) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("you")).toBool()) continue;   // a guest's own row: the line above says it
        const QString name = str(item.value(QStringLiteral("name"))).isEmpty() ? QStringLiteral("someone") : str(item.value(QStringLiteral("name")));
        if (item.value(QStringLiteral("driving")).toBool()) presence << QStringLiteral("%1 is typing").arg(name);
        else if (item.value(QStringLiteral("online")) == QJsonValue(false)) presence << QStringLiteral("%1 is away").arg(name);
        else presence << QStringLiteral("%1 is watching").arg(name);
    }
    setPlain(m_presenceLabel, presence.join(QStringLiteral(" · ")));
}

QString RemotePane::driveText() const { return m_driveLabel->text(); }

// ---- pane_state ----------------------------------------------------------------------------------

void RemotePane::updateState(const QJsonObject &state)
{
    const QJsonValue seqValue = state.value(QStringLiteral("seq"));
    if (seqValue.isDouble()) {
        const qint64 seq = qint64(seqValue.toDouble());
        if (seq < m_lastSeq) return;   // an older state, overtaken
        m_lastSeq = seq;
    }
    m_state = state;
    // Which row is the prompt this view just queued (or just made a steer): the first new one.
    if (m_staged.stage > 0) {
        for (const QJsonValue &value : rowList()) {
            const QJsonObject row = value.toObject();
            const QString id = str(row.value(QStringLiteral("id")));
            if (m_staged.known.contains(id)) continue;
            const QString rowKind = str(row.value(QStringLiteral("kind")));
            if (m_staged.stage == 1 && m_staged.rowId.isEmpty() && (rowKind == QLatin1String("agent") || rowKind == QLatin1String("command")))
                m_staged.rowId = id;
            else if (m_staged.stage == 2 && m_staged.steerId.isEmpty() && rowKind == QLatin1String("steer"))
                m_staged.steerId = id;
        }
    }
    renderThinking();
    renderQueue();
    renderStrip();
}

QJsonArray RemotePane::rowList() const
{
    return m_state.value(QStringLiteral("queue")).toObject().value(QStringLiteral("rows")).toArray();
}

QJsonObject RemotePane::rowById(const QString &id) const
{
    for (const QJsonValue &value : rowList())
        if (str(value.toObject().value(QStringLiteral("id"))) == id) return value.toObject();
    return {};
}

bool RemotePane::busy() const
{
    return m_state.value(QStringLiteral("turn")).toObject().value(QStringLiteral("busy")).toBool();
}

void RemotePane::setTail(const QString &text)
{
    // Never replace the text under a selection: the latest tail waits until it is gone.
    if (m_thinkingTail->textCursor().hasSelection()) {
        m_pendingTail = text;
        m_hasPendingTail = true;
        return;
    }
    m_hasPendingTail = false;
    if (m_thinkingTail->toPlainText() == text) return;
    QScrollBar *bar = m_thinkingTail->verticalScrollBar();
    const bool follow = bar->value() >= bar->maximum() - 4;
    m_thinkingTail->setPlainText(text);
    if (follow) bar->setValue(bar->maximum());
}

void RemotePane::renderThinking()
{
    const QJsonObject t = m_state.value(QStringLiteral("thinking")).toObject();
    const bool visible = t.value(QStringLiteral("visible")).toBool();
    if (!visible) m_thinkingHidden = false;
    m_thinking->setVisible(visible && !m_thinkingHidden);
    if (t.isEmpty()) return;
    m_thinkingHeader->setText(str(t.value(QStringLiteral("header"))));
    setTail(str(t.value(QStringLiteral("tail"))));
    const int lines = m_thinkingExpanded ? 14 : 4;
    m_thinkingTail->setFixedHeight(lines * QFontMetrics(m_thinkingTail->font()).lineSpacing() + 8);
    m_thinkingToggle->setText(m_thinkingExpanded ? QStringLiteral("▾") : QStringLiteral("▴"));
    m_thinkingToggle->setToolTip(m_thinkingExpanded ? QStringLiteral("Show less reasoning") : QStringLiteral("Show more reasoning"));
}

void RemotePane::renderQueue()
{
    const QJsonObject q = m_state.value(QStringLiteral("queue")).toObject();
    const QJsonArray list = rowList();
    const QString running = str(q.value(QStringLiteral("running")).toObject().value(QStringLiteral("label")));
    const bool paused = q.value(QStringLiteral("paused")).toBool();
    m_queue->setVisible(!q.isEmpty() && (!list.isEmpty() || paused || !running.isEmpty()));
    m_queueTitle->setText(paused ? QStringLiteral("QUEUE · PAUSED") : QStringLiteral("QUEUE"));
    setPlain(m_queueHint, str(q.value(QStringLiteral("hint"))));
    setPlain(m_queueReason, paused ? str(q.value(QStringLiteral("pause_reason"))) : QString());
    setPlain(m_running, running.isEmpty() ? QString() : QStringLiteral("▸ running  ") + running);

    const QString selected = m_rows->currentItem() ? m_rows->currentItem()->data(Qt::UserRole).toString() : QString();
    const bool hadFocus = m_rows->hasFocus();
    m_rows->clear();
    for (const QJsonValue &value : list) {
        const QJsonObject row = value.toObject();
        const QString id = str(row.value(QStringLiteral("id")));
        const QString label = str(row.value(QStringLiteral("label")));
        const QStringList actions = offeredActions(row);
        auto *item = new QListWidgetItem;
        item->setData(Qt::UserRole, id);
        item->setToolTip(label);
        if (actions.isEmpty()) item->setFlags(Qt::ItemIsEnabled);
        auto *widget = new QWidget;
        auto *h = new QHBoxLayout(widget);
        h->setContentsMargins(4, 1, 2, 1);
        auto *text = plainLabel(QStringLiteral("remoteRowLabel"));
        text->setText(label);
        h->addWidget(text, 1);
        if (actions.contains(QStringLiteral("remove"))) {
            auto *x = new QToolButton;
            x->setObjectName(QStringLiteral("remoteRowRemove"));
            x->setText(QStringLiteral("×"));
            x->setAutoRaise(true);
            x->setToolTip(actionWords(QStringLiteral("remove"), str(row.value(QStringLiteral("kind")))));
            x->setProperty("rowId", id);
            connect(x, &QToolButton::clicked, this, [this, id] {
                const QJsonObject current = rowById(id);
                triggerRowAction(id, QStringLiteral("remove"));
                if (str(current.value(QStringLiteral("kind"))) == QLatin1String("steer"))
                    hint(QStringLiteral("remote.queue.steer.remove.mouse"), QStringLiteral("Next time: ↑ then Shift+Delete"));
                else
                    hint(QStringLiteral("remote.queue.remove.mouse"),
                         QStringLiteral("Next time: ↑ selects a row, Shift+Delete removes, Ctrl+↑/↓ reorders"));
            });
            h->addWidget(x);
        }
        item->setSizeHint(widget->sizeHint());
        m_rows->addItem(item);
        m_rows->setItemWidget(item, widget);
        if (id == selected && !actions.isEmpty()) m_rows->setCurrentItem(item);
    }
    const int shown = std::min<int>(int(list.size()), 6);
    m_rows->setFixedHeight(shown > 0 ? shown * (m_rows->sizeHintForRow(0) + 1) + 4 : 0);
    m_rows->setVisible(!list.isEmpty());
    if (hadFocus && !m_rows->currentItem()) m_box->setFocus();
}

void RemotePane::renderStrip()
{
    const QJsonObject c = m_state.value(QStringLiteral("composer")).toObject();
    const bool canCompose = !c.isEmpty();
    m_box->setVisible(canCompose);
    m_mode->setVisible(canCompose);
    m_send->setVisible(canCompose);
    m_sendMenu->setVisible(canCompose && busy());
    if (canCompose) {
        const QString placeholder = str(c.value(QStringLiteral("placeholder")));
        if (!placeholder.isEmpty()) m_box->setPlaceholderText(placeholder);
        setPlain(m_mode, str(c.value(QStringLiteral("mode"))));
    }
    setPlain(m_folder, str(m_state.value(QStringLiteral("folder")).toObject().value(QStringLiteral("label"))));
    setPlain(m_clock, str(m_state.value(QStringLiteral("turn")).toObject().value(QStringLiteral("clock"))));
    const QJsonObject context = m_state.value(QStringLiteral("context")).toObject();
    setPlain(m_context, str(context.value(QStringLiteral("label"))));
    const QJsonValue left = context.value(QStringLiteral("percent_left"));
    const bool warn = left.isDouble() && left.toDouble() <= 15;
    m_context->setStyleSheet(warn ? QStringLiteral("color: %1;").arg(theme::Warning.name()) : QString());

    // The Relay Free allowance: the desktop's label and detail, warned the way the context chip
    // is. A state without one (the pane moved to a provider with a key) hides it.
    const QJsonObject allowance = m_state.value(QStringLiteral("allowance")).toObject();
    setPlain(m_allowance, str(allowance.value(QStringLiteral("label"))));
    m_allowance->setToolTip(str(allowance.value(QStringLiteral("detail"))));
    m_allowance->setStyleSheet(allowance.value(QStringLiteral("warn")).toBool()
                                   ? QStringLiteral("color: %1;").arg(theme::Warning.name()) : QString());

    const QJsonObject model = m_state.value(QStringLiteral("model")).toObject();
    m_model->setVisible(!model.isEmpty());
    m_model->setText(str(model.value(QStringLiteral("label"))));
    const bool pickable = !model.value(QStringLiteral("choices")).toArray().isEmpty();
    m_model->setMenu(pickable ? m_modelMenu : nullptr);
    m_model->setAutoRaise(!pickable);
    m_model->setToolButtonStyle(Qt::ToolButtonTextOnly);

    const QJsonObject sessions = m_state.value(QStringLiteral("sessions")).toObject();
    m_sessions->setVisible(m_state.contains(QStringLiteral("sessions")) && m_state.value(QStringLiteral("sessions")).isObject());
    QString current;
    for (const QJsonValue &value : sessions.value(QStringLiteral("rows")).toArray())
        if (value.toObject().value(QStringLiteral("current")).toBool()) current = str(value.toObject().value(QStringLiteral("title")));
    m_sessions->setText(QStringLiteral("☰ ") + (current.isEmpty() ? QStringLiteral("Conversations") : current));
}

// ---- rows and menus ------------------------------------------------------------------------------

bool RemotePane::rowHasRemoveButton(const QString &rowId) const
{
    for (int i = 0; i < m_rows->count(); ++i) {
        QListWidgetItem *item = m_rows->item(i);
        if (item->data(Qt::UserRole).toString() != rowId) continue;
        QWidget *widget = m_rows->itemWidget(item);
        return widget && widget->findChild<QToolButton *>(QStringLiteral("remoteRowRemove"));
    }
    return false;
}

QStringList RemotePane::rowMenuActions(const QString &rowId) const
{
    const QJsonObject row = rowById(rowId);
    return row.isEmpty() ? QStringList() : offeredActions(row);
}

void RemotePane::triggerRowAction(const QString &rowId, const QString &action)
{
    const QJsonObject row = rowById(rowId);
    if (row.isEmpty() || !offeredActions(row).contains(action)) return;   // only what the desktop offered
    if (action == QLatin1String("remove")) send({{"t", "queue_remove"}, {"row", rowId}});
    else if (action == QLatin1String("edit")) send({{"t", "queue_edit"}, {"row", rowId}});
    else if (action == QLatin1String("send_now")) send({{"t", "queue_send_now"}, {"row", rowId}});
    else send({{"t", "queue_move"}, {"row", rowId}, {"to", action}});
}

void RemotePane::rowMenu(const QString &rowId, const QPoint &globalPos)
{
    const QJsonObject row = rowById(rowId);
    const QStringList actions = offeredActions(row);
    if (actions.isEmpty()) return;
    QMenu menu(this);
    const QString kind = str(row.value(QStringLiteral("kind")));
    for (const QString &action : actions) {
        if (action == QLatin1String("remove") && actions.size() > 1) menu.addSeparator();
        menu.addAction(actionWords(action, kind), this, [this, rowId, action] {
            triggerRowAction(rowId, action);
            static const QHash<QString, QString> keys{
                {QStringLiteral("edit"), QStringLiteral("↑ then Enter")}, {QStringLiteral("steer"), QStringLiteral("↑ then Ctrl+↑")},
                {QStringLiteral("send_now"), QStringLiteral("↑ then Ctrl+Enter")}, {QStringLiteral("to_queue"), QStringLiteral("↑ then Ctrl+↓")},
                {QStringLiteral("up"), QStringLiteral("↑ then Ctrl+↑")}, {QStringLiteral("down"), QStringLiteral("↑ then Ctrl+↓")},
                {QStringLiteral("remove"), QStringLiteral("↑ then Shift+Delete")}};
            hint(QStringLiteral("remote.queue.%1.mouse").arg(action), QStringLiteral("Next time: ") + keys.value(action));
        });
    }
    menu.exec(globalPos);
}

QStringList RemotePane::modelMenuLabels() const
{
    QStringList labels;
    for (const QJsonValue &value : m_state.value(QStringLiteral("model")).toObject().value(QStringLiteral("choices")).toArray())
        labels << str(value.toObject().value(QStringLiteral("label")));
    return labels;
}

void RemotePane::pickModel(int index)
{
    const QJsonArray choices = m_state.value(QStringLiteral("model")).toObject().value(QStringLiteral("choices")).toArray();
    if (index < 0 || index >= choices.size()) return;
    const QString id = str(choices.at(index).toObject().value(QStringLiteral("id")));
    if (!id.isEmpty()) send({{"t", "model_pick"}, {"choice", id}});
}

QStringList RemotePane::conversationMenuLabels() const
{
    const QJsonValue value = m_state.value(QStringLiteral("sessions"));
    if (!value.isObject()) return {};
    const QJsonObject sessions = value.toObject();
    QStringList labels;
    if (sessions.value(QStringLiteral("can_new")).toBool()) labels << QStringLiteral("New conversation");
    for (const QJsonValue &row : sessions.value(QStringLiteral("rows")).toArray()) {
        const QString title = str(row.toObject().value(QStringLiteral("title")));
        const QString when = str(row.toObject().value(QStringLiteral("when")));
        labels << (when.isEmpty() ? title : title + QStringLiteral("   ") + when);
    }
    return labels;
}

void RemotePane::triggerConversation(int index)
{
    const QJsonObject sessions = m_state.value(QStringLiteral("sessions")).toObject();
    if (sessions.isEmpty()) return;
    const bool canNew = sessions.value(QStringLiteral("can_new")).toBool();
    if (canNew && index == 0) { send({{"t", "conversation_new"}}); return; }
    const QJsonArray rows = sessions.value(QStringLiteral("rows")).toArray();
    const int rowIndex = canNew ? index - 1 : index;
    if (rowIndex < 0 || rowIndex >= rows.size()) return;
    const QJsonObject row = rows.at(rowIndex).toObject();
    // Opening one is the owner's level, and the conversation already open is never opened again.
    if (!sessions.value(QStringLiteral("can_open")).toBool() || row.value(QStringLiteral("current")).toBool()) return;
    const QString id = str(row.value(QStringLiteral("id")));
    if (!id.isEmpty()) send({{"t", "conversation_open"}, {"session", id}});
}

// ---- the prompt box ------------------------------------------------------------------------------

void RemotePane::compose(const QString &text, const QString &when)
{
    if (text.trimmed().isEmpty()) return;
    // `agent: false` asks the desktop to route the line as its own prompt box would — a command
    // runs in the shell, anything else goes to the agent. Only a device the state offers the
    // composer's routing modes to may ask for that (app/pane.js compose()).
    const QJsonArray modes = m_state.value(QStringLiteral("composer")).toObject().value(QStringLiteral("modes")).toArray();
    const bool mayRoute = modes.contains(QStringLiteral("auto")) || modes.contains(QStringLiteral("shell"));
    QJsonObject message{{"t", "compose"}, {"text", text}, {"when", when}, {"msg_id", messageId()}};
    if (mayRoute) message.insert(QStringLiteral("agent"), false);
    send(message);
    m_staged = Staged();
    if (when == QLatin1String("queue")) {
        m_staged.text = text;
        m_staged.stage = 1;
        m_staged.at = QDateTime::currentMSecsSinceEpoch();
        for (const QJsonValue &value : rowList()) m_staged.known.insert(str(value.toObject().value(QStringLiteral("id"))));
    }
    m_box->clear();
}

void RemotePane::sendPrompt(const QString &when)
{
    const QString text = m_box->toPlainText();
    if (guest()) {
        // A guest's prompt is a request (section 10.4): it goes to the owner, who sees the whole
        // text, and only an approved one reaches their agent — never the shell, so no `agent:
        // false`. It always queues, as app/guest.js sends it: approving it never interrupts a turn.
        if (m_role != QLatin1String("editor") || m_ended || text.trimmed().isEmpty()) return;
        const QString owner = m_desktop.isEmpty() ? QStringLiteral("the owner") : m_desktop;
        if (m_paused) { showNote(QStringLiteral("%1 paused guests: this was not sent.").arg(owner)); return; }
        send({{"t", "compose"}, {"text", text}, {"when", "queue"}, {"msg_id", messageId()}});
        m_box->clear();
        if (!m_toldApproval) {
            m_toldApproval = true;
            showNote(QStringLiteral("Sent to %1. A guest's prompt waits for them to approve it before their agent sees it.").arg(owner), 8000);
        }
        return;
    }
    compose(text, when.isEmpty() ? (busy() ? QStringLiteral("queue") : QStringLiteral("now")) : when);
}

// Enter: send, or queue while the agent works. On the empty box, a second Enter makes the prompt
// just queued a steer and a third sends it now — the desktop prompt box's three-step Enter.
void RemotePane::enter()
{
    const QString text = m_box->toPlainText();
    if (!text.trimmed().isEmpty()) { sendPrompt(); return; }
    if (guest()) return;   // a guest's prompt is not theirs to steer or send now: it waits for the owner
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_staged.stage == 0 || now - m_staged.at > 15000) { m_staged = Staged(); return; }
    QSet<QString> known;
    for (const QJsonValue &value : rowList()) known.insert(str(value.toObject().value(QStringLiteral("id"))));
    if (m_staged.stage == 1) {
        if (!m_staged.rowId.isEmpty()) send({{"t", "queue_move"}, {"row", m_staged.rowId}, {"to", "steer"}});
        else send({{"t", "compose"}, {"text", m_staged.text}, {"when", "steer"}, {"msg_id", messageId()}});
        m_staged.stage = 2;
        m_staged.at = now;
        m_staged.known = known;
    } else {
        const QJsonObject kept = rowById(m_staged.rowId);
        const QString row = !m_staged.steerId.isEmpty() ? m_staged.steerId
            : (offeredActions(kept).contains(QStringLiteral("send_now")) ? m_staged.rowId : QString());
        if (!row.isEmpty()) send({{"t", "queue_send_now"}, {"row", row}});
        else send({{"t", "compose"}, {"text", m_staged.text}, {"when", "now"}, {"msg_id", messageId()}});
        m_staged = Staged();
    }
}

bool RemotePane::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::KeyPress) return QWidget::eventFilter(watched, event);
    auto *key = static_cast<QKeyEvent *>(event);
    const Qt::KeyboardModifiers mods = key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    const bool enterKey = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
    if (watched == m_box) {
        if (enterKey && mods == Qt::NoModifier) { enter(); return true; }
        if (enterKey && mods == Qt::ControlModifier) {
            // Send now, interrupting a running turn (agent.interrupt on the desktop). A guest cannot
            // interrupt anything: theirs is sent for approval like any other.
            if (guest()) sendPrompt();
            else if (!m_box->toPlainText().trimmed().isEmpty()) compose(m_box->toPlainText(), QStringLiteral("now"));
            return true;
        }
        if (key->key() == Qt::Key_Up && mods == Qt::NoModifier && m_box->toPlainText().isEmpty()) {
            for (int i = 0; i < m_rows->count(); ++i) {
                if (m_rows->item(i)->flags() & Qt::ItemIsSelectable) {
                    m_rows->setCurrentRow(i);
                    m_rows->setFocus();
                    return true;
                }
            }
        }
        return false;
    }
    if (watched == m_rows) {
        QListWidgetItem *item = m_rows->currentItem();
        const QString id = item ? item->data(Qt::UserRole).toString() : QString();
        const QStringList actions = rowMenuActions(id);
        if (key->key() == Qt::Key_Escape) { m_rows->setCurrentItem(nullptr); m_box->setFocus(); return true; }
        if (key->key() == Qt::Key_Down && mods == Qt::NoModifier && m_rows->currentRow() == m_rows->count() - 1) {
            m_rows->setCurrentItem(nullptr);
            m_box->setFocus();
            return true;
        }
        if (id.isEmpty()) return false;
        if (key->key() == Qt::Key_Up && mods == Qt::ControlModifier) {
            triggerRowAction(id, actions.contains(QStringLiteral("up")) ? QStringLiteral("up") : QStringLiteral("steer"));
            return true;
        }
        if (key->key() == Qt::Key_Down && mods == Qt::ControlModifier) {
            triggerRowAction(id, actions.contains(QStringLiteral("down")) ? QStringLiteral("down") : QStringLiteral("to_queue"));
            return true;
        }
        if (key->key() == Qt::Key_Delete && mods == Qt::ShiftModifier) { triggerRowAction(id, QStringLiteral("remove")); return true; }
        if (enterKey && mods == Qt::ControlModifier) { triggerRowAction(id, QStringLiteral("send_now")); return true; }
        if (enterKey && mods == Qt::NoModifier) {
            triggerRowAction(id, QStringLiteral("edit"));
            if (actions.contains(QStringLiteral("edit"))) m_box->setFocus();
            return true;
        }
        if (key->key() == Qt::Key_Menu) {
            rowMenu(id, m_rows->viewport()->mapToGlobal(m_rows->visualItemRect(item).bottomLeft()));
            return true;
        }
        if (!key->text().isEmpty() && key->text().at(0).isPrint() && !(mods & (Qt::ControlModifier | Qt::AltModifier))
            && actions.contains(QStringLiteral("edit"))) {
            // Typing on a row takes it back into the prompt box, as on the desktop.
            m_typedAhead += key->text();
            triggerRowAction(id, QStringLiteral("edit"));
            m_box->setFocus();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void RemotePane::showNote(const QString &text, int milliseconds)
{
    m_note->setText(text);
    m_note->setVisible(!text.isEmpty());
    const int serial = ++m_noteSerial;
    if (milliseconds > 0)
        QTimer::singleShot(milliseconds, this, [this, serial] { if (serial == m_noteSerial) m_note->hide(); });
}

QString RemotePane::noteText() const { return m_note->isHidden() ? QString() : m_note->text(); }

void RemotePane::hint(const QString &id, const QString &text)
{
    if (text.isEmpty() || !ShortcutHints::instance().shouldShow(id)) return;
    showNote(text, 5000);
}

QString RemotePane::paneTitle() const
{
    const QString title = m_title.isEmpty() ? m_pane : m_title;
    return m_desktop.isEmpty() ? title : QStringLiteral("%1 · %2").arg(title, m_desktop);
}

void RemotePane::focusView()
{
    if (m_driving || !m_box->isVisible()) m_screen->setFocus(Qt::OtherFocusReason);
    else m_box->setFocus(Qt::OtherFocusReason);
}

void RemotePane::setHeaderRightInset(int pixels)
{
    if (pixels == m_rightInset) return;
    m_rightInset = pixels;
    if (auto *drive = qobject_cast<QHBoxLayout *>(m_driveBar->layout())) drive->setContentsMargins(8, 3, 8 + pixels, 3);
}

// ----- RemoteViewer ------------------------------------------------------------------------------

RemoteViewer &RemoteViewer::instance()
{
    static RemoteViewer viewer(false);
    return viewer;
}

RemoteViewer &RemoteViewer::guest()
{
    static RemoteViewer viewer(true);
    return viewer;
}

RemoteViewer::RemoteViewer(bool guest) : m_guest(guest)
{
    if (m_guest) {
        m_capability = QStringLiteral("guest");
        m_state = QStringLiteral("unjoined");
    }
    if (QCoreApplication::instance())
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &RemoteViewer::stop);
}

RemoteViewer::~RemoteViewer() = default;

bool RemoteViewer::running() const { return m_process && m_process->state() == QProcess::Running; }

bool RemoteViewer::ensure(QString *error)
{
    if (m_process) return true;
    const QString script = QString::fromLocal8Bit(qgetenv("RELAY_REMOTE_VIEWER"));
    const QString root = viewerRoot();
    if (script.isEmpty() && root.isEmpty()) {
        if (error) *error = QStringLiteral("Relay's remote viewer (remote/viewer.py) is missing from this installation.");
        return false;
    }
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(root.isEmpty() ? QFileInfo(script).absolutePath() : root);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    m_process->setProcessEnvironment(environment);
    m_process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &RemoteViewer::onReadable);
    QProcess *process = m_process;
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process](int, QProcess::ExitStatus) {
                if (m_process != process) return;
                m_process->deleteLater();
                m_process = nullptr;
                m_pending.clear();
                m_state = QStringLiteral("offline");
                emit status(m_state, QStringLiteral("The viewer stopped."));
            });
    QStringList arguments = script.isEmpty() ? QStringList{QStringLiteral("-m"), QStringLiteral("remote.viewer")} : QStringList{script};
    if (m_guest) arguments << QStringLiteral("--guest");
    m_process->start(QStringLiteral("python3"), arguments);
    if (!m_process->waitForStarted(5000)) {
        if (error) *error = QStringLiteral("python3 could not start the remote viewer.");
        m_process->deleteLater();
        m_process = nullptr;
        return false;
    }
    return true;
}

void RemoteViewer::send(const QJsonObject &message)
{
    if (!running()) return;
    m_process->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

void RemoteViewer::onReadable()
{
    if (!m_process) return;
    m_pending.append(m_process->readAllStandardOutput());
    int newline;
    while ((newline = m_pending.indexOf('\n')) >= 0) {
        const QByteArray line = m_pending.left(newline);
        m_pending.remove(0, newline + 1);
        if (line.trimmed().isEmpty()) continue;
        const QJsonObject message = QJsonDocument::fromJson(line).object();
        if (!message.isEmpty()) handle(message);
        if (!m_process) return;
    }
}

void RemoteViewer::handle(const QJsonObject &line)
{
    const QString kind = str(line.value(QStringLiteral("t")));
    if (kind == QLatin1String("status")) {
        m_state = str(line.value(QStringLiteral("state")));
        emit status(m_state, str(line.value(QStringLiteral("message"))));
    } else if (kind == QLatin1String("code")) {
        emit code(line.value(QStringLiteral("code")).isString() ? str(line.value(QStringLiteral("code")))
                                                               : QString::number(line.value(QStringLiteral("code")).toInt()));
    } else if (kind == QLatin1String("paired")) {
        m_desktop = str(line.value(QStringLiteral("desktop")));
        emit paired(m_desktop, str(line.value(QStringLiteral("fingerprint"))));
    } else if (kind == QLatin1String("joined")) {
        m_desktop = str(line.value(QStringLiteral("desktop")));
        const QString role = str(line.value(QStringLiteral("role")));
        if (!role.isEmpty()) m_role = role;
        m_expires = qint64(line.value(QStringLiteral("expires")).toDouble());
        m_capability = QStringLiteral("guest");
        QStringList panes;
        for (const QJsonValue &value : line.value(QStringLiteral("panes")).toArray())
            if (value.isString()) panes.append(value.toString());
        emit joined(m_desktop, m_role, panes);
    } else if (kind == QLatin1String("ended")) {
        m_role.clear();
        m_participant.clear();
        m_state = QStringLiteral("unjoined");
        emit ended(str(line.value(QStringLiteral("message"))));
    } else if (kind == QLatin1String("welcome")) {
        m_capability = m_guest ? QStringLiteral("guest") : str(line.value(QStringLiteral("capability")));
        const QString role = str(line.value(QStringLiteral("role")));
        if (!role.isEmpty()) m_role = role;
        const QString participant = str(line.value(QStringLiteral("participant")));
        if (!participant.isEmpty()) m_participant = participant;
        m_features.clear();
        for (const QJsonValue &value : line.value(QStringLiteral("features")).toArray()) m_features.append(value.toString());
        const QString desktop = str(line.value(QStringLiteral("desktop")));
        if (!desktop.isEmpty()) m_desktop = desktop;
        m_device = str(line.value(QStringLiteral("device")));
        emit welcome(m_capability, m_features);
    } else if (kind == QLatin1String("message")) {
        const QJsonObject message = line.value(QStringLiteral("message")).toObject();
        const QString t = str(message.value(QStringLiteral("t")));
        if (t == QLatin1String("panes")) {
            m_panes = message.value(QStringLiteral("items")).toArray();
            emit panesChanged(m_panes);
        } else if (t == QLatin1String("welcome")) {
            const QString name = str(message.value(QStringLiteral("desktop")).toObject().value(QStringLiteral("name")));
            if (!name.isEmpty()) m_desktop = name;
            if (!m_guest && message.value(QStringLiteral("capability")).isString()) m_capability = str(message.value(QStringLiteral("capability")));
            if (m_guest && message.value(QStringLiteral("role")).isString()) m_role = str(message.value(QStringLiteral("role")));
        } else if (t == QLatin1String("participants") && m_guest) {
            // The guest's own row keeps its role current, for the panes opened after a change.
            for (const QJsonValue &value : message.value(QStringLiteral("items")).toArray()) {
                const QJsonObject item = value.toObject();
                if (!item.value(QStringLiteral("you")).toBool()) continue;
                const QString role = str(item.value(QStringLiteral("role")));
                if (role == QLatin1String("viewer") || role == QLatin1String("editor")) m_role = role;
                if (!str(item.value(QStringLiteral("id"))).isEmpty()) m_participant = str(item.value(QStringLiteral("id")));
            }
        }
        emit this->message(message);
    } else if (kind == QLatin1String("error")) {
        emit failed(str(line.value(QStringLiteral("message"))), str(line.value(QStringLiteral("reason"))));
    }
}

void RemoteViewer::paneOpened(const QString &paneId)
{
    m_openIds.insert(paneId);
    m_open = int(m_openIds.size());
}

void RemoteViewer::paneClosed(const QString &paneId)
{
    if (!m_openIds.remove(paneId)) return;
    m_open = int(m_openIds.size());
    send({{"t", "close"}, {"pane", paneId}});
    release();
}

void RemoteViewer::release()
{
    if (m_open > 0 || m_dialogUp || !m_process) return;
    // Later rather than now: a pane closed from inside the dialog's own flow must not take the
    // process down under the next line it sends.
    QTimer::singleShot(0, this, [this] {
        if (m_open > 0 || m_dialogUp) return;
        stop();
    });
}

void RemoteViewer::stop()
{
    if (!m_process) return;
    QProcess *process = m_process;
    send({{"t", "stop"}});
    process->closeWriteChannel();
    if (!process->waitForFinished(1500)) {
        process->kill();
        process->waitForFinished(500);
    }
    if (m_process == process) {
        m_process->deleteLater();
        m_process = nullptr;
        m_state = QStringLiteral("offline");
    }
    m_openIds.clear();
    m_open = 0;
    // A joined share goes with its sidecar: a later join starts a session of its own.
    if (m_guest) delete GuestSession::current();
}

// ----- the dialog --------------------------------------------------------------------------------

RemotePaneDialog::RemotePaneDialog(Place place, QWidget *parent) : QDialog(parent), m_place(std::move(place))
{
    setWindowTitle(QStringLiteral("Open a shared pane"));
    setObjectName(QStringLiteral("remotePaneDialog"));
    setMinimumWidth(520);
    auto *layout = new QVBoxLayout(this);
    m_pages = new QStackedWidget;
    layout->addWidget(m_pages, 1);

    m_waitPage = new QWidget;
    {
        auto *v = new QVBoxLayout(m_waitPage);
        m_wait = plainLabel(QStringLiteral("remoteWait"));
        m_wait->setWordWrap(true);
        m_wait->setText(QStringLiteral("Connecting…"));
        m_wait->setAlignment(Qt::AlignHCenter);
        v->addStretch(1);
        v->addWidget(m_wait);
        v->addStretch(1);
    }
    m_pages->addWidget(m_waitPage);

    m_pairPage = new QWidget;
    {
        auto *v = new QVBoxLayout(m_pairPage);
        auto *intro = plainLabel(QStringLiteral("remotePairIntro"));
        intro->setWordWrap(true);
        intro->setText(QStringLiteral("On the desktop, open “Share this pane…” and copy its pairing link. "
                                      "Paste it here: this Relay becomes one of your own devices on that desktop."));
        v->addWidget(intro);
        m_link = new QLineEdit;
        m_link->setObjectName(QStringLiteral("remotePairLink"));
        m_link->setPlaceholderText(QStringLiteral("https://…/pair#…"));
        m_link->setClearButtonEnabled(true);
        v->addWidget(m_link);
        m_pairButton = new QPushButton(QStringLiteral("Pair"));
        m_pairButton->setObjectName(QStringLiteral("remotePair"));
        m_pairButton->setDefault(true);
        m_pairButton->setEnabled(false);
        auto *row = new QHBoxLayout;
        row->addStretch(1);
        row->addWidget(m_pairButton);
        v->addLayout(row);
        v->addStretch(1);
        connect(m_link, &QLineEdit::textChanged, this, [this](const QString &text) { m_pairButton->setEnabled(!text.trimmed().isEmpty()); });
        connect(m_link, &QLineEdit::returnPressed, this, &RemotePaneDialog::pair);
        connect(m_pairButton, &QPushButton::clicked, this, &RemotePaneDialog::pair);
    }
    m_pages->addWidget(m_pairPage);

    m_codePage = new QWidget;
    {
        auto *v = new QVBoxLayout(m_codePage);
        auto *title = plainLabel(QStringLiteral("remoteCodeTitle"));
        title->setText(QStringLiteral("Confirmation code"));
        m_code = plainLabel(QStringLiteral("remoteCode"));
        QFont big = m_code->font();
        big.setPointSizeF(std::max<qreal>(22, big.pointSizeF() * 2.4));
        big.setBold(true);
        big.setLetterSpacing(QFont::AbsoluteSpacing, 4);
        m_code->setFont(big);
        m_codeNote = plainLabel(QStringLiteral("remoteCodeNote"));
        m_codeNote->setWordWrap(true);
        m_codeNote->setText(QStringLiteral("Check it matches your desktop."));
        // Full width with the text centred: a word-wrapped label placed with an alignment flag
        // is given its narrowest width and cuts the sentence off.
        for (QLabel *label : {title, m_code, m_codeNote}) label->setAlignment(Qt::AlignHCenter);
        v->addStretch(1);
        v->addWidget(title);
        v->addWidget(m_code);
        v->addWidget(m_codeNote);
        v->addStretch(1);
    }
    m_pages->addWidget(m_codePage);

    m_panesPage = new QWidget;
    {
        auto *v = new QVBoxLayout(m_panesPage);
        m_desktopLabel = plainLabel(QStringLiteral("remoteDesktop"));
        v->addWidget(m_desktopLabel);
        m_list = new QListWidget;
        m_list->setObjectName(QStringLiteral("remotePanes"));
        m_list->setTextElideMode(Qt::ElideMiddle);
        m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        v->addWidget(m_list, 1);
        auto *row = new QHBoxLayout;
        m_forget = new QPushButton(QStringLiteral("Forget this desktop"));
        m_forget->setObjectName(QStringLiteral("remoteForget"));
        m_open = new QPushButton(QStringLiteral("Open"));
        m_open->setObjectName(QStringLiteral("remoteOpen"));
        m_open->setDefault(true);
        m_open->setEnabled(false);
        row->addWidget(m_forget);
        row->addStretch(1);
        row->addWidget(m_open);
        v->addLayout(row);
        connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) { m_open->setEnabled(item); });
        connect(m_list, &QListWidget::itemActivated, this, [this] { openSelected(); });
        connect(m_open, &QPushButton::clicked, this, &RemotePaneDialog::openSelected);
        connect(m_forget, &QPushButton::clicked, this, [this] {
            RemoteViewer::instance().send({{"t", "forget"}});
            m_paired = false;
            m_list->clear();
            m_pages->setCurrentWidget(m_pairPage);
            m_link->setFocus();
        });
    }
    m_pages->addWidget(m_panesPage);

    m_status = plainLabel(QStringLiteral("remoteDialogStatus"));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *close = new QPushButton(QStringLiteral("Close"));
    close->setAutoDefault(false);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    RemoteViewer &viewer = RemoteViewer::instance();
    connect(&viewer, &RemoteViewer::status, this, &RemotePaneDialog::showStatus);
    connect(&viewer, &RemoteViewer::code, this, [this](const QString &code) {
        m_code->setText(code);
        m_pages->setCurrentWidget(m_codePage);
        m_status->setText(QString());
    });
    connect(&viewer, &RemoteViewer::paired, this, [this](const QString &desktop, const QString &) {
        m_paired = true;
        m_status->setText(QStringLiteral("Paired with %1.").arg(desktop.isEmpty() ? QStringLiteral("the desktop") : desktop));
    });
    connect(&viewer, &RemoteViewer::panesChanged, this, &RemotePaneDialog::showPanes);
    connect(&viewer, &RemoteViewer::failed, this, [this](const QString &message) {
        m_status->setText(message);
        if (m_pages->currentWidget() == m_waitPage || m_pages->currentWidget() == m_codePage) {
            m_pages->setCurrentWidget(m_pairPage);
            m_link->setFocus();
        }
    });

    QString error;
    if (!viewer.ensure(&error)) {
        m_status->setText(error);
        m_pages->setCurrentWidget(m_pairPage);
        m_link->setEnabled(false);
        return;
    }
    viewer.setDialogUp(true);
    if (viewer.state() == QLatin1String("connected")) {
        showPanes(viewer.panes());
        m_pages->setCurrentWidget(m_panesPage);
    } else {
        // Paired already? Then this is all it takes; "unpaired" comes back otherwise.
        m_pages->setCurrentWidget(m_waitPage);
        viewer.send({{"t", "connect"}});
    }
}

RemotePaneDialog::~RemotePaneDialog()
{
    RemoteViewer &viewer = RemoteViewer::instance();
    viewer.setDialogUp(false);
    viewer.release();
}

void RemotePaneDialog::open(QWidget *parent, Place place)
{
    auto *dialog = new RemotePaneDialog(std::move(place), parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void RemotePaneDialog::showStatus(const QString &state, const QString &message)
{
    const QString desktop = RemoteViewer::instance().desktop();
    const QString who = desktop.isEmpty() ? QStringLiteral("the desktop") : desktop;
    if (state == QLatin1String("unpaired")) {
        if (m_pages->currentWidget() != m_codePage) m_pages->setCurrentWidget(m_pairPage);
        m_link->setFocus();
        m_status->setText(message);
    } else if (state == QLatin1String("pairing")) {
        m_status->setText(message.isEmpty() ? QStringLiteral("Pairing…") : message);
    } else if (state == QLatin1String("connecting")) {
        if (m_pages->currentWidget() != m_codePage) {
            m_wait->setText(QStringLiteral("Connecting to %1…").arg(who));
            m_pages->setCurrentWidget(m_waitPage);
        }
        m_status->setText(message);
    } else if (state == QLatin1String("connected")) {
        showPanes(RemoteViewer::instance().panes());
        m_pages->setCurrentWidget(m_panesPage);
        m_status->setText(message);
    } else if (state == QLatin1String("reconnecting")) {
        m_status->setText(QStringLiteral("Reconnecting to %1…").arg(who));
    } else if (state == QLatin1String("offline")) {
        m_status->setText(message.isEmpty() ? QStringLiteral("Offline.") : message);
        if (m_pages->currentWidget() == m_waitPage) m_pages->setCurrentWidget(m_pairPage);
    }
}

void RemotePaneDialog::showPanes(const QJsonArray &items)
{
    const QString desktop = RemoteViewer::instance().desktop();
    m_desktopLabel->setText(items.isEmpty() ? QStringLiteral("Waiting for the desktop's panes…")
                                            : QStringLiteral("Panes shared by %1").arg(desktop.isEmpty() ? QStringLiteral("the desktop") : desktop));
    const QString selected = m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toString() : QString();
    m_list->clear();
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QString id = str(item.value(QStringLiteral("id")));
        if (id.isEmpty()) continue;
        const QString title = str(item.value(QStringLiteral("title")));
        const QString cwd = str(item.value(QStringLiteral("cwd")));
        const QString status = str(item.value(QStringLiteral("status")));
        QString text = title.isEmpty() ? id : title;
        if (!cwd.isEmpty()) text += QStringLiteral("  —  ") + cwd;
        if (!status.isEmpty() && status != QLatin1String("idle")) text += QStringLiteral("  ·  ") + status;
        auto *row = new QListWidgetItem(text);   // a list item's text is never rich text
        row->setData(Qt::UserRole, id);
        row->setData(Qt::UserRole + 1, title);
        m_list->addItem(row);
        if (id == selected || (selected.isEmpty() && m_list->count() == 1)) m_list->setCurrentItem(row);
    }
    if (m_pages->currentWidget() == m_codePage && !items.isEmpty()) m_pages->setCurrentWidget(m_panesPage);
}

void RemotePaneDialog::pair()
{
    const QString url = m_link->text().trimmed();
    if (url.isEmpty()) return;
    RemoteViewer::instance().send({{"t", "pair"}, {"url", url}, {"name", QSysInfo::machineHostName()},
                                   {"platform", "Relay"}});
    m_link->clear();   // it carries a one-time secret; it is not left on screen
    m_wait->setText(QStringLiteral("Pairing…"));
    m_pages->setCurrentWidget(m_waitPage);
}

void RemotePaneDialog::openSelected()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item) return;
    const QString id = item->data(Qt::UserRole).toString();
    const QString title = item->data(Qt::UserRole + 1).toString();
    RemotePane *pane = RemotePane::openFromViewer(id, title, RemoteViewer::instance().desktop());
    if (m_place) m_place(pane);
    accept();
}

// ----- GuestSession ------------------------------------------------------------------------------

namespace {
QPointer<GuestSession> g_guestSession;
} // namespace

GuestSession *GuestSession::current() { return g_guestSession.data(); }

GuestSession::GuestSession(Place place) : QObject(&RemoteViewer::guest()), m_place(std::move(place))
{
    RemoteViewer &viewer = RemoteViewer::guest();
    connect(&viewer, &RemoteViewer::panesChanged, this, &GuestSession::sync);
    // Each pane marks itself ended from the same signal; there is nothing left to follow.
    connect(&viewer, &RemoteViewer::ended, this, &QObject::deleteLater);
}

GuestSession *GuestSession::start(const QStringList &panes, Place place)
{
    auto *session = new GuestSession(std::move(place));
    // A rejoin of the share already open adopts its panes rather than opening them twice.
    if (GuestSession *old = g_guestSession.data()) {
        for (auto it = old->m_panes.cbegin(); it != old->m_panes.cend(); ++it)
            if (it.value() && !it.value()->ended()) session->m_panes.insert(it.key(), it.value());
        delete old;
    }
    g_guestSession = session;
    for (const QString &id : panes) {
        if (id.isEmpty() || session->m_known.contains(id)) continue;
        session->m_known.insert(id);
        if (!session->m_panes.value(id)) session->openPane(id);
    }
    return session;
}

QStringList GuestSession::openIds() const
{
    QStringList ids;
    for (auto it = m_panes.cbegin(); it != m_panes.cend(); ++it)
        if (it.value()) ids << it.key();
    ids.sort();
    return ids;
}

void GuestSession::openPane(const QString &id)
{
    RemoteViewer &viewer = RemoteViewer::guest();
    QString title;
    for (const QJsonValue &value : viewer.panes())
        if (str(value.toObject().value(QStringLiteral("id"))) == id) title = str(value.toObject().value(QStringLiteral("title")));
    RemotePane *pane = RemotePane::openFromViewer(id, title, viewer.desktop(), viewer);
    m_panes.insert(id, pane);
    const bool first = !m_placed;
    m_placed = true;
    if (m_place) m_place(pane, first);
}

void GuestSession::sync(const QJsonArray &items)
{
    QStringList scope;
    for (const QJsonValue &value : items) {
        const QString id = str(value.toObject().value(QStringLiteral("id")));
        if (!id.isEmpty() && !scope.contains(id)) scope << id;
    }
    // Gone from the scope: the pane stays, with the reason on it, and the viewer stops streaming it.
    const QSet<QString> known = m_known;
    for (const QString &id : known) {
        if (scope.contains(id)) continue;
        m_known.remove(id);
        if (QPointer<RemotePane> pane = m_panes.take(id)) {
            pane->markEnded(QStringLiteral("No longer shared with you."));
            pane->onClosed = nullptr;
            RemoteViewer::guest().paneClosed(id);
        }
    }
    // New to it: opened and placed beside the others, as a tab shared whole grows.
    for (const QString &id : scope) {
        if (m_known.contains(id)) continue;
        m_known.insert(id);
        if (!m_panes.value(id)) openPane(id);
    }
}

// ----- JoinDialog --------------------------------------------------------------------------------

namespace {

// The viewer's reason, in words, for when it sends none of its own.
QString joinErrorWords(const QString &reason)
{
    if (reason == QLatin1String("wrong_pin")) return QStringLiteral("That PIN is not right. Check it and try again.");
    if (reason == QLatin1String("burned")) return QStringLiteral("That code had too many wrong PINs and no longer works. Ask for a new one.");
    if (reason == QLatin1String("expired")) return QStringLiteral("That code has expired. Ask for a new one.");
    if (reason == QLatin1String("no_such_code")) return QStringLiteral("Nothing is shared under that code. Check the four letters.");
    if (reason == QLatin1String("not_admitted")) return QStringLiteral("You were not let in.");
    if (reason == QLatin1String("rate_limited")) return QStringLiteral("Too many tries. Wait a minute, then try again.");
    if (reason == QLatin1String("closed")) return QStringLiteral("The share closed before you were let in.");
    return QStringLiteral("Joining did not work. Try again.");
}

QString loginName()
{
    const QString user = qEnvironmentVariable("USER");
    return user.isEmpty() ? QDir::home().dirName() : user;
}

} // namespace

JoinDialog::JoinDialog(const QString &code, Place place, QWidget *parent) : QDialog(parent), m_place(std::move(place))
{
    setWindowTitle(QStringLiteral("Join a shared session"));
    setObjectName(QStringLiteral("joinDialog"));
    setMinimumWidth(440);
    auto *layout = new QVBoxLayout(this);
    m_pages = new QStackedWidget;
    layout->addWidget(m_pages, 1);
    const QSettings settings;

    m_formPage = new QWidget;
    {
        auto *v = new QVBoxLayout(m_formPage);
        auto *intro = plainLabel(QStringLiteral("joinIntro"));
        intro->setWordWrap(true);
        intro->setText(QStringLiteral("Enter the meeting code and PIN from the person sharing. "
                                      "They see your name and let you in."));
        v->addWidget(intro);
        auto *form = new QFormLayout;
        m_code = new QLineEdit;
        m_code->setObjectName(QStringLiteral("joinCode"));
        m_code->setPlaceholderText(QStringLiteral("BQRT"));
        m_code->setMaxLength(4);
        m_code->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[A-Za-z]{0,4}")), m_code));
        QString prefill;
        for (const QChar c : code.trimmed())
            if (c.isLetter() && c.unicode() < 128) prefill += c.toUpper();
        m_code->setText(prefill.left(4));
        m_pin = new QLineEdit;
        m_pin->setObjectName(QStringLiteral("joinPin"));
        m_pin->setEchoMode(QLineEdit::Password);
        m_pin->setMaxLength(4);
        m_pin->setPlaceholderText(QStringLiteral("4 digits"));
        m_pin->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{0,4}")), m_pin));
        m_name = new QLineEdit;
        m_name->setObjectName(QStringLiteral("joinName"));
        const QString savedName = settings.value(QStringLiteral("remote/joinName")).toString();
        m_name->setText(savedName.isEmpty() ? loginName() : savedName);
        m_name->setMaxLength(64);
        form->addRow(QStringLiteral("Code"), m_code);
        form->addRow(QStringLiteral("PIN"), m_pin);
        form->addRow(QStringLiteral("Your name"), m_name);
        v->addLayout(form);

        // The rendezvous, folded away: almost nobody joins through anything but the default.
        const QString server = settings.value(QStringLiteral("remote/joinServer"), QString::fromLatin1(kDefaultServer)).toString();
        auto *serverRow = new QHBoxLayout;
        m_serverToggle = new QToolButton;
        m_serverToggle->setObjectName(QStringLiteral("joinServerToggle"));
        m_serverToggle->setText(QStringLiteral("Server…"));
        m_serverToggle->setAutoRaise(true);
        m_serverToggle->setCheckable(true);
        m_server = new QLineEdit;
        m_server->setObjectName(QStringLiteral("joinServer"));
        m_server->setText(server);
        m_server->setPlaceholderText(QString::fromLatin1(kDefaultServer));
        serverRow->addWidget(m_serverToggle);
        serverRow->addWidget(m_server, 1);
        v->addLayout(serverRow);
        // A server that is not the default is shown, so nobody joins through it without seeing it.
        const bool custom = server != QLatin1String(kDefaultServer);
        m_serverToggle->setChecked(custom);
        m_server->setVisible(custom);
        connect(m_serverToggle, &QToolButton::toggled, this, [this](bool on) {
            m_server->setVisible(on);
            if (on) m_server->setFocus();
        });

        m_error = plainLabel(QStringLiteral("joinError"));
        m_error->setWordWrap(true);
        // A join that failed is an error, so it is painted in the error token: amber is reserved
        // for "this is waiting on you" (be81edb) and said the wrong thing here. Restyled on every
        // theme change too — set once in the constructor, it kept the old theme's colour.
        const auto restyleError = [this] {
            m_error->setStyleSheet(QStringLiteral("color: %1;").arg(theme::Error.name()));
        };
        restyleError();
        connect(theme::notifier(), &theme::Notifier::themeChanged, this, restyleError);
        m_error->hide();
        v->addWidget(m_error);
        v->addStretch(1);

        auto *row = new QHBoxLayout;
        auto *close = new QPushButton(QStringLiteral("Close"));
        close->setAutoDefault(false);
        connect(close, &QPushButton::clicked, this, &JoinDialog::reject);
        m_join = new QPushButton(QStringLiteral("Join"));
        m_join->setObjectName(QStringLiteral("joinButton"));
        m_join->setDefault(true);
        row->addStretch(1);
        row->addWidget(close);
        row->addWidget(m_join);
        v->addLayout(row);

        connect(m_code, &QLineEdit::textEdited, this, [this](const QString &text) {
            const int at = m_code->cursorPosition();
            if (text != text.toUpper()) {
                m_code->setText(text.toUpper());
                m_code->setCursorPosition(at);
            }
            // Four letters in: on to the PIN, as a code read out is typed in one go.
            if (m_code->text().size() == 4 && m_pin->text().isEmpty()) m_pin->setFocus();
        });
        // Enter in any field presses Join through the dialog's default button, and only that way:
        // wiring returnPressed as well joined, moved the focus to Cancel, and the same key then
        // reached the dialog and pressed Cancel (the live drive of 2026-09-18 caught it).
        for (QLineEdit *edit : {m_code, m_pin, m_name, m_server})
            connect(edit, &QLineEdit::textChanged, this, &JoinDialog::updateJoinButton);
        connect(m_join, &QPushButton::clicked, this, &JoinDialog::join);
    }
    m_pages->addWidget(m_formPage);

    m_waitPage = new QWidget;
    {
        auto *v = new QVBoxLayout(m_waitPage);
        m_wait = plainLabel(QStringLiteral("joinWait"));
        m_wait->setWordWrap(true);
        m_check = plainLabel(QStringLiteral("joinCheckCode"));
        QFont big = m_check->font();
        big.setPointSizeF(std::max<qreal>(22, big.pointSizeF() * 2.4));
        big.setBold(true);
        big.setLetterSpacing(QFont::AbsoluteSpacing, 4);
        m_check->setFont(big);
        m_check->hide();
        // Full width with the text centred (see the pairing dialog's code page).
        for (QLabel *label : {m_wait, m_check}) label->setAlignment(Qt::AlignHCenter);
        v->addStretch(1);
        v->addWidget(m_wait);
        v->addWidget(m_check);
        v->addStretch(1);
        auto *row = new QHBoxLayout;
        m_cancel = new QPushButton(QStringLiteral("Cancel"));
        m_cancel->setObjectName(QStringLiteral("joinCancel"));
        // Never the button Enter presses: a stray Enter while waiting must not withdraw the knock.
        m_cancel->setAutoDefault(false);
        row->addStretch(1);
        row->addWidget(m_cancel);
        v->addLayout(row);
        connect(m_cancel, &QPushButton::clicked, this, [this] {
            RemoteViewer::guest().send({{"t", "leave"}});
            backToForm(QString());
        });
    }
    m_pages->addWidget(m_waitPage);
    m_pages->setCurrentWidget(m_formPage);
    updateJoinButton();
    (m_code->text().size() == 4 ? m_pin : m_code)->setFocus();

    RemoteViewer &viewer = RemoteViewer::guest();
    connect(&viewer, &RemoteViewer::status, this, &JoinDialog::showStatus);
    connect(&viewer, &RemoteViewer::code, this, [this](const QString &check) {
        if (m_done) return;
        m_check->setText(check);
        m_check->setVisible(!check.isEmpty());
    });
    connect(&viewer, &RemoteViewer::failed, this, &JoinDialog::showError);
    connect(&viewer, &RemoteViewer::joined, this, &JoinDialog::onJoined);
    // A rejoin may come back as the share's pane list rather than a `joined` line of its own.
    connect(&viewer, &RemoteViewer::panesChanged, this, [this](const QJsonArray &items) {
        RemoteViewer &guest = RemoteViewer::guest();
        if (m_done || !m_rejoin || !waiting() || guest.state() != QLatin1String("connected") || items.isEmpty()) return;
        QStringList ids;
        for (const QJsonValue &value : items) ids << str(value.toObject().value(QStringLiteral("id")));
        onJoined(guest.desktop(), guest.role(), ids);
    });

    QString error;
    if (!viewer.ensure(&error)) {
        showError(error, QString());
        m_join->setEnabled(false);
        return;
    }
    viewer.setDialogUp(true);
    updateJoinButton();
    // Without a code, a guest record still stored from an earlier join is tried first.
    if (m_code->text().isEmpty() && viewer.state() != QLatin1String("connected")) {
        m_rejoin = true;
        viewer.send({{"t", "connect"}});
    }
}

JoinDialog::~JoinDialog()
{
    RemoteViewer &viewer = RemoteViewer::guest();
    viewer.setDialogUp(false);
    viewer.release();
}

void JoinDialog::open(QWidget *parent, const QString &code, std::function<void(RemotePane *pane, bool first)> place)
{
    auto *dialog = new JoinDialog(code, std::move(place), parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void JoinDialog::reject()
{
    // Closing while the knock waits takes it back, rather than leaving the owner a knock from
    // somebody who is no longer there.
    if (waiting() && !m_done) RemoteViewer::guest().send({{"t", "leave"}});
    QDialog::reject();
}

bool JoinDialog::waiting() const { return m_pages->currentWidget() == m_waitPage; }

void JoinDialog::updateJoinButton()
{
    static const QRegularExpression codeShape(QStringLiteral("^[A-Z]{4}$")), pinShape(QStringLiteral("^[0-9]{4}$"));
    m_join->setEnabled(codeShape.match(m_code->text()).hasMatch() && pinShape.match(m_pin->text()).hasMatch()
                       && !m_name->text().trimmed().isEmpty() && RemoteViewer::guest().running());
}

void JoinDialog::join()
{
    updateJoinButton();
    if (!m_join->isEnabled()) return;
    m_rejoin = false;
    const QString server = m_server->text().trimmed().isEmpty() ? QString::fromLatin1(kDefaultServer) : m_server->text().trimmed();
    RemoteViewer::guest().send({{"t", "join"}, {"code", m_code->text()}, {"pin", m_pin->text()},
                                {"name", m_name->text().trimmed()}, {"platform", "Relay"}, {"rendezvous", server}});
    m_error->hide();
    m_wait->setText(QStringLiteral("Checking the PIN…"));
    m_check->clear();
    m_check->hide();
    m_pages->setCurrentWidget(m_waitPage);
    m_cancel->setFocus();
}

void JoinDialog::backToForm(const QString &message)
{
    if (!message.isEmpty()) setPlain(m_error, message);
    m_check->hide();
    m_pages->setCurrentWidget(m_formPage);
    updateJoinButton();
    (m_code->text().size() == 4 ? (m_pin->text().size() == 4 ? m_join : static_cast<QWidget *>(m_pin)) : m_code)->setFocus();
}

void JoinDialog::showStatus(const QString &state, const QString &message)
{
    if (m_done) return;
    const QString desktop = RemoteViewer::guest().desktop();
    const QString who = desktop.isEmpty() ? QStringLiteral("the person sharing") : desktop;
    if (state == QLatin1String("joining")) {
        if (waiting()) m_wait->setText(QStringLiteral("Checking the PIN…"));
    } else if (state == QLatin1String("knocking")) {
        if (waiting()) m_wait->setText(QStringLiteral("Waiting for %1 to let you in. They see the code").arg(who));
    } else if (state == QLatin1String("connecting")) {
        // The rejoin a dialog opened without a code asks for shows its progress; a viewer going
        // back to a share it is still in after a refused join does not take the form away.
        if (m_rejoin && !waiting()) m_pages->setCurrentWidget(m_waitPage);
        if (waiting() && m_check->isHidden()) m_wait->setText(QStringLiteral("Connecting to %1…").arg(who));
    } else if (state == QLatin1String("reconnecting")) {
        if (waiting()) m_wait->setText(QStringLiteral("Reconnecting to %1…").arg(who));
    } else if (state == QLatin1String("offline")) {
        if (waiting()) backToForm(message.isEmpty() ? QStringLiteral("Offline: the server cannot be reached.") : message);
    } else if (state == QLatin1String("unjoined")) {
        if (waiting()) backToForm(message);
    }
    if (state == QLatin1String("unjoined") || state == QLatin1String("offline")) m_rejoin = false;
}

void JoinDialog::showError(const QString &message, const QString &reason)
{
    if (m_done) return;
    const QString text = message.isEmpty() ? joinErrorWords(reason) : message;
    backToForm(text);
    if (reason == QLatin1String("wrong_pin")) {
        m_pin->clear();
        m_pin->setFocus();
    }
}

void JoinDialog::onJoined(const QString &, const QString &, const QStringList &panes)
{
    if (m_done) return;
    m_done = true;
    QSettings settings;
    settings.setValue(QStringLiteral("remote/joinName"), m_name->text().trimmed());
    const QString server = m_server->text().trimmed();
    if (server.isEmpty() || server == QLatin1String(kDefaultServer)) settings.remove(QStringLiteral("remote/joinServer"));
    else settings.setValue(QStringLiteral("remote/joinServer"), server);
    GuestSession::start(panes, m_place);
    accept();
}

} // namespace relay
