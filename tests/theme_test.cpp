// SPDX-License-Identifier: AGPL-3.0-or-later
// Colour themes (issue 0JA7): the theme file reader, the token contract every theme has to meet,
// discovery across the user folder and the packaged one, and the Konsole colour scheme generated
// from a theme. The live switch (palette, stylesheet, engines) is src/Theme.cpp and is checked
// under Xvfb; everything here is pure and runs without a window.
#include "ThemeFile.h"
#include "Theme.h"   // contrastInk: the black-or-white a diff's green/red fill takes

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
                                     QStringLiteral("accent_text"), QStringLiteral("action")})
            QCOMPARE(spec.uiColor(token).name(), fallback.uiColor(token).name());
        for (const QString &token : syntaxTokenNames())
            QCOMPARE(spec.syntaxColor(token).name(), fallback.syntaxColor(token).name());
        for (const QString &token : boardTokenNames())
            QCOMPARE(spec.boardColor(token).name(), fallback.boardColor(token).name());
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
              // The hover only swaps the fill (`QPushButton:default:hover` in stylesheetFor); the
              // label stays `accent_text`, so the state a button spends its click in is measured
              // too. Relay Light's #008cd6 was 3.67:1 here while `accent` itself passed at 5.68.
              << Pair{ui("accent_text"), ui("accent_hover"), 4.5, "primary button label, hovered"}
              << Pair{ui("border_strong"), ui("background"), 3.0, "focused pane outline (UI)"};
        for (const char *dest : {"shell", "agent"}) {
            pairs << Pair{ui(dest), ui("background"), 4.5, dest};
            for (const QColor &face : raisedFaces) pairs << Pair{ui(dest), face, 4.5, dest};
        }
        for (const char *state : {"success", "warning", "error", "action"}) {
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

    // A diff's add/remove lines are drawn as a fill — the theme's own success/error — with pure
    // black or white on it, whichever reads (theme::contrastInk; owner, 2026-09-19). Whichever of
    // the two inks the helper picks has to clear AA on that fill in every shipped theme, or the
    // request buys colour at the price of legibility.
    void diffFillsTakeABlackOrWhiteInkThatReads() {
        const auto files = discoverThemeFiles({QStringLiteral("data/theme/themes")});
        QVERIFY(files.size() >= 5);
        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            const ThemeSpec spec = shipped(it.key());
            for (const char *state : {"success", "error"}) {
                const QColor fill = spec.uiColor(QString::fromLatin1(state));
                const QColor ink = contrastInk(fill);
                QVERIFY2(ink == QColor(Qt::black) || ink == QColor(Qt::white),
                         qPrintable(it.key() + QStringLiteral(": %1 is not black or white").arg(state)));
                QVERIFY2(contrast(ink, fill) >= 4.5,
                         qPrintable(QStringLiteral("%1: %2 ink on %3 is %4:1")
                                        .arg(it.key(), QString::fromLatin1(state), fill.name())
                                        .arg(contrast(ink, fill), 0, 'f', 2)));
            }
        }
    }

    // "Legible text" (docs/ARCHITECTURE.md, owner 2026-09-18): every shipped theme, not only the
    // auditioned two. Text, muted text and every state colour that is drawn as text (links, tab
    // labels, chips) clear 4.5:1 on each ground the chrome paints: background, surface and
    // surface_raised. Muted text is also the terminal's note and tool ink, so it clears the
    // terminal's ground too; the composer's syntax colours are read on surface and surface_raised.
    void everyShippedThemeKeepsItsTextLegible() {
        const auto files = discoverThemeFiles({QStringLiteral("data/theme/themes")});
        QVERIFY(files.size() >= 5);   // six until Solarized Dark was dropped (owner, 2026-09-18)
        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            const ThemeSpec spec = shipped(it.key());
            const auto ui = [&spec](const char *t) { return spec.uiColor(QString::fromLatin1(t)); };
            // The board face is a ground like the other three: a Switchboard row's title, its id,
            // its badges and its status mark are all painted straight onto it (src/BoardPane.cpp).
            const QList<QPair<const char *, QColor>> grounds{
                {"background", ui("background")}, {"surface", ui("surface")}, {"surface_raised", ui("surface_raised")},
                {"board.face", spec.boardColor(QStringLiteral("face"))}};
            QList<std::tuple<QString, QColor, QString, QColor>> pairs;
            for (const char *fg : {"text", "text_muted", "accent", "shell", "agent", "success", "warning", "error",
                                   "action", "tool", "link"})
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

    // The Switchboard's priority flag (card #VKFV): a filled disc a person has to find on the
    // board's face, so the bar is WCAG 1.4.11's 3:1 for a shape, not text's 4.5:1. The "white"
    // of +1 is the theme's own text token (a literal white would vanish on a light face), so
    // what this really pins down is that the derivation and every shipped theme agree.
    void priorityFlagsReadOnTheBoardFace() {
        const auto files = discoverThemeFiles({QStringLiteral("data/theme/themes")});
        QVERIFY(files.size() >= 5);
        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            const ThemeSpec spec = shipped(it.key());
            const QColor face = spec.boardColor(QStringLiteral("face"));
            for (const char *token : {"priority_low", "priority_one", "priority_two",
                                      "priority_three"}) {
                const QColor ink = spec.boardColor(QString::fromLatin1(token));
                QVERIFY2(ink.isValid(),
                         qPrintable(it.key() + QStringLiteral(": %1 is not set").arg(token)));
                QVERIFY2(contrast(ink, face) >= 3.0,
                         qPrintable(QStringLiteral("%1: %2 %3 on the face %4 is %5:1, needs 3:1")
                                        .arg(it.key(), QString::fromLatin1(token), ink.name(),
                                             face.name())
                                        .arg(contrast(ink, face), 0, 'f', 2)));
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
                                  QStringLiteral("gruvbox-dark")}) {
            const ThemeSpec spec = shipped(id);
            QVERIFY2(!spec.flag(QStringLiteral("bevel")) && !spec.flag(QStringLiteral("square")), qPrintable(id));
        }
    }

    // Owner, 2026-09-18: "remove the solarized dark theme". Its palette could not reach 4.5:1
    // without being lightened away from Schoonover's original (card #N50J), and the answer was to
    // drop it rather than exempt it. Nothing may ship it back in under the same id: a settings file
    // that still names it falls back to Relay Dark (src/Theme.cpp resolveTheme, and the live check
    // in tests/themeswitch_test.cpp), which only works while the id is genuinely absent.
    void solarizedDarkIsGone() {
        const auto files = discoverThemeFiles({QStringLiteral("data/theme/themes")});
        QVERIFY2(!files.contains(QStringLiteral("solarized-dark")), "data/theme/themes/solarized-dark.toml is back");
        QVERIFY(files.contains(QStringLiteral("relay-dark")));
        // The theme a stale setting lands on has to be whole, or the fallback is an empty palette.
        QVERIFY(isComplete(shipped(QStringLiteral("relay-dark"))));
    }

    // Owner, 2026-09-19, on amber carrying two meanings at once: "agree, address that". A tool
    // pane's header band was drawn in `warning`, the same token as the "needs you" glyph, the
    // ask and every other mark that says a person is blocked. It has its own `[ui] tool`
    // now — brass, the same family, dulled — and this holds the two apart in every shipped theme:
    // the band's brass is never the flag's amber, and it stays as legible as everything else.
    void theToolBandIsBrassAndNotTheAmberOfAFlag() {
        const auto files = discoverThemeFiles({QStringLiteral("data/theme/themes")});
        QVERIFY(!files.isEmpty());
        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            const ThemeSpec spec = shipped(it.key());
            const auto ui = [&spec](const char *t) { return spec.uiColor(QString::fromLatin1(t)); };
            const QColor tool = ui("tool"), warning = ui("warning");
            QVERIFY2(tool.isValid(), qPrintable(it.key()));
            QVERIFY2(tool != warning, qPrintable(QStringLiteral("%1: tool is still the warning token")
                                                     .arg(it.key())));
            // Far enough to read as another colour rather than a shade of the same one. The bar is
            // lower than the dE 20 two *flags* are held to: these are never side by side, and the
            // band carries a glyph and the pane's name besides.
            const double d = deltaE(tool, warning);
            QVERIFY2(d >= 10.0, qPrintable(QStringLiteral("%1: tool %2 vs warning %3 is dE %4 < 10")
                                               .arg(it.key(), tool.name(), warning.name()).arg(d, 0, 'f', 1)));
        }
    }

    // Owner, 2026-09-19: "clickable things need to be understood from colors". `[ui] link` is the
    // one colour that means "you can open this" — a path in program output, the composer's path
    // token, a fold's "open x.py", a Markdown link, QPalette::Link — so it has to be a green (the
    // owner's "dark green, like Warp"), legible on every ground a link is read on including the
    // terminal's, and a different colour from `success` (a link is a thing you can open, not a
    // thing that finished) and from both destinations (a link is not a place your typing goes).
    void theLinkGreenIsOneColourAndClearOfSuccessAndTheDestinationPair() {
        const auto files = discoverThemeFiles({QStringLiteral("data/theme/themes")});
        QVERIFY(!files.isEmpty());
        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            const ThemeSpec spec = shipped(it.key());
            const auto ui = [&spec](const char *t) { return spec.uiColor(QString::fromLatin1(t)); };
            const QColor link = ui("link");
            QVERIFY2(link.isValid(), qPrintable(it.key()));
            const int hue = link.toHsl().hslHue();
            QVERIFY2(hue >= 135 && hue <= 180, qPrintable(QStringLiteral("%1: link %2 is hue %3, not a green")
                                                             .arg(it.key(), link.name()).arg(hue)));
            // And not the palette's own greens either, which programs paint `ls -F` executables and
            // diff additions in: ANSI 2 and 10 are the colours a link would otherwise be mistaken for.
            for (int i : {2, 10}) {
                const double d = deltaE(link, spec.ansi.value(i));
                QVERIFY2(d >= 20.0, qPrintable(QStringLiteral("%1: link %2 vs ANSI %3 %4 is dE %5 < 20")
                                                   .arg(it.key(), link.name()).arg(i).arg(spec.ansi.value(i).name()).arg(d, 0, 'f', 1)));
            }
            for (const auto &[what, other] : QList<QPair<QString, QColor>>{{QStringLiteral("shell"), ui("shell")},
                                                                         {QStringLiteral("agent"), ui("agent")},
                                                                         {QStringLiteral("error"), ui("error")},
                                                                         {QStringLiteral("warning"), ui("warning")},
                                                                         {QStringLiteral("success"), ui("success")}}) {
                const double d = deltaE(link, other);
                QVERIFY2(d >= 20.0, qPrintable(QStringLiteral("%1: link %2 vs %3 %4 is dE %5 < 20")
                                                   .arg(it.key(), link.name(), what, other.name()).arg(d, 0, 'f', 1)));
            }
            // Legible on the terminal ground too — that is where most links are — at both ends of a
            // shaded one. (The window grounds are covered by everyShippedThemeKeepsItsTextLegible.)
            QList<QColor> grounds{spec.terminalBackground};
            if (spec.terminalBackgroundEnd.isValid()) grounds << spec.terminalBackgroundEnd;
            for (const QColor &ground : grounds) {
                const double r = contrast(link, ground);
                QVERIFY2(r >= 4.5, qPrintable(QStringLiteral("%1: link %2 on the terminal %3 is %4:1")
                                                  .arg(it.key(), link.name(), ground.name()).arg(r, 0, 'f', 2)));
            }
            // The composer's path token is the same colour: a path you type is one you can open.
            QCOMPARE(spec.syntaxColor(QStringLiteral("path")).name(), link.name());
        }
    }

    // A theme that says nothing about `[ui] link` gets its own dark green (ANSI 2) — moved until
    // it reads on every ground when the palette's green is too dim for the window — never Relay
    // Dark's, which is under 3:1 on paper.
    void aThemeThatNamesNoLinkColourGetsOneFromItsOwnGreen() {
        const QString head = QStringLiteral(
            "[theme]\nname = \"Silent\"\nvariant = \"dark\"\n"
            "[ui]\nbackground = \"#101014\"\nsurface = \"#181820\"\nsurface_raised = \"#20202a\"\n"
            "border = \"#2a2a34\"\nborder_strong = \"#4a4a58\"\ntext = \"#e8e8ee\"\n"
            "text_muted = \"#9a9aa6\"\naccent = \"#4ab8e0\"\naccent_text = \"#04161e\"\n");
        QString error;
        // No [terminal]: the palette is Relay Dark's, whose ANSI 2 already reads on this ground.
        const ThemeSpec inherited = parseTheme(head, QStringLiteral("silent"), builtinDark(), &error);
        QCOMPARE(inherited.uiColor(QStringLiteral("link")).name(), inherited.ansi.value(2).name());
        // A palette whose green is a dim forest: the link is lifted off it until it clears 4.5:1 on
        // every ground, and is still a green.
        const ThemeSpec dim = parseTheme(head + QStringLiteral(
            "[terminal]\nbackground = \"#101014\"\nforeground = \"#e8e8ee\"\npalette = [\n"
            "  \"#000000\", \"#cc0000\", \"#0b4d2a\", \"#c4a000\", \"#3465a4\", \"#75507b\", \"#06989a\", \"#d3d7cf\",\n"
            "  \"#555753\", \"#ef2929\", \"#8ae234\", \"#fce94f\", \"#729fcf\", \"#ad7fa8\", \"#34e2e2\", \"#eeeeec\",\n]\n"),
            QStringLiteral("silent-dim"), builtinDark(), &error);
        const QColor link = dim.uiColor(QStringLiteral("link"));
        QVERIFY(link != QColor(0x0b, 0x4d, 0x2a));
        QVERIFY2(link != QColor(0x12, 0xa4, 0x57), "inherited Relay Dark's green instead of deriving one");
        for (const char *ground : {"background", "surface", "surface_raised"}) {
            const double r = contrast(link, dim.uiColor(QString::fromLatin1(ground)));
            QVERIFY2(r >= 4.5, qPrintable(QStringLiteral("%1 on %2 is %3:1").arg(link.name(), QString::fromLatin1(ground)).arg(r, 0, 'f', 2)));
        }
        const int hue = link.toHsl().hslHue();
        QVERIFY2(hue >= 135 && hue <= 180, qPrintable(QStringLiteral("derived link %1 is hue %2").arg(link.name()).arg(hue)));
    }

    // A theme that says nothing about `[ui] tool` gets one dulled out of its *own* amber, never
    // Relay Dark's, for the reason `action` is derived rather than inherited: it has to keep a
    // measured distance from a colour of the theme it lands in.
    void aThemeThatNamesNoToolColourGetsOneFromItsOwnAmber() {
        QString error;
        const ThemeSpec spec = parseTheme(QStringLiteral(
            "[theme]\nname = \"Silent\"\nvariant = \"dark\"\n"
            "[ui]\nbackground = \"#101014\"\nsurface = \"#181820\"\nsurface_raised = \"#20202a\"\n"
            "border = \"#2a2a34\"\nborder_strong = \"#4a4a58\"\ntext = \"#e8e8ee\"\n"
            "text_muted = \"#9a9aa6\"\naccent = \"#4ab8e0\"\naccent_text = \"#04161e\"\n"
            "warning = \"#d8a23c\"\n"),
            QStringLiteral("silent"), builtinDark(), &error);
        const QColor tool = spec.uiColor(QStringLiteral("tool"));
        const QColor amber = spec.uiColor(QStringLiteral("warning"));
        QCOMPARE(amber.name(), QStringLiteral("#d8a23c"));
        QVERIFY2(tool != amber, qPrintable(tool.name()));
        QVERIFY2(tool != QColor(0xc8, 0xa4, 0x5c), "inherited Relay Dark's brass instead of deriving one");
        QVERIFY2(deltaE(tool, amber) >= 10.0, qPrintable(QStringLiteral("dE %1").arg(deltaE(tool, amber), 0, 'f', 1)));
        // Derived at the amber's own luminance, so it inherits every contrast the amber passed.
        QVERIFY2(std::abs(contrast(tool, spec.uiColor(QStringLiteral("background")))
                          - contrast(amber, spec.uiColor(QStringLiteral("background")))) < 0.35,
                 "the brass did not land on the amber's luminance");
    }

    // A user theme names what it cares about and leaves the rest out. What it leaves out has to be
    // worked out from its *own* colours wherever src/Theme.cpp documents how — `shell` is the
    // accent, and every composer colour is one of the ui tokens — and borrowed from Relay Dark
    // only where there is nothing to work it out from. Until 2026-09-19 the borrow ran first and
    // the derivations were dead: this light theme, silent about `[ui] shell`, was painted Relay
    // Dark's cyan, which is under 2:1 on paper.
    void aLightThemeThatNamesNoShellColourGetsItsOwnAccent() {
        QString error;
        const ThemeSpec paper = parseTheme(QStringLiteral(
            "[theme]\nname = \"Paper\"\nvariant = \"light\"\n"
            "[ui]\nbackground = \"#fbfbf7\"\nsurface = \"#f2f2ec\"\nsurface_raised = \"#e8e8e0\"\n"
            "border = \"#d6d6cc\"\nborder_strong = \"#a8a89c\"\ntext = \"#1c1c18\"\n"
            "text_muted = \"#5a5a52\"\naccent = \"#1c5fa8\"\naccent_text = \"#ffffff\"\n"),
            QStringLiteral("paper"), builtinDark(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const QColor accent = paper.uiColor(QStringLiteral("accent"));
        QCOMPARE(paper.uiColor(QStringLiteral("shell")), accent);
        QVERIFY2(paper.uiColor(QStringLiteral("shell")) != builtinDark().uiColor(QStringLiteral("shell")),
                 "inherited Relay Dark's cyan instead of this theme's own accent");
        // The rest of the accent's family is this theme's too, at builtinDark()'s own steps.
        QCOMPARE(paper.uiColor(QStringLiteral("accent_hover")), accent.lighter(115));
        QCOMPARE(paper.uiColor(QStringLiteral("selection")), accent.darker(200));
        QCOMPARE(paper.uiColor(QStringLiteral("disabled")),
                 paper.uiColor(QStringLiteral("text_muted")).darker(150));
        // And so is every composer colour: `command` is the shell destination, `path` the link.
        QCOMPARE(paper.syntaxColor(QStringLiteral("command")), accent);
        QCOMPARE(paper.syntaxColor(QStringLiteral("path")), paper.uiColor(QStringLiteral("link")));
        QCOMPARE(paper.syntaxColor(QStringLiteral("operator")), paper.uiColor(QStringLiteral("text_muted")));
        QVERIFY2(paper.syntaxColor(QStringLiteral("command")) != builtinDark().syntaxColor(QStringLiteral("command")),
                 "the composer kept Relay Dark's syntax palette");
        // What no derivation covers is still borrowed — and named, so the loader can print one
        // line saying which keys this theme is wearing Relay Dark's colours for.
        QVERIFY2(!paper.borrowed.contains(QStringLiteral("ui.shell")), "shell was borrowed, not derived");
        for (const char *token : {"ui.success", "ui.warning", "ui.error", "ui.agent"})
            QVERIFY2(paper.borrowed.contains(QString::fromLatin1(token)), token);
        QCOMPARE(paper.uiColor(QStringLiteral("success")), builtinDark().uiColor(QStringLiteral("success")));
        // A theme that names everything borrows nothing, so no shipped theme logs that line.
        for (const char *id : {"relay-dark", "relay-light", "dark-copper", "gruvbox-dark", "ibm-beige"})
            QVERIFY2(shipped(QString::fromLatin1(id)).borrowed.isEmpty(),
                     qPrintable(QString::fromLatin1(id) + QStringLiteral(": ")
                                + shipped(QString::fromLatin1(id)).borrowed.join(QStringLiteral(", "))));
    }

    // Owner, 2026-09-18: "make actions red-orange". The Actions pane's band, glyph and title-bar
    // button take `[ui] action`, a fifth meaning hue. It has to be a red-orange — not the red an
    // ssh pane is banded in, and not the amber the Switchboard uses — in every shipped theme, and
    // legible as text like every other meaning colour (everyShippedThemeKeepsItsTextLegible covers
    // the contrast; this covers that it is a distinct colour).
    void actionsAreARedOrangeOfTheirOwn() {
        const auto files = discoverThemeFiles({QStringLiteral("data/theme/themes")});
        QVERIFY(!files.isEmpty());
        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            const ThemeSpec spec = shipped(it.key());
            const auto ui = [&spec](const char *t) { return spec.uiColor(QString::fromLatin1(t)); };
            const QColor action = ui("action");
            QVERIFY2(action.isValid(), qPrintable(it.key()));
            // Red-orange: past the reds, well short of the yellows. Hue is the one thing a name
            // like "red-orange" pins down, so it is asserted rather than left to the eye.
            const int hue = action.toHsv().hsvHue();
            QVERIFY2(hue >= 12 && hue <= 30,
                     qPrintable(QStringLiteral("%1: action %2 is at hue %3, not a red-orange (12-30)")
                                    .arg(it.key(), action.name()).arg(hue)));
            // Clear of the two hues it stands between, by the same CIELAB margin the destination
            // pair and the copper accent are held to.
            for (const char *other : {"error", "warning"}) {
                const double d = deltaE(action, ui(other));
                QVERIFY2(d >= 20.0, qPrintable(QStringLiteral("%1: action %2 vs %3 %4 is dE %5 < 20")
                                                   .arg(it.key(), action.name(), QString::fromLatin1(other),
                                                        ui(other).name())
                                                   .arg(d, 0, 'f', 1)));
            }
        }
        // Dark Copper's accent is itself an orange, so there the red-orange has a third neighbour.
        const ThemeSpec copper = shipped(QStringLiteral("dark-copper"));
        QVERIFY(deltaE(copper.uiColor(QStringLiteral("action")), copper.uiColor(QStringLiteral("accent"))) >= 20.0);
    }

    // A user theme that says nothing about Actions must not inherit Relay Dark's orange: on paper
    // it would be under 2:1. It gets one turned out of its own red, at that red's luminance, so it
    // is legible exactly where the red is.
    void aThemeThatNamesNoActionColourGetsOneFromItsOwnRed() {
        QString error;
        const ThemeSpec paper = parseTheme(QStringLiteral("[theme]\nname = \"Paper\"\nvariant = \"light\"\n"
                                                          "[ui]\nbackground = \"#ffffff\"\nerror = \"#a11020\"\n"),
                                           QStringLiteral("paper"), builtinDark(), &error);
        const QColor red = paper.uiColor(QStringLiteral("error"));
        const QColor action = paper.uiColor(QStringLiteral("action"));
        QCOMPARE(red.name(), QStringLiteral("#a11020"));
        QVERIFY2(action != builtinDark().uiColor(QStringLiteral("action")), qPrintable(action.name()));
        const int hue = action.toHsv().hsvHue();
        QVERIFY2(hue >= 12 && hue <= 30, qPrintable(QStringLiteral("%1 is at hue %2").arg(action.name()).arg(hue)));
        // Same luminance as the red it came from, so every ground the red cleared, it clears.
        const QColor white(QStringLiteral("#ffffff"));
        QVERIFY2(std::abs(contrast(action, white) - contrast(red, white)) < 0.15,
                 qPrintable(QStringLiteral("action %1 is %2:1 on white, the red %3 is %4:1")
                                .arg(action.name()).arg(contrast(action, white), 0, 'f', 2)
                                .arg(red.name()).arg(contrast(red, white), 0, 'f', 2)));
        // A theme that does name one keeps it, untouched.
        const ThemeSpec named = parseTheme(QStringLiteral("[theme]\nname = \"Named\"\n[ui]\naction = \"#ff5522\"\n"),
                                           QStringLiteral("named"), builtinDark(), &error);
        QCOMPARE(named.uiColor(QStringLiteral("action")).name(), QStringLiteral("#ff5522"));
    }

    // --- the Switchboard's materials (owner, 2026-09-19: "yeah build that out") ------------------
    // `[board]` was dead data until the board widgets were painted from it: three colours that rode
    // along in ThemeSpec::extra. They are first-class tokens now, every shipped theme names them,
    // and the rules are the ones docs/SWITCHBOARD-AESTHETIC.md 3.1-3.4 set out. The one that
    // matters most is measured by everyShippedThemeKeepsItsTextLegible() above, which reads the
    // face as a fourth ground: a card row's text sits on the board, not on `background`.

    void everyShippedThemeWearsTheBoardMaterials() {
        const auto files = discoverThemeFiles({QStringLiteral("data/theme/themes")});
        QVERIFY(files.size() >= 5);
        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            const ThemeSpec spec = shipped(it.key());
            const QString id = it.key();
            const QColor face = spec.boardColor(QStringLiteral("face"));
            const QColor metal = spec.boardColor(QStringLiteral("metal"));
            const QColor dim = spec.boardColor(QStringLiteral("metal_dim"));
            for (const QColor &c : {face, metal, dim}) QVERIFY2(c.isValid(), qPrintable(id));
            // Every theme names its own: nothing here was computed, so nothing is in `derived`.
            QVERIFY2(!spec.derived.contains(QStringLiteral("board.face")),
                     qPrintable(id + QStringLiteral(" leaves [board] to the derivation")));
            QVERIFY2(spec.flag(QStringLiteral("board_material")),
                     qPrintable(id + QStringLiteral(" does not say board_material")));
            // The metal carries text: an engraved label beside a jack is drawn in it.
            const double onFace = contrast(metal, face);
            QVERIFY2(onFace >= 4.5, qPrintable(QStringLiteral("%1: metal %2 on face %3 is %4:1, needs 4.5:1")
                                                   .arg(id, metal.name(), face.name()).arg(onFace, 0, 'f', 2)));
            // Unlit hardware is a rule you can see and never a flag: visible against the face,
            // and a long way dimmer than the lit metal (the copper trap, THEMES.md 4.4).
            const double dimOnFace = contrast(dim, face);
            QVERIFY2(dimOnFace >= 1.4, qPrintable(QStringLiteral("%1: metal_dim %2 on face %3 is only %4:1")
                                                      .arg(id, dim.name(), face.name()).arg(dimOnFace, 0, 'f', 2)));
            QVERIFY2(dimOnFace < onFace * 0.75,
                     qPrintable(QStringLiteral("%1: metal_dim is %2:1 where the lit metal is %3:1 — not dim enough")
                                    .arg(id).arg(dimOnFace, 0, 'f', 2).arg(onFace, 0, 'f', 2)));
            // The board is a different object from the chrome around it, or there is no board.
            QVERIFY2(face != spec.uiColor(QStringLiteral("background")), qPrintable(id));
            QVERIFY2(face != spec.uiColor(QStringLiteral("surface")), qPrintable(id));
            // Structure, never state: the brass may not be mistaken for the amber that means
            // somebody is waiting on you (the same bar `tool` is held to).
            QVERIFY2(deltaE(metal, spec.uiColor(QStringLiteral("warning"))) >= 10.0,
                     qPrintable(QStringLiteral("%1: board metal %2 is dE %3 from the amber")
                                    .arg(id, metal.name())
                                    .arg(deltaE(metal, spec.uiColor(QStringLiteral("warning"))), 0, 'f', 1)));
        }
    }

    // A user theme names its palette and leaves the board alone. What it gets has to come from its
    // *own* colours: Relay Dark's bakelite is any dark window's colour again, and its brass is
    // 2.35:1 on white, so a borrow would hand a light theme an invisible board.
    void aThemeThatNamesNoBoardMaterialsDerivesThem() {
        QString error;
        const ThemeSpec paper = parseTheme(QStringLiteral(
            "[theme]\nname = \"Paper\"\nvariant = \"light\"\n"
            "[ui]\nbackground = \"#fdfdfb\"\nsurface = \"#f4f4f0\"\nsurface_raised = \"#e7e7e1\"\n"
            "border = \"#cfcfc8\"\nborder_strong = \"#8a8a82\"\ntext = \"#141414\"\n"
            "text_muted = \"#4f4f4a\"\naccent = \"#00558a\"\naccent_text = \"#ffffff\"\n"
            "warning = \"#7a5200\"\nerror = \"#9a1b1b\"\nsuccess = \"#1a6b3c\"\n"
            "[board]\nglow = \"#123456\"\n"),
            QStringLiteral("paper"), builtinDark(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const QColor face = paper.boardColor(QStringLiteral("face"));
        const QColor metal = paper.boardColor(QStringLiteral("metal"));
        const QColor dim = paper.boardColor(QStringLiteral("metal_dim"));
        // Named, not silently absent: the theme's author is told what was decided for them.
        for (const char *token : {"face", "metal", "metal_dim"})
            QVERIFY2(paper.derived.contains(QStringLiteral("board.") + QString::fromLatin1(token)),
                     qPrintable(QStringLiteral("board.%1 is not in derived: %2")
                                    .arg(QString::fromLatin1(token), paper.derived.join(QStringLiteral(", ")))));
        QVERIFY(paper.borrowed.filter(QStringLiteral("board.")).isEmpty());
        // Nothing came from Relay Dark.
        for (const QString &token : boardTokenNames())
            QVERIFY2(paper.boardColor(token) != builtinDark().boardColor(token),
                     qPrintable(QStringLiteral("board.%1 is Relay Dark's %2").arg(token, paper.boardColor(token).name())));
        // The face is a light theme's face: between its raised chip and its window, and paper.
        QVERIFY2(contrast(face, QColor(Qt::white)) < 1.6, qPrintable(face.name()));
        QVERIFY2(face != paper.uiColor(QStringLiteral("surface_raised")), qPrintable(face.name()));
        // Legible, by the same rules a shipped theme's board is held to.
        for (const char *token : {"text", "text_muted"}) {
            const double r = contrast(paper.uiColor(QString::fromLatin1(token)), face);
            QVERIFY2(r >= 4.5, qPrintable(QStringLiteral("%1 on the derived face is %2:1")
                                              .arg(QString::fromLatin1(token)).arg(r, 0, 'f', 2)));
        }
        QVERIFY2(contrast(metal, face) >= 4.5, qPrintable(QStringLiteral("metal %1 on face %2 is %3:1")
                                                              .arg(metal.name(), face.name())
                                                              .arg(contrast(metal, face), 0, 'f', 2)));
        // The metal is the theme's own brass (`ui.tool`, itself dulled out of its amber), and the
        // dim metal is that brass half way into the face.
        QVERIFY2(deltaE(metal, paper.uiColor(QStringLiteral("tool"))) < 10.0,
                 qPrintable(QStringLiteral("metal %1 is not this theme's brass %2")
                                .arg(metal.name(), paper.uiColor(QStringLiteral("tool")).name())));
        QVERIFY2(contrast(dim, face) < contrast(metal, face), qPrintable(dim.name()));
        // A key inside [board] that is not a material is still kept verbatim, as before.
        QCOMPARE(paper.extra.value(QStringLiteral("board.glow")), QStringList{QStringLiteral("#123456")});
        // A theme that does name them keeps exactly what it named.
        const ThemeSpec named = parseTheme(QStringLiteral(
            "[theme]\nname = \"Named\"\n[board]\nface = \"#101010\"\nmetal = \"#ddaa44\"\n"),
            QStringLiteral("named"), builtinDark(), &error);
        QCOMPARE(named.boardColor(QStringLiteral("face")).name(), QStringLiteral("#101010"));
        QCOMPARE(named.boardColor(QStringLiteral("metal")).name(), QStringLiteral("#ddaa44"));
        QVERIFY(named.derived.contains(QStringLiteral("board.metal_dim")));   // the one it left out
    }

    // The derivation is measured against five real palettes, not only the made-up one above: strip
    // the [board] table out of each shipped theme and the board it gets has to be as legible as the
    // one it ships. A theme author who copies a shipped file and deletes what they do not care
    // about is the common case, and this is the answer they get.
    void everyShippedThemeCouldDeriveItsBoardMaterials() {
        const auto files = discoverThemeFiles({QStringLiteral("data/theme/themes")});
        QVERIFY(!files.isEmpty());
        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            QString text = read(*it);
            // Drop the [board] table: from its header to the next one.
            const int start = text.indexOf(QStringLiteral("\n[board]"));
            QVERIFY2(start >= 0, qPrintable(it.key() + QStringLiteral(" has no [board] table")));
            const int next = text.indexOf(QStringLiteral("\n["), start + 1);
            text.remove(start, (next < 0 ? text.size() : next) - start);
            QString error;
            const ThemeSpec spec = parseTheme(text, it.key(), builtinDark(), &error);
            QVERIFY2(error.isEmpty(), qPrintable(it.key() + QStringLiteral(": ") + error));
            // Derived, not read: the table is gone (a comment elsewhere may still name it).
            for (const QString &token : boardTokenNames())
                QVERIFY2(spec.derived.contains(QStringLiteral("board.") + token),
                         qPrintable(it.key() + QStringLiteral(": board.") + token
                                    + QStringLiteral(" was not derived")));
            QStringList missing;
            QVERIFY2(isComplete(spec, &missing),
                     qPrintable(it.key() + QStringLiteral(": ") + missing.join(QStringLiteral(", "))));
            const QColor face = spec.boardColor(QStringLiteral("face"));
            const QColor metal = spec.boardColor(QStringLiteral("metal"));
            for (const char *token : {"text", "text_muted", "accent", "shell", "agent", "success",
                                      "warning", "error", "action", "tool", "link"}) {
                const double r = contrast(spec.uiColor(QString::fromLatin1(token)), face);
                QVERIFY2(r >= 4.5, qPrintable(QStringLiteral("%1: %2 on the derived face %3 is %4:1")
                                                  .arg(it.key(), QString::fromLatin1(token), face.name())
                                                  .arg(r, 0, 'f', 2)));
            }
            QVERIFY2(contrast(metal, face) >= 4.5,
                     qPrintable(QStringLiteral("%1: derived metal %2 on derived face %3 is %4:1")
                                    .arg(it.key(), metal.name(), face.name()).arg(contrast(metal, face), 0, 'f', 2)));
        }
    }
};

QTEST_MAIN(ThemeTests)
#include "theme_test.moc"
