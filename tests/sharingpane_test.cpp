// SPDX-License-Identifier: GPL-3.0-or-later
// The multiplayer model behind the Sharing pane (#W5N2, docs/REMOTE-PROTOCOL.md section 10).
// Everything here is the logic the owner's answers depend on — who is here, what is waiting, whose
// countdown has run out, what the pane header says — so it is exercised without a hub and without
// a display.
#include "SharingPane.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace relay::sharing;

namespace {

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

QJsonArray items(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).array();
}

constexpr qint64 kNow = 1'700'000'000'000LL;

}  // namespace

class SharingTest : public QObject {
    Q_OBJECT
private slots:
    void sharedPanesAndTitles();
    void participantsAndInvites();
    void knockIsOnePersonAtTheDoor();
    void knockIsClearedOnceTheyAreIn();
    void controlAndPromptCarryTheirOwnClocks();
    void requestsLapseOnTheHubsClock();
    void whoIsDriving();
    void removingSomeoneTakesTheirQuestions();
    void endingAShareForgetsIt();
    void theChipSaysWhatIsGoingOn();
    void sentences();
};

void SharingTest::sharedPanesAndTitles()
{
    Model model;
    QVERIFY(!model.anyShared());
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")},
                          {QStringLiteral("p2"), QString()}});
    QVERIFY(model.anyShared());
    QCOMPARE(model.paneTitle(QStringLiteral("p1")), QStringLiteral("build"));
    // A pane with no title of its own is named by its token rather than by nothing at all.
    QCOMPARE(model.paneTitle(QStringLiteral("p2")), QStringLiteral("p2"));
    QCOMPARE(model.paneTitle(QStringLiteral("gone")), QStringLiteral("gone"));
}

void SharingTest::participantsAndInvites()
{
    Model model;
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")}});
    model.setParticipants(items(R"([
        {"id":"a1","name":"alice","platform":"Chrome","role":"editor","panes":["p1"],
         "invite":"i1","fingerprint":"AB12 CD34","expires":80000},
        {"id":"b2","name":"bob","platform":"Safari","role":"viewer","panes":["p9"],
         "invite":"i2","fingerprint":"EF56 7890","expires":600}
    ])"), items(R"([
        {"id":"i1","panes":["p1"],"role":"editor","uses":1,"expires":86400},
        {"id":"i3","panes":["p9"],"role":"viewer","uses":3,"expires":3600}
    ])"));

    const QList<Participant> here = model.participantsOn(QStringLiteral("p1"));
    QCOMPARE(here.size(), 1);
    QCOMPARE(here.first().name, QStringLiteral("alice"));
    QCOMPARE(here.first().role, QStringLiteral("editor"));
    QCOMPARE(here.first().fingerprint, QStringLiteral("AB12 CD34"));
    // Pane scoping is the whole point of an invite: bob is on p9 and must not show up on p1.
    QCOMPARE(model.guestsOn(QStringLiteral("p1")), 1);
    QCOMPARE(model.guestsOn(QStringLiteral("p9")), 1);

    const QList<Invite> invites = model.invitesOn(QStringLiteral("p1"));
    QCOMPARE(invites.size(), 1);
    QCOMPARE(invites.first().id, QStringLiteral("i1"));
    QCOMPARE(invites.first().uses, 1);

    // An unknown role is read as the safe one, never as editor.
    model.setParticipants(items(R"([{"id":"c3","name":"x","role":"admin","panes":["p1"]}])"), {});
    QCOMPARE(model.participantsOn(QStringLiteral("p1")).first().role, QStringLiteral("viewer"));
}

void SharingTest::knockIsOnePersonAtTheDoor()
{
    Model model;
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")}});
    model.addKnock(json(R"({"t":"knock","participant":"a1","name":"alice","platform":"Chrome",
        "code":"48213","fingerprint":"AB12 CD34 EF56","peer":"192.0.2.7","role":"editor",
        "pane":"p1","panes":["p1"],"invite":"i1"})"), kNow);
    QCOMPARE(model.waiting(), 1);
    const Request knock = model.requests(QStringLiteral("p1")).first();
    QCOMPARE(knock.kind, Request::Kind::Knock);
    QCOMPARE(knock.code, QStringLiteral("48213"));
    QCOMPARE(knock.role, QStringLiteral("editor"));
    QCOMPARE(knock.seconds, kKnockSeconds);
    QCOMPARE(knock.secondsLeft(kNow), kKnockSeconds);

    // A knock with no `pane` but a `panes` list still lands on that pane's share.
    model.addKnock(json(R"({"participant":"b2","name":"bob","role":"viewer","panes":["p1"],
        "code":"11111"})"), kNow);
    QCOMPARE(model.requests(QStringLiteral("p1")).size(), 2);

    // The hub only ever has one knock outstanding per person, so a repeat replaces rather
    // than stacking a second row with the same code.
    model.addKnock(json(R"({"participant":"a1","name":"alice","role":"viewer","pane":"p1",
        "code":"48213"})"), kNow + 1000);
    QCOMPARE(model.requests(QStringLiteral("p1")).size(), 2);
    QCOMPARE(model.requests(QStringLiteral("p1")).first().role, QStringLiteral("viewer"));
}

void SharingTest::knockIsClearedOnceTheyAreIn()
{
    Model model;
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")}});
    model.addKnock(json(R"({"participant":"a1","name":"alice","role":"editor","pane":"p1",
        "code":"48213"})"), kNow);
    QCOMPARE(model.waiting(), 1);
    // The `participants` line that follows an admission is what says the door is shut again,
    // whoever answered it — the pane's own button, another window, or the hub's timeout.
    model.setParticipants(items(R"([{"id":"a1","name":"alice","role":"editor","panes":["p1"]}])"), {});
    QCOMPARE(model.waiting(), 0);
}

void SharingTest::controlAndPromptCarryTheirOwnClocks()
{
    Model model;
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")}});
    model.setParticipants(items(R"([{"id":"a1","name":"alice","role":"editor","panes":["p1"]}])"), {});
    model.addControlAsk(json(R"({"pane":"p1","participant":"a1"})"), kNow);
    model.addPromptAsk(json(R"({"id":"q7","participant":"a1","pane":"p1",
        "text":"run the full test suite and tell me what broke"})"), kNow);

    const QList<Request> waiting = model.requests(QStringLiteral("p1"));
    QCOMPARE(waiting.size(), 2);
    QCOMPARE(waiting.at(0).kind, Request::Kind::Control);
    QCOMPARE(waiting.at(0).seconds, kControlSeconds);
    // Neither line has to carry the name: the participant list already has it.
    QCOMPARE(waiting.at(0).name, QStringLiteral("alice"));
    QCOMPARE(waiting.at(1).kind, Request::Kind::Prompt);
    QCOMPARE(waiting.at(1).id, QStringLiteral("q7"));
    QCOMPARE(waiting.at(1).seconds, kPromptSeconds);
    QCOMPARE(waiting.at(1).text,
             QStringLiteral("run the full test suite and tell me what broke"));
    // A knock, a control request and a prompt from one person are three different rows.
    QVERIFY(waiting.at(0).key() != waiting.at(1).key());
}

void SharingTest::requestsLapseOnTheHubsClock()
{
    Model model;
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")}});
    model.addKnock(json(R"({"participant":"a1","name":"alice","role":"viewer","pane":"p1",
        "code":"48213"})"), kNow);
    model.addControlAsk(json(R"({"pane":"p1","participant":"b2","name":"bob"})"), kNow);
    model.addPromptAsk(json(R"({"id":"q7","participant":"a1","pane":"p1","text":"hi"})"), kNow);

    QVERIFY(model.expire(kNow + 30'000).isEmpty());
    QCOMPARE(model.waiting(), 3);
    // 60 s: the control request is gone, the other two are not.
    QCOMPARE(model.expire(kNow + 61'000), QStringList{QStringLiteral("control/b2")});
    QCOMPARE(model.waiting(), 2);
    // 2 min: the knock.
    QCOMPARE(model.expire(kNow + 121'000), QStringList{QStringLiteral("knock/a1")});
    // 10 min: the prompt.
    QCOMPARE(model.waiting(), 1);
    QCOMPARE(model.expire(kNow + 601'000), QStringList{QStringLiteral("prompt/q7")});
    QCOMPARE(model.waiting(), 0);
}

void SharingTest::whoIsDriving()
{
    Model model;
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")}});
    model.setParticipants(items(R"([{"id":"a1","name":"alice","role":"editor","panes":["p1"]}])"), {});
    QVERIFY(model.driverOn(QStringLiteral("p1")).isEmpty());

    model.addControlAsk(json(R"({"pane":"p1","participant":"a1"})"), kNow);
    QCOMPARE(model.waiting(), 1);
    model.setControl(QStringLiteral("p1"), QStringLiteral("participant:a1"), QStringLiteral("alice"));
    QCOMPARE(model.driverOn(QStringLiteral("p1")), QStringLiteral("alice"));
    QVERIFY(model.participantsOn(QStringLiteral("p1")).first().driving);
    // Granting it answers the request; the row must not sit there counting down as well.
    QCOMPARE(model.waiting(), 0);

    // The owner taking it back, and the agent holding it, are the same state seen from here:
    // nobody else is driving.
    model.setControl(QStringLiteral("p1"), QStringLiteral("owner"), QStringLiteral("you"));
    QVERIFY(model.driverOn(QStringLiteral("p1")).isEmpty());
    QVERIFY(!model.participantsOn(QStringLiteral("p1")).first().driving);
    model.setControl(QStringLiteral("p1"), QStringLiteral("agent"), QStringLiteral("agent"));
    QVERIFY(model.driverOn(QStringLiteral("p1")).isEmpty());
}

void SharingTest::removingSomeoneTakesTheirQuestions()
{
    Model model;
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")}});
    model.setParticipants(items(R"([{"id":"a1","name":"alice","role":"editor","panes":["p1"]},
                                    {"id":"b2","name":"bob","role":"editor","panes":["p1"]}])"), {});
    model.addControlAsk(json(R"({"pane":"p1","participant":"a1"})"), kNow);
    model.addPromptAsk(json(R"({"id":"q7","participant":"a1","pane":"p1","text":"hi"})"), kNow);
    model.addPromptAsk(json(R"({"id":"q8","participant":"b2","pane":"p1","text":"hello"})"), kNow);
    QCOMPARE(model.waiting(), 3);
    model.dropParticipant(QStringLiteral("a1"));
    QCOMPARE(model.guestsOn(QStringLiteral("p1")), 1);
    QCOMPARE(model.waiting(), 1);
    QCOMPARE(model.requests().first().id, QStringLiteral("q8"));
}

void SharingTest::endingAShareForgetsIt()
{
    Model model;
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")},
                          {QStringLiteral("p2"), QStringLiteral("logs")}});
    model.setParticipants({}, items(R"([{"id":"i1","panes":["p1"],"role":"editor","uses":1,
                                         "expires":86400}])"));
    model.addKnock(json(R"({"participant":"a1","name":"alice","role":"viewer","pane":"p1",
        "code":"48213"})"), kNow);
    model.setOptions(QStringLiteral("p1"), {false, true, false});
    QCOMPARE(model.options(QStringLiteral("p1")).promptsImmediate, true);

    // The pane stopped being shared: the GUI simply stops listing it, and everything hanging off
    // it goes with it rather than waiting for a hub line that will never come.
    model.setSharedPanes({{QStringLiteral("p2"), QStringLiteral("logs")}});
    QCOMPARE(model.sharedPanes().size(), 1);
    QCOMPARE(model.waiting(), 0);
    QCOMPARE(model.invitesOn(QStringLiteral("p1")).size(), 0);
    QCOMPARE(model.options(QStringLiteral("p1")).promptsImmediate, false);
}

void SharingTest::theChipSaysWhatIsGoingOn()
{
    Model model;
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")}});
    // Not shared at all: no chip.
    QVERIFY(model.chip(QStringLiteral("p1"), false).text.isEmpty());
    // Shared, nobody else on it: the phone chip that was there before multiplayer.
    QCOMPARE(model.chip(QStringLiteral("p1"), true).text, QStringLiteral("phone"));
    QVERIFY(!model.chip(QStringLiteral("p1"), true).guestDriving);

    model.setParticipants(items(R"([{"id":"a1","name":"alice","role":"editor","panes":["p1"]}])"), {});
    QCOMPARE(model.chip(QStringLiteral("p1"), true).text, QStringLiteral("1 guest"));
    model.setParticipants(items(R"([{"id":"a1","name":"alice","role":"editor","panes":["p1"]},
                                    {"id":"b2","name":"bob","role":"viewer","panes":["p1"]}])"), {});
    QCOMPARE(model.chip(QStringLiteral("p1"), true).text, QStringLiteral("2 guests"));
    QVERIFY(model.chip(QStringLiteral("p1"), true).tooltip.contains(QStringLiteral("alice (editor)")));

    // Somebody else is typing: that beats the count, because it is the one thing the owner must
    // not be able to miss.
    model.setControl(QStringLiteral("p1"), QStringLiteral("participant:a1"), QStringLiteral("alice"));
    const ChipState driving = model.chip(QStringLiteral("p1"), true);
    QCOMPARE(driving.text, QStringLiteral("alice is typing"));
    QVERIFY(driving.guestDriving);
    QVERIFY(driving.tooltip.contains(QStringLiteral("take it straight back")));
}

void SharingTest::sentences()
{
    QVERIFY(roleSentence(QStringLiteral("viewer")).contains(QStringLiteral("cannot type")));
    QVERIFY(roleSentence(QStringLiteral("editor")).contains(QStringLiteral("waits for you")));
    // Anything that is not "editor" is described as the lesser role, never as more.
    QCOMPARE(roleSentence(QStringLiteral("nonsense")), roleSentence(QStringLiteral("viewer")));

    QCOMPARE(expiryText(0), QStringLiteral("expired"));
    QCOMPARE(expiryText(45), QStringLiteral("45 s"));
    QCOMPARE(expiryText(3600), QStringLiteral("60 min"));
    QCOMPARE(expiryText(86400), QStringLiteral("24 h"));
    QCOMPARE(expiryText(604800), QStringLiteral("7 days"));

    QCOMPARE(countdown(kKnockSeconds), QStringLiteral("2:00"));
    QCOMPARE(countdown(59), QStringLiteral("0:59"));
    QCOMPARE(countdown(4), QStringLiteral("0:04"));
    QCOMPARE(countdown(-1), QStringLiteral("0:00"));

    QCOMPARE(usesText(1), QStringLiteral("1 use left"));
    QCOMPARE(usesText(3), QStringLiteral("3 uses left"));
    QCOMPARE(usesText(0), QStringLiteral("spent"));
}

QTEST_MAIN(SharingTest)
#include "sharingpane_test.moc"
