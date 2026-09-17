// SPDX-License-Identifier: GPL-3.0-or-later
// Engine-neutral terminal interface for src/main.cpp.
//
// Implementations:
// - relay::VTermBackend (engine/backend): Relay's own engine (libghostty-vt or
//   libvterm core, QPainter view). Reports every capability.
// - a KonsolePart adapter (to be written in src/, see docs/ENGINE.md): wraps
//   KParts::ReadOnlyPart + TerminalInterface + the Session D-Bus object and
//   reports fewer capabilities.
//
// All methods are called on the GUI thread; all callbacks run on the GUI thread.
#pragma once

#include <QByteArray>
#include <QFont>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <functional>

class QWidget;

namespace relay {

class TerminalBackend {
public:
    enum Capability {
        ScreenText = 1 << 0,       // visible lines
        Scrollback = 1 << 1,       // lines above the viewport
        AltScreenState = 1 << 2,   // knows whether a full-screen app is active (+ change callback)
        LinkClicks = 1 << 3,       // host gets link/path clicks before anything opens
        Osc8Links = 1 << 4,        // explicit OSC 8 hyperlinks
        PromptMarks = 1 << 5,      // OSC 133 A/B/C/D callbacks and prompt jumps
        CwdTracking = 1 << 6,      // OSC 7 working-directory callback
        DisplayInjection = 1 << 7, // writeToDisplay()
        Search = 1 << 8,           // find()
        ScrollControl = 1 << 9,    // scrollLines/Pages/ToBottom
        LinkWalk = 1 << 10,        // stepLink(): keyboard walk over the links in the output
    };

    // A file, folder or URL found in the output (src/OutputLinks.*).
    struct Link {
        QString target;         // an absolute path, or the URL as written
        QString text;           // the output text it was found as
        bool url = false;       // open in a browser rather than a Relay pane
        bool directory = false; // a folder: the explorer pane, not the preview
        int line = -1;
        int column = -1;
    };

    virtual ~TerminalBackend() = default;

    // ---- process
    virtual bool startProgram(const QString &program, const QStringList &args, const QString &workingDirectory,
                              const QStringList &extraEnvironment = {}) = 0;
    virtual void sendInput(const QByteArray &bytes) = 0;          // raw bytes to the PTY
    virtual void sendText(const QString &text, bool asPaste) = 0; // asPaste honours bracketed paste
    virtual qint64 shellPid() const = 0;
    virtual qint64 foregroundProcessId() const = 0;
    virtual bool isRunning() const = 0;

    // ---- display
    // Bytes into the terminal emulator as if the program printed them; they
    // never reach the program (Relay's inline agent output).
    virtual void writeToDisplay(const QByteArray &bytes) = 0;
    // Ask the shell to redraw its prompt (sends the redraw sequence; Relay's
    // Bash integration binds Ctrl+X Ctrl+P, the default).
    virtual void redrawPrompt() = 0;
    virtual void setRedrawPromptSequence(const QByteArray &bytes) = 0;

    // ---- introspection
    virtual int capabilities() const = 0;
    virtual QString screenText() const = 0;                     // visible screen lines, '\n'-joined
    virtual QStringList scrollbackText(int maxLines) const = 0; // oldest first, newest last
    virtual bool altScreen() const = 0;
    virtual int rows() const = 0;
    virtual int columns() const = 0;
    virtual QString title() const = 0;
    virtual QString currentDirectory() const = 0; // OSC 7, else best effort

    // ---- geometry, focus, appearance
    virtual void resizeTerminal(int rows, int columns) = 0; // resizes the widget to fit the grid
    virtual QWidget *widget() = 0;                          // put this in a layout
    virtual QWidget *focusWidget() = 0;                     // give this keyboard focus
    virtual void setTerminalFont(const QFont &font) = 0;

    // ---- clipboard, selection, clearing
    virtual void copySelection() = 0;
    virtual void paste() = 0;
    virtual QString selectedText() const = 0;
    virtual void selectAll() = 0;
    virtual void clearScrollback() = 0;
    // Clear scrollback and screen (home the cursor). Call redrawPrompt() after
    // it when the shell is idle.
    virtual void clear() = 0;

    // ---- scrolling (e.g. PageUp/PageDown from a composer)
    virtual void scrollLines(int lines) = 0; // negative = back into history
    virtual void scrollPages(int pages) = 0;
    virtual void scrollToBottom() = 0;
    virtual bool scrollToPrompt(int direction) = 0;

    // ---- search
    virtual int find(const QString &text, bool backwards) = 0; // returns match count, selects a match

    // ---- links in the output (issues YZTK and GWXM); needs the LinkWalk capability
    // Highlight the previous (-1) / next (+1) link in the screen and the scrollback, or
    // re-read the current one (0), scrolling it into view. False when there is none.
    virtual bool stepLink(int delta, Link *link, int *index, int *count)
    {
        Q_UNUSED(delta); Q_UNUSED(link); Q_UNUSED(index); Q_UNUSED(count);
        return false;
    }
    virtual void endLinkWalk() {}
    virtual bool linkWalkActive() const { return false; }
    // A plain left click on a link opens it. Hosts that use the first click of an inactive
    // pane to move the focus disarm it until the pane is active; Ctrl+click always opens.
    virtual void setPlainClickOpensLinks(bool on) { Q_UNUSED(on); }

    // ---- host callbacks (GUI thread)
    // OSC 8 URI, URL text or an existing absolute path; line/column are -1 when absent.
    std::function<void(const QString &target, int line, int column)> onLinkActivated;
    std::function<void(const QString &title)> onTitleChanged;
    std::function<void(const QString &path)> onCwdChanged;
    std::function<void(bool active)> onAltScreenChanged;
    std::function<void()> onBell;
    // kind: 'A' prompt start, 'B' command start, 'C' output start, 'D' finished (exitCode, -1 if unknown)
    std::function<void(char kind, int exitCode)> onPromptMark;
    // Raw PTY output in batches (enable with setOutputCallbackEnabled; costs a copy).
    std::function<void(const QByteArray &bytes)> onOutput;
    std::function<void(int exitCode)> onFinished;
    virtual void setOutputCallbackEnabled(bool enabled) = 0;
};

} // namespace relay
