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
};

QTEST_MAIN(RemoteSettingsTests)
#include "remotesettings_test.moc"
