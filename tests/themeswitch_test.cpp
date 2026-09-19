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
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace relay::theme;

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

private:
    QTemporaryDir m_config;
};

QTEST_MAIN(ThemeSwitchTest)
#include "themeswitch_test.moc"
