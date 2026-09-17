// SPDX-License-Identifier: GPL-3.0-or-later
// Spike: libvterm-backed terminal widget rendered with QPainter.
// Original code; Qt Creator's src/libs/solutions/terminal was consulted only
// for the general libvterm integration approach.
#pragma once

#include "Pty.h"
#include "TerminalBackend.h"

#include <QFont>
#include <QRegion>
#include <QTimer>
#include <QWidget>

#include <deque>
#include <memory>
#include <vector>

extern "C" {
#include <vterm.h>
}

namespace relay {

class VTermWidget : public QWidget, public TerminalBackend {
    Q_OBJECT
public:
    explicit VTermWidget(QWidget *parent = nullptr);
    ~VTermWidget() override;

    // TerminalBackend
    bool startProgram(const QString &program, const QStringList &args, const QString &workingDirectory,
                      const QStringList &extraEnvironment = {}) override;
    void sendInput(const QByteArray &bytes) override;
    void sendText(const QString &text, bool asPaste) override;
    qint64 shellPid() const override;
    qint64 foregroundProcessId() const override;
    int capabilities() const override;
    QString screenText() const override;
    QStringList scrollbackText(int maxLines) const override;
    bool altScreen() const override { return m_altScreen; }
    int rows() const override { return m_rows; }
    int columns() const override { return m_cols; }
    void resizeTerminal(int rows, int columns) override;
    QWidget *widget() override { return this; }
    QWidget *focusWidget() override { return this; }

    // Spike diagnostics
    QString debugDump() const;
    quint64 bytesProcessed() const { return m_bytes; }
    quint64 paintCount() const { return m_paints; }
    void setScrollbackLimit(int lines) { m_sbLimit = lines; }
    void feedForBenchmark(const char *data, qint64 len) { onPtyOutput(data, len); }
    QSize sizeForGrid(int rows, int cols) const { return QSize(cols * m_cw, rows * m_ch); }

signals:
    void linkActivated(const QString &uri);
    void pathActivated(const QString &path);
    void dumpRequested();

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void inputMethodEvent(QInputMethodEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    bool focusNextPrevChild(bool) override { return false; }
    bool event(QEvent *) override;

private:
    using Line = std::vector<VTermScreenCell>;
    struct Link {
        qint64 startLine;
        int startCol;
        qint64 endLine;
        int endCol; // exclusive
        QString uri;
    };
    struct CellPos {
        qint64 line; // absolute line id
        int col;
    };

    // libvterm callbacks
    static int cbDamage(VTermRect rect, void *user);
    static int cbMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void *user);
    static int cbSetTermProp(VTermProp prop, VTermValue *val, void *user);
    static int cbBell(void *user);
    static int cbSbPushLine(int cols, const VTermScreenCell *cells, void *user);
    static int cbSbPopLine(int cols, VTermScreenCell *cells, void *user);
    static int cbSbClear(void *user);
    static int cbOsc(int command, VTermStringFragment frag, void *user);
    static void cbOutput(const char *s, size_t len, void *user);

    void onPtyOutput(const char *data, qint64 len);
    void scheduleRepaint(const QRect &r);
    void updateCellMetrics();
    QRect cellRect(int row, int col, int width = 1) const;

    // Absolute line ids: scrollback and screen rows share one numbering so that
    // selection and links survive output scrolling.
    qint64 firstLineId() const { return m_pushed - qint64(m_scrollback.size()); }
    qint64 screenLineId(int row) const { return m_pushed + row; }
    bool cellAt(qint64 lineId, int col, VTermScreenCell *out) const;
    QString lineText(qint64 lineId, int fromCol = 0, int toCol = -1) const;
    CellPos posAt(const QPoint &p) const;
    qint64 topVisibleLineId() const { return screenLineId(0) - m_scrollOffset; }
    bool isSelected(qint64 line, int col) const;
    QString selectedText() const;
    void setScrollOffset(int off);
    const Link *linkAt(qint64 line, int col) const;
    bool activateAt(const CellPos &pos);
    VTermModifier modifiers(Qt::KeyboardModifiers m) const;
    QColor toQColor(VTermColor c, bool fg) const;
    void copySelection(bool clipboard);
    void pasteFrom(bool clipboard);
    QString currentDirectory() const;

    VTerm *m_vt = nullptr;
    VTermScreen *m_screen = nullptr;
    VTermState *m_state = nullptr;
    std::unique_ptr<Pty> m_pty;

    int m_rows = 24;
    int m_cols = 80;
    QFont m_font;
    QFont m_boldFont, m_italicFont, m_boldItalicFont;
    int m_cw = 8, m_ch = 16, m_ascent = 12;

    std::deque<Line> m_scrollback;
    int m_sbLimit = 10000;
    qint64 m_pushed = 0; // total lines pushed minus popped
    int m_scrollOffset = 0;

    VTermPos m_cursor{0, 0};
    bool m_cursorVisible = true;
    int m_cursorShape = VTERM_PROP_CURSORSHAPE_BLOCK;
    bool m_altScreen = false;
    int m_mouseMode = VTERM_PROP_MOUSE_NONE;
    bool m_hasFocus = false;

    QRegion m_pendingDamage;
    QTimer m_repaintTimer;

    bool m_selecting = false;
    bool m_hasSelection = false;
    CellPos m_selAnchor{0, 0}, m_selEnd{0, 0};

    QByteArray m_oscBuf;
    bool m_linkOpen = false;
    CellPos m_linkStart{0, 0};
    QString m_linkUri;
    std::vector<Link> m_links;
    QString m_titleText;

    quint64 m_bytes = 0;
    quint64 m_bytesAtLastFlush = 0;
    quint64 m_paints = 0;
    QString m_preedit;
};

} // namespace relay
