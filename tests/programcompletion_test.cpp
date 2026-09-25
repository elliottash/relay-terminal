// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ProgramCompletion.h"
#include <QTest>

class ProgramCompletionTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void pythonAndIPython() {
        QCOMPARE(relay::completeProgram("python3", "pri", 3).inserts, QStringList{"print"});
        QVERIFY(relay::completeProgram("/usr/bin/python3.12", "im", 2).inserts.contains("import"));
        QVERIFY(relay::completeProgram("ipython", "%tim", 4).inserts.contains("%timeit"));
        QVERIFY(relay::completeProgram("python3", "%tim", 4).inserts.isEmpty());
    }
    void databaseAndNode() {
        QVERIFY(relay::completeProgram("psql", "\\d", 2).inserts.contains("\\dt"));
        QCOMPARE(relay::completeProgram("sqlite3", ".sch", 4).inserts, QStringList{".schema"});
        QCOMPARE(relay::completeProgram("node", ".ex", 3).inserts, QStringList{".exit"});
    }
    void respectsCursorAndUnknownProgram() {
        const auto match = relay::completeProgram("python3", "x = pri()", 7);
        QCOMPARE(match.start, 4);
        QCOMPARE(match.length, 3);
        QCOMPARE(match.inserts, QStringList{"print"});
        QVERIFY(relay::completeProgram("vim", ":q", 2).inserts.isEmpty());
    }
};
QTEST_GUILESS_MAIN(ProgramCompletionTests)
#include "programcompletion_test.moc"
