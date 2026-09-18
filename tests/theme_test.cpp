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

    // --- the generated Konsole files -----------------------------------------------------------
    void konsoleNamesAreStableAndSafe() {
        QCOMPARE(konsoleNameFor(QStringLiteral("relay-dark")), QStringLiteral("RelayThemeRelayDark"));
        QCOMPARE(konsoleNameFor(QStringLiteral("solarized_light 2")), QStringLiteral("RelayThemeSolarizedLight2"));
    }

    void theSchemeCarriesEveryColourKonsoleReads() {
        const QString text = konsoleSchemeText(builtinDark());
        // Konsole numbers the 16 ANSI colours Color0..Color7 plus Intense; Faint is the third.
        for (int i = 0; i < 8; ++i) {
            QVERIFY(text.contains(QStringLiteral("[Color%1]\n").arg(i)));
            QVERIFY(text.contains(QStringLiteral("[Color%1Faint]\n").arg(i)));
            QVERIFY(text.contains(QStringLiteral("[Color%1Intense]\n").arg(i)));
        }
        QVERIFY(text.contains(QStringLiteral("[Background]\nColor=15,17,21\n")));
        QVERIFY(text.contains(QStringLiteral("[Foreground]\nColor=216,220,227\n")));
        // The bright half of the palette is what Konsole calls Intense.
        QVERIFY(text.contains(QStringLiteral("[Color1Intense]\nColor=255,140,143\n")));
        QVERIFY(text.contains(QStringLiteral("[General]\n")));
        QVERIFY(text.contains(QStringLiteral("Description=Relay Dark")));
    }

    void faintIsTheColourMixedIntoTheBackground() {
        const QColor faint = faintOf(QColor(242, 119, 122), QColor(15, 17, 21));
        QCOMPARE(faint.red(), 167);
        // Faint of the background is the background.
        QCOMPARE(faintOf(QColor(15, 17, 21), QColor(15, 17, 21)).name(), QStringLiteral("#0f1115"));
    }

    void theProfileKeepsEverythingButTheSchemeAndTheName() {
        const QString base = QStringLiteral("[Appearance]\nColorScheme=RelayDark\nFont=Hack,11\nTerminalMargin=12\n"
                                            "\n[General]\nName=Relay\nParent=FALLBACK/\n");
        const QString out = konsoleProfileText(base, QStringLiteral("RelayThemeX"), QStringLiteral("RelayThemeX"));
        QVERIFY(out.contains(QStringLiteral("ColorScheme=RelayThemeX")));
        QVERIFY(out.contains(QStringLiteral("Name=RelayThemeX")));
        QVERIFY(!out.contains(QStringLiteral("ColorScheme=RelayDark")));
        QVERIFY(out.contains(QStringLiteral("Font=Hack,11")));
        QVERIFY(out.contains(QStringLiteral("TerminalMargin=12")));
        QVERIFY(out.contains(QStringLiteral("Parent=FALLBACK/")));
    }

    void aProfileWithoutThoseKeysStillGetsThem() {
        const QString out = konsoleProfileText(QStringLiteral("[Scrolling]\nHistorySize=20000\n"),
                                               QStringLiteral("RelayThemeX"), QStringLiteral("RelayThemeX"));
        QVERIFY(out.contains(QStringLiteral("ColorScheme=RelayThemeX")));
        QVERIFY(out.contains(QStringLiteral("Name=RelayThemeX")));
        QVERIFY(out.contains(QStringLiteral("HistorySize=20000")));
    }

    // Every shipped theme has to survive the round trip to a Konsole scheme, because that file is
    // what both KonsolePart and Relay's own engine read.
    void everyShippedThemeGeneratesAScheme() {
        const auto files = discoverThemeFiles({QStringLiteral("data/theme/themes")});
        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            const ThemeSpec spec = parseTheme(read(*it), it.key(), builtinDark());
            const QString text = konsoleSchemeText(spec);
            QVERIFY(text.contains(QStringLiteral("[Color7Intense]\n")));
            QVERIFY(text.contains(QStringLiteral("Description=") + spec.name));
            // 8 ANSI colours x (base, faint, intense) + a background and a foreground trio.
            QCOMPARE(text.count(QStringLiteral("\nColor=")), 30);
        }
    }
};

QTEST_MAIN(ThemeTests)
#include "theme_test.moc"
