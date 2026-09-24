// SPDX-License-Identifier: AGPL-3.0-or-later
#include "BoardRemote.h"

#include "BoardModel.h"
#include "BoardWorker.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTimer>

namespace relay {

namespace boardremote {

namespace {

constexpr int kMaxText = 32768;       // board_protocol.MAX_ASK_TEXT: the worker's own cap
constexpr int kMaxTitle = 300;
constexpr int kMaxQuery = 512;
constexpr int kMaxReason = 1000;
constexpr int kMaxLabels = 12;
constexpr int kMaxLabel = 40;

const QStringList &pathKeys()
{
    static const QStringList keys{QStringLiteral("path"), QStringLiteral("root"), QStringLiteral("folder"),
                                  QStringLiteral("file"), QStringLiteral("dir"), QStringLiteral("cwd"),
                                  QStringLiteral("workspace")};
    return keys;
}

// A card id as a device may spell it: "#k7q2" and "K7Q2" are the same card. The worker's
// `normalize_id` is the judge of the alphabet; this only keeps anything that is not an id at all
// — a path, a sentence — from travelling as one.
QString cardId(const QJsonValue &value)
{
    QString id = value.toString().trimmed().toUpper();
    while (id.startsWith(QLatin1Char('#')))
        id.remove(0, 1);
    static const QRegularExpression shape(QStringLiteral("^[0-9A-Z]{4}$"));
    return shape.match(id).hasMatch() ? id : QString();
}

Translated refused(const QString &text, const QString &code = QStringLiteral("board_refused"))
{
    return {QJsonObject{}, text, code};
}

QJsonValue withoutPathsValue(const QJsonValue &value)
{
    if (value.isObject())
        return withoutPaths(value.toObject());
    if (value.isArray()) {
        QJsonArray out;
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array)
            out.append(withoutPathsValue(item));
        return out;
    }
    return value;
}

}  // namespace

const QStringList &allowedRequests()
{
    static const QStringList types{
        QStringLiteral("board_open"),    QStringLiteral("board_refresh"), QStringLiteral("board_card_get"),
        QStringLiteral("board_search"),  QStringLiteral("board_comment"), QStringLiteral("board_move"),
        QStringLiteral("board_create"),  QStringLiteral("board_ask"),     QStringLiteral("board_cancel"),
        // Stop and go, per card (#7JD1): `board_cancel` pauses that card's queue, `board_resume`
        // runs it again. It is what a device's *empty* send is, the way Enter on an empty prompt
        // box is at the desk, and the only queue op a device has — the only one that needs
        // neither a row id nor any of a queue's state to aim.
        QStringLiteral("board_resume"),
        QStringLiteral("board_action")};
    return types;
}

bool requestAllowed(const QString &type)
{
    return allowedRequests().contains(type);
}

bool carriesPath(const QJsonValue &value)
{
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            if (pathKeys().contains(it.key().toLower()) || carriesPath(it.value()))
                return true;
        return false;
    }
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array)
            if (carriesPath(item))
                return true;
    }
    return false;
}

Translated toWorker(const QJsonObject &request, const QString &workerId, const QString &deviceName)
{
    const QString type = request.value(QStringLiteral("type")).toString();
    if (!requestAllowed(type))
        return refused(QStringLiteral("A paired device may not send %1.")
                           .arg(type.isEmpty() ? QStringLiteral("a request with no type") : type));
    if (carriesPath(request))
        return refused(QStringLiteral("A request from a device never names a file or a folder."));
    // `board_action` is the GUI's, not the worker's: BoardRemote::handleRequest runs it.
    if (type == QStringLiteral("board_action"))
        return refused(QStringLiteral("board_action is not a worker message."));

    const QString name = deviceName.trimmed().isEmpty() ? QStringLiteral("a paired device")
                                                        : deviceName.trimmed();
    QJsonObject message{{QStringLiteral("type"), type}, {QStringLiteral("id"), workerId}};
    if (type == QStringLiteral("board_open") || type == QStringLiteral("board_refresh"))
        return {message, QString(), QString()};

    if (type == QStringLiteral("board_search")) {
        message.insert(QStringLiteral("query"),
                       request.value(QStringLiteral("query")).toString().left(kMaxQuery));
        return {message, QString(), QString()};
    }

    if (type == QStringLiteral("board_create")) {
        const QString title = request.value(QStringLiteral("title")).toString().trimmed().left(kMaxTitle);
        // The contract calls the card's own words `request`; the worker calls them `text` (19.3).
        const QString text = request.value(QStringLiteral("request")).toString().left(kMaxText);
        if (title.isEmpty() && text.trimmed().isEmpty())
            return refused(QStringLiteral("A new card needs a title or some words."));
        static const QRegularExpression word(QStringLiteral("^[a-z0-9][a-z0-9_-]{0,39}$"));
        const QString tab = request.value(QStringLiteral("tab")).toString().trimmed();
        if (!tab.isEmpty()) {
            if (!word.match(tab).hasMatch())
                return refused(QStringLiteral("That is not one of the board's categories."));
            message.insert(QStringLiteral("tab"), tab);
        }
        message.insert(QStringLiteral("status"), QStringLiteral("inbox"));
        message.insert(QStringLiteral("text"), text.trimmed().isEmpty() ? title : text);
        if (!title.isEmpty())
            message.insert(QStringLiteral("title"), title);
        QJsonArray labels;
        const QJsonArray given = request.value(QStringLiteral("labels")).toArray();
        for (const QJsonValue &label : given) {
            const QString text = label.toString().trimmed().left(kMaxLabel);
            if (!text.isEmpty() && labels.size() < kMaxLabels)
                labels.append(text);
        }
        if (!labels.isEmpty())
            message.insert(QStringLiteral("labels"), labels);
        message.insert(QStringLiteral("source"), QStringLiteral("remote: %1").arg(name).left(120));
        return {message, QString(), QString()};
    }

    // Everything below names a card. The contract's `id` is the card; on the worker's wire `id`
    // is the request and the card is `card` (19.2: "a card id never travels as `id`").
    const QString card = cardId(request.value(QStringLiteral("id")));
    if (card.isEmpty())
        return refused(QStringLiteral("%1 needs a card id such as K7Q2.").arg(type));
    message.insert(QStringLiteral("card"), card);

    if (type == QStringLiteral("board_card_get") || type == QStringLiteral("board_cancel")
        || type == QStringLiteral("board_resume"))
        return {message, QString(), QString()};

    if (type == QStringLiteral("board_comment")) {
        const QString text = request.value(QStringLiteral("text")).toString().left(kMaxText);
        if (text.trimmed().isEmpty())
            return refused(QStringLiteral("An empty comment is not written."));
        // A person's kinds only, in the worker's own words (`board_tools.COMMENT_KINDS`): `note` is
        // what the desktop's reply box sends, and what a kind this file does not know becomes —
        // `progress` and `evidence` are what agents and the board itself write, and a device is
        // neither.
        static const QStringList kinds{QStringLiteral("note"), QStringLiteral("question"), QStringLiteral("decision")};
        QString kind = request.value(QStringLiteral("kind")).toString();
        if (!kinds.contains(kind))
            kind = QStringLiteral("note");
        QString said = text;
        if (kind == QStringLiteral("decision")) {
            // The worker refuses a decision that does not quote the owner (`_QUOTE_RE`): the rule
            // is there so that an *agent* cannot paraphrase him. Typed on his own phone the words
            // are the quote, so they are written as one; too short to be one, they are a note.
            static const QRegularExpression quoted(
                QStringLiteral("[\"\u201c\u201d\u2018\u2019']([^\"\u201c\u201d\u2018\u2019']{3,})[\"\u201c\u201d\u2018\u2019']"));
            if (!quoted.match(said).hasMatch())
                said = QStringLiteral("owner, from %1: \u201c%2\u201d").arg(name, text.trimmed());
            if (!quoted.match(said).hasMatch()) {
                said = text;
                kind = QStringLiteral("note");
            }
        }
        message.insert(QStringLiteral("text"), said);
        message.insert(QStringLiteral("kind"), kind);
        return {message, QString(), QString()};
    }

    if (type == QStringLiteral("board_move")) {
        static const QRegularExpression word(QStringLiteral("^[a-z][a-z0-9-]{0,39}$"));
        const QString status = request.value(QStringLiteral("status")).toString().trimmed();
        if (!word.match(status).hasMatch())
            return refused(QStringLiteral("board_move needs the status to move the card to."));
        const QString reason = request.value(QStringLiteral("reason")).toString().trimmed().left(kMaxReason);
        message.insert(QStringLiteral("status"), status);
        // The reason is what the card's thread records, so it is where the device is named.
        message.insert(QStringLiteral("reason"),
                       reason.isEmpty() ? QStringLiteral("moved from %1").arg(name)
                                        : QStringLiteral("%1 (from %2)").arg(reason, name));
        return {message, QString(), QString()};
    }

    // board_ask
    const QString mode = request.value(QStringLiteral("mode")).toString().isEmpty()
                             ? QStringLiteral("discuss")
                             : request.value(QStringLiteral("mode")).toString();
    if (mode != QStringLiteral("discuss") && mode != QStringLiteral("plan")
        && mode != QStringLiteral("refine"))                            // #6W9X
        return refused(QStringLiteral("board_ask mode is discuss, plan or refine."));
    const QString text = request.value(QStringLiteral("text")).toString().left(kMaxText);
    if (mode == QStringLiteral("discuss") && text.trimmed().isEmpty())
        return refused(QStringLiteral("A Discuss needs some words."));
    message.insert(QStringLiteral("mode"), mode);
    message.insert(QStringLiteral("text"), text);
    return {message, QString(), QString()};
}

bool eventForwarded(const QString &type, bool answersRequest)
{
    static const QStringList broadcast{
        QStringLiteral("board"),          QStringLiteral("board_cards"),
        QStringLiteral("board_changed"),  QStringLiteral("board_thread_appended"),
        QStringLiteral("board_written"),  QStringLiteral("board_activity"),
        QStringLiteral("board_cancelled"), QStringLiteral("board_busy"),
        QStringLiteral("board_conflict")};
    if (broadcast.contains(type))
        return true;
    // `board_resumed` answers the device that asked and nobody else: a resume changes a queue,
    // and a device is sent none of a queue's state to bring up to date (#7JD1, remote 17.4).
    static const QStringList answers{QStringLiteral("board_card"), QStringLiteral("board_search"),
                                     QStringLiteral("board_resumed"), QStringLiteral("error")};
    return answersRequest && answers.contains(type);
}

QJsonObject withoutPaths(const QJsonObject &event)
{
    QJsonObject out;
    for (auto it = event.begin(); it != event.end(); ++it) {
        if (pathKeys().contains(it.key().toLower()))
            continue;
        if (it.key() == QStringLiteral("project") && it.value().isString()
            && QDir::isAbsolutePath(it.value().toString())) {
            out.insert(it.key(), QFileInfo(it.value().toString()).fileName());
            continue;
        }
        out.insert(it.key(), withoutPathsValue(it.value()));
    }
    return out;
}

QString writeNotice(const QString &requestType, const QJsonObject &written, const QString &deviceName)
{
    const QString name = deviceName.trimmed().isEmpty() ? QStringLiteral("a paired device")
                                                        : deviceName.trimmed();
    const QString card = written.value(QStringLiteral("card_id")).toString();
    if (card.isEmpty())
        return QString();
    if (requestType == QStringLiteral("board_move")) {
        const QString status = written.value(QStringLiteral("status")).toString();
        return status.isEmpty()
                   ? QStringLiteral("Card #%1 moved from %2").arg(card, name)
                   : QStringLiteral("Card #%1 moved to %2 from %3").arg(card, board::statusTitle(status), name);
    }
    if (requestType == QStringLiteral("board_comment"))
        return QStringLiteral("Comment on #%1 from %2").arg(card, name);
    if (requestType == QStringLiteral("board_create"))
        return QStringLiteral("New card #%1 from %2").arg(card, name);
    return QString();
}

}  // namespace boardremote

// ------------------------------------------------------------------------------------------------

namespace {
constexpr int kMaxRemembered = 256;
constexpr int kMaxWatched = 200;      // the board pane's own budget (BoardView::watchIssues)
}

BoardRemote::BoardRemote(QObject *parent) : QObject(parent) {}

BoardRemote &BoardRemote::instance()
{
    static BoardRemote bridge;
    return bridge;
}

QString BoardRemote::who(const QString &deviceName)
{
    return deviceName.trimmed().isEmpty() ? QStringLiteral("a paired device") : deviceName.trimmed();
}

void BoardRemote::addHost(QObject *owner, Host host)
{
    if (!owner)
        return;
    m_hosts.append({QPointer<QObject>(owner), std::move(host)});
    connect(owner, &QObject::destroyed, this, [this, owner] {
        for (int i = int(m_hosts.size()) - 1; i >= 0; --i)
            if (!m_hosts.at(i).owner || m_hosts.at(i).owner.data() == owner)
                m_hosts.removeAt(i);
    });
}

void BoardRemote::tap(BoardWorker *worker, QObject *owner, const QString &tab)
{
    if (!worker)
        return;
    // The handler in place is kept and called first, with the very same event: the window's
    // routing to its board panes and helper panels is untouched by there being a phone.
    std::function<void(const QJsonObject &)> previous = worker->onEvent;
    QPointer<BoardRemote> self(this);
    QPointer<QObject> window(owner);
    worker->onEvent = [previous, self, window, tab](const QJsonObject &event) {
        if (previous)
            previous(event);
        if (self && window)
            self->workerEvent(window, tab, event);
    };
}

// The window a request is served by. A device stays on the board its last `board_open` found
// — the owner switching tabs on the desktop must not move the phone's writes to another project's
// cards — until that tab goes or stops having a board; `reopen` (a `board_open`) chooses again.
BoardRemote::Entry *BoardRemote::hostFor(bool reopen)
{
    if (!reopen && m_owner && !m_tab.isEmpty())
        for (Entry &entry : m_hosts)
            if (entry.owner == m_owner && entry.host.hasBoard && entry.host.hasBoard(m_tab))
                return &entry;
    Entry *found = nullptr;
    QString tab;
    for (Entry &entry : m_hosts) {
        if (!entry.owner || !entry.host.boardTab)
            continue;
        const QString candidate = entry.host.boardTab();
        if (candidate.isEmpty())
            continue;
        const bool active = entry.host.active && entry.host.active();
        if (!found || active) {
            found = &entry;
            tab = candidate;
        }
        if (active)
            break;
    }
    // No tab anywhere has a board. The window the owner last worked in is asked first whether its
    // pane stands in a project that has one; then the others, in the order they opened.
    for (int pass = 0; pass < 2 && !found; ++pass)
        for (Entry &entry : m_hosts) {
            if (!entry.owner || !entry.host.adoptBoard)
                continue;
            if ((entry.host.active && entry.host.active()) != (pass == 0))
                continue;
            tab = entry.host.adoptBoard();
            if (!tab.isEmpty()) {
                found = &entry;
                break;
            }
        }
    if (!found)
        return nullptr;
    if (m_owner != found->owner || m_tab != tab) {
        m_turns.clear();                   // another board: its turns are not this one's
        m_owner = found->owner;
        m_tab = tab;
    }
    return found;
}

// The board a device is on is watched the way a Board pane watches its own (BoardView::
// watchIssues): the folder and its subfolders, one debounced `board_refresh` per burst, which the
// worker answers with the rows that changed — and that `board_changed` is what a phone's list, and
// the hub's "a card is waiting on you" push, are made from. With no Board pane open on the
// desktop nobody else would ask.
void BoardRemote::watchBoard()
{
    QString root;
    for (const Entry &entry : std::as_const(m_hosts))
        if (entry.owner && entry.owner == m_owner && entry.host.boardDir)
            root = entry.host.boardDir(m_tab);
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        m_refresh = new QTimer(this);
        m_refresh->setSingleShot(true);
        m_refresh->setInterval(400);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_refresh->start(); });
        connect(m_refresh, &QTimer::timeout, this, [this] { boardTouched(); });
    }
    QStringList wanted;
    if (!root.isEmpty() && QFileInfo(root).isDir()) {
        wanted << root;
        QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext() && wanted.size() < kMaxWatched)
            wanted << it.next();
    }
    const QStringList known = m_watcher->directories();
    QStringList fresh, stale;
    for (const QString &path : std::as_const(wanted))
        if (!known.contains(path))
            fresh << path;
    for (const QString &path : known)
        if (!wanted.contains(path))
            stale << path;                 // another board's, or a folder that has gone
    if (!stale.isEmpty())
        m_watcher->removePaths(stale);
    if (!fresh.isEmpty())
        m_watcher->addPaths(fresh);
}

void BoardRemote::boardTouched()
{
    Entry *entry = nullptr;
    for (Entry &candidate : m_hosts)
        if (candidate.owner && candidate.owner == m_owner)
            entry = &candidate;
    const bool alive = entry && entry->host.hasBoard && entry->host.hasBoard(m_tab);
    if (!alive) {
        if (m_watcher && !m_watcher->directories().isEmpty())
            m_watcher->removePaths(m_watcher->directories());
        return;
    }
    // Remote control off: nobody to tell, so the worker is not woken for it.
    const bool paneDoesIt = entry->host.paneWatches && entry->host.paneWatches(m_tab);
    if (!paneDoesIt && remoteOn && remoteOn() && entry->host.send)
        entry->host.send(m_tab, QJsonObject{{QStringLiteral("type"), QStringLiteral("board_refresh")},
                                            {QStringLiteral("id"), remember({QJsonValue(QJsonValue::Null),
                                                                             QStringLiteral("board_watch"),
                                                                             QString(), QString(), QString()})}});
    watchBoard();                          // folders come and go as cards move between them
}

QString BoardRemote::remember(const Pending &pending)
{
    const QString id = QStringLiteral("remote-%1").arg(++m_seq);
    m_requests.insert(id, pending);
    m_order.append(id);
    while (m_order.size() > kMaxRemembered)
        m_requests.remove(m_order.takeFirst());
    return id;
}

void BoardRemote::refuse(const QJsonValue &rid, const QString &type, const QString &code, const QString &text)
{
    if (!sendEvent)
        return;
    sendEvent(rid, QJsonObject{{QStringLiteral("event"), QStringLiteral("error")},
                               {QStringLiteral("code"), code},
                               {QStringLiteral("request"), type},
                               {QStringLiteral("text"), text}});
}

void BoardRemote::handleRequest(const QJsonObject &line)
{
    const QJsonValue rid = line.value(QStringLiteral("rid"));
    const QJsonObject request = line.value(QStringLiteral("request")).toObject();
    const QString type = request.value(QStringLiteral("type")).toString();
    // The contract names the device's display name `name`; `device_name` is what the pane-level
    // messages call it, read as well so that either spelling attributes the write.
    QString device = line.value(QStringLiteral("name")).toString();
    if (device.isEmpty())
        device = line.value(QStringLiteral("device_name")).toString();

    if (!remoteOn || !remoteOn()) {
        refuse(rid, type, QStringLiteral("remote_off"),
               QStringLiteral("Remote control is off on this desktop."));
        return;
    }
    if (!boardremote::requestAllowed(type)) {
        refuse(rid, type, QStringLiteral("board_refused"),
               QStringLiteral("A paired device may not send %1.")
                   .arg(type.isEmpty() ? QStringLiteral("a request with no type") : type));
        return;
    }
    if (boardremote::carriesPath(request)) {
        refuse(rid, type, QStringLiteral("board_refused"),
               QStringLiteral("A request from a device never names a file or a folder."));
        return;
    }

    Pending pending{rid, type, device, QString(), QString()};
    QJsonObject message;
    if (type == QStringLiteral("board_action")) {
        const QString action = request.value(QStringLiteral("action")).toString();
        if (action != QStringLiteral("execute") && action != QStringLiteral("verify")) {
            refuse(rid, type, QStringLiteral("board_refused"),
                   QStringLiteral("board_action is execute or verify."));
            return;
        }
        // Read the card first, as the desktop's `x` and `v` do on a card that is not open yet:
        // the task text needs its title, whether it has a plan, and its verifier. The answer comes
        // back to workerEvent() under this id and goes no further than act().
        const boardremote::Translated read = boardremote::toWorker(
            QJsonObject{{QStringLiteral("type"), QStringLiteral("board_card_get")},
                        {QStringLiteral("id"), request.value(QStringLiteral("id"))}},
            QString(), device);
        if (!read.error.isEmpty()) {
            refuse(rid, type, read.code, QStringLiteral("board_action needs a card id such as K7Q2."));
            return;
        }
        pending.action = action;
        message = read.message;
    } else {
        const boardremote::Translated translated = boardremote::toWorker(request, QString(), device);
        if (!translated.error.isEmpty()) {
            refuse(rid, type, translated.code, translated.error);
            return;
        }
        message = translated.message;
    }
    pending.card = message.value(QStringLiteral("card")).toString();

    Entry *entry = hostFor(type == QStringLiteral("board_open"));
    if (!entry || !entry->host.send) {
        refuse(rid, type, QStringLiteral("board_not_found"),
               QStringLiteral("No tab on the desktop has a Board. Open a project's Board "
                              "there once, and it is here too."));
        return;
    }
    const QString id = remember(pending);
    message.insert(QStringLiteral("id"), id);
    if (!entry->host.send(m_tab, message)) {
        m_requests.remove(id);
        m_order.removeAll(id);
        refuse(rid, type, QStringLiteral("board_not_found"),
               QStringLiteral("The desktop's Board could not be reached."));
        return;
    }
    if (type == QStringLiteral("board_open"))
        watchBoard();
    // Said when it is asked, for the requests that start something rather than write a line: the
    // writes are said when they land (workerEvent), with the id the worker gave the card.
    if (type == QStringLiteral("board_ask") && entry->host.status) {
        // Plan, Refine (#6W9X) or Discuss: a `board_ask` with no mode is a Discuss (19.10).
        const QString mode = board::modeTitle(message.value(QStringLiteral("mode")).toString());
        entry->host.status(QStringLiteral("%1 on #%2 from %3")
                               .arg(mode.isEmpty() ? QStringLiteral("Discuss") : mode,
                                    pending.card, who(device)));
    }
}

void BoardRemote::workerEvent(QObject *owner, const QString &tab, const QJsonObject &event)
{
    // Only the board a device is on: another tab's worker is another conversation, and often
    // another project.
    if (!owner || owner != m_owner.data() || tab != m_tab || m_tab.isEmpty())
        return;
    const QString type = event.value(QStringLiteral("event")).toString();
    const QString card = event.value(QStringLiteral("card_id")).toString();
    // Which cards have a Discuss or Plan running (19.16), so that Run and Verify from a device
    // are refused mid-turn exactly as the desktop's buttons refuse them. Followed whether or not
    // remote control is on: the turn is running either way.
    if (!card.isEmpty()) {
        if (type == QStringLiteral("agent_started"))   // board_turns.CardTurns.start: the turn boundary
            m_turns.insert(card);
        else if (type == QStringLiteral("done") || type == QStringLiteral("error")
                 || type == QStringLiteral("cancelled"))
            m_turns.remove(card);
    }

    QString id = event.value(QStringLiteral("id")).toString();
    auto found = id.isEmpty() ? m_requests.constEnd() : m_requests.constFind(id);
    // A card turn's failure carries the *turn's* id, not the ask's (board_turns tags it with
    // `card_id`), so it is matched to the newest `board_ask` a device sent for that card: the
    // phone that asked is the one that has to stop waiting for an answer.
    if (found == m_requests.constEnd() && type == QStringLiteral("error") && !card.isEmpty())
        for (int i = int(m_order.size()) - 1; i >= 0 && found == m_requests.constEnd(); --i) {
            const auto asked = m_requests.constFind(m_order.at(i));
            if (asked != m_requests.constEnd() && asked->type == QStringLiteral("board_ask")
                && asked->card == card) {
                found = asked;
                id = m_order.at(i);
            }
        }
    const bool answers = found != m_requests.constEnd();
    const Pending pending = answers ? found.value() : Pending{};

    // The bridge's own read for a board_action: it ends here, in the action.
    if (answers && !pending.action.isEmpty()
        && (type == QStringLiteral("board_card") || type == QStringLiteral("error"))) {
        m_requests.remove(id);
        m_order.removeAll(id);
        if (type == QStringLiteral("error"))
            actionResult(pending, false, QString(), event.value(QStringLiteral("text")).toString());
        else
            act(pending, event);
        return;
    }

    if (!remoteOn || !remoteOn() || !sendEvent)
        return;

    // A write from a device that landed is said on the desktop, the way a remote model change is.
    if (answers && type == QStringLiteral("board_written")) {
        const QString notice = boardremote::writeNotice(pending.type, event, pending.device);
        if (!notice.isEmpty())
            for (const Entry &entry : std::as_const(m_hosts))
                if (entry.owner == m_owner && entry.host.status)
                    entry.host.status(notice);
    }
    // A project with no Switchboard yet asks the desktop before creating one (19.12). The question
    // is the desktop's to answer; the device is told why its card has not appeared.
    if (answers && type == QStringLiteral("board_init_request")) {
        refuse(pending.rid, pending.type, QStringLiteral("board_not_initialized"),
               QStringLiteral("This project has no Board yet. The desktop is asking whether to "
                              "create one."));
        return;
    }
    // The bridge's own hand-off writes (the claim, the Verify note) answer no device: a refusal is
    // the desktop's to see, and the `board_action_result` has already gone.
    if (answers && type == QStringLiteral("error")
        && (pending.type == QStringLiteral("board_claim") || pending.type == QStringLiteral("board_verify_note")
            || pending.type == QStringLiteral("board_watch"))) {
        if (pending.type != QStringLiteral("board_watch"))   // the watch's refresh failing is nobody's news
            for (const Entry &entry : std::as_const(m_hosts))
                if (entry.owner == m_owner && entry.host.status)
                    entry.host.status(event.value(QStringLiteral("text")).toString());
        return;
    }
    if (!boardremote::eventForwarded(type, answers))
        return;
    // A refresh that found nothing new — the watch firing after a write whose `board_changed` has
    // already gone — is not news to a device. One a device asked for is still its answer.
    if (type == QStringLiteral("board_changed") && (!answers || pending.rid.isNull())
        && event.value(QStringLiteral("upserts")).toArray().isEmpty()
        && event.value(QStringLiteral("removed")).toArray().isEmpty())
        return;
    sendEvent(answers ? pending.rid : QJsonValue(QJsonValue::Null), boardremote::withoutPaths(event));
}

void BoardRemote::actionResult(const Pending &pending, bool ok, const QString &pane, const QString &message)
{
    if (!sendEvent || !remoteOn || !remoteOn())
        return;
    sendEvent(pending.rid, QJsonObject{{QStringLiteral("event"), QStringLiteral("board_action_result")},
                                       {QStringLiteral("id"), pending.card},
                                       {QStringLiteral("action"), pending.action},
                                       {QStringLiteral("ok"), ok},
                                       {QStringLiteral("pane"), pane},
                                       {QStringLiteral("message"), message}});
}

// Run (`r`) and Verify (`v`) for a device. The rules are CardDetail::execute() / verify() and
// the writes are BoardView::executeCard() / verifyCard() (src/BoardPane.cpp): the same task text,
// the same hook, the same hand-off message. What differs is only what a phone cannot do — there is
// no reply box whose text rides along as a note, and no second press to arm a card that has
// neither a plan nor an acceptance: the phone asks before it sends.
void BoardRemote::act(const Pending &pending, const QJsonObject &card)
{
    Entry *entry = nullptr;
    for (Entry &candidate : m_hosts)
        if (candidate.owner && candidate.owner == m_owner)
            entry = &candidate;
    if (!entry) {
        actionResult(pending, false, QString(), QStringLiteral("The desktop window has closed."));
        return;
    }
    const Host &host = entry->host;
    const QString id = card.value(QStringLiteral("card_id")).toString().isEmpty()
                           ? pending.card : card.value(QStringLiteral("card_id")).toString();
    const QJsonObject front = card.value(QStringLiteral("front")).toObject();
    const QString status = card.value(QStringLiteral("status")).toString().isEmpty()
                               ? front.value(QStringLiteral("status")).toString()
                               : card.value(QStringLiteral("status")).toString();
    const QString title = card.value(QStringLiteral("title")).toString();
    const QString session = front.value(QStringLiteral("session")).toString().trimmed();
    const bool sessionLive = !session.isEmpty() && host.paneExists && host.paneExists(session);
    const bool execute = pending.action == QStringLiteral("execute");
    const QString name = who(pending.device);

    // A pane already has the card (#48S3): the desktop's key reveals that pane instead of opening
    // a second one. A device gets the pane it can switch to.
    const bool held = execute ? (status == QStringLiteral("executing") || status == QStringLiteral("in-progress"))
                              : status.startsWith(QStringLiteral("needs-qa"));
    if (sessionLive && held) {
        actionResult(pending, true, session,
                     execute ? QStringLiteral("#%1 is already executing in a pane.").arg(id)
                             : QStringLiteral("#%1 is already being verified in a pane.").arg(id));
        return;
    }
    if (m_turns.contains(id)) {
        actionResult(pending, false, QString(),
                     QStringLiteral("The agent is still answering on #%1. Stop it, or wait for it, "
                                    "before handing the card to a pane.").arg(id));
        return;
    }

    if (execute) {
        if (!host.executeCard) {
            actionResult(pending, false, QString(),
                         QStringLiteral("This window cannot open a terminal pane for the card."));
            return;
        }
        bool hasPlan = false;
        const QJsonArray sections = card.value(QStringLiteral("sections")).toArray();
        for (const QJsonValue &heading : sections)
            if (heading.toString().compare(QStringLiteral("Plan"), Qt::CaseInsensitive) == 0)
                hasPlan = true;
        const bool hasAcceptance = !front.value(QStringLiteral("acceptance")).toString().trimmed().isEmpty();
        const QString token = host.executeCard(m_tab, id, board::executeTask(id, title, hasPlan, hasAcceptance));
        if (token.isEmpty()) {
            // Nothing is written: the desktop's no-pane fallback records a hand-off to a pane the
            // owner can at least see failed to open; a phone would be told "executing" about a
            // card nobody has.
            actionResult(pending, false, QString(),
                         QStringLiteral("The desktop could not open a terminal pane for #%1.").arg(id));
            return;
        }
        // The claim (19.19): assignee, the move to Executing, the progress entry naming the pane
        // and the card's `session`, in one write — what BoardView::executeCard sends.
        host.send(m_tab, QJsonObject{{QStringLiteral("type"), QStringLiteral("board_claim")},
                                     {QStringLiteral("id"), remember({QJsonValue(QJsonValue::Null),
                                                                      QStringLiteral("board_claim"),
                                                                      pending.device, id, QString()})},
                                     {QStringLiteral("card"), id},
                                     {QStringLiteral("pane_token"), token},
                                     {QStringLiteral("text"), QStringLiteral("Run pressed on %1.").arg(name)}});
        if (host.status)
            host.status(QStringLiteral("Run on #%1 from %2").arg(id, name));
        actionResult(pending, true, token, QStringLiteral("#%1 is executing in a new pane.").arg(id));
        return;
    }

    // Verify
    if (status != QStringLiteral("needs-verification") && !status.startsWith(QStringLiteral("needs-qa"))) {
        actionResult(pending, false, QString(),
                     QStringLiteral("#%1 has not landed for verification yet. Move it to Needs "
                                    "verification when it lands.").arg(id));
        return;
    }
    const QJsonObject qa = card.value(QStringLiteral("qa")).toObject();
    const QString runner = board::verifyRunner(qa);
    if (runner.isEmpty()) {
        const QString why = board::verifyLine(qa);
        actionResult(pending, false, QString(),
                     why.isEmpty() ? QStringLiteral("No verifier is available for #%1 yet.").arg(id) : why);
        return;
    }
    if (!host.verifyCard) {
        actionResult(pending, false, QString(),
                     QStringLiteral("This window cannot open a terminal pane for the card."));
        return;
    }
    const QString label = board::verifyLabel(qa);
    const QString why = qa.value(QStringLiteral("recommended")).toObject().value(QStringLiteral("why")).toString();
    QString implementedBy = qa.value(QStringLiteral("implemented_by")).toString();
    if (implementedBy.isEmpty())
        implementedBy = front.value(QStringLiteral("implemented_by")).toString();
    const QString token = host.verifyCard(m_tab, id, runner,
                                          board::verifyTask(id, title, label, implementedBy, status));
    if (token.isEmpty()) {
        actionResult(pending, false, QString(),
                     QStringLiteral("The desktop could not open a terminal pane for #%1.").arg(id));
        return;
    }
    // The hand-off note, worded as BoardView::verifyCard words it (#HKAP): the thread draws
    // "Verifying (xxxxxxxx)" as a link to the verifier's pane.
    QString text = QStringLiteral("Verifying (%1) · handed to a new terminal pane on %2").arg(token.left(8), label);
    if (!why.isEmpty())
        text += QStringLiteral(" · ") + why;
    text += QStringLiteral("\n\nVerify pressed on %1.").arg(name);
    host.send(m_tab, QJsonObject{{QStringLiteral("type"), QStringLiteral("board_comment")},
                                 {QStringLiteral("id"), remember({QJsonValue(QJsonValue::Null),
                                                                  QStringLiteral("board_verify_note"),
                                                                  pending.device, id, QString()})},
                                 {QStringLiteral("card"), id},
                                 {QStringLiteral("kind"), QStringLiteral("progress")},
                                 {QStringLiteral("pane_token"), token},
                                 {QStringLiteral("text"), text}});
    if (host.status)
        host.status(QStringLiteral("Verify on #%1 from %2").arg(id, name));
    actionResult(pending, true, token, QStringLiteral("#%1 is being verified on %2 in a new pane.").arg(id, label));
}

}  // namespace relay
