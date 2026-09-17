// SPDX-License-Identifier: GPL-3.0-or-later
// TerminalSession: threaded parsing, signals on the GUI thread, display injection.
#include "session/TerminalSession.h"

#include <QSignalSpy>
#include <QThread>
#include <QtTest>

using namespace relay;

class SessionTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase_data()
    {
        QTest::addColumn<QString>("core");
        for (const QString &c : availableVtCores())
            QTest::newRow(qPrintable(c)) << c;
    }

    void runsProgramAndDeliversSignals()
    {
        QFETCH_GLOBAL(QString, core);
        TerminalSession s(core);
        QCOMPARE(s.coreName(), core);
        s.resize(10, 60, 8, 16);
        QSignalSpy finished(&s, &TerminalSession::finished);
        QSignalSpy titles(&s, &TerminalSession::titleChanged);
        QSignalSpy alt(&s, &TerminalSession::altScreenChanged);
        QSignalSpy marks(&s, &TerminalSession::promptMark);
        QSignalSpy cwd(&s, &TerminalSession::cwdChanged);
        const QThread *guiThread = QThread::currentThread();
        bool wrongThread = false;
        connect(&s, &TerminalSession::contentChanged, this, [&] { wrongThread |= QThread::currentThread() != guiThread; });

        TerminalSession::StartOptions o;
        o.program = QStringLiteral("/bin/sh");
        o.arguments = QStringList{QStringLiteral("-c"),
                       QStringLiteral("printf '\\033]2;session title\\007\\033]7;file:///tmp\\007\\033]133;A\\007'; "
                                      "printf 'TERM=%s\\n' \"$TERM\"; printf '\\033[?1049h'; printf '\\033[?1049l'; exit 5")};
        QVERIFY2(s.start(o), qPrintable(s.errorString()));
        QVERIFY(s.shellPid() > 0);
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().first().toInt(), 5);
        QVERIFY(!wrongThread);
        QCOMPARE(titles.value(0).value(0).toString(), QStringLiteral("session title"));
        QCOMPARE(cwd.value(0).value(0).toString(), QStringLiteral("/tmp"));
        QCOMPARE(s.currentDirectory(), QStringLiteral("/tmp"));
        QCOMPARE(alt.size(), 2);
        QCOMPARE(marks.size(), 1);
        QVERIFY2(s.screenText().contains(QStringLiteral("TERM=xterm-256color")), qPrintable(s.screenText()));
        QVERIFY(!s.altScreen());
    }

    void writeToDisplayNeverReachesProgram()
    {
        QFETCH_GLOBAL(QString, core);
        TerminalSession s(core);
        s.resize(6, 40, 8, 16);
        TerminalSession::StartOptions o;
        o.program = QStringLiteral("/bin/cat"); // echoes anything it receives
        QVERIFY(s.start(o));
        QSignalSpy changed(&s, &TerminalSession::contentChanged);
        s.writeToDisplay("\x1b[32magent says hi\x1b[0m\r\n");
        QVERIFY(changed.wait(2000));
        QTest::qWait(200);
        const QString text = s.screenText();
        QCOMPARE(text.count(QStringLiteral("agent says hi")), 1);
        s.sendInput("typed\n");
        QTRY_VERIFY_WITH_TIMEOUT(s.screenText().count(QStringLiteral("typed")) == 2, 3000); // tty echo + cat
        s.terminate();
    }

    void bellFloodIsCoalesced()
    {
        QFETCH_GLOBAL(QString, core);
        TerminalSession s(core);
        s.resize(10, 40, 8, 16);
        QSignalSpy finished(&s, &TerminalSession::finished);
        QSignalSpy bells(&s, &TerminalSession::bell);
        TerminalSession::StartOptions o;
        o.program = QStringLiteral("/bin/sh");
        o.arguments = QStringList{QStringLiteral("-c"), QStringLiteral("i=0; while [ $i -lt 2000 ]; do printf '\\a\\a\\a\\a\\a'; i=$((i+1)); done")};
        QVERIFY(s.start(o));
        QVERIFY(finished.wait(10000));
        QVERIFY(bells.size() >= 1);
        QVERIFY2(bells.size() < 2000, qPrintable(QString::number(bells.size())));
    }

    void displayInjectionWaitsForGround()
    {
        QFETCH_GLOBAL(QString, core);
        TerminalSession s(core);
        s.resize(6, 40, 8, 16);
        QSignalSpy finished(&s, &TerminalSession::finished);
        TerminalSession::StartOptions o;
        o.program = QStringLiteral("/bin/sh");
        // The program stops in the middle of an SGR sequence for 300 ms.
        o.arguments = QStringList{QStringLiteral("-c"), QStringLiteral("printf 'x\\033[3'; sleep 0.3; printf '1mRED\\033[0m\\n'; sleep 0.2")};
        QVERIFY(s.start(o));
        QTest::qWait(120);
        s.writeToDisplay("AGENT");
        QVERIFY(finished.wait(5000));
        QTest::qWait(100);
        const QString text = s.screenText();
        QVERIFY2(text.contains(QStringLiteral("RED")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("AGENT")), qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("1mRED")) && !text.contains(QStringLiteral("AGENT1m")), qPrintable(text));
    }

    void callerEnvironmentWins()
    {
        QFETCH_GLOBAL(QString, core);
        TerminalSession s(core);
        s.resize(6, 40, 8, 16);
        QSignalSpy finished(&s, &TerminalSession::finished);
        TerminalSession::StartOptions o;
        o.program = QStringLiteral("/bin/sh");
        o.arguments = QStringList{QStringLiteral("-c"), QStringLiteral("echo TERM=$TERM")};
        o.environment = QStringList{QStringLiteral("TERM=dumb")};
        QVERIFY(s.start(o));
        QVERIFY(finished.wait(5000));
        QTRY_VERIFY_WITH_TIMEOUT(s.screenText().contains(QStringLiteral("TERM=dumb")), 2000);
    }

    void floodIsParsedOffTheGuiThread()
    {
        QFETCH_GLOBAL(QString, core);
        TerminalSession s(core);
        s.resize(24, 80, 8, 16);
        QSignalSpy finished(&s, &TerminalSession::finished);
        TerminalSession::StartOptions o;
        o.program = QStringLiteral("/bin/sh");
        o.arguments = QStringList{QStringLiteral("-c"), QStringLiteral("i=0; while [ $i -lt 20000 ]; do echo line $i; i=$((i+1)); done")};
        QVERIFY(s.start(o));
        // The GUI thread stays responsive: timers keep firing during the flood.
        int ticks = 0;
        QTimer tick;
        connect(&tick, &QTimer::timeout, this, [&] { ++ticks; });
        tick.start(5);
        QVERIFY(finished.wait(20000));
        QVERIFY(ticks > 5);
        QTRY_VERIFY_WITH_TIMEOUT(s.screenText().contains(QStringLiteral("line 19999")), 3000);
        const QStringList history = s.scrollbackText(3);
        QCOMPARE(history.size(), 3);
        QVERIFY2(history.last().startsWith(QStringLiteral("line 199")), qPrintable(history.join(QLatin1Char('|'))));
    }
};

QObject *makeSessionTest()
{
    return new SessionTest;
}

#include "SessionTest.moc"
