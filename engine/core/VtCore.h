// SPDX-License-Identifier: GPL-3.0-or-later
// relay::VtCore: the swappable emulator core behind Relay's terminal engine.
//
// Everything the session, the view and the tests need from a VT emulator goes
// through this interface, so the parser/screen implementation can change
// without touching the PTY, the QPainter view or TerminalBackend. Two cores
// exist: "ghostty" (libghostty-vt, MIT, the default when built in) and
// "libvterm" (vendored, patched libvterm 0.3.3, MIT, always built; fallback).
//
// Threading: a VtCore is not thread-safe. The owner (TerminalSession) guards
// every call with one mutex. Events are invoked synchronously from inside
// feed() and the input calls, on the calling thread, with that mutex held;
// handlers must not call back into the core.
//
// Coordinates: "viewport" rows are what the view shows (0 = top visible row).
// The viewport normally follows the active screen; when scrolled back it stays
// pinned to the same content while output continues (both cores).
#pragma once

#include "CellTypes.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace relay {

class VtCore {
public:
    struct Events {
        // Bytes the terminal answers or encodes (DA/DSR replies, keys, mouse,
        // bracketed paste). The owner writes them to the PTY.
        std::function<void(const char *data, size_t len)> reply;
        std::function<void(const QString &title)> titleChanged;
        std::function<void(const QString &path, const QString &host)> cwdChanged; // OSC 7
        std::function<void()> bell;
        std::function<void(bool active)> altScreenChanged;
        // OSC 133 A/B/C/D, reported after the core processed the sequence.
        // `row` is the active-screen row of the cursor; exitCode is only
        // meaningful for MarkCommandFinished (-1 if absent).
        std::function<void(PromptMark kind, int row, int exitCode)> promptMark;
        // OSC 52 write (decoded). Only called when setClipboardWriteAllowed(true).
        std::function<void(const QString &target, const QByteArray &data)> clipboardWrite;
        // OSC 9 / OSC 777;notify
        std::function<void(const QString &title, const QString &body)> notification;
    };

    virtual ~VtCore() = default;

    Events events;

    virtual const char *name() const = 0;

    // ---- stream
    virtual void feed(const char *data, size_t len) = 0;
    virtual void resize(int rows, int cols, int cellWidthPx, int cellHeightPx) = 0;
    virtual int rows() const = 0;
    virtual int columns() const = 0;
    virtual void setScrollbackLines(int lines) = 0;
    // True when the parser is between sequences (no partial ESC/CSI/OSC/UTF-8),
    // so host bytes (writeToDisplay) can be fed without corrupting program output.
    virtual bool atGround() const = 0;

    // ---- rendering
    // Copy the viewport into `frame` (only dirty rows unless frame->full is
    // needed) and consume the dirty state. Returns false if nothing changed
    // since the previous call and `force` is false.
    virtual bool updateFrame(ViewportFrame *frame, bool force) = 0;

    // ---- introspection (host / agent)
    virtual QString screenText() const = 0;                 // active screen, '\n'-joined, trailing blanks trimmed
    virtual QStringList historyText(int maxLines) const = 0; // scrollback above the active screen, oldest first
    virtual bool altScreen() const = 0;
    virtual MouseTracking mouseTracking() const = 0;
    virtual bool mouseSgrPixels() const { return false; }
    virtual bool bracketedPaste() const = 0;
    virtual QString title() const = 0;
    virtual CursorState activeCursor() const = 0; // cursor in active-screen coordinates

    // ---- viewport scrolling
    virtual int historyRows() const = 0;
    virtual int viewportTop() const = 0; // 0 = oldest scrollback line
    virtual bool viewportAtBottom() const = 0;
    virtual void scrollViewport(int deltaRows) = 0; // negative = towards history
    virtual void scrollViewportToTop() = 0;
    virtual void scrollViewportToBottom() = 0;
    virtual void scrollViewportToRow(int row) = 0; // row from the top of the scrollback
    // Move the viewport to the previous (direction < 0) or next prompt
    // (OSC 133). Returns false if there is none.
    virtual bool scrollToPrompt(int direction) = 0;

    // ---- hit testing (viewport coordinates)
    virtual QString hyperlinkAt(int row, int col) const = 0; // OSC 8 URI or empty

    // ---- selection (viewport coordinates; the core keeps it attached to content)
    virtual void selectionBegin(int row, int col, SelectionUnit unit, bool rectangle) = 0;
    virtual void selectionExtend(int row, int col) = 0;
    virtual void selectionClear() = 0;
    virtual bool hasSelection() const = 0;
    virtual QString selectedText() const = 0;
    virtual void selectAll() = 0;

    // ---- search (scrollback + screen)
    // Sets the needle (empty clears) and returns the number of matches.
    virtual int searchSet(const QString &needle) = 0;
    // Selects the next match towards older content (backwards = true, i.e. up
    // into history) or newer content, wrapping, and scrolls it into view.
    // Returns the index of the selected match counted from the newest (0), or -1.
    // Matching is case-insensitive for ASCII letters.
    virtual int searchStep(bool backwards) = 0;
    virtual int searchMatchCount() const = 0;

    // ---- input (results arrive through events.reply)
    virtual void sendKey(const KeyInput &key) = 0;
    virtual void sendText(const QString &text) = 0; // committed text (IME, drag and drop), not a paste
    virtual void sendMouse(const MouseInput &mouse) = 0;
    virtual void paste(const QString &text) = 0;    // honours bracketed paste, strips unsafe control codes
    virtual void focusChanged(bool focused) = 0;
    virtual void setCellPixelSize(int w, int h, int widthPx, int heightPx) = 0;

    // ---- maintenance
    virtual void clearScrollback() = 0;
    virtual void reset() = 0;
    // Default colours and 16-colour palette (0xRRGGBB). Used for OSC 10/11/4
    // queries and resolving indexed colours; the view keeps its own copy.
    virtual void setColors(uint32_t fg, uint32_t bg, const uint32_t *palette16) = 0;
    virtual void setClipboardWriteAllowed(bool allowed) = 0;
};

// Known cores: "ghostty" (when built with RELAY_ENGINE_WITH_GHOSTTY) and
// "libvterm" (always). An empty name picks the default (first available).
// Returns nullptr for an unknown or unavailable core.
std::unique_ptr<VtCore> createVtCore(const QString &name, int rows, int cols);
QStringList availableVtCores();

} // namespace relay
