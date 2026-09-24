// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SharingPane.h"
#include "CopyOnSelect.h"
#include "PaneTabNavigation.h"
#include "RemoteSettings.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
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
        for (const QJsonValue &pane : item.value(QStringLiteral("viewing")).toArray())
            person.viewingPanes << pane.toString();
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

void Model::setControl(const QString &pane, const QString &holder, const QString &name,
                       const QString &device, const QString &deviceName)
{
    m_holder.insert(pane, holder);
    m_holderName.insert(pane, name);
    // Only an "owner" holder can be a device of the owner's, and the hub sends an empty `device`
    // for the desktop itself — so this clears as soon as the pane comes back to the keyboard.
    if (device.isEmpty()) {
        m_holderDevice.remove(pane);
        m_holderDeviceName.remove(pane);
    } else {
        m_holderDevice.insert(pane, device);
        m_holderDeviceName.insert(pane, deviceName);
    }
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

void Model::setRemote(bool on, const QString &addressLabel, bool online, int connected,
                      const QString &reason)
{
    m_remoteOn = on;
    m_remoteAddress = addressLabel;
    m_remoteOnline = online;
    m_remoteConnected = connected;
    m_remoteReason = reason;
}

void Model::setDevices(const QList<Device> &devices)
{
    m_devices = devices;
}

void Model::setDevices(const QJsonArray &items)
{
    QList<Device> devices;
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        Device device;
        device.id = item.value(QStringLiteral("id")).toString();
        device.name = item.value(QStringLiteral("name")).toString();
        device.platform = item.value(QStringLiteral("platform")).toString();
        device.online = item.value(QStringLiteral("online")).toBool();
        for (const QJsonValue &pane : item.value(QStringLiteral("panes")).toArray())
            device.panes << pane.toString();
        // "full" or "view", and whether it may answer a password prompt (section 6.7): what the
        // Devices page says beside each one (#SMDX). An older sidecar sends neither; a record
        // without a capability is read as the full one, which is what pairing granted then.
        device.capability = item.value(QStringLiteral("capability")).toString();
        if (device.capability.isEmpty()) device.capability = QStringLiteral("full");
        device.passwords = item.value(QStringLiteral("password_entry")).toBool();
        devices.append(device);
    }
    m_devices = devices;
}

QStringList Model::connectedDeviceNames() const
{
    QStringList names;
    for (const Device &device : m_devices) {
        if (!device.online) continue;
        const QString name = device.name.isEmpty()
                                 ? (device.platform.isEmpty() ? device.id : device.platform)
                                 : device.name;
        if (!name.isEmpty() && !names.contains(name)) names << name;
    }
    return names;
}

QString Model::topLine() const
{
    if (!m_remoteOn) return QStringLiteral("Remote control off");
    QString line = QStringLiteral("Remote control on");
    if (!m_remoteAddress.isEmpty()) line += QStringLiteral(" · ") + m_remoteAddress;
    if (!m_remoteOnline) {
        return line + (m_remoteReason.isEmpty() ? QStringLiteral(" · offline")
                                                : QStringLiteral(" · offline: %1").arg(m_remoteReason));
    }
    const QStringList names = connectedDeviceNames();
    if (!names.isEmpty())
        return line + QStringLiteral(" · %1 connected").arg(names.join(QStringLiteral(", ")));
    // A sidecar from before the `online` flag only says how many (#SHRP).
    if (m_remoteConnected == 1) return line + QStringLiteral(" · 1 phone connected");
    if (m_remoteConnected > 1)
        return line + QStringLiteral(" · %1 phones connected").arg(m_remoteConnected);
    return line + QStringLiteral(" · no phone connected");
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

QString Model::deviceDriverOn(const QString &pane) const
{
    const QString device = m_holderDevice.value(pane);
    if (device.isEmpty()) return QString();
    const QString name = m_holderDeviceName.value(pane);
    return name.isEmpty() ? device : name;
}

int Model::guestsOn(const QString &pane) const
{
    return int(participantsOn(pane).size());
}

ChipState Model::chip(const QString &pane, bool phone) const
{
    ChipState state;
    if (!phone) return state;
    QList<Participant> viewers;
    for (const Participant &person : participantsOn(pane))
        if (person.online && person.viewingPanes.contains(pane)) viewers << person;
    QStringList devices;
    for (const Device &device : m_devices)
        if (device.online && device.panes.contains(pane))
            devices << (device.name.isEmpty() ? device.id : device.name);
    state.visible = !viewers.isEmpty() || !devices.isEmpty();
    if (!state.visible) return state;
    const int guests = viewers.size();
    QString driver = driverOn(pane);
    if (!driver.isEmpty()) {
        bool online = false;
        for (const Participant &person : viewers)
            if (person.driving) online = true;
        if (!online) driver.clear();
    }
    if (!driver.isEmpty()) {
        state.guestDriving = true;
        state.text = QStringLiteral("%1 is typing").arg(driver);
        state.tooltip = QStringLiteral(
            "%1 holds this pane's keyboard, so their keys reach this terminal.\n"
            "Type here and you take it straight back.").arg(driver);
        return state;
    }
    // One of the owner's own devices took over. It is still the owner driving (section 10.3), so
    // this is not `guestDriving` — but the header has to say it, or the pane looks idle while a
    // phone types into it.
    const QString device = deviceDriverOn(pane);
    if (!device.isEmpty() && devices.contains(device)) {
        state.text = QStringLiteral("%1 is typing").arg(device);
        state.tooltip = QStringLiteral(
            "%1 holds this pane's keyboard, so what you type there reaches this terminal.\n"
            "Type here and you take it straight back.").arg(device);
        return state;
    }
    if (guests == 0) {
        state.tooltip = QStringLiteral("Viewed on %1.").arg(devices.join(QStringLiteral(", ")));
        return state;
    }
    state.text = guests == 1 ? QStringLiteral("1 guest") : QStringLiteral("%1 guests").arg(guests);
    QStringList names;
    for (const Participant &person : viewers)
        names << QStringLiteral("%1 (%2)").arg(person.name, person.role);
    names << devices;
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

QString promptsImmediateSentence()
{
    return QStringLiteral("Guest prompts run immediately — off, every prompt an editor writes "
                          "waits here for you; on, it goes straight to that pane's agent, on "
                          "your key, unread.");
}

QString presentOnlySentence()
{
    return QStringLiteral("Guests can act only while I'm here — on, whenever Relay's window is "
                          "not the one you are looking at, guests are paused, as though you had "
                          "pressed Pause.");
}

// ---- the pane --------------------------------------------------------------------------------

namespace {

// The sentence under the invite row before any code exists. The expiry and uses boxes belong to
// the link; a code that quietly ignored them would last longer or admit more than it seemed to.
QString codeIntro()
{
    return QStringLiteral(
        "Make a code to read out instead of sending a link. A code always lasts 10 minutes and "
        "lets in one person, whatever the expiry and uses above say; it grants the role picked "
        "here.");
}

// What the top of the pairing card says before the QR has an expiry to quote. The typed code is
// named first (#FR1C): scanning on an iPhone opens Safari, which pairs a browser tab rather than
// the Home Screen app the notifications go to.
QString pairingIntro()
{
    return QStringLiteral("Type the code beside the QR on your phone, or scan the QR with its "
                          "camera.");
}

// Large, fixed-width and letter-spaced: every character here is going to be typed on a phone or
// read out to somebody, and an ambiguous glyph is a code that does not work. One face for the
// pairing code and the meeting code.
QFont bigCodeFont()
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(30);
    font.setWeight(QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 6);
    return font;
}

QScrollArea *pageScroll(QWidget **body, QVBoxLayout **column)
{
    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("settingsPage"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    *body = new QWidget;
    (*body)->setObjectName(QStringLiteral("settingsPageBody"));
    *column = new QVBoxLayout(*body);
    (*column)->setContentsMargins(0, 0, 6, 0);
    (*column)->setSpacing(8);
    scroll->setWidget(*body);
    return scroll;
}

// The scope as the invite sentence names it: "pane “build”", "tab “thesis”", "every pane".
QString scopePlace(const Scope &scope)
{
    switch (scope.kind) {
    case Scope::Kind::All: return QStringLiteral("every pane");
    case Scope::Kind::Tab:
        return QStringLiteral("tab “%1”").arg(scope.title.isEmpty() ? scope.id : scope.title);
    case Scope::Kind::Pane: break;
    }
    return QStringLiteral("pane “%1”").arg(scope.title.isEmpty() ? scope.id : scope.title);
}

constexpr int kDevicesTab = 0;
constexpr int kPeopleTab = 1;

}  // namespace

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

    // Two pages under one tab bar, built the way the Options pane builds its own (#SMDX): Devices
    // is the owner on other screens, People is everybody else. Tab and Shift+Tab walk the bar
    // from anywhere on the page (PaneTabNavigation.h), so the bar itself takes no focus.
    m_tabs = new QTabBar(this);
    relay::paneTabs::registerTabs(this, m_tabs);
    m_tabs->setObjectName(QStringLiteral("settingsTabs"));
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(false);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setElideMode(Qt::ElideNone);
    m_tabs->setFocusPolicy(Qt::NoFocus);
    m_tabs->addTab(QStringLiteral("Devices"));
    m_tabs->addTab(QStringLiteral("People"));
    column->addWidget(m_tabs);

    m_pages = new QStackedWidget;
    column->addWidget(m_pages, 1);

    m_devicesScroll = pageScroll(&m_devicesBody, &m_devicesColumn);
    m_pages->addWidget(m_devicesScroll);
    m_scroll = pageScroll(&m_body, &m_column);
    m_pages->addWidget(m_scroll);

    QObject::connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (index < 0) return;
        m_pages->setCurrentIndex(index);
        // Leaving Devices withdraws a live offer: a code left live would pair a phone while
        // nobody is looking at the five digits it shows.
        if (index != kDevicesTab) stopPairing();
    });

    build();
    // People first: what is waiting for you is the reason this pane opens by itself.
    m_tabs->setCurrentIndex(kPeopleTab);
    m_pages->setCurrentIndex(kPeopleTab);
}

SharingView::~SharingView()
{
    stopPairing();
}

// ---- pages -----------------------------------------------------------------------------------

void SharingView::showPage(Page page)
{
    m_tabs->setCurrentIndex(page == Page::Devices ? kDevicesTab : kPeopleTab);
}

SharingView::Page SharingView::page() const
{
    return m_tabs->currentIndex() == kDevicesTab ? Page::Devices : Page::People;
}

void SharingView::refreshTabs()
{
    const int waiting = m_model ? m_model->waiting() : 0;
    m_tabs->setTabText(kPeopleTab, waiting > 0 ? QStringLiteral("People · %1 waiting").arg(waiting)
                                               : QStringLiteral("People"));
    m_tabs->setTabText(kDevicesTab, m_askId >= 0 ? QStringLiteral("Devices · 1 asking")
                                                 : QStringLiteral("Devices"));
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
    // Never Admit, never Allow: the keyboard lands on Refuse when there is something to refuse,
    // and on the page otherwise. The pane can be opened by a knock or an ask arriving, so this
    // matters.
    if (page() == Page::Devices) {
        if (m_askId >= 0 && m_askRefuse) m_askRefuse->setFocus(Qt::OtherFocusReason);
        else m_devicesScroll->setFocus(Qt::OtherFocusReason);
        return;
    }
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
    rebuildPeople();
}

void SharingView::refresh()
{
    rebuildPeople();
    refreshDevices();
    refreshTabs();
    if (onTitleChanged) onTitleChanged();
}

// Only the numbers: a row that has lapsed is taken out of the model by whoever owns the clock
// (RemoteShare's own second), which then asks every pane in every window to rebuild. The two
// codes' clocks move here too (#SMDX): they were the dialog's, and it heard the same second.
void SharingView::tick()
{
    if (m_model) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (const Request &request : m_model->requests()) {
            if (QLabel *clock = m_clocks.value(request.key()))
                clock->setText(QStringLiteral("%1 left").arg(countdown(request.secondsLeft(now))));
        }
    }
    refreshPairCode();
    codeTick();
}

void SharingView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && onClose) { onClose(); return; }
    QWidget::keyPressEvent(event);
}

QWidget *SharingView::heading(const QString &text)
{
    return plain(text, "settingsHeading");
}

QWidget *SharingView::subheading(const QString &text)
{
    return plain(text, "settingsSubheading");
}

QWidget *SharingView::note(const QString &text)
{
    return plain(text, "settingsRowDetail");
}

// Takes every row out of a column. The old rows leave the layout and the screen now, but are freed
// on the next turn of the loop: a rebuild is very often *caused* by one of them — a checkbox's
// `toggled` reaches the model, the model's change comes straight back here — and
// QCheckBox::setChecked still touches the box (accessibility) after `toggled` returns. Deleting it
// here was a SIGSEGV on "Guest prompts run immediately" (#SHCK). Hidden and parentless, an old row
// can take no click in the meantime. If the keyboard was on one of them it goes to `fallback`
// first, so hiding the row cannot hand it to the next button in the chain — which may be Admit.
void SharingView::clearColumn(QVBoxLayout *column, QWidget *fallback)
{
    QWidget *focused = QApplication::focusWidget();
    while (QLayoutItem *item = column->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            if (focused && fallback && (widget == focused || widget->isAncestorOf(focused))) {
                fallback->setFocus(Qt::OtherFocusReason);
                focused = nullptr;
            }
            widget->hide();
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }
}

void SharingView::build()
{
    buildDevicesPage();
    buildPeoplePage();
    refresh();
}

// ---- the Devices page ------------------------------------------------------------------------

void SharingView::buildDevicesPage()
{
    QVBoxLayout *column = m_devicesColumn;

    // The status line, and the switch beside it. The line is the model's (#SHRP): the window
    // feeds it and this page only prints it.
    auto *top = new QWidget;
    auto *topRow = new QHBoxLayout(top);
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(8);
    m_topLine = plain(QStringLiteral("Remote control off"), "settingsRowLabel");
    m_topLine->setObjectName(QStringLiteral("sharingTopLine"));
    topRow->addWidget(m_topLine, 1);
    m_remoteSwitch = new QCheckBox(QStringLiteral("Remote control"));
    m_remoteSwitch->setObjectName(QStringLiteral("sharingRemoteSwitch"));
    m_remoteSwitch->setToolTip(QStringLiteral(
        "On, this desktop stays reachable from your paired phones and other computers at the "
        "address below, across restarts. Off, they see nothing."));
    QObject::connect(m_remoteSwitch, &QCheckBox::clicked, this, [this](bool on) {
        if (onRemoteSwitch) onRemoteSwitch(on);
    });
    topRow->addWidget(m_remoteSwitch, 0, Qt::AlignTop);
    column->addWidget(top);

    // One line, because pairing turns remote control on by itself (#FR1C) and a switch that
    // turned itself on must say so where it happened — and say where to turn it off.
    m_alwaysOnLine = plain(relay::remotesettings::pairAlwaysOnLine(), "settingsRowDetail");
    m_alwaysOnLine->hide();
    column->addWidget(m_alwaysOnLine);

    // In reading order: a device asking to be paired comes first, because it is the one thing
    // here with a clock on it; then the offer ("Add a device…", or the QR and the code while an
    // offer is live) with the address it is made for under it; and the devices already paired
    // last, because they need nothing from you. The first screenshots of this page had the ask
    // and the offer below the device list, out of view (#SMDX evidence).
    // ----- the approval card: what a phone claims to be, and the code that proves it is the
    // phone in your hand rather than whoever else saw the QR code -------------------------------
    m_askCard = card();
    auto *askColumn = qobject_cast<QVBoxLayout *>(m_askCard->layout());
    m_askText = plain(QString(), "settingsRowLabel");
    askColumn->addWidget(m_askText);
    m_askCode = plain(QString(), "settingsRowLabel");
    m_askCode->setObjectName(QStringLiteral("sharingAskCode"));
    m_askCode->setAlignment(Qt::AlignCenter);
    // In points, like every other size in the app (docs/ARCHITECTURE.md, "Legible text"): a pixel
    // size ignores the desktop's font scaling. 21pt is the 28px this used to be at 96 dpi.
    m_askCode->setStyleSheet(QStringLiteral("font-size: 21pt; font-weight: 600; letter-spacing: 6px;"));
    askColumn->addWidget(m_askCode);
    auto *askRow = new FlowRow;
    // Refuse is the default and holds the focus. Allowing is a deliberate click — never a stray
    // Return on a card that just appeared while the person was typing somewhere else.
    m_askRefuse = button(QStringLiteral("Refuse"),
                         QStringLiteral("Turn this down. Nothing happens and the device is told."));
    askRow->add(m_askRefuse);
    // Watching and typing are separate grants, because they are very different things to hand
    // out: one shows a device everything on the screen, the other gives it the keyboard of a
    // live shell. The protocol already enforces the difference on every message.
    auto *allowView = button(QStringLiteral("Allow viewing"),
                             QStringLiteral("The device can watch this pane and read its history. "
                                            "It cannot type."));
    askRow->add(allowView);
    auto *allowType = button(QStringLiteral("Allow typing"),
                             QStringLiteral("The device can watch and, after taking over, run "
                                            "anything you could."));
    askRow->add(allowType);
    askColumn->addWidget(askRow);
    QObject::connect(m_askRefuse, &QPushButton::clicked, this, [this] { answerAsk(false, QString()); });
    QObject::connect(allowView, &QPushButton::clicked, this,
                     [this] { answerAsk(true, QStringLiteral("view")); });
    QObject::connect(allowType, &QPushButton::clicked, this,
                     [this] { answerAsk(true, QStringLiteral("full")); });
    m_askCard->hide();
    column->addWidget(m_askCard);
    m_askResult = plain(QString(), "settingsRowLabel");
    m_askResult->setObjectName(QStringLiteral("sharingAskResult"));
    m_askResult->hide();
    column->addWidget(m_askResult);

    m_addDevice = button(QStringLiteral("Add a device…"),
                         QStringLiteral("A code to type on your phone, or a QR to scan. It sees "
                                        "every pane and can type into any of them; it is you, not "
                                        "a guest."));
    QObject::connect(m_addDevice, &QPushButton::clicked, this, [this] { startPairing(); });
    column->addWidget(m_addDevice, 0, Qt::AlignLeft);

    // ----- the pairing card: the QR, the code to type and Copy link (#FR1C) ---------------------
    m_pairCard = card();
    auto *pairColumn = qobject_cast<QVBoxLayout *>(m_pairCard->layout());
    m_pairStatus = plain(QStringLiteral("Starting…"), "settingsRowLabel");
    m_pairStatus->setObjectName(QStringLiteral("sharingPairStatus"));
    pairColumn->addWidget(m_pairStatus);

    auto *pairRow = new QHBoxLayout;
    pairRow->setSpacing(16);
    // A QR code must never be squeezed or overlapped: a phone cannot read a partial one. The
    // label has a fixed size and the card grows to fit whatever else it has to say.
    m_qr = new QLabel;
    m_qr->setAlignment(Qt::AlignCenter);
    m_qr->setFixedSize(260, 260);
    pairRow->addWidget(m_qr, 0, Qt::AlignTop);

    // The code to type, beside the QR. Scanning the QR on an iPhone opens Safari, which pairs a
    // browser tab that gets no push and is not the Home Screen app; typing four letters and four
    // digits pairs whichever Relay is in the person's hand. Minted when the card opens and
    // withdrawn when it closes.
    auto *pairBox = new QWidget;
    auto *codeColumn = new QVBoxLayout(pairBox);
    codeColumn->setContentsMargins(0, 0, 0, 0);
    codeColumn->setSpacing(4);
    m_pairHeading = plain(relay::remotesettings::pairCodeHeading(), "settingsRowLabel");
    codeColumn->addWidget(m_pairHeading);
    m_pairValue = new QLabel;
    m_pairValue->setObjectName(QStringLiteral("sharingPairCode"));
    m_pairValue->setFont(bigCodeFont());
    m_pairValue->setTextFormat(Qt::PlainText);
    m_pairValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    codeColumn->addWidget(m_pairValue);
    m_pairClock = new QLabel;
    m_pairClock->setTextFormat(Qt::PlainText);
    codeColumn->addWidget(m_pairClock);
    m_pairNote = plain(QString(), "settingsRowDetail");
    codeColumn->addWidget(m_pairNote);
    m_pairAgain = button(QStringLiteral("New code"),
                         QStringLiteral("Withdraws this code and makes another."));
    m_pairAgain->hide();
    QObject::connect(m_pairAgain, &QPushButton::clicked, this, [this] { askPairCode(); });
    codeColumn->addWidget(m_pairAgain, 0, Qt::AlignLeft);
    codeColumn->addStretch(1);
    pairRow->addWidget(pairBox, 1);
    pairColumn->addLayout(pairRow);

    // Only the address, not the link: the full link is in the QR already, and its fragment is the
    // one-time secret — no reason to also print it on the page. Copy is how it reaches a phone
    // that cannot scan, on a desktop with no other way to send 140 characters (#FR1C).
    auto *urlRow = new QHBoxLayout;
    m_pairUrl = plain(QString(), "settingsRowDetail");
    m_pairUrl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    relay::installCopyOnSelect(m_pairUrl);
    urlRow->addWidget(m_pairUrl, 1);
    m_pairCopy = button(QStringLiteral("Copy link"),
                        QStringLiteral("The whole pairing link, secret and all. Send it to your own "
                                       "phone and nowhere else: whoever opens it first is the "
                                       "device that gets paired."));
    m_pairCopy->setEnabled(false);
    QObject::connect(m_pairCopy, &QPushButton::clicked, this, [this] {
        if (m_pairingLink.isEmpty()) return;
        QGuiApplication::clipboard()->setText(m_pairingLink);
        m_pairCopy->setText(QStringLiteral("Copied"));
    });
    urlRow->addWidget(m_pairCopy, 0, Qt::AlignTop);
    pairColumn->addLayout(urlRow);

    // The three ways in, one line each: the phone app, another desktop's Relay, any browser.
    pairColumn->addWidget(note(QStringLiteral(
        "On a phone: open Relay and type the code, or scan the QR.")));
    pairColumn->addWidget(note(QStringLiteral(
        "On another computer running Relay: plug menu › Open a pane your other desktop shares…, "
        "and paste the link.")));
    pairColumn->addWidget(note(QStringLiteral("In any browser: open the link.")));

    m_pairDone = button(QStringLiteral("Done"),
                        QStringLiteral("Withdraws the code and the QR. A device already paired "
                                       "stays paired."));
    QObject::connect(m_pairDone, &QPushButton::clicked, this, [this] { stopPairing(); });
    pairColumn->addWidget(m_pairDone, 0, Qt::AlignLeft);
    m_pairCard->hide();
    column->addWidget(m_pairCard);

    // Which address the phone should reach this machine on. Getting it wrong is the most likely
    // reason a phone says it cannot reach the site, so the choice is in front of the QR code.
    m_address = new QComboBox;
    m_address->setObjectName(QStringLiteral("sharingAddressPick"));
    m_address->setToolTip(QStringLiteral(
        "The address your phone will open. The tailnet name comes first when tailscale can serve "
        "it: a real certificate, no warning to accept, and it works from anywhere the phone is "
        "signed in to your tailnet. relay-terminal.ai works from anywhere with no warning; the "
        "links go through it instead of this machine. Use the network address when the phone is "
        "on the same Wi-Fi."));
    m_address->hide();
    QObject::connect(m_address, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        const QString address = m_address->itemData(index).toString();
        if (!address.isEmpty() && onAddressPick) onAddressPick(address);
    });
    column->addWidget(m_address);
    // Why the warning-free address is not on offer, when it is not. An absent entry and an entry
    // that needs one command run once look identical in a list, so the sentence is shown.
    m_addressNote = plain(QString(), "settingsRowDetail");
    m_addressNote->hide();
    column->addWidget(m_addressNote);

    column->addWidget(heading(QStringLiteral("Your devices")));
    m_deviceRows = new QWidget;
    m_deviceColumn = new QVBoxLayout(m_deviceRows);
    m_deviceColumn->setContentsMargins(0, 0, 0, 0);
    m_deviceColumn->setSpacing(6);
    column->addWidget(m_deviceRows);





    column->addStretch(1);
}

QWidget *SharingView::deviceRow(const Device &device)
{
    QFrame *frame = card();
    frame->setProperty("current", false);
    auto *column = qobject_cast<QVBoxLayout *>(frame->layout());
    const QString name = device.name.isEmpty() ? (device.platform.isEmpty() ? device.id
                                                                            : device.platform)
                                               : device.name;
    column->addWidget(plain(device.platform.isEmpty() ? name
                                                      : QStringLiteral("%1 (%2)").arg(name, device.platform),
                            "settingsRowLabel"));
    QStringList facts;
    facts << (device.online ? QStringLiteral("connected") : QStringLiteral("not connected"));
    facts << (device.capability == QLatin1String("view") ? QStringLiteral("watch only")
                                                          : QStringLiteral("watch and type"));
    column->addWidget(note(facts.join(QStringLiteral(" · "))));

    auto *buttons = new FlowRow;
    const QString id = device.id;
    const bool passwords = device.passwords;
    // Password entry is its own grant, off by default, because a password typed on a phone is
    // the one input that can end up somewhere a keystroke must never go (section 6.7).
    auto *password = button(passwords ? QStringLiteral("Passwords: on") : QStringLiteral("Passwords: off"),
                            QStringLiteral("Whether this device may answer a password prompt. Off "
                                           "until you turn it on, and only ever for a pane that is "
                                           "at a prompt right now."));
    QObject::connect(password, &QPushButton::clicked, this, [this, id, passwords] {
        if (onPasswordEntry) onPasswordEntry(id, !passwords);
    });
    buttons->add(password);
    auto *revoke = button(QStringLiteral("Revoke"),
                          QStringLiteral("This device is forgotten and disconnected. Pair it again "
                                         "to let it back in."));
    QObject::connect(revoke, &QPushButton::clicked, this, [this, id] {
        if (onRevokeDevice) onRevokeDevice(id);
    });
    buttons->add(revoke);
    column->addWidget(buttons);
    return frame;
}

void SharingView::refreshDevices()
{
    const bool on = m_model && m_model->remoteOn();
    m_topLine->setText(m_model ? m_model->topLine() : QStringLiteral("Remote control off"));
    {
        const QSignalBlocker quiet(m_remoteSwitch);
        m_remoteSwitch->setChecked(on);
    }
    m_alwaysOnLine->setVisible(on);

    clearColumn(m_deviceColumn, m_devicesScroll);
    const QList<Device> devices = m_model ? m_model->devices() : QList<Device>();
    if (devices.isEmpty()) {
        m_deviceColumn->addWidget(note(QStringLiteral("No phone is paired with this desktop yet.")));
    } else {
        for (const Device &device : devices) m_deviceColumn->addWidget(deviceRow(device));
    }
    for (QWidget *child : m_deviceRows->findChildren<QWidget *>()) {
        child->style()->unpolish(child);
        child->style()->polish(child);
    }
}

void SharingView::setService(const Service &service)
{
    m_service = service;
    refreshPairingService();
}

void SharingView::setAddresses(const QJsonArray &addresses)
{
    // The sidecar sends its best first: the tailnet name behind `tailscale serve`, which the phone
    // opens with no certificate warning at all, then this machine's own addresses behind the
    // self-signed certificate (the LAN one, then the tailnet IP). It also sends the tailnet entry
    // when it cannot be used, carrying one sentence saying why — that is not something to choose,
    // so it goes under the picker rather than into it.
    const QSignalBlocker quiet(m_address);
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
    // A public link admits as many people as the owner picks, the same as any other address
    // (owner, 2026-09-18: one link for a group). What changes is who can reach the door, so the
    // note says that, and says the part that has not changed: each person is admitted by hand.
    if (publicLink) {
        m_inviteNote->setText(QStringLiteral("Over a public link, anyone this link is forwarded to "
                                             "can knock. You admit each person by hand."));
    } else if (m_inviteNote->text().startsWith(QLatin1String("Over a public link"))) {
        m_inviteNote->setText(roleSentence(m_inviteRole->currentData().toString()));
    }
    m_addressNote->setText(reasons.join(QLatin1Char('\n')));
    m_addressNote->setVisible(!reasons.isEmpty());
}

// ----- pairing (#FR1C, #PRM2) -------------------------------------------------------------------

void SharingView::startPairing()
{
    showPage(Page::Devices);
    if (m_pairingLive) return;   // an offer is on screen: pressing again must not mint another
    m_pairingLive = true;
    m_pairingBase.clear();
    m_askResult->hide();
    m_addDevice->hide();
    m_pairStatus->setText(QStringLiteral("Starting…"));
    m_pairCard->show();
    m_devicesScroll->ensureWidgetVisible(m_pairCard);
    refreshPairingService();
}

void SharingView::stopPairing()
{
    // The code goes with the card: one left live would pair a phone while nobody is here to
    // compare the five digits it shows.
    if (m_pairDeadline && !m_pairCode.isEmpty() && onPairCodeRevoke) onPairCodeRevoke(m_pairCode);
    clearPairing();
    m_pairingLive = false;
    m_pairingBase.clear();
    m_pairNote->clear();
    if (m_pairCard) m_pairCard->hide();
    if (m_addDevice) m_addDevice->show();
}

// Everything the card shows about the current offer, back to nothing. The card itself stays where
// it is: this is what happens between one offer and the next, not the end of pairing.
void SharingView::clearPairing()
{
    m_pairingLink.clear();
    m_pairWaiting = false;
    m_pairCode.clear();
    m_pairPin.clear();
    m_pairState.clear();
    m_pairFailures = 0;
    m_pairDeadline = 0;
    if (!m_qr) return;
    m_qr->clear();
    m_pairUrl->clear();
    m_pairCopy->setEnabled(false);
    m_pairCopy->setText(QStringLiteral("Copy link"));
    m_pairValue->clear();
    m_pairClock->clear();
    m_pairAgain->hide();
    m_pairHeading->setText(relay::remotesettings::pairCodeHeading());
}

// The gate on spending a pairing room (#PRM2). Startup announces the local listener before moving
// to the remembered rendezvous: wait for that destination before minting anything, because the
// sidecar has two rooms and a QR for the wrong address is one of them gone. Nothing is minted
// twice for one address: setService() comes through here on every change and finds the base it
// already asked for.
void SharingView::refreshPairingService()
{
    if (!m_pairingLive) return;
    if (m_service.running && m_service.alwaysOn
        && (!m_service.online || m_service.onlineBase != m_service.base)) {
        m_pairStatus->setText(m_service.note.isEmpty() ? QStringLiteral("Starting…") : m_service.note);
        return;
    }
    if (m_service.running && m_pairingBase == m_service.base) return;
    clearPairing();
    m_pairingBase.clear();
    if (!m_service.running) {
        m_pairStatus->setText(QStringLiteral("Remote control is off. These pairing codes have ended."));
        m_pairNote->clear();
        return;
    }
    m_pairingBase = m_service.base;
    m_pairStatus->setText(pairingIntro());
    if (onPairRequest) onPairRequest();
    askPairCode();
}

void SharingView::askPairCode()
{
    if (!m_service.running || !m_pairingLive) return;
    // A code on screen is withdrawn before another is asked for: two live pairing codes are two
    // open doors for one phone.
    if (m_pairDeadline && !m_pairCode.isEmpty() && onPairCodeRevoke) onPairCodeRevoke(m_pairCode);
    m_pairCode.clear();
    m_pairPin.clear();
    m_pairState.clear();
    m_pairFailures = 0;
    m_pairDeadline = 0;
    m_pairWaiting = true;
    m_pairHeading->setText(relay::remotesettings::pairCodeHeading());
    m_pairValue->clear();
    m_pairClock->clear();
    m_pairAgain->hide();
    m_pairNote->setText(QStringLiteral("Making a code…"));
    if (onPairCodeRequest) onPairCodeRequest();
    // A sidecar from before this card ignores `pair_code` rather than refusing it, so waiting is
    // the only way to tell the two apart. After the wait the QR is on its own, and says so.
    QTimer::singleShot(relay::remotesettings::pairCodeWaitMs(), this, [this] { noPairCode(); });
}

void SharingView::showPairing(const QString &url, const QrMatrix &qr, int expires)
{
    if (!m_pairingLive) return;   // another window's offer, or one this pane has already closed
    const QPixmap code = qrPixmap(qr, 260);
    m_qr->setFixedSize(code.size().expandedTo(QSize(1, 1)));
    m_qr->setPixmap(code);
    m_pairUrl->setText(QStringLiteral("Phone connects to %1").arg(url.section(QLatin1Char('/'), 0, 2)));
    m_pairingLink = url;
    m_pairCopy->setEnabled(true);
    m_pairCopy->setText(QStringLiteral("Copy link"));
    QString text = QStringLiteral("%1 The QR lasts %2 s.").arg(pairingIntro()).arg(expires);
    if (m_address->count() > 1) {
        text += QStringLiteral("\nIf your phone says it cannot reach the site, choose the other "
                               "address in the list — the phone has to be on that network.");
    }
    m_pairStatus->setText(text);
}

void SharingView::showPairCode(const QString &code, const QString &pin, int expires)
{
    // A card that is waiting takes the code; one that already has a live one leaves it to the
    // pane that asked. (A late answer, after the wait gave up, is still shown: the code is live
    // on the sidecar whether this card waited for it or not.)
    if (!m_pairingLive) return;
    if (!m_pairWaiting && !m_pairCode.isEmpty()) return;
    m_pairWaiting = false;
    m_pairCode = code;
    m_pairPin = pin;
    m_pairState.clear();
    m_pairFailures = 0;
    m_pairDeadline =
        QDateTime::currentMSecsSinceEpoch() + qint64(expires > 0 ? expires : 600) * 1000;
    m_pairHeading->setText(relay::remotesettings::pairCodeHeading());
    refreshPairCode();
}

void SharingView::showPairCodeState(const QString &code, const QString &state, int failures)
{
    if (code.isEmpty() || code != m_pairCode) return;   // an older code this card no longer shows
    if (m_pairWaiting) return;   // a new code is on its way, and the old one's burn made room
    m_pairState = state;
    m_pairFailures = failures;
    m_pairDeadline = 0;
    refreshPairCode();
}

// The row itself is relay::remotesettings::pairCodeRow, so what the four states say is pinned by
// tests/remotesettings_test.cpp rather than living in a widget nothing headless can read.
void SharingView::refreshPairCode()
{
    if (!m_pairValue || m_pairCode.isEmpty()) return;
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
void SharingView::noPairCode()
{
    if (!m_pairWaiting) return;
    m_pairWaiting = false;
    m_pairHeading->setText(relay::remotesettings::pairCodeUnavailable());
    m_pairValue->clear();
    m_pairClock->clear();
    m_pairNote->clear();
    m_pairAgain->hide();
}

void SharingView::serviceFailed(const QString &message)
{
    if (m_pairingLive) m_pairStatus->setText(message);
    // The sidecar refuses a `pair_code` it cannot carry out (the 429: both rooms taken) with an
    // `error` line; said where the code was promised, and New code comes back.
    if (m_pairWaiting) {
        m_pairWaiting = false;
        m_pairValue->clear();
        m_pairClock->clear();
        m_pairNote->setText(QStringLiteral("No code was made: %1").arg(message));
        m_pairAgain->show();
    }
    // The same for a code_create it could not carry out: said here too, where the person is
    // looking, and the button comes back.
    if (m_codeAskedAt) {
        m_codeAskedAt = 0;
        m_makeCode->setEnabled(true);
        m_codeNote->setText(QStringLiteral("No code was made: %1").arg(message));
    }
}

// ----- a device asking to be paired ---------------------------------------------------------------

void SharingView::showAsk(const DeviceAsk &ask)
{
    if (!ask.valid()) {
        m_askId = -1;
        m_askCard->hide();
        refreshTabs();
        return;
    }
    m_askId = ask.id;
    m_askResult->hide();
    m_askText->setText(QStringLiteral("%1 (%2) at %3 wants access.\nKey %4.\n"
                                      "Allow it only if that device shows this code:")
                           .arg(ask.name, ask.platform, ask.peer, ask.fingerprint));
    m_askCode->setText(ask.code);
    m_askCard->show();
    refreshTabs();
    m_devicesScroll->ensureWidgetVisible(m_askCard);
    // Bring the question forward, with Refuse holding the focus. Focus set while the card was
    // hidden would not stick, so it is set here, the moment there is something to refuse.
    showPage(Page::Devices);
    m_askRefuse->setFocus(Qt::OtherFocusReason);
}

void SharingView::answerAsk(bool allow, const QString &capability)
{
    if (m_askId < 0) return;
    const QString granted = capability.isEmpty() ? QStringLiteral("view") : capability;
    const int id = m_askId;
    m_askId = -1;
    m_askCard->hide();
    if (onPairAnswer) onPairAnswer(id, allow, granted);
    if (!allow) {
        m_askResult->setText(QStringLiteral("Refused."));
    } else if (granted == QLatin1String("full")) {
        m_askResult->setText(QStringLiteral("Paired. That device can watch and type."));
    } else {
        m_askResult->setText(QStringLiteral("Paired for viewing. That device cannot type."));
    }
    m_askResult->show();
    refreshTabs();
    m_devicesScroll->setFocus(Qt::OtherFocusReason);
}

// ---- the People page -------------------------------------------------------------------------

void SharingView::buildPeoplePage()
{
    m_rows = new QWidget;
    m_rowsColumn = new QVBoxLayout(m_rows);
    m_rowsColumn->setContentsMargins(0, 0, 0, 0);
    m_rowsColumn->setSpacing(8);
    m_column->addWidget(m_rows);
    buildInviteForm(m_column);
    m_column->addStretch(1);
}

Scope SharingView::scopeOf(const SharedPane &pane, const QList<Scope> &scopes) const
{
    Scope scope;
    if (pane.tab == QLatin1String("all-tabs")) {
        scope.kind = Scope::Kind::All;
        scope.id = scope.tab = pane.tab;
        scope.title = QStringLiteral("Everything");
    } else if (!pane.tab.isEmpty()) {
        scope.kind = Scope::Kind::Tab;
        scope.id = scope.tab = pane.tab;
        scope.title = pane.tab;
    } else {
        scope.kind = Scope::Kind::Pane;
        scope.id = pane.id;
        scope.title = m_model ? m_model->paneTitle(pane.id) : pane.title;
    }
    // The window's catalogue knows the tab's name and the pane's tab; the model only knows ids.
    for (const Scope &known : scopes) {
        if (known == scope) {
            if (!known.title.isEmpty()) scope.title = known.title;
            scope.tabTitle = known.tabTitle;
            scope.panes = known.panes;
            if (scope.kind == Scope::Kind::Pane) scope.tab = known.tab;
            break;
        }
    }
    return scope;
}

QString SharingView::scopeHeading(const Scope &scope) const
{
    switch (scope.kind) {
    case Scope::Kind::All: return QStringLiteral("Everything");
    case Scope::Kind::Tab: return QStringLiteral("Tab “%1”").arg(scope.title.isEmpty() ? scope.id : scope.title);
    case Scope::Kind::Pane: break;
    }
    return QStringLiteral("Pane “%1”").arg(scope.title.isEmpty() ? scope.id : scope.title);
}

void SharingView::rebuildPeople()
{
    m_clocks.clear();
    m_firstRefuse = nullptr;
    // Rebuilt wholesale: the rows are few, and every one of them is a question whose buttons must
    // match the row's current state exactly. The invite form below the rows is not rebuilt: it
    // holds a link or a code the owner may be reading out.
    clearColumn(m_rowsColumn, m_scroll);

    QList<SharedPane> panes = m_model ? m_model->sharedPanes() : QList<SharedPane>();
    if (!m_pane.isEmpty()) {
        for (int index = 0; index < panes.size(); ++index)
            if (panes.at(index).id == m_pane) { panes.move(index, 0); break; }
    }

    // In reading order: anything waiting for you, then who is visiting which pane, with that
    // pane's controls. A pane nobody is visiting is not a row here (#SMDX): since #PH0N every
    // pane with a screen is published to the owner's own phones, so "shared with nobody" is the
    // ordinary state of every pane, and the Devices page is where the phones are.
    const QList<Request> waiting = m_model ? m_model->requests() : QList<Request>();
    if (!waiting.isEmpty()) {
        m_rowsColumn->addWidget(heading(QStringLiteral("Waiting for you")));
        for (const Request &request : waiting) {
            // The editor button appears only when the invite they used already says editor.
            const bool editorAllowed = request.role == QLatin1String("editor");
            m_rowsColumn->addWidget(requestRow(request, editorAllowed));
        }
    }

    QList<SharedPane> visited;
    for (const SharedPane &pane : std::as_const(panes)) {
        if (!m_model->participantsOn(pane.id).isEmpty() || !m_model->invitesOn(pane.id).isEmpty())
            visited.append(pane);
    }

    if (visited.isEmpty()) {
        m_rowsColumn->addWidget(plain(QStringLiteral("Nobody else is here. Your paired devices see "
                                                     "every pane while remote control is on."),
                                      "settingsRowLabel"));
        auto *invite = button(QStringLiteral("Invite…"),
                              QStringLiteral("A link, a QR or a code for somebody else, on one "
                                             "pane, a whole tab or everything."));
        QObject::connect(invite, &QPushButton::clicked, this, [this] { startInvite(Scope{}); });
        m_rowsColumn->addWidget(invite, 0, Qt::AlignLeft);
    } else {
        m_rowsColumn->addWidget(heading(QStringLiteral("Shared now")));
        // Grouped by scope: the panes of one tab shared whole sit under one "Tab “…”" heading,
        // and every pane under "Everything", because that is the door their guests came through.
        const QList<Scope> scopes = onScopes ? onScopes() : QList<Scope>();
        QList<Scope> order;
        QHash<QString, QList<SharedPane>> groups;
        for (const SharedPane &pane : std::as_const(visited)) {
            const Scope scope = scopeOf(pane, scopes);
            const QString key = QString::number(int(scope.kind)) + QLatin1Char('/') + scope.id;
            if (!groups.contains(key)) order.append(scope);
            groups[key].append(pane);
        }
        for (const Scope &scope : std::as_const(order)) {
            const QString key = QString::number(int(scope.kind)) + QLatin1Char('/') + scope.id;
            const QList<SharedPane> members = groups.value(key);
            m_rowsColumn->addWidget(subheading(scopeHeading(scope)));
            QStringList shownInvites;
            for (const SharedPane &pane : members) {
                // Under a tab or everything, the rows still say which pane each guest is on.
                if (members.size() > 1)
                    m_rowsColumn->addWidget(note(QStringLiteral("Pane “%1”").arg(m_model->paneTitle(pane.id))));
                for (const Participant &person : m_model->participantsOn(pane.id))
                    m_rowsColumn->addWidget(participantRow(person, pane.id));
                for (const Invite &invite : m_model->invitesOn(pane.id)) {
                    // A tab's link lists every pane in it: one row, not one per pane.
                    if (shownInvites.contains(invite.id)) continue;
                    shownInvites << invite.id;
                    m_rowsColumn->addWidget(inviteRow(invite));
                }
                m_rowsColumn->addWidget(shareControls(pane.id, scope));
            }
        }
        auto *shared = note(promptsImmediateSentence() + QLatin1Char(' ') + presentOnlySentence());
        shared->setObjectName(QStringLiteral("sharingOptionsNote"));
        m_rowsColumn->addWidget(shared);
    }
    for (QWidget *child : m_rows->findChildren<QWidget *>()) {
        child->style()->unpolish(child);
        child->style()->polish(child);
    }
}

QWidget *SharingView::requestRow(const Request &request, bool editorAllowed)
{
    QFrame *frame = card();
    auto *column = qobject_cast<QVBoxLayout *>(frame->layout());
    const QString who = request.name.isEmpty() ? QStringLiteral("Someone") : request.name;
    // Every question names its pane: the rows of every pane sit under one "Waiting for you"
    // (#SHRP), so "this pane" would say nothing.
    const QString where = request.pane.isEmpty() || !m_model
                              ? QStringLiteral("a pane")
                              : QStringLiteral("pane “%1”").arg(m_model->paneTitle(request.pane));

    if (request.kind == Request::Kind::Knock) {
        column->addWidget(plain(QStringLiteral("%1 wants to join %2").arg(who, where),
                                "settingsRowLabel"));
        QStringList facts;
        if (!request.platform.isEmpty()) facts << request.platform;
        if (!request.peer.isEmpty()) facts << QStringLiteral("from %1").arg(request.peer);
        if (!request.fingerprint.isEmpty()) facts << QStringLiteral("key %1").arg(request.fingerprint);
        facts << QStringLiteral("invited as %1").arg(request.role);
        column->addWidget(note(facts.join(QStringLiteral(" · "))));
        auto *code = plain(request.code, "settingsRowLabel");
        code->setAlignment(Qt::AlignCenter);
        // Points, not pixels (docs/ARCHITECTURE.md, "Legible text"): 19.5pt is the 26px this used
        // to be at 96 dpi, and it follows the desktop's font scaling as the rest of the app does.
        code->setStyleSheet(QStringLiteral("font-size: 19.5pt; font-weight: 600; letter-spacing: 6px;"));
        column->addWidget(code);
        column->addWidget(note(QStringLiteral(
            "Admit them only if their screen shows this same code. Anyone who saw the link can "
            "knock; the code is what says it is the person you sent it to.")));
    } else if (request.kind == Request::Kind::Control) {
        column->addWidget(plain(QStringLiteral("%1 asks to type in %2").arg(who, where),
                                "settingsRowLabel"));
        column->addWidget(note(QStringLiteral(
            "While they hold it their keys go straight to this terminal, as yours do. Typing in "
            "the pane yourself takes it back at once.")));
    } else {
        column->addWidget(plain(QStringLiteral("%1 wrote a prompt for the agent in %2").arg(who, where),
                                "settingsRowLabel"));
        // The whole text, wrapped and selectable, never elided: approving this row is approving
        // exactly these words, so the owner has to be able to read all of them.
        auto *text = plain(request.text, "settingsRowLabel");
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        relay::installCopyOnSelect(text);
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

QWidget *SharingView::shareControls(const QString &pane, const Scope &scope)
{
    QFrame *frame = card();
    frame->setProperty("current", false);
    auto *column = qobject_cast<QVBoxLayout *>(frame->layout());
    const ShareOptions options = m_model ? m_model->options(pane) : ShareOptions();

    auto *buttons = new FlowRow;
    auto *invite = button(QStringLiteral("Invite someone…"),
                          QStringLiteral("A link and a QR for one more person on this pane."));
    QObject::connect(invite, &QPushButton::clicked, this, [this, pane, scope] {
        if (onInvite) onInvite(pane);
        startInvite(scope);
    });
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

    // The two options on one line, the sentence behind each as its tooltip; the sentences are
    // said once, in full, under the whole section (#SHRP), not under every pane.
    auto *optionRow = new FlowRow;
    auto addOption = [&](const QString &label, const QString &detail, bool on,
                         std::function<void(bool)> set) {
        auto *box = new QCheckBox(label);
        box->setChecked(on);
        box->setToolTip(detail);
        QObject::connect(box, &QCheckBox::toggled, this, [set](bool checked) { set(checked); });
        optionRow->add(box);
    };
    addOption(QStringLiteral("Guest prompts run immediately"), promptsImmediateSentence(),
              options.promptsImmediate, [this, pane, options](bool on) {
                  if (onOptions) onOptions(pane, on, options.presentOnly);
              });
    addOption(QStringLiteral("Guests can act only while I'm here"), presentOnlySentence(),
              options.presentOnly, [this, pane, options](bool on) {
                  if (onOptions) onOptions(pane, options.promptsImmediate, on);
              });
    column->addWidget(optionRow);
    return frame;
}

// ----- the invite form (docs/REMOTE-PROTOCOL.md section 10.2, #97EG) -----------------------------

void SharingView::buildInviteForm(QVBoxLayout *column)
{
    m_inviteForm = card();
    auto *form = qobject_cast<QVBoxLayout *>(m_inviteForm->layout());
    form->addWidget(heading(QStringLiteral("Invite someone")));

    // What the link is for: one pane, a whole tab, or everything. The window builds the list
    // (onScopes) fresh each time the form opens; the view never invents a scope of its own.
    m_scopePick = new QComboBox;
    m_scopePick->setObjectName(QStringLiteral("sharingScopePick"));
    m_scopePick->setToolTip(QStringLiteral(
        "What the link or code lets them into. A tab shared whole includes every pane you add to "
        "it later; everything is every pane in every window, including ones you open later."));
    QObject::connect(m_scopePick, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     [this](int) { if (!m_fillingScopes) updateRoleNote(); });
    form->addWidget(m_scopePick);

    auto *inviteRow = new FlowRow;
    m_inviteRole = new QComboBox;
    m_inviteRole->setObjectName(QStringLiteral("sharingInviteRole"));
    m_inviteRole->addItem(QStringLiteral("Viewer"), QStringLiteral("viewer"));
    m_inviteRole->addItem(QStringLiteral("Editor"), QStringLiteral("editor"));
    QObject::connect(m_inviteRole, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     [this](int) { updateRoleNote(); });
    inviteRow->add(m_inviteRole);
    m_inviteExpiry = new QComboBox;
    m_inviteExpiry->setObjectName(QStringLiteral("sharingInviteExpiry"));
    m_inviteExpiry->addItem(QStringLiteral("for 1 hour"), 3600);
    m_inviteExpiry->addItem(QStringLiteral("for 24 hours"), 86400);
    m_inviteExpiry->addItem(QStringLiteral("for 7 days"), 604800);
    m_inviteExpiry->setCurrentIndex(1);
    inviteRow->add(m_inviteExpiry);
    m_inviteUses = new QSpinBox;
    m_inviteUses->setObjectName(QStringLiteral("sharingInviteUses"));
    m_inviteUses->setRange(1, 20);
    m_inviteUses->setValue(1);
    m_inviteUses->setPrefix(QStringLiteral("uses: "));
    m_inviteUses->setToolTip(QStringLiteral(
        "How many people the link may let in. One link, one person, is the usual thing."));
    inviteRow->add(m_inviteUses);
    auto *makeLink = button(QStringLiteral("Make a link"),
                            QStringLiteral("A link and a QR to send. Whoever opens it knocks, and "
                                           "you admit them here."));
    QObject::connect(makeLink, &QPushButton::clicked, this, [this] { createInvite(); });
    inviteRow->add(makeLink);
    // The other way to hand out the same door: two short things to say out loud, for a friend who
    // is on the phone or across the room rather than in a chat window (#97EG).
    m_makeCode = button(QStringLiteral("Make a code"),
                        QStringLiteral("A four-letter meeting code and a four-digit PIN to read "
                                       "out. It grants the role picked here, lasts 10 minutes and "
                                       "lets in one person."));
    QObject::connect(m_makeCode, &QPushButton::clicked, this, [this] { createCode(); });
    inviteRow->add(m_makeCode);
    form->addWidget(inviteRow);

    m_inviteNote = plain(QString(), "settingsRowDetail");
    m_inviteNote->setObjectName(QStringLiteral("sharingInviteNote"));
    form->addWidget(m_inviteNote);

    // The link, its QR and a Copy button. Smaller than the pairing QR on purpose: this one is
    // read by somebody else's camera across a desk if at all, and the usual way it travels is the
    // Copy button — while the pairing QR is the one being held up to a phone right now.
    m_linkRow = new QWidget;
    auto *linkRow = new QHBoxLayout(m_linkRow);
    linkRow->setContentsMargins(0, 0, 0, 0);
    linkRow->setSpacing(10);
    m_inviteQr = new QLabel;
    m_inviteQr->setAlignment(Qt::AlignCenter);
    linkRow->addWidget(m_inviteQr, 0, Qt::AlignTop);
    auto *linkColumn = new QVBoxLayout;
    // A read-only field rather than a label: a link is one long unbreakable word, so a label
    // either stretches the pane to its full length or silently cuts the end off — and the end
    // is the secret. A field scrolls, selects, and answers Ctrl+A and Ctrl+C.
    m_inviteUrl = new QLineEdit;
    m_inviteUrl->setObjectName(QStringLiteral("sharingInviteUrl"));
    m_inviteUrl->setReadOnly(true);
    relay::installCopyOnSelect(m_inviteUrl);
    m_inviteUrl->setToolTip(QStringLiteral("The whole link. Copy it and send it to one person; "
                                           "anyone who has it can knock."));
    linkColumn->addWidget(m_inviteUrl);
    auto *sendRow = new FlowRow;
    m_inviteCopy = button(QStringLiteral("Copy link"), QStringLiteral("The whole link, to paste anywhere."));
    QObject::connect(m_inviteCopy, &QPushButton::clicked, this, [this] {
        if (m_inviteLink.isEmpty()) return;
        QGuiApplication::clipboard()->setText(m_inviteLink);
        m_inviteCopy->setText(QStringLiteral("Copied"));
    });
    sendRow->add(m_inviteCopy);
    // Email is the same act as copying: for a colleague who is not in the room, and who a chat
    // window would not reach. The link is unchanged — one use over a public address, and they
    // still have to knock.
    m_inviteTo = new QLineEdit;
    m_inviteTo->setPlaceholderText(QStringLiteral("or email it to…"));
    m_inviteTo->setClearButtonEnabled(true);
    m_inviteTo->setMinimumWidth(180);
    sendRow->add(m_inviteTo);
    m_inviteSend = button(QStringLiteral("Send"),
                          QStringLiteral("Emails the link, with what it is for and how long it lasts."));
    QObject::connect(m_inviteSend, &QPushButton::clicked, this, [this] {
        const QString to = m_inviteTo->text().trimmed();
        if (m_inviteLink.isEmpty() || to.isEmpty()) return;
        m_inviteSend->setEnabled(false);
        m_inviteNote->setText(QStringLiteral("Sending to %1…").arg(to));
        if (onEmailInvite)
            onEmailInvite(m_inviteLink, to, m_inviteRoleValue, m_inviteExpiryText,
                          m_inviteScope.title.isEmpty() ? m_inviteScope.id : m_inviteScope.title);
    });
    QObject::connect(m_inviteTo, &QLineEdit::returnPressed, m_inviteSend, &QPushButton::click);
    sendRow->add(m_inviteSend);
    linkColumn->addWidget(sendRow);
    linkColumn->addStretch(1);
    linkRow->addLayout(linkColumn, 1);
    m_linkRow->hide();
    form->addWidget(m_linkRow);

    // ----- the meeting code (#97EG) -------------------------------------------------------------
    // Its own sentence, under the row: the expiry and uses boxes belong to the link, and a code
    // that quietly ignored them would be a code that lasts longer or admits more than it seems.
    m_codeNote = plain(codeIntro(), "settingsRowDetail");
    m_codeNote->setObjectName(QStringLiteral("sharingCodeNote"));
    form->addWidget(m_codeNote);

    // Large, fixed-width and letter-spaced, because each character is going to be read aloud and
    // typed by somebody else: an ambiguous glyph here is a wrong PIN there.
    m_codeBox = new QWidget;
    auto *codeRow = new QHBoxLayout(m_codeBox);
    codeRow->setContentsMargins(0, 0, 0, 0);
    codeRow->setSpacing(24);
    const QFont big = bigCodeFont();
    auto value = [&](const QString &caption, QLabel **target, const char *name) {
        auto *cell = new QVBoxLayout;
        cell->setSpacing(2);
        cell->addWidget(plain(caption, "settingsRowDetail"));
        *target = new QLabel;
        (*target)->setObjectName(QLatin1String(name));
        (*target)->setFont(big);
        (*target)->setTextFormat(Qt::PlainText);
        (*target)->setTextInteractionFlags(Qt::TextSelectableByMouse);
        cell->addWidget(*target);
        codeRow->addLayout(cell);
    };
    value(QStringLiteral("Meeting code"), &m_codeValue, "sharingMeetingCode");
    value(QStringLiteral("PIN"), &m_pinValue, "sharingMeetingPin");
    auto *codeSide = new QVBoxLayout;
    codeSide->setSpacing(4);
    m_codeClock = new QLabel;
    m_codeClock->setTextFormat(Qt::PlainText);
    codeSide->addWidget(m_codeClock);
    auto *codeButtons = new FlowRow;
    m_codeCopy = button(QStringLiteral("Copy"),
                        QStringLiteral("Copies one line with the join address, the meeting code "
                                       "and the PIN, ready to send."));
    QObject::connect(m_codeCopy, &QPushButton::clicked, this, [this] {
        if (m_code.isEmpty()) return;
        QString base = m_service.base;
        while (base.endsWith(QLatin1Char('/'))) base.chop(1);
        QGuiApplication::clipboard()->setText(
            QStringLiteral("Join my Relay pane at %1/join — meeting code %2, PIN %3")
                .arg(base, m_code, m_pin));
        m_codeCopy->setText(QStringLiteral("Copied"));
    });
    codeButtons->add(m_codeCopy);
    m_codeAgain = button(QStringLiteral("Make a new code"),
                         QStringLiteral("Another code and PIN, for the same role."));
    m_codeAgain->hide();
    QObject::connect(m_codeAgain, &QPushButton::clicked, this, [this] { createCode(); });
    codeButtons->add(m_codeAgain);
    codeSide->addWidget(codeButtons);
    codeRow->addLayout(codeSide, 1);
    m_codeBox->hide();
    form->addWidget(m_codeBox);

    m_inviteForm->hide();
    column->addWidget(m_inviteForm);
    updateRoleNote();
}

void SharingView::startInvite(const Scope &scope)
{
    showPage(Page::People);
    fillScopePicker(scope);
    m_inviteForm->show();
    // A fresh form: a link or code made for the last scope must not sit under a new one.
    m_scopePick->setFocus(Qt::OtherFocusReason);
}

// The picker's three groups, built fresh from the window's catalogue: every pane, grouped by the
// tab it sits in; every tab, as a whole; and everything. Captions are disabled rows, so the list
// reads as three sections without a second widget.
void SharingView::fillScopePicker(const Scope &wanted)
{
    // What was picked before, so that opening the form again on the same scope is not a change:
    // a meeting code the owner is reading out must survive a second press of Invite someone….
    const bool hadPick = m_scopePick->currentData().toInt() >= 0;
    const Scope before = pickedScope();
    m_fillingScopes = true;
    m_scopes = onScopes ? onScopes() : QList<Scope>();
    m_scopePick->clear();
    auto *rows = qobject_cast<QStandardItemModel *>(m_scopePick->model());
    auto caption = [&](const QString &text) {
        m_scopePick->addItem(text, -1);
        if (rows) if (QStandardItem *item = rows->item(m_scopePick->count() - 1)) item->setEnabled(false);
    };
    QStringList tabsSeen;
    for (int index = 0; index < m_scopes.size(); ++index) {
        const Scope &scope = m_scopes.at(index);
        if (scope.kind != Scope::Kind::Pane) continue;
        if (!tabsSeen.contains(scope.tabTitle)) {
            tabsSeen << scope.tabTitle;
            caption(scope.tabTitle.isEmpty() ? QStringLiteral("Panes")
                                             : QStringLiteral("Panes in “%1”").arg(scope.tabTitle));
            // Panes of one tab sit together whatever order the catalogue came in.
            for (int other = index; other < m_scopes.size(); ++other) {
                const Scope &pane = m_scopes.at(other);
                if (pane.kind != Scope::Kind::Pane || pane.tabTitle != scope.tabTitle) continue;
                m_scopePick->addItem(pane.title.isEmpty() ? pane.id : pane.title, other);
            }
        }
    }
    bool tabCaption = false;
    for (int index = 0; index < m_scopes.size(); ++index) {
        const Scope &scope = m_scopes.at(index);
        if (scope.kind != Scope::Kind::Tab) continue;
        if (!tabCaption) { caption(QStringLiteral("Whole tabs")); tabCaption = true; }
        m_scopePick->addItem(QStringLiteral("Tab “%1” (%2, and any you add)")
                                 .arg(scope.title.isEmpty() ? scope.id : scope.title,
                                      scope.panes == 1 ? QStringLiteral("1 pane")
                                                       : QStringLiteral("%1 panes").arg(scope.panes)),
                             index);
    }
    for (int index = 0; index < m_scopes.size(); ++index) {
        if (m_scopes.at(index).kind != Scope::Kind::All) continue;
        m_scopePick->addItem(QStringLiteral("Everything (every pane in every window)"), index);
    }
    // The scope asked for; otherwise the pane this Sharing pane was opened from; otherwise the
    // first thing that can be picked.
    int pick = -1;
    for (int row = 0; row < m_scopePick->count() && pick < 0; ++row) {
        const int index = m_scopePick->itemData(row).toInt();
        if (index >= 0 && m_scopes.at(index) == wanted) pick = row;
    }
    for (int row = 0; row < m_scopePick->count() && pick < 0; ++row) {
        const int index = m_scopePick->itemData(row).toInt();
        if (index >= 0 && m_scopes.at(index).kind == Scope::Kind::Pane
            && (m_scopes.at(index).id == m_pane || (m_pane.isEmpty() && m_scopes.at(index).current)))
            pick = row;
    }
    for (int row = 0; row < m_scopePick->count() && pick < 0; ++row) {
        if (m_scopePick->itemData(row).toInt() >= 0) pick = row;
    }
    // Nothing from the window at all (no catalogue wired): the scope asked for is still a scope.
    if (pick < 0 && !wanted.id.isEmpty()) {
        m_scopes.append(wanted);
        m_scopePick->addItem(wanted.title.isEmpty() ? wanted.id : wanted.title, m_scopes.size() - 1);
        pick = m_scopePick->count() - 1;
    }
    m_scopePick->setCurrentIndex(pick);
    m_scopePick->setVisible(m_scopePick->count() > 1);
    m_fillingScopes = false;
    if (!hadPick || !(pickedScope() == before)) updateRoleNote();
}

Scope SharingView::pickedScope() const
{
    const int index = m_scopePick->currentData().toInt();
    if (index < 0 || index >= m_scopes.size()) return Scope{};
    return m_scopes.at(index);
}

// What the chosen role will let the person do, in one sentence, before the link exists. The same
// sentence the rows show beside them afterwards, so the promise does not change wording.
void SharingView::updateRoleNote()
{
    if (!m_inviteNote) return;
    m_inviteNote->setText(roleSentence(m_inviteRole->currentData().toString()));
    // Changing the role or the scope after a link exists would make the link on screen say the
    // wrong thing, so the old one is put away and the row asks for another.
    m_linkRow->hide();
    m_inviteLink.clear();
    m_inviteAsked = false;
    // The same for a code on screen: it would be saying the wrong role out loud.
    if (!m_code.isEmpty()) putCodeAway();
}

void SharingView::createInvite()
{
    m_inviteScope = pickedScope();
    m_inviteAsked = true;
    if (onCreateInvite)
        onCreateInvite(m_inviteScope, m_inviteRole->currentData().toString(),
                       m_inviteExpiry->currentData().toInt(), m_inviteUses->value());
    m_inviteNote->setText(QStringLiteral("Making a link…"));
}

void SharingView::showInvite(const QString &url, const QrMatrix &qr, const QString &role, int uses,
                             int expires)
{
    // Every Sharing pane hears every `invite` line; only the one that asked shows it.
    if (!m_inviteAsked) return;
    m_inviteAsked = false;
    m_inviteLink = url;
    const QPixmap code = qrPixmap(qr, 130);
    if (!code.isNull()) {
        m_inviteQr->setFixedSize(code.size());
        m_inviteQr->setPixmap(code);
        m_inviteQr->show();
    } else {
        m_inviteQr->hide();
    }
    // The whole link, secret and all: unlike the pairing QR this one is meant to be copied and
    // sent to somebody, so it has to be on screen where it can be selected.
    m_inviteUrl->setText(url);
    m_inviteUrl->setCursorPosition(0);
    m_inviteCopy->setText(QStringLiteral("Copy link"));
    m_inviteRoleValue = role;
    m_inviteExpiryText = QStringLiteral("expires in %1").arg(expiryText(expires));
    m_inviteTo->clear();
    m_inviteSend->setEnabled(true);
    m_linkRow->show();
    m_inviteForm->show();
    m_inviteNote->setText(QStringLiteral("Send this to the person you want on %1. It lets in "
                                         "%2 and stops working in %3. %4")
                              .arg(scopePlace(m_inviteScope),
                                   uses == 1 ? QStringLiteral("one person")
                                             : QStringLiteral("%1 people").arg(uses),
                                   expiryText(expires), roleSentence(role)));
}

void SharingView::inviteSent(bool ok, const QString &message)
{
    m_inviteNote->setText(message);
    m_inviteSend->setEnabled(true);
    if (ok) m_inviteTo->clear();   // one link, one person: the next one needs a new link
}

// ----- the meeting code (#97EG) -----------------------------------------------------------------

void SharingView::createCode()
{
    // One live code per pane: the sidecar burns the one on screen to make room for this one, so
    // from this moment it is not something to read out.
    if (m_codeDeadline) {
        m_codeDeadline = 0;
        markCodeDead(true);
        m_codeClock->setText(QStringLiteral("Replaced"));
    }
    m_inviteScope = pickedScope();
    m_codeRole = m_inviteRole->currentData().toString();
    m_codeAskedAt = QDateTime::currentMSecsSinceEpoch();
    m_makeCode->setEnabled(false);
    m_codeAgain->setEnabled(false);
    if (onCreateCode) onCreateCode(m_inviteScope, m_codeRole);
    m_codeNote->setText(QStringLiteral("Making a code…"));
}

void SharingView::showCode(const QString &code, const QString &pin, int expires)
{
    // Every Sharing pane hears every `code` line; only the one that asked shows it.
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
    QString base = m_service.base;
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    m_codeNote->setText(QStringLiteral(
        "Tell your friend both, out loud or in a message. They open %1/join, type the meeting "
        "code and the PIN, and knock; you admit them on the Sharing pane, as %2 at most. The "
        "code works once, for one person, and stops in 10 minutes.")
                            .arg(base, m_codeRole == QLatin1String("editor")
                                           ? QStringLiteral("an editor")
                                           : QStringLiteral("a viewer")));
    m_codeBox->show();
    m_inviteForm->show();
    codeTick();
}

void SharingView::showCodeState(const QString &code, const QString &state, int failures)
{
    if (code.isEmpty() || code != m_code) return;   // an older code this pane no longer shows
    // A new code is on its way for this pane, and the sidecar burns the old one to make room:
    // that burn is the replacement, not three wrong PINs, and the new code is what goes here.
    if (m_codeAskedAt != 0) return;
    m_codeDeadline = 0;
    markCodeDead(true);
    if (state == QLatin1String("used")) {
        m_codeClock->setText(QStringLiteral("Used"));
        m_codeAgain->hide();
        m_codeNote->setText(QStringLiteral(
            "Someone joined with this code. Look for their knock under Waiting for you, and admit "
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
        // Closed without a wrong guess: revoked, from here or from the invite row.
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
}

// Struck through rather than cleared: the person may be on the phone asking "which code?", and the
// answer is this one, which is no longer any good. Copy goes with it: it no longer lets anyone in.
void SharingView::markCodeDead(bool dead)
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
void SharingView::codeTick()
{
    if (!m_codeClock) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_codeAskedAt > 0 && now - m_codeAskedAt > 15000) {
        m_codeAskedAt = -1;   // still listening: a late code is live on the hub and must show
        m_makeCode->setEnabled(true);
        m_codeAgain->setEnabled(true);
        m_codeNote->setText(QStringLiteral(
            "No code came back. The sharing service did not answer; try again, or make a link."));
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

// The role or the scope changed under a code on screen. A live one is revoked, not just hidden: it
// must not go on admitting somebody at a role the owner has just moved away from. (Closing the
// pane does not come through here — the owner reads the code out and closes it, and the code
// stays good.)
void SharingView::putCodeAway()
{
    const bool live = m_codeDeadline != 0;
    if (live && onRevokeCode) onRevokeCode(m_code);
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

}  // namespace relay::sharing
