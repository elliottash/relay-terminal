// SPDX-License-Identifier: GPL-3.0-or-later
#include "RemoteShare.h"

#include "Theme.h"
#include "core/VtCore.h"
#include "session/TerminalSession.h"
#include "tools/ScreenJson.h"
#include "view/TerminalView.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace relay {

namespace {

// The most scrollback rows one `history` line may ask for, matching the protocol's page cap
// (docs/REMOTE-PROTOCOL.md section 6.5). A phone pages; it does not download the buffer.
constexpr int kHistoryPage = 200;

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
    connect(m_second, &QTimer::timeout, this, [this] { emit secondPassed(); });
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
    m_process->start(QStringLiteral("python3"),
                     {QStringLiteral("-m"), QStringLiteral("remote.gui_host")});
    if (!m_process->waitForStarted(5000)) {
        if (error) *error = QStringLiteral("python3 could not start the remote sidecar.");
        m_process->deleteLater();
        m_process = nullptr;
        return false;
    }
    QJsonObject start{{"t", "start"}, {"tls", true}, {"name", QStringLiteral("this desktop")}};
    send(start);
    send({{"t", "window_active"},
          {"active", QGuiApplication::applicationState() == Qt::ApplicationActive}});
    return true;
}

void RemoteShare::send(const QJsonObject &message)
{
    if (!m_process || m_process->state() != QProcess::Running) return;
    m_process->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
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
        emit devicesChanged(message.value(QStringLiteral("items")).toArray());
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
            it->hooks.compose(message.value(QStringLiteral("text")).toString(),
                              message.value(QStringLiteral("route")).toBool(),
                              message.value(QStringLiteral("origin")).toString());
        }
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
                             message.value(QStringLiteral("name")).toString());
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
    } else if (kind == QLatin1String("error")) {
        emit failed(message.value(QStringLiteral("message")).toString());
    } else if (kind == QLatin1String("stopped")) {
        m_running = false;
        emit startedChanged();
    }
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

bool RemoteShare::sharePane(const QString &paneId, const PaneHooks &hooks, QString *error)
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
    send({{"t", "unpane"}, {"id", paneId}});
    refreshSharedPanes();
    emit sharingChanged();
}

void RemoteShare::stopAll()
{
    for (const QString &paneId : m_panes.keys()) send({{"t", "unpane"}, {"id", paneId}});
    m_panes.clear();
    send({{"t", "stop"}});
    m_poll->stop();
    refreshSharedPanes();
    emit sharingChanged();
}

void RemoteShare::sendPane(const QString &paneId)
{
    auto it = m_panes.find(paneId);
    if (it == m_panes.end()) return;
    const QString title = it->hooks.title ? it->hooks.title() : paneId;
    const QString cwd = it->hooks.cwd ? it->hooks.cwd() : QString();
    const QString status = it->hooks.status ? it->hooks.status() : QStringLiteral("idle");
    it->lastTitle = title;
    it->lastCwd = cwd;
    it->lastStatus = status;
    const ViewportFrame &frame = it->hooks.view->frame();
    QJsonObject message{{"t", "pane"}, {"id", paneId}, {"title", title}, {"cwd", cwd},
                         {"status", status}, {"rows", frame.rows}, {"cols", frame.columns}};
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

void RemoteShare::createInvite(const QString &paneId, const QString &role, int expires, int uses)
{
    send({{"t", "invite_create"}, {"pane", paneId}, {"role", role},
          {"expires", expires}, {"uses", uses}});
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

    // Which address the phone should reach this machine on. Getting it wrong is the most likely
    // reason a phone says it cannot reach the site, so the choice is in front of the QR code.
    m_address = new QComboBox;
    m_address->setToolTip(QStringLiteral(
        "The address your phone will open. Use the network one when the phone is on the same "
        "Wi-Fi, the tailnet one when it is signed in to your tailnet."));
    connect(m_address, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        const QString address = m_address->itemData(index).toString();
        if (!address.isEmpty()) RemoteShare::instance().useAddress(address);
    });
    column->addWidget(m_address);

    m_qr = new QLabel;
    m_qr->setAlignment(Qt::AlignCenter);
    m_qr->setFixedSize(260, 260);
    // A QR code must never be squeezed or overlapped: a phone cannot read a partial one. The
    // label has a fixed size and the dialog grows to fit whatever else it has to say.
    column->addWidget(m_qr, 0, Qt::AlignHCenter);
    column->setSizeConstraint(QLayout::SetMinimumSize);


    m_url = new QLabel;
    m_url->setTextFormat(Qt::PlainText);
    m_url->setWordWrap(true);
    m_url->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // The theme's muted text at the secondary size: palette(mid) is the border colour, about 1.4:1
    // on the dialog (docs/ARCHITECTURE.md, "Legible text").
    m_url->setObjectName(QStringLiteral("shareNote"));
    column->addWidget(m_url);

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
    m_askCode->setStyleSheet(QStringLiteral("font-size: 28px; font-weight: 600; letter-spacing: 6px;"));
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
    auto *inviteHeading = new QLabel(QStringLiteral("Invite someone to this pane"));
    inviteHeading->setObjectName(QStringLiteral("settingsHeading"));
    column->addWidget(inviteHeading);

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
    linkColumn->addWidget(m_inviteCopy, 0, Qt::AlignLeft);
    linkColumn->addStretch(1);
    linkRow->addLayout(linkColumn, 1);
    column->addLayout(linkRow);

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
    showAddresses(share.addresses());
    connect(&share, &RemoteShare::failed, this, [this](const QString &message) {
        m_status->setText(message);
    });
    connect(&share, &RemoteShare::startedChanged, this, [this, &share] {
        if (share.running()) {
            m_status->setText(QStringLiteral("Scan this with your phone's camera."));
            m_note->setText(share.note());
            share.requestPairing();
        }
    });
    if (share.running()) {
        m_status->setText(QStringLiteral("Scan this with your phone's camera."));
        m_note->setText(share.note());
        share.requestPairing();
    }
}

void RemoteShareDialog::showPairing(const QString &url, const QrMatrix &qr, int expires)
{
    const QPixmap code = qrPixmap(qr, 260);
    m_qr->setFixedSize(code.size().expandedTo(QSize(1, 1)));
    m_qr->setPixmap(code);
    // Only the address, not the link: the full link is in the QR already, and its fragment is
    // the one-time secret — no reason to also print it in the window.
    m_url->setText(QStringLiteral("Phone connects to %1").arg(url.section(QLatin1Char('/'), 0, 2)));
    QString text = QStringLiteral("Scan this with your phone's camera. The code lasts %1 s.")
                       .arg(expires);
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
    fit();
}

void RemoteShareDialog::createInvite()
{
    RemoteShare::instance().createInvite(m_paneId, m_inviteRole->currentData().toString(),
                                         m_inviteExpiry->currentData().toInt(),
                                         m_inviteUses->value());
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
    // In the invite section, not in the status line at the top: that line is about the pairing QR
    // still on screen above, and two different things were claiming it.
    m_inviteNote->setText(QStringLiteral("Send this to the person you want on this pane. It lets in "
                                         "%1 and stops working in %2. %3")
                              .arg(uses == 1 ? QStringLiteral("one person")
                                             : QStringLiteral("%1 people").arg(uses),
                                   sharing::expiryText(expires), sharing::roleSentence(role)));
    fit();
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
    m_address->blockSignals(true);
    m_address->clear();
    for (const QJsonValue &value : addresses) {
        const QJsonObject entry = value.toObject();
        const QString address = entry.value(QStringLiteral("value")).toString();
        m_address->addItem(QStringLiteral("%1 — reachable from %2")
                               .arg(address, entry.value(QStringLiteral("where")).toString()),
                           address);
        if (entry.value(QStringLiteral("current")).toBool()) {
            m_address->setCurrentIndex(m_address->count() - 1);
        }
    }
    m_address->setVisible(m_address->count() > 1);
    m_address->blockSignals(false);
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
