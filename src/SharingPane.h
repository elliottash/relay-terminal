// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Multiplayer, the owner's side (#W5N2, docs/REMOTE-PROTOCOL.md section 10).
//
// Sharing a pane with your own phone is pairing and stays a dialog (src/RemoteShare.h): a QR code
// is a one-off moment. Inviting another *person* is not a moment — somebody is at the door, wants
// the keyboard, or has written a prompt for your agent, and you need to see all of it at once and
// answer it whenever you look up. That is a pane, in the splitter, beside the pane being shared
// (owner's rule: new surfaces are panes, never floating strips).
//
// Two halves, deliberately separable:
//
//  * `relay::sharing::Model` — no widgets. It eats the sidecar's line JSON (10.5), keeps who is
//    here, which invites are live, what is waiting for the owner and who is driving each pane,
//    and expires a waiting request on the hub's own clock. Unit tested without a display
//    (tests/sharingpane_test.cpp).
//  * `relay::sharing::SharingView` — the pane. It renders the model and calls back; it never
//    speaks to the sidecar itself, so the transport stays in RemoteShare.
//
// The rules the UI has to keep, all of them from section 10:
//  * Refuse is the default on every question and holds the focus. A knock is answered with a
//    five-digit code shown large; Return must never admit anybody (the pairing dialog learned
//    this first, RemoteShareDialog::showAsk).
//  * Admitting may lower an invite's role and never raise it, so the editor button is simply
//    absent on a viewer-only invite.
//  * A guest prompt is shown whole, wrapped, never elided: approving is approving that text.
//  * "Approve once" only. There is no "Approve always" in v1 and the absence is by design
//    (section 10.5): every v1 invite is a bare link with no identity behind it.
#include "PaneView.h"
#include "SystemContexts.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPixmap;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QStackedWidget;
class QTabBar;
class QVBoxLayout;

namespace relay {
class ContextDock;
}

namespace relay::sharing {

// The deadlines the hub keeps (sections 10.2, 10.3, 10.4). The countdown in the pane is started
// when the line arrives rather than from a hub timestamp, so it can only ever run *ahead* of the
// hub: a row disappears from the owner's screen a moment before the hub gives up on it, never
// after, and the hub's own answer is what actually decides.
inline constexpr int kKnockSeconds = 120;
inline constexpr int kControlSeconds = 60;
inline constexpr int kPromptSeconds = 600;

struct Participant {
    QString id, name, platform, role, fingerprint, invite;
    QStringList panes;
    QStringList viewingPanes;   // panes open on a live guest channel
    QStringList drivingPanes;   // the panes whose control token they hold, as the hub reports it
    qint64 expires = 0;         // seconds this participant record has left
    bool online = true;         // a record outlives the connection: they may be away, not gone
    bool driving = false;       // filled in by participantsOn(), for the pane being asked about
};

struct Invite {
    QString id, role;
    QStringList panes;
    int uses = 0;            // uses left
    qint64 expires = 0;      // seconds
};

// One question waiting for the owner. Three kinds share a struct because they share everything
// that matters here: a person, a pane, a countdown and two or three buttons.
struct Request {
    enum class Kind { Knock, Control, Prompt };
    Kind kind = Kind::Knock;
    QString id;              // knock/control: the participant id. prompt: the prompt id
    QString participant, name, platform, fingerprint, peer, pane, code, role, text;
    QString plan;            // a guest's plan_execute arrives as a prompt (section 10.4)
    qint64 askedAtMs = 0;
    int seconds = kKnockSeconds;

    int secondsLeft(qint64 nowMs) const;
    bool lapsed(qint64 nowMs) const { return secondsLeft(nowMs) <= 0; }
    // Unique within the model: a knock and a control request from one person are different rows.
    QString key() const;
};

// A pane this desktop is sharing, and the label the owner knows it by (the pane's own title, not
// its session token, which is what a guest never sees either).
struct SharedPane {
    QString id, title;
    QString tab;             // "" on its own, the tab id when shared as a whole tab, or "all-tabs"
    bool operator==(const SharedPane &other) const {
        return id == other.id && title == other.title && tab == other.tab;
    }
    bool operator!=(const SharedPane &other) const { return !(*this == other); }
};

// One of the owner's own paired phones, as the sidecar's `devices` line lists them (#SHRP). Not a
// guest: it is the owner, on another screen, and the pane's top line is the only place it shows.
struct Device {
    QString id, name, platform;
    QStringList panes;       // panes this connected device currently has open
    bool online = false;     // holding a live channel right now
    QString capability;      // "full" (watch and type) or "view", as the sidecar reports it
    bool passwords = false;  // may answer a password prompt (section 6.7); off until turned on
};

// ---- what the view is handed and what it asks for (#SMDX) -------------------------------------
// The share window (RemoteShareDialog) is gone: pairing your own devices, inviting people and
// looking after them are the two pages of this pane. The view stays a view — it renders these
// structs and calls the hooks below; RemoteShare::attach() wires the hooks that only need the
// sidecar, and the window wires the ones that need a pane (scope catalogue, publishing a pane,
// the remote-control switch). Nothing here includes RemoteShare.h.

// A QR code as the sidecar sends it: rows of 0/1. RemoteShare.h aliases this name.
using QrMatrix = QVector<QVector<int>>;

// What one invite or meeting code is for. Pane: one pane. Tab: every pane in that tab now and
// later (protocol 10.1; the wire's `tab`). All: every pane in every window (the reserved scope
// "all-tabs", card #A11T). The window builds the list (SharingView::onScopes); the view shows it
// as one picker with three groups and never invents a scope of its own.
struct Scope {
    enum class Kind { Pane, Tab, All };
    Kind kind = Kind::Pane;
    QString id;         // Pane: the pane's session token. Tab: the window's tab id. All: "all-tabs".
    QString title;      // Pane: the pane's title. Tab: the tab's project or title. All: "Everything".
    QString tab;        // Pane: the tab it sits in (its id). Tab: == id. All: "all-tabs".
    QString tabTitle;   // Pane/Tab: what the tab is called, for grouping the picker.
    int panes = 0;      // Tab/All: how many panes it covers right now.
    bool current = false;   // the pane (or its tab) the Sharing pane was opened from
    bool operator==(const Scope &o) const { return kind == o.kind && id == o.id; }
};

// The service as RemoteShare knows it, for the Devices page's gate on spending a pairing room
// (#PRM2): nothing is minted until the sidecar is registered at the remembered address.
struct Service {
    bool running = false;    // the sidecar is up
    bool alwaysOn = false;   // remote control is on (Options › Remote / the plug)
    bool online = false;     // registered at `onlineBase`
    QString base;            // RemoteShare::base(): where the sidecar is publishing now
    QString onlineBase;      // remoteState().base
    QString note;            // RemoteShare::note(): the one sentence about the current address
    QString addressLabel;    // remotesettings::addressName(address) — "relay-terminal.ai", "your tailnet"
};

// One device asking to be paired (`ask`, section 5). Shown on the Devices page with Refuse first
// and holding the focus; `id` goes back in onPairAnswer.
struct DeviceAsk {
    int id = -1;
    QString name, platform, fingerprint, code, peer;
    bool valid() const { return id >= 0; }
};

// Per shared pane, and per share: both off by default (section 10.5).
struct ShareOptions {
    bool paused = false;
    bool promptsImmediate = false;
    bool presentOnly = false;
};

// What the pane's own header says. Empty when this pane is not shared at all.
struct ChipState {
    bool visible = false;   // a connected person or device is viewing this pane
    QString text;            // empty for icon-only, otherwise "2 guests" or "alice is typing"
    QString tooltip;
    bool guestDriving = false;
};

class Model {
public:
    // ---- lines in ---------------------------------------------------------------------------
    // `participants {items, invites}`: the whole picture, so it replaces what was held.
    void setParticipants(const QJsonArray &items, const QJsonArray &invites);
    // `knock`, `control_ask`, `prompt_ask`. A second line for the same person and kind replaces
    // the first rather than stacking, because the hub only ever has one of each outstanding.
    void addKnock(const QJsonObject &line, qint64 nowMs);
    void addControlAsk(const QJsonObject &line, qint64 nowMs);
    void addPromptAsk(const QJsonObject &line, qint64 nowMs);
    // `control {pane, holder, name, device, device_name}`: the holder is "owner", "agent" or
    // "participant:<id>". `device` is set when what holds the pane is one of the owner's own
    // paired devices — the phone is the owner, so the wire says "owner" either way, and this is
    // how the desktop knows its keystroke has somebody to take the pane back from (section 10.3).
    void setControl(const QString &pane, const QString &holder, const QString &name,
                    const QString &device = QString(), const QString &deviceName = QString());
    // `share_state {pane, paused, reason}`: why guests cannot act — "owner" (you paused it) or
    // "away" (present-only, and Relay's window is not the one you are looking at).
    void setShareState(const QString &pane, bool paused, const QString &reason);
    QString pauseReason(const QString &pane) const { return m_pauseReason.value(pane); }
    // The owner answered, or the hub says it is gone.
    void dropRequest(Request::Kind kind, const QString &id);
    void dropParticipant(const QString &id);
    // Every request whose countdown has run out, removed; returns their keys so the caller can
    // say so. Requests lapse on the hub's clock and this is the mirror of it.
    QStringList expire(qint64 nowMs);

    // Which panes the GUI is sharing at all. A pane with no guests is still listed, because
    // "shared with nobody yet" is a state the owner asked for and should see.
    void setSharedPanes(const QList<SharedPane> &panes);
    QList<SharedPane> sharedPanes() const { return m_shared; }
    QString paneTitle(const QString &pane) const;
    // A pane stopped being shared: its rows, invites and options go with it.
    void forgetPane(const QString &pane);

    void setOptions(const QString &pane, const ShareOptions &options);
    ShareOptions options(const QString &pane) const { return m_options.value(pane); }

    // Remote control as the window chrome knows it (`remote_state`, #PH0N) and the owner's own
    // paired devices (`devices`), for the pane's top line (#SHRP). `addressLabel` is the address
    // as a person says it (remotesettings::addressName); `connected` is the hub's count, which
    // is what an older sidecar sends when its device records carry no `online` flag.
    void setRemote(bool on, const QString &addressLabel, bool online, int connected = 0,
                   const QString &reason = QString());
    void setDevices(const QList<Device> &devices);
    void setDevices(const QJsonArray &items);       // the `devices` line as it arrives
    bool remoteOn() const { return m_remoteOn; }
    QList<Device> devices() const { return m_devices; }
    // The names of the devices holding a live channel, in the sidecar's order.
    QStringList connectedDeviceNames() const;
    // "Remote control on · relay-terminal.ai · iPhone, iPad connected", "… · no phone
    // connected", "… · offline: <reason>" or "Remote control off".
    QString topLine() const;

    // ---- what the UI asks -------------------------------------------------------------------
    QList<Request> requests(const QString &pane = QString()) const;
    QList<Participant> participantsOn(const QString &pane) const;
    QList<Invite> invitesOn(const QString &pane) const;
    // The guest driving this pane, by name; empty when the owner or the agent holds it.
    QString driverOn(const QString &pane) const;
    // One of the owner's own devices driving this pane, by the name it paired under ("Pixel 9")
    // and its id if it has no name; empty when the desktop, the agent or a guest holds it.
    QString deviceDriverOn(const QString &pane) const;
    int guestsOn(const QString &pane) const;
    int waitingOn(const QString &pane) const { return int(requests(pane).size()); }
    int waiting() const { return int(m_requests.size()); }
    bool anyShared() const { return !m_shared.isEmpty(); }

    // The pane header's chip. `phone` is whether the pane is published.
    ChipState chip(const QString &pane, bool phone) const;

private:
    void add(const Request &request);

    QList<Participant> m_participants;
    QList<Invite> m_invites;
    QList<Request> m_requests;
    QHash<QString, QString> m_holder;     // pane -> holder string from `control`
    QHash<QString, QString> m_holderName; // pane -> the name the hub gave
    QHash<QString, QString> m_holderDevice;     // pane -> the owner's device driving it, if any
    QHash<QString, QString> m_holderDeviceName; // pane -> what to call that device
    QHash<QString, ShareOptions> m_options;
    QHash<QString, QString> m_pauseReason;
    QList<SharedPane> m_shared;
    QList<Device> m_devices;
    bool m_remoteOn = false;
    bool m_remoteOnline = false;
    int m_remoteConnected = 0;
    QString m_remoteAddress;
    QString m_remoteReason;
};

// ---- the sentences, in one place so the dialog, the pane and the tests agree ------------------

// What a role lets someone do, said plainly and in full. Shown under the role choice in the invite
// section and again beside every participant.
QString roleSentence(const QString &role);
// "24 hours", "7 days", "58 min", "expired"
QString expiryText(qint64 seconds);
// "1:58" / "0:04" — a waiting request's countdown.
QString countdown(int secondsLeft);
// "1 use left" / "3 uses left" / "spent"
QString usesText(int uses);
// What each of the two share options does, in full: the checkbox's tooltip, and the one note
// under the Guests section that says both once (#SHRP).
QString promptsImmediateSentence();
QString presentOnlySentence();

// The QR as a picture: black cells on white, scaled so the whole code is about `target` pixels
// wide and never fractional. Here rather than in RemoteShare because both pages draw one (#SMDX).
QPixmap qrPixmap(const QrMatrix &matrix, int target);

// ---- the pane --------------------------------------------------------------------------------

class SharingView final : public QWidget, public relay::PaneView {
public:
    explicit SharingView(QWidget *parent = nullptr);
    // A live pairing code is withdrawn with the pane (stopPairing).
    ~SharingView() override;

    // ---- the two pages (#SMDX) --------------------------------------------------------------
    // Devices: remote control, its address, your paired phones and other computers, "Add a
    // device…" (the QR, the typed code and Copy link, minted on press and withdrawn on leaving),
    // and a device's approval card. People: what is waiting for you, what is shared with whom,
    // and the invite form (scope picker, role, expiry, uses, link/QR/email, meeting code).
    enum class Page { Devices, People };
    void showPage(Page page);
    Page page() const;
    // People, with the invite form open on this scope. What "Share this pane…" on a pane's
    // share chip lands on; an empty scope opens the form with the picker on its current pane.
    void startInvite(const Scope &scope);
    // Devices, with a pairing offer started ("Add a device…" pressed for the owner). What "Pair a
    // phone…" everywhere lands on. Idempotent while an offer is live.
    void startPairing();
    // The offer is withdrawn (the typed code revoked) when the owner leaves the Devices page,
    // presses Done, or the pane closes. Safe to call when nothing is live.
    void stopPairing();

    // ---- what comes in, wired by RemoteShare::attach() --------------------------------------
    void setService(const Service &service);          // startedChanged / remoteStateChanged
    void setAddresses(const QJsonArray &addresses);   // the sidecar's `addresses` line
    void showPairing(const QString &url, const QrMatrix &qr, int expires);   // `pairing`
    void showPairCode(const QString &code, const QString &pin, int expires); // `pair_code`
    void showPairCodeState(const QString &code, const QString &state, int failures);
    void showAsk(const DeviceAsk &ask);               // `ask`; an invalid ask clears the card
    void showInvite(const QString &url, const QrMatrix &qr, const QString &role, int uses,
                    int expires);                     // `invite`
    void inviteSent(bool ok, const QString &message); // the email went, or did not
    void showCode(const QString &code, const QString &pin, int expires);     // `code`
    void showCodeState(const QString &code, const QString &state, int failures);
    void serviceFailed(const QString &message);       // RemoteShare::failed

    // ---- what the view asks for (#SMDX) ------------------------------------------------------
    // Wired by RemoteShare::attach(): they need only the sidecar.
    std::function<void()> onPairRequest;                        // requestPairing + requestDevices
    std::function<void()> onPairCodeRequest;                    // requestPairCode
    std::function<void(const QString &code)> onPairCodeRevoke;  // revokePairCode
    std::function<void(int askId, bool allow, const QString &capability)> onPairAnswer;
    std::function<void(const QString &address)> onAddressPick;  // useAddress
    std::function<void(const QString &device)> onRevokeDevice;
    std::function<void(const QString &device, bool allow)> onPasswordEntry;
    std::function<void(const QString &code)> onRevokeCode;
    std::function<void(const QString &url, const QString &to, const QString &role,
                       const QString &expiry, const QString &what)> onEmailInvite;
    // Wired by the window: they need panes or settings.
    std::function<QList<Scope>()> onScopes;                     // the picker's contents, fresh
    std::function<void(bool on)> onRemoteSwitch;                // Remote control on/off
    // Make a link / a code for a scope. The window publishes what is not yet published (a pane
    // with the switch off, a tab shared whole, all tabs) and then calls RemoteShare.
    std::function<void(const Scope &scope, const QString &role, int expires, int uses)> onCreateInvite;
    std::function<void(const Scope &scope, const QString &role)> onCreateCode;

    // Everything the view can ask for. RemoteShare fills these in; the view never knows there is
    // a sidecar. Each maps to exactly one line of section 10.5.
    std::function<void(const QString &participant, bool admit, const QString &role)> onKnockAnswer;
    std::function<void(const QString &pane, const QString &participant, bool grant)> onControlAnswer;
    std::function<void(const QString &promptId, bool approve)> onPromptAnswer;
    std::function<void(const QString &participant, const QString &role)> onRoleSet;
    std::function<void(const QString &participant)> onRemove;
    std::function<void(const QString &inviteId)> onRevokeInvite;
    std::function<void(const QString &pane, bool on)> onPause;
    std::function<void(const QString &pane)> onEndShare;
    std::function<void(const QString &pane, bool promptsImmediate, bool presentOnly)> onOptions;
    // Kept for the window: an Invite… button on a shared-now row calls startInvite() itself, so
    // nobody needs to wire this any more; it is invoked as well when set.
    std::function<void(const QString &pane)> onInvite;
    std::function<void()> onClose;                          // Esc, or the pane chrome's ×
    std::function<void()> onTitleChanged;

    // The model is owned by the caller (RemoteShare keeps one per process) so that the pane's
    // header chips and this pane read the same rows.
    void setModel(Model *model) { m_model = model; }
    // Something changed: rebuild the rows. Cheap enough to call on every line.
    void refresh();
    // One second passed: the countdowns move. Nothing is rebuilt, so the focus stays where the
    // owner left it; a row that has lapsed is removed from the model by whoever owns the clock,
    // which then calls refresh() on every one of these.
    void tick();
    // Which pane's share to show first. Empty means "all of them, in the order they were shared".
    void focusPane(const QString &pane);

    QString paneTitle() const override;
    void focusView() override;
    void setHeaderRightInset(int pixels) override;

    // ---- the docked agent (card #3B1B) ------------------------------------------------------
    // The "Agent (Alt+Q)" row at the foot of both pages and the `SharingContext` it is about; the
    // window wires the dock with `wireConsoleHost`, as it wires Options' and Models'.
    relay::ContextDock *agentDock() const { return m_dock; }
    relay::agent::SharingContext *agentContext() { return &m_agentContext; }
    // What the agent's `screen` says: the page, remote control, the devices, the shared panes.
    relay::agent::SharingState agentState() const;
    // A `pane:` link in the agent's answer: the window focuses that pane. Wired by the window.
    std::function<bool(const QString &token)> onFocusPane;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void build();
    void buildDevicesPage();
    void buildPeoplePage();
    void buildInviteForm(QVBoxLayout *column);
    // The People page's rows, rebuilt wholesale; the Devices page's texts and device rows; the
    // two tabs' labels ("People · 2 waiting", "Devices · 1 asking").
    void rebuildPeople();
    void refreshDevices();
    void refreshTabs();
    // Takes every row out of a column, moving the keyboard to `fallback` first when it was on
    // one of them, so hiding a row cannot hand the focus to the next button in the chain.
    void clearColumn(QVBoxLayout *column, QWidget *fallback);
    QWidget *heading(const QString &text);
    QWidget *subheading(const QString &text);
    QWidget *requestRow(const Request &request, bool editorAllowed);
    QWidget *participantRow(const Participant &person, const QString &pane);
    QWidget *inviteRow(const Invite &invite);
    QWidget *shareControls(const QString &pane, const Scope &scope);
    QWidget *deviceRow(const Device &device);
    QWidget *note(const QString &text);
    // The scope a shared pane's block is headed by, and its heading ("Pane “build”",
    // "Tab “thesis”", "Everything"). `scopes` is the window's catalogue, for the tab's name.
    Scope scopeOf(const SharedPane &pane, const QList<Scope> &scopes) const;
    QString scopeHeading(const Scope &scope) const;
    // ---- pairing (#FR1C, #PRM2) ---------------------------------------------------------------
    void refreshPairingService();
    void askPairCode();
    void refreshPairCode();
    void noPairCode();
    void clearPairing();
    void answerAsk(bool allow, const QString &capability);
    // ---- the invite form (section 10.2, #97EG) --------------------------------------------------
    void fillScopePicker(const Scope &wanted);
    Scope pickedScope() const;
    void updateRoleNote();
    void createInvite();
    void createCode();
    void markCodeDead(bool dead);
    void codeTick();
    void putCodeAway();

    Model *m_model = nullptr;
    QWidget *m_inset = nullptr;
    QTabBar *m_tabs = nullptr;
    QStackedWidget *m_pages = nullptr;
    // The People page: its scroll area, the rows (rebuilt) and the invite form (kept).
    QScrollArea *m_scroll = nullptr;
    QWidget *m_body = nullptr;
    QVBoxLayout *m_column = nullptr;
    QWidget *m_rows = nullptr;
    QVBoxLayout *m_rowsColumn = nullptr;
    QString m_pane;                       // the share shown first, and the picker's default
    // Deleted before the context in ~SharingView: the console's wrapper writes to it as it goes.
    relay::agent::SharingContext m_agentContext;
    relay::ContextDock *m_dock = nullptr;
    // The countdown labels of the rows on screen, by request key, so a tick moves the numbers
    // without rebuilding the rows under the owner's fingers.
    QHash<QString, QLabel *> m_clocks;
    // The first Refuse button on screen: what focusView() hands the keyboard to, so the key that
    // opens the pane cannot land on Admit.
    QWidget *m_firstRefuse = nullptr;
    // The invite form.
    QFrame *m_inviteForm = nullptr;
    QComboBox *m_scopePick = nullptr;
    QList<Scope> m_scopes;                // what the picker's rows index into
    bool m_fillingScopes = false;
    QComboBox *m_inviteRole = nullptr;
    QComboBox *m_inviteExpiry = nullptr;
    QSpinBox *m_inviteUses = nullptr;
    QPushButton *m_makeCode = nullptr;
    QLabel *m_inviteNote = nullptr;
    QWidget *m_linkRow = nullptr;
    QLabel *m_inviteQr = nullptr;
    QLineEdit *m_inviteUrl = nullptr;
    QPushButton *m_inviteCopy = nullptr;
    QLineEdit *m_inviteTo = nullptr;
    QPushButton *m_inviteSend = nullptr;
    Scope m_inviteScope;                  // what the link or code on screen was made for
    QString m_inviteLink, m_inviteRoleValue, m_inviteExpiryText;
    bool m_inviteAsked = false;           // a link was asked for and has not arrived
    // The meeting code.
    QLabel *m_codeNote = nullptr;
    QWidget *m_codeBox = nullptr;
    QLabel *m_codeValue = nullptr;
    QLabel *m_pinValue = nullptr;
    QLabel *m_codeClock = nullptr;
    QPushButton *m_codeCopy = nullptr;
    QPushButton *m_codeAgain = nullptr;
    QString m_code, m_pin, m_codeRole;
    qint64 m_codeDeadline = 0;            // ms since the epoch; 0 when no code is live
    qint64 m_codeAskedAt = 0;             // ms; 0 = not asked, -1 = gave up but still listening
    // The Devices page.
    QScrollArea *m_devicesScroll = nullptr;
    QWidget *m_devicesBody = nullptr;
    QVBoxLayout *m_devicesColumn = nullptr;
    QLabel *m_topLine = nullptr;
    QCheckBox *m_remoteSwitch = nullptr;
    QLabel *m_alwaysOnLine = nullptr;
    QComboBox *m_address = nullptr;
    QLabel *m_addressNote = nullptr;
    QWidget *m_deviceRows = nullptr;
    QVBoxLayout *m_deviceColumn = nullptr;
    QPushButton *m_addDevice = nullptr;
    Service m_service;
    // The pairing card and its offer.
    QFrame *m_pairCard = nullptr;
    QLabel *m_pairStatus = nullptr;
    QLabel *m_qr = nullptr;
    QLabel *m_pairHeading = nullptr;
    QLabel *m_pairValue = nullptr;
    QLabel *m_pairClock = nullptr;
    QLabel *m_pairNote = nullptr;
    QPushButton *m_pairAgain = nullptr;
    QLabel *m_pairUrl = nullptr;
    QPushButton *m_pairCopy = nullptr;
    QPushButton *m_pairDone = nullptr;
    bool m_pairingLive = false;           // the card is up: an offer was asked for or is on screen
    QString m_pairingBase;                // the base the live offer was minted at; never re-minted
    QString m_pairingLink;
    bool m_pairWaiting = false;           // pair_code asked for, nothing back yet
    QString m_pairCode, m_pairPin, m_pairState;
    int m_pairFailures = 0;
    qint64 m_pairDeadline = 0;            // ms since the epoch; 0 when no code is live
    // The approval card.
    QFrame *m_askCard = nullptr;
    QLabel *m_askText = nullptr;
    QLabel *m_askCode = nullptr;
    QPushButton *m_askRefuse = nullptr;
    QLabel *m_askResult = nullptr;
    int m_askId = -1;
};

}  // namespace relay::sharing
