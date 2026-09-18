// SPDX-License-Identifier: GPL-3.0-or-later
#include "ThemeFile.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <cmath>

namespace relay::theme {
namespace {

// The nine tokens every theme has to carry, then the ones a theme may override and that are
// otherwise derived from the accent or taken from the dark theme. Keeping both lists in one
// place means a new token only has to be added here and in builtinDark().
const QStringList &requiredUi() {
    static const QStringList names{
        QStringLiteral("background"), QStringLiteral("surface"), QStringLiteral("surface_raised"),
        QStringLiteral("border"), QStringLiteral("border_strong"), QStringLiteral("text"),
        QStringLiteral("text_muted"), QStringLiteral("accent"), QStringLiteral("accent_text")};
    return names;
}

const QStringList &optionalUi() {
    static const QStringList names{
        // accent derivatives; a theme may pin them rather than let Theme.cpp compute them
        QStringLiteral("accent_hover"), QStringLiteral("selection"), QStringLiteral("disabled"),
        // semantic colours the stylesheet uses for state (done / warning / failed) and for the
        // two input destinations, so a light theme can darken them
        QStringLiteral("success"), QStringLiteral("warning"), QStringLiteral("error"),
        QStringLiteral("shell"), QStringLiteral("agent")};
    return names;
}

QColor parseColor(const QString &raw, bool *ok = nullptr) {
    const QString value = raw.trimmed();
    QColor color(value);
    if (ok) *ok = color.isValid();
    return color;
}

QString unquote(const QString &raw) {
    QString value = raw.trimmed();
    if (value.size() >= 2 && ((value.startsWith('"') && value.endsWith('"'))
                              || (value.startsWith('\'') && value.endsWith('\'')))) {
        value = value.mid(1, value.size() - 2);
        value.replace(QLatin1String("\\\""), QLatin1String("\""));
        value.replace(QLatin1String("\\\\"), QLatin1String("\\"));
        value.replace(QLatin1String("\\n"), QLatin1String("\n"));
    }
    return value;
}

// Strip a trailing comment that is not inside a string.
QString withoutComment(const QString &line) {
    bool inSingle = false, inDouble = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == '\\' && inDouble) { ++i; continue; }
        if (c == '\'' && !inDouble) inSingle = !inSingle;
        else if (c == '"' && !inSingle) inDouble = !inDouble;
        else if (c == '#' && !inSingle && !inDouble) return line.left(i);
    }
    return line;
}

QStringList splitArray(const QString &body) {
    QStringList out;
    QString current;
    bool inSingle = false, inDouble = false;
    for (int i = 0; i < body.size(); ++i) {
        const QChar c = body.at(i);
        if (c == '\\' && inDouble && i + 1 < body.size()) { current += c; current += body.at(++i); continue; }
        if (c == '\'' && !inDouble) inSingle = !inSingle;
        else if (c == '"' && !inSingle) inDouble = !inDouble;
        if (c == ',' && !inSingle && !inDouble) {
            if (!current.trimmed().isEmpty()) out << unquote(current);
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.trimmed().isEmpty()) out << unquote(current);
    return out;
}

QColor mix(const QColor &a, const QColor &b, double weightOfA) {
    const double w = qBound(0.0, weightOfA, 1.0);
    return QColor(int(std::lround(a.red() * w + b.red() * (1 - w))),
                  int(std::lround(a.green() * w + b.green() * (1 - w))),
                  int(std::lround(a.blue() * w + b.blue() * (1 - w))));
}

QString iniColor(const QString &group, const QColor &c) {
    return QStringLiteral("[%1]\nColor=%2,%3,%4\n\n").arg(group).arg(c.red()).arg(c.green()).arg(c.blue());
}

}  // namespace

QStringList uiTokenNames() { return requiredUi() + optionalUi(); }

QStringList syntaxTokenNames() {
    return {QStringLiteral("command"), QStringLiteral("unknown"), QStringLiteral("flag"),
            QStringLiteral("string"),  QStringLiteral("path"),    QStringLiteral("operator"),
            QStringLiteral("variable"), QStringLiteral("agent"),  QStringLiteral("token")};
}

QColor ThemeSpec::uiColor(const QString &token, const QColor &fallback) const {
    const auto it = ui.constFind(token);
    return it == ui.constEnd() ? fallback : *it;
}

QColor ThemeSpec::syntaxColor(const QString &token, const QColor &fallback) const {
    const auto it = syntax.constFind(token);
    return it == syntax.constEnd() ? fallback : *it;
}

bool ThemeSpec::flag(const QString &name, bool fallback) const {
    const auto it = flags.constFind(name);
    return it == flags.constEnd() ? fallback : *it;
}

QColor faintOf(const QColor &color, const QColor &background) { return mix(color, background, 0.67); }

// --- TOML ---------------------------------------------------------------------------------------

QMap<QString, QStringList> parseToml(const QString &text, QString *error) {
    QMap<QString, QStringList> out;
    QString table;
    QString pendingKey;         // set while an array spans several lines
    QString pendingBody;
    int lineNumber = 0;
    const auto fail = [&](const QString &what) {
        if (error && error->isEmpty()) *error = QStringLiteral("line %1: %2").arg(lineNumber).arg(what);
    };
    if (error) error->clear();

    for (const QString &rawLine : text.split('\n')) {
        ++lineNumber;
        QString line = withoutComment(rawLine).trimmed();
        if (!pendingKey.isEmpty()) {
            pendingBody += line;
            if (!line.contains(']')) continue;
            const int close = pendingBody.lastIndexOf(']');
            out.insert(pendingKey, splitArray(pendingBody.left(close)));
            pendingKey.clear();
            pendingBody.clear();
            continue;
        }
        if (line.isEmpty()) continue;
        if (line.startsWith('[')) {
            const int close = line.indexOf(']');
            if (close < 0) { fail(QStringLiteral("unterminated table header")); continue; }
            table = unquote(line.mid(1, close - 1).trimmed());
            if (table.startsWith('[')) table = table.mid(1);   // [[array of tables]] - kept flat
            continue;
        }
        const int eq = line.indexOf('=');
        if (eq <= 0) { fail(QStringLiteral("expected key = value")); continue; }
        const QString key = unquote(line.left(eq).trimmed());
        const QString value = line.mid(eq + 1).trimmed();
        if (key.isEmpty()) { fail(QStringLiteral("empty key")); continue; }
        const QString full = table.isEmpty() ? key : table + QLatin1Char('.') + key;
        if (value.startsWith('[')) {
            const int close = value.lastIndexOf(']');
            if (close > 0) out.insert(full, splitArray(value.mid(1, close - 1)));
            else { pendingKey = full; pendingBody = value.mid(1); }
            continue;
        }
        if (value.isEmpty()) { fail(QStringLiteral("empty value")); continue; }
        out.insert(full, QStringList{unquote(value)});
    }
    if (!pendingKey.isEmpty()) fail(QStringLiteral("unterminated array"));
    return out;
}

// --- the compiled-in dark theme -----------------------------------------------------------------

const ThemeSpec &builtinDark() {
    static const ThemeSpec spec = [] {
        ThemeSpec s;
        s.id = QStringLiteral("relay-dark");
        s.name = QStringLiteral("Relay Dark");
        s.variant = QStringLiteral("dark");
        s.description = QStringLiteral("Relay's default dark theme.");
        s.ui = {
            {QStringLiteral("background"), QColor(0x0f, 0x11, 0x15)},
            {QStringLiteral("surface"), QColor(0x16, 0x18, 0x1d)},
            {QStringLiteral("surface_raised"), QColor(0x1c, 0x1f, 0x26)},
            {QStringLiteral("border"), QColor(0x2a, 0x2e, 0x37)},
            {QStringLiteral("border_strong"), QColor(0x4a, 0x52, 0x60)},
            {QStringLiteral("text"), QColor(0xe6, 0xe8, 0xec)},
            {QStringLiteral("text_muted"), QColor(0x8b, 0x91, 0x9c)},
            {QStringLiteral("accent"), QColor(0x3e, 0xc5, 0xf0)},
            {QStringLiteral("accent_text"), QColor(0x06, 0x1a, 0x22)},
            {QStringLiteral("accent_hover"), QColor(0x3e, 0xc5, 0xf0).lighter(115)},
            {QStringLiteral("selection"), QColor(0x3e, 0xc5, 0xf0).darker(200)},
            {QStringLiteral("disabled"), QColor(0x8b, 0x91, 0x9c).darker(150)},
            {QStringLiteral("success"), QColor(0x7e, 0xc8, 0x8c)},
            {QStringLiteral("warning"), QColor(0xe5, 0xc0, 0x7b)},
            {QStringLiteral("error"), QColor(0xe0, 0x6c, 0x75)},
            {QStringLiteral("shell"), QColor(0x3e, 0xc5, 0xf0)},
            {QStringLiteral("agent"), QColor(0xb4, 0x8e, 0xf7)},
        };
        s.syntax = {
            {QStringLiteral("command"), QColor(0x3e, 0xc5, 0xf0)},
            {QStringLiteral("unknown"), QColor(0xf0, 0x71, 0x78)},
            {QStringLiteral("flag"), QColor(0xe5, 0xc0, 0x7b)},
            {QStringLiteral("string"), QColor(0x7e, 0xc8, 0x8c)},
            {QStringLiteral("path"), QColor(0x66, 0xd0, 0xc0)},
            {QStringLiteral("operator"), QColor(0x80, 0x87, 0x96)},
            {QStringLiteral("variable"), QColor(0xb4, 0x8e, 0xf7)},
            {QStringLiteral("agent"), QColor(0xb4, 0x8e, 0xf7)},
            {QStringLiteral("token"), QColor(0x3e, 0xc5, 0xf0)},
        };
        s.terminalBackground = QColor(15, 17, 21);
        s.terminalForeground = QColor(216, 220, 227);
        s.terminalCursor = QColor(240, 242, 246);
        s.terminalBackgroundIntense = QColor(22, 24, 29);
        s.terminalForegroundIntense = QColor(245, 247, 250);
        s.ansi = {QColor(29, 32, 39),    QColor(242, 119, 122), QColor(125, 211, 153), QColor(236, 196, 118),
                  QColor(97, 175, 239),  QColor(198, 146, 233), QColor(86, 200, 216),  QColor(200, 205, 214),
                  QColor(99, 105, 117),  QColor(255, 140, 143), QColor(152, 229, 176), QColor(248, 213, 142),
                  QColor(129, 196, 255), QColor(216, 170, 245), QColor(120, 221, 234), QColor(240, 242, 246)};
        return s;
    }();
    return spec;
}

// --- theme files --------------------------------------------------------------------------------

ThemeSpec parseTheme(const QString &text, const QString &id, const ThemeSpec &fallback, QString *error) {
    QString tomlError;
    const QMap<QString, QStringList> values = parseToml(text, &tomlError);
    QStringList problems;
    if (!tomlError.isEmpty()) problems << tomlError;

    ThemeSpec spec;
    spec.id = id;
    const auto scalar = [&values](const QString &key) -> QString {
        const auto it = values.constFind(key);
        return (it == values.constEnd() || it->isEmpty()) ? QString() : it->first();
    };
    spec.name = scalar(QStringLiteral("theme.name"));
    if (spec.name.isEmpty()) { spec.name = id; problems << QStringLiteral("theme.name is missing"); }
    spec.variant = scalar(QStringLiteral("theme.variant")).toLower();
    if (spec.variant != QLatin1String("light")) spec.variant = QStringLiteral("dark");
    spec.description = scalar(QStringLiteral("theme.description"));

    const auto readColors = [&](const QString &table, const QStringList &names,
                                const QMap<QString, QColor> &from, QMap<QString, QColor> &into) {
        for (const QString &name : names) {
            const QString raw = scalar(table + QLatin1Char('.') + name);
            bool ok = false;
            const QColor color = raw.isEmpty() ? QColor() : parseColor(raw, &ok);
            if (ok) { into.insert(name, color); continue; }
            if (!raw.isEmpty()) problems << QStringLiteral("%1.%2 is not a colour: %3").arg(table, name, raw);
            const auto it = from.constFind(name);
            if (it != from.constEnd()) into.insert(name, *it);
        }
    };
    readColors(QStringLiteral("ui"), uiTokenNames(), fallback.ui, spec.ui);
    readColors(QStringLiteral("syntax"), syntaxTokenNames(), fallback.syntax, spec.syntax);

    const auto readOne = [&](const QString &key, const QColor &fallbackColor) {
        const QString raw = scalar(key);
        bool ok = false;
        const QColor color = raw.isEmpty() ? QColor() : parseColor(raw, &ok);
        if (ok) return color;
        if (!raw.isEmpty()) problems << QStringLiteral("%1 is not a colour: %2").arg(key, raw);
        return fallbackColor;
    };
    spec.terminalBackground = readOne(QStringLiteral("terminal.background"), fallback.terminalBackground);
    spec.terminalForeground = readOne(QStringLiteral("terminal.foreground"), fallback.terminalForeground);
    spec.terminalCursor = readOne(QStringLiteral("terminal.cursor"), fallback.terminalCursor);

    const auto palette = values.value(QStringLiteral("terminal.palette"));
    if (palette.size() == 16) {
        bool allValid = true;
        QVector<QColor> colors;
        for (const QString &entry : palette) {
            bool ok = false;
            const QColor color = parseColor(entry, &ok);
            if (!ok) { allValid = false; break; }
            colors << color;
        }
        if (allValid) spec.ansi = colors;
        else problems << QStringLiteral("terminal.palette holds a value that is not a colour");
    } else if (!palette.isEmpty()) {
        problems << QStringLiteral("terminal.palette needs 16 entries, found %1").arg(palette.size());
    }
    if (spec.ansi.size() != 16) spec.ansi = fallback.ansi;

    // Intense foreground/background default to a step away from the base pair, which is what
    // Konsole's own schemes do; a theme may pin them.
    spec.terminalBackgroundIntense = readOne(QStringLiteral("terminal.background_intense"),
                                             mix(spec.terminalBackground, spec.terminalForeground, 0.93));
    spec.terminalForegroundIntense = readOne(QStringLiteral("terminal.foreground_intense"),
                                             mix(spec.terminalForeground, spec.ansi.value(15, Qt::white), 0.4));

    // Flags and anything else the reader does not know: kept so a later token or per-theme switch
    // needs no format change.
    static const QRegularExpression known(
        QStringLiteral("^(theme\\.(name|variant|description)|ui\\.|syntax\\.|terminal\\.)"));
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (it.key().startsWith(QLatin1String("flags."))) {
            const QString name = it.key().mid(6);
            const QString value = it->isEmpty() ? QString() : it->first().toLower();
            spec.flags.insert(name, value == QLatin1String("true") || value == QLatin1String("1")
                                        || value == QLatin1String("yes"));
            continue;
        }
        const QString key = it.key();
        const bool isKnownColour =
            (key.startsWith(QLatin1String("ui.")) && uiTokenNames().contains(key.mid(3)))
            || (key.startsWith(QLatin1String("syntax.")) && syntaxTokenNames().contains(key.mid(7)))
            || key.startsWith(QLatin1String("terminal."));
        if (known.match(key).hasMatch() && isKnownColour) continue;
        if (key.startsWith(QLatin1String("theme."))
            && QStringList{QStringLiteral("theme.name"), QStringLiteral("theme.variant"),
                           QStringLiteral("theme.description")}.contains(key))
            continue;
        spec.extra.insert(key, *it);
    }

    if (error) *error = problems.join(QStringLiteral("; "));
    return spec;
}

bool isComplete(const ThemeSpec &spec, QStringList *missing) {
    QStringList gaps;
    for (const QString &name : uiTokenNames())
        if (!spec.ui.contains(name) || !spec.ui.value(name).isValid()) gaps << QStringLiteral("ui.") + name;
    for (const QString &name : syntaxTokenNames())
        if (!spec.syntax.contains(name) || !spec.syntax.value(name).isValid())
            gaps << QStringLiteral("syntax.") + name;
    if (spec.ansi.size() != 16) gaps << QStringLiteral("terminal.palette");
    if (!spec.terminalBackground.isValid()) gaps << QStringLiteral("terminal.background");
    if (!spec.terminalForeground.isValid()) gaps << QStringLiteral("terminal.foreground");
    if (spec.name.isEmpty()) gaps << QStringLiteral("theme.name");
    if (missing) *missing = gaps;
    return gaps.isEmpty();
}

QStringList themeSearchDirs(const QString &dataDir, const QString &configHome) {
    QStringList dirs;
    if (!configHome.isEmpty()) dirs << QDir(configHome).absoluteFilePath(QStringLiteral("relay/themes"));
    if (!dataDir.isEmpty()) dirs << QDir(dataDir).absoluteFilePath(QStringLiteral("themes"));
    return dirs;
}

QMap<QString, QString> discoverThemeFiles(const QStringList &dirs) {
    QMap<QString, QString> found;
    for (const QString &path : dirs) {
        QDir dir(path);
        if (!dir.exists()) continue;
        const auto entries = dir.entryInfoList({QStringLiteral("*.toml")}, QDir::Files, QDir::Name);
        for (const QFileInfo &file : entries)
            if (!found.contains(file.completeBaseName()))   // earlier directories win
                found.insert(file.completeBaseName(), file.absoluteFilePath());
    }
    return found;
}

// --- generated Konsole files --------------------------------------------------------------------

QString konsoleNameFor(const QString &themeId) {
    QString name = QStringLiteral("RelayTheme");
    bool upper = true;
    for (const QChar c : themeId) {
        if (!c.isLetterOrNumber()) { upper = true; continue; }
        name += upper ? c.toUpper() : c;
        upper = false;
    }
    return name;
}

QString konsoleSchemeText(const ThemeSpec &spec) {
    const QColor background = spec.terminalBackground;
    QString out;
    out += iniColor(QStringLiteral("Background"), background);
    out += iniColor(QStringLiteral("BackgroundFaint"), background);
    out += iniColor(QStringLiteral("BackgroundIntense"), spec.terminalBackgroundIntense);
    for (int i = 0; i < 8 && spec.ansi.size() == 16; ++i) {
        const QString base = QStringLiteral("Color%1").arg(i);
        out += iniColor(base, spec.ansi.at(i));
        out += iniColor(base + QStringLiteral("Faint"), faintOf(spec.ansi.at(i), background));
        out += iniColor(base + QStringLiteral("Intense"), spec.ansi.at(i + 8));
    }
    out += iniColor(QStringLiteral("Foreground"), spec.terminalForeground);
    out += iniColor(QStringLiteral("ForegroundFaint"), faintOf(spec.terminalForeground, background));
    out += iniColor(QStringLiteral("ForegroundIntense"), spec.terminalForegroundIntense);
    out += QStringLiteral("[General]\nAnchor=0.5,0.5\nBlur=false\nColorRandomization=false\n"
                          "Description=%1\nFillStyle=Tile\nOpacity=1\nWallpaper=\n"
                          "WallpaperFlipType=NoFlip\nWallpaperOpacity=1\n")
               .arg(spec.name.isEmpty() ? spec.id : spec.name);
    return out;
}

QString konsoleProfileText(const QString &baseProfile, const QString &profileName, const QString &schemeName) {
    QStringList lines;
    bool sawScheme = false, sawName = false;
    for (const QString &line : baseProfile.split('\n')) {
        if (line.startsWith(QLatin1String("ColorScheme="))) {
            lines << QStringLiteral("ColorScheme=") + schemeName;
            sawScheme = true;
        } else if (line.startsWith(QLatin1String("Name="))) {
            lines << QStringLiteral("Name=") + profileName;
            sawName = true;
        } else {
            lines << line;
        }
    }
    QString out = lines.join('\n');
    if (!sawScheme) out += QStringLiteral("\n[Appearance]\nColorScheme=") + schemeName + QLatin1Char('\n');
    if (!sawName) out += QStringLiteral("\n[General]\nName=") + profileName + QLatin1Char('\n');
    if (!out.endsWith('\n')) out += QLatin1Char('\n');
    return out;
}

}  // namespace relay::theme
