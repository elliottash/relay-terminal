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
#include <QColor>
#include <QFont>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>
#include <functional>

class QWidget;

namespace relay {

// ---- folds (#TK9C): the detail of one agent tool call, unfolded in the grid
//
// Relay prints each tool call as one concise line wrapped in an OSC 8
// hyperlink; clicking it unfolds the call's detail *underneath that line,
// inside the terminal*. The host builds the detail as logical lines of spans
// -- one span per run of text with its own colours, so a coloured diff or a
// dim byte count is the host's to describe -- and the view wraps them to the
// grid and paints them in the terminal's own font. Declared here so a host
// only has to know this header.
struct FoldSpan {
    QString text;
    QColor fg;            // invalid = the terminal's default foreground
    QColor bg;            // invalid = the fold block's own background
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool dim = false;
    QString link;         // non-empty: clickable, reported through onLinkActivated
};

struct FoldLine {
    QVector<FoldSpan> spans;
    QString text() const; // the spans joined
};

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
        FontZoom = 1 << 10,        // zoom()
        LinkWalk = 1 << 11,        // stepLink(): keyboard walk over the links in the output
        LineDiscipline = 1 << 12,  // termiosFlags(): ICANON/ECHO of the terminal, cheaply
        Folds = 1 << 13,           // setFoldContent(): tool-call detail unfolded inside the grid
    };

    // ICANON/ECHO of the terminal the shell is reading. Together with
    // foregroundProcessId() this is everything the host needs to know whether the
    // shell is at a Readline prompt (raw, foreground == shell), running a
    // full-screen program, or asking for a password (cooked, echo off). Spelt out
    // here rather than taken from the PTY layer so this header stays engine-neutral.
    struct TermiosFlags {
        bool valid = false;     // false = the engine cannot say; ask the operating system instead
        bool canonical = false; // ICANON
        bool echo = false;      // ECHO
    };

    // A file, folder, URL or card reference found in the output (src/OutputLinks.*).
    struct Link {
        QString target;         // an absolute path, the URL as written, or relay://card/<id>
        QString text;           // the output text it was found as
        QString card;           // `#K7Q2`: the card id, upper-cased; empty for every other link
        QString cardTitle;      // what the host's board calls that card, when it knows a title
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
    // Needs the LineDiscipline capability. An engine that cannot ask its terminal
    // answers `valid = false`, and the host falls back to /proc/<pid>/fd/0.
    virtual TermiosFlags termiosFlags() const { return {}; }

    // ---- display
    // Bytes into the terminal emulator as if the program printed them; they
    // never reach the program (Relay's inline agent output).
    virtual void writeToDisplay(const QByteArray &bytes) = 0;
    // Ask the shell to redraw its prompt (sends the redraw sequence; Relay's
    // Bash integration binds Ctrl+X Ctrl+P, the default).
    virtual void redrawPrompt() = 0;
    virtual void setRedrawPromptSequence(const QByteArray &bytes) = 0;
    // While held, a resize changes the grid but not the program's window size: no SIGWINCH until
    // the hold is released, which applies the latest size. Relay holds it while its own output
    // owns the cursor line, because the shell's line editor answers SIGWINCH by clearing the
    // cursor's row ("\r\x1b[K"), which is a row of that output.
    virtual void holdProgramResize(bool hold) { (void)hold; }

    // ---- introspection
    virtual int capabilities() const = 0;
    virtual QString screenText() const = 0;                     // visible screen lines, '\n'-joined
    virtual QStringList scrollbackText(int maxLines) const = 0; // oldest first, newest last
    virtual bool altScreen() const = 0;
    virtual int rows() const = 0;
    virtual int columns() const = 0;
    virtual QString title() const = 0;
    virtual QString currentDirectory() const = 0; // OSC 7, else best effort
    // The cursor on the active screen: x is the column, y the row, both from 0; (-1, -1) when the
    // engine cannot say.
    virtual QPoint cursorPosition() const { return {-1, -1}; }

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
    // True when the view is showing the newest output rather than sitting back in the history. The
    // host asks before it resizes the terminal (the thinking and queue bubbles are rows of the
    // pane's column now, src/main.cpp) so it can put the view back at the bottom where it already
    // was. An engine that cannot say answers yes, which is what an unscrolled terminal answers.
    virtual bool viewportAtBottom() const { return true; }
    virtual bool scrollToPrompt(int direction) = 0;

    // ---- search
    virtual int find(const QString &text, bool backwards) = 0; // returns match count, selects a match

    // ---- right-click menu helpers (issue #X2F1)
    // The OSC 8 link, URL or existing path under a point in widget()'s coordinates; empty when
    // there is none, or when the engine cannot hit-test (KonsolePart). line/column are -1 when
    // the token carries none.
    virtual QString linkAt(const QPoint &pos, int *line = nullptr, int *column = nullptr) {
        Q_UNUSED(pos);
        if (line) *line = -1;
        if (column) *column = -1;
        return {};
    }
    // Font size: +1 larger, -1 smaller, 0 back to the profile's size. Returns false when the
    // engine cannot do it (the FontZoom capability is then absent too).
    virtual bool zoom(int step) {
        Q_UNUSED(step);
        return false;
    }
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
    // Which `#K7Q2` references in the output are real cards, and what they are called
    // (`relay::links::CardLookup`; spelt out here so this header stays QtCore-only). The host
    // answers from the pane's Switchboard index; until it does, and on engines that cannot
    // hit-test their output, card references stay plain text.
    virtual void setCardLookup(std::function<bool(const QString &id, QString *title)> lookup)
    {
        Q_UNUSED(lookup);
    }
    // Which paths in the output exist, and where a relative one is relative to (card #S5SH).
    // By default the engine asks this machine's filesystem, which is right until a pane is
    // logged into another one: under `ssh filly` the output names filly's files, and a path of
    // the same name here is a link for the wrong reason. A host that knows better answers
    // instead — `probe` returns -1 for nothing there, 0 for a file, 1 for a folder (the values
    // of `relay::links::Entry`, spelt out so this header stays QtCore-only), and `directory`
    // gives the folder relative paths resolve against, or an empty string for the pane's own.
    // Both are called from a mouse-move, so neither may block; an answer that is not known yet
    // is simply "nothing there", and the underline appears when the next scan finds it.
    virtual void setLinkProbe(std::function<int(const QString &absolutePath)> probe,
                              std::function<QString()> directory = {})
    {
        Q_UNUSED(probe);
        Q_UNUSED(directory);
    }
    // A batch of answers came back from the host: what is underlined was worked out with the
    // old ones, so the view reads its output again.
    virtual void linkProbeAnswered() {}

    // ---- folds (#TK9C); needs the Folds capability
    //
    // OSC 8 URIs starting with the prefix are fold anchors: a plain left click
    // (and Ctrl+click) on one toggles its block instead of opening the URI, and
    // an anchor with no content yet reaches the host through onFoldRequested.
    // The block is painted between the real rows, scrolls, selects, copies and
    // is searched as if it were output. See docs/ENGINE.md, "Folds".
    virtual void setFoldPrefix(const QString &uriPrefix) { Q_UNUSED(uriPrefix); }
    // Sets a fold's detail and opens it.
    virtual void setFoldContent(const QString &uri, const QVector<FoldLine> &lines)
    {
        Q_UNUSED(uri);
        Q_UNUSED(lines);
    }
    virtual void setFoldExpanded(const QString &uri, bool expanded)
    {
        Q_UNUSED(uri);
        Q_UNUSED(expanded);
    }
    virtual bool foldExpanded(const QString &uri) const
    {
        Q_UNUSED(uri);
        return false;
    }
    virtual void removeFold(const QString &uri) { Q_UNUSED(uri); }
    virtual void clearFolds() {}
    virtual QStringList expandedFolds() const { return {}; }
    // Open or shut a fold by URI; without content the host is asked for it.
    virtual bool toggleFold(const QString &uri)
    {
        Q_UNUSED(uri);
        return false;
    }

    // ---- host callbacks (GUI thread)
    // An anchor was clicked and the view has no detail for it: fetch it and
    // call setFoldContent(), which opens the block.
    std::function<void(const QString &uri)> onFoldRequested;
    // OSC 8 URI, URL text or an existing absolute path; line/column are -1 when absent.
    std::function<void(const QString &target, int line, int column)> onLinkActivated;
    std::function<void(const QString &title)> onTitleChanged;
    std::function<void(const QString &path)> onCwdChanged;
    // The same OSC 7 with its host part ("" for file:///path). A shell on another machine (ssh)
    // names that machine here; its path means nothing on this one (card #S5SH).
    std::function<void(const QString &path, const QString &host)> onCwdHostChanged;
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
