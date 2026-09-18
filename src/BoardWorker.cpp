// SPDX-License-Identifier: GPL-3.0-or-later
#include "BoardWorker.h"

#include <QJsonDocument>
#include <QProcessEnvironment>

namespace relay {

namespace {
constexpr int kMaxBuffer = 8 * 1024 * 1024;
}

BoardWorker::BoardWorker(QString python, QString dataDir, QObject *parent)
    : QObject(parent), m_python(std::move(python)), m_data(std::move(dataDir))
{
}

void BoardWorker::connectProcess()
{
    m_connected = true;
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_buffer += m_process.readAllStandardOutput();
        if (m_buffer.size() > kMaxBuffer) {
            m_process.kill();
            if (onStatus)
                onStatus(QStringLiteral("Switchboard worker protocol overflow; stopped."));
            return;
        }
        int index;
        while ((index = m_buffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_buffer.left(index);
            m_buffer.remove(0, index + 1);
            handleLine(line);
        }
    });
    // Provider error bodies can quote the request; they are never logged.
    connect(&m_process, &QProcess::readyReadStandardError, this,
            [this] { m_process.readAllStandardError(); });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (onStatus)
            onStatus(QStringLiteral("Switchboard worker failed: ") + m_process.errorString());
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
                m_configured = false;
                if (!m_stopping && onStatus)
                    onStatus(QStringLiteral("The Switchboard worker exited."));
            });
    connect(&m_process, &QProcess::started, this, [this] {
        for (const QByteArray &line : std::as_const(m_pending))
            m_process.write(line);
        m_pending.clear();
    });
}

void BoardWorker::handleLine(const QByteArray &line)
{
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return;
    const QJsonObject event = doc.object();
    const QString type = event.value(QStringLiteral("event")).toString();
    if (type == QStringLiteral("ready") && !m_configure.isEmpty())
        send(m_configure);
    else if (type == QStringLiteral("configured"))
        m_configured = true;
    if (onEvent)
        onEvent(event);
}

void BoardWorker::start(const QJsonObject &configure)
{
    const bool sameSettings = configure == m_configure;
    m_configure = configure;
    m_workspace = configure.value(QStringLiteral("workspace")).toString();
    if (!m_connected)
        connectProcess();
    if (running()) {
        if (!sameSettings || !m_configured)
            send(m_configure);
        return;
    }
    m_stopping = false;
    m_buffer.clear();
    m_pending.clear();
    m_configured = false;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("RELAY_PANE_ID"), QStringLiteral("switchboard"));
    m_process.setProcessEnvironment(environment);
    m_process.setProgram(m_python);
    m_process.setArguments({QStringLiteral("-S"), QStringLiteral("-u"),
                            m_data + QStringLiteral("/backend/worker.py")});
    m_process.start();
    // `configure` goes out when the worker answers `ready`, so it never races the handshake.
}

void BoardWorker::send(const QJsonObject &message)
{
    const QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (m_process.state() == QProcess::Running)
        m_process.write(line);
    else if (m_process.state() == QProcess::Starting)
        m_pending.append(line);
}

void BoardWorker::stop()
{
    m_stopping = true;
    if (running()) {
        send({{QStringLiteral("type"), QStringLiteral("shutdown")}});
        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(1500))
            m_process.kill();
    }
}

}  // namespace relay
