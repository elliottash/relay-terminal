// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Remote control as a service (card #PH0N, phase 1): the switch in Options › Remote, the address
// it remembers, the `start` line the sidecar is sent and the `remote_state` line it sends back.
//
// The contract this pins down is the one the Python sidecar is being built against in parallel, so
// every field name here is load-bearing: `always` and `address` going out, and
// `on/address/base/online/devices/reason` coming back.
#include "RemoteSettings.h"
#include "SettingsCache.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

namespace rs = relay::remotesettings;

namespace {

// One entry of the sidecar's `addresses` array, as remote/gui_host.py writes it.
QJsonObject entry(const QString &kind, const QString &value, bool available,
                  const QString &label = QString(), const QString &reason = QString())
{
    return {{QStringLiteral("kind"), kind},
            {QStringLiteral("value"), value},
            {QStringLiteral("available"), available},
            {QStringLiteral("label"), label},
            {QStringLiteral("where"), QStringLiteral("this network")},
            {QStringLiteral("reason"), reason}};
}

// The list a running sidecar sends on this machine: a tailnet name, the hosted rendezvous, one
// LAN address, and the public tunnel that is never an always-on address.
QJsonArray liveAddresses()
{
    return {entry(QStringLiteral("tailscale"), QStringLiteral("spark.tail0.ts.net"), true,
                  QStringLiteral("https://spark.tail0.ts.net — no certificate warning")),
            entry(QStringLiteral("hosted"), QStringLiteral("relay-terminal.ai"), true,
                  QStringLiteral("relay-terminal.ai — works from anywhere")),
            entry(QStringLiteral("ip"), QStringLiteral("192.168.1.9"), true,
                  QStringLiteral("192.168.1.9 — reachable from this network")),
            entry(QStringLiteral("cloudflare"), QStringLiteral("cloudflare"), true,
                  QStringLiteral("a public link"))};
}

QStringList valuesOf(const QList<rs::Choice> &choices)
{
    QStringList values;
    for (const rs::Choice &choice : choices) values << choice.value;
    return values;
}

const relay::SettingRow *rowWithId(const relay::SettingsSection &section, const QString &id)
{
    for (const relay::SettingRow &row : section.rows)
        if (row.id == id) return &row;
    return nullptr;
}

}  // namespace

class RemoteSettingsTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminalTest"));
        QCoreApplication::setApplicationName(QStringLiteral("RemoteSettingsTest"));
    }

    void init() { QSettings().clear(); relay::settings::invalidate(); }

    // ----- what is remembered ------------------------------------------------------------------

    // Off on a fresh profile: a desktop nobody asked to publish itself publishes nothing, and the
    // address it would use is the hosted one, the only one that answers from a bus.
    void theSwitchIsOffAndTheAddressIsHostedByDefault() {
        QVERIFY(!rs::alwaysOn());
        QCOMPARE(rs::address(), QStringLiteral("relay-terminal.ai"));
        QCOMPARE(rs::alwaysOnKey(), QStringLiteral("remote/alwaysOn"));
        QCOMPARE(rs::addressKey(), QStringLiteral("remote/address"));
    }

    // Both survive the restart that used to end the service: they are QSettings keys, read back
    // through the settings cache, which the writes invalidate.
    void theSwitchAndTheAddressAreRemembered() {
        rs::setAlwaysOn(true);
        rs::setAddress(rs::tailnetValue());
        QVERIFY(rs::alwaysOn());
        QCOMPARE(rs::address(), QStringLiteral("tailscale"));
        QCOMPARE(QSettings().value(rs::alwaysOnKey()).toBool(), true);
        QCOMPARE(QSettings().value(rs::addressKey()).toString(), QStringLiteral("tailscale"));
        rs::setAlwaysOn(false);
        QVERIFY(!rs::alwaysOn());
    }

    // ----- the `start` line --------------------------------------------------------------------

    // What the GUI sends the sidecar while the switch is on. `always` says "register and stay
    // registered"; `address` is the remembered pick, in the picker's own value strings.
    void startCarriesAlwaysAndAddressWhenTheSwitchIsOn() {
        rs::setAlwaysOn(true);
        rs::setAddress(rs::hostedValue());
        const QJsonObject start = rs::startMessage(QStringLiteral("this desktop"), rs::alwaysOn(),
                                                   rs::address());
        QCOMPARE(start.value(QStringLiteral("t")).toString(), QStringLiteral("start"));
        QCOMPARE(start.value(QStringLiteral("tls")).toBool(), true);
        QCOMPARE(start.value(QStringLiteral("name")).toString(), QStringLiteral("this desktop"));
        QCOMPARE(start.value(QStringLiteral("always")).toBool(), true);
        QCOMPARE(start.value(QStringLiteral("address")).toString(), QStringLiteral("relay-terminal.ai"));
    }

    // With the switch off the field is still there and false, so the sidecar never has to guess
    // from its absence what an older GUI meant.
    void startSaysAlwaysFalseWhenTheSwitchIsOff() {
        const QJsonObject start = rs::startMessage(QStringLiteral("this desktop"), false,
                                                   QStringLiteral("192.168.1.9"));
        QVERIFY(start.contains(QStringLiteral("always")));
        QCOMPARE(start.value(QStringLiteral("always")).toBool(), false);
        QCOMPARE(start.value(QStringLiteral("address")).toString(), QStringLiteral("192.168.1.9"));
    }

    // An empty address is left out rather than sent as "": the sidecar's own preferred address is
    // then what it starts on, which is what it did before this card.
    void startLeavesAnEmptyAddressOut() {
        const QJsonObject start = rs::startMessage(QStringLiteral("this desktop"), true, QString());
        QVERIFY(!start.contains(QStringLiteral("address")));
    }

    // ----- `remote_state` ----------------------------------------------------------------------

    void remoteStateIsParsed() {
        const rs::State state = rs::parseState(
            {{QStringLiteral("t"), QStringLiteral("remote_state")},
             {QStringLiteral("on"), true},
             {QStringLiteral("address"), QStringLiteral("relay-terminal.ai")},
             {QStringLiteral("base"), QStringLiteral("https://join.relay-terminal.ai/d/abc")},
             {QStringLiteral("online"), true},
             {QStringLiteral("devices"), 2},
             {QStringLiteral("reason"), QString()}});
        QVERIFY(state.on);
        QVERIFY(state.online);
        QCOMPARE(state.address, QStringLiteral("relay-terminal.ai"));
        QCOMPARE(state.base, QStringLiteral("https://join.relay-terminal.ai/d/abc"));
        QCOMPARE(state.devices, 2);
        QVERIFY(state.reason.isEmpty());
    }

    // The line the chrome shows. It says where, and either how many devices are connected or why
    // nothing is: "on" with no registration behind it is the failure this indicator exists for.
    void theStatusLineSaysWhereAndHowMany() {
        rs::State state;
        QCOMPARE(rs::statusLine(state), QStringLiteral("Remote control off"));
        state.on = true;
        state.online = true;
        state.address = QStringLiteral("relay-terminal.ai");
        state.devices = 2;
        QCOMPARE(rs::statusLine(state),
                 QStringLiteral("Remote control on · relay-terminal.ai · 2 devices"));
        state.devices = 1;
        QCOMPARE(rs::statusLine(state),
                 QStringLiteral("Remote control on · relay-terminal.ai · 1 device"));
        state.devices = 0;
        QCOMPARE(rs::statusLine(state),
                 QStringLiteral("Remote control on · relay-terminal.ai · no phone connected"));
        state.online = false;
        state.reason = QStringLiteral("relay-terminal.ai did not answer");
        QCOMPARE(rs::statusLine(state),
                 QStringLiteral("Remote control on · relay-terminal.ai · offline: "
                                "relay-terminal.ai did not answer"));
        state.address = rs::tailnetValue();
        state.reason.clear();
        QCOMPARE(rs::statusLine(state), QStringLiteral("Remote control on · your tailnet · offline"));
    }

    // ----- the address picker ------------------------------------------------------------------

    // Before anything has started there is no live list, and the two addresses that do not depend
    // on this machine's interfaces are still offered.
    void thePickerOffersHostedAndTheTailnetWithNoSidecar() {
        const QList<rs::Choice> choices = rs::addressChoices({}, rs::address());
        QCOMPARE(valuesOf(choices), QStringList({QStringLiteral("relay-terminal.ai"),
                                                 QStringLiteral("tailscale")}));
    }

    // With the sidecar running its own list joins them: the LAN address by its number, the tailnet
    // entry by the word (the name is this machine's and goes in the label), and never the public
    // tunnel, which stops answering when sharing stops.
    void thePickerAddsThisMachinesAddressesAndNeverTheTunnel() {
        const QList<rs::Choice> choices = rs::addressChoices(liveAddresses(), rs::address());
        QCOMPARE(valuesOf(choices), QStringList({QStringLiteral("relay-terminal.ai"),
                                                 QStringLiteral("tailscale"),
                                                 QStringLiteral("192.168.1.9")}));
        QVERIFY(choices.at(1).label.contains(QStringLiteral("spark.tail0.ts.net")));
        QVERIFY(choices.at(2).label.contains(QStringLiteral("192.168.1.9")));
    }

    // An entry this machine cannot serve keeps its sentence instead of vanishing: "there is no
    // such option" and "you have not run one command yet" look identical otherwise.
    void anUnavailableEntryKeepsItsReason() {
        QJsonArray live{entry(QStringLiteral("tailscale"), QString(), false, QString(),
                              QStringLiteral("tailscale is not logged in on this machine")),
                        entry(QStringLiteral("hosted"), QStringLiteral("relay-terminal.ai"), false,
                              QString(), QStringLiteral("relay-terminal.ai did not answer"))};
        const QList<rs::Choice> choices = rs::addressChoices(live, rs::address());
        QCOMPARE(choices.size(), 2);
        QCOMPARE(choices.at(0).detail, QStringLiteral("relay-terminal.ai did not answer"));
        QCOMPARE(choices.at(1).detail, QStringLiteral("tailscale is not logged in on this machine"));
    }

    // A remembered address this machine is not offering right now is still the one the service is
    // on, so the picker shows it rather than silently moving to another.
    void aRememberedAddressIsAlwaysOffered() {
        rs::setAddress(QStringLiteral("10.1.2.3"));
        const QList<rs::Choice> choices = rs::addressChoices(liveAddresses(), rs::address());
        QVERIFY(valuesOf(choices).contains(QStringLiteral("10.1.2.3")));
    }

    // ----- Options › Remote --------------------------------------------------------------------

    // The page: the switch, one sentence saying what it does, the address, and the live state.
    void theSectionIsTheSwitchTheSentenceAndTheAddress() {
        rs::SectionHooks hooks;
        hooks.addresses = liveAddresses();
        hooks.state.on = true;
        hooks.state.online = true;
        hooks.state.address = QStringLiteral("relay-terminal.ai");
        hooks.state.devices = 1;
        const relay::SettingsSection section = rs::section(hooks);
        QCOMPARE(section.id, QStringLiteral("remote"));
        QCOMPARE(section.title, QStringLiteral("Remote"));
        const relay::SettingRow *toggle = rowWithId(section, rs::alwaysOnRowId());
        QVERIFY(toggle);
        QCOMPARE(toggle->kind, relay::SettingRow::Toggle);
        QCOMPARE(toggle->label, QStringLiteral("Remote control"));
        QVERIFY(toggle->detail.contains(QStringLiteral("Every pane on this desktop is reachable")));
        QVERIFY(toggle->detail.contains(QStringLiteral("guests still need an invite")));
        const relay::SettingRow *address = rowWithId(section, rs::addressRowId());
        QVERIFY(address);
        QCOMPARE(address->kind, relay::SettingRow::Choice);
        QCOMPARE(address->options, QStringList({QStringLiteral("relay-terminal.ai"),
                                                QStringLiteral("tailscale"),
                                                QStringLiteral("192.168.1.9")}));
        QCOMPARE(address->current, QStringLiteral("relay-terminal.ai"));
        const relay::SettingRow *state = rowWithId(section, QStringLiteral("info:remote.state"));
        QVERIFY(state);
        QVERIFY(state->label.startsWith(QStringLiteral("Remote control on · relay-terminal.ai · 1 device")));
    }

    // The rows write through the hooks — which is how RemoteShare hears about it at all — and fall
    // back to the settings themselves when nothing is wired, so the page is never a dead switch.
    void theRowsWriteThroughTheirHooks() {
        bool asked = false;
        QString picked;
        rs::SectionHooks hooks;
        hooks.setAlwaysOn = [&asked](bool on) { asked = on; };
        hooks.setAddress = [&picked](const QString &value) { picked = value; };
        const relay::SettingsSection section = rs::section(hooks);
        rowWithId(section, rs::alwaysOnRowId())->onToggle(true);
        QVERIFY(asked);
        rowWithId(section, rs::addressRowId())->onChoose(rs::tailnetValue());
        QCOMPARE(picked, QStringLiteral("tailscale"));

        const relay::SettingsSection plain = rs::section(rs::SectionHooks{});
        rowWithId(plain, rs::alwaysOnRowId())->onToggle(true);
        QVERIFY(rs::alwaysOn());
        rowWithId(plain, rs::addressRowId())->onChoose(QStringLiteral("192.168.1.9"));
        QCOMPARE(rs::address(), QStringLiteral("192.168.1.9"));
    }

    // "Reset to defaults" reaches both rows, and off / hosted is what Relay ships with.
    void resettingThePagePutsTheServiceBack() {
        rs::setAlwaysOn(true);
        rs::setAddress(QStringLiteral("192.168.1.9"));
        const relay::SettingsSection section = rs::section(rs::SectionHooks{});
        const relay::SettingRow *toggle = rowWithId(section, rs::alwaysOnRowId());
        const relay::SettingRow *address = rowWithId(section, rs::addressRowId());
        QVERIFY(toggle->changed);
        QVERIFY(address->changed);
        toggle->reset();
        address->reset();
        QVERIFY(!rs::alwaysOn());
        QCOMPARE(rs::address(), QStringLiteral("relay-terminal.ai"));
    }

    // ----- one entry point: the plug menu (#FR1C) ----------------------------------------------

    // The plug at the top right of the window. "Pair a phone…" is the first row a person can
    // choose — the status line above it is not clickable — and the switch itself is the row under
    // it, so turning remote control on is one click from the window rather than three from
    // Options ("even step 1 of enabling remote, that was not obvious to me").
    void thePlugMenuLeadsWithPairingAndCarriesTheSwitch() {
        rs::State off;
        const QList<rs::PlugItem> items = rs::plugMenu(off);
        QCOMPARE(items.size(), 7);
        QCOMPARE(items[0].kind, rs::PlugItem::Status);
        QCOMPARE(items[0].id, QStringLiteral("remote.status"));
        QCOMPARE(items[0].label, QStringLiteral("Remote control off"));
        QCOMPARE(items[1].kind, rs::PlugItem::Separator);
        QCOMPARE(items[2].kind, rs::PlugItem::Action);
        QCOMPARE(items[2].id, QStringLiteral("remote.pair"));
        QCOMPARE(items[2].label, QStringLiteral("Pair a phone…"));
        QCOMPARE(items[3].kind, rs::PlugItem::Toggle);
        QCOMPARE(items[3].id, QStringLiteral("remote.control"));
        QCOMPARE(items[3].label, QStringLiteral("Remote control: off"));
        QVERIFY(!items[3].checked);
        QCOMPARE(items[4].kind, rs::PlugItem::Separator);
        QCOMPARE(items[5].id, QStringLiteral("remote.join"));
        QCOMPARE(items[6].id, QStringLiteral("remote.openShared"));

        rs::State on;
        on.on = true;
        on.online = true;
        on.address = rs::hostedValue();
        on.devices = 2;
        const QList<rs::PlugItem> lit = rs::plugMenu(on);
        QCOMPARE(lit[0].label, QStringLiteral("Remote control on · relay-terminal.ai · 2 devices"));
        QCOMPARE(lit[3].label, QStringLiteral("Remote control: on"));
        QVERIFY(lit[3].checked);
        // Joining somebody else's session is still there, under the separator: the plug keeps
        // what it had and gains the two rows above.
        QStringList ids;
        for (const rs::PlugItem &item : lit) ids << item.id;
        QVERIFY(ids.contains(QStringLiteral("remote.openShared")));
    }

    // "Pair a phone" on a desktop whose switch is off turns it on the way Options would: the
    // switch itself, and the hosted address, which is then what the `start` line carries.
    void pairingTurnsTheSwitchOnWithTheHostedAddress() {
        QVERIFY(!rs::alwaysOn());
        QCOMPARE(rs::turnOnForPairing(), QStringLiteral("relay-terminal.ai"));
        QVERIFY(rs::alwaysOn());
        QCOMPARE(QSettings().value(rs::alwaysOnKey()).toBool(), true);
        QCOMPARE(QSettings().value(rs::addressKey()).toString(), QStringLiteral("relay-terminal.ai"));
        const QJsonObject start = rs::startMessage(QStringLiteral("this desktop"), rs::alwaysOn(),
                                                   rs::address());
        QCOMPARE(start.value(QStringLiteral("always")).toBool(), true);
        QCOMPARE(start.value(QStringLiteral("address")).toString(), QStringLiteral("relay-terminal.ai"));
    }

    // An address picked earlier is kept: "pair a phone" is not the place to move a desktop off
    // the tailnet somebody deliberately put it on.
    void pairingKeepsAnAddressTheOwnerAlreadyPicked() {
        rs::setAddress(rs::tailnetValue());
        QCOMPARE(rs::turnOnForPairing(), QStringLiteral("tailscale"));
        QVERIFY(rs::alwaysOn());
        QCOMPARE(rs::address(), QStringLiteral("tailscale"));
    }

    // ----- the pairing code (#FR1C) --------------------------------------------------------------
    // The wire contract the sidecar half is being built against in parallel, so every field name
    // here is load-bearing: `pair_code` out, `pair_code` and `pair_code_state` back,
    // `pair_code_revoke` when the dialog closes.

    void theDialogAsksForACodeAndWithdrawsIt() {
        const QJsonObject ask = rs::pairCodeRequest();
        QCOMPARE(ask.value(QStringLiteral("t")).toString(), QStringLiteral("pair_code"));
        QCOMPARE(ask.size(), 1);
        const QJsonObject revoke = rs::pairCodeRevoke(QStringLiteral("ABCD"));
        QCOMPARE(revoke.value(QStringLiteral("t")).toString(), QStringLiteral("pair_code_revoke"));
        QCOMPARE(revoke.value(QStringLiteral("code")).toString(), QStringLiteral("ABCD"));
    }

    void theCodeAndItsStateAreParsed() {
        rs::PairCode code;
        QVERIFY(rs::parsePairCode(QJsonObject{{QStringLiteral("t"), QStringLiteral("pair_code")},
                                              {QStringLiteral("code"), QStringLiteral("ABCD")},
                                              {QStringLiteral("pin"), QStringLiteral("4829")},
                                              {QStringLiteral("expires"), 600}},
                                  &code));
        QCOMPARE(code.code, QStringLiteral("ABCD"));
        QCOMPARE(code.pin, QStringLiteral("4829"));
        QCOMPARE(code.expires, 600);
        // A sidecar that leaves the ttl out means the ten minutes a meeting code always lasts.
        rs::PairCode bare;
        QVERIFY(rs::parsePairCode(QJsonObject{{QStringLiteral("code"), QStringLiteral("WXYZ")}}, &bare));
        QCOMPARE(bare.expires, 600);
        QVERIFY(!rs::parsePairCode(QJsonObject{{QStringLiteral("t"), QStringLiteral("pair_code")}}, nullptr));

        rs::PairCodeState state;
        QVERIFY(rs::parsePairCodeState(
            QJsonObject{{QStringLiteral("t"), QStringLiteral("pair_code_state")},
                        {QStringLiteral("code"), QStringLiteral("ABCD")},
                        {QStringLiteral("state"), QStringLiteral("burned")},
                        {QStringLiteral("failures"), 3}},
            &state));
        QCOMPARE(state.code, QStringLiteral("ABCD"));
        QCOMPARE(state.state, QStringLiteral("burned"));
        QCOMPARE(state.failures, 3);
        QVERIFY(!rs::parsePairCodeState(QJsonObject{{QStringLiteral("code"), QStringLiteral("ABCD")}},
                                        nullptr));
    }

    // The four states of the row beside the QR: live with a countdown, then used, burned or
    // expired — each struck through, each offering a new code.
    void theCodeRowShowsItsFourStates() {
        const rs::PairCode code{QStringLiteral("ABCD"), QStringLiteral("4829"), 600};
        const rs::PairCodeRow live = rs::pairCodeRow(code, QString(), 0, 598);
        QCOMPARE(live.value, QStringLiteral("ABCD 4829"));
        QVERIFY(!live.dead);
        QCOMPARE(live.clock, QStringLiteral("Expires in 9:58"));
        QVERIFY(!live.again);
        QVERIFY(live.note.contains(QStringLiteral("five digits")));

        const rs::PairCodeRow used = rs::pairCodeRow(code, QStringLiteral("used"), 0, 0);
        QCOMPARE(used.clock, QStringLiteral("Used"));
        QVERIFY(used.dead);
        QVERIFY(used.again);

        const rs::PairCodeRow burned = rs::pairCodeRow(code, QStringLiteral("burned"), 3, 0);
        QCOMPARE(burned.clock, QStringLiteral("Closed"));
        QVERIFY(burned.dead);
        QVERIFY(burned.again);
        QVERIFY(burned.note.startsWith(QStringLiteral("Three wrong PINs")));

        // Closed with no wrong guess is a revoked code, not three misses.
        const rs::PairCodeRow revoked = rs::pairCodeRow(code, QStringLiteral("burned"), 0, 0);
        QVERIFY(revoked.note.contains(QStringLiteral("before any phone used it")));

        const rs::PairCodeRow expired = rs::pairCodeRow(code, QStringLiteral("expired"), 0, 0);
        QCOMPARE(expired.clock, QStringLiteral("Expired"));
        QVERIFY(expired.dead);
        QVERIFY(expired.again);
    }

    // A sidecar from before this card ignores `pair_code` rather than refusing it, so the dialog
    // waits three seconds and then says the QR is the way in.
    void aSidecarThatCannotMakeACodeLeavesTheQr() {
        QCOMPARE(rs::pairCodeWaitMs(), 3000);
        QCOMPARE(rs::pairCodeUnavailable(),
                 QStringLiteral("This desktop cannot make a code; scan the QR instead."));
        QVERIFY(rs::pairCodeHeading().contains(QStringLiteral("On your phone")));
        // And the line the dialog puts at the top, because pairing turned the switch on itself.
        QVERIFY(rs::pairAlwaysOnLine().startsWith(QStringLiteral("Remote control is on")));
        QVERIFY(rs::pairAlwaysOnLine().contains(QStringLiteral("plug menu")));
    }
};

QTEST_MAIN(RemoteSettingsTests)
#include "remotesettings_test.moc"
