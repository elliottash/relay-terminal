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

} // namespace relay
