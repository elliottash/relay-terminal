// SPDX-License-Identifier: GPL-3.0-or-later
// The live end of the theme (src/Theme.cpp): what the running application does with the id in
// `theme/name`, as opposed to what the reader does with a file (tests/theme_test.cpp).
//
// It exists for one question the owner's 2026-09-18 decision raised — "remove the solarized dark
// theme" — which is what happens to somebody who had already chosen it. The answer has to be that
// Relay comes up on the default theme with a whole palette, not on an empty one: `resolveTheme()`
// falls back to `defaultThemeId()` — Dark Copper since the owner's "use dark copper by default on
// all builds", the same day — then to `relay-dark`, then to the compiled-in theme if even that is
// missing. Nothing checked that before, because nothing had ever removed a theme.
#include "Theme.h"
#include "ThemeFile.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace relay::theme;

namespace {
// WCAG 2.1 relative luminance and contrast, the same arithmetic as tests/theme_test.cpp.
double channel(int v) { const double s = v / 255.0; return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4); }
double luminance(const QColor &c) { return 0.2126 * channel(c.red()) + 0.7152 * channel(c.green()) + 0.0722 * channel(c.blue()); }
double contrast(const QColor &a, const QColor &b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
}  // namespace

class ThemeSwitchTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() {
        QVERIFY(m_config.isValid());
        // Never the real profile: this writes theme/name.
        qputenv("XDG_CONFIG_HOME", m_config.path().toLocal8Bit());
        qputenv("RELAY_THEME_DIR", QByteArray(RELAY_SOURCE_DIR "/data/theme"));
        // The setting a user who had chosen Solarized Dark is still carrying.
        QSettings settings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("RelayTerminal"),
                           QStringLiteral("relay"));
        settings.setValue(QStringLiteral("theme/name"), QStringLiteral("solarized-dark"));
        settings.sync();
        QCOMPARE(settings.value(QStringLiteral("theme/name")).toString(), QStringLiteral("solarized-dark"));
    }

    // A tab switching to its own theme restyles without touching the stored default: that one is
    // what Relay opens on and what a new tab starts with (owner, 2026-09-19).
    void aTabsThemeDoesNotRewriteTheDefault() {
        QSettings settings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("RelayTerminal"),
                           QStringLiteral("relay"));
        const QString before = settings.value(QStringLiteral("theme/name")).toString();
        QVERIFY(setActiveTheme(QStringLiteral("ibm-beige"), false));
        QCOMPARE(activeThemeId(), QStringLiteral("ibm-beige"));
        settings.sync();
        QCOMPARE(settings.value(QStringLiteral("theme/name")).toString(), before);
        QCOMPARE(specFor(QStringLiteral("gruvbox-dark")).id, QStringLiteral("gruvbox-dark"));
        QVERIFY(specFor(QStringLiteral("no-such-theme")).id != QStringLiteral("no-such-theme"));
        QCOMPARE(activeThemeId(), QStringLiteral("ibm-beige"));   // looking one up activates nothing
    }

    // The tick in a checked box is drawn on the accent, so its ink follows the accent: the dark
    // glyph on copper, the light one on IBM Beige's navy, where the dark one could not be seen
    // (owner, 2026-09-19). A light theme also gets the darker chevrons. Both files must exist.
    void theTickAndTheChevronsTakeTheInkTheirGroundNeeds() {
        QVERIFY(setActiveTheme(QStringLiteral("ibm-beige"), false));
        QString css = qApp->styleSheet();
        QVERIFY2(css.contains(QStringLiteral("/icons/check-light.svg")), "Beige's navy accent still gets the dark tick");
        QVERIFY(!css.contains(QStringLiteral("/icons/check.svg")));
        QVERIFY(css.contains(QStringLiteral("/icons/chevron-down-dark.svg")));
        QVERIFY(setActiveTheme(QStringLiteral("dark-copper"), false));
        css = qApp->styleSheet();
        QVERIFY2(css.contains(QStringLiteral("/icons/check.svg")), "copper is a light accent: the dark tick");
        QVERIFY(!css.contains(QStringLiteral("/icons/check-light.svg")));
        QVERIFY(css.contains(QStringLiteral("/icons/chevron-down.svg")));
        for (const char *name : {"check-light.svg", "chevron-down-dark.svg", "chevron-up-dark.svg"})
            QVERIFY2(QFile::exists(QStringLiteral(RELAY_SOURCE_DIR "/data/theme/icons/") + QString::fromLatin1(name)), name);
    }

    // The pane's button row is in every pane at all times: nothing appears, lifts or rearranges
    // under the pointer (1b270ef). What went with the hover row was the raised tile and the outline
    // it carried, so the permanent row read as three grey glyphs floating on the header — "the
    // permanent pane icons should use the brighter outline that we had with the dynamic pane icons"
    // (owner, card #0T2R). The tile is back, permanently. `@raised` is the top of the ground stack,
    // so a hovered button cannot lift off the row by ground — `background: @surface` would now make
    // it darker than the row it sits on — and lifts by ink and a stronger outline instead.
    void thePaneButtonRowKeepsTheTileAndTheOutline() {
        for (const char *id : {"dark-copper", "ibm-beige", "relay-dark"}) {
            QVERIFY2(setActiveTheme(QString::fromLatin1(id), false), id);
            const QString css = qApp->styleSheet();
            // The radius is the theme's own (IBM Beige is square), the tile and the outline are not.
            QVERIFY2(css.contains(QStringLiteral("QFrame#paneChrome { background: %1; border: 1px solid %2; "
                                                 "border-radius:").arg(SurfaceRaised.name(), Border.name())), id);
            QVERIFY2(css.contains(QStringLiteral("QToolButton#paneChromeButton:hover { color: %1; border-color: %2; }")
                                      .arg(Text.name(), BorderStrong.name())), id);
            // A hovered button must not take a ground of its own: on this tile any of them is darker.
            QVERIFY2(!css.contains(QStringLiteral("QToolButton#paneChromeButton:hover { color: %1; border-color: %2; "
                                                  "background:").arg(Text.name(), BorderStrong.name())), id);
            // A metal or plastic theme gives the tile the face it gives every other raised chip,
            // rather than leaving it the one flat rectangle in the window.
            QVERIFY2(!css.contains(QStringLiteral("QToolButton#workChip, QMenu,")), id);
        }
    }

    void theThemeItAsksForIsGone() {
        for (const ThemeChoice &choice : availableThemes())
            QVERIFY2(choice.id != QStringLiteral("solarized-dark"), "solarized-dark is still on offer");
        QVERIFY(availableThemes().size() >= 5);
        // Choosing it by id is refused rather than half-applied.
        QVERIFY(!setActiveTheme(QStringLiteral("solarized-dark")));
    }

    // The thing that must not happen: a start-up that leaves the tokens, the palette and the
    // stylesheet on whatever they happened to hold.
    void aStaleThemeNameStartsOnTheDefault() {
        QCOMPARE(defaultThemeId(), QStringLiteral("dark-copper"));
        auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
        QVERIFY(app);
        applyTheme(*app);
        QCOMPARE(activeThemeId(), QStringLiteral("dark-copper"));
        QCOMPARE(active().name, QStringLiteral("Dark Copper"));
        QVERIFY(isComplete(active()));
        // A whole palette, not an empty one: the tokens are Dark Copper's own values.
        QCOMPARE(Background.name(), QStringLiteral("#0e0f12"));
        QCOMPARE(Text.name(), QStringLiteral("#ece6e0"));
        QCOMPARE(Action.name(), QStringLiteral("#e56a30"));
        QCOMPARE(app->palette().color(QPalette::Window).name(), Background.name());
        QCOMPARE(app->palette().color(QPalette::WindowText).name(), Text.name());
        QVERIFY(app->styleSheet().size() > 1000);
        QVERIFY(!app->styleSheet().contains(QStringLiteral("@bg")));   // every token was substituted
        // The fallback is not written back: the setting is still the user's own until they pick
        // again, so a theme folder that returns (a user theme of that id) is picked up as before.
        QSettings settings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("RelayTerminal"),
                           QStringLiteral("relay"));
        QCOMPARE(settings.value(QStringLiteral("theme/name")).toString(), QStringLiteral("solarized-dark"));
    }

    // A profile that never chose a theme — every fresh install, on every build — starts on Dark
    // Copper, and nothing is written for it: the default is a default, not a choice made for them.
    void aProfileThatNeverChoseStartsOnDarkCopper() {
        QSettings settings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("RelayTerminal"),
                           QStringLiteral("relay"));
        const QString before = settings.value(QStringLiteral("theme/name")).toString();
        settings.remove(QStringLiteral("theme/name"));
        settings.sync();
        auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
        applyTheme(*app);
        QCOMPARE(activeThemeId(), QStringLiteral("dark-copper"));
        QCOMPARE(Background.name(), QStringLiteral("#0e0f12"));
        settings.sync();
        QVERIFY(!settings.contains(QStringLiteral("theme/name")));
        settings.setValue(QStringLiteral("theme/name"), before);
        settings.sync();
    }

    // Owner, 2026-09-18: "make actions red-orange". The Actions colour is a live token like the
    // rest — it follows a theme switch, so the band, the glyph and the title-bar button that read
    // it at paint time follow too.
    void theActionsColourFollowsTheTheme() {
        QVERIFY(setActiveTheme(QStringLiteral("relay-dark")));
        QCOMPARE(Action.name(), QStringLiteral("#e5844f"));
        QVERIFY(setActiveTheme(QStringLiteral("ibm-beige")));
        QCOMPARE(Action.name(), QStringLiteral("#803700"));
        QVERIFY(setActiveTheme(QStringLiteral("dark-copper")));
        QCOMPARE(Action.name(), QStringLiteral("#e56a30"));
        QVERIFY(setActiveTheme(QStringLiteral("relay-dark")));
    }

    // A user theme in ~/.config/relay/themes that says nothing about Actions still gets a
    // red-orange, and one made out of its own red rather than Relay Dark's.
    void aUserThemeWithoutAnActionColourStillGetsOne() {
        const QString dir = m_config.path() + QStringLiteral("/relay/themes");
        QVERIFY(QDir().mkpath(dir));
        QFile file(dir + QStringLiteral("/paper.toml"));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(
            "[theme]\nname = \"Paper\"\nvariant = \"light\"\n"
            "[ui]\nbackground = \"#ffffff\"\nsurface = \"#f4f4f4\"\nsurface_raised = \"#e8e8e8\"\n"
            "border = \"#cccccc\"\nborder_strong = \"#888888\"\ntext = \"#111111\"\n"
            "text_muted = \"#555555\"\naccent = \"#005577\"\naccent_text = \"#ffffff\"\nerror = \"#a11020\"\n");
        file.close();
        refreshThemes();
        QVERIFY(setActiveTheme(QStringLiteral("paper")));
        QVERIFY(Action.isValid());
        QVERIFY2(Action != QColor(QStringLiteral("#e5844f")), qPrintable(Action.name()));
        const int hue = Action.toHsv().hsvHue();
        QVERIFY2(hue >= 12 && hue <= 30, qPrintable(QStringLiteral("%1 is at hue %2").arg(Action.name()).arg(hue)));
        QVERIFY(setActiveTheme(QStringLiteral("relay-dark")));
    }

    // The Switchboard's materials are live tokens like the rest (owner, 2026-09-19: "yeah build
    // that out"): the board pane's face is a stylesheet rule and its brass is read at paint time by
    // RowDelegate and the empty board, so a theme switch has to move all three.
    void theBoardMaterialsFollowTheTheme() {
        auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
        QVERIFY(app);
        QVERIFY(setActiveTheme(QStringLiteral("relay-dark")));
        QVERIFY(BoardMaterial);
        QCOMPARE(BoardFace.name(), QStringLiteral("#17140f"));
        QCOMPARE(BoardMetal.name(), QStringLiteral("#c8a45c"));
        QCOMPARE(BoardMetalDim.name(), QStringLiteral("#6b5637"));
        QVERIFY2(app->styleSheet().contains(QStringLiteral("QWidget#boardView { background: #17140f; }")),
                 "the board pane is not painted on the board's face");
        QVERIFY(setActiveTheme(QStringLiteral("ibm-beige")));
        QCOMPARE(BoardFace.name(), QStringLiteral("#e0d6bd"));
        QCOMPARE(BoardMetal.name(), QStringLiteral("#63492b"));
        QCOMPARE(BoardMetalDim.name(), QStringLiteral("#8a7550"));
        QVERIFY(app->styleSheet().contains(QStringLiteral("QWidget#boardView { background: #e0d6bd; }")));
        // `@boardMetalDim` has to be substituted before `@boardMetal`, or the dim rule comes out as
        // the lit colour with "Dim" left after it. Nothing may be left unsubstituted either way.
        QVERIFY(app->styleSheet().contains(QStringLiteral("border-bottom: 1px solid #8a7550")));
        QVERIFY(!app->styleSheet().contains(QStringLiteral("@board")));
        QVERIFY(setActiveTheme(QStringLiteral("dark-copper")));
        QCOMPARE(BoardFace.name(), QStringLiteral("#1a1210"));
        QCOMPARE(BoardMetal.name(), QStringLiteral("#c08556"));
        QVERIFY(setActiveTheme(QStringLiteral("relay-dark")));
    }

    // `[flags] board_material = false` (docs/SWITCHBOARD-AESTHETIC.md 3.4): the board degrades to
    // hairlines — the face becomes the text surface, lit hardware the accent, unlit hardware the
    // resting border — and the substitution is made once, in adoptTokens(), so no painter and no
    // stylesheet rule has to know which form the theme asked for. Nothing moves, only fills.
    void aThemeThatRefusesTheMaterialGetsHairlines() {
        const QString dir = m_config.path() + QStringLiteral("/relay/themes");
        QVERIFY(QDir().mkpath(dir));
        QFile file(dir + QStringLiteral("/plainboard.toml"));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(
            "[theme]\nname = \"Plain Board\"\nvariant = \"dark\"\n"
            "[ui]\nbackground = \"#101014\"\nsurface = \"#181820\"\nsurface_raised = \"#20202a\"\n"
            "border = \"#2a2a34\"\nborder_strong = \"#4a4a58\"\ntext = \"#e8e8ee\"\n"
            "text_muted = \"#9a9aa6\"\naccent = \"#4ab8e0\"\naccent_text = \"#04161e\"\n"
            "[board]\nface = \"#2b1d10\"\nmetal = \"#d8a24a\"\nmetal_dim = \"#7a5b2c\"\n"
            "[flags]\nboard_material = false\n");
        file.close();
        refreshThemes();
        QVERIFY(setActiveTheme(QStringLiteral("plainboard")));
        QVERIFY(!BoardMaterial);
        // Its own [board] colours are still read — the theme may turn the material back on — but
        // nothing paints them while the flag is false.
        QCOMPARE(active().boardColor(QStringLiteral("face")).name(), QStringLiteral("#2b1d10"));
        QCOMPARE(BoardFace.name(), Surface.name());
        QCOMPARE(BoardMetal.name(), Accent.name());
        QCOMPARE(BoardMetalDim.name(), Border.name());
        auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
        QVERIFY(app->styleSheet().contains(QStringLiteral("QWidget#boardView { background: ")
                                           + Surface.name() + QStringLiteral("; }")));
        QVERIFY(setActiveTheme(QStringLiteral("relay-dark")));
        QVERIFY(BoardMaterial);
    }

    // Which pane is the active one has to be readable at a glance (owner, 2026-09-19: "its too hard
    // to tell what is the active pane"). Two cues carry it in every theme: the pane's own outline
    // goes from `border` to `text_muted`, and the pane's name from `text_muted` to `text`. Both are
    // grey on purpose — the accent means "shell" in Relay's visual language, so a pane frame must
    // not compete with the composer (data/theme/themes/relay-dark.toml, `border_strong`).
    //
    // The rules are checked against the live tokens, and then the tokens themselves: a theme whose
    // `text_muted` sat near its own background would put the cue back where the complaint found it,
    // and the sheet would still be spelled correctly. 3:1 is WCAG's floor for a non-text mark.
    void theActivePaneIsVisiblyTheActiveOne() {
        for (const char *id : {"relay-dark", "relay-light", "dark-copper", "gruvbox-dark", "ibm-beige"}) {
            QVERIFY2(setActiveTheme(QString::fromLatin1(id), false), id);
            const QString css = qApp->styleSheet();
            QVERIFY2(css.contains(QStringLiteral("QWidget#pane { background: %1; border: 1px solid %2;")
                                      .arg(Background.name(), Border.name())), id);
            QVERIFY2(css.contains(QStringLiteral("QWidget#pane[relayActive=\"true\"] { border: 1px solid %1; }")
                                      .arg(TextMuted.name())), id);
            QVERIFY2(css.contains(QStringLiteral("QLabel#paneTitle { color: %1; font-weight: 600; }")
                                      .arg(TextMuted.name())), id);
            QVERIFY2(css.contains(QStringLiteral("QLabel#paneTitle[relayActive=\"true\"] { color: %1; }")
                                      .arg(Text.name())), id);
            // The path beside the name is the pane's address, not a focus mark: it stays legible in
            // both states rather than dimming with the rest of the header.
            QVERIFY2(css.contains(QStringLiteral("QLabel#paneCwd { color: %1; font-size: 9pt; }").arg(TextMuted.name())), id);
            QVERIFY2(!css.contains(QStringLiteral("QLabel#paneCwd[relayActive")), id);

            const QString why = QStringLiteral("%1: active outline %2 on %3 is %4:1, and the resting one is %5")
                                    .arg(QString::fromLatin1(id), TextMuted.name(), Background.name())
                                    .arg(contrast(TextMuted, Background), 0, 'f', 2).arg(Border.name());
            QVERIFY2(TextMuted != Border, qPrintable(why));
            QVERIFY2(contrast(TextMuted, Background) >= 3.0, qPrintable(why));
            // The live outline has to be the louder of the two, whichever way the theme runs.
            QVERIFY2(contrast(TextMuted, Background) > contrast(Border, Background), qPrintable(why));
            QVERIFY2(contrast(Text, Background) > contrast(TextMuted, Background), qPrintable(why));
        }
        // A bevelled theme redraws the frame as two-tone and keeps the cue: its own 2px override.
        QVERIFY(setActiveTheme(QStringLiteral("ibm-beige"), false));
        QVERIFY(active().flag(QStringLiteral("bevel")));
        QVERIFY(qApp->styleSheet().contains(
            QStringLiteral("QWidget#pane[relayActive=\"true\"] { border: 2px solid %1; }").arg(TextMuted.name())));
    }

private:
    QTemporaryDir m_config;
};

QTEST_MAIN(ThemeSwitchTest)
#include "themeswitch_test.moc"
