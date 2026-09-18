// SPDX-License-Identifier: GPL-3.0-or-later
// Which emulator core a terminal pane uses.
//
// Every pane runs Relay's own engine (relay::EngineBackend over engine/). KonsolePart was the
// other implementation until 2026-09-18, when the owner retired it: the engine had been the
// default long enough to be the only one. What is left to choose is the core underneath it:
//
//   --engine-core=ghostty|libvterm   which emulator core a pane uses
//   RELAY_ENGINE_CORE=...            same, from the environment
#pragma once

#include <QList>
#include <QString>

class QWidget;

namespace relay {

class TerminalBackend;

// The emulator core for new panes; empty means "the engine's default".
// commandLine wins over environment; anything else is ignored.
QString resolveEngineCore(const QString &commandLine, const QString &environment);

// Process-wide default for new panes, set once from the command line at startup.
QString defaultEngineCore();
void setDefaultEngineCore(const QString &core);

// Creates a terminal backend. Throws std::runtime_error when this build has no engine
// (RELAY_HAVE_ENGINE), which is only the case for the headless test builds.
TerminalBackend *createTerminalBackend(const QString &core, QWidget *parent);

// ----- the terminal pane's right-click menu (issue #X2F1) ---------------------------------------
//
// An entry the engine cannot do is left out rather than shown
// dead, except where it is only unavailable right now (nothing selected, no link under the
// pointer), when it is greyed. Described as data so the list can be checked without a window
// or a shell (tests/backends_test.cpp).

struct TerminalMenuItem {
    QString id;        // "copy", "paste", …; "-" is a separator
    QString label;
    bool enabled = true;
    bool isSeparator() const { return id == QLatin1String("-"); }
};

// What the pane can offer right now. The capability flags come from
// relay::TerminalBackend::capabilities(); the rest is pane state.
struct TerminalMenuState {
    bool hasSelection = false;       // something is selected (engines that can say so)
    bool selectionKnown = true;      // the engine can answer "is anything selected?"
    bool canSearch = false;          // TerminalBackend::Search
    bool canReadOutput = false;      // ScreenText or Scrollback: "Save output as…" has something
    bool canInject = false;          // DisplayInjection: clear-and-reset can be written
    bool canZoom = false;            // the engine can change its font size
    QString link;                    // the URL under the pointer, empty when there is none
    QString filePath;                // an existing path under the pointer, empty when there is none
    bool hasTurn = false;            // the pane has a finished agent turn to open
    bool canTakeControl = false;     // the terminal is running, so the keyboard can be handed over
    bool canClosePane = true;
};

// The entries for one right-click, in order, with separators as items whose id is "-". Never
// starts or ends with a separator and never has two in a row.
QList<TerminalMenuItem> terminalContextMenu(const TerminalMenuState &state);

} // namespace relay
