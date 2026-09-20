// SPDX-License-Identifier: AGPL-3.0-or-later
// The hot-path settings cache (card #057J). The promise is a count: a value read a thousand times
// builds one QSettings, and a write still takes effect at once. Uses a private XDG_CONFIG_HOME,
// so it never touches the real profile.
#include "Logging.h"
#include "PaneUsage.h"
#include "SettingsCache.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class SettingsCacheTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminalTest"));
        QCoreApplication::setApplicationName(QStringLiteral("relay-settingscache-test"));
        QVERIFY(m_home.isValid());
        qputenv("XDG_CONFIG_HOME", m_home.path().toUtf8());
        qputenv("XDG_DATA_HOME", m_home.path().toUtf8());
        QStandardPaths::setTestModeEnabled(false);
    }

    void init() { relay::settings::invalidate(); }

    // The whole point: reading the same key again and again touches the file exactly once.
    void aRepeatedReadBuildsOneQSettings() {
        QSettings().setValue(QStringLiteral("test/flag"), true);
        relay::settings::invalidate();
        const qint64 before = relay::settings::reads();
        for (int i = 0; i < 1000; ++i)
            QVERIFY(relay::settings::boolValue(QStringLiteral("test/flag"), false));
        QCOMPARE(relay::settings::reads() - before, qint64(1));
    }

    // A key that is not in the file caches its fallback too, or an unset setting — which is most
    // of them on a fresh profile — would be re-read for ever.
    void anUnsetKeyCachesItsFallback() {
        const qint64 before = relay::settings::reads();
        for (int i = 0; i < 100; ++i)
            QCOMPARE(relay::settings::stringValue(QStringLiteral("test/never_written"),
                                                  QStringLiteral("fallback")),
                     QStringLiteral("fallback"));
        QCOMPARE(relay::settings::reads() - before, qint64(1));
    }

    // …and the value is still the file's the moment somebody writes one.
    void aWriteTakesEffectAtOnce() {
        QSettings().setValue(QStringLiteral("test/flag"), true);
        relay::settings::invalidate();
        QVERIFY(relay::settings::boolValue(QStringLiteral("test/flag"), false));
        QSettings().setValue(QStringLiteral("test/flag"), false);
        relay::settings::invalidate();      // what SettingsWatch::notify() does for Options
        QVERIFY(!relay::settings::boolValue(QStringLiteral("test/flag"), false));
    }

    void invalidateMovesTheGenerationForDerivedValues() {
        const quint64 first = relay::settings::generation();
        QCOMPARE(relay::settings::generation(), first);
        relay::settings::invalidate();
        QVERIFY(relay::settings::generation() != first);
    }

    void intsAndStringsCacheToo() {
        QSettings().setValue(QStringLiteral("test/number"), 42);
        QSettings().setValue(QStringLiteral("test/word"), QStringLiteral("hello"));
        relay::settings::invalidate();
        const qint64 before = relay::settings::reads();
        for (int i = 0; i < 50; ++i) {
            QCOMPARE(relay::settings::intValue(QStringLiteral("test/number"), 0), 42);
            QCOMPARE(relay::settings::stringValue(QStringLiteral("test/word")), QStringLiteral("hello"));
        }
        QCOMPARE(relay::settings::reads() - before, qint64(2));
    }

    // The two callers the card names, through their own helpers: the status poll's boolean and
    // the level every log line is filtered against.
    void metersEnabledIsReadOnce() {
        QSettings().setValue(QStringLiteral("appearance/pane_usage"), true);
        relay::settings::invalidate();
        const qint64 before = relay::settings::reads();
        for (int i = 0; i < 500; ++i) QVERIFY(relay::usage::metersEnabled());
        QCOMPARE(relay::settings::reads() - before, qint64(1));
        // The Options toggle still switches the meters off for the next poll.
        QSettings().setValue(QStringLiteral("appearance/pane_usage"), false);
        relay::settings::invalidate();
        QVERIFY(!relay::usage::metersEnabled());
        QSettings().remove(QStringLiteral("appearance/pane_usage"));
        relay::settings::invalidate();
    }

    void logLevelIsReadOnce() {
        relay::log::setLevel(QStringLiteral("info"));
        const qint64 before = relay::settings::reads();
        for (int i = 0; i < 500; ++i) QCOMPARE(relay::log::level(), relay::log::Level::Info);
        QCOMPARE(relay::settings::reads() - before, qint64(1));
        // setLevel() invalidates for itself: nothing else is listening in this test.
        relay::log::setLevel(QStringLiteral("error"));
        QCOMPARE(relay::log::level(), relay::log::Level::Error);
    }

private:
    QTemporaryDir m_home;
};

QTEST_MAIN(SettingsCacheTest)
#include "settingscache_test.moc"
