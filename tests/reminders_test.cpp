// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Reminders.h"
#include <QTemporaryDir>
#include <QTest>

class RemindersTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void survivesRestartAndDeliversOnce() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString path = temp.filePath(QStringLiteral("reminders.json"));
        const QDateTime now = QDateTime::currentDateTimeUtc();
        relay::Reminder item;
        QString error;
        {
            relay::Reminders first(path);
            QVERIFY2(first.add(QStringLiteral("Review the logs"), now.addSecs(3600), &item, &error), qPrintable(error));
        }
        int fired = 0;
        relay::Reminders reopened(path);
        reopened.onDue([&](const relay::Reminder &due) {
            QCOMPARE(due.id, item.id);
            QCOMPARE(due.text, item.text);
            ++fired;
        });
        QList<relay::Reminder> pending;
        QVERIFY2(reopened.list(&pending, &error), qPrintable(error));
        QCOMPARE(pending.size(), 1);
        reopened.checkDue(now.addSecs(3601));
        QCOMPARE(fired, 1);
        reopened.checkDue(now.addSecs(3602));
        QCOMPARE(fired, 1);
        relay::Reminders anotherProcess(path);
        anotherProcess.onDue([&](const relay::Reminder &) { ++fired; });
        anotherProcess.checkDue(now.addSecs(3603));
        QCOMPARE(fired, 1);
        QVERIFY(reopened.list(&pending, &error));
        QCOMPARE(pending.size(), 0);
    }

    void cancelAndRejectBadInput() {
        QTemporaryDir temp;
        relay::Reminders reminders(temp.filePath(QStringLiteral("reminders.json")));
        QString error;
        relay::Reminder item;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QVERIFY(!reminders.add(QStringLiteral(" "), now.addSecs(60), &item, &error));
        QVERIFY(!reminders.add(QStringLiteral("Past"), now.addSecs(-60), &item, &error));
        QVERIFY(reminders.add(QStringLiteral("Cancel me"), now.addSecs(60), &item, &error));
        QVERIFY2(reminders.cancel(item.id, &error), qPrintable(error));
        QVERIFY(!reminders.cancel(item.id, &error));
        QList<relay::Reminder> pending;
        QVERIFY(reminders.list(&pending, &error));
        QVERIFY(pending.isEmpty());
    }
};

QTEST_GUILESS_MAIN(RemindersTests)
#include "reminders_test.moc"
