// SPDX-License-Identifier: AGPL-3.0-or-later
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
#include <QFont>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cmath>

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
// The Actions pane: its header band, its glyph and the title-bar button that opens it (owner,
// 2026-09-18, "make actions red-orange"). A fifth meaning hue between `Warning` and `Error`; a
// theme file that omits `[ui] action` gets one derived from its own `error` (src/Theme.cpp).
inline QColor Action{0xe5, 0x84, 0x4f};
// The brass a tool pane's header band wears (the Switchboard and its neighbours). Its own token
// since 2026-09-19: it used to be `Warning`, so amber said both "this pane is a tool" and "this is
// waiting on you", and only the second may ever be missed. A theme file that omits `[ui] tool`
// gets one dulled out of its own amber (src/ThemeFile.cpp, brassFrom).
inline QColor Tool{0xc8, 0xa4, 0x5c};
// "You can open this": a path, a folder, a URL or a card reference, wherever one is shown — the
// terminal grid (engine ColorScheme::link), the composer's path token, a fold's "open x.py" row,
// the agent's Markdown links (indexed: the theme's ANSI 2, which `[ui] link` defaults to) and
// QPalette::Link in the chrome. Owner, 2026-09-19: "clickable things need to be understood from
// colors", then "dark green, like Warp". As dark a green as 4.5:1 allows in each theme, and
// measured clear of the success green and the destination pair (tests/theme_test.cpp).
inline QColor Link{0x12, 0xa4, 0x57};
// The two input destinations: the mode chip, the caret and the prefix chips.
inline QColor Shell{0x3e, 0xc5, 0xf0};
inline QColor Agent{0xb4, 0x8e, 0xf7};

// --- the Switchboard's materials (docs/SWITCHBOARD-AESTHETIC.md 3.1-3.4) -------------------------
// The board is the one surface in Relay that is allowed to be a physical object: a face with
// hardware on it. `BoardFace` is that face — the ground of the Switchboard pane, and the ground a
// card row's text is measured against (tests/theme_test.cpp) — `BoardMetal` is a lit piece of
// hardware (the engraved rule under the section the pointer is on), and `BoardMetalDim` is the same
// hardware unlit (every other rule, and the jack rings of an empty board). Brass is structure,
// never state: it may never be read as the amber that means something is waiting on you.
//
// A theme that sets `[flags] board_material = false` gets the hairline form of every one of those
// (SWITCHBOARD-AESTHETIC 3.4) and `BoardMaterial` says so, but the substitution is made once, in
// adoptTokens(): the face becomes `Surface`, lit hardware the accent and unlit hardware `Border`.
// Painting code reads these three whatever the theme said, so nothing paints two ways.
inline QColor BoardFace{0x17, 0x14, 0x0f};
inline QColor BoardMetal{0xc8, 0xa4, 0x5c};
inline QColor BoardMetalDim{0x6b, 0x56, 0x37};
inline bool BoardMaterial = true;
// The card row's priority flag (owner, card #VKFV: "priority -1 is yellow. +1 is white, +2 is
// pale green, +3 is bright green"). Four tokens of the board's own material table, so a theme
// answers for their legibility itself (tests/theme_test.cpp holds them to 3:1 on the face, the
// non-text bar): the yellow is the theme's warning, the bright green its success, the pale green
// that success half stepped toward the text, and the "white" the theme's own text — near-white
// on every dark board the flag was designed on, and the theme's brightest legible ink on a light
// one, where a literal white dot would vanish into the face.
inline QColor BoardPriorityLow{0xe5, 0xc0, 0x7b};
inline QColor BoardPriorityOne{0xe6, 0xe8, 0xec};
inline QColor BoardPriorityTwo{0xb7, 0xda, 0xc1};
inline QColor BoardPriorityThree{0x7e, 0xc8, 0x8c};

// The composer's syntax colours (src/ShellHighlighter.cpp).
inline QColor SyntaxCommand{0x3e, 0xc5, 0xf0};
inline QColor SyntaxUnknown{0xf0, 0x71, 0x78};
inline QColor SyntaxFlag{0xe5, 0xc0, 0x7b};
inline QColor SyntaxString{0x7e, 0xc8, 0x8c};
inline QColor SyntaxPath{0x12, 0xa4, 0x57};   // = Link unless a theme says otherwise
inline QColor SyntaxOperator{0x80, 0x87, 0x96};
inline QColor SyntaxVariable{0xb4, 0x8e, 0xf7};
inline QColor SyntaxAgent{0xb4, 0x8e, 0xf7};
inline QColor SyntaxToken{0x3e, 0xc5, 0xf0};

// --- legible text (docs/ARCHITECTURE.md, "Legible text") ----------------------------------------
// Three sizes, in points, that every font in Relay stays at or above: the stylesheet's font-size
// rules and anything a widget paints with QPainter. tests/theme_test.cpp holds the text tokens to
// 4.5:1 on every surface; tests/buttonfit_test.cpp holds the stylesheet to these sizes.
//   BodyPt       the application font: prose, row titles, labels. applyTheme() raises a smaller
//                system default to it (Qt's generic default is 9pt; KDE's and GNOME's are 10–11).
//   SecondaryPt  what explains the line above it: a setting's detail, a notification's body, a
//                card's meta line, the composer strip's chips.
//   FloorPt      nothing smaller, anywhere: timestamps, key hints, counts, engraved headings.
inline constexpr qreal BodyPt = 10.0;
inline constexpr qreal SecondaryPt = 9.5;
inline constexpr qreal FloorPt = 9.0;
// `font` at no less than `atLeast` points (a pixel-sized font is converted at 96 dpi). Inline, so
// the small libraries that read the tokens without linking Theme.cpp can use it too.
inline QFont legible(const QFont &font, qreal atLeast = FloorPt) {
    QFont out(font);
    const qreal points = font.pointSizeF() > 0 ? font.pointSizeF() : font.pixelSize() * 0.75;
    if (points < atLeast) out.setPointSizeF(atLeast);
    return out;
}

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
// One of those at random, never any of `avoid` — pass the theme in use so a press always changes
// something; the "random theme on each new tab" mode passes the default and the previous draw too,
// so a run of new tabs repeats nothing it can avoid. Empty when every theme is in `avoid`.
QString randomThemeId(const QStringList &avoid = {});
// The theme in use. `activeThemeId()` is what QSettings stores under `theme/name`.
const ThemeSpec &active();
QString activeThemeId();
// The theme a profile that never chose one gets: Dark Copper, on every build.
QString defaultThemeId();
// Switch. Returns false when the id is unknown. Restyles the application, regenerates the
// terminal schemes and emits themeChanged(); no restart and, for Relay-engine panes, no new pane.
// `persist` = also store it as `theme/name`, the theme Relay opens on and a new tab starts with
// (Options › Appearance). A tab switching to its own theme passes false: it restyles, and the
// default stays what the owner chose.
bool setActiveTheme(const QString &id, bool persist = true);
// The stored default, resolved: what Relay opens on and what a new tab starts with.
QString startupThemeId();
// A theme's parsed file without making it the active one (the tab bar paints each tab's swatch
// from it). An unknown id gives the theme Relay would fall back to; compare `.id` to tell.
ThemeSpec specFor(const QString &id);

// Fusion style, the theme's QPalette, and the application stylesheet. Call after QApplication
// exists; reads the theme named by QSettings `theme/name`.
void applyTheme(QApplication &app);
// Older name, kept so existing call sites keep working.
void applyDarkTheme(QApplication &app);
// Unpolish/polish and repaint every top-level widget and its children.
void repolishAll();
// The ink that stays legible on a chip filled with `fill`: a very dark or very light tint of the
// fill itself (the @onShell / @onAgent stylesheet tokens are made with it). Painting code that
// fills a band in a channel colour — the line the user typed — uses the same rule.
QColor chipInk(const QColor &fill);

// Pure black or pure white — whichever reads on `fill` — for text that sits on one of the meaning
// colours used as a ground rather than an ink: a diff's add/remove fills. chipInk() tints the fill
// itself; this is the black/white a band of green or red asks for (owner, 2026-09-19: "black/white
// text with a green or red background"). Whichever of the two wins does so at 4.6:1 or better,
// because the crossover — WCAG relative luminance 0.179 — is the one fill where they score alike.
inline QColor contrastInk(const QColor &fill) {
    const auto channel = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    const double luma = 0.2126 * channel(fill.redF()) + 0.7152 * channel(fill.greenF())
                      + 0.0722 * channel(fill.blueF());
    return luma < 0.179 ? QColor(Qt::white) : QColor(Qt::black);
}

// Name-based hooks for widgets that the stylesheet targets by object name.
void polishWindow(QWidget *window);
// Directory containing themes/, icons/ and terminal.conf, or empty when not found.
// Safe to call before QApplication exists.
QString themeDataDir();
}
