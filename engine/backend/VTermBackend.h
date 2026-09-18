// SPDX-License-Identifier: GPL-3.0-or-later
// relay::VTermBackend: TerminalBackend over Relay's own engine
// (TerminalSession + TerminalView, libghostty-vt or libvterm core).
#pragma once

#include "TerminalBackend.h"

#include <QObject>

class QScrollBar;

namespace relay {

class TerminalSession;
class TerminalView;

class VTermBackend : public QObject, public TerminalBackend {
    Q_OBJECT
public:
    // coreName: "ghostty", "libvterm" or empty for the default core.
    explicit VTermBackend(const QString &coreName = QString(), QWidget *parent = nullptr);
    ~VTermBackend() override;

    TerminalSession *session() const { return m_session; }
    TerminalView *view() const { return m_view; }

    bool startProgram(const QString &program, const QStringList &args, const QString &workingDirectory,
                      const QStringList &extraEnvironment = {}) override;
    void sendInput(const QByteArray &bytes) override;
    void sendText(const QString &text, bool asPaste) override;
    qint64 shellPid() const override;
    qint64 foregroundProcessId() const override;
    bool isRunning() const override;
    TermiosFlags termiosFlags() const override;

    void writeToDisplay(const QByteArray &bytes) override;
    void redrawPrompt() override;
    void setRedrawPromptSequence(const QByteArray &bytes) override { m_redrawSequence = bytes; }

    int capabilities() const override;
    QString screenText() const override;
    QStringList scrollbackText(int maxLines) const override;
    bool altScreen() const override;
    int rows() const override;
    int columns() const override;
    QString title() const override;
    QString currentDirectory() const override;

    void resizeTerminal(int rows, int columns) override;
    QWidget *widget() override;
    QWidget *focusWidget() override;
    void setTerminalFont(const QFont &font) override;

    void copySelection() override;
    void paste() override;
    QString selectedText() const override;
    void selectAll() override;
    void clearScrollback() override;
    void clear() override;

    void scrollLines(int lines) override;
    void scrollPages(int pages) override;
    void scrollToBottom() override;
    bool viewportAtBottom() const override;
    bool scrollToPrompt(int direction) override;

    int find(const QString &text, bool backwards) override;

    QString linkAt(const QPoint &pos, int *line = nullptr, int *column = nullptr) override;
    bool zoom(int step) override;
    bool stepLink(int delta, Link *link, int *index, int *count) override;
    void endLinkWalk() override;
    bool linkWalkActive() const override;
    void setPlainClickOpensLinks(bool on) override;
    void setCardLookup(std::function<bool(const QString &id, QString *title)> lookup) override;

    void setOutputCallbackEnabled(bool enabled) override;

private:
    TerminalSession *m_session = nullptr;
    QWidget *m_container = nullptr;
    TerminalView *m_view = nullptr;
    QScrollBar *m_scrollBar = nullptr;
    QByteArray m_redrawSequence = QByteArray("\x18\x10"); // Ctrl+X Ctrl+P
    bool m_updatingScrollBar = false;
};

} // namespace relay
