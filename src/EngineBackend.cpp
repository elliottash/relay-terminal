// SPDX-License-Identifier: GPL-3.0-or-later
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
}

// Relay ships one Konsole profile in data/theme/konsole; engine panes read its font and
// spacing so both engines look the same side by side. Colour comes from the theme below.
void EngineBackend::applyRelayProfile()
{
    TerminalView *v = view();
    if (!v)
        return;
    const QString dir = theme::themeDataDir();
    if (dir.isEmpty())
        return;

    const QSettings profile(dir + QStringLiteral("/konsole/Relay.profile"), QSettings::IniFormat);
    const QString fontSpec = profile.value(QStringLiteral("Appearance/Font")).toString();
    QFont font;
    if (!fontSpec.isEmpty() && font.fromString(fontSpec))
        v->setTerminalFont(font);
    // Konsole's own spacing keys: without them engine panes look tighter than Konsole panes.
    v->setLineSpacing(profile.value(QStringLiteral("Appearance/LineSpacing"), 0).toInt());
    v->setPadding(profile.value(QStringLiteral("Appearance/TerminalMargin"), 2).toInt());
    v->setUnfocusedCursorVisible(profile.value(QStringLiteral("Cursor Options/ShowUnfocusedCursor"), true).toBool());

    applyThemeColors();
}

// Colour comes from the selected theme (issue 0JA7) rather than from a checked-in .colorscheme:
// data/theme/themes/<id>.toml is the single source of truth, and the Konsole scheme KonsolePart
// panes read is generated from the same file.
void EngineBackend::applyThemeColors()
{
    TerminalView *v = view();
    if (!v)
        return;
    const theme::ThemeSpec &spec = theme::active();
    ColorScheme colors = v->colorScheme();
    if (spec.terminalBackground.isValid())
        colors.background = spec.terminalBackground;
    if (spec.terminalForeground.isValid())
        colors.foreground = spec.terminalForeground;
    colors.cursor = spec.terminalCursor.isValid() ? spec.terminalCursor : colors.foreground;
    colors.cursorText = colors.background;
    // The engine numbers the 16 ANSI colours 0-7 then the bright eight, as the theme file does.
    if (spec.ansi.size() == 16)
        for (int i = 0; i < 16; ++i)
            colors.palette[size_t(i)] = spec.ansi.at(i).rgb();
    v->setColorScheme(colors);
    v->update();
}

} // namespace relay
