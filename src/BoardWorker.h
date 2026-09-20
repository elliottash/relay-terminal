// SPDX-License-Identifier: AGPL-3.0-or-later
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
#include <QList>
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
    // A protocol message. Everything except `configure` waits for the worker's `ready`, so
    // nothing can overtake the configure that start() holds back (see the .cpp).
    void send(const QJsonObject &message);
    // Ask for the board (`board_open`) once `configure` has gone out. It does not wait for
    // `configured`: that never comes without a provider key, and the cards do not need one.
    void open();
    void stop();

    bool running() const { return m_process.state() != QProcess::NotRunning; }
    bool configured() const { return m_configured; }
    QString workspace() const { return m_workspace; }

    std::function<void(const QJsonObject &)> onEvent;
    std::function<void(const QString &)> onStatus;
    // The process has gone when nobody asked it to (it exited, it crashed, it could not start).
    // `onStatus` says so in a sentence for the status bar and the board's empty area; this is the
    // *event*, and it exists because a turn was very likely running when it happened: the panels
    // of the tab are still showing a busy strip for an answer that is never coming, and only
    // something that fires here can put them back (§30.7, #H6VQ). `crashed` is a signal rather
    // than a non-zero exit.
    std::function<void(bool crashed)> onExit;

private:
    void connectProcess();
    void handleLine(const QByteArray &line);
    void writeLine(const QJsonObject &message);   // straight down the pipe, no ordering rule

    QProcess m_process;
    QString m_python, m_data, m_workspace;
    QByteArray m_buffer;
    QList<QByteArray> m_pending;
    QJsonObject m_configure;
    // Messages sent before the worker answered `ready`, in order. `configure` is held until then
    // (start(), so it never races the handshake), and anything written in the meantime would go
    // out *in front of it* — see send().
    QList<QJsonObject> m_queued;
    bool m_connected = false, m_configured = false, m_stopping = false;
    bool m_ready = false, m_openPending = false;   // `ready` seen; a board_open waiting for it
};

}  // namespace relay
