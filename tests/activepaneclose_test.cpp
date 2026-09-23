// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ActivePaneClose.h"
#include <QApplication>
#include <QTest>
#include <QTimer>

class ActivePaneCloseTest : public QObject {
    Q_OBJECT
private slots:
    void escapeAndEnterCancel_data() {
        QTest::addColumn<int>("key");
        QTest::newRow("escape") << int(Qt::Key_Escape);
        QTest::newRow("default-enter") << int(Qt::Key_Return);
    }
    void escapeAndEnterCancel() {
        QFETCH(int, key);
        QTimer::singleShot(0, [key] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(box);
            QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel));
            QTest::keyClick(box, Qt::Key(key));
        });
        QCOMPARE(relay::paneclose::ask(nullptr), relay::paneclose::Choice::Cancel);
    }
    void choices_data() {
        QTest::addColumn<QString>("button");
        QTest::addColumn<int>("expected");
        QTest::newRow("stop") << QStringLiteral("closeStopJob") << int(relay::paneclose::Choice::Stop);
        QTest::newRow("background") << QStringLiteral("closeBackgroundJob") << int(relay::paneclose::Choice::Background);
    }
    void choices() {
        QFETCH(QString, button);
        QFETCH(int, expected);
        QTimer::singleShot(0, [button] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(box);
            QVERIFY(box->text().contains(QStringLiteral("2 panes")));
            auto *target = box->findChild<QPushButton *>(button);
            QVERIFY(target);
            QTest::mouseClick(target, Qt::LeftButton);
        });
        QCOMPARE(int(relay::paneclose::ask(nullptr, 2)), expected);
    }
    void dismissWindowCancels() {
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(box);
            box->close();
        });
        QCOMPARE(relay::paneclose::ask(nullptr), relay::paneclose::Choice::Cancel);
    }
};
QTEST_MAIN(ActivePaneCloseTest)
#include "activepaneclose_test.moc"
