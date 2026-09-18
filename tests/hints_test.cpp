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
        const auto first = hints.nextIdleTip(tips);
        QCOMPARE(first.id, QStringLiteral("t1"));
        hints.recordShown(first.id);                              // the caller put it on screen
        QVERIFY(hints.nextIdleTip(tips).id.isEmpty());            // global gap
        age(2000);
        QCOMPARE(hints.nextIdleTip(tips).id, QStringLiteral("t2"));
    }

    // A hint counts as shown only when it reaches the screen: asking costs nothing, however often.
    void checkingRecordsNothing() {
        auto &hints = ShortcutHints::instance();
        for (int i = 0; i < 5; ++i) QVERIFY(hints.mayShow(QStringLiteral("a"), 2, 60));
        QCOMPARE(hints.shownCount(QStringLiteral("a")), 0);
        QVERIFY(hints.mayShow(QStringLiteral("b")));              // no global gap started either
        hints.recordShown(QStringLiteral("a"));
        QCOMPARE(hints.shownCount(QStringLiteral("a")), 1);
        QVERIFY(!hints.mayShow(QStringLiteral("b")));             // now the global gap holds
        QVERIFY(!hints.mayShow(QStringLiteral("a"), 2, 60));
        age(ShortcutHints::kGlobalGapSeconds + 1);
        QVERIFY(!hints.mayShow(QStringLiteral("a"), 2, 60));      // per-hint cooldown
        QVERIFY(hints.mayShow(QStringLiteral("b")));
        age(61);
        hints.recordShown(QStringLiteral("a"));
        age(1000);
        QVERIFY(!hints.mayShow(QStringLiteral("a"), 2, 60));      // limit reached
        // A tip picked but never drawn leaves the next pick free.
        const QList<ShortcutHints::Tip> tips{{QStringLiteral("t1"), QStringLiteral("one")}};
        QCOMPARE(hints.nextIdleTip(tips).id, QStringLiteral("t1"));
        QCOMPARE(hints.nextIdleTip(tips).id, QStringLiteral("t1"));
        QCOMPARE(hints.shownCount(QStringLiteral("t1")), 0);
    }
};

QTEST_MAIN(HintsTests)
#include "hints_test.moc"
