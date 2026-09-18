// SPDX-License-Identifier: GPL-3.0-or-later
#include "SharingPane.h"

#include <QCheckBox>
#include <QDateTime>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

namespace relay::sharing {

namespace {

// The epoch after which nothing is treated as "seconds from now" any more. Anything past it is a
// unix timestamp; 2001-09-09 is far enough in the past that no plausible countdown reaches it and
// far enough in the future that no plausible timestamp falls short of it.
constexpr double kEpochThreshold = 1'000'000'000.0;

qint64 secondsLeft(double value)
{
    if (value <= 0) return 0;
    if (value < kEpochThreshold) return qint64(value);
    return qMax<qint64>(0, qint64(value - double(QDateTime::currentSecsSinceEpoch())));
}

QString roleWord(const QString &role)
{
    return role == QLatin1String("editor") ? QStringLiteral("editor") : QStringLiteral("viewer");
}

// Lays its children out left to right and wraps to the next line when the width runs out. The
// answer buttons are three short words in a pane the owner may have made narrow; without this one
// of them would be pushed off the edge, which on a row whose middle button is "Admit as editor"
// is not a cosmetic failure.
class FlowRow final : public QWidget {
public:
    explicit FlowRow(QWidget *parent = nullptr) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    }
    void add(QWidget *child)
    {
        child->setParent(this);
        m_children.append(child);
        updateGeometry();
    }
    QSize sizeHint() const override { return {widestChild(), layoutFor(width() > 0 ? width() : widestChild(), false)}; }
    QSize minimumSizeHint() const override { return {widestChild(), layoutFor(widestChild(), false)}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return layoutFor(width, false); }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        layoutFor(width(), true);
    }

private:
    int widestChild() const
    {
        int widest = 0;
        for (const QWidget *child : m_children) widest = std::max(widest, child->sizeHint().width());
        return widest;
    }
    int layoutFor(int width, bool place) const
    {
        const int gap = 6;
        int x = 0, y = 0, lineHeight = 0;
        for (QWidget *child : m_children) {
            const QSize hint = child->sizeHint();
            if (x > 0 && x + hint.width() > width) { x = 0; y += lineHeight + gap; lineHeight = 0; }
            if (place) child->setGeometry(x, y, std::min(hint.width(), std::max(width, 1)), hint.height());
            x += hint.width() + gap;
            lineHeight = std::max(lineHeight, hint.height());
        }
        return y + lineHeight;
    }

    QList<QWidget *> m_children;
};

QLabel *plain(const QString &text, const char *name)
{
    auto *label = new QLabel(text);
    label->setObjectName(QLatin1String(name));
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    return label;
}

QPushButton *button(const QString &text, const QString &tip)
{
    auto *press = new QPushButton(text);
    press->setToolTip(tip);
    press->setAutoDefault(false);
    press->setDefault(false);
    return press;
}

QFrame *card()
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("settingsRow"));
    frame->setProperty("current", true);   // the Options pane's highlighted-row look: a soft panel
    auto *column = new QVBoxLayout(frame);
    column->setContentsMargins(10, 8, 10, 8);
    column->setSpacing(4);
    return frame;
}

}  // namespace

// ---- Request ---------------------------------------------------------------------------------

int Request::secondsLeft(qint64 nowMs) const
{
    const qint64 gone = (nowMs - askedAtMs) / 1000;
    return int(qMax<qint64>(0, seconds - gone));
}

QString Request::key() const
{
    const QString kindName = kind == Kind::Knock ? QStringLiteral("knock")
                           : kind == Kind::Control ? QStringLiteral("control")
                                                   : QStringLiteral("prompt");
    return kindName + QLatin1Char('/') + id;
}

// ---- Model -----------------------------------------------------------------------------------

void Model::setParticipants(const QJsonArray &items, const QJsonArray &invites)
{
    m_participants.clear();
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        Participant person;
        person.id = item.value(QStringLiteral("id")).toString();
        person.name = item.value(QStringLiteral("name")).toString();
        person.platform = item.value(QStringLiteral("platform")).toString();
        person.role = roleWord(item.value(QStringLiteral("role")).toString());
        person.fingerprint = item.value(QStringLiteral("fingerprint")).toString();
        person.invite = item.value(QStringLiteral("invite")).toString();
        // A participant's `expires` is an absolute epoch (remote/guests.py) while an invite's is
        // already the seconds left (`Invite.seconds_left()`). Both arrive as one field name, so
        // the big one is read as a clock time and turned into what the row actually shows.
        person.expires = secondsLeft(item.value(QStringLiteral("expires")).toDouble());
        // `online` is false for a record whose channel is not up: a guest whose phone went to
        // sleep is still admitted, and saying "gone" would be wrong.
        person.online = !item.contains(QStringLiteral("online"))
                        || item.value(QStringLiteral("online")).toBool();
        for (const QJsonValue &pane : item.value(QStringLiteral("panes")).toArray())
            person.panes << pane.toString();
        for (const QJsonValue &pane : item.value(QStringLiteral("driving")).toArray())
            person.drivingPanes << pane.toString();
        m_participants.append(person);
    }
    m_invites.clear();
    for (const QJsonValue &value : invites) {
        const QJsonObject item = value.toObject();
        Invite invite;
        invite.id = item.value(QStringLiteral("id")).toString();
        invite.role = roleWord(item.value(QStringLiteral("role")).toString());
        invite.uses = item.value(QStringLiteral("uses")).toInt();
        invite.expires = secondsLeft(item.value(QStringLiteral("expires")).toDouble());
        for (const QJsonValue &pane : item.value(QStringLiteral("panes")).toArray())
            invite.panes << pane.toString();
        m_invites.append(invite);
    }
    // A person who is in the list is no longer at the door: the knock that let them in is done,
    // whoever answered it. The same goes for a row the hub dropped underneath us.
    for (const Participant &person : std::as_const(m_participants))
        dropRequest(Request::Kind::Knock, person.id);
}

void Model::add(const Request &request)
{
    for (Request &existing : m_requests) {
        if (existing.kind == request.kind && existing.id == request.id) { existing = request; return; }
    }
    m_requests.append(request);
}

void Model::addKnock(const QJsonObject &line, qint64 nowMs)
{
    Request request;
    request.kind = Request::Kind::Knock;
    request.id = request.participant = line.value(QStringLiteral("participant")).toString();
    request.name = line.value(QStringLiteral("name")).toString();
    request.platform = line.value(QStringLiteral("platform")).toString();
    request.fingerprint = line.value(QStringLiteral("fingerprint")).toString();
    request.peer = line.value(QStringLiteral("peer")).toString();
    request.code = line.value(QStringLiteral("code")).toString();
    request.role = roleWord(line.value(QStringLiteral("role")).toString());
    request.pane = line.value(QStringLiteral("pane")).toString();
    if (request.pane.isEmpty()) {
        const QJsonArray panes = line.value(QStringLiteral("panes")).toArray();
        if (!panes.isEmpty()) request.pane = panes.first().toString();
    }
    request.askedAtMs = nowMs;
    request.seconds = kKnockSeconds;
    add(request);
}

void Model::addControlAsk(const QJsonObject &line, qint64 nowMs)
{
    Request request;
    request.kind = Request::Kind::Control;
    request.id = request.participant = line.value(QStringLiteral("participant")).toString();
    request.name = line.value(QStringLiteral("name")).toString();
    request.pane = line.value(QStringLiteral("pane")).toString();
    request.askedAtMs = nowMs;
    request.seconds = kControlSeconds;
    if (request.name.isEmpty()) {
        for (const Participant &person : std::as_const(m_participants))
            if (person.id == request.participant) request.name = person.name;
    }
    add(request);
}

void Model::addPromptAsk(const QJsonObject &line, qint64 nowMs)
{
    Request request;
    request.kind = Request::Kind::Prompt;
    request.id = line.value(QStringLiteral("id")).toString();
    request.participant = line.value(QStringLiteral("participant")).toString();
    request.name = line.value(QStringLiteral("name")).toString();
    request.pane = line.value(QStringLiteral("pane")).toString();
    request.text = line.value(QStringLiteral("text")).toString();
    request.plan = line.value(QStringLiteral("plan")).toString();
    request.askedAtMs = nowMs;
    request.seconds = kPromptSeconds;
    if (request.name.isEmpty()) {
        for (const Participant &person : std::as_const(m_participants))
            if (person.id == request.participant) request.name = person.name;
    }
    add(request);
}

void Model::setControl(const QString &pane, const QString &holder, const QString &name)
{
    m_holder.insert(pane, holder);
    m_holderName.insert(pane, name);
    const QString driver = holder.startsWith(QStringLiteral("participant:"))
                               ? holder.mid(QStringLiteral("participant:").size()) : QString();
    for (Participant &person : m_participants) {
        if (!person.panes.contains(pane)) continue;
        person.drivingPanes.removeAll(pane);
        if (!driver.isEmpty() && person.id == driver) person.drivingPanes << pane;
    }
    // Whoever now holds the pane is no longer asking for it.
    if (!driver.isEmpty()) dropRequest(Request::Kind::Control, driver);
}

void Model::setShareState(const QString &pane, bool paused, const QString &reason)
{
    ShareOptions options = m_options.value(pane);
    options.paused = paused;
    m_options.insert(pane, options);
    if (paused) m_pauseReason.insert(pane, reason); else m_pauseReason.remove(pane);
}

void Model::dropRequest(Request::Kind kind, const QString &id)
{
    for (int index = m_requests.size() - 1; index >= 0; --index)
        if (m_requests.at(index).kind == kind && m_requests.at(index).id == id)
            m_requests.removeAt(index);
}

void Model::dropParticipant(const QString &id)
{
    for (int index = m_participants.size() - 1; index >= 0; --index)
        if (m_participants.at(index).id == id) m_participants.removeAt(index);
    // Their parked questions go with them, whichever kind they are (#W5N2, hub side too).
    for (int index = m_requests.size() - 1; index >= 0; --index)
        if (m_requests.at(index).participant == id) m_requests.removeAt(index);
}

QStringList Model::expire(qint64 nowMs)
{
    QStringList gone;
    for (int index = m_requests.size() - 1; index >= 0; --index) {
        if (!m_requests.at(index).lapsed(nowMs)) continue;
        gone.prepend(m_requests.at(index).key());
        m_requests.removeAt(index);
    }
    return gone;
}

void Model::setSharedPanes(const QList<SharedPane> &panes)
{
    QStringList ids;
    for (const SharedPane &pane : panes) ids << pane.id;
    // By value: forgetPane() removes the very row this id lives in.
    for (int index = m_shared.size() - 1; index >= 0; --index) {
        const QString id = m_shared.at(index).id;
        if (!ids.contains(id)) forgetPane(id);
    }
    m_shared = panes;
}

QString Model::paneTitle(const QString &pane) const
{
    for (const SharedPane &shared : m_shared)
        if (shared.id == pane) return shared.title.isEmpty() ? pane : shared.title;
    return pane;
}

void Model::forgetPane(const QString &pane)
{
    for (int index = m_shared.size() - 1; index >= 0; --index)
        if (m_shared.at(index).id == pane) m_shared.removeAt(index);
    for (int index = m_requests.size() - 1; index >= 0; --index)
        if (m_requests.at(index).pane == pane) m_requests.removeAt(index);
    for (int index = m_invites.size() - 1; index >= 0; --index)
        if (m_invites.at(index).panes == QStringList{pane}) m_invites.removeAt(index);
    m_options.remove(pane);
    m_holder.remove(pane);
    m_holderName.remove(pane);
}

void Model::setOptions(const QString &pane, const ShareOptions &options)
{
    m_options.insert(pane, options);
}

QList<Request> Model::requests(const QString &pane) const
{
    if (pane.isEmpty()) return m_requests;
    QList<Request> mine;
    for (const Request &request : m_requests)
        if (request.pane == pane || request.pane.isEmpty()) mine.append(request);
    return mine;
}

QList<Participant> Model::participantsOn(const QString &pane) const
{
    QList<Participant> here;
    for (const Participant &person : m_participants) {
        if (!person.panes.contains(pane)) continue;
        Participant copy = person;
        copy.driving = person.drivingPanes.contains(pane) || m_holder.value(pane)
                           == QStringLiteral("participant:") + person.id;
        here.append(copy);
    }
    return here;
}

QList<Invite> Model::invitesOn(const QString &pane) const
{
    QList<Invite> here;
    for (const Invite &invite : m_invites)
        if (invite.panes.contains(pane)) here.append(invite);
    return here;
}

QString Model::driverOn(const QString &pane) const
{
    QString holder = m_holder.value(pane);
    if (holder.isEmpty()) {
        // No `control` line yet in this session: the participants list says it too, which is what
        // a Sharing pane opened after the handoff has to read.
        for (const Participant &person : m_participants)
            if (person.drivingPanes.contains(pane))
                holder = QStringLiteral("participant:") + person.id;
    }
    if (!holder.startsWith(QStringLiteral("participant:"))) return QString();
    const QString id = holder.mid(QStringLiteral("participant:").size());
    for (const Participant &person : m_participants)
        if (person.id == id) return person.name.isEmpty() ? id : person.name;
    const QString named = m_holderName.value(pane);
    return named.isEmpty() ? id : named;
}

int Model::guestsOn(const QString &pane) const
{
    return int(participantsOn(pane).size());
}

ChipState Model::chip(const QString &pane, bool phone) const
{
    ChipState state;
    if (!phone) return state;
    const QString driver = driverOn(pane);
    const int guests = guestsOn(pane);
    if (!driver.isEmpty()) {
        state.guestDriving = true;
        state.text = QStringLiteral("%1 is typing").arg(driver);
        state.tooltip = QStringLiteral(
            "%1 holds this pane's keyboard, so their keys reach this terminal.\n"
            "Type here and you take it straight back.").arg(driver);
        return state;
    }
    if (guests == 0) {
        state.text = QStringLiteral("phone");
        state.tooltip = QStringLiteral(
            "Shared with your phone: it sees this pane and can type into it.\n"
            "The share chip under the prompt box shows the code or stops it.");
        return state;
    }
    state.text = guests == 1 ? QStringLiteral("1 guest") : QStringLiteral("%1 guests").arg(guests);
    QStringList names;
    for (const Participant &person : participantsOn(pane))
        names << QStringLiteral("%1 (%2)").arg(person.name, person.role);
    state.tooltip = QStringLiteral("Shared with %1.\nYou are typing; nobody else holds this "
                                   "pane's keyboard.").arg(names.join(QStringLiteral(", ")));
    return state;
}

// ---- the sentences ---------------------------------------------------------------------------

QString roleSentence(const QString &role)
{
    if (roleWord(role) == QLatin1String("editor"))
        return QStringLiteral("An editor sees everything on this pane's screen, can ask to type "
                              "into it, and can send prompts to your agent — every prompt waits "
                              "for you to approve it.");
    return QStringLiteral("A viewer sees everything on this pane's screen. They cannot type and "
                          "cannot reach your agent.");
}

QString expiryText(qint64 seconds)
{
    if (seconds <= 0) return QStringLiteral("expired");
    if (seconds < 90) return QStringLiteral("%1 s").arg(seconds);
    if (seconds < 5400) return QStringLiteral("%1 min").arg((seconds + 30) / 60);
    if (seconds < 172800) return QStringLiteral("%1 h").arg((seconds + 1800) / 3600);
    return QStringLiteral("%1 days").arg((seconds + 43200) / 86400);
}

QString countdown(int secondsLeft)
{
    if (secondsLeft <= 0) return QStringLiteral("0:00");
    return QStringLiteral("%1:%2").arg(secondsLeft / 60).arg(secondsLeft % 60, 2, 10, QLatin1Char('0'));
}

QString usesText(int uses)
{
    if (uses <= 0) return QStringLiteral("spent");
    return uses == 1 ? QStringLiteral("1 use left") : QStringLiteral("%1 uses left").arg(uses);
}

// ---- the pane --------------------------------------------------------------------------------

SharingView::SharingView(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("settingsPane"));
    setFocusPolicy(Qt::StrongFocus);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(10, 8, 10, 8);
    column->setSpacing(6);

    // No title row: the pane's own type band already says "Sharing" above this, and the tab says
    // how many questions are waiting. A second "Sharing" line under the first only took height.
    // The inset is still kept, because the pane chrome's buttons ask for room on the first row
    // whenever the band is not drawn.
    m_inset = new QWidget;
    m_inset->setFixedHeight(0);
    column->addWidget(m_inset);

    m_scroll = new QScrollArea;
    m_scroll->setObjectName(QStringLiteral("settingsPage"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_body = new QWidget;
    m_body->setObjectName(QStringLiteral("settingsPageBody"));
    m_column = new QVBoxLayout(m_body);
    m_column->setContentsMargins(0, 0, 6, 0);
    m_column->setSpacing(8);
    m_scroll->setWidget(m_body);
    column->addWidget(m_scroll, 1);
    build();
}

QString SharingView::paneTitle() const
{
    const int waiting = m_model ? m_model->waiting() : 0;
    if (waiting == 1) return QStringLiteral("Sharing · 1 waiting");
    if (waiting > 1) return QStringLiteral("Sharing · %1 waiting").arg(waiting);
    return QStringLiteral("Sharing");
}

void SharingView::focusView()
{
    // Never Admit: the keyboard lands on Refuse when there is something to refuse, and on the
    // list otherwise. The pane can be opened by a knock arriving, so this matters.
    if (m_firstRefuse) m_firstRefuse->setFocus(Qt::OtherFocusReason);
    else m_scroll->setFocus(Qt::OtherFocusReason);
}

void SharingView::setHeaderRightInset(int pixels)
{
    // With the type band drawn the chrome's buttons sit in the band and this is 0; without one
    // they cover the top right of the pane, so the first row gives way by that much height.
    m_inset->setFixedHeight(pixels > 0 ? 20 : 0);
}

void SharingView::focusPane(const QString &pane)
{
    if (m_pane == pane) return;
    m_pane = pane;
    build();
}

void SharingView::refresh()
{
    build();
    if (onTitleChanged) onTitleChanged();
}

// Only the numbers: a row that has lapsed is taken out of the model by whoever owns the clock
// (RemoteShare's own second), which then asks every pane in every window to rebuild.
void SharingView::tick()
{
    if (!m_model) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const Request &request : m_model->requests()) {
        if (QLabel *clock = m_clocks.value(request.key()))
            clock->setText(QStringLiteral("%1 left").arg(countdown(request.secondsLeft(now))));
    }
}

void SharingView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && onClose) { onClose(); return; }
    QWidget::keyPressEvent(event);
}

QWidget *SharingView::heading(const QString &text)
{
    auto *label = plain(text, "settingsHeading");
    return label;
}

QWidget *SharingView::note(const QString &text)
{
    return plain(text, "settingsRowDetail");
}

QWidget *SharingView::requestRow(const Request &request, bool editorAllowed)
{
    QFrame *frame = card();
    auto *column = qobject_cast<QVBoxLayout *>(frame->layout());
    const QString who = request.name.isEmpty() ? QStringLiteral("Someone") : request.name;

    if (request.kind == Request::Kind::Knock) {
        column->addWidget(plain(QStringLiteral("%1 wants to join this pane").arg(who),
                                "settingsRowLabel"));
        QStringList facts;
        if (!request.platform.isEmpty()) facts << request.platform;
        if (!request.peer.isEmpty()) facts << QStringLiteral("from %1").arg(request.peer);
        if (!request.fingerprint.isEmpty()) facts << QStringLiteral("key %1").arg(request.fingerprint);
        facts << QStringLiteral("invited as %1").arg(request.role);
        column->addWidget(note(facts.join(QStringLiteral(" · "))));
        auto *code = plain(request.code, "settingsRowLabel");
        code->setAlignment(Qt::AlignCenter);
        code->setStyleSheet(QStringLiteral("font-size: 26px; font-weight: 600; letter-spacing: 6px;"));
        column->addWidget(code);
        column->addWidget(note(QStringLiteral(
            "Admit them only if their screen shows this same code. Anyone who saw the link can "
            "knock; the code is what says it is the person you sent it to.")));
    } else if (request.kind == Request::Kind::Control) {
        column->addWidget(plain(QStringLiteral("%1 asks to type in this pane").arg(who),
                                "settingsRowLabel"));
        column->addWidget(note(QStringLiteral(
            "While they hold it their keys go straight to this terminal, as yours do. Typing in "
            "the pane yourself takes it back at once.")));
    } else {
        column->addWidget(plain(QStringLiteral("%1 wrote a prompt for this pane's agent").arg(who),
                                "settingsRowLabel"));
        // The whole text, wrapped and selectable, never elided: approving this row is approving
        // exactly these words, so the owner has to be able to read all of them.
        auto *text = plain(request.text, "settingsRowLabel");
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        text->setStyleSheet(QStringLiteral("padding: 4px 0;"));
        column->addWidget(text);
        column->addWidget(note(
            request.plan.isEmpty()
                ? QStringLiteral("Approving runs it on your API key, in this pane's folder, with "
                                 "this pane's tools. It is approved once: there is no \"approve "
                                 "always\" while an invite is a bare link.")
                : QStringLiteral("This asks to run a plan, which is why it is a prompt and not a "
                                 "button. Approving runs it on your API key, in this pane's "
                                 "folder, with this pane's tools, once.")));
    }

    auto *buttons = new FlowRow;
    QPushButton *refuse = button(QStringLiteral("Refuse"),
                                 QStringLiteral("Turn this down. Nothing happens and they are told."));
    buttons->add(refuse);
    if (!m_firstRefuse) m_firstRefuse = refuse;
    const QString id = request.id, pane = request.pane;
    if (request.kind == Request::Kind::Knock) {
        QObject::connect(refuse, &QPushButton::clicked, this, [this, id] {
            if (onKnockAnswer) onKnockAnswer(id, false, QStringLiteral("viewer"));
            if (m_model) m_model->dropRequest(Request::Kind::Knock, id);
            refresh();
        });
        auto *asViewer = button(QStringLiteral("Admit as viewer"), roleSentence(QStringLiteral("viewer")));
        QObject::connect(asViewer, &QPushButton::clicked, this, [this, id] {
            if (onKnockAnswer) onKnockAnswer(id, true, QStringLiteral("viewer"));
            if (m_model) m_model->dropRequest(Request::Kind::Knock, id);
            refresh();
        });
        buttons->add(asViewer);
        // Admitting may lower an invite's role and never raise it (section 10.5), so a viewer-only
        // invite simply has no editor button — rather than one that the hub would refuse.
        if (editorAllowed) {
            auto *asEditor = button(QStringLiteral("Admit as editor"), roleSentence(QStringLiteral("editor")));
            QObject::connect(asEditor, &QPushButton::clicked, this, [this, id] {
                if (onKnockAnswer) onKnockAnswer(id, true, QStringLiteral("editor"));
                if (m_model) m_model->dropRequest(Request::Kind::Knock, id);
                refresh();
            });
            buttons->add(asEditor);
        }
    } else if (request.kind == Request::Kind::Control) {
        QObject::connect(refuse, &QPushButton::clicked, this, [this, id, pane] {
            if (onControlAnswer) onControlAnswer(pane, id, false);
            if (m_model) m_model->dropRequest(Request::Kind::Control, id);
            refresh();
        });
        auto *grant = button(QStringLiteral("Let them type"),
                             QStringLiteral("Hand this pane's keyboard over until you take it back."));
        QObject::connect(grant, &QPushButton::clicked, this, [this, id, pane] {
            if (onControlAnswer) onControlAnswer(pane, id, true);
            if (m_model) m_model->dropRequest(Request::Kind::Control, id);
            refresh();
        });
        buttons->add(grant);
    } else {
        QObject::connect(refuse, &QPushButton::clicked, this, [this, id] {
            if (onPromptAnswer) onPromptAnswer(id, false);
            if (m_model) m_model->dropRequest(Request::Kind::Prompt, id);
            refresh();
        });
        auto *approve = button(QStringLiteral("Approve once"),
                               QStringLiteral("Send exactly this text to this pane's agent, now."));
        QObject::connect(approve, &QPushButton::clicked, this, [this, id] {
            if (onPromptAnswer) onPromptAnswer(id, true);
            if (m_model) m_model->dropRequest(Request::Kind::Prompt, id);
            refresh();
        });
        buttons->add(approve);
    }
    auto *clock = plain(QStringLiteral("%1 left")
                            .arg(countdown(request.secondsLeft(QDateTime::currentMSecsSinceEpoch()))),
                        "settingsRowDetail");
    clock->setWordWrap(false);
    clock->setToolTip(QStringLiteral("Unanswered, it lapses by itself and they are told."));
    m_clocks.insert(request.key(), clock);
    buttons->add(clock);
    column->addWidget(buttons);
    return frame;
}

QWidget *SharingView::participantRow(const Participant &person, const QString &pane)
{
    QFrame *frame = card();
    frame->setProperty("current", false);
    auto *column = qobject_cast<QVBoxLayout *>(frame->layout());
    const QString driving = person.driving ? QStringLiteral(" · typing in this pane right now")
                                           : QString();
    column->addWidget(plain(QStringLiteral("%1 — %2%3").arg(person.name, person.role, driving),
                            "settingsRowLabel"));
    QStringList facts;
    if (!person.platform.isEmpty()) facts << person.platform;
    if (!person.online) facts << QStringLiteral("not connected right now");
    if (!person.fingerprint.isEmpty()) facts << QStringLiteral("key %1").arg(person.fingerprint);
    facts << QStringLiteral("access ends in %1").arg(expiryText(person.expires));
    column->addWidget(note(facts.join(QStringLiteral(" · "))));
    column->addWidget(note(roleSentence(person.role)));

    auto *buttons = new FlowRow;
    const bool editor = person.role == QLatin1String("editor");
    const QString id = person.id;
    auto *swap = button(editor ? QStringLiteral("Make viewer") : QStringLiteral("Make editor"),
                        roleSentence(editor ? QStringLiteral("viewer") : QStringLiteral("editor")));
    QObject::connect(swap, &QPushButton::clicked, this, [this, id, editor] {
        if (onRoleSet) onRoleSet(id, editor ? QStringLiteral("viewer") : QStringLiteral("editor"));
    });
    buttons->add(swap);
    if (person.driving) {
        auto *take = button(QStringLiteral("Take the keyboard back"),
                            QStringLiteral("This pane's keys are yours again at once."));
        QObject::connect(take, &QPushButton::clicked, this, [this, pane] {
            if (onControlAnswer) onControlAnswer(pane, QString(), false);
        });
        buttons->add(take);
    }
    auto *remove = button(QStringLiteral("Remove"),
                          QStringLiteral("End their access now and burn the invite they came in on."));
    QObject::connect(remove, &QPushButton::clicked, this, [this, id] {
        if (onRemove) onRemove(id);
        if (m_model) m_model->dropParticipant(id);
        refresh();
    });
    buttons->add(remove);
    column->addWidget(buttons);
    return frame;
}

QWidget *SharingView::inviteRow(const Invite &invite)
{
    QFrame *frame = card();
    frame->setProperty("current", false);
    auto *column = qobject_cast<QVBoxLayout *>(frame->layout());
    // The first characters of the invite's own id: two links made a minute apart are otherwise
    // the same sentence twice, and Revoke has to be aimed at one of them.
    column->addWidget(plain(QStringLiteral("%1 link %2 — %3, expires in %4")
                                .arg(invite.role == QLatin1String("editor") ? QStringLiteral("Editor")
                                                                            : QStringLiteral("Viewer"),
                                     invite.id.left(6), usesText(invite.uses),
                                     expiryText(invite.expires)),
                            "settingsRowLabel"));
    column->addWidget(note(roleSentence(invite.role)));
    auto *buttons = new FlowRow;
    const QString id = invite.id;
    auto *revoke = button(QStringLiteral("Revoke"),
                          QStringLiteral("The link stops working. Anybody already admitted through "
                                         "it stays until you remove them."));
    QObject::connect(revoke, &QPushButton::clicked, this, [this, id] {
        if (onRevokeInvite) onRevokeInvite(id);
    });
    buttons->add(revoke);
    column->addWidget(buttons);
    return frame;
}

QWidget *SharingView::shareControls(const QString &pane)
{
    QFrame *frame = card();
    frame->setProperty("current", false);
    auto *column = qobject_cast<QVBoxLayout *>(frame->layout());
    const ShareOptions options = m_model ? m_model->options(pane) : ShareOptions();

    auto *buttons = new FlowRow;
    auto *invite = button(QStringLiteral("Invite someone…"),
                          QStringLiteral("A link and a QR for one more person on this pane."));
    QObject::connect(invite, &QPushButton::clicked, this, [this, pane] { if (onInvite) onInvite(pane); });
    buttons->add(invite);
    auto *pause = button(options.paused ? QStringLiteral("Let guests act again")
                                        : QStringLiteral("Pause guests"),
                         QStringLiteral("Paused, guests keep seeing the screen but nothing they "
                                        "type or send is accepted."));
    QObject::connect(pause, &QPushButton::clicked, this, [this, pane, options] {
        if (onPause) onPause(pane, !options.paused);
    });
    buttons->add(pause);
    auto *end = button(QStringLiteral("End sharing"),
                       QStringLiteral("Disconnects everyone, burns this pane's invite links and "
                                      "forgets the guests."));
    QObject::connect(end, &QPushButton::clicked, this, [this, pane] { if (onEndShare) onEndShare(pane); });
    buttons->add(end);
    column->addWidget(buttons);
    if (options.paused) {
        const QString why = m_model ? m_model->pauseReason(pane) : QString();
        column->addWidget(note(why == QLatin1String("away")
            ? QStringLiteral("Guests are paused because you are not here: “guests can act only "
                             "while I'm here” is on and Relay is not the window you are looking at.")
            : QStringLiteral("Guests are paused. They still see this pane's screen; nothing they "
                             "type or send is accepted.")));
    }

    auto addOption = [&](const QString &label, const QString &detail, bool on,
                         std::function<void(bool)> set) {
        auto *box = new QCheckBox(label);
        box->setChecked(on);
        QObject::connect(box, &QCheckBox::toggled, this, [set](bool checked) { set(checked); });
        column->addWidget(box);
        column->addWidget(note(detail));
    };
    addOption(QStringLiteral("Guest prompts run immediately"),
              QStringLiteral("Off: every prompt an editor writes waits here for you. On: it goes "
                             "straight to this pane's agent, on your key, unread."),
              options.promptsImmediate, [this, pane, options](bool on) {
                  if (onOptions) onOptions(pane, on, options.presentOnly);
              });
    addOption(QStringLiteral("Guests can act only while I'm here"),
              QStringLiteral("On: whenever Relay's window is not the one you are looking at, "
                             "guests are paused, as though you had pressed Pause."),
              options.presentOnly, [this, pane, options](bool on) {
                  if (onOptions) onOptions(pane, options.promptsImmediate, on);
              });
    return frame;
}

void SharingView::build()
{
    m_clocks.clear();
    m_firstRefuse = nullptr;
    // Rebuilt wholesale: the rows are few, and every one of them is a question whose buttons must
    // match the row's current state exactly. deleteLater would leave the old buttons live for a
    // turn of the loop, so they go now.
    while (QLayoutItem *item = m_column->takeAt(0)) {
        if (QWidget *widget = item->widget()) delete widget;
        delete item;
    }

    QList<SharedPane> panes = m_model ? m_model->sharedPanes() : QList<SharedPane>();
    if (!m_pane.isEmpty()) {
        for (int index = 0; index < panes.size(); ++index)
            if (panes.at(index).id == m_pane) { panes.move(index, 0); break; }
    }

    if (panes.isEmpty()) {
        m_column->addWidget(plain(QStringLiteral("Nothing is shared right now."), "settingsRowLabel"));
        m_column->addWidget(note(QStringLiteral(
            "The share button under a pane's prompt box pairs your own phone, and offers a link "
            "you can send to somebody else. Whoever is here, whoever is knocking and whatever is "
            "waiting for you shows up on this pane.")));
        m_column->addStretch(1);
        if (onTitleChanged) onTitleChanged();
        return;
    }

    for (const SharedPane &pane : std::as_const(panes)) {
        m_column->addWidget(heading(QStringLiteral("Pane “%1”").arg(m_model->paneTitle(pane.id))));
        const QList<Request> waiting = m_model->requests(pane.id);
        if (!waiting.isEmpty()) {
            m_column->addWidget(heading(QStringLiteral("Waiting for you")));
            for (const Request &request : waiting) {
                // The editor button appears only when the invite they used already says editor.
                bool editorAllowed = request.role == QLatin1String("editor");
                m_column->addWidget(requestRow(request, editorAllowed));
            }
        }
        const QList<Participant> people = m_model->participantsOn(pane.id);
        m_column->addWidget(heading(people.isEmpty() ? QStringLiteral("Nobody here yet")
                                                     : QStringLiteral("Here now")));
        if (people.isEmpty())
            m_column->addWidget(note(QStringLiteral(
                "Your own paired phones are not guests and are not listed here; the share window "
                "lists those.")));
        for (const Participant &person : people) m_column->addWidget(participantRow(person, pane.id));

        const QList<Invite> invites = m_model->invitesOn(pane.id);
        if (!invites.isEmpty()) {
            m_column->addWidget(heading(QStringLiteral("Live invite links")));
            for (const Invite &invite : invites) m_column->addWidget(inviteRow(invite));
        }
        m_column->addWidget(heading(QStringLiteral("This share")));
        m_column->addWidget(shareControls(pane.id));
    }
    m_column->addStretch(1);
    for (QWidget *child : m_body->findChildren<QWidget *>()) {
        child->style()->unpolish(child);
        child->style()->polish(child);
    }
}

}  // namespace relay::sharing
