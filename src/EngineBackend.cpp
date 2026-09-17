// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineBackend.h"

#include "Theme.h"
#include "view/TerminalView.h"

#include <QFile>
#include <QFont>
#include <QSettings>

namespace relay {

namespace {

QColor colorAt(const QSettings &scheme, const QString &group, const QColor &fallback)
{
    const QStringList parts = scheme.value(group + QStringLiteral("/Color")).toString().split(QLatin1Char(','));
    if (parts.size() != 3)
        return fallback;
    bool r = false, g = false, b = false;
    const QColor color(parts[0].trimmed().toInt(&r), parts[1].trimmed().toInt(&g), parts[2].trimmed().toInt(&b));
    return r && g && b && color.isValid() ? color : fallback;
}

} // namespace

EngineBackend::EngineBackend(const QString &coreName, QWidget *parent)
    : VTermBackend(coreName, parent)
{
    applyRelayProfile();
    applySettings();
    if (TerminalView *v = view())
        v->setScrollToBottomOnKeystroke(true);
}

void EngineBackend::applySettings()
{
    if (TerminalView *v = view())
        v->setCopyOnSelect(QSettings().value(QStringLiteral("terminal/copy_on_select"), false).toBool());
}

// Relay ships one Konsole profile and colour scheme in data/theme/konsole; engine
// panes read the same files so both engines look the same side by side.
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

    const QString schemeName = profile.value(QStringLiteral("Appearance/ColorScheme"), QStringLiteral("RelayDark")).toString();
    const QString schemePath = dir + QStringLiteral("/konsole/") + schemeName + QStringLiteral(".colorscheme");
    if (!QFile::exists(schemePath))
        return;
    const QSettings scheme(schemePath, QSettings::IniFormat);
    ColorScheme colors = v->colorScheme();
    colors.background = colorAt(scheme, QStringLiteral("Background"), colors.background);
    colors.foreground = colorAt(scheme, QStringLiteral("Foreground"), colors.foreground);
    colors.cursor = colorAt(scheme, QStringLiteral("Color7Intense"), colors.foreground);
    colors.cursorText = colors.background;
    // Konsole numbers the 16 ANSI colours Color0..Color7 plus their Intense variants.
    for (int i = 0; i < 8; ++i) {
        const QString base = QStringLiteral("Color%1").arg(i);
        colors.palette[size_t(i)] = colorAt(scheme, base, QColor::fromRgb(colors.palette[size_t(i)])).rgb();
        colors.palette[size_t(i + 8)] =
            colorAt(scheme, base + QStringLiteral("Intense"), QColor::fromRgb(colors.palette[size_t(i + 8)])).rgb();
    }
    v->setColorScheme(colors);
}

} // namespace relay
