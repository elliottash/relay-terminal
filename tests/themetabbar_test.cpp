// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ThemeTabBar.h"

#include <QTest>

class ThemeTabBarTest final : public QObject {
    Q_OBJECT

private slots:
    void middleClickReportsTheClickedTabOnly() {
        WindowTabWidget tabs;
        tabs.addTab(new QWidget(&tabs), QStringLiteral("First"));
        tabs.addTab(new QWidget(&tabs), QStringLiteral("Second"));
        tabs.resize(480, 200);
        tabs.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tabs));

        auto *bar = static_cast<ThemeTabBar *>(tabs.tabBar());
        int closed = -1;
        bar->onMiddleClick = [&closed](int index) { closed = index; };
        QTest::mouseClick(bar, Qt::MiddleButton, Qt::NoModifier, bar->tabRect(1).center());
        QCOMPARE(closed, 1);

        const QPoint empty(bar->rect().bottomRight() + QPoint(8, 8));
        QVERIFY(bar->tabAt(empty) < 0);
        closed = -1;
        QTest::mouseClick(bar, Qt::MiddleButton, Qt::NoModifier, empty);
        QCOMPARE(closed, -1);
    }
};

QTEST_MAIN(ThemeTabBarTest)
#include "themetabbar_test.moc"
