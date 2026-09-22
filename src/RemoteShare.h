// SPDX-License-Identifier: AGPL-3.0-or-later
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
#include "RemoteSettings.h"
#include "SharingPane.h"

#include <QByteArray>
#include <QDialog>
#include <QJsonArray>
#include <QSet>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

class QCheckBox;
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

    // ----- remote control as a service (card #PH0N, phase 1) -----------------------------------
    // The switch is Options › Remote and it is remembered in `remote/alwaysOn`. While it is on the
    // sidecar starts with Relay rather than with the first share, `start` carries `always` and the
    // remembered `address`, and the window publishes every pane that has a screen as it appears.
    // Off is what this file did before: a pane at a time, from its share button.
    bool alwaysOn() const;
    void setAlwaysOn(bool on);
    // Options › Remote picked another address. Remembered either way; sent to a running sidecar
    // only while the service is on, because a per-share address is the dialog's business.
    void setRemoteAddress(const QString &value);
    // Called once from main(): brings the sidecar up when the switch is on, and does nothing at
    // all when it is off — a desktop with no phone starts no python.
    void startAtLaunch();
    // The last `remote_state` line: whether the service is registered, where, and how many of the
    // owner's devices are connected. The window chrome reads it.
    const remotesettings::State &remoteState() const { return m_remoteState; }

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
        // The phone's empty send: run the queue again after a Stop (#7JD1). It names no row.
        std::function<bool()> queueResume;
        std::function<bool(const QString &choice, const QString &deviceName)> modelPick;
        std::function<bool(const QString &deviceName)> conversationNew;
        // {session}: a token from a pane_state this pane sent, never a path (owner level).
        std::function<bool(const QString &session, const QString &deviceName)> conversationOpen;
        // {session}: the real conversation id behind a published token, on explicit ask (owner
        // level). Empty when the token is no longer in the list this pane published.
        std::function<QString(const QString &session)> conversationId;
        std::function<void()> publishPaneState;   // pane_state_get: publish this pane now
        std::function<void()> recap;              // recap_request, which used to be dropped here
    };

    // Start the sidecar if needed and share this pane. Returns false with `error` set when the
    // pane cannot be shared (a KonsolePart pane, or the sidecar failed to start).
    // `tab` is set when the pane is shared as part of a whole tab: it rides on the first `pane`
    // line, so the tab's guests hold the pane before the list announcing it reaches them.
    bool sharePane(const QString &paneId, const PaneHooks &hooks, QString *error,
                   const QString &tab = QString());
    void stopSharing(const QString &paneId);
    void stopAll();

    // "Share whole tab" (owner, 2026-09-18). A tab shared whole is an id the window gives the tab
    // page; every pane in it is shared under that id, the window shares each pane added later,
    // and an invite made with the id grows with the tab (docs/REMOTE-PROTOCOL.md section 10.1).
    bool isTabShared(const QString &tab) const { return !tab.isEmpty() && m_tabShares.contains(tab); }
    bool hasTabShares() const { return !m_tabShares.isEmpty() || m_allTabsShared; }
    bool isAllTabsShared() const { return m_allTabsShared; }
    static QString allTabsScope() { return QStringLiteral("all-tabs"); }
    QString tabOf(const QString &paneId) const { return m_panes.value(paneId).tab; }
    QStringList panesInTab(const QString &tab) const;
    // Start sharing the tab whole. The window then shares its panes (tabSharesChanged).
    void shareTab(const QString &tab);
    // Stop: every pane shared under the tab stops being shared, and the tab's guests go with it.
    void unshareTab(const QString &tab);
    // "All tabs" is the process-wide sibling of whole-tab sharing. Every Relay window publishes
    // every pane it has now and later; invites carrying allTabsScope() grow with every published
    // pane, regardless of its tab id.
    void shareAllTabs();
    void unshareAllTabs();
    void markAllTabsPane(const QString &paneId) { m_allTabsPanes.insert(paneId); }
    // A shared pane moved into a tab shared whole, or out of one ("" for on its own).
    void setPaneTab(const QString &paneId, const QString &tab);

    void requestPairing();
    // The pairing code (#FR1C): four letters and four digits the owner types on the phone, instead
    // of scanning a QR that opens Safari and pairs a browser tab no notification ever reaches.
    // `pair_code` mints one; `pair_code_revoke` withdraws it, which the dialog does as it closes.
    void requestPairCode();
    void revokePairCode(const QString &code);
    // The paired devices, as the sidecar last reported them (`devices`), and a request for the
    // list again. The sidecar reports the list when a device pairs, is revoked or has its
    // password switch moved, and once at `start` — with remote control on (#PH0N) that is at
    // launch, long before any share window exists to hear it, so a window opened later read
    // an empty list for a phone that was connected the whole time (found by the hosted drive,
    // docs/qa_evidence/2026-09-21-ph0n-hosted-drive). The window now opens on the cached list.
    QJsonArray devices() const { return m_devices; }
    void requestDevices();
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
    // With `tab`, the invite is for the whole tab: its panes now and every one added later.
    void createInvite(const QString &paneId, const QString &role, int expires, int uses,
                      const QString &tab = QString());
    void revokeInvite(const QString &inviteId);
    // `code_create {pane, role}` → a `code` line: a four-letter meeting code and a four-digit PIN
    // that a person reads out instead of sending a link (#97EG). It always lasts ten minutes and
    // lets in one person; the sidecar mints an ordinary invite behind it.
    void createCode(const QString &paneId, const QString &role, const QString &tab = QString());
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

    // The Switchboard on the owner's devices (#SWPH, src/BoardRemote.h). One worker event going
    // out as `{"t":"board_event","rid":n|null,"event":{…}}`: `rid` is the `rid` of the
    // `board_request` it answers, or null for a broadcast. The hub decides which devices see it
    // (the owner's `full` ones, never a guest) and sanitises it again.
    void sendBoardEvent(const QJsonValue &rid, const QJsonObject &event);

signals:
    // A `board_request` line, whole: {"t","rid","device","name","request":{…}}. BoardRemote
    // checks it against the allow-list and answers through sendBoardEvent().
    void boardRequest(const QJsonObject &line);
    void startedChanged();
    void addressesChanged(const QJsonArray &addresses);
    void pairingReady(const QString &url, const relay::QrMatrix &qr, int expires);
    void pairingAsked(int id, const QString &name, const QString &platform,
                      const QString &fingerprint, const QString &code, const QString &peer);
    void devicesChanged(const QJsonArray &items);
    void failed(const QString &message);
    void inviteSent(bool ok, const QString &message);
    void sharingChanged();
    void tabSharesChanged();
    // An `invite` line: the link to hand out, its QR, and what it grants.
    void inviteReady(const QString &url, const relay::QrMatrix &qr, const QString &role,
                     int uses, int expires);
    // A `code` line. The PIN is the secret half: it goes to the dialog and nowhere else.
    void codeReady(const QString &code, const QString &pin, int expires, const QString &invite);
    // A `code_state` line: "used", "burned" (too many wrong PINs) or "expired".
    void codeStateChanged(const QString &code, const QString &state, int failures);
    // A `pair_code` line (#FR1C). The PIN is the secret half, as an invite code's is: it goes to
    // the pairing dialog and nowhere else.
    void pairCodeReady(const QString &code, const QString &pin, int expires);
    // A `pair_code_state` line: "used", "burned" (too many wrong PINs) or "expired".
    void pairCodeStateChanged(const QString &code, const QString &state, int failures);
    // Something changed in sharingModel(): the Sharing pane and the pane headers redraw.
    void sharingModelChanged();
    // Somebody is at the door, wants the keyboard, or has written a prompt. The window opens the
    // Sharing pane and posts a notification — and never takes the keyboard, because the next
    // keystroke would otherwise land on a button that admits a stranger.
    void needsOwner(const QString &paneId, const QString &title, const QString &body);
    // One second passed: the waiting rows' countdowns move and a lapsed one goes.
    void secondPassed();
    // The remote-control switch was turned on or off (#PH0N). On: every window publishes the panes
    // it already has, since only the ones opened afterwards would otherwise be reachable.
    void alwaysOnChanged(bool on);
    // A `remote_state` line: the service came up or went down, changed address, or gained or lost
    // one of the owner's devices. The window chrome's plug reads remoteState().
    void remoteStateChanged();

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
        qint64 statusSince = 0;   // epoch seconds when `status` last changed: the phone's "running · 3m"
        QString tab;              // the tab it is shared under, or "" for a pane on its own
        bool needFull = true;
    };
    QSet<QString> m_tabShares;
    QSet<QString> m_allTabsPanes;       // panes published only because All tabs is on
    bool m_allTabsShared = false;

    QProcess *m_process = nullptr;
    QByteArray m_pending;
    QHash<QString, Shared> m_panes;
    QTimer *m_poll = nullptr;
    bool m_running = false;
    QString m_base;
    QString m_note;
    QJsonArray m_addresses;
    QJsonArray m_devices;
    remotesettings::State m_remoteState;
    sharing::Model m_sharing;
    QTimer *m_second = nullptr;
};

// The window behind the share button: the QR code, the code to compare, and who is connected.
class RemoteShareDialog final : public QDialog {
    Q_OBJECT
public:
    explicit RemoteShareDialog(const QString &paneId, QWidget *parent = nullptr);
    // Every closing path — the X, Escape, "Stop sharing" — comes through done(), which is where
    // the pairing code is withdrawn: a code left live after the window went would pair a phone
    // nobody is watching for.
    void done(int result) override;
    // The tab the pane is in, so "Share the whole tab" can be offered. `tab` is the window's id
    // for the tab page; `panes` counts the terminals in it now, for the sentence beside the box.
    void setTab(const QString &tab, int panes);

private:
    void refreshPairingService();
    QString m_pairingBase;
    void updateWholeTab();
    void showPairing(const QString &url, const relay::QrMatrix &qr, int expires);
    // The pairing code beside the QR (#FR1C): minted when this window opens, withdrawn when it
    // closes, and drawn from relay::remotesettings::pairCodeRow so its four states are testable.
    void askPairCode();
    void showPairCode(const QString &code, const QString &pin, int expires);
    void showPairCodeState(const QString &code, const QString &state, int failures);
    void refreshPairCode();
    void noPairCode();
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
    QString m_tab;
    int m_tabPanes = 0;
    QCheckBox *m_wholeTab = nullptr;
    QCheckBox *m_allTabs = nullptr;
    QLabel *m_inviteHeading = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_qr = nullptr;
    QLabel *m_url = nullptr;
    // The one line saying remote control is on and where to turn it off: pairing turns it on, and
    // a switch that turned itself on must say so where it happened.
    QLabel *m_alwaysOnLine = nullptr;
    // Copying the pairing link, as the invite link is copied: on a Linux desktop a 140-character
    // link has no other way of reaching a phone, and scanning is not always an option.
    QPushButton *m_pairCopy = nullptr;
    QString m_pairingLink;
    // The pairing code column beside the QR.
    QWidget *m_pairBox = nullptr;
    QLabel *m_pairHeading = nullptr;
    QLabel *m_pairValue = nullptr;
    QLabel *m_pairClock = nullptr;
    QLabel *m_pairNote = nullptr;
    QPushButton *m_pairAgain = nullptr;
    QString m_pairCode, m_pairPin, m_pairState;
    int m_pairFailures = 0;
    qint64 m_pairDeadline = 0;    // ms since the epoch; 0 once the code is no longer live
    bool m_pairWaiting = false;   // a pair_code is out and nothing has answered it yet
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
