// SPDX-License-Identifier: AGPL-3.0-or-later
// The panes of this Relay, by address, and the delivery of one pane's message to another
// (card #R5TC, protocol section 37).
//
// Every local pane that can run a Relay agent registers here with a PaneHooks of std::function
// only — the RemoteShare::PaneHooks shape — so this library knows nothing about Pane and Pane
// includes nothing circular. The directory mints the pane's `p<n>` (src/PaneAddress.h), answers
// `pane_list`, and routes `pane_send`: it resolves the address, refuses what it must with a named
// code, and hands the note to the recipient's `deliver` hook, which decides between reading it at
// the next step boundary (busy), starting a turn (idle, a wake) and holding it as a note (the wake
// was withheld). The sender never waits for anything but that decision: a send means delivered,
// never read.
//
// One process-wide instance, because a message may cross windows. It is also the one sweep the
// kill switch needs.
#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <functional>

#include "PaneAddress.h"

namespace relay::panedir {

// What reaches the recipient. The recipient's worker builds the frame the model reads from these
// fields; nothing in `text` is ever treated as the frame.
struct Note {
    int from = 0;              // the sender's handle
    QString fromToken;         // the sender's pane token, for a link that says when it is gone
    QString fromTitle, fromWorkspace;
    QString text;              // already printable-filtered
    bool idle = false;         // a notify_when_idle notice ("p2 is idle now"), not a message
    bool mayWake = true;       // false: the sender's turn was itself started by a wake (depth one)
};

// Outcomes a `deliver` hook returns.
inline const QString kWoke = QStringLiteral("woke");
inline const QString kDelivered = QStringLiteral("delivered");   // it is busy: read at its next step
inline const QString kNoWake = QStringLiteral("no_wake");        // idle, but the wake was withheld

struct PaneHooks {
    std::function<QString()> title;
    std::function<QString()> workspace;
    std::function<bool()> busy;          // an agent turn is running, starting or queued
    std::function<bool()> configured;    // a Relay agent is set up and can take a turn
    std::function<bool()> guestInFront;  // a Claude Code / Codex guest is this pane's agent (#GT7X)
    // Take the note. Returns kWoke, kDelivered or kNoWake.
    std::function<QString(const Note &)> deliver;
};

struct Result {
    bool ok = false;
    QString code;          // empty when ok; otherwise one of the refusal codes below
    QString outcome;       // kWoke, kDelivered or kNoWake when ok
    int to = 0;
    QString toTitle;
    QString message;       // one sentence for the model
    QJsonArray panes;      // the directory as the sender sees it, so a refusal can be corrected
    QJsonObject toJson() const;
};

// Refusal codes (protocol 37.4).
inline const QString kUnknownPane = QStringLiteral("unknown_pane");
inline const QString kSelf = QStringLiteral("self");
inline const QString kClosed = QStringLiteral("closed");
inline const QString kNotConfigured = QStringLiteral("not_configured");
inline const QString kNotSupported = QStringLiteral("not_supported");
inline const QString kDisabled = QStringLiteral("disabled");
inline const QString kEmpty = QStringLiteral("empty");

class Directory {
public:
    static Directory &instance();

    // Register a pane; returns its newly minted handle. Registering a token twice keeps its handle.
    int add(const QString &token, PaneHooks hooks);
    // The pane closed: its handle is retired (a later send to it is `closed`, never another pane)
    // and every idle subscription it held or was the subject of is dropped.
    void remove(const QString &token);

    int handleOf(const QString &token) const;
    QString tokenOf(int handle) const;
    int size() const { return int(m_panes.size()); }
    QStringList tokens() const;

    // Every registered pane but `exceptToken`, in handle order:
    // {pane: "p2", title, workspace, state: "idle" | "busy" | "no agent" | "guest"}.
    QJsonArray roster(const QString &exceptToken) const;

    // Route one message. `to` is anything paneaddress::parse() accepts. `mayWake` is false when the
    // sender's own turn was started by a wake. `notifyWhenIdle` subscribes the sender to one notice
    // when the recipient next goes idle. Writes one `crosspane_send` log line with identifiers only.
    Result send(const QString &fromToken, const QString &to, const QString &text, bool notifyWhenIdle,
                bool mayWake);

    // The pane's agent went idle: fire (and drop) every notify_when_idle subscription on it.
    void wentIdle(const QString &token);

    // The kill switch (Options `agent/cross_pane`, the palette's Stop cross-pane messaging). The
    // window sets it from the setting at startup and whenever it changes.
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);

    // The roster changed: a pane joined or closed, a busy flag or a title flipped, the switch
    // moved. The sink is called once, 250 ms later at the earliest, because every pane pushes the
    // whole roster to its worker and a split that renames nothing must not wake five sockets.
    void rosterChanged();
    void setRosterSink(std::function<void()> sink) { m_rosterSink = std::move(sink); }


    // Peer text may not forge Relay's own lines: control characters other than newline and tab
    // are dropped, and so are bidi overrides. At most 16 KB is kept.
    static QString printable(const QString &text);

    // Tests only: forget everything (handles keep counting up; they are never reused).
    void resetForTest();

private:
    struct Entry { int handle = 0; PaneHooks hooks; };
    struct Subscription { QString watcher; bool mayWake = true; };
    QHash<QString, Entry> m_panes;              // token -> entry
    QHash<int, QString> m_byHandle;             // live handle -> token
    QSet<int> m_retired;                        // handles of closed panes
    QHash<QString, QList<Subscription>> m_idleWatchers;   // subject token -> watchers
    std::function<void()> m_rosterSink;        // the window's, called coalesced
    QTimer *m_rosterTimer = nullptr;           // 250 ms of quiet, then one push
    bool m_enabled = true;

    QJsonObject row(const Entry &entry) const;
    static void logSend(const QString &from, const QString &fromWs, const QString &to, const QString &toWs,
                        int bytes, const QString &outcome);
};

} // namespace relay::panedir
