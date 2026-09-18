// SPDX-License-Identifier: GPL-3.0-or-later
#include "RemoteShare.h"

#include "Theme.h"
#include "tools/ScreenJson.h"
#include "view/TerminalView.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace relay {

namespace {

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
        QrMatrix matrix;
        for (const QJsonValue &row : message.value(QStringLiteral("qr")).toArray()) {
            QVector<int> cells;
            for (const QJsonValue &cell : row.toArray()) cells.append(cell.toInt());
            matrix.append(cells);
        }
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
    emit sharingChanged();
    return true;
}

void RemoteShare::stopSharing(const QString &paneId)
{
    if (!m_panes.remove(paneId)) return;
    send({{"t", "unpane"}, {"id", paneId}});
    emit sharingChanged();
}

void RemoteShare::stopAll()
{
    for (const QString &paneId : m_panes.keys()) send({{"t", "unpane"}, {"id", paneId}});
    m_panes.clear();
    send({{"t", "stop"}});
    m_poll->stop();
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
    m_url->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    column->addWidget(m_url);

    m_note = new QLabel;
    m_note->setWordWrap(true);
    m_note->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
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
