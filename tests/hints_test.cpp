// SPDX-License-Identifier: GPL-3.0-or-later
#include "Hints.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

using relay::ShortcutHints;

class HintsTests : public QObject {
    Q_OBJECT
    // Move the global gap and per-hint timestamps into the past, as if time passed.
    static void age(qint64 seconds) {
        QSettings settings;
        for (const QString &key : settings.allKeys())
            if (key == QStringLiteral("hints/last_any") || key.startsWith(QStringLiteral("hints/last/")))
                settings.setValue(key, settings.value(key).toLongLong() - seconds);
    }
private Q_SLOTS:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminalTest"));
        QCoreApplication::setApplicationName(QStringLiteral("hints-test"));
    }
    void init() { QSettings().clear(); }

    void limitGapAndCooldown() {
        auto &hints = ShortcutHints::instance();
        QVERIFY(hints.shouldShow(QStringLiteral("a"), 2, 60));
        QVERIFY(!hints.shouldShow(QStringLiteral("b")));          // global gap
        age(ShortcutHints::kGlobalGapSeconds + 1);
        QVERIFY(!hints.shouldShow(QStringLiteral("a"), 2, 60));   // per-hint cooldown
        QVERIFY(hints.shouldShow(QStringLiteral("b")));
        age(61);
        QVERIFY(hints.shouldShow(QStringLiteral("a"), 2, 60));
        age(1000);
        QVERIFY(!hints.shouldShow(QStringLiteral("a"), 2, 60));   // limit reached
        QCOMPARE(hints.shownCount(QStringLiteral("a")), 2);
        hints.resetAll();
        QCOMPARE(hints.shownCount(QStringLiteral("a")), 0);
        QVERIFY(hints.shouldShow(QStringLiteral("a"), 2, 60));
    }

    void disabled() {
        auto &hints = ShortcutHints::instance();
        hints.setEnabled(false);
        QVERIFY(!hints.shouldShow(QStringLiteral("a")));
        hints.setEnabled(true);
        QVERIFY(hints.shouldShow(QStringLiteral("a")));
    }

    void nextTimeAndIdleTips() {
        QCOMPARE(ShortcutHints::nextTime(QString(), QStringLiteral("x")), QString());
        QCOMPARE(ShortcutHints::nextTime(QStringLiteral("Ctrl+T"), QStringLiteral("new tab")), QStringLiteral("Next time: Ctrl+T · new tab"));
        auto &hints = ShortcutHints::instance();
        const QList<ShortcutHints::Tip> tips{{QStringLiteral("t1"), QStringLiteral("one")}, {QStringLiteral("t2"), QStringLiteral("two")}};
        QCOMPARE(hints.nextIdleTip(tips).id, QStringLiteral("t1"));
        QVERIFY(hints.nextIdleTip(tips).id.isEmpty());            // global gap
        age(2000);
        QCOMPARE(hints.nextIdleTip(tips).id, QStringLiteral("t2"));
    }
};

QTEST_MAIN(HintsTests)
#include "hints_test.moc"
