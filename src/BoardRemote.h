// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The Switchboard on the owner's phone (card #SWPH, step 1): the bridge between a window's
// `BoardWorker` and the remote sidecar.
//
// The phone draws what the desktop's board draws and never touches the repository: it sends
// `board_request {rid, request}` to the hub, the hub hands the desktop
//
//     {"t":"board_request","rid":n,"device":"<id>","name":"<device name>","request":{…}}
//
// and the desktop answers with `{"t":"board_event","rid":n|null,"event":{…}}` lines. The hub
// filters and sanitises too; this is the last line, so everything is checked again here:
//
//   * `request.type` against the card's allow-list, and any request carrying a path-like field
//     anywhere is refused — a device never names a file. A refused request never reaches a worker.
//   * the request is **rebuilt** field by field into the worker's own message (protocol 19.2–19.4,
//     19.16), so a field this file does not know cannot ride through.
//   * events go back only when the allow-list names them, and with every path field taken out.
//   * while remote control is off, nothing is forwarded and every request is refused.
//
// There is one worker per tab and the bridge never starts a second one for a board: a request is
// sent down the same `helperWorker(page, true)` a Switchboard pane uses, which starts the tab's
// worker on demand, configured exactly as the pane would have had it. The bridge **taps** that
// worker's `onEvent` (tap()); the window's own handler runs first and unchanged, so every board
// pane keeps receiving everything it did before.
//
// `board_action {id, action: "execute"|"verify"}` is GUI-level, as `x` and `v` are on the desktop
// (19.10): the bridge reads the card, builds the same task text (`board::executeTask` /
// `board::verifyTask`), opens the pane through the window's Execute / Verify hook, records the
// hand-off with the same worker message the board pane sends (`board_claim`, or the Verify progress
// note), and answers `board_action_result {id, action, ok, pane, message}`.
//
// Widget-free and RemoteShare-free on purpose: the window and RemoteShare are handed in as
// functions, so tests/boardremote_test.cpp drives the whole bridge with neither.
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>

namespace relay {

class BoardWorker;

namespace boardremote {

// The request types a device may send (the card's contract), and nothing else.
const QStringList &allowedRequests();
bool requestAllowed(const QString &type);
// Whether `value` carries a path-like key (`path`, `root`, `folder`, `file`, `dir`, `cwd`,
// `workspace`) at any depth. A request that does is refused whole.
bool carriesPath(const QJsonValue &value);

// One device request as the worker's own message. `error` non-empty means refused (with `code`),
// and `message` is then empty. `workerId` becomes the message's `id` — on the worker's wire `id`
// is the *request* id and the card travels as `card`, while the contract's `id` is the card.
struct Translated {
    QJsonObject message;
    QString error;
    QString code;
};
Translated toWorker(const QJsonObject &request, const QString &workerId, const QString &deviceName);

// Whether a worker event of this type goes to the devices. `answersRequest` is whether it carries
// the id of a request the bridge sent: `board_card`, `board_search` and `error` go back only as
// answers, never as a broadcast of what the desktop's own panes asked.
bool eventForwarded(const QString &type, bool answersRequest);
// The event as it leaves the desktop: every path-like key removed at any depth, and an absolute
// `project` reduced to the project's name.
QJsonObject withoutPaths(const QJsonObject &event);

// The line the desktop says when a device's write lands: "Card #K7Q2 moved to done from iPhone",
// "Comment on #K7Q2 from iPhone", "New card #K7Q2 from iPhone". Empty for anything else.
QString writeNotice(const QString &requestType, const QJsonObject &written, const QString &deviceName);

}  // namespace boardremote

class BoardRemote : public QObject {
public:
    explicit BoardRemote(QObject *parent = nullptr);
    // The application's bridge. Tests build their own.
    static BoardRemote &instance();

    // ---- the remote side (RemoteShare in the application) -------------------------------------
    // Whether remote control is on. Unset reads as off.
    std::function<bool()> remoteOn;
    // One `board_event` line: `rid` is the request's own `rid` value, or null for a broadcast.
    std::function<void(const QJsonValue &rid, const QJsonObject &event)> sendEvent;

    // ---- the window side ----------------------------------------------------------------------
    // A window that can serve a board. Every function is called on the GUI thread.
    struct Host {
        // Whether this is the window the owner last worked in.
        std::function<bool()> active;
        // The tab whose board a device works on — the current tab when it has a board, otherwise
        // the first one that has — as that tab's id; empty when this window has no board at all.
        std::function<QString()> boardTab;
        // Whether that tab still exists and still has a board.
        std::function<bool(const QString &tab)> hasBoard;
        // The tab's worker, started on demand exactly as a Switchboard pane starts it, handed one
        // message. False when the tab is gone.
        std::function<bool(const QString &tab, const QJsonObject &message)> send;
        // Execute and Verify: the desktop's own hooks (BoardView::onExecuteCard / onVerifyCard as
        // the window installs them). The new pane's session token, or empty when none opened.
        std::function<QString(const QString &tab, const QString &card, const QString &task)> executeCard;
        std::function<QString(const QString &tab, const QString &card, const QString &runner,
                              const QString &task)> verifyCard;
        // Whether a pane with this session token is still open (a claimed card's pane).
        std::function<bool(const QString &token)> paneExists;
        // One line for the window's status area.
        std::function<void(const QString &text)> status;
    };
    // `owner` is the window: the host goes when it is destroyed.
    void addHost(QObject *owner, Host host);

    // Fan the worker's events out to the bridge as well as to whoever has `onEvent` now. Call it
    // *after* assigning `worker->onEvent`: the handler in place is kept and runs first.
    void tap(BoardWorker *worker, QObject *owner, const QString &tab);
    // What tap() calls; public so that a test (or a worker with no `onEvent` of its own) can feed it.
    void workerEvent(QObject *owner, const QString &tab, const QJsonObject &event);

    // One `board_request` line from the sidecar, whole.
    void handleRequest(const QJsonObject &line);

    // The board a device is on: set by its `board_open`, kept until that tab goes.
    QString currentTab() const { return m_tab; }

private:
    struct Pending {
        QJsonValue rid;
        QString type;        // the contract's request type
        QString device;      // the device's display name
        QString card;
        QString action;      // board_action: "execute" or "verify", on the bridge's own card read
    };
    struct Entry {
        QPointer<QObject> owner;
        Host host;
    };

    Entry *hostFor(bool reopen);
    void refuse(const QJsonValue &rid, const QString &type, const QString &code, const QString &text);
    QString remember(const Pending &pending);
    void act(const Pending &pending, const QJsonObject &card);
    void actionResult(const Pending &pending, bool ok, const QString &pane, const QString &message);
    static QString who(const QString &deviceName);

    QList<Entry> m_hosts;
    QPointer<QObject> m_owner;       // the window whose tab `m_tab` is
    QString m_tab;
    QHash<QString, Pending> m_requests;   // by the worker-side request id
    QStringList m_order;                  // oldest first, to bound m_requests
    QSet<QString> m_turns;                // cards with a Discuss or Plan running on this board
    quint64 m_seq = 0;
};

}  // namespace relay
