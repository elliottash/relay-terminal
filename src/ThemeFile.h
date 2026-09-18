// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Colour themes (issue 0JA7): one file per theme, in TOML, holding both the 16-colour ANSI
// terminal palette and the UI tokens the stylesheet is built from.
//
// Built-in themes live in `data/theme/themes/`, the user's own in `~/.config/relay/themes/`;
// a user file wins over a built-in of the same id. The engine takes its colours straight from
// the theme, so a theme file is the only source of truth for colour.
//
// Everything here is plain QtGui so the rules can be tested without a window; src/Theme.cpp
// turns a ThemeSpec into the live QPalette, stylesheet and terminal colours.
//
// Format, with every key optional except [theme].name and the [ui] tokens:
//
//   [theme]
//   name = "Relay Dark"
//   variant = "dark"            # dark | light - hints for anything that must pick a direction
//   description = "..."
//
//   [ui]                        # the stylesheet tokens (see uiTokenNames())
//   background = "#0f1115"
//   ...
//
//   [syntax]                    # the composer's shell/agent colours (see syntaxTokenNames())
//   command = "#3ec5f0"
//   ...
//
//   [terminal]
//   background = "#0f1115"
//   foreground = "#d8dce3"
//   cursor = "#f0f2f6"
//   palette = ["#1d2027", ...]  # 16 entries: the 8 ANSI colours then the 8 bright ones
//
//   [flags]                     # per-theme booleans; unknown ones are kept verbatim
//   some_future_switch = false
//
// Unknown tables, keys and flags are preserved in ThemeSpec::extra/flags rather than rejected,
// so a later theme can carry extra tokens (for example the material tokens proposed in
// docs/SWITCHBOARD-AESTHETIC.md) without a format change or a reader change.
#include <QColor>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace relay::theme {

// A theme as it was read from disk. Colours are already parsed; anything the reader did not
// recognise stays in `extra` as "table.key" -> raw values.
struct ThemeSpec {
    QString id;            // file stem, e.g. "relay-dark"; the stable id stored in QSettings
    QString name;          // "Relay Dark"
    QString variant;       // "dark" or "light"; anything else is treated as "dark"
    QString description;
    QString path;          // the file it came from, empty for the compiled-in fallback
    bool builtin = true;   // false when it came from the user's themes folder

    QMap<QString, QColor> ui;      // uiTokenNames()
    QMap<QString, QColor> syntax;  // syntaxTokenNames()

    QColor terminalBackground, terminalForeground, terminalCursor;
    // Optional: the colour the terminal ground fades to at the bottom. Invalid = flat.
    QColor terminalBackgroundEnd;
    QColor terminalBackgroundIntense, terminalForegroundIntense;
    QVector<QColor> ansi;          // exactly 16 entries once complete

    QMap<QString, bool> flags;             // [flags]
    QMap<QString, QStringList> extra;      // everything else, "table.key" -> values

    bool isLight() const { return variant == QLatin1String("light"); }
    QColor uiColor(const QString &token, const QColor &fallback = QColor()) const;
    QColor syntaxColor(const QString &token, const QColor &fallback = QColor()) const;
    // Per-theme booleans, for switches a later theme may want (`board.material`, ...).
    bool flag(const QString &name, bool fallback = false) const;
};

// The token names src/Theme.cpp substitutes into the stylesheet. A theme must define all of them.
QStringList uiTokenNames();
// The composer's syntax colours (src/ShellHighlighter.cpp).
QStringList syntaxTokenNames();

// --- reading ------------------------------------------------------------------------------------

// A deliberately small TOML reader: comments, [table] headers, bare and quoted keys, string,
// boolean and integer values, and arrays of those (on one line or several). Returns a flat map of
// "table.key" -> values; a scalar is a one-element list. On a syntax error it returns what it read
// so far and sets *error.
QMap<QString, QStringList> parseToml(const QString &text, QString *error = nullptr);

// Parse a theme file. `id` is the file stem. Missing tokens are reported through *error and taken
// from `fallback`, so a slightly wrong user theme still renders. One exception: `[ui] action`, the
// Actions pane's red-orange, is turned out of the theme's own `error` at that red's luminance
// rather than inherited, because another theme's orange need not be legible on this one's ground.
ThemeSpec parseTheme(const QString &text, const QString &id, const ThemeSpec &fallback, QString *error = nullptr);

// True when every required token and all 16 ANSI colours are present.
bool isComplete(const ThemeSpec &spec, QStringList *missing = nullptr);

// Relay's dark theme, compiled in. Used when no theme file can be found at all, and as the
// fallback for tokens a theme file leaves out.
const ThemeSpec &builtinDark();

// --- discovery ----------------------------------------------------------------------------------

// Where themes are looked for, most specific first: the user's folder then the installed data
// folder. `configHome` is $XDG_CONFIG_HOME (or ~/.config); `dataDir` is themeDataDir().
QStringList themeSearchDirs(const QString &dataDir, const QString &configHome);

// Every *.toml in `dirs`, keyed by file stem. Earlier directories win, so a user file of the same
// id shadows the built-in. The result is sorted by id (QMap).
QMap<QString, QString> discoverThemeFiles(const QStringList &dirs);

}  // namespace relay::theme
