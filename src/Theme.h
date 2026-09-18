// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Relay's live design tokens, the application stylesheet built from them, and the engine
// colour schemes generated from the same theme file.
//
// The tokens below are **variables, not constants**: picking another theme in Settings assigns
// new values and re-polishes every window, so painting code that reads them at paint time
// (ChromeButton, SubagentsPanel, the turn transcript) follows along without a restart. Anything
// that caches a colour — a per-widget stylesheet, a QPalette copied onto a label — has to be
// rebuilt from notifier()'s themeChanged() signal instead, or moved into the global stylesheet.
//
// The theme files themselves are read by src/ThemeFile.{h,cpp} (`relay-theme`, tested in
// tests/theme_test.cpp). See issue 0JA7 and docs/ARCHITECTURE.md section 14.
#include "ThemeFile.h"

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>

class QApplication;
class QWidget;

namespace relay::theme {
// --- live tokens --------------------------------------------------------------------------------
// Chrome. Their initial values are the compiled-in dark theme; setActiveTheme() replaces them.
inline QColor Background{0x0f, 0x11, 0x15};
inline QColor Surface{0x16, 0x18, 0x1d};
inline QColor SurfaceRaised{0x1c, 0x1f, 0x26};
inline QColor Border{0x2a, 0x2e, 0x37};
// The focused pane's outline. Grey on purpose: the accent means "shell" in Relay's
// visual language (the composer, prompt chips), so panes must not compete with it.
inline QColor BorderStrong{0x4a, 0x52, 0x60};
inline QColor Text{0xe6, 0xe8, 0xec};
inline QColor TextMuted{0x8b, 0x91, 0x9c};
inline QColor Accent{0x3e, 0xc5, 0xf0};
inline QColor AccentText{0x06, 0x1a, 0x22};
// State colours shared by the stylesheet and by painting code.
inline QColor Success{0x7e, 0xc8, 0x8c};
inline QColor Warning{0xe5, 0xc0, 0x7b};
inline QColor Error{0xe0, 0x6c, 0x75};
// The two input destinations: the mode chip, the caret and the prefix chips.
inline QColor Shell{0x3e, 0xc5, 0xf0};
inline QColor Agent{0xb4, 0x8e, 0xf7};

// The composer's syntax colours (src/ShellHighlighter.cpp).
inline QColor SyntaxCommand{0x3e, 0xc5, 0xf0};
inline QColor SyntaxUnknown{0xf0, 0x71, 0x78};
inline QColor SyntaxFlag{0xe5, 0xc0, 0x7b};
inline QColor SyntaxString{0x7e, 0xc8, 0x8c};
inline QColor SyntaxPath{0x66, 0xd0, 0xc0};
inline QColor SyntaxOperator{0x80, 0x87, 0x96};
inline QColor SyntaxVariable{0xb4, 0x8e, 0xf7};
inline QColor SyntaxAgent{0xb4, 0x8e, 0xf7};
inline QColor SyntaxToken{0x3e, 0xc5, 0xf0};

// --- switching ----------------------------------------------------------------------------------
// Emits themeChanged() after the tokens, the palette, the stylesheet and the generated terminal
// schemes are all in place, so a slot may read the new values straight away.
class Notifier final : public QObject {
    Q_OBJECT
public:
    void emitChanged() { Q_EMIT themeChanged(); }
Q_SIGNALS:
    void themeChanged();
};
Notifier *notifier();

struct ThemeChoice {
    QString id, name, description;
    bool builtin = true;
};
// Built-in themes plus anything in ~/.config/relay/themes, by id. Read from disk on the first
// call and after refreshThemes().
QList<ThemeChoice> availableThemes();
void refreshThemes();
// The theme in use. `activeThemeId()` is what QSettings stores under `theme/name`.
const ThemeSpec &active();
QString activeThemeId();
// Switch. Returns false when the id is unknown. Restyles the application, regenerates the
// terminal schemes and emits themeChanged(); no restart and, for Relay-engine panes, no new pane.
bool setActiveTheme(const QString &id);

// Fusion style, the theme's QPalette, and the application stylesheet. Call after QApplication
// exists; reads the theme named by QSettings `theme/name`.
void applyTheme(QApplication &app);
// Older name, kept so existing call sites keep working.
void applyDarkTheme(QApplication &app);
// Unpolish/polish and repaint every top-level widget and its children.
void repolishAll();

// Name-based hooks for widgets that the stylesheet targets by object name.
void polishWindow(QWidget *window);
// Directory containing themes/, icons/ and terminal.conf, or empty when not found.
// Safe to call before QApplication exists.
QString themeDataDir();
}
