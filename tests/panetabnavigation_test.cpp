#include "PaneTabNavigation.h"

#include <QLineEdit>
#include <QMenu>
#include <QTest>
#include <QVBoxLayout>

class PaneTabNavigationTest : public QObject {
    Q_OBJECT
private:
    struct Fixture {
        QWidget host;
        QVBoxLayout layout{&host};
        QTabBar bar;
        QLineEdit search;
        Fixture() {
            layout.addWidget(&bar);
            layout.addWidget(&search);
            bar.addTab("Projects"); bar.addTab("Sessions"); bar.addTab("Globals");
            relay::paneTabs::registerTabs(&host, &bar);
            host.show();
            QApplication::processEvents();
        }
    };
    static bool key(QWidget *focus, int key = Qt::Key_Tab,
                    Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                    QEvent::Type type = QEvent::KeyPress) {
        QKeyEvent event(type, key, modifiers);
        return relay::paneTabs::handle(focus, &event);
    }
private slots:
    void wrapsAndShortcutOverrideDoesNotMove() {
        Fixture f;
        QVERIFY(key(&f.search, Qt::Key_Tab, Qt::NoModifier, QEvent::ShortcutOverride));
        QCOMPARE(f.bar.currentIndex(), 0);
        QVERIFY(key(&f.search)); QCOMPARE(f.bar.currentIndex(), 1);
        QVERIFY(key(&f.search)); QCOMPARE(f.bar.currentIndex(), 2);
        QVERIFY(key(&f.search)); QCOMPARE(f.bar.currentIndex(), 0);
        QVERIFY(key(&f.search, Qt::Key_Backtab, Qt::ShiftModifier));
        QCOMPARE(f.bar.currentIndex(), 2);
        QVERIFY(key(&f.search, Qt::Key_Tab, Qt::ShiftModifier));
        QCOMPARE(f.bar.currentIndex(), 1);
        QVERIFY(key(&f.search, Qt::Key_Backtab)); QCOMPARE(f.bar.currentIndex(), 0);
    }
    void skipsHiddenAndDisabledTabs() {
        Fixture f;
        f.bar.addTab("Extra");
        f.bar.setTabEnabled(1, false); f.bar.setTabVisible(2, false);
        QVERIFY(key(&f.search)); QCOMPARE(f.bar.currentIndex(), 3);
        QVERIFY(key(&f.search)); QCOMPARE(f.bar.currentIndex(), 0);
        f.bar.setTabEnabled(3, false);
        QVERIFY(!key(&f.search));
        f.bar.setTabEnabled(3, true); f.bar.hide();
        QVERIFY(!key(&f.search));
        f.bar.show(); f.bar.setEnabled(false);
        QVERIFY(!key(&f.search));
    }
    void nearestEligibleHostAndDeletedBar() {
        Fixture outer;
        auto *inner = new QWidget(&outer.host);
        auto *layout = new QVBoxLayout(inner);
        auto *bar = new QTabBar(inner);
        auto *search = new QLineEdit(inner);
        layout->addWidget(bar); layout->addWidget(search);
        outer.layout.addWidget(inner);
        bar->addTab("One"); bar->addTab("Two");
        relay::paneTabs::registerTabs(inner, bar);
        inner->show(); QApplication::processEvents();
        QVERIFY(key(search)); QCOMPARE(bar->currentIndex(), 1);
        QCOMPARE(outer.bar.currentIndex(), 0);
        bar->hide();
        QVERIFY(key(search)); QCOMPARE(outer.bar.currentIndex(), 1);
        delete bar;
        QVERIFY(key(search)); QCOMPARE(outer.bar.currentIndex(), 2);
    }
    void leavesOtherShortcutsAlone() {
        Fixture f;
        QVERIFY(!key(&f.search, Qt::Key_Tab, Qt::ControlModifier));
        QVERIFY(!key(&f.search, Qt::Key_Backtab, Qt::ControlModifier | Qt::ShiftModifier));
        QVERIFY(!key(&f.search, Qt::Key_Tab, Qt::AltModifier));
        QVERIFY(!key(&f.search, Qt::Key_1, Qt::AltModifier));
        QVERIFY(!key(&f.search, Qt::Key_Tab, Qt::NoModifier, QEvent::KeyRelease));
        QCOMPARE(f.bar.currentIndex(), 0);
    }
    void reservesConsoleAndEditors() {
        Fixture f;
        QWidget console(&f.host);
        QLineEdit composer(&console);
        console.setProperty("paneTabKeysReserved", true);
        QVERIFY(!key(&composer));
        QVERIFY(!key(&composer, Qt::Key_Backtab, Qt::ShiftModifier));
        QPlainTextEdit plain(&f.host);
        QTextEdit rich(&f.host);
        QVERIFY(!key(plain.viewport())); QVERIFY(!key(rich.viewport()));
        plain.setReadOnly(true); rich.setReadOnly(true);
        QVERIFY(key(plain.viewport())); QVERIFY(key(rich.viewport()));
        // An exclusion outside the nearest host still takes precedence.
        f.host.setProperty("paneTabKeysReserved", true);
        QVERIFY(!key(&f.search));
    }
    void popupKeepsNavigation() {
        Fixture f;
        QWidget completion(&f.host, Qt::Popup);
        QLineEdit entry(&completion);
        QVERIFY(!key(&entry));
        QMenu menu;
        menu.addAction("completion");
        menu.popup(f.host.mapToGlobal(QPoint(0, 0)));
        QApplication::processEvents();
        QVERIFY(QApplication::activePopupWidget());
        QVERIFY(!key(&f.search));
        menu.hide();
        QVERIFY(key(&f.search));
    }
};
QTEST_MAIN(PaneTabNavigationTest)
#include "panetabnavigation_test.moc"
