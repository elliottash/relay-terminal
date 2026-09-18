// SPDX-License-Identifier: GPL-3.0-or-later
// Which terminal engine a pane uses.
//
// Relay has two implementations of relay::TerminalBackend (engine/TerminalBackend.h):
// KonsolePart (relay::KonsoleBackend, the default) and Relay's own engine
// (relay::EngineBackend over engine/). The choice is per pane, so both can run side
// by side in one window:
//
//   --engine=konsole|relay      command line, sets the default for new panes
//   RELAY_ENGINE=konsole|relay  environment, used when --engine is absent
//   --engine-core=ghostty|libvterm   which emulator core an engine pane uses
//   RELAY_ENGINE_CORE=...            same, from the environment
//   palette: "New pane (Relay engine)" / "New pane (Konsole engine)"
#pragma once

#include <QList>
#include <QString>

class QWidget;

namespace relay {

class TerminalBackend;

enum class EngineKind {
    Konsole, // KParts KonsolePart (default)
    Relay,   // Relay's own engine: engine/ (TerminalSession + TerminalView)
};

// "konsole" / "relay" — the spelling used by --engine, RELAY_ENGINE and session state.
QString engineKindName(EngineKind kind);
// Accepts the canonical names and a few aliases ("kpart"; "vterm", "engine", "own").
// Case-insensitive, surrounding whitespace ignored. Returns false on anything else.
bool parseEngineKind(const QString &text, EngineKind *out);

// The engine for new panes when neither the caller nor the pane state says otherwise.
// commandLine wins over environment; an unparseable value is ignored and reported in
// `warning` (left untouched when everything parsed).
EngineKind resolveEngineKind(const QString &commandLine, const QString &environment, QString *warning = nullptr);
// Same for the emulator core of engine panes; empty means "the engine's default".
QString resolveEngineCore(const QString &commandLine, const QString &environment);

// Process-wide default for new panes, set once from the command line at startup.
EngineKind defaultEngineKind();
void setDefaultEngineKind(EngineKind kind);
QString defaultEngineCore();
void setDefaultEngineCore(const QString &core);

// Whether Relay was built with its own engine linked in (RELAY_HAVE_ENGINE).
bool engineAvailable();

// Creates the backend for `kind`. Throws std::runtime_error when it cannot be
// created (no KonsolePart installed, or no engine in this build).
TerminalBackend *createTerminalBackend(EngineKind kind, const QString &core, QWidget *parent);

// ----- the terminal pane's right-click menu (issue #X2F1) ---------------------------------------
//
// Both engines show the same menu; an entry an engine cannot do is left out rather than shown
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
    bool selectionKnown = false;     // the engine can answer "is anything selected?"
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
