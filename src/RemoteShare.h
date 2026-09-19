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
#include "SharingPane.h"

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
class QLineEdit;
class QListWidget;
class QProcess;
class QPushButton;
class QSpinBox;
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
        // `when` is "now", "queue" or "steer" — the phone's three-way send, the same choice the
        // composer's Enter makes. `originName` is a guest's display name for the queue row; the
        // id stays in `origin` and is never shown.
        std::function<void(const QString &text, bool route, const QString &origin,
                           const QString &when, const QString &originName)> compose;
        std::function<void()> stopAgent;
        // A voice clip from a phone (docs/REMOTE-PROTOCOL.md section 6.4). The pane hands it to
        // the same worker request its own microphone uses and answers with `voiceResult`; the
        // text belongs to the device that spoke, never to the desktop's prompt box.
        std::function<void(const QString &requestId, const QByteArray &audio,
                           const QString &format)> transcribe;
        // pane_state (docs/REMOTE-PROTOCOL.md section 16): a client acting on the rows, the model
        // choices and the sessions this pane published. Each takes ids the pane minted and answers
        // false when the row is gone or no longer offers the action — the client was looking at an
        // older state, and the pane publishes a fresh one rather than arguing with it.
        std::function<bool(const QString &row)> queueRemove;
        std::function<bool(const QString &row, const QString &to)> queueMove;
        // Takes the row back for the client's own prompt box: `text` is what it held.
        std::function<bool(const QString &row, QString *text)> queueEdit;
        std::function<bool(const QString &row)> queueSendNow;
        std::function<bool(const QString &choice, const QString &deviceName)> modelPick;
        std::function<bool(const QString &deviceName)> conversationNew;
        // {session}: a token from a pane_state this pane sent, never a path (owner level).
        std::function<bool(const QString &session, const QString &deviceName)> conversationOpen;
        std::function<void()> publishPaneState;   // pane_state_get: publish this pane now
        std::function<void()> recap;              // recap_request, which used to be dropped here
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

    // ----- multiplayer, the owner's controls (docs/REMOTE-PROTOCOL.md section 10.5) ------------
    // Every one of these is desktop-only by the protocol: the same name arriving over the wire
    // from any device, the owner's own paired phone included, is refused `not_permitted`. They
    // are here rather than in the Sharing pane so that the pane stays a view.

    // `invite_create {pane, role, expires, uses}` → an `invite` line with the link and its QR.
    void createInvite(const QString &paneId, const QString &role, int expires, int uses);
    void revokeInvite(const QString &inviteId);
    // `code_create {pane, role}` → a `code` line: a four-letter meeting code and a four-digit PIN
    // that a person reads out instead of sending a link (#97EG). It always lasts ten minutes and
    // lets in one person; the sidecar mints an ordinary invite behind it.
    void createCode(const QString &paneId, const QString &role);
    // `code_revoke {code}`: the code and its invite burn now; answered by `code_state` "burned".
    void revokeCode(const QString &code);
    // `knock_answer`. May lower the invite's role and never raise it; the hub checks that too.
    void answerKnock(const QString &participant, bool admit, const QString &role);
    void setRole(const QString &participant, const QString &role);
    void removeParticipant(const QString &participant);
    // `control_answer {pane, participant, grant}`. An empty participant with `grant` false is the
    // owner taking a pane back from whoever holds it (`control_revoke`).
    void answerControl(const QString &paneId, const QString &participant, bool grant);
    // `control_take {pane}`: the owner's own keystroke landed in a pane somebody else was
    // driving — a guest, or one of the owner's own paired devices, because section 10.3 has one
    // holder and the phone is in the same book. Sent from Pane's event filter, which never
    // swallows the key that sent it.
    void takeControl(const QString &paneId);
    void answerPrompt(const QString &promptId, bool approve);
    // `invite_email`: post the link the dialog is showing. Desktop-only, like the other invite
    // names; the answer comes back as inviteSent().
    void emailInvite(const QString &url, const QString &to, const QString &role,
                     const QString &expiry, const QString &pane);
    void pauseShare(const QString &paneId, bool on);
    void endShare(const QString &paneId);
    void setShareOptions(const QString &paneId, bool promptsImmediate, bool presentOnly);
    void requestParticipants();

    // Who is here, what is waiting and who is driving, for every shared pane. One per process:
    // the Sharing pane and the pane headers read the same rows.
    sharing::Model &sharingModel() { return m_sharing; }
    const sharing::Model &sharingModel() const { return m_sharing; }

    // The answer to one `voice` line, carrying back the id it arrived with so that two clips in
    // flight cannot be given each other's words. `error` is what the phone shows when `ok` is
    // false; no audio and no key ever leaves this machine.
    void voiceResult(const QString &paneId, const QString &requestId, bool ok,
                     const QString &text, const QString &error);

    // One pane's `pane_state` (section 16), on its way to every device watching that pane. The
    // hub decides who sees what; this only carries it across. Called from Pane::onPaneState,
    // which the publisher coalesces, so this is at most one message per pane per 100 ms.
    void paneState(const QString &paneId, const QJsonObject &state);

signals:
    void startedChanged();
    void addressesChanged(const QJsonArray &addresses);
    void pairingReady(const QString &url, const relay::QrMatrix &qr, int expires);
    void pairingAsked(int id, const QString &name, const QString &platform,
                      const QString &fingerprint, const QString &code, const QString &peer);
    void devicesChanged(const QJsonArray &items);
    void failed(const QString &message);
    void inviteSent(bool ok, const QString &message);
    void sharingChanged();
    // An `invite` line: the link to hand out, its QR, and what it grants.
    void inviteReady(const QString &url, const relay::QrMatrix &qr, const QString &role,
                     int uses, int expires);
    // A `code` line. The PIN is the secret half: it goes to the dialog and nowhere else.
    void codeReady(const QString &code, const QString &pin, int expires, const QString &invite);
    // A `code_state` line: "used", "burned" (too many wrong PINs) or "expired".
    void codeStateChanged(const QString &code, const QString &state, int failures);
    // Something changed in sharingModel(): the Sharing pane and the pane headers redraw.
    void sharingModelChanged();
    // Somebody is at the door, wants the keyboard, or has written a prompt. The window opens the
    // Sharing pane and posts a notification — and never takes the keyboard, because the next
    // keystroke would otherwise land on a button that admits a stranger.
    void needsOwner(const QString &paneId, const QString &title, const QString &body);
    // One second passed: the waiting rows' countdowns move and a lapsed one goes.
    void secondPassed();

private:
    RemoteShare();
    ~RemoteShare() override;

    bool ensureSidecar(QString *error);
    void send(const QJsonObject &message);
    void onReadable();
    void handle(const QJsonObject &message);
    void sendFrame(const QString &paneId, bool full);
    // One `history` line from the sidecar, answered from the pane's own core. On the GUI thread,
    // read-only, and capped at one page: `VtCore::historyLines` is const and moves nothing, so a
    // phone paging back never scrolls the screen the owner is looking at.
    void sendHistoryPage(const QJsonObject &request);
public:
    // One worker event from a pane. Forwarded only while that pane is shared; the sidecar's
    // allow-list decides what a client may actually see.
    void paneEvent(const QString &paneId, const QJsonObject &event);
private:
    void sendPane(const QString &paneId);
    void poll();
    // Which panes are shared, and what the owner calls each one, handed to the model so the
    // Sharing pane can name a share by its pane's title rather than by its session token.
    void refreshSharedPanes();

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
    sharing::Model m_sharing;
    QTimer *m_second = nullptr;
};

// The window behind the share button: the QR code, the code to compare, and who is connected.
class RemoteShareDialog final : public QDialog {
    Q_OBJECT
public:
    explicit RemoteShareDialog(const QString &paneId, QWidget *parent = nullptr);

private:
    void showPairing(const QString &url, const relay::QrMatrix &qr, int expires);
    void showInvite(const QString &url, const relay::QrMatrix &qr, const QString &role,
                    int uses, int expires);
    void createInvite();
    void updateRoleNote();
    void createCode();
    void showCode(const QString &code, const QString &pin, int expires);
    void showCodeState(const QString &code, const QString &state, int failures);
    void codeTick();
    void markCodeDead(bool dead);
    void putCodeAway();
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
    QLabel *m_addressNote = nullptr;
    QLabel *m_note = nullptr;
    QWidget *m_askBox = nullptr;
    QLabel *m_askText = nullptr;
    QLabel *m_askCode = nullptr;
    QPushButton *m_refuse = nullptr;
    QListWidget *m_devices = nullptr;
    QPushButton *m_passwords = nullptr;
    QPushButton *m_stop = nullptr;
    int m_askId = -1;
    // "Invite someone to this pane": the second way in, under the pairing QR, because pairing
    // your own phone is the common case and stays the first thing offered.
    QComboBox *m_inviteRole = nullptr;
    QComboBox *m_inviteExpiry = nullptr;
    QSpinBox *m_inviteUses = nullptr;
    QLabel *m_inviteNote = nullptr;
    QLabel *m_inviteQr = nullptr;
    QLineEdit *m_inviteUrl = nullptr;
    QPushButton *m_inviteCopy = nullptr;
    // Emailing the link is the same act as copying it: the link is already minted, this posts it.
    QLineEdit *m_inviteTo = nullptr;
    QString m_inviteRoleValue, m_inviteExpiryText;   // what the link that is on screen grants
    QPushButton *m_inviteSend = nullptr;
    QString m_inviteLink;
    // "Make a code": a meeting code and a PIN to read out, beside "Make a link". Its own note,
    // because m_inviteNote also carries the link's sentences and the public-address warning.
    QPushButton *m_makeCode = nullptr;
    QLabel *m_codeNote = nullptr;
    QWidget *m_codeBox = nullptr;
    QLabel *m_codeValue = nullptr;
    QLabel *m_pinValue = nullptr;
    QLabel *m_codeClock = nullptr;
    QPushButton *m_codeCopy = nullptr;
    QPushButton *m_codeAgain = nullptr;
    QString m_code, m_pin, m_codeRole;
    qint64 m_codeDeadline = 0;       // ms since the epoch; 0 once the code is no longer live
    // ms since the epoch while a code_create is unanswered; -1 once the wait has been given up on
    // but a late answer would still be shown; 0 when this window is not waiting for one.
    qint64 m_codeAskedAt = 0;
};

} // namespace relay
