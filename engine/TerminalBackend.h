// SPDX-License-Identifier: GPL-3.0-or-later
// Engine-neutral terminal interface. Intended to be implemented by both a
// KonsolePart adapter (KParts::ReadOnlyPart + TerminalInterface + the Session
// D-Bus calls) and the Relay-owned libvterm engine, so src/main.cpp can talk to
// either. Not wired into the app yet (spike).
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <functional>

class QWidget;

namespace relay {

class TerminalBackend {
public:
    enum Capability {
        ScreenText = 1 << 0,     // visible lines
        Scrollback = 1 << 1,     // lines above the viewport
        AltScreenState = 1 << 2, // knows whether a full-screen app is active
        LinkClicks = 1 << 3,     // host gets link/path clicks before anything opens
        Osc8Links = 1 << 4,      // explicit OSC 8 hyperlinks
    };

    virtual ~TerminalBackend() = default;

    // Process
    virtual bool startProgram(const QString &program, const QStringList &args,
                              const QString &workingDirectory,
                              const QStringList &extraEnvironment = {}) = 0;
    virtual void sendInput(const QByteArray &bytes) = 0;          // raw bytes to the PTY
    virtual void sendText(const QString &text, bool asPaste) = 0; // asPaste honours bracketed paste
    virtual qint64 shellPid() const = 0;
    virtual qint64 foregroundProcessId() const = 0;

    // Introspection
    virtual int capabilities() const = 0;
    virtual QString screenText() const = 0;                     // visible lines, '\n'-joined
    virtual QStringList scrollbackText(int maxLines) const = 0; // oldest first, newest last
    virtual bool altScreen() const = 0;
    virtual int rows() const = 0;
    virtual int columns() const = 0;

    // Geometry / focus
    virtual void resizeTerminal(int rows, int columns) = 0;
    virtual QWidget *widget() = 0;      // put this in a layout
    virtual QWidget *focusWidget() = 0; // give this keyboard focus

    // Host callbacks
    std::function<void(const QString &uri)> onLinkActivated;          // OSC 8 or URL text
    std::function<void(const QString &absolutePath)> onPathActivated; // Ctrl+click on an existing file
    std::function<void(const QString &title)> onTitleChanged;
    std::function<void(int exitCode)> onFinished;
};

} // namespace relay
