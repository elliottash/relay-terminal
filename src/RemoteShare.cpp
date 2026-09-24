// SPDX-License-Identifier: AGPL-3.0-or-later
#include "RemoteShare.h"
#include "AppPaths.h"
#include "core/VtCore.h"
#include "session/TerminalSession.h"
#include "tools/ScreenJson.h"
#include "view/TerminalView.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QProcess>
#include <QStringList>
#include <QTimer>

#include <algorithm>

namespace relay {

namespace {

// The most scrollback rows one `history` line may ask for, matching the protocol's page cap
// (docs/REMOTE-PROTOCOL.md section 6.5). A phone pages; it does not download the buffer.
constexpr int kHistoryPage = 200;

// What the sidecar calls this machine on a phone's screen. One string, because `start` is now sent
// from two places: the first share, and the switch in Options › Remote (#PH0N).
QString desktopName() { return QStringLiteral("this desktop"); }

// The sidecar lives beside the backend: <data>/remote in an install, the source tree otherwise.
QString sidecarRoot()
{
    const QStringList candidates{
        QString::fromLocal8Bit(qgetenv("RELAY_REMOTE_DIR")),
        QStringLiteral(RELAY_SOURCE_DIR),
        QStringLiteral(RELAY_DATA_DIR),
    };
    for (const QString &candidate : candidates) {
        if (candidate.isEmpty()) continue;
        if (QFileInfo::exists(candidate + QStringLiteral("/remote/gui_host.py"))) return candidate;
    }
    return QString();
}

QrMatrix qrMatrixOf(const QJsonArray &rows)
{
    QrMatrix matrix;
    for (const QJsonValue &row : rows) {
        QVector<int> cells;
        for (const QJsonValue &cell : row.toArray()) cells.append(cell.toInt());
        matrix.append(cells);
    }
    return matrix;
}

} // namespace

RemoteShare &RemoteShare::instance()
{
    static RemoteShare shared;
    return shared;
}

RemoteShare::RemoteShare()
{
    // Titles, working directories and pane status are polled rather than pushed: they change
    // rarely, and a pull keeps Pane free of any knowledge that sharing exists.
    m_poll = new QTimer(this);
    m_poll->setInterval(700);
    connect(m_poll, &QTimer::timeout, this, &RemoteShare::poll);
    // A knock, a control request and a guest prompt each lapse on the hub's clock (2 min, 60 s,
    // 10 min). One second is the resolution the countdowns on the Sharing pane need; the row goes
    // when it reaches zero, which is a moment before the hub gives up on it, never after.
    m_second = new QTimer(this);
    m_second->setInterval(1000);
    connect(m_second, &QTimer::timeout, this, [this] {
        // Expiring here rather than in the pane: a lapsed row has to go from every Sharing pane
        // in every window, and whichever one ticked first would otherwise be the only one told.
        if (!m_sharing.expire(QDateTime::currentMSecsSinceEpoch()).isEmpty()) emit sharingModelChanged();
        emit secondPassed();
    });
    m_second->start();
    // The presence rule for notifications (docs/REMOTE-PROTOCOL.md section 9): the hub must not
    // push to a phone while this window is the active, focused one, because the person is already
    // looking at it. Only the GUI knows that, so it says so.
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState s) {
        send({{"t", "window_active"}, {"active", s == Qt::ApplicationActive}});
    });
}

RemoteShare::~RemoteShare() = default;

bool RemoteShare::ensureSidecar(QString *error)
{
    if (m_process) return true;
    const QString root = sidecarRoot();
    if (root.isEmpty()) {
        if (error) *error = QStringLiteral("Relay's remote sidecar (remote/gui_host.py) is missing "
                                           "from this installation.");
        return false;
    }
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(root);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    m_process->setProcessEnvironment(environment);
    m_process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &RemoteShare::onReadable);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        emit failed(QStringLiteral("The remote sidecar could not start."));
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
                m_running = false;
                m_panes.clear();
                m_poll->stop();
                emit startedChanged();
                emit sharingChanged();
            });
    m_process->start(relayPython(),
                     {QStringLiteral("-X"), QStringLiteral("utf8"), QStringLiteral("-u"),
                      QStringLiteral("-m"), QStringLiteral("remote.gui_host")});
    if (!m_process->waitForStarted(5000)) {
        if (error) *error = QStringLiteral("Python could not start the remote sidecar.");
        m_process->deleteLater();
        m_process = nullptr;
        return false;
    }
    // `always` and `address` are phase 1 of #PH0N: with the switch on the sidecar registers at the
    // remembered address and keeps itself registered, rather than waiting for a pane to be shared.
    send(remotesettings::startMessage(desktopName(), remotesettings::alwaysOn(),
                                      remotesettings::address()));
    send({{"t", "window_active"},
          {"active", QGuiApplication::applicationState() == Qt::ApplicationActive}});
    return true;
}

void RemoteShare::send(const QJsonObject &message)
{
    if (!m_process || m_process->state() != QProcess::Running) return;
    m_process->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

void RemoteShare::sendBoardEvent(const QJsonValue &rid, const QJsonObject &event)
{
    send({{QStringLiteral("t"), QStringLiteral("board_event")},
          {QStringLiteral("rid"), rid.isUndefined() ? QJsonValue(QJsonValue::Null) : rid},
          {QStringLiteral("event"), event}});
}

void RemoteShare::onReadable()
{
    m_pending.append(m_process->readAllStandardOutput());
    int newline;
    while ((newline = m_pending.indexOf('\n')) >= 0) {
        const QByteArray line = m_pending.left(newline);
        m_pending.remove(0, newline + 1);
        if (line.trimmed().isEmpty()) continue;
        const QJsonObject message = QJsonDocument::fromJson(line).object();
        if (!message.isEmpty()) handle(message);
    }
}

void RemoteShare::handle(const QJsonObject &message)
{
    const QString kind = message.value(QStringLiteral("t")).toString();
    if (kind == QLatin1String("started")) {
        m_running = true;
        m_base = message.value(QStringLiteral("base")).toString();
        m_note = message.value(QStringLiteral("note")).toString();
        m_addresses = message.value(QStringLiteral("addresses")).toArray();
        m_poll->start();
        emit addressesChanged(m_addresses);
        emit startedChanged();
    } else if (kind == QLatin1String("pairing")) {
        // QA hook: the pairing URL carries a one-time secret, so it is never logged. A driver
        // script that has to pair a headless browser asks for it explicitly with this variable.
        const QByteArray dump = qgetenv("RELAY_REMOTE_PAIR_FILE");
        if (!dump.isEmpty()) {
            QFile file(QString::fromLocal8Bit(dump));
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
                file.write(message.value(QStringLiteral("url")).toString().toUtf8() + '\n');
            }
        }
        const QrMatrix matrix = qrMatrixOf(message.value(QStringLiteral("qr")).toArray());
        emit pairingReady(message.value(QStringLiteral("url")).toString(), matrix,
                          message.value(QStringLiteral("expires")).toInt());
    } else if (kind == QLatin1String("ask")) {
        // Kept until answer(): the Sharing pane is usually opened after this line arrived, and
        // attach() replays it there (#SMDX).
        m_pendingAsk.id = message.value(QStringLiteral("id")).toInt();
        m_pendingAsk.name = message.value(QStringLiteral("name")).toString();
        m_pendingAsk.platform = message.value(QStringLiteral("platform")).toString();
        m_pendingAsk.fingerprint = message.value(QStringLiteral("fingerprint")).toString();
        m_pendingAsk.code = message.value(QStringLiteral("code")).toString();
        m_pendingAsk.peer = message.value(QStringLiteral("peer")).toString();
        emit pairingAsked(m_pendingAsk.id, m_pendingAsk.name, m_pendingAsk.platform,
                          m_pendingAsk.fingerprint, m_pendingAsk.code, m_pendingAsk.peer);
        // With no share window to pop up, an ask has to reach the owner even when no Sharing
        // pane is open. The empty pane id is what says "a device, not a guest": the window opens
        // the Sharing pane on its Devices page rather than beside a shared pane.
        emit needsOwner(QString(), QStringLiteral("A device wants to pair"),
                        QStringLiteral("%1 (%2) wants access — compare the code on the Sharing "
                                       "pane's Devices page")
                            .arg(m_pendingAsk.name, m_pendingAsk.platform));
    } else if (kind == QLatin1String("devices")) {
        m_devices = message.value(QStringLiteral("items")).toArray();
        emit devicesChanged(m_devices);
    } else if (kind == QLatin1String("input")) {
        const QString paneId = message.value(QStringLiteral("pane")).toString();
        auto it = m_panes.find(paneId);
        if (it != m_panes.end() && it->hooks.input) {
            it->hooks.input(QByteArray::fromBase64(
                message.value(QStringLiteral("bytes")).toString().toLatin1()));
        }
    } else if (kind == QLatin1String("compose")) {
        const QString paneId = message.value(QStringLiteral("pane")).toString();
        auto it = m_panes.find(paneId);
        if (it != m_panes.end() && it->hooks.compose) {
            const QString when = message.value(QStringLiteral("when")).toString();
            it->hooks.compose(message.value(QStringLiteral("text")).toString(),
                              message.value(QStringLiteral("route")).toBool(),
                              message.value(QStringLiteral("origin")).toString(),
                              when.isEmpty() ? QStringLiteral("now") : when,
                              message.value(QStringLiteral("origin_name")).toString());
        }
    // ----- pane_state (relay-terminal-71): section 16 ------------------------------------------
    // Every one of these names a row, a choice or nothing at all — never a path, a preset or a
    // worker type. The hub has already checked the device's capability and refused guests; the
    // pane checks that the row still offers the action, against the pane as it is now.
    } else if (kind == QLatin1String("pane_state_get")) {
        auto it = m_panes.find(message.value(QStringLiteral("pane")).toString());
        if (it != m_panes.end() && it->hooks.publishPaneState) it->hooks.publishPaneState();
    } else if (kind == QLatin1String("queue_remove")) {
        // `item` is what the phone's queue rows have been called since before pane_state; a
        // `row` from the newer client means the same thing. Answered by the state that follows.
        auto it = m_panes.find(message.value(QStringLiteral("pane")).toString());
        const QString row = message.value(QStringLiteral("row")).toString().isEmpty()
                                ? message.value(QStringLiteral("item")).toString()
                                : message.value(QStringLiteral("row")).toString();
        if (it != m_panes.end() && it->hooks.queueRemove && !row.isEmpty()) it->hooks.queueRemove(row);
    } else if (kind == QLatin1String("queue_move")) {
        auto it = m_panes.find(message.value(QStringLiteral("pane")).toString());
        if (it != m_panes.end() && it->hooks.queueMove)
            it->hooks.queueMove(message.value(QStringLiteral("row")).toString(),
                                message.value(QStringLiteral("to")).toString());
    } else if (kind == QLatin1String("queue_send_now")) {
        auto it = m_panes.find(message.value(QStringLiteral("pane")).toString());
        if (it != m_panes.end() && it->hooks.queueSendNow)
            it->hooks.queueSendNow(message.value(QStringLiteral("row")).toString());
    } else if (kind == QLatin1String("queue_resume")) {
        // The empty send on a phone, which is Enter on an empty prompt box at the desk (#7JD1).
        // It names no row: the pane decides whether there is a pause to lift, and the
        // `pane_state` that follows says whether there still is one.
        auto it = m_panes.find(message.value(QStringLiteral("pane")).toString());
        if (it != m_panes.end() && it->hooks.queueResume) it->hooks.queueResume();
    } else if (kind == QLatin1String("queue_edit")) {
        // The hub is holding a device's request open for this answer, keyed by `id`, so every
        // path answers: the text on success, `ok: false` when the row cannot be taken back.
        const QString paneId = message.value(QStringLiteral("pane")).toString();
        const QString row = message.value(QStringLiteral("row")).toString();
        QJsonObject reply{{"t", QStringLiteral("queue_edit_text")}, {"pane", paneId}, {"row", row},
                          {"id", message.value(QStringLiteral("id"))}};
        QString text;
        auto it = m_panes.find(paneId);
        if (it != m_panes.end() && it->hooks.queueEdit && it->hooks.queueEdit(row, &text)) {
            reply.insert(QStringLiteral("text"), text);
        } else {
            reply.insert(QStringLiteral("ok"), false);
            reply.insert(QStringLiteral("error"), QStringLiteral("that row is no longer waiting."));
        }
        send(reply);
    } else if (kind == QLatin1String("model_pick")) {
        auto it = m_panes.find(message.value(QStringLiteral("pane")).toString());
        if (it != m_panes.end() && it->hooks.modelPick)
            it->hooks.modelPick(message.value(QStringLiteral("choice")).toString(),
                                message.value(QStringLiteral("device_name")).toString());
    } else if (kind == QLatin1String("effort_pick")) {
        // A refused pick is answered by the pane's own state (card #EFT9). `remoteEffortPick`
        // returns false for the two refusals a client can reach — a model whose level is fixed,
        // and a model that changed between the state the client drew and the tap — and a refusal
        // changes nothing on the pane, so nothing else would republish and the client would keep
        // the rejected word for the rest of the session. `publishPaneState` forces one with a
        // fresh `seq`, so the authoritative level overwrites the guess, on every watching device:
        // they all drew the same stale chip. The sibling `conversation_id` answers the one device
        // that asked instead, which it must — it carries an id nobody else may see — but a
        // reasoning level is not secret and is already in every state, so the state is both the
        // cheaper answer and the more correct one. `true` when the level was already the pane's,
        // which is nothing to correct.
        auto it = m_panes.find(message.value(QStringLiteral("pane")).toString());
        if (it != m_panes.end() && it->hooks.effortPick) {
            const std::function<void()> publish = it->hooks.publishPaneState;
            const bool took = it->hooks.effortPick(message.value(QStringLiteral("effort")).toString(),
                                                   message.value(QStringLiteral("device_name")).toString());
            if (!took && publish) publish();
        }
    } else if (kind == QLatin1String("conversation_new")) {
        auto it = m_panes.find(message.value(QStringLiteral("pane")).toString());
        if (it != m_panes.end() && it->hooks.conversationNew)
            it->hooks.conversationNew(message.value(QStringLiteral("device_name")).toString());
    } else if (kind == QLatin1String("invite_sent")) {
        emit inviteSent(message.value(QStringLiteral("ok")).toBool(),
                        message.value(QStringLiteral("message")).toString());
    } else if (kind == QLatin1String("conversation_open")) {
        auto it = m_panes.find(message.value(QStringLiteral("pane")).toString());
        if (it != m_panes.end() && it->hooks.conversationOpen)
            it->hooks.conversationOpen(message.value(QStringLiteral("session")).toString(),
                                       message.value(QStringLiteral("device_name")).toString());
    } else if (kind == QLatin1String("conversation_id")) {
        // The hub holds the device's ask open for this answer, keyed by `id`, so every path
        // answers: the id on success, `ok: false` when the token is not the pane's to give.
        const QString paneId = message.value(QStringLiteral("pane")).toString();
        const QString session = message.value(QStringLiteral("session")).toString();
        QJsonObject reply{{"t", QStringLiteral("conversation_id_text")}, {"pane", paneId},
                          {"session", session}, {"id", message.value(QStringLiteral("id"))}};
        auto it = m_panes.find(paneId);
        if (it != m_panes.end() && it->hooks.conversationId) {
            const QString conversation = it->hooks.conversationId(session);
            if (!conversation.isEmpty()) {
                reply.insert(QStringLiteral("conversation"), conversation);
            } else {
                reply.insert(QStringLiteral("ok"), false);
                reply.insert(QStringLiteral("error"),
                             QStringLiteral("that conversation is not in the pane's list anymore."));
            }
        } else {
            reply.insert(QStringLiteral("ok"), false);
            reply.insert(QStringLiteral("error"), QStringLiteral("that pane is not shared anymore."));
        }
        send(reply);
    } else if (kind == QLatin1String("recap_request")) {
        // Forwarded by the sidecar since the phone first had a recap button, and dropped here
        // until now (relay-terminal-71, 2026-09-18).
        auto it = m_panes.find(message.value(QStringLiteral("pane")).toString());
        if (it != m_panes.end() && it->hooks.recap) it->hooks.recap();
    } else if (kind == QLatin1String("secret_input")) {
        const QString paneId = message.value(QStringLiteral("pane")).toString();
        auto it = m_panes.find(paneId);
        if (it != m_panes.end() && it->hooks.secret) {
            // The sidecar checked the nonce and the prompt; this last check is the fresh termios
            // read at the moment of the write (section 6.7), so the line can never land at the
            // shell instead of the prompt that asked for it.
            it->hooks.secret(QByteArray::fromBase64(
                message.value(QStringLiteral("bytes")).toString().toLatin1()));
        }
    } else if (kind == QLatin1String("voice")) {
        // A clip recorded on a phone. It is handed to the pane's own worker, which holds the
        // key; this process decodes the base64 and nothing more.
        const QString paneId = message.value(QStringLiteral("pane")).toString();
        const QString requestId = message.value(QStringLiteral("id")).toString();
        auto it = m_panes.find(paneId);
        if (it == m_panes.end() || !it->hooks.transcribe) {
            voiceResult(paneId, requestId, false, QString(),
                        QStringLiteral("That pane is no longer shared."));
        } else {
            it->hooks.transcribe(requestId,
                                 QByteArray::fromBase64(
                                     message.value(QStringLiteral("data")).toString().toLatin1()),
                                 message.value(QStringLiteral("format")).toString());
        }
    } else if (kind == QLatin1String("history")) {
        sendHistoryPage(message);
    } else if (kind == QLatin1String("invite")) {
        // QA hook, the same rule as the pairing one above: an invite link holds an unguessable
        // secret in its fragment, so it is never logged and only written out when asked for.
        const QByteArray dump = qgetenv("RELAY_REMOTE_INVITE_FILE");
        if (!dump.isEmpty()) {
            QFile file(QString::fromLocal8Bit(dump));
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
                file.write(message.value(QStringLiteral("url")).toString().toUtf8() + '\n');
            }
        }
        emit inviteReady(message.value(QStringLiteral("url")).toString(),
                         qrMatrixOf(message.value(QStringLiteral("qr")).toArray()),
                         message.value(QStringLiteral("role")).toString(),
                         message.value(QStringLiteral("uses")).toInt(),
                         message.value(QStringLiteral("expires")).toInt());
        requestParticipants();
    } else if (kind == QLatin1String("code")) {
        // QA hook, the same rule as the invite one above: the PIN is the secret half of a meeting
        // code (#97EG), so it is never logged and only written out when a driver asks for it.
        const QString code = message.value(QStringLiteral("code")).toString();
        const QString pin = message.value(QStringLiteral("pin")).toString();
        const QByteArray dump = qgetenv("RELAY_REMOTE_CODE_FILE");
        if (!dump.isEmpty()) {
            QFile file(QString::fromLocal8Bit(dump));
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
                file.write((code + QLatin1Char(' ') + pin).toUtf8() + '\n');
            }
        }
        emit codeReady(code, pin, message.value(QStringLiteral("expires")).toInt(600));
        requestParticipants();   // the invite behind the code is a row on the Sharing pane
    } else if (kind == QLatin1String("code_state")) {
        emit codeStateChanged(message.value(QStringLiteral("code")).toString(),
                              message.value(QStringLiteral("state")).toString(),
                              message.value(QStringLiteral("failures")).toInt());
        requestParticipants();
    } else if (kind == QLatin1String("pair_code")) {
        // The pairing code (#FR1C): #97EG's meeting code with a pairing fragment behind it. The
        // PIN is the secret half, so it is never logged — the same QA hook as the invite code's,
        // under its own variable because a driver pairing a phone is not a driver inviting a guest.
        remotesettings::PairCode parsed;
        if (!remotesettings::parsePairCode(message, &parsed)) return;
        const QByteArray dump = qgetenv("RELAY_REMOTE_PAIR_CODE_FILE");
        if (!dump.isEmpty()) {
            QFile file(QString::fromLocal8Bit(dump));
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
                file.write((parsed.code + QLatin1Char(' ') + parsed.pin).toUtf8() + '\n');
            }
        }
        emit pairCodeReady(parsed.code, parsed.pin, parsed.expires);
    } else if (kind == QLatin1String("pair_code_state")) {
        remotesettings::PairCodeState parsed;
        if (!remotesettings::parsePairCodeState(message, &parsed)) return;
        emit pairCodeStateChanged(parsed.code, parsed.state, parsed.failures);
    } else if (kind == QLatin1String("knock")) {
        m_sharing.addKnock(message, QDateTime::currentMSecsSinceEpoch());
        emit sharingModelChanged();
        emit needsOwner(message.value(QStringLiteral("pane")).toString(),
                        QStringLiteral("%1 wants to join a shared pane")
                            .arg(message.value(QStringLiteral("name")).toString()),
                        QStringLiteral("Code %1 — refuse or admit them on the Sharing pane.")
                            .arg(message.value(QStringLiteral("code")).toString()));
    } else if (kind == QLatin1String("participants")) {
        m_sharing.setParticipants(message.value(QStringLiteral("items")).toArray(),
                                  message.value(QStringLiteral("invites")).toArray());
        emit sharingModelChanged();
    } else if (kind == QLatin1String("request_gone")) {
        // A knock, a prompt or a control request the owner decided from a `full` phone (#PH0N),
        // or that lapsed on the hub: its row goes now rather than counting down to nothing.
        const QString what = message.value(QStringLiteral("kind")).toString();
        const auto which = what == QLatin1String("knock") ? sharing::Request::Kind::Knock
                         : what == QLatin1String("prompt") ? sharing::Request::Kind::Prompt
                                                           : sharing::Request::Kind::Control;
        m_sharing.dropRequest(which, message.value(QStringLiteral("id")).toString());
        emit sharingModelChanged();
        if (which == sharing::Request::Kind::Knock) requestParticipants();
    } else if (kind == QLatin1String("control_ask")) {
        m_sharing.addControlAsk(message, QDateTime::currentMSecsSinceEpoch());
        emit sharingModelChanged();
        emit needsOwner(message.value(QStringLiteral("pane")).toString(),
                        QStringLiteral("%1 asks to type in a shared pane")
                            .arg(message.value(QStringLiteral("name")).toString()),
                        QStringLiteral("They get this pane's keyboard until you take it back."));
    } else if (kind == QLatin1String("prompt_ask")) {
        m_sharing.addPromptAsk(message, QDateTime::currentMSecsSinceEpoch());
        emit sharingModelChanged();
        emit needsOwner(message.value(QStringLiteral("pane")).toString(),
                        QStringLiteral("%1 wrote a prompt for your agent")
                            .arg(message.value(QStringLiteral("name")).toString()),
                        message.value(QStringLiteral("text")).toString());
    } else if (kind == QLatin1String("control")) {
        m_sharing.setControl(message.value(QStringLiteral("pane")).toString(),
                             message.value(QStringLiteral("holder")).toString(),
                             message.value(QStringLiteral("name")).toString(),
                             message.value(QStringLiteral("device")).toString(),
                             message.value(QStringLiteral("device_name")).toString());
        emit sharingModelChanged();
    } else if (kind == QLatin1String("share_state")) {
        // Why guests cannot act: "owner" (you paused it) or "away" (present-only, and this is not
        // the window you are looking at). The second one the GUI never asked for, so it has to be
        // told rather than assumed.
        m_sharing.setShareState(message.value(QStringLiteral("pane")).toString(),
                                message.value(QStringLiteral("paused")).toBool(),
                                message.value(QStringLiteral("reason")).toString());
        emit sharingModelChanged();
    } else if (kind == QLatin1String("agent_stop")) {
        const QString paneId = message.value(QStringLiteral("pane")).toString();
        auto it = m_panes.find(paneId);
        if (it != m_panes.end() && it->hooks.stopAgent) it->hooks.stopAgent();
    } else if (kind == QLatin1String("resend")) {
        const QString paneId = message.value(QStringLiteral("pane")).toString();
        auto it = m_panes.find(paneId);
        if (it != m_panes.end()) it->needFull = true;
    } else if (kind == QLatin1String("board_request")) {
        // The Switchboard from one of the owner's devices (#SWPH). Nothing is decided here: the
        // bridge re-checks the request against the allow-list and answers with sendBoardEvent().
        emit boardRequest(message);
    } else if (kind == QLatin1String("remote_state")) {
        // The service's own state (#PH0N): on, where it is registered, whether that registration
        // is live and how many of the owner's devices are connected. Sent whenever any of it
        // changes, including a reconnection after a drop, so the chrome never has to poll.
        m_remoteState = remotesettings::parseState(message);
        emit remoteStateChanged();
    } else if (kind == QLatin1String("error")) {
        emit failed(message.value(QStringLiteral("message")).toString());
    } else if (kind == QLatin1String("stopped")) {
        m_running = false;
        m_remoteState = remotesettings::State{};
        emit startedChanged();
        emit remoteStateChanged();
    }
}

// ----- remote control as a service (#PH0N, phase 1) --------------------------------------------

bool RemoteShare::alwaysOn() const { return remotesettings::alwaysOn(); }

void RemoteShare::startAtLaunch()
{
    if (!remotesettings::alwaysOn()) return;
    QString error;
    if (!ensureSidecar(&error)) emit failed(error);
}

void RemoteShare::setAlwaysOn(bool on)
{
    if (!on) {
        remotesettings::setAlwaysOn(false);
        // Stop publishing. Every pane is withdrawn and the service comes down, which is what the
        // switch says: the sidecar has no "stay up but publish nothing" line, and inventing one
        // here would leave a registration nobody can see.
        stopAll();
        m_remoteState = remotesettings::State{};
        emit remoteStateChanged();
        emit alwaysOnChanged(false);
        return;
    }
    remotesettings::setAlwaysOn(true);          // ensureSidecar reads it while building `start`
    QString error;
    if (!m_process) {
        if (!ensureSidecar(&error)) {
            remotesettings::setAlwaysOn(false);
            emit failed(error);
            emit alwaysOnChanged(false);
            return;
        }
    } else {
        // Already running for an ordinary share: the same `start` line again is how the service
        // is told it is on now, rather than a second name for the same thing.
        send(remotesettings::startMessage(desktopName(), true, remotesettings::address()));
    }
    emit alwaysOnChanged(true);
}

void RemoteShare::setRemoteAddress(const QString &value)
{
    if (value.isEmpty() || value == remotesettings::address()) return;
    remotesettings::setAddress(value);
    if (m_process && remotesettings::alwaysOn())
        send(remotesettings::startMessage(desktopName(), true, value));
    emit remoteStateChanged();     // the page and the chrome are showing the old address
}

void RemoteShare::voiceResult(const QString &paneId, const QString &requestId, bool ok,
                              const QString &text, const QString &error)
{
    QJsonObject reply{{"t", QStringLiteral("transcribed")}, {"pane", paneId},
                      {"id", requestId}, {"ok", ok}};
    if (ok) reply.insert(QStringLiteral("text"), text);
    else reply.insert(QStringLiteral("error"), error);
    send(reply);
}

void RemoteShare::paneState(const QString &paneId, const QJsonObject &state)
{
    if (!m_panes.contains(paneId)) return;   // stopped sharing between the publish and here
    QJsonObject message = state;
    message.insert(QStringLiteral("t"), QStringLiteral("pane_state"));
    message.insert(QStringLiteral("pane"), paneId);
    send(message);
}

bool RemoteShare::sharePane(const QString &paneId, const PaneHooks &hooks, QString *error,
                            const QString &tab)
{
    if (!hooks.view) {
        if (error) {
            *error = QStringLiteral("This pane has no screen to share.");
        }
        return false;
    }
    if (!ensureSidecar(error)) return false;

    Shared shared;
    shared.hooks = hooks;
    shared.tab = tab;
    m_panes.insert(paneId, shared);
    sendPane(paneId);
    connect(hooks.view, &TerminalView::frameChanged, this, [this, paneId] {
        sendFrame(paneId, false);
    });
    connect(hooks.view, &QObject::destroyed, this, [this, paneId] { stopSharing(paneId); });
    sendFrame(paneId, true);
    refreshSharedPanes();
    requestParticipants();
    emit sharingChanged();
    return true;
}

void RemoteShare::stopSharing(const QString &paneId)
{
    if (!m_panes.remove(paneId)) return;
    m_allTabsPanes.remove(paneId);
    send({{"t", "unpane"}, {"id", paneId}});
    refreshSharedPanes();
    emit sharingChanged();
}

void RemoteShare::stopAll()
{
    for (const QString &paneId : m_panes.keys()) send({{"t", "unpane"}, {"id", paneId}});
    m_panes.clear();
    m_allTabsPanes.clear();
    m_allTabsShared = false;
    if (!m_tabShares.isEmpty()) {
        m_tabShares.clear();
        emit tabSharesChanged();
    }
    send({{"t", "stop"}});
    m_poll->stop();
    refreshSharedPanes();
    emit sharingChanged();
}

QStringList RemoteShare::panesInTab(const QString &tab) const
{
    QStringList panes;
    if (tab.isEmpty()) return panes;
    for (auto it = m_panes.cbegin(); it != m_panes.cend(); ++it)
        if (it->tab == tab) panes.append(it.key());
    panes.sort();
    return panes;
}

void RemoteShare::shareTab(const QString &tab)
{
    if (tab.isEmpty() || m_tabShares.contains(tab)) return;
    m_tabShares.insert(tab);
    emit tabSharesChanged();     // the window shares the tab's panes under it
    emit sharingChanged();
}

void RemoteShare::unshareTab(const QString &tab)
{
    if (!m_tabShares.remove(tab)) return;
    if (m_allTabsShared) {
        // End only this tab's guests. Its panes remain published for All tabs, and changing their
        // tab field to empty keeps later panes from regrowing the tab scope we just ended.
        send({{"t", "scope_end"}, {"tab", tab}});
        for (const QString &paneId : panesInTab(tab)) setPaneTab(paneId, QString());
        emit tabSharesChanged();
        emit sharingChanged();
        return;
    }
    // Every pane that is shared because the tab was ends its share, which is what the owner
    // asked for: the tab's guests leave, its invites burn, and nothing more of it is published.
    for (const QString &paneId : panesInTab(tab)) endShare(paneId);
    emit tabSharesChanged();
    emit sharingChanged();
}

void RemoteShare::shareAllTabs()
{
    if (m_allTabsShared) return;
    m_allTabsShared = true;
    emit tabSharesChanged();       // every window publishes the panes it already has
    emit sharingChanged();
}

void RemoteShare::unshareAllTabs()
{
    if (!m_allTabsShared) return;
    m_allTabsShared = false;
    // End the desktop-wide invitations and participants even for panes that stay published by a
    // whole-tab share. Unpublishing those panes would also cut off the narrower tab's guests.
    send({{"t", "scope_end"}, {"tab", allTabsScope()}});
    const QSet<QString> automatic = m_allTabsPanes;
    m_allTabsPanes.clear();
    for (const QString &paneId : automatic) {
        const auto it = m_panes.constFind(paneId);
        if (it != m_panes.cend() && m_tabShares.contains(it->tab)) continue;
        stopSharing(paneId);
    }
    emit tabSharesChanged();
    emit sharingChanged();
}

void RemoteShare::setPaneTab(const QString &paneId, const QString &tab)
{
    auto it = m_panes.find(paneId);
    if (it == m_panes.end() || it->tab == tab) return;
    it->tab = tab;
    sendPane(paneId);            // the sidecar grows or shrinks the tabs' guests from this line
    requestParticipants();
    emit sharingChanged();
}

void RemoteShare::sendPane(const QString &paneId)
{
    auto it = m_panes.find(paneId);
    if (it == m_panes.end()) return;
    const QString title = it->hooks.title ? it->hooks.title() : paneId;
    const QString cwd = it->hooks.cwd ? it->hooks.cwd() : QString();
    const QString status = it->hooks.status ? it->hooks.status() : QStringLiteral("idle");
    if (it->statusSince == 0 || status != it->lastStatus)
        it->statusSince = QDateTime::currentSecsSinceEpoch();
    it->lastTitle = title;
    it->lastCwd = cwd;
    it->lastStatus = status;
    const ViewportFrame &frame = it->hooks.view->frame();
    QJsonObject message{{"t", "pane"}, {"id", paneId}, {"title", title}, {"cwd", cwd},
                         {"status", status}, {"rows", frame.rows}, {"cols", frame.columns},
                         {"updated", static_cast<double>(it->statusSince)}};
    if (!it->tab.isEmpty()) message["tab"] = it->tab;
    if (it->hooks.shellPid) message["pid"] = static_cast<double>(it->hooks.shellPid());
    if (it->hooks.foregroundPid)
        message["foreground_pid"] = static_cast<double>(it->hooks.foregroundPid());
    send(message);
}

void RemoteShare::sendFrame(const QString &paneId, bool full)
{
    auto it = m_panes.find(paneId);
    if (it == m_panes.end() || !it->hooks.view) return;
    const bool everything = full || it->needFull;
    it->needFull = false;
    QJsonObject message = screenjson::frameOf(it->hooks.view->frame(), everything);
    message["t"] = QStringLiteral("frame");
    message["pane"] = paneId;
    message["full"] = everything || message.value(QStringLiteral("rows")).isDouble();
    send(message);
}

// A page of scrollback for a phone (docs/REMOTE-PROTOCOL.md section 6.5). `before_row` is the
// absolute row the page ends just below; a negative one asks for the newest page. The rows go out
// through screenjson::rowOf(), the same serializer sendFrame() uses, so a history row and a live
// row cannot end up different shapes.
void RemoteShare::sendHistoryPage(const QJsonObject &request)
{
    const QString paneId = request.value(QStringLiteral("pane")).toString();
    const QString requestId = request.value(QStringLiteral("id")).toString();
    QJsonObject reply{{"t", QStringLiteral("history_page")}, {"pane", paneId}, {"id", requestId}};

    auto it = m_panes.find(paneId);
    if (it == m_panes.end() || !it->hooks.view || !it->hooks.view->session()) {
        reply.insert(QStringLiteral("ok"), false);
        reply.insert(QStringLiteral("error"), QStringLiteral("That pane is no longer shared."));
        send(reply);
        return;
    }

    const int count = qBound(1, request.value(QStringLiteral("count")).toInt(60), kHistoryPage);
    const int endRow = request.value(QStringLiteral("before_row")).toInt(-1);
    int total = 0, from = 0;
    std::vector<Line> lines;
    it->hooks.view->session()->withCore([&](VtCore &core) {
        total = core.historyRows();
        const int end = qBound(0, endRow >= 0 ? endRow : total, total);
        const int want = qMin(count, end);
        from = core.historyLines(end - want, want, &lines);
    });

    QJsonArray rows;
    for (int index = 0; index < int(lines.size()); ++index)
        rows.append(screenjson::rowOf(lines[size_t(index)], from + index));
    reply.insert(QStringLiteral("ok"), true);
    reply.insert(QStringLiteral("from_row"), from);
    reply.insert(QStringLiteral("total"), total);
    reply.insert(QStringLiteral("more"), from > 0);
    reply.insert(QStringLiteral("lines"), rows);
    send(reply);
}

void RemoteShare::paneEvent(const QString &paneId, const QJsonObject &event)
{
    if (!m_panes.contains(paneId)) return;
    send({{"t", "agent"}, {"pane", paneId}, {"event", event}});
}

void RemoteShare::poll()
{
    for (auto it = m_panes.begin(); it != m_panes.end(); ++it) {
        const QString title = it->hooks.title ? it->hooks.title() : QString();
        const QString cwd = it->hooks.cwd ? it->hooks.cwd() : QString();
        const QString status = it->hooks.status ? it->hooks.status() : QString();
        if (title != it->lastTitle || cwd != it->lastCwd || status != it->lastStatus) {
            sendPane(it.key());
            refreshSharedPanes();
        }
    }
}

void RemoteShare::requestPairing() { send({{"t", "pair"}}); }

// The pairing code (#FR1C). The Sharing pane's Devices page mints one when "Add a device…" is
// pressed and withdraws it on leaving the page; the sidecar answers `pair_code`, and
// `pair_code_state` afterwards.
void RemoteShare::requestPairCode() { send(remotesettings::pairCodeRequest()); }

void RemoteShare::revokePairCode(const QString &code)
{
    if (code.isEmpty()) return;
    send(remotesettings::pairCodeRevoke(code));
}

void RemoteShare::requestDevices() { send({{"t", "devices"}}); }

void RemoteShare::useAddress(const QString &address)
{
    send({{"t", "address"}, {"value", address}});
}

void RemoteShare::answer(int askId, bool allow, const QString &capability)
{
    send({{"t", "answer"}, {"id", askId}, {"allow", allow}, {"capability", capability}});
    if (m_pendingAsk.id == askId) m_pendingAsk = sharing::DeviceAsk{};
}

void RemoteShare::revoke(const QString &deviceId)
{
    send({{"t", "revoke"}, {"device", deviceId}});
}

void RemoteShare::setPasswordEntry(const QString &deviceId, bool allow)
{
    send({{"t", "password_entry"}, {"device", deviceId}, {"allow", allow}});
}

// ---- multiplayer: the owner's controls (docs/REMOTE-PROTOCOL.md section 10.5) -------------------
// One method per line, each doing nothing but naming it. The hub refuses every one of these from
// the wire, so this file is the only place they are ever sent from.

void RemoteShare::createInvite(const QString &paneId, const QString &role, int expires, int uses,
                               const QString &tab)
{
    QJsonObject message{{"t", "invite_create"}, {"pane", paneId}, {"role", role},
                        {"expires", expires}, {"uses", uses}};
    if (!tab.isEmpty()) message["tab"] = tab;
    send(message);
}

void RemoteShare::createCode(const QString &paneId, const QString &role, const QString &tab)
{
    QJsonObject message{{"t", "code_create"}, {"pane", paneId}, {"role", role}};
    if (!tab.isEmpty()) message["tab"] = tab;
    send(message);
}

void RemoteShare::revokeCode(const QString &code)
{
    send({{"t", "code_revoke"}, {"code", code}});
}

void RemoteShare::revokeInvite(const QString &inviteId)
{
    send({{"t", "invite_revoke"}, {"id", inviteId}});
    requestParticipants();
}

void RemoteShare::answerKnock(const QString &participant, bool admit, const QString &role)
{
    send({{"t", "knock_answer"}, {"participant", participant}, {"admit", admit}, {"role", role}});
    m_sharing.dropRequest(sharing::Request::Kind::Knock, participant);
    emit sharingModelChanged();
    requestParticipants();
}

void RemoteShare::setRole(const QString &participant, const QString &role)
{
    send({{"t", "role_set"}, {"participant", participant}, {"role", role}});
    requestParticipants();
}

void RemoteShare::removeParticipant(const QString &participant)
{
    send({{"t", "participant_remove"}, {"participant", participant}});
    m_sharing.dropParticipant(participant);
    emit sharingModelChanged();
    requestParticipants();
}

void RemoteShare::answerControl(const QString &paneId, const QString &participant, bool grant)
{
    // No participant and no grant is "give it back to me", whoever has it: `control_revoke`.
    if (participant.isEmpty() && !grant) { send({{"t", "control_revoke"}, {"pane", paneId}}); return; }
    send({{"t", "control_answer"}, {"pane", paneId}, {"participant", participant}, {"grant", grant}});
    m_sharing.dropRequest(sharing::Request::Kind::Control, participant);
    emit sharingModelChanged();
}

void RemoteShare::takeControl(const QString &paneId)
{
    send({{"t", "control_take"}, {"pane", paneId}});
    // Say so here and now rather than waiting for the hub's `control` to come back: the owner has
    // physically typed, and the pane header must not still read "alice is typing" while it does.
    m_sharing.setControl(paneId, QStringLiteral("owner"), QString());
    emit sharingModelChanged();
}

void RemoteShare::answerPrompt(const QString &promptId, bool approve)
{
    send({{"t", "prompt_answer"}, {"id", promptId}, {"approve", approve}});
    m_sharing.dropRequest(sharing::Request::Kind::Prompt, promptId);
    emit sharingModelChanged();
}

void RemoteShare::emailInvite(const QString &url, const QString &to, const QString &role,
                              const QString &expiry, const QString &pane)
{
    // The owner's own name on the mail, from the desktop's name — a colleague should see who is
    // sharing, not a machine id. Nothing about the link changes: this posts what is on screen.
    send({{"t", "invite_email"}, {"url", url}, {"to", to}, {"role", role},
          {"expiry", expiry}, {"pane", pane}, {"from_name", qEnvironmentVariable("RELAY_MAIL_NAME")}});
}

void RemoteShare::pauseShare(const QString &paneId, bool on)
{
    send({{"t", "share_pause"}, {"pane", paneId}, {"on", on}});
    sharing::ShareOptions options = m_sharing.options(paneId);
    options.paused = on;
    m_sharing.setOptions(paneId, options);
    emit sharingModelChanged();
}

void RemoteShare::endShare(const QString &paneId)
{
    send({{"t", "share_end"}, {"pane", paneId}});
    stopSharing(paneId);
    requestParticipants();
}

void RemoteShare::setShareOptions(const QString &paneId, bool promptsImmediate, bool presentOnly)
{
    send({{"t", "share_options"}, {"pane", paneId},
          {"prompts_immediate", promptsImmediate}, {"present_only", presentOnly}});
    sharing::ShareOptions options = m_sharing.options(paneId);
    options.promptsImmediate = promptsImmediate;
    options.presentOnly = presentOnly;
    m_sharing.setOptions(paneId, options);
    emit sharingModelChanged();
}

void RemoteShare::requestParticipants()
{
    send({{"t", "participants"}});
}

// The Sharing pane names a share by the pane's own title, which only the GUI knows, so the list
// is refreshed wherever a title could have changed: sharing, un-sharing and the title poll.
void RemoteShare::refreshSharedPanes()
{
    QList<sharing::SharedPane> panes;
    for (auto it = m_panes.constBegin(); it != m_panes.constEnd(); ++it) {
        sharing::SharedPane pane;
        pane.id = it.key();
        pane.title = it->lastTitle.isEmpty() && it->hooks.title ? it->hooks.title() : it->lastTitle;
        // The scope it is shared under: its tab when that tab is shared whole, the reserved
        // "all-tabs" when it is published only because All tabs is on, "" on its own.
        if (!it->tab.isEmpty()) pane.tab = it->tab;
        else if (m_allTabsShared || m_allTabsPanes.contains(it.key())) pane.tab = allTabsScope();
        panes.append(pane);
    }
    std::sort(panes.begin(), panes.end(), [](const sharing::SharedPane &a, const sharing::SharedPane &b) {
        return a.id < b.id;
    });
    if (panes == m_sharing.sharedPanes()) return;
    m_sharing.setSharedPanes(panes);
    emit sharingModelChanged();
}

// ---- the Sharing pane (#SMDX) ------------------------------------------------------------------

sharing::Service RemoteShare::service() const
{
    sharing::Service service;
    service.running = m_running;
    service.alwaysOn = alwaysOn();
    service.online = m_remoteState.online;
    service.base = m_base;
    service.onlineBase = m_remoteState.base;
    service.note = m_note;
    // The address the service is registered at, or the remembered one before the sidecar has
    // said: the same choice the window's plug makes, so the page and the chrome agree.
    service.addressLabel = remotesettings::addressName(
        m_remoteState.address.isEmpty() ? remotesettings::address() : m_remoteState.address);
    return service;
}

void RemoteShare::attach(sharing::SharingView *view)
{
    if (!view) return;
    // Every connection has the view as its context, so it goes when the pane does and a line
    // arriving afterwards is delivered to nobody rather than to a dead widget.

    // The pane's top line and the Devices page read the same state the window chrome's plug
    // does (#SHRP); this used to be the window's `feed` lambda and lives here now because the
    // model is fed the same way whichever surface opened.
    auto feed = [this, view] {
        const remotesettings::State &state = m_remoteState;
        const QString where = state.address.isEmpty() ? remotesettings::address() : state.address;
        m_sharing.setRemote(state.on, remotesettings::addressName(where), state.online,
                            state.devices, state.reason);
        view->setService(service());
        view->refresh();
    };
    connect(this, &RemoteShare::startedChanged, view, feed);
    connect(this, &RemoteShare::remoteStateChanged, view, feed);
    connect(this, &RemoteShare::addressesChanged, view,
            [view](const QJsonArray &addresses) { view->setAddresses(addresses); });
    connect(this, &RemoteShare::pairingReady, view,
            [view](const QString &url, const QrMatrix &qr, int expires) {
                view->showPairing(url, qr, expires);
            });
    connect(this, &RemoteShare::pairCodeReady, view,
            [view](const QString &code, const QString &pin, int expires) {
                view->showPairCode(code, pin, expires);
            });
    connect(this, &RemoteShare::pairCodeStateChanged, view,
            [view](const QString &code, const QString &state, int failures) {
                view->showPairCodeState(code, state, failures);
            });
    connect(this, &RemoteShare::pairingAsked, view,
            [view](int id, const QString &name, const QString &platform, const QString &fingerprint,
                   const QString &code, const QString &peer) {
                sharing::DeviceAsk ask;
                ask.id = id;
                ask.name = name;
                ask.platform = platform;
                ask.fingerprint = fingerprint;
                ask.code = code;
                ask.peer = peer;
                view->showAsk(ask);
            });
    connect(this, &RemoteShare::devicesChanged, view, [this, view](const QJsonArray &items) {
        m_sharing.setDevices(items);
        view->refresh();
    });
    connect(this, &RemoteShare::inviteReady, view,
            [view](const QString &url, const QrMatrix &qr, const QString &role, int uses,
                   int expires) { view->showInvite(url, qr, role, uses, expires); });
    connect(this, &RemoteShare::codeReady, view,
            [view](const QString &code, const QString &pin, int expires) {
                view->showCode(code, pin, expires);
            });
    connect(this, &RemoteShare::codeStateChanged, view,
            [view](const QString &code, const QString &state, int failures) {
                view->showCodeState(code, state, failures);
            });
    connect(this, &RemoteShare::inviteSent, view,
            [view](bool ok, const QString &message) { view->inviteSent(ok, message); });
    connect(this, &RemoteShare::failed, view,
            [view](const QString &message) { view->serviceFailed(message); });
    connect(this, &RemoteShare::secondPassed, view, [view] { view->tick(); });

    // The hooks that need only the sidecar. The window sets the rest (onScopes, onRemoteSwitch,
    // onCreateInvite, onCreateCode, onClose, onTitleChanged): they need panes or settings.
    view->onPairRequest = [this] {
        requestPairing();
        requestDevices();
    };
    view->onPairCodeRequest = [this] { requestPairCode(); };
    view->onPairCodeRevoke = [this](const QString &code) { revokePairCode(code); };
    view->onPairAnswer = [this](int askId, bool allow, const QString &capability) {
        answer(askId, allow, capability);
    };
    view->onAddressPick = [this](const QString &address) { useAddress(address); };
    view->onRevokeDevice = [this](const QString &device) { revoke(device); };
    view->onPasswordEntry = [this](const QString &device, bool allow) {
        setPasswordEntry(device, allow);
    };
    view->onRevokeCode = [this](const QString &code) { revokeCode(code); };
    view->onEmailInvite = [this](const QString &url, const QString &to, const QString &role,
                                 const QString &expiry, const QString &what) {
        emailInvite(url, to, role, expiry, what);
    };
    view->onKnockAnswer = [this](const QString &participant, bool admit, const QString &role) {
        answerKnock(participant, admit, role);
    };
    view->onControlAnswer = [this](const QString &pane, const QString &participant, bool grant) {
        answerControl(pane, participant, grant);
    };
    view->onPromptAnswer = [this](const QString &promptId, bool approve) {
        answerPrompt(promptId, approve);
    };
    view->onRoleSet = [this](const QString &participant, const QString &role) {
        setRole(participant, role);
    };
    view->onRemove = [this](const QString &participant) { removeParticipant(participant); };
    view->onRevokeInvite = [this](const QString &inviteId) { revokeInvite(inviteId); };
    view->onPause = [this](const QString &pane, bool on) { pauseShare(pane, on); };
    view->onEndShare = [this](const QString &pane) { endShare(pane); };
    view->onOptions = [this](const QString &pane, bool promptsImmediate, bool presentOnly) {
        setShareOptions(pane, promptsImmediate, presentOnly);
    };

    // Replay what is already known: the pane is usually opened after these lines arrived, and
    // with remote control on (#PH0N) the `devices` line came at launch.
    view->setAddresses(m_addresses);
    m_sharing.setDevices(m_devices);
    feed();
    if (m_pendingAsk.valid()) view->showAsk(m_pendingAsk);
    requestDevices();   // the `online` flags, fresh (#SHRP)
}

} // namespace relay
