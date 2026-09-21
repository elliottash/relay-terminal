// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// relay::agent::Host -- what an AgentConsole is drawn on.
//
// The console (src/AgentConsole.h) is the prompt box and everything that belongs to it: the
// composer, the queue, the transcript, the folds, the model box, the worker. It never names the
// widget it lives in. Everything it needs from that widget is here, and there is one
// implementation per host: `Pane` (a terminal pane) today, the Switchboard, a card, Options and
// Sessions after card #AGNT's later steps.
//
// The interface is terminal-shaped on purpose (#AGNT, Risks 3): `writeTerminal`, `columns`,
// folds, `screenText`. Every context keeps a vterm as its transcript surface, which costs one
// emulator per console and buys the whole ANSI / fold / OSC 8 transcript, the theme repaint, the
// scrollback and the phone's screen stream for nothing. A context that one day wants a different
// surface -- a rich-text document, a canvas -- implements this interface instead of `Pane` and
// touches nothing else. Nobody should pretend it is surface-agnostic today.
//
// The calls are the thirty `m_backend` touches the agent block of src/Pane.h had before the
// extraction, grouped and named; `status` / `toast` / `hint` and `bubbleRoom` /
// `setBubbleHeight` are the pane's own, and the last two are the one place an embedded host
// answers differently (a 320 px panel has no room for a bubble the way a full pane does).

#include "InputPolicy.h"       // relay::input::TerminalMode: who is reading the pane's tty
#include "TerminalBackend.h"   // relay::FoldLine: what a fold's content is made of

#include <QByteArray>
#include <QPoint>
#include <QString>
#include <QVector>

class QWidget;

namespace relay::agent {

class Host {
public:
    virtual ~Host() = default;

    // ----- the transcript surface -------------------------------------------------------------
    // Bytes go to the emulator as if the program had printed them: nothing is typed into the
    // shell, so agent text never reaches shell history and is never executed.
    virtual void writeTerminal(const QByteArray &bytes) = 0;
    // The grid's width in cells, or 0 when it is not known ("do not cut").
    virtual int columns() const = 0;
    // Is the cursor at column 0 of a fresh row? The inline printer tracks this as it writes.
    virtual bool atLineStart() const = 0;

    // ----- folds (the engine's fold layer, OSC 8 anchors) --------------------------------------
    virtual bool terminalFolds() const = 0;          // does this surface have a fold layer at all
    virtual bool foldExpanded(const QString &uri) const = 0;
    virtual void setFoldExpanded(const QString &uri, bool expanded) = 0;
    virtual void setFoldContent(const QString &uri, const QVector<relay::FoldLine> &lines) = 0;
    virtual bool toggleFold(const QString &uri) = 0;

    // ----- the viewport ------------------------------------------------------------------------
    // A surface showing the newest output still shows it after a bubble resized it; a reader who
    // had scrolled back into the history is left where they were.
    virtual bool viewportAtBottom() const = 0;
    virtual void scrollToBottom() = 0;

    // ----- what is on the screen right now ------------------------------------------------------
    virtual QString screenText() const = 0;
    virtual QPoint cursorPosition() const = 0;

    // ----- the shell behind the surface, when there is one --------------------------------------
    // A context with `shell = false` answers 0 / Unknown and does nothing: the console asks
    // rather than assumes, which is what lets the same code run in a panel with no pty.
    virtual void sendText(const QString &text) = 0;
    virtual void paste() = 0;
    virtual int shellPid() const = 0;
    virtual int foregroundProcessId() const = 0;
    virtual relay::input::TerminalMode terminalMode() const = 0;

    // ----- telling the person something ----------------------------------------------------------
    virtual void status(const QString &text) = 0;
    virtual void toast(const QString &text, int milliseconds) = 0;
    // false when the hint was not shown (hints off, or this one has been shown its limit).
    virtual bool hint(const QString &id, const QString &text, int limit) = 0;

    // ----- how much room a bubble may take --------------------------------------------------------
    // Measured against the host's own height and its other rows, never against the transcript,
    // because none of those change as a bubble comes and goes -- so the answer cannot oscillate.
    virtual int bubbleRoom() const = 0;
    virtual void setBubbleHeight(QWidget *bubble, int height) = 0;
};

}  // namespace relay::agent
