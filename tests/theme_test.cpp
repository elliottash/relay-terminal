// SPDX-License-Identifier: GPL-3.0-or-later
// Colour themes (issue 0JA7): the theme file reader, the token contract every theme has to meet,
// discovery across the user folder and the packaged one, and the Konsole colour scheme generated
// from a theme. The live switch (palette, stylesheet, engines) is src/Theme.cpp and is checked
// under Xvfb; everything here is pure and runs without a window.
#include "ThemeFile.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>

using namespace relay::theme;

namespace {
QString read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll());
}

void write(const QString &path, const QString &text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(text.toUtf8());
}

// --- contrast (WCAG 2.1) and colour difference (CIELAB dE76) -----------------------------------
// The same arithmetic as docs/qa_evidence/2026-09-18-copper-and-beige-themes/contrast.py, which
// additionally measures the ink-on-fill pairs Theme.cpp derives; these are the direct pairs.

double channel(int v) {
    const double s = v / 255.0;
    return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
}

double luminance(const QColor &c) {
    return 0.2126 * channel(c.red()) + 0.7152 * channel(c.green()) + 0.0722 * channel(c.blue());
}

double contrast(const QColor &a, const QColor &b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

double deltaE(const QColor &a, const QColor &b) {
    const auto lab = [](const QColor &c) {
        const auto lin = [](int v) {
            const double s = v / 255.0;
            return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
        };
        const double r = lin(c.red()), g = lin(c.green()), bl = lin(c.blue());
        const double x = (0.4124 * r + 0.3576 * g + 0.1805 * bl) / 0.95047;
        const double y = 0.2126 * r + 0.7152 * g + 0.0722 * bl;
        const double z = (0.0193 * r + 0.1192 * g + 0.9505 * bl) / 1.08883;
        const auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; };
        return std::array<double, 3>{116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z))};
    };
    const auto p = lab(a), q = lab(b);
    return std::sqrt(std::pow(p[0] - q[0], 2) + std::pow(p[1] - q[1], 2) + std::pow(p[2] - q[2], 2));
}

ThemeSpec shipped(const QString &id) {
    QString error;
    return parseTheme(read(QStringLiteral("data/theme/themes/") + id + QStringLiteral(".toml")), id,
                      builtinDark(), &error);
}
}  // namespace

class ThemeTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // --- the TOML subset -------------------------------------------------------------------
    void readsTablesKeysAndArrays() {
        const QString text = QStringLiteral(
            "# a comment\n"
            "[theme]\n"
            "name = \"Relay Dark\"   # trailing comment\n"
            "variant = 'dark'\n"
            "\n"
            "[ui]\n"
            "background = \"#0f1115\"\n"
            "\n"
            "[terminal]\n"
            "palette = [\"#000000\", \"#ffffff\"]\n"
            "[flags]\n"
            "board_material = true\n");
        QString error;
        const auto values = parseToml(text, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(values.value(QStringLiteral("theme.name")), QStringList{QStringLiteral("Relay Dark")});
        QCOMPARE(values.value(QStringLiteral("theme.variant")), QStringList{QStringLiteral("dark")});
        QCOMPARE(values.value(QStringLiteral("ui.background")), QStringList{QStringLiteral("#0f1115")});
        QCOMPARE(values.value(QStringLiteral("terminal.palette")).size(), 2);
        QCOMPARE(values.value(QStringLiteral("flags.board_material")), QStringList{QStringLiteral("true")});
    }

    void readsAnArraySpreadOverLines() {
        const QString text = QStringLiteral("[terminal]\npalette = [\n  \"#111111\", \"#222222\",\n  \"#333333\",\n]\n");
        QString error;
        const auto values = parseToml(text, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(values.value(QStringLiteral("terminal.palette")),
                 QStringList({QStringLiteral("#111111"), QStringLiteral("#222222"), QStringLiteral("#333333")}));
    }

    void reportsASyntaxErrorWithoutLosingTheRest() {
        QString error;
        const auto values = parseToml(QStringLiteral("[ui]\nbackground = \"#000\"\nnonsense\n"), &error);
        QVERIFY(!error.isEmpty());
        QCOMPARE(values.value(QStringLiteral("ui.background")), QStringList{QStringLiteral("#000")});
    }

    // --- the token contract ----------------------------------------------------------------
    void theBuiltInThemeIsComplete() {
        QStringList missing;
        QVERIFY2(isComplete(builtinDark(), &missing), qPrintable(missing.join(QStringLiteral(", "))));
        QCOMPARE(builtinDark().ansi.size(), 16);
        QCOMPARE(builtinDark().id, QStringLiteral("relay-dark"));
    }

    // Every theme Relay ships has to carry every token, or a switch would leave a widget
    // coloured by the previous theme.
    void everyShippedThemeIsComplete() {
        const auto files = discoverThemeFiles({QStringLiteral("data/theme/themes")});
        QVERIFY2(files.size() >= 4, "expected the built-in themes in data/theme/themes");
        QVERIFY(files.contains(QStringLiteral("relay-dark")));
        QVERIFY(files.contains(QStringLiteral("relay-light")));
        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            QString error;
            const ThemeSpec spec = parseTheme(read(*it), it.key(), ThemeSpec{}, &error);
            QVERIFY2(error.isEmpty(), qPrintable(it.key() + QStringLiteral(": ") + error));
            QStringList missing;
            QVERIFY2(isComplete(spec, &missing),
                     qPrintable(it.key() + QStringLiteral(": ") + missing.join(QStringLiteral(", "))));
            QVERIFY(!spec.name.isEmpty());
        }
    }

    // The shipped dark theme must still be the colours the app was designed in.
    void relayDarkMatchesTheCompiledInFallback() {
        QString error;
        const ThemeSpec spec =
            parseTheme(read(QStringLiteral("data/theme/themes/relay-dark.toml")), QStringLiteral("relay-dark"),
                       ThemeSpec{}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const ThemeSpec &fallback = builtinDark();
        for (const QString &token : {QStringLiteral("background"), QStringLiteral("surface"),
                                     QStringLiteral("surface_raised"), QStringLiteral("border"),
                                     QStringLiteral("border_strong"), QStringLiteral("text"),
                                     QStringLiteral("text_muted"), QStringLiteral("accent"),
                                     QStringLiteral("accent_text")})
            QCOMPARE(spec.uiColor(token).name(), fallback.uiColor(token).name());
        for (const QString &token : syntaxTokenNames())
            QCOMPARE(spec.syntaxColor(token).name(), fallback.syntaxColor(token).name());
        QCOMPARE(spec.ansi, fallback.ansi);
        QCOMPARE(spec.terminalBackground.name(), fallback.terminalBackground.name());
        QCOMPARE(spec.terminalForeground.name(), fallback.terminalForeground.name());
    }

    void theLightThemeIsMarkedLight() {
        QString error;
        const ThemeSpec spec = parseTheme(read(QStringLiteral("data/theme/themes/relay-light.toml")),
                                          QStringLiteral("relay-light"), ThemeSpec{}, &error);
        QVERIFY(spec.isLight());
        // A light theme is only usable if the text is darker than the surface it sits on.
        QVERIFY(spec.uiColor(QStringLiteral("text")).lightness()
                < spec.uiColor(QStringLiteral("background")).lightness());
        QVERIFY(spec.terminalForeground.lightness() < spec.terminalBackground.lightness());
    }

    // Issue 0EXJ: /light and /dark are the two themes the owner named, by id, so the two theme
    // files those commands reach for have to be there and be the right way round. Renaming either
    // file is what would break the commands, and this is what says so.
    void theLightAndDarkCommandsHaveTheirThemes() {
        const ThemeSpec beige = shipped(QStringLiteral("ibm-beige"));
        QCOMPARE(beige.name, QStringLiteral("IBM Beige"));
        QVERIFY(beige.isLight());
        const ThemeSpec copper = shipped(QStringLiteral("dark-copper"));
        QCOMPARE(copper.name, QStringLiteral("Dark Copper"));
        QVERIFY(!copper.isLight());
    }

    // --- filling gaps and keeping the unknown ------------------------------------------------
    void missingTokensComeFromTheFallback() {
        QString error;
        const ThemeSpec spec = parseTheme(QStringLiteral("[theme]\nname = \"Half\"\n[ui]\naccent = \"#ff0000\"\n"),
                                          QStringLiteral("half"), builtinDark(), &error);
        QVERIFY(isComplete(spec));
        QCOMPARE(spec.uiColor(QStringLiteral("accent")).name(), QStringLiteral("#ff0000"));
        QCOMPARE(spec.uiColor(QStringLiteral("text")).name(), builtinDark().uiColor(QStringLiteral("text")).name());
        QCOMPARE(spec.ansi, builtinDark().ansi);
    }

    void aBadColourIsReportedAndFallsBack() {
        QString error;
        const ThemeSpec spec = parseTheme(QStringLiteral("[theme]\nname = \"Bad\"\n[ui]\naccent = \"not-a-colour\"\n"),
                                          QStringLiteral("bad"), builtinDark(), &error);
        QVERIFY(error.contains(QStringLiteral("ui.accent")));
        QCOMPARE(spec.uiColor(QStringLiteral("accent")).name(), builtinDark().uiColor(QStringLiteral("accent")).name());
    }

    // The format has to carry tokens and per-theme booleans a later issue adds (for example the
    // material tokens in docs/SWITCHBOARD-AESTHETIC.md) without any change to the reader.
    void unknownTokensAndFlagsSurvive() {
        QString error;
        const ThemeSpec spec = parseTheme(QStringLiteral("[theme]\nname = \"Future\"\n"
                                                         "[flags]\nboard_material = false\nglow = true\n"
                                                         "[material]\nplate = \"#332211\"\n"),
                                          QStringLiteral("future"), builtinDark(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(spec.flag(QStringLiteral("board_material"), true), false);
        QCOMPARE(spec.flag(QStringLiteral("glow")), true);
        QCOMPARE(spec.flag(QStringLiteral("never_set"), true), true);   // the caller's default
        QCOMPARE(spec.extra.value(QStringLiteral("material.plate")), QStringList{QStringLiteral("#332211")});
    }

    void aSixteenthEntryIsRequiredForThePalette() {
        QString error;
        const ThemeSpec spec = parseTheme(QStringLiteral("[theme]\nname = \"Short\"\n"
                                                         "[terminal]\npalette = [\"#000000\", \"#ffffff\"]\n"),
                                          QStringLiteral("short"), builtinDark(), &error);
        QVERIFY(error.contains(QStringLiteral("16 entries")));
        QCOMPARE(spec.ansi, builtinDark().ansi);
    }

    // --- discovery ----------------------------------------------------------------------------
    void theUserFolderShadowsTheBuiltIn() {
        QTemporaryDir home, data;
        write(data.filePath(QStringLiteral("themes/relay-dark.toml")), QStringLiteral("[theme]\nname=\"Packaged\"\n"));
        write(data.filePath(QStringLiteral("themes/gruvbox-dark.toml")), QStringLiteral("[theme]\nname=\"Gruvbox\"\n"));
        write(home.filePath(QStringLiteral("relay/themes/relay-dark.toml")), QStringLiteral("[theme]\nname=\"Mine\"\n"));
        write(home.filePath(QStringLiteral("relay/themes/mine.toml")), QStringLiteral("[theme]\nname=\"Mine Too\"\n"));

        const QStringList dirs = themeSearchDirs(data.path(), home.path());
        QCOMPARE(dirs.size(), 2);
        QVERIFY(dirs.first().startsWith(home.path()));   // the user folder is searched first

        const auto files = discoverThemeFiles(dirs);
        QCOMPARE(files.keys(), QStringList({QStringLiteral("gruvbox-dark"), QStringLiteral("mine"),
                                            QStringLiteral("relay-dark")}));
        QVERIFY(files.value(QStringLiteral("relay-dark")).startsWith(home.path()));
        QVERIFY(files.value(QStringLiteral("gruvbox-dark")).startsWith(data.path()));
    }

    void discoveryIgnoresAMissingFolder() {
        QVERIFY(discoverThemeFiles({QStringLiteral("/nonexistent/relay/themes")}).isEmpty());
        QCOMPARE(themeSearchDirs(QString(), QString()), QStringList());
    }

    // --- what the engine is handed -------------------------------------------------------------
    // Colour reaches the terminal straight from the theme (src/EngineBackend.cpp applyThemeColors),
    // so a shipped theme has to carry a whole grid: 16 ANSI entries, a ground, a foreground and a
    // cursor. This is what the generated Konsole .colorscheme used to prove before KonsolePart was
    // retired (2026-09-18).
    void everyShippedThemeCarriesAWholeGrid() {
        const auto files = discoverThemeFiles({QStringLiteral("data/theme/themes")});
        QVERIFY(!files.isEmpty());
        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            const ThemeSpec spec = parseTheme(read(*it), it.key(), builtinDark());
            QCOMPARE(spec.ansi.size(), 16);
            for (int i = 0; i < 16; ++i)
                QVERIFY2(spec.ansi.at(i).isValid(), qPrintable(it.key() + QStringLiteral(" ANSI %1").arg(i)));
            for (const QColor &c : {spec.terminalBackground, spec.terminalForeground, spec.terminalCursor,
                                    spec.terminalBackgroundIntense, spec.terminalForegroundIntense})
                QVERIFY2(c.isValid(), qPrintable(it.key()));
            // A gradient end is optional, but a theme that names one has to mean it.
            if (!spec.terminalBackgroundEnd.isValid())
                continue;
            QVERIFY2(spec.terminalBackgroundEnd != spec.terminalBackground, qPrintable(it.key()));
        }
    }

    // --- the auditioned themes: Dark Copper and IBM Beige (owner, 2026-09-18) --------------------
    // Every text/background pair a person reads, at WCAG AA (4.5:1) or, for an edge a person
    // must find, 3:1 (WCAG 1.4.11). The incumbents are measured by the evidence script but not
    // asserted here: they are not this change's to alter.

    void theAuditionedThemesMeetTheirContrastContract_data() {
        QTest::addColumn<QString>("id");
        QTest::newRow("dark-copper") << QStringLiteral("dark-copper");
        QTest::newRow("ibm-beige") << QStringLiteral("ibm-beige");
    }
    void theAuditionedThemesMeetTheirContrastContract() {
        QFETCH(QString, id);
        const ThemeSpec spec = shipped(id);
        QVERIFY2(isComplete(spec), qPrintable(id + QStringLiteral(" is not complete")));
        struct Pair { QColor fg, bg; double min; const char *what; };
        const auto ui = [&spec](const char *t) { return spec.uiColor(QString::fromLatin1(t)); };
        QList<Pair> pairs;
        // A theme with a chrome material paints its raised faces as a gradient, so the lit top
        // and the shaded bottom of a chip are both grounds text is read on.
        QList<QColor> raisedFaces{ui("surface_raised")};
        if (spec.flag(QStringLiteral("metal")) || spec.flag(QStringLiteral("plastic"))) {
            for (const char *stop : {"material.light", "material.dark", "material.chrome_light",
                                     "material.chrome_dark"}) {
                const QStringList raw = spec.extra.value(QString::fromLatin1(stop));
                QVERIFY2(!raw.isEmpty(), stop);
                const QColor face(raw.first().trimmed());
                QVERIFY2(face.isValid(), stop);
                raisedFaces << face;
            }
        }
        for (const QColor &face : raisedFaces)
            pairs << Pair{ui("text"), face, 4.5, "text on a raised face"}
                  << Pair{ui("text_muted"), face, 4.5, "text_muted on a raised face"};
        for (const char *ground : {"background", "surface", "surface_raised"}) {
            pairs << Pair{ui("text"), ui(ground), 4.5, "text"} << Pair{ui("text_muted"), ui(ground), 4.5, "text_muted"};
        }
        pairs << Pair{ui("accent"), ui("background"), 4.5, "accent as text"}
              << Pair{ui("accent_text"), ui("accent"), 4.5, "primary button label"}
              << Pair{ui("border_strong"), ui("background"), 3.0, "focused pane outline (UI)"};
        for (const char *dest : {"shell", "agent"}) {
            pairs << Pair{ui(dest), ui("background"), 4.5, dest};
            for (const QColor &face : raisedFaces) pairs << Pair{ui(dest), face, 4.5, dest};
        }
        for (const char *state : {"success", "warning", "error"}) {
            pairs << Pair{ui(state), ui("background"), 4.5, state};
            for (const QColor &face : raisedFaces) pairs << Pair{ui(state), face, 4.5, state};
        }
        // The idle composer is `surface`, the focused one `surface_raised`: syntax is read on both.
        for (const QString &token : syntaxTokenNames())
            for (const char *ground : {"surface", "surface_raised"})
                pairs << Pair{spec.syntaxColor(token), ui(ground), 4.5, "syntax"};
        // A shaded grid is measured at both ends of the fade: text sitting on the bottom edge is
        // read exactly as often as text on the top one.
        QList<QColor> grounds{spec.terminalBackground};
        if (spec.terminalBackgroundEnd.isValid())
            grounds << spec.terminalBackgroundEnd;
        for (const QColor &ground : grounds) {
            pairs << Pair{spec.terminalForeground, ground, 4.5, "terminal text"}
                  << Pair{spec.terminalCursor, ground, 3.0, "cursor (UI)"};
            // ANSI 0 is a background in practice; ANSI 8 is the deliberately dim one (UI 3:1).
            for (int i = 1; i < 16; ++i)
                pairs << Pair{spec.ansi.value(i), ground, i == 8 ? 3.0 : 4.5, "ANSI"};
        }

        for (const Pair &p : pairs) {
            const double r = contrast(p.fg, p.bg);
            QVERIFY2(r >= p.min, qPrintable(QStringLiteral("%1: %2 %3 on %4 is %5:1, needs %6:1")
                                                .arg(id, QString::fromLatin1(p.what), p.fg.name(), p.bg.name())
                                                .arg(r, 0, 'f', 2).arg(p.min, 0, 'f', 1)));
        }
    }

    // "Legible text" (docs/ARCHITECTURE.md, owner 2026-09-18): every shipped theme, not only the
    // auditioned two. Text, muted text and every state colour that is drawn as text (links, tab
    // labels, chips) clear 4.5:1 on each ground the chrome paints: background, surface and
    // surface_raised. Muted text is also the terminal's note and tool ink, so it clears the
    // terminal's ground too; the composer's syntax colours are read on surface and surface_raised.
    void everyShippedThemeKeepsItsTextLegible() {
        const auto files = discoverThemeFiles({QStringLiteral("data/theme/themes")});
        QVERIFY(files.size() >= 6);
        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            const ThemeSpec spec = shipped(it.key());
            const auto ui = [&spec](const char *t) { return spec.uiColor(QString::fromLatin1(t)); };
            const QList<QPair<const char *, QColor>> grounds{
                {"background", ui("background")}, {"surface", ui("surface")}, {"surface_raised", ui("surface_raised")}};
            QList<std::tuple<QString, QColor, QString, QColor>> pairs;
            for (const char *fg : {"text", "text_muted", "accent", "shell", "agent", "success", "warning", "error"})
                for (const auto &ground : grounds)
                    pairs.append({QString::fromLatin1(fg), ui(fg), QString::fromLatin1(ground.first), ground.second});
            QList<QColor> terminal{spec.terminalBackground};
            if (spec.terminalBackgroundEnd.isValid()) terminal << spec.terminalBackgroundEnd;
            for (const QColor &ground : terminal)
                pairs.append({QStringLiteral("text_muted"), ui("text_muted"), QStringLiteral("the terminal"), ground});
            for (const QString &token : syntaxTokenNames())
                for (const char *ground : {"surface", "surface_raised"})
                    pairs.append({QStringLiteral("syntax.") + token, spec.syntaxColor(token), QString::fromLatin1(ground), ui(ground)});
            for (const auto &[what, fg, where, bg] : pairs) {
                const double r = contrast(fg, bg);
                QVERIFY2(r >= 4.5, qPrintable(QStringLiteral("%1: %2 %3 on %4 %5 is %6:1, needs 4.5:1")
                                                  .arg(it.key(), what, fg.name(), where, bg.name())
                                                  .arg(r, 0, 'f', 2)));
            }
        }
    }

    // The trap the owner's brief named: copper sits between amber (warning) and red (error).
    void copperStaysClearOfAmberAndRed() {
        const ThemeSpec spec = shipped(QStringLiteral("dark-copper"));
        const auto ui = [&spec](const char *t) { return spec.uiColor(QString::fromLatin1(t)); };
        // The bright copper, the accent, has to be a different colour, not merely a darker one.
        for (const char *meaning : {"warning", "error"}) {
            const double d = deltaE(ui("accent"), ui(meaning));
            QVERIFY2(d >= 20.0, qPrintable(QStringLiteral("accent vs %1: dE %2 < 20")
                                               .arg(QString::fromLatin1(meaning)).arg(d, 0, 'f', 1)));
        }
        // The structural copper has to stay dim: a rule must never read as a lit warning.
        const double warning = contrast(ui("warning"), ui("background"));
        for (const char *chrome : {"border", "surface_raised"}) {
            const double c = contrast(ui(chrome), ui("background"));
            QVERIFY2(c * 2 < warning, qPrintable(QStringLiteral("%1 is %2:1 on the ground, too close to warning's %3:1")
                                                     .arg(QString::fromLatin1(chrome)).arg(c, 0, 'f', 2)
                                                     .arg(warning, 0, 'f', 2)));
        }
        // The meaning colours are Relay Dark's, unchanged.
        QCOMPARE(ui("warning"), builtinDark().uiColor(QStringLiteral("warning")));
        QCOMPARE(ui("error"), builtinDark().uiColor(QStringLiteral("error")));
    }

    // The owner asked for a greyed pair on IBM Beige; it must still be two colours.
    void theDestinationPairStaysTwoColours_data() {
        QTest::addColumn<QString>("id");
        QTest::newRow("dark-copper") << QStringLiteral("dark-copper");
        QTest::newRow("ibm-beige") << QStringLiteral("ibm-beige");
    }
    void theDestinationPairStaysTwoColours() {
        QFETCH(QString, id);
        const ThemeSpec spec = shipped(id);
        const QColor shell = spec.uiColor(QStringLiteral("shell")), agent = spec.uiColor(QStringLiteral("agent"));
        const double d = deltaE(shell, agent);
        QVERIFY2(d >= 20.0, qPrintable(QStringLiteral("%1: shell %2 vs agent %3 is dE %4 < 20")
                                           .arg(id, shell.name(), agent.name()).arg(d, 0, 'f', 1)));
        // Shell is the cooler of the two in every theme; violet is never the bluer.
        QVERIFY(shell.blue() - shell.red() > agent.blue() - agent.red());
    }

    void theBeigeTerminalInvertsTheAnsiRamp() {
        // On a light ground ANSI 7 and 15 must be the dark end, or \e[37m text vanishes.
        const ThemeSpec spec = shipped(QStringLiteral("ibm-beige"));
        QVERIFY(spec.isLight());
        const QColor bg = spec.terminalBackground;
        QVERIFY(contrast(spec.ansi.value(15), bg) > contrast(spec.ansi.value(8), bg));
        QVERIFY(contrast(spec.ansi.value(7), bg) >= 4.5);
    }

    void theChromeFlagsAndBevelColoursAreRead() {
        const ThemeSpec beige = shipped(QStringLiteral("ibm-beige"));
        QVERIFY(beige.flag(QStringLiteral("bevel")));
        QVERIFY(beige.flag(QStringLiteral("square")));
        QCOMPARE(QColor(beige.extra.value(QStringLiteral("bevel.light")).value(0)), QColor(QStringLiteral("#f0e4d4")));
        QCOMPARE(QColor(beige.extra.value(QStringLiteral("bevel.dark")).value(0)), QColor(QStringLiteral("#8e785d")));
        // Off unless a theme asks: the incumbents' stylesheet must not change.
        const ThemeSpec copper = shipped(QStringLiteral("dark-copper"));
        QVERIFY(!copper.flag(QStringLiteral("bevel")));
        QVERIFY(!copper.flag(QStringLiteral("square")));
        for (const QString &id : {QStringLiteral("relay-dark"), QStringLiteral("relay-light"),
                                  QStringLiteral("gruvbox-dark"), QStringLiteral("solarized-dark")}) {
            const ThemeSpec spec = shipped(id);
            QVERIFY2(!spec.flag(QStringLiteral("bevel")) && !spec.flag(QStringLiteral("square")), qPrintable(id));
        }
    }
};

QTEST_MAIN(ThemeTests)
#include "theme_test.moc"
