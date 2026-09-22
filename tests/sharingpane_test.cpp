// SPDX-License-Identifier: AGPL-3.0-or-later
// The multiplayer model behind the Sharing pane (#W5N2, docs/REMOTE-PROTOCOL.md section 10).
// Everything here is the logic the owner's answers depend on — who is here, what is waiting, whose
// countdown has run out, what the pane header says — so it is exercised without a hub and without
// a display.
#include "SharingPane.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCheckBox>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
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
QStringList labels(const QWidget &view, const char *name)
{
    QStringList texts;
    for (const QLabel *label : view.findChildren<QLabel *>(QLatin1String(name)))
        texts << label->text();
    return texts;
}

QStringList allLabels(const QWidget &view)
{
    QStringList texts;
    for (const QLabel *label : view.findChildren<QLabel *>()) texts << label->text();
    return texts;
}

QStringList buttons(const QWidget &view, const QString &text)
{
    QStringList found;
    for (const QPushButton *press : view.findChildren<QPushButton *>())
        if (press->text() == text) found << press->text();
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
    void quietPanesAreOneRowEach();
    void guestsGetABlockEachAndTheOptionsNoteOnce();
    void aKnockComesFirstAndNamesItsPane();
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
    QCOMPARE(model.chip(QStringLiteral("p1"), true).text, QStringLiteral("phone"));
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

// The hub says who is connected and who holds each pane's control token on the `participants`
// line itself, so a Sharing pane opened after a handoff reads the same state as one that watched
// it happen (remote/gui_host.py, report_participants).
void SharingTest::theParticipantsLineCarriesPresenceAndControl()
{
    Model model;
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")}});
    model.setParticipants(items(R"([
        {"id":"a1","name":"alice","role":"editor","panes":["p1"],"online":true,"driving":["p1"]},
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

// #SHRP: the top line is the model's, so the window feeds it and the view only prints it.
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

    // The view prints exactly that line, with Pair a phone… beside it, in every state.
    SharingView view;
    view.setModel(&model);
    view.refresh();
    QCOMPARE(labels(view, "sharingTopLine"), QStringList{model.topLine()});
    QCOMPARE(buttons(view, QStringLiteral("Pair a phone…")).size(), 1);
    bool paired = false;
    view.onPairPhone = [&] { paired = true; };
    for (QPushButton *press : view.findChildren<QPushButton *>())
        if (press->text() == QStringLiteral("Pair a phone…")) press->click();
    QVERIFY(paired);
}

// Three panes nobody is visiting: one section, one row and one Invite… each — none of the
// per-pane kit that made the pane the same block three times over (#SHRP).
void SharingTest::quietPanesAreOneRowEach()
{
    Model model;
    model.setRemote(true, QStringLiteral("relay-terminal.ai"), true, 0);
    model.setSharedPanes({{QStringLiteral("p1"), QStringLiteral("build")},
                          {QStringLiteral("p2"), QStringLiteral("logs")},
                          {QStringLiteral("p3"), QStringLiteral("deploy")}});
    SharingView view;
    view.setModel(&model);
    view.refresh();

    QCOMPARE(labels(view, "settingsHeading"), QStringList{QStringLiteral("Nobody is visiting")});
    QVERIFY(labels(view, "settingsSubheading").isEmpty());
    const QStringList rows = labels(view, "settingsRowLabel");
    QVERIFY2(rows.contains(QStringLiteral("build")) && rows.contains(QStringLiteral("logs"))
                 && rows.contains(QStringLiteral("deploy")), qPrintable(rows.join(QStringLiteral(" | "))));
    QCOMPARE(buttons(view, QStringLiteral("Invite…")).size(), 3);
    QVERIFY(view.findChildren<QCheckBox *>().isEmpty());
    QVERIFY(buttons(view, QStringLiteral("Pause guests")).isEmpty());
    QVERIFY(buttons(view, QStringLiteral("End sharing")).isEmpty());
    const QStringList everything = allLabels(view);
    for (const QString &text : everything) {
        QVERIFY2(!text.contains(QStringLiteral("This share")), qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("Nobody here yet")), qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("not guests and are not listed here")), qPrintable(text));
        QVERIFY2(!text.startsWith(QStringLiteral("Pane “")), qPrintable(text));
    }
    QVERIFY(everything.contains(QStringLiteral("Every pane is reachable from your phones. To let "
                                               "someone else in, invite them.")));

    // Invite… on a row asks for that row's pane.
    QString invited;
    view.onInvite = [&](const QString &pane) { invited = pane; };
    QList<QPushButton *> invites;
    for (QPushButton *press : view.findChildren<QPushButton *>())
        if (press->text() == QStringLiteral("Invite…")) invites << press;
    invites.at(1)->click();
    QCOMPARE(invited, QStringLiteral("p2"));

    // Remote control off: the sentence says so instead of promising the phones.
    model.setRemote(false, QString(), false, 0);
    view.refresh();
    QVERIFY(allLabels(view).contains(QStringLiteral("Remote control is off, so your phones cannot "
                                                    "reach these panes. To let someone else in, "
                                                    "invite them.")));
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

    QCOMPARE(labels(view, "settingsHeading"),
             (QStringList{QStringLiteral("Guests"), QStringLiteral("Nobody is visiting")}));
    QCOMPARE(labels(view, "settingsSubheading"),
             (QStringList{QStringLiteral("Pane “build”"), QStringLiteral("Pane “deploy”")}));
    // Two checkboxes per block, side by side, with the sentences as tooltips…
    const QList<QCheckBox *> boxes = view.findChildren<QCheckBox *>();
    QCOMPARE(boxes.size(), 4);
    int prompts = 0, present = 0;
    for (const QCheckBox *box : boxes) {
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
    // The three buttons, per visited pane, and Invite… for the quiet one.
    QCOMPARE(buttons(view, QStringLiteral("Invite someone…")).size(), 2);
    QCOMPARE(buttons(view, QStringLiteral("Pause guests")).size(), 2);
    QCOMPARE(buttons(view, QStringLiteral("End sharing")).size(), 2);
    QCOMPARE(buttons(view, QStringLiteral("Invite…")).size(), 1);
    QVERIFY(allLabels(view).contains(QStringLiteral("logs")));
    QVERIFY(allLabels(view).contains(QStringLiteral("Viewer link i1 — 1 use left, expires in 60 min")));
    QVERIFY(allLabels(view).contains(QStringLiteral("alice — editor")));

    // The pane the view was opened from comes first within its section, and only there.
    view.focusPane(QStringLiteral("p3"));
    QCOMPARE(labels(view, "settingsSubheading"),
             (QStringList{QStringLiteral("Pane “deploy”"), QStringLiteral("Pane “build”")}));
    QCOMPARE(labels(view, "settingsHeading"),
             (QStringList{QStringLiteral("Guests"), QStringLiteral("Nobody is visiting")}));
}

void SharingTest::aKnockComesFirstAndNamesItsPane()
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
    view.refresh();

    QCOMPARE(labels(view, "settingsHeading"),
             (QStringList{QStringLiteral("Waiting for you"), QStringLiteral("Guests"),
                          QStringLiteral("Nobody is visiting")}));
    QVERIFY(allLabels(view).contains(QStringLiteral("alice wants to join pane “logs”")));
    QCOMPARE(allLabels(view).filter(QStringLiteral("48213")).size(), 1);
    QCOMPARE(buttons(view, QStringLiteral("Admit as editor")).size(), 1);
    QCOMPARE(view.paneTitle(), QStringLiteral("Sharing · 1 waiting"));
    // The top line is still the first thing on the pane.
    QCOMPARE(allLabels(view).first(), model.topLine());
}

QTEST_MAIN(SharingTest)
#include "sharingpane_test.moc"
