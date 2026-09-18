// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Sharing a pane with a phone, from inside Relay.
//
// The protocol, the crypto and the web app live in a Python sidecar (`remote/gui_host.py`), the
// same arrangement as the agent worker: Relay speaks line JSON to it and never links a crypto
// library. What stays here is what only the GUI can do — the screen state of a live pane, the
// keystrokes a phone sends back, and the dialog where a person compares two five-digit codes.
//
// One instance per process, like the notification centre. Several panes can be shared at once.
//
// Screen state comes from the frame `relay::TerminalView` has already pulled: `VtCore::updateFrame`
// consumes the dirty state, so a second caller would stop the pane repainting. A pane without a
// frame cannot be shared (docs/ENGINE.md).
#include <QByteArray>
#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

class QComboBox;
class QLabel;
class QListWidget;
class QProcess;
class QPushButton;
class QTimer;

namespace relay {

class TerminalView;

using QrMatrix = QVector<QVector<int>>;

class RemoteShare final : public QObject {
    Q_OBJECT
public:
    static RemoteShare &instance();

    bool running() const { return m_running; }
    QString base() const { return m_base; }
    QString note() const { return m_note; }
    bool isSharing(const QString &paneId) const { return m_panes.contains(paneId); }

    // What a pane hands over. `status` is polled (idle/running/password/finished/failed) and
    // `input` receives the bytes a phone typed.
    struct PaneHooks {
        TerminalView *view = nullptr;
        std::function<QString()> title;
        std::function<QString()> cwd;
        std::function<QString()> status;
        // The pids behind a password prompt: the shell's for a fresh termios read, the
        // foreground's to bind the desktop-minted nonce to the process that is asking.
        std::function<qint64()> shellPid;
        std::function<qint64()> foregroundPid;
        std::function<void(const QByteArray &)> input;
        // A password line from a phone. Returns false when the fresh termios read at the moment
        // of the write says the prompt has ended, in which case the bytes are dropped.
        std::function<bool(const QByteArray &)> secret;
        // A prompt from a client. `route` means decide shell or agent the way the composer does;
        // without it the text may only reach the agent (the hub enforces which devices get it).
        std::function<void(const QString &text, bool route, const QString &origin)> compose;
        std::function<void()> stopAgent;
    };

    // Start the sidecar if needed and share this pane. Returns false with `error` set when the
    // pane cannot be shared (a KonsolePart pane, or the sidecar failed to start).
    bool sharePane(const QString &paneId, const PaneHooks &hooks, QString *error);
    void stopSharing(const QString &paneId);
    void stopAll();

    void requestPairing();
    // Which of this machine's addresses the pairing link points at. A phone on the same Wi-Fi
    // needs the network address; a phone on the tailnet needs the tailnet one, and only the
    // person knows which the phone is on.
    QJsonArray addresses() const { return m_addresses; }
    void useAddress(const QString &address);
    void answer(int askId, bool allow, const QString &capability);
    void revoke(const QString &deviceId);
    // Password entry is off for every device until the owner turns it on for that one
    // (docs/REMOTE-PROTOCOL.md section 6.7); this is the switch the dialog drives.
    void setPasswordEntry(const QString &deviceId, bool allow);

signals:
    void startedChanged();
    void addressesChanged(const QJsonArray &addresses);
    void pairingReady(const QString &url, const relay::QrMatrix &qr, int expires);
    void pairingAsked(int id, const QString &name, const QString &platform,
                      const QString &fingerprint, const QString &code, const QString &peer);
    void devicesChanged(const QJsonArray &items);
    void failed(const QString &message);
    void sharingChanged();

private:
    RemoteShare();
    ~RemoteShare() override;

    bool ensureSidecar(QString *error);
    void send(const QJsonObject &message);
    void onReadable();
    void handle(const QJsonObject &message);
    void sendFrame(const QString &paneId, bool full);
public:
    // One worker event from a pane. Forwarded only while that pane is shared; the sidecar's
    // allow-list decides what a client may actually see.
    void paneEvent(const QString &paneId, const QJsonObject &event);
private:
    void sendPane(const QString &paneId);
    void poll();

    struct Shared {
        PaneHooks hooks;
        QString lastStatus;
        QString lastTitle;
        QString lastCwd;
        bool needFull = true;
    };

    QProcess *m_process = nullptr;
    QByteArray m_pending;
    QHash<QString, Shared> m_panes;
    QTimer *m_poll = nullptr;
    bool m_running = false;
    QString m_base;
    QString m_note;
    QJsonArray m_addresses;
};

// The window behind the share button: the QR code, the code to compare, and who is connected.
class RemoteShareDialog final : public QDialog {
    Q_OBJECT
public:
    explicit RemoteShareDialog(const QString &paneId, QWidget *parent = nullptr);

private:
    void showPairing(const QString &url, const relay::QrMatrix &qr, int expires);
    void showAsk(int id, const QString &name, const QString &platform, const QString &fingerprint,
                 const QString &code, const QString &peer);
    void showDevices(const QJsonArray &items);
    void passwordLabel();
    void showAddresses(const QJsonArray &addresses);
    void answer(bool allow, const QString &capability = QString());
    void fit();

    QString m_paneId;
    QLabel *m_status = nullptr;
    QLabel *m_qr = nullptr;
    QLabel *m_url = nullptr;
    QComboBox *m_address = nullptr;
    QLabel *m_note = nullptr;
    QWidget *m_askBox = nullptr;
    QLabel *m_askText = nullptr;
    QLabel *m_askCode = nullptr;
    QPushButton *m_refuse = nullptr;
    QListWidget *m_devices = nullptr;
    QPushButton *m_passwords = nullptr;
    QPushButton *m_stop = nullptr;
    int m_askId = -1;
};

} // namespace relay
