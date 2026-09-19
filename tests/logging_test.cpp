// SPDX-License-Identifier: AGPL-3.0-or-later
// Rotating diagnostics log (issue SQAM): location, permissions, level filtering, rotation and
// redaction. Uses a private XDG_DATA_HOME, so it never touches the real profile.
#include "Logging.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

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
