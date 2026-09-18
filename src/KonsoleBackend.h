// SPDX-License-Identifier: GPL-3.0-or-later
// relay::KonsoleBackend: relay::TerminalBackend over KonsolePart, Relay's default
// engine on Linux. Everything Relay used to do to KonsolePart directly lives here:
// the KParts plugin load, TerminalInterface, the Session D-Bus object (inline agent
// output and alternate-screen notifications), the hidden scrollbar and the
// TerminalDisplay clipboard slots.
//
// See docs/ENGINE.md ("Integration plan") and engine/TerminalBackend.h.
#pragma once

#include "TerminalBackend.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

class QScrollBar;
class TerminalInterface;

namespace KParts {
class ReadOnlyPart;
}

namespace relay {

class KonsoleBackend : public QObject, public TerminalBackend {
    Q_OBJECT
public:
    // Loads the KonsolePart plugin; throws std::runtime_error when it is missing.
    explicit KonsoleBackend(QObject *parent = nullptr);
    ~KonsoleBackend() override;

    // Colour themes (issue 0JA7): hand this session the Konsole profile generated for the
    // selected theme. Connected to the theme notifier. KonsolePart offers no API for this, so it
    // goes through the Session object's scriptable setProfile(); where that is missing the new
    // colours only reach new panes.
    void applyTheme();

    bool startProgram(const QString &program, const QStringList &args, const QString &workingDirectory,
                      const QStringList &extraEnvironment = {}) override;
    void sendInput(const QByteArray &bytes) override;
    void sendText(const QString &text, bool asPaste) override;
    qint64 shellPid() const override;
    qint64 foregroundProcessId() const override;
    bool isRunning() const override;

    void writeToDisplay(const QByteArray &bytes) override;
    void redrawPrompt() override;
    void setRedrawPromptSequence(const QByteArray &bytes) override { m_redrawSequence = bytes; }

    int capabilities() const override;
    QString screenText() const override;
    QStringList scrollbackText(int maxLines) const override;
    bool altScreen() const override { return m_altScreen; }
    int rows() const override;
    int columns() const override;
    QString title() const override;
    QString currentDirectory() const override;

    void resizeTerminal(int rows, int columns) override;
    QWidget *widget() override { return m_widget; }
    QWidget *focusWidget() override { return m_widget; }
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
    bool scrollToPrompt(int) override { return false; }

    int find(const QString &, bool) override { return 0; }

    void setOutputCallbackEnabled(bool) override {}

public Q_SLOTS:
    // Konsole's Session signal, connected by name (see connectSession()).
    void primaryScreenInUse(bool primary);

private:
    // Konsole's Session is not reachable through KParts, but every session registers
    // itself on D-Bus; in-process objectRegisteredAt() hands back the QObject.
    QObject *konsoleSession();
    void connectSession();
    QObject *display() const; // the TerminalDisplay child with copyToClipboard()
    QScrollBar *scrollBar() const;
    void invokeOnDisplay(const char *slot);

    QPointer<KParts::ReadOnlyPart> m_part;
    TerminalInterface *m_iface = nullptr;
    QWidget *m_widget = nullptr;
    QPointer<QObject> m_session, m_watchedSession;
    QTimer m_sessionProbe;
    QByteArray m_redrawSequence{"\x18\x10"}; // Ctrl+X Ctrl+P, Relay's Bash binding
    bool m_altScreen = false;
    bool m_destroying = false;
};

} // namespace relay
