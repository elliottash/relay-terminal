// SPDX-License-Identifier: AGPL-3.0-or-later
// Rotating diagnostics log (issue SQAM): location, permissions, level filtering, rotation and
// redaction. Uses a private XDG_DATA_HOME, so it never touches the real profile.
#include "CrashLog.h"
#include "Logging.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

class LoggingTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminalTest"));
        QCoreApplication::setApplicationName(QStringLiteral("relay-logging-test"));
        QVERIFY(m_home.isValid());
        qputenv("XDG_DATA_HOME", m_home.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", m_home.path().toUtf8());
        QStandardPaths::setTestModeEnabled(false);
    }

    void init() {
        QSettings().setValue(QStringLiteral("logging/level"), QStringLiteral("info"));
        QDir(relay::log::directory()).removeRecursively();
    }

    void directoryAndPermissions() {
        relay::log::info(QStringLiteral("gui_start version=0.1.0"));
        const QString path = relay::log::filePath();
        QVERIFY(path.endsWith(QStringLiteral("/relay/logs/relay.log")));
        QVERIFY(QFile::exists(path));
        QCOMPARE(QFile::permissions(path) & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther),
                 QFile::Permissions());
        QVERIFY(read(path).contains(QStringLiteral("INFO relay.gui gui_start")));
        // ISO-8601 UTC first, matching the worker's worker.log.
        QVERIFY(QRegularExpression(QStringLiteral("^\\d{4}-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\d\\.\\d{3}Z "))
                    .match(read(path)).hasMatch());
    }

    void levelFiltersAndOffWritesNothing() {
        QSettings().setValue(QStringLiteral("logging/level"), QStringLiteral("error"));
        relay::log::info(QStringLiteral("event type=agent_started"));
        relay::log::error(QStringLiteral("event type=error msg=\"boom\""));
        const QString body = read(relay::log::filePath());
        QVERIFY(!body.contains(QStringLiteral("agent_started")));
        QVERIFY(body.contains(QStringLiteral("boom")));

        QSettings().setValue(QStringLiteral("logging/level"), QStringLiteral("off"));
        QFile::remove(relay::log::filePath());
        relay::log::error(QStringLiteral("event type=error"));
        QVERIFY(!QFile::exists(relay::log::filePath()));
    }

    void secretsAreMasked() {
        const QString key = QStringLiteral("sk-relaytestkey0123456789abcdef");
        relay::log::info(QStringLiteral("configured key=%1 auth=\"Bearer %1\"").arg(key));
        const QString body = read(relay::log::filePath());
        QVERIFY(!body.contains(key));
        QVERIFY(!body.contains(QStringLiteral("relaytestkey")));
        QVERIFY(body.contains(QStringLiteral("redacted")));
    }

    void rotationKeepsThreeBackups() {
        const QString path = relay::log::filePath();
        const QString filler(4000, QLatin1Char('x'));
        // 5 MiB x 3: a little over four full files is enough to fill every backup slot.
        for (int index = 0; index < 5600; ++index) relay::log::info(QStringLiteral("fill %1 %2").arg(index).arg(filler));
        QVERIFY(QFile::exists(path));
        QVERIFY(QFile::exists(path + QStringLiteral(".1")));
        QVERIFY(QFile::exists(path + QStringLiteral(".3")));
        QVERIFY(!QFile::exists(path + QStringLiteral(".4")));
        QVERIFY(QFileInfo(path + QStringLiteral(".1")).size() <= 6 * 1024 * 1024);
    }

    // A fatal signal must leave its frames in relay.log and still kill the process the way it was
    // killed — a crash that a handler turns into a clean exit is a crash that hides from apport,
    // from a debugger and from the exit status. Run in a child: this one really does crash.
    void crashHandlerWritesFramesAndStillDies() {
        relay::log::info(QStringLiteral("gui_start version=0.1.0"));
        const QString path = relay::log::filePath();
        const qint64 before = QFileInfo(path).size();
        ::fflush(nullptr);
        const pid_t child = ::fork();
        QVERIFY(child >= 0);
        if (child == 0) {
            // No core for this one: the deliberate crash of a test should not be reported to the
            // machine's crash handler as if Relay had died.
            ::prctl(PR_SET_DUMPABLE, 0);
            (void)::freopen("/dev/null", "w", stderr);   // the report’s copy on stderr is not test output
            relay::crashlog::install(QStringLiteral("test-build.1"));
            ::raise(SIGSEGV);
            ::_exit(97);   // unreachable: the handler re-raises and the default action kills us
        }
        int status = 0;
        QCOMPARE(::waitpid(child, &status, 0), child);
        QVERIFY2(WIFSIGNALED(status), "the child exited instead of dying of the signal");
        QCOMPARE(WTERMSIG(status), SIGSEGV);

        const QString report = read(path).mid(int(before));
        QVERIFY2(report.contains(QStringLiteral("gui_crash signal=11 name=SIGSEGV")), qPrintable(report));
        QVERIFY(report.contains(QStringLiteral("build=test-build.1")));
        QVERIFY(report.contains(QStringLiteral("gui_crash_frames_begin count=")));
        QVERIFY(report.contains(QStringLiteral("gui_crash_frames_end")));
        // The frames themselves: at least the handler's own address, in backtrace_symbols_fd's
        // shape, which `addr2line -e` reads.
        QVERIFY(report.contains(QStringLiteral("logging-tests")) || report.contains(QLatin1Char('[')));
        // And it is a log line like any other: ISO timestamp, level, source.
        QVERIFY(QRegularExpression(QStringLiteral("\\d{4}-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\d\\.\\d{3}Z ERROR relay.gui gui_crash "))
                    .match(report).hasMatch());
    }

    void levelNamesRoundTrip() {
        for (const QStringList &choice : relay::log::levelChoices()) {
            relay::log::setLevel(choice.at(0));
            QCOMPARE(relay::log::levelName(relay::log::level()), choice.at(0));
        }
        relay::log::setLevel(QStringLiteral("nonsense"));
        QCOMPARE(relay::log::levelName(relay::log::level()), QStringLiteral("info"));
    }

private:
    static QString read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(file.readAll()) : QString();
    }
    QTemporaryDir m_home;
};

QTEST_MAIN(LoggingTest)
#include "logging_test.moc"
