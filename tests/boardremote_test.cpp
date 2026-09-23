// SPDX-License-Identifier: AGPL-3.0-or-later
// The Switchboard on the owner's phone, desktop half (src/BoardRemote.h, card #SWPH): what a device
// may ask, what each request becomes on the worker's wire, which events go back and under which
// `rid`, that the board pane still gets every event, that Execute and Verify go through the
// window's hooks, and that all of it is refused while remote control is off.
//
// The window and RemoteShare are functions to the bridge, so this drives it with neither.
#include "BoardRemote.h"
#include "BoardWorker.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using relay::BoardRemote;
namespace br = relay::boardremote;

namespace {

struct Sent {
    QJsonValue rid;
    QJsonObject event;
};

// A window with one tab ("tab-1") that has a board, everything it was asked recorded.
struct Rig {
    BoardRemote bridge;
    QObject window;
    bool remote = true;
    bool hasBoard = true;
    QList<QJsonObject> toWorker;
    QList<Sent> toHub;
    QStringList status;
    QStringList executed, verified;
    QString paneToken = QStringLiteral("pane-token-0123456789");
    QStringList livePanes;

    Rig()
    {
        bridge.remoteOn = [this] { return remote; };
        bridge.sendEvent = [this](const QJsonValue &rid, const QJsonObject &event) {
            toHub.append({rid, event});
        };
        BoardRemote::Host host;
        host.active = [] { return true; };
        host.boardTab = [this] { return hasBoard ? QStringLiteral("tab-1") : QString(); };
        host.hasBoard = [this](const QString &tab) { return hasBoard && tab == QStringLiteral("tab-1"); };
        host.send = [this](const QString &tab, const QJsonObject &message) {
            if (tab != QStringLiteral("tab-1")) return false;
            toWorker.append(message);
            return true;
        };
        host.executeCard = [this](const QString &, const QString &card, const QString &task) {
            executed << card + QLatin1Char('|') + task;
            return paneToken;
        };
        host.verifyCard = [this](const QString &, const QString &card, const QString &runner, const QString &task) {
            verified << card + QLatin1Char('|') + runner + QLatin1Char('|') + task;
            return paneToken;
        };
        host.paneExists = [this](const QString &token) { return livePanes.contains(token); };
        host.status = [this](const QString &text) { status << text; };
        bridge.addHost(&window, host);
    }

    void request(int rid, QJsonObject request, const QString &name = QStringLiteral("iPhone"))
    {
        bridge.handleRequest({{"t", "board_request"}, {"rid", rid}, {"device", "dev-1"},
                              {"name", name}, {"request", request}});
    }
    void event(QJsonObject event) { bridge.workerEvent(&window, QStringLiteral("tab-1"), event); }
    QString lastWorkerId() const { return toWorker.last().value("id").toString(); }
};

QJsonObject without(QJsonObject object, const QString &key)
{
    object.remove(key);
    return object;
}

}  // namespace

class BoardRemoteTests : public QObject {
    Q_OBJECT

private slots:
    void theAllowListIsTheCardsAndNothingElse();
    void aRefusedRequestNeverReachesTheWorker_data();
    void aRefusedRequestNeverReachesTheWorker();
    void eachRequestBecomesTheWorkersOwnMessage_data();
    void eachRequestBecomesTheWorkersOwnMessage();
    void unknownFieldsDoNotRideThrough();
    void anAnswerCarriesItsRidAndABroadcastCarriesNull();
    void onlyTheAllowListedEventsGoBackAndWithoutPaths();
    void theDesktopsOwnCardReadsAreNotBroadcast();
    void thePaneStillGetsEveryEvent();
    void aDeviceWriteIsSaidOnTheDesktop();
    void executeGoesThroughTheWindowsHookAndClaimsTheCard();
    void verifyGoesThroughTheWindowsHookOnTheRecommendedRunner();
    void executeIsRefusedMidTurnAndRevealsAPaneThatHasTheCard();
    void aCardTurnsFailureReachesTheDeviceThatAsked();
    void everythingIsRefusedWhileRemoteControlIsOff();
    void noBoardOnTheDesktopIsSaid();
    void aDeviceStaysOnTheBoardItOpened();
    void theBoardIsWatchedWhenNoPaneWatchesIt();
};

void BoardRemoteTests::theAllowListIsTheCardsAndNothingElse()
{
    // Eleven since card #7JD1: `board_resume` joined `board_cancel`, because a device that can
    // stop a card's turn has to be able to start its queue again without the desktop's button.
    const QStringList expected{"board_open", "board_refresh", "board_card_get", "board_search",
                               "board_comment", "board_move", "board_create", "board_ask",
                               "board_cancel", "board_resume", "board_action"};
    QCOMPARE(br::allowedRequests(), expected);
    for (const char *never : {"board_delete", "board_folder", "board_folder_rename", "board_cleanup",
                              "board_claim", "set_board", "board_init", "board_init_answer",
                              "board_import_propose", "board_import_apply", "board_sync",
                              "board_update", "board_priority", "board_undo", "board_sections",
                              "configure", "ask", "shutdown", ""})
        QVERIFY2(!br::requestAllowed(QString::fromLatin1(never)), never);
}

void BoardRemoteTests::aRefusedRequestNeverReachesTheWorker_data()
{
    QTest::addColumn<QJsonObject>("request");
    QTest::newRow("board_delete") << QJsonObject{{"type", "board_delete"}, {"id", "K7Q2"}};
    QTest::newRow("board_claim") << QJsonObject{{"type", "board_claim"}, {"id", "K7Q2"}};
    QTest::newRow("set_board") << QJsonObject{{"type", "set_board"}};
    QTest::newRow("no type") << QJsonObject{{"id", "K7Q2"}};
    QTest::newRow("a path") << QJsonObject{{"type", "board_card_get"}, {"id", "K7Q2"}, {"path", "/etc/passwd"}};
    QTest::newRow("a nested path") << QJsonObject{{"type", "board_create"}, {"title", "x"},
                                                  {"labels", QJsonArray{QJsonObject{{"file", "a"}}}}};
    QTest::newRow("a root") << QJsonObject{{"type", "board_open"}, {"root", "/home/someone"}};
    QTest::newRow("a workspace") << QJsonObject{{"type", "board_open"}, {"Workspace", "/home/someone"}};
    QTest::newRow("an id that is a path") << QJsonObject{{"type", "board_card_get"}, {"id", "../../x"}};
    QTest::newRow("an empty comment") << QJsonObject{{"type", "board_comment"}, {"id", "K7Q2"}, {"text", "  "}};
    QTest::newRow("a move to nowhere") << QJsonObject{{"type", "board_move"}, {"id", "K7Q2"}};
    QTest::newRow("a cleanup mode") << QJsonObject{{"type", "board_ask"}, {"id", "K7Q2"}, {"text", "x"}, {"mode", "cleanup"}};
    QTest::newRow("cancel everything") << QJsonObject{{"type", "board_cancel"}};
    // `board_resume` names one card's queue, like every other op that names a queue (#7JD1).
    QTest::newRow("resume everything") << QJsonObject{{"type", "board_resume"}};
    QTest::newRow("an unknown action") << QJsonObject{{"type", "board_action"}, {"id", "K7Q2"}, {"action", "delete"}};
}

void BoardRemoteTests::aRefusedRequestNeverReachesTheWorker()
{
    QFETCH(QJsonObject, request);
    Rig rig;
    rig.request(7, request);
    QVERIFY(rig.toWorker.isEmpty());
    QCOMPARE(rig.toHub.size(), 1);
    QCOMPARE(rig.toHub.first().rid.toInt(), 7);
    QCOMPARE(rig.toHub.first().event.value("event").toString(), QStringLiteral("error"));
    QCOMPARE(rig.toHub.first().event.value("code").toString(), QStringLiteral("board_refused"));
    QVERIFY(!rig.toHub.first().event.value("text").toString().isEmpty());
}

void BoardRemoteTests::eachRequestBecomesTheWorkersOwnMessage_data()
{
    QTest::addColumn<QJsonObject>("request");
    QTest::addColumn<QJsonObject>("expected");   // without the bridge's own `id`
    QTest::newRow("board_open") << QJsonObject{{"type", "board_open"}}
                                << QJsonObject{{"type", "board_open"}};
    QTest::newRow("board_refresh") << QJsonObject{{"type", "board_refresh"}}
                                   << QJsonObject{{"type", "board_refresh"}};
    QTest::newRow("board_card_get") << QJsonObject{{"type", "board_card_get"}, {"id", "#k7q2"}}
                                    << QJsonObject{{"type", "board_card_get"}, {"card", "K7Q2"}};
    QTest::newRow("board_search") << QJsonObject{{"type", "board_search"}, {"query", "phone"}}
                                  << QJsonObject{{"type", "board_search"}, {"query", "phone"}};
    QTest::newRow("board_comment") << QJsonObject{{"type", "board_comment"}, {"id", "K7Q2"}, {"text", "is it done?"}, {"kind", "question"}}
                                   << QJsonObject{{"type", "board_comment"}, {"card", "K7Q2"}, {"text", "is it done?"}, {"kind", "question"}};
    // `note` is what the desktop's reply box sends; the worker has no `comment` kind.
    QTest::newRow("no kind is a note") << QJsonObject{{"type", "board_comment"}, {"id", "K7Q2"}, {"text", "x"}}
                                       << QJsonObject{{"type", "board_comment"}, {"card", "K7Q2"}, {"text", "x"}, {"kind", "note"}};
    QTest::newRow("a machine's kind is a note") << QJsonObject{{"type", "board_comment"}, {"id", "K7Q2"}, {"text", "x"}, {"kind", "evidence"}}
                                                << QJsonObject{{"type", "board_comment"}, {"card", "K7Q2"}, {"text", "x"}, {"kind", "note"}};
    // The worker refuses a decision that quotes nobody; the owner's own typing is the quote.
    QTest::newRow("a decision is the owner's words") << QJsonObject{{"type", "board_comment"}, {"id", "K7Q2"}, {"text", "yes, do it"}, {"kind", "decision"}}
                                                     << QJsonObject{{"type", "board_comment"}, {"card", "K7Q2"}, {"kind", "decision"},
                                                                    {"text", QString::fromUtf8("owner, from iPhone: \u201cyes, do it\u201d")}};
    QTest::newRow("a decision already quoted") << QJsonObject{{"type", "board_comment"}, {"id", "K7Q2"}, {"text", "I said \"cloud is fine\""}, {"kind", "decision"}}
                                               << QJsonObject{{"type", "board_comment"}, {"card", "K7Q2"}, {"text", "I said \"cloud is fine\""}, {"kind", "decision"}};
    QTest::newRow("a decision too short to quote") << QJsonObject{{"type", "board_comment"}, {"id", "K7Q2"}, {"text", "ok"}, {"kind", "decision"}}
                                                   << QJsonObject{{"type", "board_comment"}, {"card", "K7Q2"}, {"text", "ok"}, {"kind", "note"}};
    QTest::newRow("board_move") << QJsonObject{{"type", "board_move"}, {"id", "K7Q2"}, {"status", "done"}, {"reason", "shipped"}}
                                << QJsonObject{{"type", "board_move"}, {"card", "K7Q2"}, {"status", "done"}, {"reason", "shipped (from iPhone)"}};
    QTest::newRow("board_move, no reason") << QJsonObject{{"type", "board_move"}, {"id", "K7Q2"}, {"status", "planned"}}
                                           << QJsonObject{{"type", "board_move"}, {"card", "K7Q2"}, {"status", "planned"}, {"reason", "moved from iPhone"}};
    QTest::newRow("board_create") << QJsonObject{{"type", "board_create"}, {"tab", "features"}, {"title", "A title"},
                                                 {"request", "The words I dictated."}, {"labels", QJsonArray{"remote", "phone"}}}
                                  << QJsonObject{{"type", "board_create"}, {"tab", "features"}, {"status", "inbox"},
                                                 {"title", "A title"}, {"text", "The words I dictated."},
                                                 {"labels", QJsonArray{"remote", "phone"}}, {"source", "remote: iPhone"}};
    QTest::newRow("board_create, words only") << QJsonObject{{"type", "board_create"}, {"request", "just this"}}
                                              << QJsonObject{{"type", "board_create"}, {"status", "inbox"}, {"text", "just this"},
                                                             {"source", "remote: iPhone"}};
    QTest::newRow("board_ask discuss") << QJsonObject{{"type", "board_ask"}, {"id", "K7Q2"}, {"text", "why?"}, {"mode", "discuss"}}
                                       << QJsonObject{{"type", "board_ask"}, {"card", "K7Q2"}, {"text", "why?"}, {"mode", "discuss"}};
    QTest::newRow("board_ask plan, no words") << QJsonObject{{"type", "board_ask"}, {"id", "K7Q2"}, {"mode", "plan"}}
                                              << QJsonObject{{"type", "board_ask"}, {"card", "K7Q2"}, {"text", ""}, {"mode", "plan"}};
    QTest::newRow("board_cancel") << QJsonObject{{"type", "board_cancel"}, {"id", "K7Q2"}}
                                  << QJsonObject{{"type", "board_cancel"}, {"card", "K7Q2"}};
    // The empty send from a card view: the queue that Stop paused runs again (#7JD1).
    QTest::newRow("board_resume") << QJsonObject{{"type", "board_resume"}, {"id", "K7Q2"}}
                                  << QJsonObject{{"type", "board_resume"}, {"card", "K7Q2"}};
}

void BoardRemoteTests::eachRequestBecomesTheWorkersOwnMessage()
{
    QFETCH(QJsonObject, request);
    QFETCH(QJsonObject, expected);
    Rig rig;
    rig.request(3, request);
    QVERIFY2(rig.toHub.isEmpty(), QJsonDocument(rig.toHub.value(0).event).toJson().constData());
    QCOMPARE(rig.toWorker.size(), 1);
    // `id` on the worker's wire is the request id, and it is the bridge's own, never the device's.
    QVERIFY(rig.lastWorkerId().startsWith(QStringLiteral("remote-")));
    QCOMPARE(without(rig.toWorker.first(), QStringLiteral("id")), expected);
}

void BoardRemoteTests::unknownFieldsDoNotRideThrough()
{
    Rig rig;
    rig.request(1, {{"type", "board_move"}, {"id", "K7Q2"}, {"status", "done"}, {"override", "because"},
                    {"evidence", "trust me"}, {"author", "agent"}, {"before", "AAAA"}, {"force", true}});
    QCOMPARE(rig.toWorker.size(), 1);
    QStringList keys = rig.toWorker.first().keys();
    keys.sort();
    QCOMPARE(keys, (QStringList{"card", "id", "reason", "status", "type"}));
}

void BoardRemoteTests::anAnswerCarriesItsRidAndABroadcastCarriesNull()
{
    Rig rig;
    rig.request(41, {{"type", "board_open"}});
    const QString id = rig.lastWorkerId();
    rig.event({{"event", "board"}, {"id", id}, {"rev", 1}, {"cards", QJsonArray{QJsonObject{{"id", "K7Q2"}}}}, {"more", true}});
    rig.event({{"event", "board_cards"}, {"id", id}, {"cards", QJsonArray{}}, {"more", false}});
    rig.event({{"event", "board_changed"}, {"rev", 2}, {"upserts", QJsonArray{QJsonObject{{"id", "K7Q2"}}}}});
    QCOMPARE(rig.toHub.size(), 3);
    QCOMPARE(rig.toHub.at(0).rid.toInt(), 41);
    QCOMPARE(rig.toHub.at(0).event.value("event").toString(), QStringLiteral("board"));
    QCOMPARE(rig.toHub.at(0).event.value("cards").toArray().size(), 1);
    QCOMPARE(rig.toHub.at(1).rid.toInt(), 41);           // every batch of the same answer
    QVERIFY(rig.toHub.at(2).rid.isNull());                // nobody asked for this one

    rig.request(42, {{"type", "board_move"}, {"id", "K7Q2"}, {"status", "nowhere"}});
    rig.event({{"event", "error"}, {"id", rig.lastWorkerId()}, {"code", "board_refused"}, {"text", "no such status"}});
    QCOMPARE(rig.toHub.last().rid.toInt(), 42);
    QCOMPARE(rig.toHub.last().event.value("text").toString(), QStringLiteral("no such status"));
}

void BoardRemoteTests::onlyTheAllowListedEventsGoBackAndWithoutPaths()
{
    Rig rig;
    rig.request(1, {{"type", "board_open"}});
    rig.toHub.clear();
    for (const char *type : {"configured", "presets", "model_roles", "delta", "thinking_delta", "tool_started",
                             "tool_result", "status", "app_command", "board_problems", "board_init_request",
                             "signals_changed", "tests_run", "key_tested", "board_folder_changed", "board_undone"})
        rig.event({{"event", QString::fromLatin1(type)}, {"card_id", "K7Q2"}});
    rig.event({{"event", "error"}, {"text", "a pane's own refused write"}, {"id", "sb1-4"}});
    QVERIFY(rig.toHub.isEmpty());

    for (const char *type : {"board", "board_cards", "board_changed", "board_thread_appended", "board_written",
                             "board_activity", "board_busy", "board_conflict", "board_cancelled"})
        rig.event({{"event", QString::fromLatin1(type)}, {"removed", QJsonArray{"AAAA"}}});
    QCOMPARE(rig.toHub.size(), 9);
    // `board_chat_started` and `board_chat_queued` were in that list until card #AGNT retired
    // them from the wire; a `board_chat_*` prefix rule forwarded anything else of that shape and
    // is gone with them. Nothing named `board_chat` reaches a device now.
    rig.toHub.clear();
    rig.event({{"event", "board_chat_started"}, {"removed", QJsonArray{"AAAA"}}});
    rig.event({{"event", "board_chat_queued"}, {"removed", QJsonArray{"AAAA"}}});
    QVERIFY(rig.toHub.isEmpty());
    // A refresh that found nothing is not news — unless a device asked for it.
    rig.toHub.clear();
    rig.event({{"event", "board_changed"}, {"rev", 4}, {"upserts", QJsonArray{}}, {"removed", QJsonArray{}}});
    QVERIFY(rig.toHub.isEmpty());
    rig.request(2, {{"type", "board_refresh"}});
    rig.event({{"event", "board_changed"}, {"id", rig.lastWorkerId()}, {"rev", 4}, {"upserts", QJsonArray{}}});
    QCOMPARE(rig.toHub.size(), 1);
    QCOMPARE(rig.toHub.first().rid.toInt(), 2);

    rig.toHub.clear();
    rig.event({{"event", "board"}, {"root", "/home/o/p/issues"}, {"workspace", "/home/o/p"}, {"project", "/home/o/p"},
               {"cards", QJsonArray{QJsonObject{{"id", "K7Q2"}, {"path", "features/x.md"}, {"title", "T"}}}},
               {"config", QJsonObject{{"tabs", QJsonArray{QJsonObject{{"id", "features"}, {"folder", "features"}}}}}}});
    QCOMPARE(rig.toHub.size(), 1);
    const QJsonObject out = rig.toHub.first().event;
    QVERIFY(!br::carriesPath(out));
    QCOMPARE(out.value("project").toString(), QStringLiteral("p"));
    QCOMPARE(out.value("cards").toArray().first().toObject().value("title").toString(), QStringLiteral("T"));
    QCOMPARE(out.value("config").toObject().value("tabs").toArray().first().toObject().value("id").toString(),
             QStringLiteral("features"));
    QVERIFY(!QJsonDocument(out).toJson().contains("/home/o"));
}

void BoardRemoteTests::theDesktopsOwnCardReadsAreNotBroadcast()
{
    Rig rig;
    rig.request(1, {{"type", "board_open"}});
    rig.toHub.clear();
    rig.event({{"event", "board_card"}, {"id", "sb1f-9"}, {"card_id", "K7Q2"}, {"body", "…"}});
    rig.event({{"event", "board_search"}, {"id", "sb1f-10"}, {"ids", QJsonArray{"K7Q2"}}});
    QVERIFY(rig.toHub.isEmpty());
    rig.request(2, {{"type", "board_card_get"}, {"id", "K7Q2"}});
    rig.event({{"event", "board_card"}, {"id", rig.lastWorkerId()}, {"card_id", "K7Q2"}, {"path", "/x/y.md"}, {"body", "b"}});
    rig.request(3, {{"type", "board_search"}, {"query", "b"}});
    rig.event({{"event", "board_search"}, {"id", rig.lastWorkerId()}, {"ids", QJsonArray{"K7Q2"}}});
    QCOMPARE(rig.toHub.size(), 2);
    QCOMPARE(rig.toHub.at(0).rid.toInt(), 2);
    QVERIFY(!rig.toHub.at(0).event.contains("path"));
    QCOMPARE(rig.toHub.at(1).rid.toInt(), 3);
}

void BoardRemoteTests::thePaneStillGetsEveryEvent()
{
    Rig rig;
    relay::BoardWorker worker(QStringLiteral("python3"), QStringLiteral("/nonexistent"));
    QList<QJsonObject> pane;
    worker.onEvent = [&pane](const QJsonObject &event) { pane.append(event); };
    rig.bridge.tap(&worker, &rig.window, QStringLiteral("tab-1"));

    rig.request(5, {{"type", "board_open"}});
    const QList<QJsonObject> events{
        {{"event", "board"}, {"id", rig.lastWorkerId()}, {"root", "/r"}, {"cards", QJsonArray{}}},
        {{"event", "configured"}, {"model", "m"}},
        {{"event", "delta"}, {"card_id", "K7Q2"}, {"text", "streaming"}},
        {{"event", "board_changed"}, {"rev", 2}, {"upserts", QJsonArray{QJsonObject{{"id", "K7Q2"}}}}}};
    for (const QJsonObject &event : events)
        worker.onEvent(event);
    QCOMPARE(pane, events);                       // all of them, untouched — paths and all
    QCOMPARE(rig.toHub.size(), 2);                // and the two a device may see
    QCOMPARE(rig.toHub.at(0).rid.toInt(), 5);
    QVERIFY(rig.toHub.at(1).rid.isNull());

    // With remote control off the pane is none the wiser, and the hub hears nothing.
    rig.remote = false;
    rig.toHub.clear();
    pane.clear();
    worker.onEvent(events.last());
    QCOMPARE(pane.size(), 1);
    QVERIFY(rig.toHub.isEmpty());

    // A worker nobody listens to can be tapped too.
    relay::BoardWorker bare(QStringLiteral("python3"), QStringLiteral("/nonexistent"));
    rig.bridge.tap(&bare, &rig.window, QStringLiteral("tab-1"));
    rig.remote = true;
    bare.onEvent(events.last());
    QCOMPARE(rig.toHub.size(), 1);
}

void BoardRemoteTests::aDeviceWriteIsSaidOnTheDesktop()
{
    Rig rig;
    rig.request(1, {{"type", "board_open"}});
    rig.request(2, {{"type", "board_move"}, {"id", "K7Q2"}, {"status", "done"}}, QStringLiteral("Elliott's iPhone"));
    rig.event({{"event", "board_written"}, {"id", rig.lastWorkerId()}, {"kind", "board_move"}, {"card_id", "K7Q2"}, {"status", "done"}});
    rig.request(3, {{"type", "board_comment"}, {"id", "K7Q2"}, {"text", "ok"}}, QStringLiteral("iPad"));
    rig.event({{"event", "board_written"}, {"id", rig.lastWorkerId()}, {"kind", "board_comment"}, {"card_id", "K7Q2"}});
    rig.request(4, {{"type", "board_create"}, {"title", "New thing"}}, QString());
    rig.event({{"event", "board_written"}, {"id", rig.lastWorkerId()}, {"kind", "board_create"}, {"card_id", "M3XJ"}});
    // The desktop's own write says nothing here: the board pane has its own notice for it.
    rig.event({{"event", "board_written"}, {"id", "sb1f-3"}, {"kind", "board_move"}, {"card_id", "AAAA"}, {"status", "done"}});
    QCOMPARE(rig.status, (QStringList{"Card #K7Q2 moved to Done from Elliott's iPhone",
                                      "Comment on #K7Q2 from iPad",
                                      "New card #M3XJ from a paired device"}));
}

void BoardRemoteTests::executeGoesThroughTheWindowsHookAndClaimsTheCard()
{
    Rig rig;
    rig.request(1, {{"type", "board_open"}});
    rig.toWorker.clear();
    rig.toHub.clear();
    rig.request(9, {{"type", "board_action"}, {"id", "K7Q2"}, {"action", "execute"}});
    // First the card is read, as `x` on a card that is not open reads it …
    QCOMPARE(rig.toWorker.size(), 1);
    QCOMPARE(without(rig.toWorker.first(), "id"), (QJsonObject{{"type", "board_card_get"}, {"card", "K7Q2"}}));
    QVERIFY(rig.executed.isEmpty());
    rig.event({{"event", "board_card"}, {"id", rig.lastWorkerId()}, {"card_id", "K7Q2"}, {"title", "The title"},
               {"status", "planned"}, {"hash", "h1"}, {"sections", QJsonArray{"Issue", "Plan"}},
               {"front", QJsonObject{{"acceptance", "it works"}}}});
    // … then the window's Execute hook opens the pane, with the desktop's own task text …
    QCOMPARE(rig.executed.size(), 1);
    QVERIFY(rig.executed.first().startsWith(QStringLiteral("K7Q2|")));
    QVERIFY(rig.executed.first().contains(QStringLiteral("K7Q2")));
    QVERIFY(rig.executed.first().size() > 40);
    // … the card is claimed for that pane in one write (19.19) …
    QCOMPARE(rig.toWorker.size(), 2);
    const QJsonObject claim = rig.toWorker.last();
    QCOMPARE(claim.value("type").toString(), QStringLiteral("board_claim"));
    QCOMPARE(claim.value("card").toString(), QStringLiteral("K7Q2"));
    QCOMPARE(claim.value("pane_token").toString(), rig.paneToken);
    QVERIFY(claim.value("text").toString().contains(QStringLiteral("iPhone")));
    // … and the device is told, and the bridge's own card read went no further than the bridge.
    QCOMPARE(rig.toHub.size(), 1);
    QCOMPARE(rig.toHub.first().rid.toInt(), 9);
    QCOMPARE(rig.toHub.first().event, (QJsonObject{{"event", "board_action_result"}, {"id", "K7Q2"}, {"action", "execute"},
                                                   {"ok", true}, {"pane", rig.paneToken},
                                                   {"message", "#K7Q2 is executing in a new pane."}}));
    QCOMPARE(rig.status, (QStringList{"Execute on #K7Q2 from iPhone"}));
    // The claim's `board_written` is a broadcast like any other write.
    rig.event({{"event", "board_written"}, {"id", rig.lastWorkerId()}, {"kind", "board_claim"}, {"card_id", "K7Q2"}});
    QCOMPARE(rig.toHub.size(), 2);
    QVERIFY(rig.toHub.last().rid.isNull());

    // No pane could be opened: said, and nothing is written about a hand-off that did not happen.
    rig.paneToken.clear();
    rig.toWorker.clear();
    rig.request(10, {{"type", "board_action"}, {"id", "K7Q2"}, {"action", "execute"}});
    rig.event({{"event", "board_card"}, {"id", rig.lastWorkerId()}, {"card_id", "K7Q2"}, {"status", "planned"}});
    QCOMPARE(rig.toWorker.size(), 1);
    QCOMPARE(rig.toHub.last().rid.toInt(), 10);
    QCOMPARE(rig.toHub.last().event.value("ok").toBool(), false);
    QVERIFY(!rig.toHub.last().event.value("message").toString().isEmpty());

    // A card that does not exist: the worker's refusal is the result.
    rig.request(11, {{"type", "board_action"}, {"id", "ZZZZ"}, {"action", "execute"}});
    rig.event({{"event", "error"}, {"id", rig.lastWorkerId()}, {"text", "no card #ZZZZ on this board."}});
    QCOMPARE(rig.toHub.last().event.value("event").toString(), QStringLiteral("board_action_result"));
    QCOMPARE(rig.toHub.last().event.value("ok").toBool(), false);
    QCOMPARE(rig.toHub.last().event.value("message").toString(), QStringLiteral("no card #ZZZZ on this board."));
}

void BoardRemoteTests::verifyGoesThroughTheWindowsHookOnTheRecommendedRunner()
{
    Rig rig;
    rig.request(1, {{"type", "board_open"}});
    rig.toWorker.clear();
    rig.toHub.clear();
    const QJsonObject qa{{"implemented_by", "anthropic/claude-opus-5-5"},
                         {"recommended", QJsonObject{{"runner", "guest:codex"}, {"label", "Codex"},
                                                     {"available", true}, {"why", "a different family"}}}};
    rig.request(12, {{"type", "board_action"}, {"id", "K7Q2"}, {"action", "verify"}});
    rig.event({{"event", "board_card"}, {"id", rig.lastWorkerId()}, {"card_id", "K7Q2"}, {"title", "T"},
               {"status", "needs-verification"}, {"qa", qa}});
    QCOMPARE(rig.verified.size(), 1);
    QVERIFY(rig.verified.first().startsWith(QStringLiteral("K7Q2|guest:codex|")));
    const QJsonObject note = rig.toWorker.last();
    QCOMPARE(note.value("type").toString(), QStringLiteral("board_comment"));
    QCOMPARE(note.value("kind").toString(), QStringLiteral("progress"));
    QCOMPARE(note.value("pane_token").toString(), rig.paneToken);
    QVERIFY(note.value("text").toString().startsWith(QStringLiteral("Verifying (pane-tok) · handed to a new terminal pane on ")));
    QCOMPARE(rig.toHub.last().rid.toInt(), 12);
    QCOMPARE(rig.toHub.last().event.value("ok").toBool(), true);
    QCOMPARE(rig.toHub.last().event.value("action").toString(), QStringLiteral("verify"));
    QCOMPARE(rig.toHub.last().event.value("pane").toString(), rig.paneToken);

    // Not landed yet, and no verifier: both are said, and no pane opens.
    rig.verified.clear();
    rig.request(13, {{"type", "board_action"}, {"id", "K7Q2"}, {"action", "verify"}});
    rig.event({{"event", "board_card"}, {"id", rig.lastWorkerId()}, {"card_id", "K7Q2"}, {"status", "executing"}, {"qa", qa}});
    QCOMPARE(rig.toHub.last().event.value("ok").toBool(), false);
    QVERIFY(rig.toHub.last().event.value("message").toString().contains(QStringLiteral("not landed")));
    rig.request(14, {{"type", "board_action"}, {"id", "K7Q2"}, {"action", "verify"}});
    rig.event({{"event", "board_card"}, {"id", rig.lastWorkerId()}, {"card_id", "K7Q2"}, {"status", "needs-qa-llm"}});
    QCOMPARE(rig.toHub.last().event.value("ok").toBool(), false);
    QVERIFY(rig.verified.isEmpty());
}

void BoardRemoteTests::executeIsRefusedMidTurnAndRevealsAPaneThatHasTheCard()
{
    Rig rig;
    rig.request(1, {{"type", "board_open"}});
    rig.event({{"event", "agent_started"}, {"card_id", "K7Q2"}, {"mode", "plan"}});
    rig.request(2, {{"type", "board_action"}, {"id", "K7Q2"}, {"action", "execute"}});
    rig.event({{"event", "board_card"}, {"id", rig.lastWorkerId()}, {"card_id", "K7Q2"}, {"status", "planning"}});
    QVERIFY(rig.executed.isEmpty());
    QCOMPARE(rig.toHub.last().event.value("ok").toBool(), false);
    QVERIFY(rig.toHub.last().event.value("message").toString().contains(QStringLiteral("still answering")));

    rig.event({{"event", "done"}, {"card_id", "K7Q2"}, {"mode", "plan"}});
    rig.livePanes << QStringLiteral("live-pane-token");
    rig.request(3, {{"type", "board_action"}, {"id", "K7Q2"}, {"action", "execute"}});
    rig.event({{"event", "board_card"}, {"id", rig.lastWorkerId()}, {"card_id", "K7Q2"}, {"status", "executing"},
               {"front", QJsonObject{{"session", "live-pane-token"}}}});
    QVERIFY(rig.executed.isEmpty());                      // no second pane (#48S3)
    QCOMPARE(rig.toHub.last().event.value("ok").toBool(), true);
    QCOMPARE(rig.toHub.last().event.value("pane").toString(), QStringLiteral("live-pane-token"));
}

void BoardRemoteTests::aCardTurnsFailureReachesTheDeviceThatAsked()
{
    Rig rig;
    rig.request(1, {{"type", "board_open"}});
    rig.request(2, {{"type", "board_ask"}, {"id", "K7Q2"}, {"text", "why?"}});
    QCOMPARE(rig.status, (QStringList{"Discuss on #K7Q2 from iPhone"}));
    rig.toHub.clear();
    // The turn's own events carry the turn id, not the ask's.
    rig.event({{"event", "error"}, {"id", "t-14"}, {"card_id", "K7Q2"}, {"mode", "discuss"}, {"text", "provider HTTP 401"}});
    QCOMPARE(rig.toHub.size(), 1);
    QCOMPARE(rig.toHub.first().rid.toInt(), 2);
    // A turn the desktop started on another card is the desktop's.
    rig.event({{"event", "error"}, {"id", "t-15"}, {"card_id", "M3XJ"}, {"text", "x"}});
    QCOMPARE(rig.toHub.size(), 1);

    // `board_resumed` answers the device that asked and nobody else (#7JD1): a resume changes a
    // queue, and no device is sent a queue's state that would have to be brought up to date.
    rig.toHub.clear();
    rig.event({{"event", "board_resumed"}, {"card_id", "K7Q2"}, {"resumed", true}});
    QVERIFY(rig.toHub.isEmpty());
    rig.request(11, {{"type", "board_resume"}, {"id", "K7Q2"}});
    rig.toHub.clear();
    rig.event({{"event", "board_resumed"}, {"id", rig.lastWorkerId()}, {"card_id", "K7Q2"}, {"resumed", true}});
    QCOMPARE(rig.toHub.size(), 1);
    QCOMPARE(rig.toHub.first().rid.toInt(), 11);
    QCOMPARE(rig.toHub.first().event.value("resumed").toBool(), true);
}

void BoardRemoteTests::everythingIsRefusedWhileRemoteControlIsOff()
{
    Rig rig;
    rig.request(1, {{"type", "board_open"}});
    rig.remote = false;
    rig.toWorker.clear();
    rig.toHub.clear();
    for (const QString &type : br::allowedRequests())
        rig.request(8, {{"type", type}, {"id", "K7Q2"}, {"text", "x"}, {"status", "done"}, {"title", "t"}, {"action", "execute"}});
    QVERIFY(rig.toWorker.isEmpty());
    QVERIFY(rig.executed.isEmpty());
    QCOMPARE(rig.toHub.size(), br::allowedRequests().size());
    for (const Sent &sent : std::as_const(rig.toHub)) {
        QCOMPARE(sent.event.value("event").toString(), QStringLiteral("error"));
        QCOMPARE(sent.event.value("code").toString(), QStringLiteral("remote_off"));
        QCOMPARE(sent.rid.toInt(), 8);
    }
    rig.toHub.clear();
    rig.event({{"event", "board_changed"}, {"rev", 3}, {"upserts", QJsonArray{QJsonObject{{"id", "K7Q2"}}}}});
    rig.event({{"event", "board"}, {"cards", QJsonArray{}}});
    QVERIFY(rig.toHub.isEmpty());

    // A bridge nobody wired is off too.
    BoardRemote bare;
    QList<QJsonObject> said;
    bare.sendEvent = [&said](const QJsonValue &, const QJsonObject &event) { said.append(event); };
    bare.handleRequest({{"t", "board_request"}, {"rid", 1}, {"request", QJsonObject{{"type", "board_open"}}}});
    QCOMPARE(said.size(), 1);
    QCOMPARE(said.first().value("code").toString(), QStringLiteral("remote_off"));
}

void BoardRemoteTests::noBoardOnTheDesktopIsSaid()
{
    Rig rig;
    rig.hasBoard = false;
    rig.request(1, {{"type", "board_open"}});
    QVERIFY(rig.toWorker.isEmpty());
    QCOMPARE(rig.toHub.size(), 1);
    QCOMPARE(rig.toHub.first().event.value("code").toString(), QStringLiteral("board_not_found"));
    // Nothing is forwarded from a board no device is on.
    rig.toHub.clear();
    rig.event({{"event", "board_changed"}, {"rev", 3}, {"upserts", QJsonArray{QJsonObject{{"id", "K7Q2"}}}}});
    QVERIFY(rig.toHub.isEmpty());

    // A window whose pane stands in a project with a Switchboard nobody has opened yet adopts it,
    // as the desktop's own key would — asked only because no tab had a board.
    int asked = 0;
    BoardRemote::Host adopting;
    adopting.active = [] { return true; };
    adopting.boardTab = [] { return QString(); };
    adopting.adoptBoard = [&asked] { ++asked; return QStringLiteral("tab-9"); };
    adopting.hasBoard = [](const QString &tab) { return tab == QStringLiteral("tab-9"); };
    QStringList sent;
    adopting.send = [&sent](const QString &tab, const QJsonObject &message) {
        sent << tab + QLatin1Char(':') + message.value("type").toString();
        return true;
    };
    QObject other;
    rig.bridge.addHost(&other, adopting);
    rig.request(2, {{"type", "board_open"}});
    rig.request(3, {{"type", "board_refresh"}});
    QCOMPARE(asked, 1);                       // the refresh stays on the board the open found
    QCOMPARE(sent, (QStringList{"tab-9:board_open", "tab-9:board_refresh"}));
    rig.hasBoard = true;                      // a tab with a board is never passed over for an adoption
    rig.request(4, {{"type", "board_open"}});
    QCOMPARE(asked, 1);
    QCOMPARE(rig.bridge.currentTab(), QStringLiteral("tab-1"));
}

void BoardRemoteTests::aDeviceStaysOnTheBoardItOpened()
{
    // Two windows. The phone opened the first one's board; the owner then works in the second.
    // The phone's next write still goes to the board it is looking at, and the second board's
    // events are not mixed into it — until the phone opens the Switchboard again.
    BoardRemote bridge;
    bridge.remoteOn = [] { return true; };
    QList<Sent> toHub;
    bridge.sendEvent = [&toHub](const QJsonValue &rid, const QJsonObject &event) { toHub.append({rid, event}); };
    QObject first, second;
    bool secondActive = false;
    QStringList sentTo;
    auto host = [&](const QString &tab, std::function<bool()> active) {
        BoardRemote::Host h;
        h.active = std::move(active);
        h.boardTab = [tab] { return tab; };
        h.hasBoard = [tab](const QString &asked) { return asked == tab; };
        h.send = [&sentTo, tab](const QString &asked, const QJsonObject &message) {
            sentTo << asked + QLatin1Char(':') + message.value("type").toString();
            return asked == tab;
        };
        return h;
    };
    bridge.addHost(&first, host(QStringLiteral("tab-a"), [&] { return !secondActive; }));
    bridge.addHost(&second, host(QStringLiteral("tab-b"), [&] { return secondActive; }));

    bridge.handleRequest({{"rid", 1}, {"request", QJsonObject{{"type", "board_open"}}}});
    QCOMPARE(bridge.currentTab(), QStringLiteral("tab-a"));
    secondActive = true;
    bridge.handleRequest({{"rid", 2}, {"request", QJsonObject{{"type", "board_move"}, {"id", "K7Q2"}, {"status", "done"}}}});
    QCOMPARE(sentTo, (QStringList{"tab-a:board_open", "tab-a:board_move"}));
    bridge.workerEvent(&second, QStringLiteral("tab-b"), {{"event", "board_changed"}, {"rev", 9}, {"upserts", QJsonArray{QJsonObject{{"id", "K7Q2"}}}}});
    QVERIFY(toHub.isEmpty());
    bridge.workerEvent(&first, QStringLiteral("tab-a"), {{"event", "board_changed"}, {"rev", 2}, {"upserts", QJsonArray{QJsonObject{{"id", "K7Q2"}}}}});
    QCOMPARE(toHub.size(), 1);

    bridge.handleRequest({{"rid", 3}, {"request", QJsonObject{{"type", "board_open"}}}});
    QCOMPARE(bridge.currentTab(), QStringLiteral("tab-b"));
    QCOMPARE(sentTo.last(), QStringLiteral("tab-b:board_open"));
}

void BoardRemoteTests::theBoardIsWatchedWhenNoPaneWatchesIt()
{
    // A pane's agent (or a `git pull`) writes a card and no Switchboard pane is open: the bridge's
    // own watch asks the worker to look again, and the `board_changed` that answers is a broadcast.
    QTemporaryDir board;
    QVERIFY(board.isValid());
    QVERIFY(QDir(board.path()).mkpath(QStringLiteral("features")));
    Rig rig;
    bool paneOpen = false;
    BoardRemote::Host host;
    host.active = [] { return true; };
    host.boardTab = [] { return QStringLiteral("tab-w"); };
    host.hasBoard = [](const QString &tab) { return tab == QStringLiteral("tab-w"); };
    host.boardDir = [&board](const QString &) { return board.path(); };
    host.paneWatches = [&paneOpen](const QString &) { return paneOpen; };
    QList<QJsonObject> sent;
    host.send = [&sent](const QString &, const QJsonObject &message) { sent.append(message); return true; };
    BoardRemote bridge;
    bridge.remoteOn = [&rig] { return rig.remote; };
    QList<Sent> toHub;
    bridge.sendEvent = [&toHub](const QJsonValue &rid, const QJsonObject &event) { toHub.append({rid, event}); };
    QObject window;
    bridge.addHost(&window, host);
    bridge.handleRequest({{"rid", 1}, {"request", QJsonObject{{"type", "board_open"}}}});
    QCOMPARE(sent.size(), 1);

    auto touch = [&board](const QString &name) {
        QFile card(board.path() + QStringLiteral("/features/") + name);
        QVERIFY(card.open(QIODevice::WriteOnly));
        card.write("x");
    };
    touch(QStringLiteral("one.md"));
    QTRY_COMPARE_WITH_TIMEOUT(sent.size(), 2, 5000);
    QCOMPARE(sent.last().value("type").toString(), QStringLiteral("board_refresh"));
    bridge.workerEvent(&window, QStringLiteral("tab-w"),
                       {{"event", "board_changed"}, {"id", sent.last().value("id")}, {"rev", 2},
                        {"upserts", QJsonArray{QJsonObject{{"id", "K7Q2"}, {"waiting_on", "owner"}}}}});
    QCOMPARE(toHub.size(), 1);
    QVERIFY(toHub.first().rid.isNull());

    // A Switchboard pane in the tab does this itself; and with remote control off nobody is told.
    paneOpen = true;
    touch(QStringLiteral("two.md"));
    QTest::qWait(900);
    QCOMPARE(sent.size(), 2);
    paneOpen = false;
    rig.remote = false;
    touch(QStringLiteral("three.md"));
    QTest::qWait(900);
    QCOMPARE(sent.size(), 2);
}

QTEST_MAIN(BoardRemoteTests)
#include "boardremote_test.moc"
