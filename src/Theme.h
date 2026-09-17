// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QColor>
#include <QString>

class QApplication;
class QWidget;

namespace relay::theme {
// Design tokens for Relay's Warp-inspired dark theme.
inline const QColor Background{0x0f, 0x11, 0x15};
inline const QColor Surface{0x16, 0x18, 0x1d};
inline const QColor SurfaceRaised{0x1c, 0x1f, 0x26};
inline const QColor Border{0x2a, 0x2e, 0x37};
inline const QColor Text{0xe6, 0xe8, 0xec};
inline const QColor TextMuted{0x8b, 0x91, 0x9c};
inline const QColor Accent{0x3e, 0xc5, 0xf0};
inline const QColor AccentText{0x06, 0x1a, 0x22};

// Fusion style, dark palette, and application stylesheet. Call after QApplication exists.
void applyDarkTheme(QApplication &app);
// Name-based hooks for widgets that the stylesheet targets by object name.
void polishWindow(QWidget *window);
// Directory containing relayrc and konsole/ data, or empty when not found.
// Safe to call before QApplication exists.
QString themeDataDir();
// Prepend the theme directory to XDG_CONFIG_DIRS and XDG_DATA_DIRS so KonsolePart
// finds Relay's default profile. Call before QApplication. Returns false if not found.
bool exposeKonsoleProfile();
// Restore the original XDG variables so the user's shell does not inherit Relay's paths.
void restoreXdgEnvironment();
}
