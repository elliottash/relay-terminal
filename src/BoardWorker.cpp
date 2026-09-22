// SPDX-License-Identifier: AGPL-3.0-or-later
#include "BoardWorker.h"
#include "AppPaths.h"

#include <QJsonDocument>
#include <QProcessEnvironment>

namespace relay {

namespace {
constexpr int kMaxBuffer = 8 * 1024 * 1024;
// How many messages may wait for `ready`. A helper panel's first ask is one; a panel that somehow
// asked a hundred times before the worker was up has a worse problem than a dropped message.
constexpr int kMaxQueued = 32;
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
                onStatus(QStringLiteral("Board worker protocol overflow; stopped."));
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
            onStatus(QStringLiteral("Board worker failed: ") + m_process.errorString());
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus status) {
                m_configured = false;
                m_ready = false;
                // Nothing that was waiting for `ready` will ever be written now, and keeping it
                // would send a dead turn's messages to the *next* worker (start() clears the list
                // too, which is one restart too late for anything read in between).
                m_queued.clear();
                if (m_stopping)
                    return;
                if (onStatus)
                    onStatus(QStringLiteral("The Board worker exited."));
                if (onExit)
                    onExit(status == QProcess::CrashExit);
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
    if (type == QStringLiteral("ready")) {
        m_ready = true;
        if (!m_configure.isEmpty())
            send(m_configure);
        // Then whatever was said while the worker was starting, in the order it was said, behind
        // the configure it would otherwise have overtaken (send()).
        const QList<QJsonObject> waiting = m_queued;   // a handler may send more
        m_queued.clear();
        for (const QJsonObject &message : waiting)
            writeLine(message);
        if (m_openPending) {
            m_openPending = false;
            open();
        }
    } else if (type == QStringLiteral("configured"))
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
    m_ready = false;
    m_queued.clear();
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("RELAY_PANE_ID"), QStringLiteral("switchboard"));
    m_process.setProcessEnvironment(environment);
    m_process.setProgram(m_python.isEmpty() ? relayPython() : m_python);
    m_process.setArguments({QStringLiteral("-X"), QStringLiteral("utf8"), QStringLiteral("-S"), QStringLiteral("-u"),
                            m_data + QStringLiteral("/backend/worker.py")});
    m_process.start();
    // `configure` goes out when the worker answers `ready`, so it never races the handshake.
}

// Nothing overtakes `configure`. start() holds the configure back until the worker answers
// `ready`, so that it never races the handshake — and until 2026-09-20 a message written in that
// window went out *in front of* it, because the process is already running by then and the
// configure is not. That is exactly what a console's **first** ask does: it starts the tab's
// worker and sends its `ask` in the same breath (§33, "started on the first ask"), and the worker
// refused it with "Configure a provider and workspace first" while the surface sat there running,
// waiting for an answer that was never coming. The Board never hit it because `open()`
// already waited for `ready`.
//
// So everything but the configure itself waits for `ready` and is then written in order.
void BoardWorker::send(const QJsonObject &message)
{
    if (!m_ready
        && message.value(QStringLiteral("type")).toString() != QStringLiteral("configure")) {
        if (m_queued.size() < kMaxQueued)
            m_queued.append(message);
        return;
    }
    writeLine(message);
}

void BoardWorker::writeLine(const QJsonObject &message)
{
    const QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (m_process.state() == QProcess::Running)
        m_process.write(line);
    else if (m_process.state() == QProcess::Starting)
        m_pending.append(line);
}

void BoardWorker::open()
{
    // The worker reads its stdin in order, so a board_open written after `configure` is answered
    // for the configured workspace whether or not the provider part of `configure` succeeded.
    if (m_ready)
        send({{QStringLiteral("type"), QStringLiteral("board_open")}});
    else
        m_openPending = true;
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
