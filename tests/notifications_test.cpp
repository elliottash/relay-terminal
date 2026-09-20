// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Notifications.h"

#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using relay::Notification;
using relay::NotificationCenter;

class NotificationsTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminalTest"));
        QCoreApplication::setApplicationName(QStringLiteral("notifications-test"));
    }
    void init() { QSettings().clear(); NotificationCenter::instance().clear(); }

    void postsNewestFirstAndCounts() {
        auto &centre = NotificationCenter::instance();
        QSignalSpy changed(&centre, &NotificationCenter::changed);
        const QString first = centre.post(QStringLiteral("Command finished"), QStringLiteral("Exit 0"));
        const QString second = centre.post(QStringLiteral("Agent finished"), QStringLiteral("3 tool calls"),
                                           NotificationCenter::kindSuccess, QStringLiteral("pane-7"));
        QCOMPARE(changed.count(), 2);
        QVERIFY(first != second);
        const auto entries = centre.entries();
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.first().title, QStringLiteral("Agent finished"));   // newest first
        QCOMPARE(entries.first().source, QStringLiteral("pane-7"));
        QCOMPARE(entries.first().kind, NotificationCenter::kindSuccess);
        QCOMPARE(entries.last().title, QStringLiteral("Command finished"));
        QCOMPARE(centre.unseen(), 2);
    }

    void emptyTitleIsIgnored() {
        auto &centre = NotificationCenter::instance();
        QVERIFY(centre.post(QStringLiteral("   ")).isEmpty());
        QCOMPARE(centre.count(), 0);
    }

    void seenRemoveAndClear() {
        auto &centre = NotificationCenter::instance();
        centre.post(QStringLiteral("One"));
        const QString second = centre.post(QStringLiteral("Two"));
        QCOMPARE(centre.unseen(), 2);
        centre.markAllSeen();
        QCOMPARE(centre.unseen(), 0);
        QCOMPARE(centre.count(), 2);
        centre.post(QStringLiteral("Three"));
        QCOMPARE(centre.unseen(), 1);       // only the new one is unread
        centre.remove(second);
        QCOMPARE(centre.count(), 2);
        centre.remove(QStringLiteral("no-such-id"));
        QCOMPARE(centre.count(), 2);
        centre.clear();
        QCOMPARE(centre.count(), 0);
        QCOMPARE(centre.unseen(), 0);
    }

    void markSeenOneEntry() {
        // #NQP9: notifications.jump marks the entry it lands on, so the badge drops one at a
        // time rather than the popup's markAllSeen clearing the lot.
        auto &centre = NotificationCenter::instance();
        QSignalSpy changed(&centre, &NotificationCenter::changed);
        const QString first = centre.post(QStringLiteral("One"));
        const QString second = centre.post(QStringLiteral("Two"));
        QCOMPARE(centre.unseen(), 2);
        centre.markSeen(first);
        QCOMPARE(changed.count(), 3);              // two posts and the one mark
        QCOMPARE(centre.unseen(), 1);              // the badge drops by one, not to zero
        QVERIFY(!centre.entries().first().seen);   // "Two", the newest, still unread
        QVERIFY(centre.entries().last().seen);     // "One", the entry jumped to
        centre.markSeen(first);                    // already seen: no changed
        QCOMPARE(changed.count(), 3);
        centre.markSeen(QStringLiteral("no-such-id"));
        QCOMPARE(changed.count(), 3);
        QCOMPARE(centre.unseen(), 1);
        centre.markSeen(second);
        QCOMPARE(centre.unseen(), 0);
        QCOMPARE(changed.count(), 4);
    }

    void oldestEntriesAreDropped() {
        auto &centre = NotificationCenter::instance();
        for (int i = 0; i < NotificationCenter::kMaxEntries + 5; ++i)
            centre.post(QStringLiteral("Note %1").arg(i));
        QCOMPARE(centre.count(), NotificationCenter::kMaxEntries);
        QCOMPARE(centre.entries().first().title, QStringLiteral("Note %1").arg(NotificationCenter::kMaxEntries + 4));
        QCOMPARE(centre.entries().last().title, QStringLiteral("Note 5"));
    }

    void desktopSettingRoundTrips() {
        QVERIFY(NotificationCenter::desktopEnabled());      // on by default
        NotificationCenter::setDesktopEnabled(false);
        QVERIFY(!NotificationCenter::desktopEnabled());
        NotificationCenter::setDesktopEnabled(true);
        QVERIFY(NotificationCenter::desktopEnabled());
    }

    void relativeTimeReads() {
        // A fixed "now" so the test does not read differently at midnight.
        const QDateTime now(QDate(2026, 9, 17), QTime(15, 0));
        QCOMPARE(NotificationCenter::relativeTime(now, now), QStringLiteral("now"));
        QCOMPARE(NotificationCenter::relativeTime(now.addSecs(-120), now), QStringLiteral("2 min ago"));
        QCOMPARE(NotificationCenter::relativeTime(now.addSecs(-7200), now), QStringLiteral("2 h ago"));
        QCOMPARE(NotificationCenter::relativeTime(QDateTime(), now), QString());
        // Yesterday reads as a weekday and time, not "25 h ago".
        const QDateTime yesterday(QDate(2026, 9, 16), QTime(9, 15));
        QCOMPARE(NotificationCenter::relativeTime(yesterday, now), QStringLiteral("Wed 09:15"));
        const QDateTime lastMonth(QDate(2026, 8, 12), QTime(9, 15));
        QCOMPARE(NotificationCenter::relativeTime(lastMonth, now), QStringLiteral("12 Aug 09:15"));
    }
};

QTEST_MAIN(NotificationsTests)
#include "notifications_test.moc"
