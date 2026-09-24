// SPDX-License-Identifier: AGPL-3.0-or-later
#include "RemoteSettings.h"

#include "SettingsCache.h"

#include <QSettings>

namespace relay::remotesettings {

namespace {

const QLatin1String kHosted("relay-terminal.ai");
const QLatin1String kTailnet("tailscale");
// The Sharing pane's public-tunnel entry. It is never offered here: a quick tunnel has no name
// until it is running and stops answering when sharing stops, which is the one thing an always-on
// address may not do.
const QLatin1String kCloudflare("cloudflare");

bool entryAvailable(const QJsonObject &entry)
{
    return !entry.contains(QStringLiteral("available"))
           || entry.value(QStringLiteral("available")).toBool();
}

}  // namespace

QString alwaysOnKey() { return QStringLiteral("remote/alwaysOn"); }
QString addressKey() { return QStringLiteral("remote/address"); }
QString hostedValue() { return kHosted; }
QString tailnetValue() { return kTailnet; }
QString alwaysOnRowId() { return QStringLiteral("option:") + alwaysOnKey(); }
QString addressRowId() { return QStringLiteral("option:") + addressKey(); }

// Read through the cache (card #057J): the window's once-a-second publish sweep asks whether the
// switch is on, and eight statx a second for one boolean is what that cache exists to stop. Every
// write here invalidates it, so the next read is the value just written.
bool alwaysOn() { return relay::settings::boolValue(alwaysOnKey(), false); }

void setAlwaysOn(bool on)
{
    QSettings().setValue(alwaysOnKey(), on);
    relay::settings::invalidate();
}

QString address()
{
    const QString remembered = relay::settings::stringValue(addressKey(), QString());
    return remembered.isEmpty() ? hostedValue() : remembered;
}

void setAddress(const QString &value)
{
    if (value.isEmpty()) QSettings().remove(addressKey());
    else QSettings().setValue(addressKey(), value);
    relay::settings::invalidate();
}

QList<Choice> addressChoices(const QJsonArray &live, const QString &remembered)
{
    QString tailnetName, tailnetReason, hostedReason;
    QList<Choice> locals;
    for (const QJsonValue &value : live) {
        const QJsonObject entry = value.toObject();
        const QString kind = entry.value(QStringLiteral("kind")).toString();
        const bool available = entryAvailable(entry);
        const QString reason = entry.value(QStringLiteral("reason")).toString();
        if (kind == kTailnet) {
            if (available) tailnetName = entry.value(QStringLiteral("value")).toString();
            else tailnetReason = reason;
            continue;
        }
        if (kind == QLatin1String("hosted")) {
            if (!available) hostedReason = reason;
            continue;
        }
        if (kind == kCloudflare || !available) continue;
        const QString what = entry.value(QStringLiteral("value")).toString();
        if (what.isEmpty()) continue;
        QString label = entry.value(QStringLiteral("label")).toString();
        if (label.isEmpty()) {
            label = QStringLiteral("%1 — reachable from %2")
                        .arg(what, entry.value(QStringLiteral("where")).toString());
        }
        locals.append({what, label, entry.value(QStringLiteral("where")).toString()});
    }

    QList<Choice> out;
    out.append({kHosted,
                hostedReason.isEmpty()
                    ? QStringLiteral("relay-terminal.ai — works from anywhere, no certificate warning")
                    : QStringLiteral("relay-terminal.ai — not answering right now"),
                hostedReason.isEmpty()
                    ? QStringLiteral("Relay's hosted rendezvous carries ciphertext it cannot read. "
                                     "A phone on any network reaches this desktop by name.")
                    : hostedReason});
    out.append({kTailnet,
                tailnetName.isEmpty()
                    ? QStringLiteral("Your tailnet — your own devices only")
                    : QStringLiteral("%1 — your tailnet, no certificate warning").arg(tailnetName),
                tailnetReason.isEmpty()
                    ? QStringLiteral("Only devices on your tailnet reach this desktop, and nothing "
                                     "of it is published anywhere else.")
                    : tailnetReason});
    out.append(locals);

    if (!remembered.isEmpty()) {
        bool known = false;
        for (const Choice &choice : out) known = known || choice.value == remembered;
        if (!known) {
            out.append({remembered, remembered,
                        QStringLiteral("Remembered from an earlier pick. This machine is not "
                                       "offering that address right now.")});
        }
    }
    return out;
}

QJsonObject startMessage(const QString &name, bool always, const QString &addressValue, bool tls)
{
    QJsonObject start{{QStringLiteral("t"), QStringLiteral("start")},
                      {QStringLiteral("tls"), tls},
                      {QStringLiteral("name"), name},
                      {QStringLiteral("always"), always}};
    // The address rides on every start, not only the always-on one: a desktop that remembered an
    // address and then shared one pane by hand meant that address for the pane too.
    if (!addressValue.isEmpty()) start.insert(QStringLiteral("address"), addressValue);
    return start;
}

State parseState(const QJsonObject &message)
{
    State state;
    state.on = message.value(QStringLiteral("on")).toBool();
    state.address = message.value(QStringLiteral("address")).toString();
    state.base = message.value(QStringLiteral("base")).toString();
    state.online = message.value(QStringLiteral("online")).toBool();
    state.devices = message.value(QStringLiteral("devices")).toInt();
    state.reason = message.value(QStringLiteral("reason")).toString();
    return state;
}

QString addressName(const QString &value)
{
    if (value.isEmpty()) return QString();
    if (value == kTailnet) return QStringLiteral("your tailnet");
    if (value == kCloudflare) return QStringLiteral("a public link");
    return value;
}

QString statusLine(const State &state)
{
    if (!state.on) return QStringLiteral("Remote control off");
    QString line = QStringLiteral("Remote control on");
    const QString where = addressName(state.address.isEmpty() ? address() : state.address);
    if (!where.isEmpty()) line += QStringLiteral(" · ") + where;
    if (!state.online) {
        line += state.reason.isEmpty() ? QStringLiteral(" · offline")
                                       : QStringLiteral(" · offline: %1").arg(state.reason);
        return line;
    }
    if (state.devices <= 0) return line + QStringLiteral(" · no phone connected");
    return line + (state.devices == 1 ? QStringLiteral(" · 1 device")
                                      : QStringLiteral(" · %1 devices").arg(state.devices));
}

SettingsSection section(const SectionHooks &hooks)
{
    SettingsSection remote;
    remote.id = QStringLiteral("remote");
    remote.title = QStringLiteral("Remote");
    remote.blurb = QStringLiteral(
        "Your phone, all day. With remote control on, this desktop publishes itself when Relay "
        "starts and stays reachable through drops and sleep — you never press a share button "
        "again. Nothing leaves this machine in the clear: the rendezvous carries ciphertext only, "
        "and the pane content is sealed to the devices you paired.");

    {
        SettingRow row;
        row.kind = SettingRow::Toggle;
        row.id = alwaysOnRowId();
        row.label = QStringLiteral("Remote control");
        row.detail = QStringLiteral(
            "Every pane on this desktop is reachable by the phones you have paired, through the "
            "address below; guests still need an invite.");
        row.aliases = QStringLiteral("phone iphone always on remote control sidecar publish panes");
        row.checked = alwaysOn();
        row.changed = row.checked;     // off is what Relay ships with
        const auto write = hooks.setAlwaysOn;
        row.onToggle = [write](bool on) {
            if (write) write(on);
            else setAlwaysOn(on);
        };
        row.reset = [write] {
            if (write) write(false);
            else setAlwaysOn(false);
        };
        remote.rows << row;
    }

    {
        const QString current = address();
        const QList<Choice> choices = addressChoices(hooks.addresses, current);
        SettingRow row;
        row.kind = SettingRow::Choice;
        row.id = addressRowId();
        row.label = QStringLiteral("Address");
        row.aliases = QStringLiteral("address rendezvous tailnet tailscale hosted relay-terminal.ai lan");
        QStringList detail{QStringLiteral(
            "Where your phones reach this desktop. Changing it drops the devices connected "
            "through the old one; they come back on the new address.")};
        for (const Choice &choice : choices) {
            row.options << choice.value;
            row.optionLabels << choice.label;
            if (!choice.detail.isEmpty())
                detail << QStringLiteral("%1: %2").arg(choice.label, choice.detail);
        }
        row.detail = detail.join(QLatin1Char('\n'));
        row.current = current;
        const auto write = hooks.setAddress;
        row.onChoose = [write](const QString &value) {
            if (write) write(value);
            else setAddress(value);
        };
        row.reset = [write] {
            if (write) write(hostedValue());
            else setAddress(QString());
        };
        row.changed = current != hostedValue();
        remote.rows << row;
    }

    {
        SettingRow row;
        row.kind = SettingRow::Info;
        row.id = QStringLiteral("info:remote.state");
        row.label = statusLine(hooks.state);
        if (hooks.state.on && hooks.state.online && !hooks.state.base.isEmpty())
            row.label += QStringLiteral("\n%1").arg(hooks.state.base);
        remote.rows << row;
    }

    if (hooks.pairPhone) {
        SettingRow row;
        row.kind = SettingRow::Button;
        row.id = QStringLiteral("remote.pair");
        row.label = QStringLiteral("Pair a phone");
        row.detail = QStringLiteral(
            "Opens the Sharing pane's Devices page: the QR code, the code to type on the phone and "
            "the five digits to compare. A paired phone sees every pane while remote control is on.");
        row.buttonText = QStringLiteral("Pair…");
        row.run = hooks.pairPhone;
        row.agentSafeButtons = {};      // pairing admits a device; never an agent's to press
        remote.rows << row;
    }
    return remote;
}

// ----- one entry point: the plug menu (#FR1C) ---------------------------------------------------

QList<PlugItem> plugMenu(const State &state)
{
    QList<PlugItem> items;
    // The line the plug has carried since #PH0N: where this desktop is published and how many of
    // the owner's phones are on it. Not clickable — it is the answer to "is it on?", and the two
    // rows under it are what to do about it.
    items.append({PlugItem::Status, QStringLiteral("remote.status"), statusLine(state), false});
    items.append({PlugItem::Separator, QString(), QString(), false});
    // First, because it is the thing a person with a phone in their hand came here to do, and
    // because it turns remote control on by itself when it is off.
    items.append({PlugItem::Action, QStringLiteral("remote.pair"),
                  QStringLiteral("Pair a phone…"), false});
    // The switch itself, one click from the window rather than three from Options. Turning it off
    // here is what "Disconnect all" used to be: the phones go and the service goes with them.
    items.append({PlugItem::Toggle, QStringLiteral("remote.control"),
                  state.on ? QStringLiteral("Remote control: on")
                           : QStringLiteral("Remote control: off"),
                  state.on});
    items.append({PlugItem::Separator, QString(), QString(), false});
    // Somebody else's session, and your own other desktop's panes: the two the plug started with.
    items.append({PlugItem::Action, QStringLiteral("remote.join"),
                  QStringLiteral("Join with a code…"), false});
    items.append({PlugItem::Action, QStringLiteral("remote.openShared"),
                  QStringLiteral("Open a pane your other desktop shares…"), false});
    return items;
}

QString turnOnForPairing()
{
    // An address the owner picked earlier is kept: "pair a phone" is not the place to move a
    // desktop off the tailnet it was deliberately put on.
    const QString remembered = relay::settings::stringValue(addressKey(), QString());
    if (remembered.isEmpty()) setAddress(hostedValue());
    setAlwaysOn(true);
    return address();
}

// ----- the pairing code (#FR1C) -----------------------------------------------------------------

QJsonObject pairCodeRequest() { return {{QStringLiteral("t"), QStringLiteral("pair_code")}}; }

QJsonObject pairCodeRevoke(const QString &code)
{
    return {{QStringLiteral("t"), QStringLiteral("pair_code_revoke")},
            {QStringLiteral("code"), code}};
}

bool parsePairCode(const QJsonObject &message, PairCode *out)
{
    const QString code = message.value(QStringLiteral("code")).toString();
    if (code.isEmpty()) return false;
    if (out) {
        out->code = code;
        out->pin = message.value(QStringLiteral("pin")).toString();
        // A sidecar that leaves the field out means the ten minutes #97EG's codes always last.
        const int expires = message.value(QStringLiteral("expires")).toInt(600);
        out->expires = expires > 0 ? expires : 600;
    }
    return true;
}

bool parsePairCodeState(const QJsonObject &message, PairCodeState *out)
{
    const QString code = message.value(QStringLiteral("code")).toString();
    const QString state = message.value(QStringLiteral("state")).toString();
    if (code.isEmpty() || state.isEmpty()) return false;
    if (out) {
        out->code = code;
        out->state = state;
        out->failures = message.value(QStringLiteral("failures")).toInt();
    }
    return true;
}

int pairCodeWaitMs() { return 3000; }

QString pairCodeUnavailable()
{
    return QStringLiteral("This desktop cannot make a code; scan the QR instead.");
}

QString pairCodeHeading()
{
    return QStringLiteral("On your phone, open Relay and enter");
}

QString pairAlwaysOnLine()
{
    return QStringLiteral("Remote control is on: your paired phones see every pane. Turn it off in "
                          "the plug menu or Options › Remote.");
}

PairCodeRow pairCodeRow(const PairCode &code, const QString &state, int failures, int secondsLeft)
{
    PairCodeRow row;
    // One value, spaced: it is read off the screen and typed on a phone in one go, and two
    // captioned cells would be two things to find. The PIN is still the secret half.
    row.value = (code.code + QLatin1Char(' ') + code.pin).trimmed();
    if (state.isEmpty()) {
        const int left = secondsLeft > 0 ? secondsLeft : 0;
        row.clock = QStringLiteral("Expires in %1:%2")
                        .arg(left / 60)
                        .arg(left % 60, 2, 10, QLatin1Char('0'));
        row.note = QStringLiteral(
            "The code pairs one phone and lasts 10 minutes. The phone then shows five digits; "
            "allow it here when they match what it shows.");
        return row;
    }
    // Struck through rather than cleared, as a meeting code is: the answer to "which code?" is
    // this one, and it is no longer any good.
    row.dead = true;
    row.again = true;
    if (state == QLatin1String("used")) {
        row.clock = QStringLiteral("Used");
        row.note = QStringLiteral(
            "A phone used this code. Compare the five digits it is showing with the ones here and "
            "allow it. Make a new code to pair another phone.");
    } else if (state == QLatin1String("burned")) {
        row.clock = QStringLiteral("Closed");
        row.note = failures > 0
                       ? QStringLiteral("%1 wrong PINs were tried, so the code was closed. Nothing "
                                        "was paired; make a new one.")
                             .arg(failures == 3 ? QStringLiteral("Three") : QString::number(failures))
                       : QStringLiteral("This code was closed before any phone used it. Make a new "
                                        "one if you still want to pair.");
    } else {
        row.clock = QStringLiteral("Expired");
        row.note = QStringLiteral("This code expired before any phone used it. Make a new one when "
                                  "the phone is in your hand.");
    }
    return row;
}

}  // namespace relay::remotesettings
