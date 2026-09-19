// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EngineBackend.h"

#include "Theme.h"
#include "ThemeFile.h"
#include "view/TerminalView.h"

#include <QFont>
#include <QSettings>

namespace relay {

EngineBackend::EngineBackend(const QString &coreName, QWidget *parent)
    : VTermBackend(coreName, parent)
{
    applyRelayProfile();
    applySettings();
    // A theme switch repaints running engine panes; no new pane is needed for this engine.
    connect(theme::notifier(), &theme::Notifier::themeChanged, this, [this] { applyThemeColors(); });
    if (TerminalView *v = view())
        v->setScrollToBottomOnKeystroke(true);
}

void EngineBackend::applySettings()
{
    if (TerminalView *v = view())
        v->setCopyOnSelect(QSettings().value(QStringLiteral("terminal/copy_on_select"), false).toBool());
    // Paths and URLs in program output wear the link colour at rest, not only under the pointer
    // (owner, 2026-09-19). On by default; Options › Terminal turns it off.
    if (TerminalView *v = view())
        v->setLinksColouredAtRest(QSettings().value(QStringLiteral("terminal/colour_links"), true).toBool());
    applyThemeColors();   // the prompt band follows the same option
}

// Font, spacing and cursor defaults come from data/theme/terminal.conf; colour comes from the
// theme below. (The file was Relay's Konsole profile until KonsolePart was retired, which is why
// its keys are still grouped the way Konsole grouped them.)
void EngineBackend::applyRelayProfile()
{
    TerminalView *v = view();
    if (!v)
        return;
    const QString dir = theme::themeDataDir();
    if (dir.isEmpty())
        return;

    const QSettings profile(dir + QStringLiteral("/terminal.conf"), QSettings::IniFormat);
    const QString fontSpec = profile.value(QStringLiteral("Appearance/Font")).toString();
    QFont font;
    if (!fontSpec.isEmpty() && font.fromString(fontSpec))
        v->setTerminalFont(font);
    v->setLineSpacing(profile.value(QStringLiteral("Appearance/LineSpacing"), 0).toInt());
    v->setPadding(profile.value(QStringLiteral("Appearance/TerminalMargin"), 2).toInt());
    v->setUnfocusedCursorVisible(profile.value(QStringLiteral("Cursor Options/ShowUnfocusedCursor"), true).toBool());

    applyThemeColors();
}

// Colour comes from the selected theme (issue 0JA7) rather than from a checked-in .colorscheme:
// data/theme/themes/<id>.toml is the single source of truth.
void EngineBackend::applyThemeColors()
{
    TerminalView *v = view();
    if (!v)
        return;
    const theme::ThemeSpec &spec = theme::active();
    ColorScheme colors = v->colorScheme();
    if (spec.terminalBackground.isValid())
        colors.background = spec.terminalBackground;
    colors.backgroundEnd = spec.terminalBackgroundEnd;
    if (spec.terminalForeground.isValid())
        colors.foreground = spec.terminalForeground;
    colors.cursor = spec.terminalCursor.isValid() ? spec.terminalCursor : colors.foreground;
    colors.cursorText = colors.background;
    // The one colour that means "you can open this" (src/Theme.h, Link): the hover underline, the
    // fold rows' links, OSC 8 hyperlinks and, at rest, every path and URL the output holds.
    colors.link = theme::Link;
    // The shell's prompt row wears a cyan band, the shell's channel colour blended into the ground
    // (the row's ink is the shell's own — a coloured PS1 — so the band stays a tint, unlike the
    // agent line's, whose ink Relay chooses). Off with Options › Terminal › "Band behind what you
    // typed" = none. Needs shell integration, which is what marks the row.
    const QString band = QSettings().value(QStringLiteral("terminal/echo_band"), QStringLiteral("channel")).toString();
    // The rows Relay marks as typed by the user (OSC 7772): the band is the destination colour
    // itself with the chip ink on it; "chrome" is the raised surface with the text colour; "none"
    // is no band and the destination colour as the ink. The view resolves these at paint time, so
    // every such row — scrollback included — follows a theme switch (owner, 2026-09-19: "can we
    // change the design that the background highlights shift with theme changes").
    if (band == QStringLiteral("none")) {
        colors.userShellBand = colors.userAgentBand = QColor();
        colors.userShellInk = theme::Shell; colors.userAgentInk = theme::Agent;
    } else if (band == QStringLiteral("chrome")) {
        colors.userShellBand = colors.userAgentBand = theme::SurfaceRaised;
        colors.userShellInk = colors.userAgentInk = theme::Text;
    } else {
        colors.userShellBand = theme::Shell; colors.userShellInk = theme::chipInk(theme::Shell);
        colors.userAgentBand = theme::Agent; colors.userAgentInk = theme::chipInk(theme::Agent);
    }
    if (band == QStringLiteral("none")) colors.promptBand = QColor();
    else {
        const QColor ground = colors.background;
        const QColor fill = band == QStringLiteral("chrome") ? theme::SurfaceRaised : theme::Shell;
        const qreal w = band == QStringLiteral("chrome") ? 1.0 : (ground.lightnessF() > 0.5 ? 0.16 : 0.22);
        colors.promptBand = QColor(int(fill.red() * w + ground.red() * (1 - w)), int(fill.green() * w + ground.green() * (1 - w)),
                                   int(fill.blue() * w + ground.blue() * (1 - w)));
    }
    // The engine numbers the 16 ANSI colours 0-7 then the bright eight, as the theme file does.
    if (spec.ansi.size() == 16)
        for (int i = 0; i < 16; ++i)
            colors.palette[size_t(i)] = spec.ansi.at(i).rgb();
    v->setColorScheme(colors);
    v->update();
}

} // namespace relay
