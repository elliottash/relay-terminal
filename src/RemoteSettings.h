// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Remote control as a service the owner switches on once (card #PH0N, phase 1).
//
// Until now a pane reached a phone only while its share button was held down in this Relay
// session: the sidecar started on the first share, the address was chosen inside the share dialog
// and forgotten at the next start, so a phone paired yesterday saw nothing after a desktop
// restart. The switch is `remote/alwaysOn` and the address it uses is `remote/address`; both live
// in QSettings, so they survive the restart that used to end the service.
//
// This file holds the parts that are only settings and words — which keys, what the picker
// offers, the `start` line the sidecar is sent and the `remote_state` line it sends back, and the
// Options › Remote section built from them. It links no widgets of its own and knows nothing
// about panes, so tests/remotesettings_test.cpp can run the whole contract without a sidecar,
// without a window and without the app. RemoteShare (src/RemoteShare.cpp) is the caller: it owns
// the process, the panes and the signals.
#include "SettingsPane.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <functional>

namespace relay::remotesettings {

// The two QSettings keys. They are named here rather than spelled in each caller, because a
// remembered switch that half the code reads under another name is a service that is on for
// nobody.
QString alwaysOnKey();
QString addressKey();

// What the picker sends back for the two addresses that are not a number on this machine. The
// tailnet entry is the *word*, not the tailnet name: the name is this machine's and changes with
// it, and "use the tailnet, whatever it is called today" is what the owner meant when picking it.
// The sidecar resolves it at `start` (remote/gui_host.py), the same way the share dialog's
// `address` line has always resolved the hosted one.
QString hostedValue();      // "relay-terminal.ai"
QString tailnetValue();     // "tailscale"

bool alwaysOn();
void setAlwaysOn(bool on);
// The remembered address. Defaults to the hosted rendezvous: it is the only one of the three that
// answers from a bus, which is what the switch is for.
QString address();
void setAddress(const QString &value);

// One entry of the Options picker. `value` is what goes into `remote/address` and out on the wire.
struct Choice {
    QString value, label, detail;
};

// What Options offers, given the sidecar's own `addresses` array (empty while it is not running).
// The hosted entry and the tailnet entry are always there — they are the two the owner can pick
// before anything has started — and this machine's own addresses join them from the live list,
// which is the same list RemoteShareDialog::showAddresses draws. An unavailable live entry carries
// its one-sentence reason as the row's detail rather than being dropped, so "there is no such
// option" and "you have not run one command yet" do not look identical. A remembered value that is
// in neither list is kept as its own entry: a picker that silently moves the address the service
// is running on is worse than one showing a name it cannot explain.
QList<Choice> addressChoices(const QJsonArray &live, const QString &remembered);

// The `start` line. `always` and `address` are the two fields phase 1 adds: with `always` true the
// sidecar registers at the chosen address and keeps itself registered across drops, rather than
// waiting for the first pane. Sent again, verbatim, when the switch is turned on while the sidecar
// is already running for an ordinary share.
QJsonObject startMessage(const QString &name, bool always, const QString &address, bool tls = true);

// `remote_state`, the line the sidecar sends whenever any of this changes.
struct State {
    bool on = false;
    QString address;
    QString base;
    bool online = false;
    int devices = 0;
    QString reason;     // why it is not online, in one sentence
};
State parseState(const QJsonObject &message);

// The address as a person says it: the hosted host name, "your tailnet", "a public link", or the
// address itself.
QString addressName(const QString &value);

// The one line the window chrome shows: "Remote control on · relay-terminal.ai · 2 devices", or
// "… · offline: <reason>" when it is on and not registered, or "Remote control off".
QString statusLine(const State &state);

// What Options › Remote needs from its caller. The section reads the live values it is given and
// writes through these, so the page has no idea a sidecar exists.
struct SectionHooks {
    QJsonArray addresses;                           // the sidecar's list, empty when it is down
    State state;
    std::function<void(bool)> setAlwaysOn;
    std::function<void(const QString &)> setAddress;
    std::function<void()> pairPhone;                // the share dialog, where pairing lives
};
SettingsSection section(const SectionHooks &hooks);

// ----- one entry point: the plug menu (#FR1C) --------------------------------------------------
// "even step 1 of enabling remote, that was not obvious to me" (owner, 2026-09-20). The plug at
// the top right of the window said "Join a shared session" and nothing about this desktop's own
// phones, so turning remote control on was Options › Remote and nowhere else. The menu is a list
// of rows here rather than QMenu calls inside the window, so a test can read the rows and their
// order without building a window.
struct PlugItem {
    enum Kind { Status, Action, Toggle, Separator };
    Kind kind = Action;
    QString id;         // "remote.status", "remote.pair", "remote.control", "remote.join", …
    QString label;
    bool checked = false;   // Toggle rows only
};
QList<PlugItem> plugMenu(const State &state);

// "Pair a phone" on a desktop whose switch is off. A phone paired against a desktop that
// publishes nothing shows an empty list and no notification, so the switch goes on first, every
// time — writing exactly what the Options switch writes: the hosted address unless another one is
// already remembered, and `remote/alwaysOn`. Returns the address, which is what the `start` line
// that follows carries (startMessage(name, true, address()), with `always` true).
QString turnOnForPairing();

// ----- the pairing code (#FR1C) ----------------------------------------------------------------
// Four letters and four digits the owner types on the phone. Scanning the pairing QR on an iPhone
// opens Safari, which pairs a browser tab that gets no push and is not the installed app; a typed
// code reaches whichever Relay is in front of the person. It is #97EG's meeting code with a
// pairing fragment behind it, and these are the GUI's half of that wire contract — the sidecar's
// half is task 2 of the same card.
QJsonObject pairCodeRequest();                       // {"t":"pair_code"}
QJsonObject pairCodeRevoke(const QString &code);     // {"t":"pair_code_revoke","code":"ABCD"}

// `{"t":"pair_code","code":"ABCD","pin":"4829","expires":600}`. The PIN is the secret half and
// never leaves the dialog, exactly as an invite code's does.
struct PairCode {
    QString code, pin;
    int expires = 600;
};
bool parsePairCode(const QJsonObject &message, PairCode *out);

// `{"t":"pair_code_state","code":"ABCD","state":"used"|"burned"|"expired","failures":n}`.
struct PairCodeState {
    QString code, state;
    int failures = 0;
};
bool parsePairCodeState(const QJsonObject &message, PairCodeState *out);

// How long the dialog waits for a `pair_code` answer before showing the QR on its own, and the one
// sentence it then says. A sidecar from before this card ignores the line rather than refusing it,
// so the wait is what tells the two apart.
int pairCodeWaitMs();
QString pairCodeUnavailable();
// Above the code, because it is an instruction rather than a caption.
QString pairCodeHeading();
// The line at the top of the pairing dialog: pairing turned remote control on, it stays on, and
// where to turn it off.
QString pairAlwaysOnLine();

// What the code row shows. `state` is empty while the code is live, otherwise the last
// `pair_code_state` word; `secondsLeft` is the countdown the dialog ticks.
struct PairCodeRow {
    QString value;      // "ABCD 4829"
    bool dead = false;  // struck through: it no longer pairs anything
    QString clock;      // "Expires in 9:58" · "Used" · "Closed" · "Expired"
    QString note;       // one sentence under it
    bool again = false; // the "New code" button
};
PairCodeRow pairCodeRow(const PairCode &code, const QString &state, int failures, int secondsLeft);

// Row ids, so a test and a "reveal this option" link name the same string.
QString alwaysOnRowId();
QString addressRowId();

}  // namespace relay::remotesettings
