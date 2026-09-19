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
        QStringLiteral("shell"), QStringLiteral("agent"),
        // The Actions pane's red-orange (owner, 2026-09-18). Unlike the rest of this list, a theme
        // that is silent about it does not inherit Relay Dark's: see redOrangeFrom() below.
        QStringLiteral("action"),
        // The brass a tool pane's header band is drawn in (the Switchboard and its neighbours).
        // It used to *be* `warning`, which made amber mean both "this pane is a tool" and "this is
        // waiting on you" — the second of which must never be missed (owner, 2026-09-19). Derived
        // from the theme's own amber when the file is silent, not inherited: see brassFrom().
        QStringLiteral("tool"),
        // "You can open this": a path, a folder, a URL, a card reference — in the terminal grid,
        // the composer, the fold rows and the chrome alike (owner, 2026-09-19: "clickable things
        // need to be understood from colors", then "dark green, like Warp"). Derived from the
        // theme's own dark green (ANSI 2) when the file is silent: see linkFrom().
        QStringLiteral("link")};
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

double relativeLuminance(const QColor &c) {
    const auto channel = [](int v) {
        const double s = v / 255.0;
        return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.red()) + 0.7152 * channel(c.green()) + 0.0722 * channel(c.blue());
}

double contrastRatio(const QColor &a, const QColor &b) {
    const double la = relativeLuminance(a), lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// The Actions pane's red-orange for a theme file that does not name one. This is the one `[ui]`
// token that is not simply inherited from the fallback theme: an orange picked for Relay Dark's
// near-black chrome is under 2:1 on a light theme's paper, and the Actions band would be the one
// piece of chrome a user theme could not make legible. So it is turned out of that theme's own
// red instead — the hue moved to a red-orange, then the lightness walked back until the relative
// luminance matches the red's exactly, which hands it every contrast `error` already passed (at
// an unchanged HSL lightness an orange is the brighter of the two, and on paper that costs
// contrast). Saturation has a floor so a theme whose red is nearly grey still gets a colour.
// Brass from this theme's own amber: the same hue family, dulled, at exactly the amber's relative
// luminance so it inherits every contrast that `warning` already passed on this theme's grounds.
// A material, not a flag — which is the whole distinction the token exists to draw.
QColor brassFrom(const QColor &amber) {
    const QColor hsl = amber.toHsl();
    const int saturation = qMax(int(hsl.hslSaturation() * 0.72), 90);
    const double want = relativeLuminance(amber);
    QColor best = QColor::fromHsl(36, saturation, hsl.lightness()).toRgb();
    for (int lightness = 0; lightness <= 255; ++lightness) {
        const QColor tried = QColor::fromHsl(36, saturation, lightness).toRgb();
        if (std::abs(relativeLuminance(tried) - want) < std::abs(relativeLuminance(best) - want)) best = tried;
    }
    return best;
}

// `seed` moved as little as it takes to clear 4.5:1 on every one of `grounds`: it is walked
// towards whichever pole (white or black) the hardest ground is further from, so the colour keeps
// its hue and only its lightness gives. Used for a link colour and for the board's metal, both of
// which are a colour the theme already owns that has to be legible somewhere new.
QColor legibleOn(const QColor &seed, const QList<QColor> &grounds) {
    const QColor &blue = seed;
    const auto clears = [&grounds](const QColor &c) {
        for (const QColor &g : grounds) if (contrastRatio(c, g) < 4.5) return false;
        return true;
    };
    if (clears(blue)) return blue;
    // Which pole helps: the one the *darkest-to-read* ground is further from.
    double best = 0; QColor pole = Qt::white;
    for (const QColor &g : grounds) {
        for (const QColor &p : {QColor(Qt::white), QColor(Qt::black)}) {
            const double r = contrastRatio(p, g);
            if (r > best) { best = r; pole = p; }
        }
    }
    QColor out = blue;
    for (int step = 1; step <= 40; ++step) {
        const double w = step / 40.0;
        out = QColor(int(pole.red() * w + blue.red() * (1 - w)), int(pole.green() * w + blue.green() * (1 - w)),
                     int(pole.blue() * w + blue.blue() * (1 - w)));
        if (clears(out)) return out;
    }
    return out;
}

// The link green for a theme file that does not name one: the theme's own ANSI 2 (the dark green of
// every palette), lifted until it clears 4.5:1 on every ground the app reads it on — the window,
// the text surface, the raised face, the terminal — so a silent user theme still gets a link colour
// a person can read.
QColor linkFrom(const QColor &green, const QList<QColor> &grounds) { return legibleOn(green, grounds); }

// --- the Switchboard's materials, for a theme that names no [board] table ------------------------
// The board is a physical object in the pane: a face with hardware on it (docs/SWITCHBOARD-AESTHETIC
// .md 3.4). All three come out of the theme's own chrome, because Relay Dark's bakelite is invisible
// on paper and its brass is 2.35:1 on white.

// The face: one step from the raised chip face towards the window, so the board reads as a sheet
// mounted on the chassis rather than as either of them. Both ends already carry this theme's text
// contrast, so what lies between them does too.
QColor boardFaceFrom(const QColor &raised, const QColor &background) {
    if (!raised.isValid()) return background;
    if (!background.isValid()) return raised;
    return mix(raised, background, 0.65);
}

// The hardware: this theme's own brass (`ui.tool` — the metal a tool pane's band is made of),
// lifted if it does not read on the face, because a jack ring carries the enamel label beside it.
QColor boardMetalFrom(const QColor &brass, const QColor &face) {
    if (!brass.isValid()) return face;
    return face.isValid() ? legibleOn(brass, {face}) : brass;
}

// Unlit hardware: the same metal half sunk into the face. Structural, never a flag — which is why
// it is a step of the material and not a step towards the amber.
QColor boardMetalDimFrom(const QColor &metal, const QColor &face) {
    return face.isValid() && metal.isValid() ? mix(metal, face, 0.5) : metal;
}

QColor redOrangeFrom(const QColor &red) {
    const QColor hsl = red.toHsl();
    const int saturation = qMax(hsl.hslSaturation(), 150);
    const double want = relativeLuminance(red);
    QColor best = QColor::fromHsl(20, saturation, hsl.lightness()).toRgb();
    for (int lightness = 0; lightness <= 255; ++lightness) {
        const QColor tried = QColor::fromHsl(20, saturation, lightness).toRgb();
        if (std::abs(relativeLuminance(tried) - want) < std::abs(relativeLuminance(best) - want)) best = tried;
    }
    return best;
}

}  // namespace

QStringList uiTokenNames() { return requiredUi() + optionalUi(); }

QStringList syntaxTokenNames() {
    return {QStringLiteral("command"), QStringLiteral("unknown"), QStringLiteral("flag"),
            QStringLiteral("string"),  QStringLiteral("path"),    QStringLiteral("operator"),
            QStringLiteral("variable"), QStringLiteral("agent"),  QStringLiteral("token")};
}

QStringList boardTokenNames() {
    return {QStringLiteral("face"), QStringLiteral("metal"), QStringLiteral("metal_dim")};
}

QColor ThemeSpec::uiColor(const QString &token, const QColor &fallback) const {
    const auto it = ui.constFind(token);
    return it == ui.constEnd() ? fallback : *it;
}

QColor ThemeSpec::syntaxColor(const QString &token, const QColor &fallback) const {
    const auto it = syntax.constFind(token);
    return it == syntax.constEnd() ? fallback : *it;
}

QColor ThemeSpec::boardColor(const QString &token, const QColor &fallback) const {
    const auto it = board.constFind(token);
    return it == board.constEnd() ? fallback : *it;
}

bool ThemeSpec::flag(const QString &name, bool fallback) const {
    const auto it = flags.constFind(name);
    return it == flags.constEnd() ? fallback : *it;
}

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
            {QStringLiteral("action"), QColor(0xe5, 0x84, 0x4f)},
            {QStringLiteral("tool"), QColor(0xc8, 0xa4, 0x5c)},
            {QStringLiteral("link"), QColor(0x12, 0xa4, 0x57)},
            {QStringLiteral("shell"), QColor(0x3e, 0xc5, 0xf0)},
            {QStringLiteral("agent"), QColor(0xb4, 0x8e, 0xf7)},
        };
        s.syntax = {
            {QStringLiteral("command"), QColor(0x3e, 0xc5, 0xf0)},
            {QStringLiteral("unknown"), QColor(0xf0, 0x71, 0x78)},
            {QStringLiteral("flag"), QColor(0xe5, 0xc0, 0x7b)},
            {QStringLiteral("string"), QColor(0x7e, 0xc8, 0x8c)},
            {QStringLiteral("path"), QColor(0x12, 0xa4, 0x57)},   // = link: a path you type is one you can open
            {QStringLiteral("operator"), QColor(0x80, 0x87, 0x96)},
            {QStringLiteral("variable"), QColor(0xb4, 0x8e, 0xf7)},
            {QStringLiteral("agent"), QColor(0xb4, 0x8e, 0xf7)},
            {QStringLiteral("token"), QColor(0x3e, 0xc5, 0xf0)},
        };
        // The Switchboard's materials, as docs/SWITCHBOARD-AESTHETIC.md 3.2 draws them: a bakelite
        // face, brass hardware, and the same brass unlit. Identical to relay-dark.toml's [board].
        s.board = {
            {QStringLiteral("face"), QColor(0x17, 0x14, 0x0f)},
            {QStringLiteral("metal"), QColor(0xc8, 0xa4, 0x5c)},
            {QStringLiteral("metal_dim"), QColor(0x6b, 0x56, 0x37)},
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

    // What the file itself names, and only that. A value that is not a colour is reported and
    // treated as absent. Every gap is filled below, in one order that matters: a derivation from
    // *this* theme's own colours first, and only then a borrow from `fallback`.
    //
    // The borrow used to come first, which made every derivation Theme.cpp documents dead code: a
    // light theme that left `[ui] shell` out was handed Relay Dark's cyan on white paper instead
    // of its own accent, and the same for `selection`, `disabled`, `accent_hover` and all nine
    // `syntax.*` colours (2026-09-19 review).
    const auto readColors = [&](const QString &table, const QStringList &names, QMap<QString, QColor> &into) {
        for (const QString &name : names) {
            const QString raw = scalar(table + QLatin1Char('.') + name);
            if (raw.isEmpty()) continue;
            bool ok = false;
            const QColor color = parseColor(raw, &ok);
            if (ok) { into.insert(name, color); continue; }
            problems << QStringLiteral("%1.%2 is not a colour: %3").arg(table, name, raw);
        }
    };
    readColors(QStringLiteral("ui"), uiTokenNames(), spec.ui);
    readColors(QStringLiteral("syntax"), syntaxTokenNames(), spec.syntax);
    readColors(QStringLiteral("board"), boardTokenNames(), spec.board);

    // Taken from `fallback` when the file is silent, because there is nothing to compute them
    // from: the nine tokens every theme has to carry, the three meaning colours (a theme's own
    // green, amber and red cannot be guessed from its accent) and the agent violet. Each one is
    // named in `spec.borrowed`, which is what src/Theme.cpp's one warning line prints.
    const auto borrow = [&](const QString &token) {
        if (spec.ui.contains(token)) return;
        const auto it = fallback.ui.constFind(token);
        if (it == fallback.ui.constEnd()) return;
        spec.ui.insert(token, *it);
        spec.borrowed << QStringLiteral("ui.") + token;
    };
    for (const QString &name : requiredUi()) borrow(name);
    for (const char *name : {"success", "warning", "error", "agent"}) borrow(QString::fromLatin1(name));

    // Derived from this theme's own accent and muted text, as src/Theme.cpp's adoptTokens() and
    // stylesheet say: `shell` *is* the accent (the two input destinations are the accent and the
    // violet), and the accent's lighter, darker and dulled steps are the hover, the selection and
    // the disabled grey. The steps are builtinDark()'s own arithmetic, so Relay Dark comes out
    // unchanged whether it is read from its file or derived.
    const auto derive = [&spec](const QString &token, const QColor &color) {
        if (!spec.ui.contains(token) && color.isValid()) spec.ui.insert(token, color);
    };
    const QColor ownAccent = spec.uiColor(QStringLiteral("accent"));
    derive(QStringLiteral("shell"), ownAccent);
    derive(QStringLiteral("accent_hover"), ownAccent.isValid() ? ownAccent.lighter(115) : QColor());
    derive(QStringLiteral("selection"), ownAccent.isValid() ? ownAccent.darker(200) : QColor());
    const QColor ownMuted = spec.uiColor(QStringLiteral("text_muted"));
    derive(QStringLiteral("disabled"), ownMuted.isValid() ? ownMuted.darker(150) : QColor());
    // `ui.action` is derived from this theme's own red rather than inherited (redOrangeFrom), and
    // `ui.tool` from its own amber (brassFrom), for the same reason: a colour that has to stay a
    // measured distance from another of *this* theme's colours cannot be borrowed from Relay Dark.
    {
        bool named = false;
        const QString raw = scalar(QStringLiteral("ui.action"));
        if (!raw.isEmpty()) parseColor(raw, &named);
        const auto red = spec.ui.constFind(QStringLiteral("error"));
        if (!named && red != spec.ui.constEnd()) spec.ui.insert(QStringLiteral("action"), redOrangeFrom(*red));
    }
    {
        bool named = false;
        const QString raw = scalar(QStringLiteral("ui.tool"));
        if (!raw.isEmpty()) parseColor(raw, &named);
        const auto amber = spec.ui.constFind(QStringLiteral("warning"));
        if (!named && amber != spec.ui.constEnd()) spec.ui.insert(QStringLiteral("tool"), brassFrom(*amber));
    }
    // `ui.link` is derived from this theme's own ANSI 2 (linkFrom) — but only once the terminal
    // ground is known, so it is done below, after [terminal] is read.
    bool linkNamed = false;
    {
        const QString raw = scalar(QStringLiteral("ui.link"));
        if (!raw.isEmpty()) parseColor(raw, &linkNamed);
    }

    const auto readOne = [&](const QString &key, const QColor &fallbackColor) {
        const QString raw = scalar(key);
        bool ok = false;
        const QColor color = raw.isEmpty() ? QColor() : parseColor(raw, &ok);
        if (ok) return color;
        if (!raw.isEmpty()) problems << QStringLiteral("%1 is not a colour: %2").arg(key, raw);
        return fallbackColor;
    };
    spec.terminalBackground = readOne(QStringLiteral("terminal.background"), fallback.terminalBackground);
    // A gradient is opt-in: no key, no gradient (and no fallback from another theme).
    spec.terminalBackgroundEnd = readOne(QStringLiteral("terminal.background_end"), QColor());
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

    // `ui.link` for a file that named none: this theme's own dark green, ANSI 2, moved until it
    // reads on every ground a link is painted on (linkFrom). Never Relay Dark's: a green picked for
    // a near-black window is under 3:1 on paper.
    if (!linkNamed) {
        QList<QColor> grounds{spec.uiColor(QStringLiteral("background")), spec.uiColor(QStringLiteral("surface")),
                              spec.uiColor(QStringLiteral("surface_raised")), spec.terminalBackground};
        if (spec.terminalBackgroundEnd.isValid()) grounds << spec.terminalBackgroundEnd;
        grounds.removeAll(QColor());
        spec.ui.insert(QStringLiteral("link"), linkFrom(spec.ansi.value(2, QColor(0x12, 0xa4, 0x57)), grounds));
    }

    // The Switchboard's materials for a theme that named none (docs/SWITCHBOARD-AESTHETIC.md 3.4):
    // the face out of this theme's own chrome, the metal out of its own brass, the dim metal out of
    // the two. Never borrowed from Relay Dark — a bakelite mixed for a near-black window is the
    // window's own colour on paper, and Relay Dark's brass is 2.35:1 on white. Each one that had to
    // be computed is named in `spec.derived`, so a theme author can see what was decided for them.
    const auto deriveBoard = [&spec](const QString &token, const QColor &color) {
        if (spec.board.contains(token) || !color.isValid()) return;
        spec.board.insert(token, color);
        spec.derived << QStringLiteral("board.") + token;
    };
    deriveBoard(QStringLiteral("face"), boardFaceFrom(spec.uiColor(QStringLiteral("surface_raised")),
                                                     spec.uiColor(QStringLiteral("background"))));
    const QColor boardFace = spec.boardColor(QStringLiteral("face"));
    deriveBoard(QStringLiteral("metal"), boardMetalFrom(spec.uiColor(QStringLiteral("tool")), boardFace));
    deriveBoard(QStringLiteral("metal_dim"),
                boardMetalDimFrom(spec.boardColor(QStringLiteral("metal")), boardFace));

    // Every composer colour a theme leaves out is one of its *own* ui colours — the mapping
    // src/Theme.cpp's adoptTokens() spells out. Done here, after `link` is settled, because
    // `syntax.path` is the link colour: a path you can type is a path you can open.
    const auto deriveSyntax = [&spec](const QString &token, const QString &from) {
        if (spec.syntax.contains(token)) return;
        const QColor color = spec.uiColor(from);
        if (color.isValid()) spec.syntax.insert(token, color);
    };
    deriveSyntax(QStringLiteral("command"), QStringLiteral("shell"));
    deriveSyntax(QStringLiteral("unknown"), QStringLiteral("error"));
    deriveSyntax(QStringLiteral("flag"), QStringLiteral("warning"));
    deriveSyntax(QStringLiteral("string"), QStringLiteral("success"));
    deriveSyntax(QStringLiteral("path"), QStringLiteral("link"));
    deriveSyntax(QStringLiteral("operator"), QStringLiteral("text_muted"));
    deriveSyntax(QStringLiteral("variable"), QStringLiteral("agent"));
    deriveSyntax(QStringLiteral("agent"), QStringLiteral("agent"));
    deriveSyntax(QStringLiteral("token"), QStringLiteral("shell"));
    // A theme so incomplete that even the derivation had nothing to work from still renders.
    for (const QString &name : syntaxTokenNames()) {
        if (spec.syntax.contains(name)) continue;
        const auto it = fallback.syntax.constFind(name);
        if (it == fallback.syntax.constEnd()) continue;
        spec.syntax.insert(name, *it);
        spec.borrowed << QStringLiteral("syntax.") + name;
    }

    // Intense foreground/background default to a step away from the base pair, which is what
    // Konsole's own schemes do; a theme may pin them.
    spec.terminalBackgroundIntense = readOne(QStringLiteral("terminal.background_intense"),
                                             mix(spec.terminalBackground, spec.terminalForeground, 0.93));
    spec.terminalForegroundIntense = readOne(QStringLiteral("terminal.foreground_intense"),
                                             mix(spec.terminalForeground, spec.ansi.value(15, Qt::white), 0.4));

    // Flags and anything else the reader does not know: kept so a later token or per-theme switch
    // needs no format change.
    static const QRegularExpression known(
        QStringLiteral("^(theme\\.(name|variant|description)|ui\\.|syntax\\.|board\\.|terminal\\.)"));
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
            || (key.startsWith(QLatin1String("board.")) && boardTokenNames().contains(key.mid(6)))
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
    for (const QString &name : boardTokenNames())
        if (!spec.board.contains(name) || !spec.board.value(name).isValid())
            gaps << QStringLiteral("board.") + name;
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

}  // namespace relay::theme
