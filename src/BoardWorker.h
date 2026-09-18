// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// One `backend/worker.py` per window for the Switchboard: it answers the `board_*` protocol
// messages of docs/AGENT-SESSIONS-PROTOCOL.md section 17 and runs the Switchboard agent, whose
// model is the `switchboard` role (section 13). Card chats therefore never enter a pane's
// conversation, and a pane's worker keeps its own queue for the user's terminal work.
//
// Deliberately smaller than a pane's worker: no terminal, no queue UI, no isolation unit. NDJSON
// framing, a pending-write buffer for the window between start() and started(), and a restart.
#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <functional>

namespace relay {

class BoardWorker : public QObject {
public:
    BoardWorker(QString python, QString dataDir, QObject *parent = nullptr);

    // Start the process if it is not running, then send `configure`. Safe to call again: a second
    // call with the same object is ignored, a different one reconfigures.
    void start(const QJsonObject &configure);
    void send(const QJsonObject &message);
    void stop();

    bool running() const { return m_process.state() != QProcess::NotRunning; }
    bool configured() const { return m_configured; }
    QString workspace() const { return m_workspace; }

    std::function<void(const QJsonObject &)> onEvent;
    std::function<void(const QString &)> onStatus;

private:
    void connectProcess();
    void handleLine(const QByteArray &line);

    QProcess m_process;
    QString m_python, m_data, m_workspace;
    QByteArray m_buffer;
    QList<QByteArray> m_pending;
    QJsonObject m_configure;
    bool m_connected = false, m_configured = false, m_stopping = false;
};

}  // namespace relay
