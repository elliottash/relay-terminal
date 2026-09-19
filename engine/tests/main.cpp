// SPDX-License-Identifier: AGPL-3.0-or-later
// relay-engine-tests: runs every engine test object. Extra arguments are
// passed to QTest (e.g. `relay-engine-tests -maxwarnings 0`).
#include <QApplication>
#include <QtTest>

#include <memory>

QObject *makeCoreTest();
QObject *makeFaintInkTest();
QObject *makeFoldLayerTest();
QObject *makeFoldSearchTest();
QObject *makePtyTest();
QObject *makeSessionTest();
QObject *makeViewTest();

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    // RELAY_ENGINE_TEST=SessionTest runs one test object (so QTest function
    // filters on the command line apply to it only).
    const QByteArray only = qgetenv("RELAY_ENGINE_TEST");
    int failures = 0;
    for (auto make : {makeCoreTest, makeFaintInkTest, makeFoldLayerTest, makeFoldSearchTest, makePtyTest,
                      makeSessionTest, makeViewTest}) {
        std::unique_ptr<QObject> test(make());
        if (!only.isEmpty() && only != test->metaObject()->className())
            continue;
        failures += QTest::qExec(test.get(), argc, argv);
    }
    return failures == 0 ? 0 : 1;
}
