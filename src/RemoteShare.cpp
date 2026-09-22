// SPDX-License-Identifier: AGPL-3.0-or-later
#include "RemoteShare.h"
#include "AppPaths.h"
#include "CopyOnSelect.h"

#include "Theme.h"
#include "core/VtCore.h"
#include "session/TerminalSession.h"
#include "tools/ScreenJson.h"
#include "view/TerminalView.h"

#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

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

QPixmap qrPixmap(const QrMatrix &matrix, int target)
{
    if (matrix.isEmpty()) return QPixmap();
    const int cells = matrix.size();
    const int scale = std::max(2, target / cells);
    QPixmap pixmap(cells * scale, cells * scale);
    pixmap.fill(Qt::white);
    QPainter painter(&pixmap);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    for (int row = 0; row < cells; ++row) {
        for (int column = 0; column < matrix[row].size(); ++column) {
            if (matrix[row][column]) painter.drawRect(column * scale, row * scale, scale, scale);
        }
    }
    return pixmap;
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

// The sentence under the invite row before any code exists. The expiry and uses boxes belong to
// the link; a code that quietly ignored them would last longer or admit more than it seemed to.
QString codeIntro()
{
    return QStringLiteral(
        "Make a code to read out instead of sending a link. A code always lasts 10 minutes and "
        "lets in one person, whatever the expiry and uses above say; it grants the role picked "
        "here.");
}

// What the top of the pairing half says before the QR has an expiry to quote. The typed code is
// named first (#FR1C): scanning on an iPhone opens Safari, which pairs a browser tab rather than
// the Home Screen app the notifications go to.
QString pairingIntro()
{
    return QStringLiteral("Type the code beside the QR on your phone, or scan the QR with its "
                          "camera.");
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
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState s) {
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
        emit pairingAsked(message.value(QStringLiteral("id")).toInt(),
                          message.value(QStringLiteral("name")).toString(),
                          message.value(QStringLiteral("platform")).toString(),
                          message.value(QStringLiteral("fingerprint")).toString(),
                          message.value(QStringLiteral("code")).toString(),
                          message.value(QStringLiteral("peer")).toString());
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
        emit codeReady(code, pin, message.value(QStringLiteral("expires")).toInt(600),
                       message.value(QStringLiteral("invite")).toString());
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

// The pairing code (#FR1C). The GUI mints one whenever the pairing dialog opens and withdraws it
// when the dialog closes; the sidecar answers `pair_code`, and `pair_code_state` afterwards.
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
        panes.append(pane);
    }
    std::sort(panes.begin(), panes.end(), [](const sharing::SharedPane &a, const sharing::SharedPane &b) {
        return a.id < b.id;
    });
    if (panes == m_sharing.sharedPanes()) return;
    m_sharing.setSharedPanes(panes);
    emit sharingModelChanged();
}

// ---- the dialog --------------------------------------------------------------------------------

RemoteShareDialog::RemoteShareDialog(const QString &paneId, QWidget *parent)
    : QDialog(parent), m_paneId(paneId)
{
    setWindowTitle(QStringLiteral("Share this pane"));
    setModal(false);
    setMinimumWidth(380);
    auto *column = new QVBoxLayout(this);
    column->setSpacing(10);

    m_status = new QLabel(QStringLiteral("Starting…"));
    m_status->setWordWrap(true);
    column->addWidget(m_status);

    // One line, at the top, because pairing turns remote control on by itself (#FR1C) and a
    // switch that turned itself on must say so where it happened — and say where to turn it off.
    m_alwaysOnLine = new QLabel(relay::remotesettings::pairAlwaysOnLine());
    m_alwaysOnLine->setWordWrap(true);
    m_alwaysOnLine->setTextFormat(Qt::PlainText);
    m_alwaysOnLine->setObjectName(QStringLiteral("settingsRowDetail"));
    m_alwaysOnLine->setVisible(relay::remotesettings::alwaysOn());
    column->addWidget(m_alwaysOnLine);

    // Which address the phone should reach this machine on. Getting it wrong is the most likely
    // reason a phone says it cannot reach the site, so the choice is in front of the QR code.
    m_address = new QComboBox;
    m_address->setToolTip(QStringLiteral(
        "The address your phone will open. The tailnet name comes first when tailscale can serve "
        "it: a real certificate, no warning to accept, and it works from anywhere the phone is "
        "signed in to your tailnet. relay-terminal.ai works from anywhere with no warning; the "
        "links go through it instead of this machine. Use the network address when the phone is "
        "on the same Wi-Fi."));
    connect(m_address, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        const QString address = m_address->itemData(index).toString();
        if (!address.isEmpty()) RemoteShare::instance().useAddress(address);
    });
    column->addWidget(m_address);

    // Why the warning-free address is not on offer, when it is not. An absent entry and an entry
    // that needs one command run once look identical in a list, so the sentence is shown.
    m_addressNote = new QLabel;
    m_addressNote->setWordWrap(true);
    m_addressNote->setObjectName(QStringLiteral("shareNote"));
    m_addressNote->setVisible(false);
    column->addWidget(m_addressNote);

    m_qr = new QLabel;
    m_qr->setAlignment(Qt::AlignCenter);
    m_qr->setFixedSize(260, 260);
    // A QR code must never be squeezed or overlapped: a phone cannot read a partial one. The
    // label has a fixed size and the dialog grows to fit whatever else it has to say.
    auto *pairRow = new QHBoxLayout;
    pairRow->setSpacing(16);
    pairRow->addWidget(m_qr, 0, Qt::AlignTop);

    // Large, fixed-width and letter-spaced: every character here is going to be typed on a phone,
    // and an ambiguous glyph is a code that does not work. The same face the meeting code uses.
    QFont bigCode = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    bigCode.setPixelSize(30);
    bigCode.setWeight(QFont::DemiBold);
    bigCode.setLetterSpacing(QFont::AbsoluteSpacing, 6);

    // The code to type, beside the QR (#FR1C). Scanning the QR on an iPhone opens Safari, which
    // pairs a browser tab that gets no push and is not the Home Screen app; typing four letters
    // and four digits pairs whichever Relay is in the person's hand. Minted when this window
    // opens and withdrawn when it closes.
    m_pairBox = new QWidget;
    auto *pairColumn = new QVBoxLayout(m_pairBox);
    pairColumn->setContentsMargins(0, 0, 0, 0);
    pairColumn->setSpacing(4);
    m_pairHeading = new QLabel(relay::remotesettings::pairCodeHeading());
    m_pairHeading->setWordWrap(true);
    m_pairHeading->setTextFormat(Qt::PlainText);
    pairColumn->addWidget(m_pairHeading);
    m_pairValue = new QLabel;
    m_pairValue->setFont(bigCode);
    m_pairValue->setTextFormat(Qt::PlainText);
    m_pairValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pairColumn->addWidget(m_pairValue);
    m_pairClock = new QLabel;
    m_pairClock->setTextFormat(Qt::PlainText);
    pairColumn->addWidget(m_pairClock);
    m_pairNote = new QLabel;
    m_pairNote->setWordWrap(true);
    m_pairNote->setTextFormat(Qt::PlainText);
    m_pairNote->setObjectName(QStringLiteral("settingsRowDetail"));
    pairColumn->addWidget(m_pairNote);
    m_pairAgain = new QPushButton(QStringLiteral("New code"));
    m_pairAgain->setAutoDefault(false);
    m_pairAgain->hide();
    connect(m_pairAgain, &QPushButton::clicked, this, [this] { askPairCode(); });
    pairColumn->addWidget(m_pairAgain, 0, Qt::AlignLeft);
    pairColumn->addStretch(1);
    pairRow->addWidget(m_pairBox, 1);
    column->addLayout(pairRow);
    column->setSizeConstraint(QLayout::SetMinimumSize);


    m_url = new QLabel;
    m_url->setTextFormat(Qt::PlainText);
    m_url->setWordWrap(true);
    m_url->setTextInteractionFlags(Qt::TextSelectableByMouse);
    relay::installCopyOnSelect(m_url);
    // The theme's muted text at the secondary size: palette(mid) is the border colour, about 1.4:1
    // on the dialog (docs/ARCHITECTURE.md, "Legible text").
    m_url->setObjectName(QStringLiteral("shareNote"));
    auto *urlRow = new QHBoxLayout;
    urlRow->addWidget(m_url, 1);
    // The invite link has had a Copy button since it existed; the pairing link had none, and on a
    // Linux desktop a 140-character link with a one-time secret in its fragment has no other way
    // of reaching a phone that cannot scan (#FR1C).
    m_pairCopy = new QPushButton(QStringLiteral("Copy link"));
    m_pairCopy->setAutoDefault(false);
    m_pairCopy->setToolTip(QStringLiteral(
        "The whole pairing link, secret and all. Send it to your own phone and nowhere else: "
        "whoever opens it first is the device that gets paired."));
    m_pairCopy->setEnabled(false);
    connect(m_pairCopy, &QPushButton::clicked, this, [this] {
        if (m_pairingLink.isEmpty()) return;
        QGuiApplication::clipboard()->setText(m_pairingLink);
        m_pairCopy->setText(QStringLiteral("Copied"));
    });
    urlRow->addWidget(m_pairCopy, 0, Qt::AlignTop);
    column->addLayout(urlRow);

    m_note = new QLabel;
    m_note->setWordWrap(true);
    m_note->setObjectName(QStringLiteral("shareNote"));
    column->addWidget(m_note);

    // The approval box: what a phone claims to be, and the code that proves it is the phone in
    // your hand rather than whoever else saw the QR code.
    m_askBox = new QWidget;
    auto *askColumn = new QVBoxLayout(m_askBox);
    askColumn->setContentsMargins(0, 0, 0, 0);
    m_askText = new QLabel;
    m_askText->setWordWrap(true);
    m_askText->setTextFormat(Qt::PlainText);
    askColumn->addWidget(m_askText);
    m_askCode = new QLabel;
    m_askCode->setAlignment(Qt::AlignCenter);
    // In points, like every other size in the app (docs/ARCHITECTURE.md, "Legible text"): a pixel
    // size ignores the desktop's font scaling. 21pt is the 28px this used to be at 96 dpi.
    m_askCode->setStyleSheet(QStringLiteral("font-size: 21pt; font-weight: 600; letter-spacing: 6px;"));
    askColumn->addWidget(m_askCode);
    auto *askRow = new QHBoxLayout;
    auto *refuse = new QPushButton(QStringLiteral("Refuse"));
    m_refuse = refuse;
    // Watching and typing are separate grants, because they are very different things to hand
    // out: one shows a device everything on the screen, the other gives it the keyboard of a
    // live shell. The protocol already enforces the difference on every message.
    auto *allowView = new QPushButton(QStringLiteral("Allow viewing"));
    allowView->setToolTip(QStringLiteral(
        "The device can watch this pane and read its history. It cannot type."));
    auto *allowType = new QPushButton(QStringLiteral("Allow typing"));
    allowType->setToolTip(QStringLiteral(
        "The device can watch and, after taking over, run anything you could."));
    // Refuse is the default and holds the focus. Allowing is a deliberate click — never a stray
    // Return in a window that just appeared while the person was typing somewhere else.
    refuse->setDefault(true);
    allowView->setAutoDefault(false);
    allowType->setAutoDefault(false);
    askRow->addWidget(refuse);
    askRow->addWidget(allowView);
    askRow->addWidget(allowType);
    askColumn->addLayout(askRow);
    m_askBox->hide();
    column->addWidget(m_askBox);
    connect(refuse, &QPushButton::clicked, this, [this] { answer(false); });
    connect(allowView, &QPushButton::clicked, this, [this] { answer(true, QStringLiteral("view")); });
    connect(allowType, &QPushButton::clicked, this, [this] { answer(true, QStringLiteral("full")); });

    // ----- inviting somebody else (docs/REMOTE-PROTOCOL.md section 10.2) ----------------------
    // Under the QR, not instead of it: pairing your own phone is the common case and stays the
    // first thing offered. This is the second, and it hands out a link rather than a device grant.
    // "Share the whole tab" (owner, 2026-09-18): every pane in the tab, and every pane added to
    // it later, is shared, and a link made while this is ticked lets its guests into all of them.
    // Hidden until the window says which tab this is (setTab).
    m_wholeTab = new QCheckBox(QStringLiteral("Share the whole tab"));
    m_wholeTab->hide();
    connect(m_wholeTab, &QCheckBox::toggled, this, [this](bool on) {
        RemoteShare &share = RemoteShare::instance();
        if (on) share.shareTab(m_tab);
        else share.unshareTab(m_tab);
        updateWholeTab();
    });
    column->addWidget(m_wholeTab);

    m_allTabs = new QCheckBox(QStringLiteral("Share all tabs"));
    m_allTabs->setToolTip(QStringLiteral(
        "Every pane in every tab is shared, including panes and tabs you create later. A link or "
        "meeting code made while this is ticked gives its guests that desktop-wide scope."));
    connect(m_allTabs, &QCheckBox::toggled, this, [this](bool on) {
        RemoteShare &share = RemoteShare::instance();
        if (on) share.shareAllTabs();
        else share.unshareAllTabs();
        updateWholeTab();
    });
    column->addWidget(m_allTabs);

    m_inviteHeading = new QLabel(QStringLiteral("Invite someone to this pane"));
    m_inviteHeading->setObjectName(QStringLiteral("settingsHeading"));
    column->addWidget(m_inviteHeading);

    auto *inviteRow = new QHBoxLayout;
    m_inviteRole = new QComboBox;
    m_inviteRole->addItem(QStringLiteral("Viewer"), QStringLiteral("viewer"));
    m_inviteRole->addItem(QStringLiteral("Editor"), QStringLiteral("editor"));
    connect(m_inviteRole, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { updateRoleNote(); });
    inviteRow->addWidget(m_inviteRole);
    m_inviteExpiry = new QComboBox;
    m_inviteExpiry->addItem(QStringLiteral("for 1 hour"), 3600);
    m_inviteExpiry->addItem(QStringLiteral("for 24 hours"), 86400);
    m_inviteExpiry->addItem(QStringLiteral("for 7 days"), 604800);
    m_inviteExpiry->setCurrentIndex(1);
    inviteRow->addWidget(m_inviteExpiry);
    m_inviteUses = new QSpinBox;
    m_inviteUses->setRange(1, 20);
    m_inviteUses->setValue(1);
    m_inviteUses->setPrefix(QStringLiteral("uses: "));
    m_inviteUses->setToolTip(QStringLiteral(
        "How many people the link may let in. One link, one person, is the usual thing."));
    inviteRow->addWidget(m_inviteUses);
    auto *makeLink = new QPushButton(QStringLiteral("Make a link"));
    makeLink->setAutoDefault(false);
    connect(makeLink, &QPushButton::clicked, this, [this] { createInvite(); });
    inviteRow->addWidget(makeLink);
    // The other way to hand out the same door: two short things to say out loud, for a friend who
    // is on the phone or across the room rather than in a chat window (#97EG).
    m_makeCode = new QPushButton(QStringLiteral("Make a code"));
    m_makeCode->setAutoDefault(false);
    m_makeCode->setToolTip(QStringLiteral(
        "A four-letter meeting code and a four-digit PIN to read out. It grants the role picked "
        "here, lasts 10 minutes and lets in one person."));
    connect(m_makeCode, &QPushButton::clicked, this, [this] { createCode(); });
    inviteRow->addWidget(m_makeCode);
    inviteRow->addStretch(1);
    column->addLayout(inviteRow);

    m_inviteNote = new QLabel;
    m_inviteNote->setWordWrap(true);
    m_inviteNote->setTextFormat(Qt::PlainText);
    m_inviteNote->setObjectName(QStringLiteral("settingsRowDetail"));
    column->addWidget(m_inviteNote);
    updateRoleNote();

    // The link, its QR and a Copy button. Smaller than the pairing QR on purpose: this one is
    // read by somebody else's camera across a desk if at all, and the usual way it travels is the
    // Copy button — while the pairing QR above is the one being held up to a phone right now.
    auto *linkRow = new QHBoxLayout;
    m_inviteQr = new QLabel;
    m_inviteQr->setAlignment(Qt::AlignCenter);
    m_inviteQr->hide();
    linkRow->addWidget(m_inviteQr, 0, Qt::AlignTop);
    auto *linkColumn = new QVBoxLayout;
    // A read-only field rather than a label: a link is one long unbreakable word, so a label
    // either stretches the window to its full length or silently cuts the end off — and the end
    // is the secret. A field scrolls, selects, and answers Ctrl+A and Ctrl+C.
    m_inviteUrl = new QLineEdit;
    m_inviteUrl->setReadOnly(true);
    relay::installCopyOnSelect(m_inviteUrl);
    m_inviteUrl->setCursorPosition(0);
    m_inviteUrl->setToolTip(QStringLiteral("The whole link. Copy it and send it to one person; "
                                           "anyone who has it can knock."));
    m_inviteUrl->hide();
    linkColumn->addWidget(m_inviteUrl);
    m_inviteCopy = new QPushButton(QStringLiteral("Copy link"));
    m_inviteCopy->setAutoDefault(false);
    m_inviteCopy->hide();
    connect(m_inviteCopy, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_inviteLink);
        m_inviteCopy->setText(QStringLiteral("Copied"));
    });
    auto *sendRow = new QHBoxLayout;
    sendRow->addWidget(m_inviteCopy, 0, Qt::AlignLeft);
    // Email is the same act as copying: for a colleague who is not in the room, and who a chat
    // window would not reach. The link is unchanged — one use over a public address, and they
    // still have to knock.
    m_inviteTo = new QLineEdit;
    m_inviteTo->setPlaceholderText(QStringLiteral("or email it to…"));
    m_inviteTo->setClearButtonEnabled(true);
    m_inviteTo->hide();
    sendRow->addWidget(m_inviteTo, 1);
    m_inviteSend = new QPushButton(QStringLiteral("Send"));
    m_inviteSend->setAutoDefault(false);
    m_inviteSend->hide();
    connect(m_inviteSend, &QPushButton::clicked, this, [this] {
        const QString to = m_inviteTo->text().trimmed();
        if (m_inviteLink.isEmpty() || to.isEmpty()) return;
        m_inviteSend->setEnabled(false);
        m_inviteNote->setText(QStringLiteral("Sending to %1…").arg(to));
        RemoteShare::instance().emailInvite(m_inviteLink, to, m_inviteRoleValue,
                                            m_inviteExpiryText, windowTitle());
    });
    connect(m_inviteTo, &QLineEdit::returnPressed, m_inviteSend, &QPushButton::click);
    sendRow->addWidget(m_inviteSend);
    linkColumn->addLayout(sendRow);
    linkColumn->addStretch(1);
    linkRow->addLayout(linkColumn, 1);
    column->addLayout(linkRow);

    // ----- the meeting code (#97EG) -------------------------------------------------------------
    // Its own sentence, under the row: the expiry and uses boxes belong to the link, and a code
    // that quietly ignored them would be a code that lasts longer or admits more than it seems.
    m_codeNote = new QLabel(codeIntro());
    m_codeNote->setWordWrap(true);
    m_codeNote->setTextFormat(Qt::PlainText);
    m_codeNote->setObjectName(QStringLiteral("settingsRowDetail"));
    column->addWidget(m_codeNote);

    // Large, fixed-width and letter-spaced, because each character is going to be read aloud and
    // typed by somebody else: an ambiguous glyph here is a wrong PIN there.
    m_codeBox = new QWidget;
    auto *codeRow = new QHBoxLayout(m_codeBox);
    codeRow->setContentsMargins(0, 0, 0, 0);
    codeRow->setSpacing(24);
    const QFont big = bigCode;   // the pairing code's face, built above: one look for both codes
    auto value = [&](const QString &caption, QLabel **target) {
        auto *cell = new QVBoxLayout;
        cell->setSpacing(2);
        auto *label = new QLabel(caption);
        label->setObjectName(QStringLiteral("settingsRowDetail"));
        cell->addWidget(label);
        *target = new QLabel;
        (*target)->setFont(big);
        (*target)->setTextFormat(Qt::PlainText);
        (*target)->setTextInteractionFlags(Qt::TextSelectableByMouse);
        cell->addWidget(*target);
        codeRow->addLayout(cell);
    };
    value(QStringLiteral("Meeting code"), &m_codeValue);
    value(QStringLiteral("PIN"), &m_pinValue);
    auto *codeSide = new QVBoxLayout;
    codeSide->setSpacing(4);
    m_codeClock = new QLabel;
    m_codeClock->setTextFormat(Qt::PlainText);
    codeSide->addWidget(m_codeClock);
    auto *codeButtons = new QHBoxLayout;
    m_codeCopy = new QPushButton(QStringLiteral("Copy"));
    m_codeCopy->setAutoDefault(false);
    m_codeCopy->setToolTip(QStringLiteral(
        "Copies one line with the join address, the meeting code and the PIN, ready to send."));
    connect(m_codeCopy, &QPushButton::clicked, this, [this] {
        if (m_code.isEmpty()) return;
        QString base = RemoteShare::instance().base();
        while (base.endsWith(QLatin1Char('/'))) base.chop(1);
        QGuiApplication::clipboard()->setText(
            QStringLiteral("Join my Relay pane at %1/join — meeting code %2, PIN %3")
                .arg(base, m_code, m_pin));
        m_codeCopy->setText(QStringLiteral("Copied"));
    });
    codeButtons->addWidget(m_codeCopy);
    m_codeAgain = new QPushButton(QStringLiteral("Make a new code"));
    m_codeAgain->setAutoDefault(false);
    m_codeAgain->hide();
    connect(m_codeAgain, &QPushButton::clicked, this, [this] { createCode(); });
    codeButtons->addWidget(m_codeAgain);
    codeButtons->addStretch(1);
    codeSide->addLayout(codeButtons);
    codeRow->addLayout(codeSide, 1);
    m_codeBox->hide();
    column->addWidget(m_codeBox);
    // Height the window keeps after a longer sentence goes collects here, not between the lines.
    column->addStretch(1);

    m_devices = new QListWidget;
    m_devices->setMaximumHeight(90);
    column->addWidget(m_devices);

    auto *buttons = new QHBoxLayout;
    auto *revoke = new QPushButton(QStringLiteral("Revoke selected"));
    connect(revoke, &QPushButton::clicked, this, [this] {
        const auto *item = m_devices->currentItem();
        if (item) RemoteShare::instance().revoke(item->data(Qt::UserRole).toString());
    });
    buttons->addWidget(revoke);
    // Password entry is its own grant, off by default, because a password typed on a phone is
    // the one input that can end up somewhere a keystroke must never go (section 6.7).
    m_passwords = new QPushButton;
    m_passwords->setToolTip(QStringLiteral(
        "Whether this device may answer a password prompt. Off until you turn it on, and only "
        "ever for a pane that is at a prompt right now."));
    connect(m_passwords, &QPushButton::clicked, this, [this] {
        const auto *item = m_devices->currentItem();
        if (!item) return;
        const QString device = item->data(Qt::UserRole).toString();
        const bool now = item->data(Qt::UserRole + 1).toBool();
        RemoteShare::instance().setPasswordEntry(device, !now);
    });
    connect(m_devices, &QListWidget::itemSelectionChanged, this, [this] { passwordLabel(); });
    buttons->addWidget(m_passwords);
    buttons->addStretch(1);
    m_stop = new QPushButton(QStringLiteral("Stop sharing"));
    connect(m_stop, &QPushButton::clicked, this, [this] {
        RemoteShare::instance().stopSharing(m_paneId);
        accept();
    });
    buttons->addWidget(m_stop);
    column->addLayout(buttons);

    RemoteShare &share = RemoteShare::instance();
    connect(&share, &RemoteShare::pairingReady, this, &RemoteShareDialog::showPairing);
    connect(&share, &RemoteShare::pairingAsked, this, &RemoteShareDialog::showAsk);
    connect(&share, &RemoteShare::devicesChanged, this, &RemoteShareDialog::showDevices);
    connect(&share, &RemoteShare::addressesChanged, this, &RemoteShareDialog::showAddresses);
    connect(&share, &RemoteShare::inviteReady, this, &RemoteShareDialog::showInvite);
    connect(&share, &RemoteShare::codeReady, this,
            [this](const QString &code, const QString &pin, int expires, const QString &) {
                showCode(code, pin, expires);
            });
    connect(&share, &RemoteShare::codeStateChanged, this, &RemoteShareDialog::showCodeState);
    connect(&share, &RemoteShare::pairCodeReady, this, &RemoteShareDialog::showPairCode);
    connect(&share, &RemoteShare::pairCodeStateChanged, this, &RemoteShareDialog::showPairCodeState);
    connect(&share, &RemoteShare::secondPassed, this, &RemoteShareDialog::codeTick);
    connect(&share, &RemoteShare::secondPassed, this, &RemoteShareDialog::refreshPairCode);
    connect(&share, &RemoteShare::inviteSent, this, [this](bool ok, const QString &message) {
        m_inviteNote->setText(message);
        m_inviteSend->setEnabled(true);
        if (ok) m_inviteTo->clear();   // one link, one person: the next one needs a new link
        fit();
    });
    showAddresses(share.addresses());
    connect(&share, &RemoteShare::failed, this, [this](const QString &message) {
        m_status->setText(message);
        if (m_pairWaiting) {
            m_pairWaiting = false;
            m_pairValue->clear();
            m_pairClock->clear();
            m_pairNote->setText(QStringLiteral("No code was made: %1").arg(message));
            m_pairAgain->show();
        }
        // The sidecar answers a code_create it could not carry out with an `error` line; said
        // here too, where the person is looking, and the button comes back.
        if (m_codeAskedAt) {
            m_codeAskedAt = 0;
            m_makeCode->setEnabled(true);
            m_codeNote->setText(QStringLiteral("No code was made: %1").arg(message));
            fit();
        }
    });
    connect(&share, &RemoteShare::startedChanged, this,
            &RemoteShareDialog::refreshPairingService);
    connect(&share, &RemoteShare::remoteStateChanged, this,
            &RemoteShareDialog::refreshPairingService);
    if (share.running()) {
        // The phones paired before this window existed — at launch, with remote control on —
        // from the list RemoteShare kept, then a fresh one from the sidecar.
        showDevices(share.devices());
    }
    refreshPairingService();
}

// ----- the pairing code (#FR1C) -----------------------------------------------------------------

void RemoteShareDialog::refreshPairingService()
{
    RemoteShare &share = RemoteShare::instance();
    m_note->setText(share.note());
    // Startup announces the local listener before moving to the remembered rendezvous.
    // Wait for that destination before spending either of its two pairing rooms.
    if (share.running() && share.alwaysOn()
        && (!share.remoteState().online
            || share.remoteState().base != share.base())) return;
    if (share.running() && m_pairingBase == share.base()) return;
    m_pairingBase.clear();
    m_pairingLink.clear();
    m_qr->clear();
    m_url->clear();
    m_pairCopy->setEnabled(false);
    m_pairWaiting = false;
    m_pairCode.clear();
    m_pairPin.clear();
    m_pairDeadline = 0;
    m_pairValue->clear();
    m_pairClock->clear();
    m_pairAgain->hide();
    if (!share.running()) {
        m_status->setText(QStringLiteral("Remote control is off. These pairing codes have ended."));
        m_pairNote->clear();
        return;
    }
    m_pairingBase = share.base();
    m_status->setText(pairingIntro());
    share.requestPairing();
    share.requestDevices();
    askPairCode();
}

void RemoteShareDialog::askPairCode()
{
    RemoteShare &share = RemoteShare::instance();
    if (!share.running() || !m_pairBox) return;
    // A code on screen is withdrawn before another is asked for: two live pairing codes are two
    // open doors for one phone.
    if (m_pairDeadline && !m_pairCode.isEmpty()) share.revokePairCode(m_pairCode);
    m_pairCode.clear();
    m_pairPin.clear();
    m_pairState.clear();
    m_pairFailures = 0;
    m_pairDeadline = 0;
    m_pairWaiting = true;
    m_pairBox->show();
    m_pairHeading->setText(relay::remotesettings::pairCodeHeading());
    m_pairValue->clear();
    m_pairClock->clear();
    m_pairAgain->hide();
    m_pairNote->setText(QStringLiteral("Making a code…"));
    share.requestPairCode();
    // A sidecar from before this card ignores `pair_code` rather than refusing it, so waiting is
    // the only way to tell the two apart. After the wait the QR is on its own, and says so.
    QTimer::singleShot(relay::remotesettings::pairCodeWaitMs(), this, [this] { noPairCode(); });
    fit();
}

void RemoteShareDialog::showPairCode(const QString &code, const QString &pin, int expires)
{
    // A window that is waiting takes the code; one that already has a live one leaves it to the
    // window that asked. (A late answer, after the 3 s wait gave up, is still shown: the code is
    // live on the sidecar whether this window waited for it or not.)
    if (!m_pairWaiting && !m_pairCode.isEmpty()) return;
    m_pairWaiting = false;
    m_pairCode = code;
    m_pairPin = pin;
    m_pairState.clear();
    m_pairFailures = 0;
    m_pairDeadline =
        QDateTime::currentMSecsSinceEpoch() + qint64(expires > 0 ? expires : 600) * 1000;
    m_pairBox->show();
    m_pairHeading->setText(relay::remotesettings::pairCodeHeading());
    refreshPairCode();
    fit();
}

void RemoteShareDialog::showPairCodeState(const QString &code, const QString &state, int failures)
{
    if (code.isEmpty() || code != m_pairCode) return;   // an older code this window no longer shows
    if (m_pairWaiting) return;   // a new code is on its way, and the old one's burn made room
    m_pairState = state;
    m_pairFailures = failures;
    m_pairDeadline = 0;
    refreshPairCode();
    fit();
}

// The row itself is relay::remotesettings::pairCodeRow, so what the four states say is pinned by
// tests/remotesettings_test.cpp rather than living in a widget nothing headless can read.
void RemoteShareDialog::refreshPairCode()
{
    if (!m_pairBox || m_pairCode.isEmpty()) return;
    int left = 0;
    if (m_pairDeadline) {
        left = int((m_pairDeadline - QDateTime::currentMSecsSinceEpoch() + 999) / 1000);
        if (left <= 0) {   // it ran out here before the sidecar said so
            m_pairDeadline = 0;
            m_pairState = QStringLiteral("expired");
            left = 0;
        }
    }
    const relay::remotesettings::PairCode code{m_pairCode, m_pairPin, 600};
    const relay::remotesettings::PairCodeRow row =
        relay::remotesettings::pairCodeRow(code, m_pairState, m_pairFailures, left);
    m_pairValue->setText(row.value);
    QFont font = m_pairValue->font();
    if (font.strikeOut() != row.dead) {
        font.setStrikeOut(row.dead);
        m_pairValue->setFont(font);
    }
    m_pairClock->setText(row.clock);
    m_pairNote->setText(row.note);
    m_pairAgain->setVisible(row.again);
}

// Nothing answered `pair_code`. The QR is still good, and saying so is better than an empty
// column where a code was promised.
void RemoteShareDialog::noPairCode()
{
    if (!m_pairWaiting) return;
    m_pairWaiting = false;
    m_pairHeading->setText(relay::remotesettings::pairCodeUnavailable());
    m_pairValue->clear();
    m_pairClock->clear();
    m_pairNote->clear();
    m_pairAgain->hide();
    fit();
}

void RemoteShareDialog::done(int result)
{
    // The code goes with the window: one left live would pair a phone while nobody is here to
    // compare the five digits it shows.
    if (m_pairDeadline && !m_pairCode.isEmpty()) RemoteShare::instance().revokePairCode(m_pairCode);
    m_pairDeadline = 0;
    m_pairWaiting = false;
    QDialog::done(result);
}

void RemoteShareDialog::showPairing(const QString &url, const QrMatrix &qr, int expires)
{
    const QPixmap code = qrPixmap(qr, 260);
    m_qr->setFixedSize(code.size().expandedTo(QSize(1, 1)));
    m_qr->setPixmap(code);
    // Only the address, not the link: the full link is in the QR already, and its fragment is
    // the one-time secret — no reason to also print it in the window.
    m_url->setText(QStringLiteral("Phone connects to %1").arg(url.section(QLatin1Char('/'), 0, 2)));
    m_pairingLink = url;
    if (m_pairCopy) {
        m_pairCopy->setEnabled(true);
        m_pairCopy->setText(QStringLiteral("Copy link"));
    }
    QString text = QStringLiteral("%1 The QR lasts %2 s.").arg(pairingIntro()).arg(expires);
    if (m_address->count() > 1) {
        text += QStringLiteral("\nIf your phone says it cannot reach the site, choose the other "
                               "address in the list — the phone has to be on that network.");
    }
    m_status->setText(text);
    fit();
}

void RemoteShareDialog::showAsk(int id, const QString &name, const QString &platform,
                                const QString &fingerprint, const QString &code,
                                const QString &peer)
{
    m_askId = id;
    m_askText->setText(QStringLiteral("%1 (%2) at %3 wants access.\nKey %4.\n"
                                      "Allow it only if that device shows this code:")
                           .arg(name, platform, peer, fingerprint));
    m_askCode->setText(code);
    m_askBox->show();
    fit();
    // Bring the question forward, with Refuse holding the focus. Focus set while the box was
    // hidden would not stick, so it is set here, the moment there is something to refuse.
    raise();
    activateWindow();
    m_refuse->setFocus(Qt::OtherFocusReason);
}

void RemoteShareDialog::answer(bool allow, const QString &capability)
{
    if (m_askId < 0) return;
    const QString granted = capability.isEmpty() ? QStringLiteral("view") : capability;
    RemoteShare::instance().answer(m_askId, allow, granted);
    m_askId = -1;
    m_askBox->hide();
    if (!allow) {
        m_status->setText(QStringLiteral("Refused."));
    } else if (granted == QLatin1String("full")) {
        m_status->setText(QStringLiteral("Paired. That device can watch and type."));
    } else {
        m_status->setText(QStringLiteral("Paired for viewing. That device cannot type."));
    }
}

// What the chosen role will let the person do, in one sentence, before the link exists. The same
// sentence the Sharing pane shows beside them afterwards, so the promise does not change wording.
void RemoteShareDialog::updateRoleNote()
{
    m_inviteNote->setText(sharing::roleSentence(m_inviteRole->currentData().toString()));
    // Changing the role after a link exists would make the link on screen say the wrong thing, so
    // the old one is put away and the row asks for another.
    if (m_inviteQr) m_inviteQr->hide();
    if (m_inviteUrl) m_inviteUrl->hide();
    if (m_inviteCopy) m_inviteCopy->hide();
    m_inviteLink.clear();
    // The same for a code on screen: it would be saying the wrong role out loud.
    if (m_codeBox && !m_code.isEmpty()) putCodeAway();
    fit();
}

void RemoteShareDialog::setTab(const QString &tab, int panes)
{
    m_tab = tab;
    m_tabPanes = panes;
    updateWholeTab();
    connect(&RemoteShare::instance(), &RemoteShare::tabSharesChanged, this,
            [this] { updateWholeTab(); }, Qt::UniqueConnection);
}

void RemoteShareDialog::updateWholeTab()
{
    if (!m_wholeTab) return;
    m_wholeTab->setVisible(!m_tab.isEmpty());
    RemoteShare &share = RemoteShare::instance();
    const bool whole = share.isTabShared(m_tab);
    const bool all = share.isAllTabsShared();
    {
        const QSignalBlocker quiet(m_wholeTab);
        m_wholeTab->setChecked(whole);
    }
    if (m_allTabs) {
        const QSignalBlocker quiet(m_allTabs);
        m_allTabs->setChecked(all);
    }
    m_wholeTab->setEnabled(!all);
    // Scope growth is the thing to be told about before, not after: the guests of a tab shared
    // whole will see a pane the moment it is split off, so the box says so in its own words.
    m_wholeTab->setToolTip(QStringLiteral(
        "Every pane in this tab is shared, and every pane you add to it later is shared the moment "
        "it opens. A link made while this is ticked lets its guests into all of them. Moving a pane "
        "out of the tab, or closing it, takes it away from them."));
    m_wholeTab->setText(m_tabPanes > 1
                            ? QStringLiteral("Share the whole tab (%1 panes, and any you add)").arg(m_tabPanes)
                            : QStringLiteral("Share the whole tab (and any pane you add to it)"));
    if (m_inviteHeading)
        m_inviteHeading->setText(all ? QStringLiteral("Invite someone to all tabs")
                                     : whole ? QStringLiteral("Invite someone to this tab")
                                             : QStringLiteral("Invite someone to this pane"));
    setWindowTitle(all ? QStringLiteral("Share all tabs")
                       : whole ? QStringLiteral("Share this tab")
                               : QStringLiteral("Share this pane"));
}

void RemoteShareDialog::createInvite()
{
    RemoteShare &share = RemoteShare::instance();
    const QString scope = share.isAllTabsShared() ? RemoteShare::allTabsScope()
                                                  : share.isTabShared(m_tab) ? m_tab : QString();
    share.createInvite(m_paneId, m_inviteRole->currentData().toString(),
                       m_inviteExpiry->currentData().toInt(), m_inviteUses->value(),
                       scope);
    m_inviteNote->setText(QStringLiteral("Making a link…"));
}

void RemoteShareDialog::showInvite(const QString &url, const QrMatrix &qr, const QString &role,
                                   int uses, int expires)
{
    m_inviteLink = url;
    const QPixmap code = qrPixmap(qr, 130);
    if (!code.isNull()) {
        m_inviteQr->setFixedSize(code.size());
        m_inviteQr->setPixmap(code);
        m_inviteQr->show();
    }
    // The whole link, secret and all: unlike the pairing QR this one is meant to be copied and
    // sent to somebody, so it has to be on screen where it can be selected.
    m_inviteUrl->setText(url);
    m_inviteUrl->setCursorPosition(0);
    m_inviteUrl->show();
    m_inviteCopy->setText(QStringLiteral("Copy link"));
    m_inviteCopy->show();
    m_inviteRoleValue = role;
    m_inviteExpiryText = QStringLiteral("expires in %1").arg(sharing::expiryText(expires));
    m_inviteTo->clear();
    m_inviteTo->show();
    m_inviteSend->setEnabled(true);
    m_inviteSend->show();
    // In the invite section, not in the status line at the top: that line is about the pairing QR
    // still on screen above, and two different things were claiming it.
    RemoteShare &share = RemoteShare::instance();
    const QString place = share.isAllTabsShared() ? QStringLiteral("all tabs")
                         : share.isTabShared(m_tab) ? QStringLiteral("this tab")
                                                    : QStringLiteral("this pane");
    m_inviteNote->setText(QStringLiteral("Send this to the person you want on %1. It lets in "
                                         "%2 and stops working in %3. %4")
                              .arg(place,
                                   uses == 1 ? QStringLiteral("one person")
                                             : QStringLiteral("%1 people").arg(uses),
                                   sharing::expiryText(expires), sharing::roleSentence(role)));
    fit();
}

// ---- the meeting code (#97EG) ------------------------------------------------------------------

void RemoteShareDialog::createCode()
{
    // One live code per pane: the sidecar burns the one on screen to make room for this one, so
    // from this moment it is not something to read out.
    if (m_codeDeadline) {
        m_codeDeadline = 0;
        markCodeDead(true);
        m_codeClock->setText(QStringLiteral("Replaced"));
    }
    m_codeRole = m_inviteRole->currentData().toString();
    m_codeAskedAt = QDateTime::currentMSecsSinceEpoch();
    m_makeCode->setEnabled(false);
    m_codeAgain->setEnabled(false);
    RemoteShare &share = RemoteShare::instance();
    const QString scope = share.isAllTabsShared() ? RemoteShare::allTabsScope()
                                                  : share.isTabShared(m_tab) ? m_tab : QString();
    share.createCode(m_paneId, m_codeRole, scope);
    m_codeNote->setText(QStringLiteral("Making a code…"));
    fit();
}

void RemoteShareDialog::showCode(const QString &code, const QString &pin, int expires)
{
    // Every open share window hears every `code` line; only the one that asked shows it.
    if (!m_codeAskedAt) return;
    m_codeAskedAt = 0;
    m_code = code;
    m_pin = pin;
    m_codeDeadline = QDateTime::currentMSecsSinceEpoch() + qint64(expires > 0 ? expires : 600) * 1000;
    m_codeValue->setText(code);
    m_pinValue->setText(pin);
    markCodeDead(false);
    m_codeCopy->setText(QStringLiteral("Copy"));
    m_codeAgain->hide();
    m_makeCode->setEnabled(true);
    QString base = RemoteShare::instance().base();
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    m_codeNote->setText(QStringLiteral(
        "Tell your friend both, out loud or in a message. They open %1/join, type the meeting "
        "code and the PIN, and knock; you admit them on the Sharing pane, as %2 at most. The "
        "code works once, for one person, and stops in 10 minutes.")
                            .arg(base, m_codeRole == QLatin1String("editor")
                                           ? QStringLiteral("an editor")
                                           : QStringLiteral("a viewer")));
    m_codeBox->show();
    codeTick();
    fit();
}

void RemoteShareDialog::showCodeState(const QString &code, const QString &state, int failures)
{
    if (code.isEmpty() || code != m_code) return;   // an older code this window no longer shows
    // A new code is on its way for this pane, and the sidecar burns the old one to make room:
    // that burn is the replacement, not three wrong PINs, and the new code is what goes here.
    if (m_codeAskedAt != 0) return;
    m_codeDeadline = 0;
    markCodeDead(true);
    if (state == QLatin1String("used")) {
        m_codeClock->setText(QStringLiteral("Used"));
        m_codeAgain->hide();
        m_codeNote->setText(QStringLiteral(
            "Someone joined with this code. Look for their knock on the Sharing pane, and admit "
            "them only if it is the person you told. The code is closed now."));
    } else if (state == QLatin1String("burned")) {
        m_codeClock->setText(QStringLiteral("Closed"));
        m_codeAgain->setEnabled(true);
        m_codeAgain->show();
        m_codeNote->setText(QStringLiteral(
            "%1 wrong PINs were tried, so the code was closed. Nobody got in. Make a new one and "
            "tell your friend again.")
                                .arg(failures == 3 ? QStringLiteral("Three")
                                                   : QString::number(failures)));
        // Closed without a wrong guess: revoked, from here or from the Sharing pane's invite row.
        if (failures <= 0)
            m_codeNote->setText(QStringLiteral(
                "This code was closed before anyone used it. Make a new one if your friend still "
                "needs to join."));
    } else {
        m_codeClock->setText(QStringLiteral("Expired"));
        m_codeAgain->setEnabled(true);
        m_codeAgain->show();
        m_codeNote->setText(QStringLiteral(
            "This code expired before anyone used it. Make a new one if your friend still needs "
            "to join."));
    }
    fit();
}

// Struck through rather than cleared: the person may be on the phone asking "which code?", and the
// answer is this one, which is no longer any good. Copy goes with it: it no longer lets anyone in.
void RemoteShareDialog::markCodeDead(bool dead)
{
    for (QLabel *label : {m_codeValue, m_pinValue}) {
        QFont font = label->font();
        font.setStrikeOut(dead);
        label->setFont(font);
    }
    m_codeCopy->setVisible(!dead);
}

// Once a second: the countdown, a code that ran out before the sidecar said so, and a code_create
// that nothing ever answered (a sidecar from before meeting codes ignores the line).
void RemoteShareDialog::codeTick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_codeAskedAt > 0 && now - m_codeAskedAt > 15000) {
        m_codeAskedAt = -1;   // still listening: a late code is live on the hub and must show
        m_makeCode->setEnabled(true);
        m_codeAgain->setEnabled(true);
        m_codeNote->setText(QStringLiteral(
            "No code came back. The sharing service did not answer; try again, or make a link."));
        fit();
    }
    if (!m_codeDeadline) return;
    const qint64 left = (m_codeDeadline - now + 999) / 1000;
    if (left <= 0) {
        showCodeState(m_code, QStringLiteral("expired"), 0);
        return;
    }
    m_codeClock->setText(QStringLiteral("Expires in %1:%2")
                             .arg(left / 60)
                             .arg(left % 60, 2, 10, QLatin1Char('0')));
}

// The role changed under a code on screen. A live one is revoked, not just hidden: it must not go
// on admitting somebody at a role the owner has just moved away from. (Closing the window does
// not come through here — the owner reads the code out and closes it, and the code stays good.)
void RemoteShareDialog::putCodeAway()
{
    const bool live = m_codeDeadline != 0;
    if (live) RemoteShare::instance().revokeCode(m_code);
    m_code.clear();
    m_pin.clear();
    m_codeDeadline = 0;
    m_codeValue->clear();
    m_pinValue->clear();
    m_codeBox->hide();
    m_codeNote->setText(live ? QStringLiteral(
        "The code on screen was for the other role, so it has been closed and no longer lets "
        "anyone in. Make a new code for this one.")
                             : codeIntro());
}

// Wrapped labels need more height the narrower they are, and a top-level window's automatic
// minimum ignores that, so the layout would squeeze the rows onto one another — on top of the QR
// code, which a phone then cannot read. Work out the real height for this width and hold it.
void RemoteShareDialog::fit()
{
    QLayout *column = layout();
    if (!column) return;
    column->activate();
    const int needed = column->hasHeightForWidth() ? column->totalHeightForWidth(width())
                                                   : column->totalMinimumSize().height();
    setMinimumHeight(needed);
    if (height() < needed) resize(width(), needed);
}

void RemoteShareDialog::showAddresses(const QJsonArray &addresses)
{
    // The sidecar sends its best first: the tailnet name behind `tailscale serve`, which the phone
    // opens with no certificate warning at all, then this machine's own addresses behind the
    // self-signed certificate (the LAN one, then the tailnet IP). It also sends the tailnet entry
    // when it cannot be used, carrying one sentence saying why — that is not something to choose,
    // so it goes under the picker rather than into it.
    m_address->blockSignals(true);
    m_address->clear();
    // Each unavailable entry says why in its own sentence. Only the tailnet one gets the
    // "No warning-free tailnet address" prefix: the hosted (relay-terminal.ai) and cloudflare
    // entries carry sentences that already name what they are about.
    QStringList reasons;
    bool publicLink = false;
    for (const QJsonValue &value : addresses) {
        const QJsonObject entry = value.toObject();
        if (entry.contains(QStringLiteral("available"))
            && !entry.value(QStringLiteral("available")).toBool()) {
            const QString why = entry.value(QStringLiteral("reason")).toString();
            if (why.isEmpty()) continue;
            const QString entryKind = entry.value(QStringLiteral("kind")).toString();
            reasons << (entryKind == QLatin1String("tailscale")
                            ? QStringLiteral("No warning-free tailnet address: %1").arg(why)
                            : why);
            continue;
        }
        const QString address = entry.value(QStringLiteral("value")).toString();
        if (address.isEmpty()) continue;
        QString label = entry.value(QStringLiteral("label")).toString();
        if (label.isEmpty()) {
            label = QStringLiteral("%1 — reachable from %2")
                        .arg(address, entry.value(QStringLiteral("where")).toString());
        }
        m_address->addItem(label, address);
        // `where` can say more than the label has room for (the hosted entry: what a switch drops).
        m_address->setItemData(m_address->count() - 1,
                               entry.value(QStringLiteral("where")).toString(), Qt::ToolTipRole);
        if (entry.value(QStringLiteral("current")).toBool()) {
            m_address->setCurrentIndex(m_address->count() - 1);
            const QString current = entry.value(QStringLiteral("kind")).toString();
            publicLink = current == QLatin1String("cloudflare") || current == QLatin1String("hosted");
        }
    }
    m_address->setVisible(m_address->count() > 1);
    m_address->blockSignals(false);
    // A public link admits one person per link: a multi-use link on a LAN address is a room of
    // colleagues at a desk, and the same link on a public URL is that many admissions for whoever
    // it is forwarded to. The sidecar clamps it either way; capping the box here is so the number
    // on screen is the number that will happen. Switching back to the LAN or tailnet lifts it.
    // A public link admits as many people as the owner picks, the same as any other address
    // (owner, 2026-09-18: one link for a group). What changes is who can reach the door, so the
    // note says that, and says the part that has not changed: each person is admitted by hand.
    if (publicLink) {
        m_inviteNote->setText(QStringLiteral("Over a public link, anyone this link is forwarded to "
                                             "can knock. You admit each person by hand."));
        m_inviteNote->show();
    } else if (m_inviteNote->text().startsWith(QLatin1String("Over a public link"))) {
        m_inviteNote->clear();
    }
    m_addressNote->setText(reasons.join(QLatin1Char('\n')));
    m_addressNote->setVisible(!reasons.isEmpty());
    fit();
}

void RemoteShareDialog::showDevices(const QJsonArray &items)
{
    m_devices->clear();
    for (const QJsonValue &value : items) {
        const QJsonObject device = value.toObject();
        auto *item = new QListWidgetItem(
            QStringLiteral("%1 (%2) · %3%4")
                .arg(device.value(QStringLiteral("name")).toString(),
                     device.value(QStringLiteral("platform")).toString(),
                     device.value(QStringLiteral("capability")).toString(),
                     device.value(QStringLiteral("password_entry")).toBool()
                         ? QStringLiteral(" · passwords") : QString()));
        item->setData(Qt::UserRole, device.value(QStringLiteral("id")).toString());
        item->setData(Qt::UserRole + 1, device.value(QStringLiteral("password_entry")).toBool());
        m_devices->addItem(item);
    }
    passwordLabel();
}

void RemoteShareDialog::passwordLabel()
{
    const auto *item = m_devices->currentItem();
    if (!item) {
        m_passwords->setEnabled(false);
        m_passwords->setText(QStringLiteral("Passwords off"));
        return;
    }
    m_passwords->setEnabled(true);
    m_passwords->setText(item->data(Qt::UserRole + 1).toBool()
                             ? QStringLiteral("Passwords: on")
                             : QStringLiteral("Passwords: off"));
}

} // namespace relay
