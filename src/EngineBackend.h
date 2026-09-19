// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::EngineBackend: relay::TerminalBackend over Relay's own terminal engine
// (engine/, docs/ENGINE.md). It is relay::VTermBackend plus Relay's look: the font
// and colours of data/theme/konsole (the same profile KonsolePart panes use) and
// Relay's terminal settings (copy on select).
//
// Selected per pane with --engine=relay, RELAY_ENGINE=relay, or the palette action
// "New pane (Relay engine)"; KonsolePart stays the default.
#pragma once

#include "backend/VTermBackend.h"

namespace relay {

class EngineBackend final : public VTermBackend {
    Q_OBJECT
public:
    // coreName: "ghostty", "libvterm", or empty for the engine's default core.
    explicit EngineBackend(const QString &coreName = QString(), QWidget *parent = nullptr);

    // Re-read Relay's terminal settings (copy on select) into the view.
    void applySettings();
    // Re-read the selected theme's colours into the view. Connected to the theme notifier, so a
    // theme switch recolours a running engine pane without a new pane (issue 0JA7).
    void applyThemeColors();

private:
    void applyRelayProfile();
};

} // namespace relay
