// SPDX-License-Identifier: AGPL-3.0-or-later
// TerminalSession: threaded parsing, signals on the GUI thread, display injection.
#include "session/TerminalSession.h"

#include <QSignalSpy>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
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

    void inputGapIsConditionalAndOrdered()
    {
        QFETCH_GLOBAL(QString, core);
        const QByteArray gap = "\x1b]7772;input-gap\x1b\\";
        // A missing gap, an existing gap, unterminated output, and the top/bottom edges.
        for (const QByteArray &before : {QByteArray("Done\r\n"), QByteArray("Done\r\n\r\n"),
                                        QByteArray("Done"), QByteArray(""), QByteArray("1\r\n2\r\nDone\r\n")}) {
            TerminalSession s(core);
            s.resize(4, 40, 8, 16);
            s.writeToDisplay(before + gap + gap + "ls\r\n\r\nresult");
            const QString text = s.scrollbackText(100).join('\n') + '\n' + s.screenText();
            if (before.isEmpty()) QVERIFY2(text.contains("\nls\n\nresult"), qPrintable(text));
            else QVERIFY2(text.contains("Done\n\nls\n\nresult"), qPrintable(text));
            QVERIFY2(!text.contains("\n\n\nls"), qPrintable(text));
        }
        TerminalSession alt(core);
        alt.resize(5, 40, 8, 16);
        alt.writeToDisplay("\x1b[?1049hDone\r\n" + gap + "ls");
        QVERIFY(alt.screenText().startsWith("Done\nls"));
    }

    void inputGapSurvivesFragmentedPtyOutput()
    {
        QFETCH_GLOBAL(QString, core);
        TerminalSession s(core);
        s.resize(8, 40, 8, 16);
        QSignalSpy finished(&s, &TerminalSession::finished);
        QSignalSpy marks(&s, &TerminalSession::promptMark);
        TerminalSession::StartOptions o;
        o.program = "/bin/sh";
        o.arguments = QStringList{"-c", "printf 'Done\\n\\033]7772;input-'; sleep 0.1; "
            "printf 'gap\\033'; sleep 0.1; printf '\\\\ls\\n\\nresult\\n'"};
        QVERIFY(s.start(o));
        QVERIFY(finished.wait(5000));
        QVERIFY2(s.screenText().startsWith("Done\n\nls\n\nresult"), qPrintable(s.screenText()));
        QCOMPARE(marks.size(), 0); // the gap must not become a synthetic command-finished mark
    }

    void bashBackgroundNotificationBeforeStagedCommand()
    {
        QFETCH_GLOBAL(QString, core);
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        const QString root = QFileInfo(QString::fromUtf8(__FILE__)).absolutePath() + "/../..";
        TerminalSession s(core);
        s.resize(24, 120, 8, 16);
        TerminalSession::StartOptions o;
        o.program = "/bin/bash";
        o.arguments = QStringList{"--noprofile", "--rcfile", root + "/shell/integration.bash", "-i"};
        o.environment = QStringList{"RELAY_CLEAN_SHELL=1", "RELAY_RUNTIME_DIR=" + runtime.path(),
            "RELAY_SESSION_TOKEN=bgsp-test", "RELAY_SHELL_EVENT=" + root + "/shell/event.py",
            "RELAY_PYTHON=/usr/bin/python3", "PS1=prompt> ", "HISTFILE=/dev/null"};
        auto state = [&] {
            QFile file(runtime.filePath("state.json"));
            if (!file.open(QIODevice::ReadOnly)) return QJsonObject{};
            return QJsonDocument::fromJson(file.readAll()).object();
        };
        auto stage = [&](const QByteArray &command) {
            QFile file(runtime.filePath("input.txt"));
            if (!file.open(QIODevice::WriteOnly)) return false;
            if (file.write(command) != command.size()) return false;
            file.close();
            s.sendInput("\x18\x12");
            return true;
        };
        QVERIFY(s.start(o));
        QTRY_VERIFY_WITH_TIMEOUT(state()["event"] == "ready" && !s.termiosFlags().canonical, 5000);
        QVERIFY(stage("(sleep 0.2; printf 'LATE_OUTPUT\\n') &"));
        QTRY_COMPARE_WITH_TIMEOUT(state()["event"].toString(), QString("loaded"), 5000);
        s.sendInput("\r");
        QTRY_VERIFY_WITH_TIMEOUT(state()["event"] == "ready" && !s.termiosFlags().canonical, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(s.screenText().contains("\nLATE_OUTPUT\n"), 5000);
        QTest::qWait(100); // let Bash reap the background child before the next Readline binding
        QVERIFY(stage("printf 'RESULT\\n'"));
        QTRY_COMPARE_WITH_TIMEOUT(state()["event"].toString(), QString("loaded"), 5000);
        s.sendInput("\r");
        QTRY_VERIFY_WITH_TIMEOUT(state()["event"] == "ready" && !s.termiosFlags().canonical, 5000);
        const QString text = s.screenText();
        QVERIFY2(text.contains("Done"), qPrintable(text));
        QVERIFY2(text.contains("\n\nprintf 'RESULT\\n'\n\nRESULT\n"), qPrintable(text));
        const auto lines = text.split('\n');
        const int command = lines.indexOf("printf 'RESULT\\n'");
        QVERIFY(command >= 2 && lines[command - 2].contains("Done"));
        ViewportFrame frame;
        s.withCore([&](VtCore &c) { c.updateFrame(&frame, true); });
        QVERIFY(frame.lines[size_t(command)].marks & MarkUserShell);
        QVERIFY(!(frame.lines[size_t(command - 1)].marks & MarkUserShell));
        QVERIFY(!(frame.lines[size_t(command + 1)].marks & MarkUserShell));
        s.terminate();
    }

    // Relay's inline output holds the program's size: the grid follows a resize at once, the
    // PTY (and so SIGWINCH) only when the hold is released.
    void heldResizeReachesTheProgramOnRelease()
    {
        QFETCH_GLOBAL(QString, core);
        TerminalSession s(core);
        s.resize(8, 40, 8, 16);
        TerminalSession::StartOptions o;
        o.program = QStringLiteral("/bin/sh");
        o.arguments = QStringList{QStringLiteral("-c"), QStringLiteral("while read l; do echo \"size=$(stty size)\"; done")};
        QVERIFY(s.start(o));
        s.holdPtyResize(true);
        s.resize(12, 50, 8, 16);
        QCOMPARE(s.rows(), 12);
        QCOMPARE(s.columns(), 50);
        s.sendInput("\n");
        QTRY_VERIFY_WITH_TIMEOUT(s.screenText().contains(QStringLiteral("size=8 40")), 3000);
        s.holdPtyResize(false);
        s.sendInput("\n");
        QTRY_VERIFY_WITH_TIMEOUT(s.screenText().contains(QStringLiteral("size=12 50")), 3000);
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
