// SPDX-License-Identifier: GPL-3.0-or-later
// relay-engine-tests: runs every engine test object. Extra arguments are
// passed to QTest (e.g. `relay-engine-tests -maxwarnings 0`).
#include <QApplication>
#include <QtTest>

#include <memory>

QObject *makeCoreTest();
QObject *makePtyTest();
QObject *makeSessionTest();
QObject *makeViewTest();

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    int failures = 0;
    for (auto make : {makeCoreTest, makePtyTest, makeSessionTest, makeViewTest}) {
        std::unique_ptr<QObject> test(make());
        failures += QTest::qExec(test.get(), argc, argv);
    }
    return failures == 0 ? 0 : 1;
}
