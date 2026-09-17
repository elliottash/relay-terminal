// SPDX-License-Identifier: GPL-3.0-or-later
// relay::Pty tests: spawn, echo, resize, environment, exit codes, input.
#include "pty/Pty.h"

#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <mutex>

using namespace relay;

namespace {

struct Collector {
    std::mutex mutex;
    QByteArray data;
    std::atomic<int> exitCode{-1000};

    void attach(Pty *pty)
    {
        pty->onOutput = [this](const char *d, size_t n) {
            std::lock_guard<std::mutex> lock(mutex);
            data.append(d, int(n));
        };
        pty->onFinished = [this](int code) { exitCode = code; };
    }
    QByteArray snapshot()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return data;
    }
    bool waitFor(const QByteArray &needle, int ms = 5000)
    {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            if (snapshot().contains(needle))
                return true;
            QTest::qWait(10);
        }
        return false;
    }
    bool waitExit(int ms = 5000)
    {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms && exitCode.load() == -1000)
            QTest::qWait(10);
        return exitCode.load() != -1000;
    }
};

} // namespace

class PtyTest : public QObject {
    Q_OBJECT

private slots:
    void echoAndExitCode()
    {
        auto pty = Pty::create();
        Collector c;
        c.attach(pty.get());
        Pty::StartOptions o;
        o.program = QStringLiteral("/bin/sh");
        o.arguments = QStringList{QStringLiteral("-c"), QStringLiteral("printf 'hello-pty\\n'; exit 7")};
        QVERIFY2(pty->start(o), qPrintable(pty->errorString()));
        QVERIFY(pty->childPid() > 0);
        QVERIFY(c.waitFor("hello-pty\r\n")); // ONLCR on the slave side
        QVERIFY(c.waitExit());
        QCOMPARE(c.exitCode.load(), 7);
        QVERIFY(!pty->isRunning());
    }

    void environmentAndWorkingDirectory()
    {
        QTemporaryDir dir;
        auto pty = Pty::create();
        Collector c;
        c.attach(pty.get());
        Pty::StartOptions o;
        o.program = QStringLiteral("sh"); // resolved through PATH
        o.arguments = QStringList{QStringLiteral("-c"), QStringLiteral("echo \"cwd=$(pwd) v=$RELAY_TEST_VAR h=${HOME:+set}\"")};
        o.workingDirectory = dir.path();
        o.environment = QStringList{QStringLiteral("RELAY_TEST_VAR=42")};
        QVERIFY(pty->start(o));
        QVERIFY(c.waitExit());
        const QString out = QString::fromUtf8(c.snapshot());
        QVERIFY2(out.contains(QStringLiteral("cwd=") + QFileInfo(dir.path()).canonicalFilePath()), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("v=42 h=set")), qPrintable(out));
    }

    void interactiveInputResizeAndForeground()
    {
        auto pty = Pty::create();
        Collector c;
        c.attach(pty.get());
        Pty::StartOptions o;
        o.program = QStringLiteral("bash");
        o.arguments = QStringList{QStringLiteral("--norc"), QStringLiteral("--noprofile"), QStringLiteral("-i")};
        o.rows = 30;
        o.cols = 100;
        o.environment = QStringList{QStringLiteral("PS1=$ ")};
        QVERIFY(pty->start(o));
        pty->write(QByteArray("stty size\n"));
        QVERIFY(c.waitFor("30 100"));
        pty->resize(40, 120);
        pty->write(QByteArray("stty size\n"));
        QVERIFY(c.waitFor("40 120"));
        // The shell is the foreground process group while idle.
        QTRY_COMPARE_WITH_TIMEOUT(pty->foregroundPid(), pty->childPid(), 3000);
        pty->write(QByteArray("sleep 5\n"));
        QTRY_VERIFY_WITH_TIMEOUT(pty->foregroundPid() > 0 && pty->foregroundPid() != pty->childPid(), 3000);
        // Ctrl+C reaches the foreground job (signal dispositions were reset).
        pty->write(QByteArray("\x03"));
        QTRY_COMPARE_WITH_TIMEOUT(pty->foregroundPid(), pty->childPid(), 3000);
        pty->write(QByteArray("exit 3\n"));
        QVERIFY(c.waitExit());
        QCOMPARE(c.exitCode.load(), 3);
    }

    void largeInputDoesNotBlock()
    {
        auto pty = Pty::create();
        Collector c;
        c.attach(pty.get());
        Pty::StartOptions o;
        o.program = QStringLiteral("/bin/sh");
        o.arguments = QStringList{QStringLiteral("-c"), QStringLiteral("stty -echo; sleep 1; wc -c; exit 0")};
        QVERIFY(pty->start(o));
        QTest::qWait(200);
        // 1 MiB into a program that is not reading yet: write() must return at once.
        QByteArray chunk(1 << 20, 'x');
        for (int i = 999; i < chunk.size(); i += 1000)
            chunk[i] = '\n'; // canonical mode caps lines at 4095 bytes
        chunk[chunk.size() - 1] = '\n';                  // so ^D below is EOF, not a flush
        QElapsedTimer t;
        t.start();
        pty->write(chunk);
        QVERIFY2(t.elapsed() < 200, qPrintable(QString::number(t.elapsed())));
        pty->write(QByteArray("\x04"));
        QVERIFY(c.waitExit(15000));
        QVERIFY2(c.snapshot().contains(QByteArray::number(chunk.size())), c.snapshot().right(200).constData());
    }

    void missingProgram()
    {
        auto pty = Pty::create();
        Pty::StartOptions o;
        o.program = QStringLiteral("/nonexistent/relay-no-such-program");
        QVERIFY(!pty->start(o));
        QVERIFY(!pty->errorString().isEmpty());
    }

    void destructorDoesNotWaitForDetachedChild()
    {
        // The child closes its tty descriptors (like nohup) and keeps running:
        // the reader sees EOF but must not block the destructor on waitpid.
        QElapsedTimer t;
        {
            auto pty = Pty::create();
            Collector c;
            c.attach(pty.get());
            Pty::StartOptions o;
            o.program = QStringLiteral("/bin/sh");
            o.arguments = QStringList{QStringLiteral("-c"), QStringLiteral("exec </dev/null >/dev/null 2>&1; trap '' HUP; sleep 3")};
            QVERIFY(pty->start(o));
            QTest::qWait(300);
            t.start();
        }
        QVERIFY2(t.elapsed() < 1000, qPrintable(QString::number(t.elapsed())));
    }

    void terminateHangsUp()
    {
        auto pty = Pty::create();
        Collector c;
        c.attach(pty.get());
        Pty::StartOptions o;
        o.program = QStringLiteral("/bin/sh");
        o.arguments = QStringList{QStringLiteral("-c"), QStringLiteral("sleep 30")};
        QVERIFY(pty->start(o));
        QTest::qWait(100);
        pty->terminate();
        QVERIFY(c.waitExit());
        QCOMPARE(c.exitCode.load(), 128 + 1); // SIGHUP
    }
};

QObject *makePtyTest()
{
    return new PtyTest;
}

#include "PtyTest.moc"
