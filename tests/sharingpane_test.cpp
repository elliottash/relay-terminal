// SPDX-License-Identifier: AGPL-3.0-or-later
// The multiplayer model behind the Sharing pane (#W5N2, docs/REMOTE-PROTOCOL.md section 10).
// Everything here is the logic the owner's answers depend on — who is here, what is waiting, whose
// countdown has run out, what the pane header says — so it is exercised without a hub and without
// a display.
#include "SharingPane.h"
#include "ContextDock.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTabBar>
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

// The texts of the view's labels with this object name, in the order they are laid out
// (findChildren walks the children in the order they were added, depth first).
// Only what is on screen: the other page of the tab bar, a hidden card and the invite form
// before it is opened hold labels too, and a test that counted them would be counting the
// furniture. isVisibleTo() is what a widget would show if the view itself were shown.
QStringList labels(const QWidget &view, const char *name)
{
    QStringList texts;
    for (const QLabel *label : view.findChildren<QLabel *>(QLatin1String(name)))
        if (label->isVisibleTo(&view)) texts << label->text();
    return texts;
}

QStringList allLabels(const QWidget &view)
{
    QStringList texts;
    for (const QLabel *label : view.findChildren<QLabel *>())
        if (label->isVisibleTo(&view)) texts << label->text();
    return texts;
}

QStringList buttons(const QWidget &view, const QString &text)
{
    QStringList found;
    for (const QPushButton *press : view.findChildren<QPushButton *>())
        if (press->text() == text && press->isVisibleTo(&view)) found << press->text();
    return found;
}

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
    void theOwnersOwnPhoneIsADriverToo();
    void removingSomeoneTakesTheirQuestions();
    void endingAShareForgetsIt();
    void theChipSaysWhatIsGoingOn();
    void theParticipantsLineCarriesPresenceAndControl();
    void pausedSaysWhy();
    void aPlanArrivesAsAPrompt();
    void expiryIsReadInWhicheverUnitArrived();
    void sentences();
    void togglingAnOptionSurvivesTheRebuildItCauses();
    void theTopLineSaysWhatIsOnAndWhoIsConnected();
    void defaultPageIsPeopleAndQuietPanesAreNotRows();
    void startInviteOpensTheFormOnThatScope();
    void pairingIsMintedOnceAndOnlyWhenTheServiceIsUsable();
    void anAskLandsOnDevicesWithRefuseHoldingTheFocus();
    void aKnockShowsOnPeopleAndInTheTab();
    void guestsGetABlockEachAndTheOptionsNoteOnce();
    void blocksAreHeadedByTheirScope();
    void tickMovesEveryClock();
    void theDockedAgentReadsThePaneAndEndsOnlyASharedOne();

private:
    // The window's scope catalogue, as onScopes hands it over: three panes in two tabs, the two
    // tabs, and everything.
    QList<Scope> catalogue();
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

void SharingTest::theOwnersOwnPhoneIsADriverToo()
{
    // Section 10.3 has one holder per pane and the owner's phone is in the same book, so the wire
    // says `holder: "owner"` for it — with the device beside it, which is how the desktop knows
    // its keystroke has a pane to take back (#W5N2).
    Model model;
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")}});
    model.setDevices(items(R"([{"id":"dev1","name":"Pixel 9","online":true,"panes":["p1"]},
                               {"id":"dev2","online":true,"panes":["p1"]}])"));
    QVERIFY(model.deviceDriverOn(QStringLiteral("p1")).isEmpty());

    model.setControl(QStringLiteral("p1"), QStringLiteral("owner"), QStringLiteral("this desktop"),
                     QStringLiteral("dev1"), QStringLiteral("Pixel 9"));
    QCOMPARE(model.deviceDriverOn(QStringLiteral("p1")), QStringLiteral("Pixel 9"));
    QVERIFY(model.driverOn(QStringLiteral("p1")).isEmpty());   // it is not a guest
    ChipState chip = model.chip(QStringLiteral("p1"), true);
    QCOMPARE(chip.text, QStringLiteral("Pixel 9 is typing"));
    QVERIFY(!chip.guestDriving);                               // the owner is still the owner

    // A device with no name it paired under is still a driver, by id.
    model.setControl(QStringLiteral("p1"), QStringLiteral("owner"), QString(),
                     QStringLiteral("dev2"), QString());
    QCOMPARE(model.deviceDriverOn(QStringLiteral("p1")), QStringLiteral("dev2"));

    // The keyboard coming back to the desktop clears it — `control` carries no device then.
    model.setControl(QStringLiteral("p1"), QStringLiteral("owner"), QStringLiteral("this desktop"));
    QVERIFY(model.deviceDriverOn(QStringLiteral("p1")).isEmpty());
    QVERIFY(model.chip(QStringLiteral("p1"), true).text.isEmpty());
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
    // Published but nobody has opened it: no icon.
    const ChipState phoneOnly = model.chip(QStringLiteral("p1"), true);
    QVERIFY(phoneOnly.text.isEmpty());
    QVERIFY(!phoneOnly.visible);
    QVERIFY(!phoneOnly.guestDriving);

    model.setDevices(items(R"([{"id":"d1","name":"iPhone","online":true,"panes":["p1"]}])"));
    const ChipState phoneViewing = model.chip(QStringLiteral("p1"), true);
    QVERIFY(phoneViewing.visible);
    QVERIFY(phoneViewing.tooltip.contains(QStringLiteral("iPhone")));
    model.setDevices(items(R"([{"id":"d1","name":"iPhone","online":true,"panes":[]}])"));
    QVERIFY(!model.chip(QStringLiteral("p1"), true).visible);
    model.setDevices(items(R"([{"id":"d1","name":"iPhone","online":false,"panes":["p1"]}])"));
    QVERIFY(!model.chip(QStringLiteral("p1"), true).visible);

    model.setParticipants(items(R"([{"id":"a1","name":"alice","role":"editor","panes":["p1"]}])"), {});
    QVERIFY(!model.chip(QStringLiteral("p1"), true).visible);
    model.setParticipants(items(R"([{"id":"a1","name":"alice","role":"editor","panes":["p1"],"viewing":["p1"]}])"), {});
    QVERIFY(model.chip(QStringLiteral("p1"), true).visible);
    QCOMPARE(model.chip(QStringLiteral("p1"), true).text, QStringLiteral("1 guest"));
    model.setParticipants(items(R"([{"id":"a1","name":"alice","role":"editor","panes":["p1"],"viewing":["p1"]},
                                    {"id":"b2","name":"bob","role":"viewer","panes":["p1"],"viewing":["p1"]}])"), {});
    QCOMPARE(model.chip(QStringLiteral("p1"), true).text, QStringLiteral("2 guests"));
    QVERIFY(model.chip(QStringLiteral("p1"), true).tooltip.contains(QStringLiteral("alice (editor)")));

    // Somebody else is typing: that beats the count, because it is the one thing the owner must
    // not be able to miss.
    model.setControl(QStringLiteral("p1"), QStringLiteral("participant:a1"), QStringLiteral("alice"));
    const ChipState driving = model.chip(QStringLiteral("p1"), true);
    QCOMPARE(driving.text, QStringLiteral("alice is typing"));
    QVERIFY(driving.guestDriving);
    QVERIFY(driving.tooltip.contains(QStringLiteral("take it straight back")));
    model.setParticipants(items(R"([{"id":"a1","name":"alice","role":"editor","panes":["p1"],"online":false}])"), {});
    QVERIFY(!model.chip(QStringLiteral("p1"), true).visible);
}

// The hub says who is connected and who holds each pane's control token on the `participants`
// line itself, so a Sharing pane opened after a handoff reads the same state as one that watched
// it happen (remote/gui_host.py, report_participants).
void SharingTest::theParticipantsLineCarriesPresenceAndControl()
{
    Model model;
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")}});
    model.setParticipants(items(R"([
        {"id":"a1","name":"alice","role":"editor","panes":["p1"],"viewing":["p1"],"online":true,"driving":["p1"]},
        {"id":"b2","name":"bob","role":"viewer","panes":["p1"],"online":false,"driving":[]}
    ])"), {});
    QCOMPARE(model.driverOn(QStringLiteral("p1")), QStringLiteral("alice"));
    const QList<Participant> here = model.participantsOn(QStringLiteral("p1"));
    QVERIFY(here.at(0).driving);
    QVERIFY(here.at(0).online);
    QVERIFY(!here.at(1).driving);
    QVERIFY(!here.at(1).online);
    QCOMPARE(model.chip(QStringLiteral("p1"), true).text, QStringLiteral("alice is typing"));
    // A line without the two fields (an older hub) is read as "connected, not driving" rather
    // than as "gone".
    model.setParticipants(items(R"([{"id":"a1","name":"alice","role":"editor","panes":["p1"]}])"), {});
    QVERIFY(model.participantsOn(QStringLiteral("p1")).first().online);
    QVERIFY(model.driverOn(QStringLiteral("p1")).isEmpty());
}

void SharingTest::pausedSaysWhy()
{
    Model model;
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")}});
    QVERIFY(!model.options(QStringLiteral("p1")).paused);
    model.setShareState(QStringLiteral("p1"), true, QStringLiteral("away"));
    QVERIFY(model.options(QStringLiteral("p1")).paused);
    QCOMPARE(model.pauseReason(QStringLiteral("p1")), QStringLiteral("away"));
    model.setShareState(QStringLiteral("p1"), false, QString());
    QVERIFY(!model.options(QStringLiteral("p1")).paused);
    QVERIFY(model.pauseReason(QStringLiteral("p1")).isEmpty());
}

// A guest's plan_execute arrives as an ordinary prompt carrying the plan's id (section 10.4), and
// the row has to say so: approving it runs a plan, not a sentence.
void SharingTest::aPlanArrivesAsAPrompt()
{
    Model model;
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")}});
    model.addPromptAsk(json(R"({"id":"q9","participant":"a1","pane":"p1","text":"run step 2",
        "when":"now","plan":"plan-7"})"), kNow);
    const Request prompt = model.requests().first();
    QCOMPARE(prompt.plan, QStringLiteral("plan-7"));
    QCOMPARE(prompt.seconds, kPromptSeconds);
}

// A participant's `expires` is an absolute epoch and an invite's is already the seconds left
// (remote/guests.py). Both arrive under one field name, and reading a clock time as a countdown is
// how "access ends in 20716 days" happened.
void SharingTest::expiryIsReadInWhicheverUnitArrived()
{
    Model model;
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")}});
    const qint64 nowSecs = QDateTime::currentSecsSinceEpoch();
    const QByteArray items_json = QByteArray(R"([{"id":"a1","name":"alice","role":"editor",
        "panes":["p1"],"expires":)") + QByteArray::number(nowSecs + 7200) + QByteArray("}]");
    model.setParticipants(QJsonDocument::fromJson(items_json).array(),
                          items(R"([{"id":"i1","panes":["p1"],"role":"editor","uses":1,
                                     "expires":3600}])"));
    const qint64 left = model.participantsOn(QStringLiteral("p1")).first().expires;
    QVERIFY2(left > 7000 && left <= 7200, qPrintable(QString::number(left)));
    QCOMPARE(expiryText(left), QStringLiteral("2 h"));
    // The invite's 3600 is already a countdown and must not be treated as a date.
    QCOMPARE(model.invitesOn(QStringLiteral("p1")).first().expires, 3600);
    // An expiry in the past reads as expired, not as a huge negative.
    model.setParticipants(QJsonDocument::fromJson(
        QByteArray(R"([{"id":"a1","name":"alice","role":"editor","panes":["p1"],"expires":)")
        + QByteArray::number(nowSecs - 10) + QByteArray("}]")).array(), {});
    QCOMPARE(model.participantsOn(QStringLiteral("p1")).first().expires, 0);
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

void SharingTest::togglingAnOptionSurvivesTheRebuildItCauses()
{
    // #SHCK: clicking "Guest prompts run immediately" crashed Relay. The box's `toggled` reached
    // the model, the model's change rebuilt the view at once, and the rebuild deleted the box
    // while QCheckBox::setChecked was still running on it. The wiring here is the window's own:
    // the option is written to the model and the view refreshed synchronously.
    Model model;
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")}});
    model.setParticipants(items(R"([{"id":"a1","name":"alice","role":"editor","panes":["p1"]}])"), {});
    SharingView view;
    view.setModel(&model);
    view.refresh();
    view.onOptions = [&](const QString &pane, bool immediate, bool present) {
        model.setOptions(pane, {model.options(pane).paused, immediate, present});
        view.refresh();
    };
    QPointer<QCheckBox> box;
    for (QCheckBox *candidate : view.findChildren<QCheckBox *>())
        if (candidate->text() == QStringLiteral("Guest prompts run immediately")) box = candidate;
    QVERIFY(box);
    QVERIFY(!box->isChecked());
    box->click();
    // The click returned with the box still alive: it goes on the next turn of the loop, not
    // under its own signal.
    QVERIFY(box);
    QCOMPARE(model.options(QStringLiteral("p1")).promptsImmediate, true);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!box);
    QCheckBox *fresh = nullptr;
    for (QCheckBox *candidate : view.findChildren<QCheckBox *>())
        if (candidate->text() == QStringLiteral("Guest prompts run immediately")) fresh = candidate;
    QVERIFY(fresh);
    QVERIFY(fresh->isChecked());
}

// #SHRP/#SMDX: the top line is the model's, so the window feeds it and the view only prints it —
// on the Devices page, above the switch, the address and the paired devices.
void SharingTest::theTopLineSaysWhatIsOnAndWhoIsConnected()
{
    Model model;
    QCOMPARE(model.topLine(), QStringLiteral("Remote control off"));
    model.setRemote(false, QStringLiteral("relay-terminal.ai"), true, 2);
    QCOMPARE(model.topLine(), QStringLiteral("Remote control off"));

    model.setRemote(true, QStringLiteral("relay-terminal.ai"), true, 0);
    QCOMPARE(model.topLine(), QStringLiteral("Remote control on · relay-terminal.ai · no phone connected"));

    // The `devices` line names them and says which hold a channel; a paired phone that is not
    // connected is not on the line.
    model.setDevices(items(R"([
        {"id":"d1","name":"iPhone","platform":"Safari","online":true},
        {"id":"d2","name":"iPad","platform":"Safari","online":true},
        {"id":"d3","name":"old Pixel","platform":"Chrome","online":false}
    ])"));
    QCOMPARE(model.connectedDeviceNames(), (QStringList{QStringLiteral("iPhone"), QStringLiteral("iPad")}));
    QCOMPARE(model.topLine(), QStringLiteral("Remote control on · relay-terminal.ai · iPhone, iPad connected"));

    // An older sidecar's records carry no `online`: the hub's count is what there is.
    model.setDevices(items(R"([{"id":"d1","name":"iPhone","platform":"Safari"}])"));
    model.setRemote(true, QStringLiteral("your tailnet"), true, 1);
    QCOMPARE(model.topLine(), QStringLiteral("Remote control on · your tailnet · 1 phone connected"));
    model.setRemote(true, QStringLiteral("your tailnet"), true, 2);
    QCOMPARE(model.topLine(), QStringLiteral("Remote control on · your tailnet · 2 phones connected"));

    // On but not registered anywhere: the reason, not a phone count.
    model.setRemote(true, QStringLiteral("relay-terminal.ai"), false, 0,
                    QStringLiteral("the rendezvous link is down; reconnecting."));
    QCOMPARE(model.topLine(), QStringLiteral("Remote control on · relay-terminal.ai · offline: "
                                             "the rendezvous link is down; reconnecting."));

    // The Devices page prints exactly that line, with the switch reflecting the model, and one
    // row per paired device with its capability and its password grant.
    model.setDevices(items(R"([
        {"id":"d1","name":"iPhone","platform":"Safari","online":true,"capability":"full"},
        {"id":"d2","name":"iPad","platform":"Safari","online":false,"capability":"view",
         "password_entry":true}
    ])"));
    SharingView view;
    view.setModel(&model);
    view.refresh();
    view.showPage(SharingView::Page::Devices);
    QCOMPARE(labels(view, "sharingTopLine"), QStringList{model.topLine()});
    auto *remote = view.findChild<QCheckBox *>(QStringLiteral("sharingRemoteSwitch"));
    QVERIFY(remote && remote->isChecked());
    QCOMPARE(labels(view, "settingsHeading"), QStringList{QStringLiteral("Your devices")});
    const QStringList rows = labels(view, "settingsRowLabel");
    QVERIFY2(rows.contains(QStringLiteral("iPhone (Safari)")) && rows.contains(QStringLiteral("iPad (Safari)")),
             qPrintable(rows.join(QStringLiteral(" | "))));
    const QStringList details = labels(view, "settingsRowDetail");
    QVERIFY(details.contains(QStringLiteral("connected · watch and type")));
    QVERIFY(details.contains(QStringLiteral("not connected · watch only")));
    QCOMPARE(buttons(view, QStringLiteral("Passwords: off")).size(), 1);
    QCOMPARE(buttons(view, QStringLiteral("Passwords: on")).size(), 1);
    QCOMPARE(buttons(view, QStringLiteral("Revoke")).size(), 2);
    QString passwordDevice, revoked;
    bool passwordAllow = false;
    view.onPasswordEntry = [&](const QString &device, bool allow) { passwordDevice = device; passwordAllow = allow; };
    view.onRevokeDevice = [&](const QString &device) { revoked = device; };
    for (QPushButton *press : view.findChildren<QPushButton *>())
        if (press->text() == QStringLiteral("Passwords: on")) press->click();
    QCOMPARE(passwordDevice, QStringLiteral("d2"));
    QVERIFY(!passwordAllow);   // it was on, so the press turns it off
    QList<QPushButton *> revokes;
    for (QPushButton *press : view.findChildren<QPushButton *>())
        if (press->text() == QStringLiteral("Revoke")) revokes << press;
    revokes.first()->click();
    QCOMPARE(revoked, QStringLiteral("d1"));

    // The switch is the window's: clicking it asks, and nothing here flips the model.
    bool switched = false, switchedTo = false;
    view.onRemoteSwitch = [&](bool on) { switched = true; switchedTo = on; };
    remote->click();
    QVERIFY(switched && !switchedTo);
    QVERIFY(model.remoteOn());

    // Off: no devices, no line about the switch having turned itself on.
    model.setRemote(false, QString(), false, 0);
    model.setDevices(QList<Device>());
    view.refresh();
    QVERIFY(!remote->isChecked());
    QVERIFY(labels(view, "settingsRowDetail").contains(QStringLiteral("No phone is paired with this desktop yet.")));
    QVERIFY(buttons(view, QStringLiteral("Revoke")).isEmpty());
}

// Three panes nobody is visiting are not rows at all (#SMDX): since #PH0N every pane is reachable
// from the owner's phones, so "shared with nobody" is every pane's ordinary state. People opens
// first and says so in one sentence, with one Invite… button.
void SharingTest::defaultPageIsPeopleAndQuietPanesAreNotRows()
{
    Model model;
    model.setRemote(true, QStringLiteral("relay-terminal.ai"), true, 0);
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")},
                          {QStringLiteral("p2"), QStringLiteral("logs")},
                          {QStringLiteral("p3"), QStringLiteral("deploy")}});
    SharingView view;
    view.setModel(&model);
    view.refresh();

    QCOMPARE(view.page(), SharingView::Page::People);
    auto *tabs = view.findChild<QTabBar *>(QStringLiteral("settingsTabs"));
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(tabs->tabText(0), QStringLiteral("Devices"));
    QCOMPARE(tabs->tabText(1), QStringLiteral("People"));
    QCOMPARE(tabs->currentIndex(), 1);

    QVERIFY(labels(view, "settingsHeading").isEmpty());
    QVERIFY(labels(view, "settingsSubheading").isEmpty());
    QCOMPARE(labels(view, "settingsRowLabel"),
             QStringList{QStringLiteral("Nobody else is here. Your paired devices see every pane "
                                        "while remote control is on.")});
    QCOMPARE(buttons(view, QStringLiteral("Invite…")).size(), 1);
    QVERIFY(buttons(view, QStringLiteral("Invite someone…")).isEmpty());
    QVERIFY(buttons(view, QStringLiteral("Pause guests")).isEmpty());
    QVERIFY(buttons(view, QStringLiteral("End sharing")).isEmpty());
    for (const QString &text : allLabels(view)) {
        QVERIFY2(!text.startsWith(QStringLiteral("Pane “")), qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("Nobody is visiting")), qPrintable(text));
        QVERIFY2(text != QStringLiteral("build") && text != QStringLiteral("logs"), qPrintable(text));
    }
    // The top line is the Devices page's, not this one's.
    QVERIFY(labels(view, "sharingTopLine").isEmpty());
    QCOMPARE(view.paneTitle(), QStringLiteral("Sharing"));

    // Invite… opens the form on the current pane; the form is not on screen before that.
    QVERIFY(view.findChildren<QComboBox *>(QStringLiteral("sharingScopePick")).size() == 1);
    QVERIFY(!view.findChild<QComboBox *>(QStringLiteral("sharingScopePick"))->isVisibleTo(&view));
    for (QPushButton *press : view.findChildren<QPushButton *>())
        if (press->text() == QStringLiteral("Invite…")) press->click();
    QVERIFY(view.findChild<QComboBox *>(QStringLiteral("sharingScopePick"))->isVisibleTo(&view)
            || view.findChild<QComboBox *>(QStringLiteral("sharingInviteRole"))->isVisibleTo(&view));
    QCOMPARE(buttons(view, QStringLiteral("Make a link")).size(), 1);
    QCOMPARE(buttons(view, QStringLiteral("Make a code")).size(), 1);
}

QList<Scope> SharingTest::catalogue()
{
    Scope a;
    a.kind = Scope::Kind::Pane; a.id = QStringLiteral("pA"); a.title = QStringLiteral("build");
    a.tab = QStringLiteral("t1"); a.tabTitle = QStringLiteral("work"); a.current = true;
    Scope b = a;
    b.id = QStringLiteral("pB"); b.title = QStringLiteral("logs"); b.current = false;
    Scope c = a;
    c.id = QStringLiteral("pC"); c.title = QStringLiteral("notes"); c.tab = QStringLiteral("t2");
    c.tabTitle = QStringLiteral("thesis"); c.current = false;
    Scope t1;
    t1.kind = Scope::Kind::Tab; t1.id = t1.tab = QStringLiteral("t1"); t1.title = QStringLiteral("work");
    t1.tabTitle = QStringLiteral("work"); t1.panes = 2;
    Scope t2 = t1;
    t2.id = t2.tab = QStringLiteral("t2"); t2.title = t2.tabTitle = QStringLiteral("thesis"); t2.panes = 1;
    Scope all;
    all.kind = Scope::Kind::All; all.id = all.tab = QStringLiteral("all-tabs");
    all.title = QStringLiteral("Everything"); all.panes = 3;
    return {a, b, c, t1, t2, all};
}

void SharingTest::startInviteOpensTheFormOnThatScope()
{
    Model model;
    model.setSharedPanes({{QStringLiteral("pA"), QStringLiteral("build")},
                          {QStringLiteral("pB"), QStringLiteral("logs")}});
    SharingView view;
    view.setModel(&model);
    view.onScopes = [this] { return catalogue(); };
    view.refresh();
    view.showPage(SharingView::Page::Devices);

    Scope b;
    b.kind = Scope::Kind::Pane;
    b.id = QStringLiteral("pB");
    view.startInvite(b);
    QCOMPARE(view.page(), SharingView::Page::People);
    auto *pick = view.findChild<QComboBox *>(QStringLiteral("sharingScopePick"));
    QVERIFY(pick && pick->isVisibleTo(&view));
    QCOMPARE(pick->currentText(), QStringLiteral("logs"));
    // Three groups: the panes under their tabs, the tabs, and everything; the captions cannot be
    // picked.
    QStringList rows;
    for (int index = 0; index < pick->count(); ++index) rows << pick->itemText(index);
    QCOMPARE(rows, (QStringList{QStringLiteral("Panes in “work”"), QStringLiteral("build"),
                                QStringLiteral("logs"), QStringLiteral("Panes in “thesis”"),
                                QStringLiteral("notes"), QStringLiteral("Whole tabs"),
                                QStringLiteral("Tab “work” (2 panes, and any you add)"),
                                QStringLiteral("Tab “thesis” (1 pane, and any you add)"),
                                QStringLiteral("Everything (every pane in every window)")}));
    auto *rowsModel = qobject_cast<QStandardItemModel *>(pick->model());
    QVERIFY(rowsModel);
    QVERIFY(!rowsModel->item(0)->isEnabled());
    QVERIFY(rowsModel->item(1)->isEnabled());
    QVERIFY(!rowsModel->item(5)->isEnabled());

    Scope made;
    QString role;
    int expires = 0, uses = 0;
    view.onCreateInvite = [&](const Scope &scope, const QString &r, int e, int u) {
        made = scope; role = r; expires = e; uses = u;
    };
    for (QPushButton *press : view.findChildren<QPushButton *>())
        if (press->text() == QStringLiteral("Make a link")) press->click();
    QCOMPARE(made.kind, Scope::Kind::Pane);
    QCOMPARE(made.id, QStringLiteral("pB"));
    QCOMPARE(made.title, QStringLiteral("logs"));
    QCOMPARE(role, QStringLiteral("viewer"));
    QCOMPARE(expires, 86400);
    QCOMPARE(uses, 1);
    QVERIFY(labels(view, "sharingInviteNote").first().startsWith(QStringLiteral("Making a link")));

    // The link comes back: the field holds the whole URL and the sentence names the scope.
    view.showInvite(QStringLiteral("https://relay.example/join#secret"), QrMatrix{{1, 0}, {0, 1}},
                    QStringLiteral("viewer"), 1, 86400);
    auto *url = view.findChild<QLineEdit *>(QStringLiteral("sharingInviteUrl"));
    QVERIFY(url && url->isVisibleTo(&view));
    QCOMPARE(url->text(), QStringLiteral("https://relay.example/join#secret"));
    QVERIFY2(labels(view, "sharingInviteNote").first().startsWith(
                 QStringLiteral("Send this to the person you want on pane “logs”. It lets in one "
                                "person and stops working in 24 h.")),
             qPrintable(labels(view, "sharingInviteNote").first()));
    // An empty scope opens the form on the current pane.
    view.startInvite(Scope{});
    QCOMPARE(pick->currentText(), QStringLiteral("build"));
    // Which put the link away: it was for the other pane.
    QVERIFY(!url->isVisibleTo(&view));

    // A code, for the scope picked, at the role picked; changing the role revokes a live one.
    Scope codeScope;
    QString codeRole, revoked;
    view.onCreateCode = [&](const Scope &scope, const QString &r) { codeScope = scope; codeRole = r; };
    view.onRevokeCode = [&](const QString &code) { revoked = code; };
    for (QPushButton *press : view.findChildren<QPushButton *>())
        if (press->text() == QStringLiteral("Make a code")) press->click();
    QCOMPARE(codeScope.id, QStringLiteral("pA"));
    QCOMPARE(codeRole, QStringLiteral("viewer"));
    view.showCode(QStringLiteral("WXYZ"), QStringLiteral("2468"), 600);
    QCOMPARE(labels(view, "sharingMeetingCode"), QStringList{QStringLiteral("WXYZ")});
    QCOMPARE(labels(view, "sharingMeetingPin"), QStringList{QStringLiteral("2468")});
    // Opening the form again on the same scope is not a change: the code being read out stays.
    view.startInvite(Scope{});
    QVERIFY(revoked.isEmpty());
    QCOMPARE(labels(view, "sharingMeetingCode"), QStringList{QStringLiteral("WXYZ")});
    view.findChild<QComboBox *>(QStringLiteral("sharingInviteRole"))->setCurrentIndex(1);
    QCOMPARE(revoked, QStringLiteral("WXYZ"));
    QVERIFY(labels(view, "sharingMeetingCode").isEmpty());
}

// #PRM2: a pairing room is spent once per offer, and only once the sidecar is registered where
// the phone will look.
void SharingTest::pairingIsMintedOnceAndOnlyWhenTheServiceIsUsable()
{
    Model model;
    model.setRemote(true, QStringLiteral("relay-terminal.ai"), true, 0);
    SharingView view;
    view.setModel(&model);
    view.refresh();
    int pairRequests = 0, codeRequests = 0;
    QString revoked;
    view.onPairRequest = [&] { ++pairRequests; };
    view.onPairCodeRequest = [&] { ++codeRequests; };
    view.onPairCodeRevoke = [&](const QString &code) { revoked = code; };

    Service service;
    service.running = true;
    service.online = true;
    service.base = service.onlineBase = QStringLiteral("https://desk.example");
    view.setService(service);
    QCOMPARE(pairRequests, 0);   // nothing is minted before Add a device… is pressed

    view.startPairing();
    QCOMPARE(view.page(), SharingView::Page::Devices);
    QCOMPARE(pairRequests, 1);
    QCOMPARE(codeRequests, 1);
    view.startPairing();
    view.setService(service);
    QCOMPARE(pairRequests, 1);
    QCOMPARE(codeRequests, 1);
    QVERIFY(buttons(view, QStringLiteral("Add a device…")).isEmpty());
    QCOMPARE(buttons(view, QStringLiteral("Done")).size(), 1);
    QVERIFY(labels(view, "settingsRowDetail").contains(QStringLiteral("Making a code…")));

    view.showPairCode(QStringLiteral("ABCD"), QStringLiteral("1234"), 600);
    QCOMPARE(labels(view, "sharingPairCode").size(), 1);
    QVERIFY2(labels(view, "sharingPairCode").first().contains(QStringLiteral("ABCD")),
             qPrintable(labels(view, "sharingPairCode").first()));
    view.showPairing(QStringLiteral("https://desk.example/pair#secret"), QrMatrix{{1, 0}, {0, 1}}, 120);
    QVERIFY(labels(view, "settingsRowDetail").contains(QStringLiteral("Phone connects to https://desk.example")));
    QVERIFY(labels(view, "sharingPairStatus").first().contains(QStringLiteral("The QR lasts 120 s.")));

    view.stopPairing();
    QCOMPARE(revoked, QStringLiteral("ABCD"));
    QVERIFY(labels(view, "sharingPairCode").isEmpty());
    QCOMPARE(buttons(view, QStringLiteral("Add a device…")).size(), 1);
    QVERIFY(buttons(view, QStringLiteral("Done")).isEmpty());
    // Nothing to revoke twice.
    revoked.clear();
    view.stopPairing();
    QVERIFY(revoked.isEmpty());

    // Leaving the page is Done.
    view.startPairing();
    QCOMPARE(pairRequests, 2);
    view.showPairCode(QStringLiteral("EFGH"), QStringLiteral("5678"), 600);
    view.showPage(SharingView::Page::People);
    QCOMPARE(revoked, QStringLiteral("EFGH"));

    // With the switch on, the sidecar announces its local listener before it is registered at
    // the remembered address: nothing is minted until it is, and then exactly once.
    SharingView later;
    later.setModel(&model);
    later.refresh();
    int laterPair = 0, laterCode = 0;
    later.onPairRequest = [&] { ++laterPair; };
    later.onPairCodeRequest = [&] { ++laterCode; };
    Service starting;
    starting.running = true;
    starting.alwaysOn = true;
    starting.online = false;
    starting.base = QStringLiteral("https://relay-terminal.ai/d/abc");
    later.setService(starting);
    later.startPairing();
    QCOMPARE(laterPair, 0);
    QCOMPARE(laterCode, 0);
    starting.online = true;
    starting.onlineBase = QStringLiteral("https://127.0.0.1:8765");   // registered, but not there yet
    later.setService(starting);
    QCOMPARE(laterPair, 0);
    starting.onlineBase = starting.base;
    later.setService(starting);
    QCOMPARE(laterPair, 1);
    QCOMPARE(laterCode, 1);
    later.setService(starting);
    QCOMPARE(laterPair, 1);

    // The 429: the sidecar could not make a code. Said where the code was promised, with New code.
    later.serviceFailed(QStringLiteral("both pairing rooms are taken."));
    QVERIFY(labels(later, "settingsRowDetail").contains(QStringLiteral("No code was made: both pairing rooms are taken.")));
    QCOMPARE(buttons(later, QStringLiteral("New code")).size(), 1);

    // Not running at all: the card says so and asks for nothing.
    SharingView off;
    off.setModel(&model);
    off.refresh();
    int offPair = 0;
    off.onPairRequest = [&] { ++offPair; };
    off.startPairing();
    QCOMPARE(offPair, 0);
    QCOMPARE(labels(off, "sharingPairStatus"),
             QStringList{QStringLiteral("Remote control is off. These pairing codes have ended.")});
}

void SharingTest::anAskLandsOnDevicesWithRefuseHoldingTheFocus()
{
    Model model;
    model.setRemote(true, QStringLiteral("relay-terminal.ai"), true, 0);
    SharingView view;
    view.setModel(&model);
    view.refresh();
    QCOMPARE(view.page(), SharingView::Page::People);
    int answeredId = -1;
    bool answeredAllow = false;
    QString answeredWith;
    view.onPairAnswer = [&](int id, bool allow, const QString &capability) {
        answeredId = id; answeredAllow = allow; answeredWith = capability;
    };

    DeviceAsk ask;
    ask.id = 7;
    ask.name = QStringLiteral("Pixel 9");
    ask.platform = QStringLiteral("Android");
    ask.fingerprint = QStringLiteral("AB12 CD34");
    ask.code = QStringLiteral("48213");
    ask.peer = QStringLiteral("192.0.2.9");
    view.showAsk(ask);
    QCOMPARE(view.page(), SharingView::Page::Devices);
    auto *tabs = view.findChild<QTabBar *>(QStringLiteral("settingsTabs"));
    QCOMPARE(tabs->tabText(0), QStringLiteral("Devices · 1 asking"));
    QCOMPARE(labels(view, "sharingAskCode"), QStringList{QStringLiteral("48213")});
    QVERIFY(labels(view, "settingsRowLabel").contains(
        QStringLiteral("Pixel 9 (Android) at 192.0.2.9 wants access.\nKey AB12 CD34.\n"
                       "Allow it only if that device shows this code:")));
    QPushButton *refuse = nullptr;
    for (QPushButton *press : view.findChildren<QPushButton *>())
        if (press->text() == QStringLiteral("Refuse") && press->isVisibleTo(&view)) refuse = press;
    QVERIFY(refuse);
    view.focusView();
    QCOMPARE(view.focusWidget(), refuse);
    // A rebuild in the meantime leaves the question and its focus where they are.
    view.refresh();
    QCOMPARE(view.focusWidget(), refuse);
    QCOMPARE(labels(view, "sharingAskCode"), QStringList{QStringLiteral("48213")});

    for (QPushButton *press : view.findChildren<QPushButton *>())
        if (press->text() == QStringLiteral("Allow typing")) press->click();
    QCOMPARE(answeredId, 7);
    QVERIFY(answeredAllow);
    QCOMPARE(answeredWith, QStringLiteral("full"));
    QVERIFY(labels(view, "sharingAskCode").isEmpty());
    QCOMPARE(labels(view, "sharingAskResult"), QStringList{QStringLiteral("Paired. That device can watch and type.")});
    QCOMPARE(tabs->tabText(0), QStringLiteral("Devices"));
    // Nothing to answer twice.
    answeredId = -1;
    view.focusView();
    QVERIFY(view.focusWidget() != refuse);

    // Refusing says so; an invalid ask clears the card without a word.
    ask.id = 8;
    view.showAsk(ask);
    for (QPushButton *press : view.findChildren<QPushButton *>())
        if (press->text() == QStringLiteral("Refuse") && press->isVisibleTo(&view)) press->click();
    QCOMPARE(answeredId, 8);
    QVERIFY(!answeredAllow);
    QCOMPARE(labels(view, "sharingAskResult"), QStringList{QStringLiteral("Refused.")});
    ask.id = 9;
    view.showAsk(ask);
    QCOMPARE(tabs->tabText(0), QStringLiteral("Devices · 1 asking"));
    view.showAsk(DeviceAsk{});
    QVERIFY(labels(view, "sharingAskCode").isEmpty());
    QCOMPARE(tabs->tabText(0), QStringLiteral("Devices"));
}

void SharingTest::aKnockShowsOnPeopleAndInTheTab()
{
    Model model;
    model.setRemote(true, QStringLiteral("relay-terminal.ai"), true, 0);
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")},
                          {QStringLiteral("p2"), QStringLiteral("logs")}});
    model.setParticipants(items(R"([{"id":"b2","name":"bob","role":"viewer","panes":["p1"]}])"), {});
    model.addKnock(json(R"({"participant":"a1","name":"alice","platform":"Chrome","code":"48213",
        "role":"editor","pane":"p2"})"), QDateTime::currentMSecsSinceEpoch());
    SharingView view;
    view.setModel(&model);
    view.showPage(SharingView::Page::Devices);
    view.refresh();

    // A knock does not switch pages by itself; the page it shows on is People.
    QCOMPARE(view.page(), SharingView::Page::Devices);
    auto *tabs = view.findChild<QTabBar *>(QStringLiteral("settingsTabs"));
    QCOMPARE(tabs->tabText(1), QStringLiteral("People · 1 waiting"));
    QCOMPARE(view.paneTitle(), QStringLiteral("Sharing · 1 waiting"));
    view.showPage(SharingView::Page::People);
    QCOMPARE(labels(view, "settingsHeading"),
             (QStringList{QStringLiteral("Waiting for you"), QStringLiteral("Shared now")}));
    QVERIFY(allLabels(view).contains(QStringLiteral("alice wants to join pane “logs”")));
    QCOMPARE(allLabels(view).filter(QStringLiteral("48213")).size(), 1);
    QCOMPARE(buttons(view, QStringLiteral("Admit as editor")).size(), 1);
    // The keyboard goes to Refuse, never to Admit.
    QPushButton *refuse = nullptr;
    for (QPushButton *press : view.findChildren<QPushButton *>())
        if (press->text() == QStringLiteral("Refuse") && press->isVisibleTo(&view)) refuse = press;
    QVERIFY(refuse);
    view.focusView();
    QCOMPARE(view.focusWidget(), refuse);

    // Answered, the row goes and the tab says so.
    QString knocked;
    view.onKnockAnswer = [&](const QString &participant, bool, const QString &) { knocked = participant; };
    refuse->click();
    QCOMPARE(knocked, QStringLiteral("a1"));
    QCOMPARE(tabs->tabText(1), QStringLiteral("People"));
    QCOMPARE(labels(view, "settingsHeading"), QStringList{QStringLiteral("Shared now")});
}

void SharingTest::guestsGetABlockEachAndTheOptionsNoteOnce()
{
    Model model;
    model.setRemote(true, QStringLiteral("relay-terminal.ai"), true, 0);
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")},
                          {QStringLiteral("p2"), QStringLiteral("logs")},
                          {QStringLiteral("p3"), QStringLiteral("deploy")}});
    model.setParticipants(items(R"([{"id":"a1","name":"alice","role":"editor","panes":["p1"],
                                     "platform":"Chrome","expires":600}])"),
                          items(R"([{"id":"i1","panes":["p3"],"role":"viewer","uses":1,
                                     "expires":3600}])"));
    SharingView view;
    view.setModel(&model);
    view.refresh();

    QCOMPARE(labels(view, "settingsHeading"), QStringList{QStringLiteral("Shared now")});
    QCOMPARE(labels(view, "settingsSubheading"),
             (QStringList{QStringLiteral("Pane “build”"), QStringLiteral("Pane “deploy”")}));
    // Two checkboxes per block, side by side, with the sentences as tooltips…
    int prompts = 0, present = 0;
    for (const QCheckBox *box : view.findChildren<QCheckBox *>()) {
        if (!box->isVisibleTo(&view)) continue;
        if (box->text() == QStringLiteral("Guest prompts run immediately")) {
            ++prompts;
            QCOMPARE(box->toolTip(), promptsImmediateSentence());
        } else if (box->text() == QStringLiteral("Guests can act only while I'm here")) {
            ++present;
            QCOMPARE(box->toolTip(), presentOnlySentence());
        }
    }
    QCOMPARE(prompts, 2);
    QCOMPARE(present, 2);
    // …and the note that spells both out exactly once, under the section.
    QCOMPARE(labels(view, "sharingOptionsNote").size(), 1);
    QCOMPARE(labels(view, "sharingOptionsNote").first(),
             promptsImmediateSentence() + QLatin1Char(' ') + presentOnlySentence());
    // The three buttons, per visited pane; the quiet one is not a row and gets no button.
    QCOMPARE(buttons(view, QStringLiteral("Invite someone…")).size(), 2);
    QCOMPARE(buttons(view, QStringLiteral("Pause guests")).size(), 2);
    QCOMPARE(buttons(view, QStringLiteral("End sharing")).size(), 2);
    QVERIFY(buttons(view, QStringLiteral("Invite…")).isEmpty());
    QVERIFY(!allLabels(view).contains(QStringLiteral("logs")));
    QVERIFY(allLabels(view).contains(QStringLiteral("Viewer link i1 — 1 use left, expires in 60 min")));
    QVERIFY(allLabels(view).contains(QStringLiteral("alice — editor")));

    // The pane the view was opened from comes first within its section, and only there.
    view.focusPane(QStringLiteral("p3"));
    QCOMPARE(labels(view, "settingsSubheading"),
             (QStringList{QStringLiteral("Pane “deploy”"), QStringLiteral("Pane “build”")}));
    QCOMPARE(labels(view, "settingsHeading"), QStringList{QStringLiteral("Shared now")});

    // Invite someone… on a block opens the form on that block's scope, and still tells the window.
    QString invited;
    view.onInvite = [&](const QString &pane) { invited = pane; };
    view.onScopes = [this] { return catalogue(); };
    QList<QPushButton *> invites;
    for (QPushButton *press : view.findChildren<QPushButton *>())
        if (press->text() == QStringLiteral("Invite someone…") && press->isVisibleTo(&view)) invites << press;
    QCOMPARE(invites.size(), 2);
    invites.at(1)->click();
    QCOMPARE(invited, QStringLiteral("p1"));
    auto *pick = view.findChild<QComboBox *>(QStringLiteral("sharingScopePick"));
    QVERIFY(pick->isVisibleTo(&view));
    // p1 is not in the window's catalogue (the catalogue is another window's): the scope asked
    // for is still the one picked.
    QCOMPARE(pick->currentText(), QStringLiteral("build"));
}

// A block is headed by what its guests were let into: the pane, the whole tab (by the tab's
// name, which only the window knows), or everything.
void SharingTest::blocksAreHeadedByTheirScope()
{
    Model model;
    model.setRemote(true, QStringLiteral("relay-terminal.ai"), true, 0);
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build"), QString()},
                          {QStringLiteral("p2"), QStringLiteral("logs"), QStringLiteral("all-tabs")},
                          {QStringLiteral("p3"), QStringLiteral("deploy"), QStringLiteral("t2")},
                          {QStringLiteral("p4"), QStringLiteral("notes"), QStringLiteral("t2")},
                          {QStringLiteral("p5"), QStringLiteral("scratch"), QStringLiteral("t9")}});
    model.setParticipants(items(R"([
        {"id":"a1","name":"alice","role":"editor","panes":["p1"]},
        {"id":"b2","name":"bob","role":"viewer","panes":["p2"]},
        {"id":"c3","name":"carol","role":"viewer","panes":["p3","p4"]},
        {"id":"d4","name":"dave","role":"viewer","panes":["p5"]}
    ])"), items(R"([{"id":"i1","panes":["p3","p4"],"role":"viewer","uses":1,"expires":3600}])"));
    SharingView view;
    view.setModel(&model);
    view.onScopes = [this] { return catalogue(); };
    view.refresh();
    QCOMPARE(labels(view, "settingsSubheading"),
             (QStringList{QStringLiteral("Pane “build”"), QStringLiteral("Everything"),
                          QStringLiteral("Tab “thesis”"), QStringLiteral("Tab “t9”")}));
    // Two panes under one tab: one heading, each pane named under it, the tab's link once.
    QCOMPARE(allLabels(view).filter(QStringLiteral("Viewer link i1")).size(), 1);
    const QStringList details = labels(view, "settingsRowDetail");
    QVERIFY(details.contains(QStringLiteral("Pane “deploy”")));
    QVERIFY(details.contains(QStringLiteral("Pane “notes”")));
    QVERIFY(!details.contains(QStringLiteral("Pane “build”")));
    QCOMPARE(buttons(view, QStringLiteral("End sharing")).size(), 5);

    // Invite someone… under the tab's heading asks for the tab.
    Scope made;
    view.onCreateInvite = [&](const Scope &scope, const QString &, int, int) { made = scope; };
    QList<QPushButton *> invites;
    for (QPushButton *press : view.findChildren<QPushButton *>())
        if (press->text() == QStringLiteral("Invite someone…") && press->isVisibleTo(&view)) invites << press;
    invites.at(2)->click();
    auto *pick = view.findChild<QComboBox *>(QStringLiteral("sharingScopePick"));
    QCOMPARE(pick->currentText(), QStringLiteral("Tab “thesis” (1 pane, and any you add)"));
    for (QPushButton *press : view.findChildren<QPushButton *>())
        if (press->text() == QStringLiteral("Make a link")) press->click();
    QCOMPARE(made.kind, Scope::Kind::Tab);
    QCOMPARE(made.id, QStringLiteral("t2"));
}

// tick() moves every clock on the pane: the requests', the pairing code's and the meeting code's.
void SharingTest::tickMovesEveryClock()
{
    Model model;
    model.setRemote(true, QStringLiteral("relay-terminal.ai"), true, 0);
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")}});
    model.addKnock(json(R"({"participant":"a1","name":"alice","role":"viewer","pane":"p1",
        "code":"48213"})"), QDateTime::currentMSecsSinceEpoch() - 30'000);
    SharingView view;
    view.setModel(&model);
    view.refresh();
    Service service;
    service.running = true;
    service.online = true;
    service.base = service.onlineBase = QStringLiteral("https://desk.example/");
    view.setService(service);
    view.startInvite(Scope{});
    view.onCreateCode = [](const Scope &, const QString &) {};
    for (QPushButton *press : view.findChildren<QPushButton *>())
        if (press->text() == QStringLiteral("Make a code")) press->click();
    view.showCode(QStringLiteral("WXYZ"), QStringLiteral("2468"), 600);
    view.tick();
    QStringList everything = allLabels(view);
    QVERIFY2(everything.filter(QStringLiteral("1:")).size() >= 1, qPrintable(everything.join(QStringLiteral(" | "))));
    QVERIFY(everything.contains(QStringLiteral("Expires in 10:00")) || everything.contains(QStringLiteral("Expires in 9:59")));
    // Copy says the one line a friend needs, at the service's address.
    for (QPushButton *press : view.findChildren<QPushButton *>())
        if (press->text() == QStringLiteral("Copy") && press->isVisibleTo(&view)) press->click();
    QCOMPARE(QGuiApplication::clipboard()->text(),
             QStringLiteral("Join my Relay pane at https://desk.example/join — meeting code WXYZ, PIN 2468"));
    // The sidecar says it was used: struck through, Copy gone, the sentence says where to look.
    view.showCodeState(QStringLiteral("WXYZ"), QStringLiteral("used"), 0);
    QVERIFY(buttons(view, QStringLiteral("Copy")).isEmpty());
    QVERIFY(allLabels(view).contains(QStringLiteral("Used")));
}

// Card #3B1B: the agent docked under both pages. Its `screen` names each shared pane by token (so
// its answer can link `pane:<token>`), its guests and the pane this was opened from; End sharing
// is offered only while that pane is shared, and runs the pane's own End sharing; a `pane:` link
// goes to the window through onFocusPane.
void SharingTest::theDockedAgentReadsThePaneAndEndsOnlyASharedOne()
{
    Model model;
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")}});
    model.setParticipants(items(R"([{"id":"a1","name":"alice","role":"editor","panes":["p1"]}])"), {});
    SharingView view;
    view.setModel(&model);
    QVERIFY(view.agentDock());
    view.focusPane(QStringLiteral("p9"));
    view.refresh();
    QString screen = view.agentContext()->spec().screen;
    QVERIFY2(screen.contains(QStringLiteral("- pane:p1 (build) · 1 guest")), qPrintable(screen));
    QVERIFY2(screen.contains(QStringLiteral("Guests: alice (editor) on build")), qPrintable(screen));
    QVERIFY2(screen.contains(QStringLiteral("Opened from: pane:p9")), qPrintable(screen));
    QVERIFY(!view.agentContext()->actions().at(1).enabled);   // p9 is not shared
    QStringList ended;
    view.onEndShare = [&ended](const QString &pane) { ended << pane; };
    view.focusPane(QStringLiteral("p1"));
    const QList<relay::agent::Action> row = view.agentContext()->actions();
    QVERIFY(row.at(1).enabled);
    row.at(1).run();
    QCOMPARE(ended, QStringList{QStringLiteral("p1")});
    QString focused;
    view.onFocusPane = [&focused](const QString &token) { focused = token; return true; };
    relay::links::Target link;
    link.valid = true;
    link.target = relay::links::paneTarget(QStringLiteral("p1"));
    QVERIFY(view.agentContext()->resolveLink(link));
    QCOMPARE(focused, QStringLiteral("p1"));
}

QTEST_MAIN(SharingTest)
#include "sharingpane_test.moc"
