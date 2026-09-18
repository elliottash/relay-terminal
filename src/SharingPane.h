// SPDX-License-Identifier: GPL-3.0-or-later
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

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

class QLabel;
class QScrollArea;
class QVBoxLayout;

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
    qint64 expires = 0;      // seconds this participant record has left
    bool driving = false;    // holds this pane's control token right now
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
};

// Per shared pane, and per share: both off by default (section 10.5).
struct ShareOptions {
    bool paused = false;
    bool promptsImmediate = false;
    bool presentOnly = false;
};

// What the pane's own header says. Empty when this pane is not shared at all.
struct ChipState {
    QString text;            // "phone", "2 guests", "alice is typing"
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
    // `control {pane, holder, name}`: "owner", "agent" or "participant:<id>".
    void setControl(const QString &pane, const QString &holder, const QString &name);
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

    // ---- what the UI asks -------------------------------------------------------------------
    QList<Request> requests(const QString &pane = QString()) const;
    QList<Participant> participantsOn(const QString &pane) const;
    QList<Invite> invitesOn(const QString &pane) const;
    // The guest driving this pane, by name; empty when the owner or the agent holds it.
    QString driverOn(const QString &pane) const;
    int guestsOn(const QString &pane) const;
    int waitingOn(const QString &pane) const { return int(requests(pane).size()); }
    int waiting() const { return int(m_requests.size()); }
    bool anyShared() const { return !m_shared.isEmpty(); }

    // The pane header's chip. `phone` is whether the pane is shared at all.
    ChipState chip(const QString &pane, bool phone) const;

private:
    void add(const Request &request);

    QList<Participant> m_participants;
    QList<Invite> m_invites;
    QList<Request> m_requests;
    QHash<QString, QString> m_holder;     // pane -> holder string from `control`
    QHash<QString, QString> m_holderName; // pane -> the name the hub gave
    QHash<QString, ShareOptions> m_options;
    QList<SharedPane> m_shared;
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

// ---- the pane --------------------------------------------------------------------------------

class SharingView final : public QWidget, public relay::PaneView {
public:
    explicit SharingView(QWidget *parent = nullptr);

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
    std::function<void(const QString &pane)> onInvite;      // open the share dialog on this pane
    std::function<void()> onClose;                          // Esc, or the pane chrome's ×
    std::function<void()> onTitleChanged;

    // The model is owned by the caller (RemoteShare keeps one per process) so that the pane's
    // header chips and this pane read the same rows.
    void setModel(Model *model) { m_model = model; }
    // Something changed: rebuild the rows. Cheap enough to call on every line.
    void refresh();
    // One second passed: the countdowns move, and a row that lapsed goes. Nothing is rebuilt
    // unless something actually went, so the focus stays where the owner left it.
    void tick();
    // Which pane's share to show first. Empty means "all of them, in the order they were shared".
    void focusPane(const QString &pane);

    QString paneTitle() const override;
    void focusView() override;
    void setHeaderRightInset(int pixels) override;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void build();
    QWidget *heading(const QString &text);
    QWidget *requestRow(const Request &request, bool editorAllowed);
    QWidget *participantRow(const Participant &person, const QString &pane);
    QWidget *inviteRow(const Invite &invite);
    QWidget *shareControls(const QString &pane);
    QWidget *note(const QString &text);

    Model *m_model = nullptr;
    QLabel *m_title = nullptr;
    QWidget *m_inset = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_body = nullptr;
    QVBoxLayout *m_column = nullptr;
    QString m_pane;                       // the share shown first
    // The countdown labels of the rows on screen, by request key, so a tick moves the numbers
    // without rebuilding the rows under the owner's fingers.
    QHash<QString, QLabel *> m_clocks;
    // The first Refuse button on screen: what focusView() hands the keyboard to, so the key that
    // opens the pane cannot land on Admit.
    QWidget *m_firstRefuse = nullptr;
};

}  // namespace relay::sharing
