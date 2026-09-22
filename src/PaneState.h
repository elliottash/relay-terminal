// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::panestate: one pane, two views (relay-terminal-71, card in
// issues/features/2026-09-18-remote-pane-state.md).
//
// A paired phone, tablet or laptop browser is a thin view of the pane on the desktop. The desktop
// pane publishes a `pane_state` message — what the turn is doing, the reasoning tail, the queue
// with the actions each row allows right now, the model and the models it may switch to, the
// composer, the context left and the session list — and the web view draws it, in the theme this
// side names. The desktop writes every label the pane shows about its own work and the view draws
// it as it arrived, deciding nothing; the words on the view's own controls (its action sheet, its
// send menu, its accessibility labels) are the view's, and are all it writes — owner, 2026-09-19,
// correcting "the web formats nothing". docs/REMOTE-PROTOCOL.md section 16 is the normative shape.
//
// This file is the pure half: plain inputs in, the v1 JSON out, plus a publisher that coalesces the
// pane's many small changes (a reasoning chunk, a clock tick) into at most one message every
// 100 ms and always sends the latest. It is QtCore only and knows nothing of Pane, so it is tested
// headless (tests/panestate_test.cpp). Pane::remoteState() gathers the inputs.
//
// Every id in the message is minted here and resolved on the desktop: a row id is
// Pane::queueRows()'s own ("steer:<request id>", "entry:<queue id>"), and a model choice or a
// session is an opaque per-publisher token ("m3", "s12"). Never a preset id, a path or a session
// file name, so nothing a phone sends back can name something the desktop did not offer it.
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>

namespace relay::panestate {

constexpr int kVersion = 1;
constexpr int kLabelMax = 400;      // any one label, in characters
constexpr int kTailMax = 2000;      // the reasoning tail
constexpr int kRowsMax = 64;        // queue rows
constexpr int kSessionsMax = 50;    // session-manager rows
constexpr int kChoicesMax = 32;     // model choices
constexpr int kEffortsMax = 8;      // reasoning levels a model takes
constexpr int kIntervalMs = 100;    // at most one message per pane this often

// One queue row, as Pane::queueRows() has it plus what the actions depend on.
struct Row {
    QString id;             // "steer:<request id>" or "entry:<queue id>"
    QString kind;           // "steer", "agent" or "command"
    QString text;           // what the strip shows for it (QueueEntry::label(), a steer's text)
    QString state;          // "waiting", "withdrawing", "queued", "editing" or "paused"
    bool written = false;   // Relay wrote it (a fix request, a terminal result): no edit, no steer
};

// A model the pane may switch to. `key` is the desktop's own handle for it ("preset:<id>",
// "role:<role>") and is never published; the message carries a token instead.
struct Choice {
    QString key, label;
    bool current = false;
};

// A row of the session manager. `key` is the session id and is never published either.
struct Session {
    QString key, title, when;
    bool current = false, running = false;
};

struct Inputs {
    QString pane;                    // the pane's session token, the id RRP already uses
    // turn
    bool busy = false;               // an agent turn is running
    bool toolRunning = false;        // ... and it is inside a tool call
    bool waiting = false;            // a program or the agent is waiting on the person
    QString clock;                   // "thinking · 12 s · step 1/256 · Esc stops", as the strip
    // thinking
    bool thinkingVisible = false;
    QString thinkingHeader, thinkingText;   // the whole panel text; build() keeps the tail
    // queue
    bool queuePaused = false;
    QString pauseReason, running, queueHint;
    QList<Row> rows;                 // in delivery order, without the running line
    // model
    QString modelLabel;
    QList<Choice> choices;
    QString effort;                  // this pane's reasoning level ("high"); empty: the model
                                     // takes none, and the phone shows no picker. Always one of
                                     // `efforts` when there are any — the level the desktop's own
                                     // box has selected — so no view can tick nothing (#EFT9)
    QStringList efforts;             // the model's own levels, in the provider's order
    bool effortFixed = false;        // the pane will not change the level whatever a view taps: a
                                     // model with no reasoning knob, or Relay Free, where the
                                     // gateway picks it for the role. The levels are still
                                     // published — Relay Free's two are worth showing — so a view
                                     // draws a chip it cannot pick from, as the desktop's box is
                                     // greyed rather than hidden (#EFT9, owner 2026-09-21)
    QString effortFixedReason;       // the desktop's own sentence for that, its greyed box's
                                     // tooltip ("Relay Free sets the level for you"); empty when
                                     // the level is the pane's to set
    // composer
    QString mode, placeholder;       // mode: "auto", "shell" or "agent"
    // context
    QString contextLabel;            // "96% left"; empty when the pane has no window yet
    int percentLeft = -1;            // -1: unknown, published as null
    // allowance (Relay Free)
    QString allowanceLabel;          // the quota chip's own words; empty: not published at all
    int allowanceLeft = -1;          // percent of today's allowance left; -1: unknown, as null
    QString allowanceDetail;         // the chip's tooltip: "182,400 of 250,000 tokens today · …"
    // appearance
    QString theme;                   // the desktop theme's id ("relay-dark"), so the phone's pane
                                     // is the colour the desktop is; empty: not published
    // sessions
    QList<Session> sessions;
    bool canNew = false;             // a new conversation may be started now
    // A past conversation may be opened into this pane. Both are the owner's level: the hub drops
    // the whole sessions block below `full` (owner's three levels, 2026-09-18).
    bool canOpen = false;
};

// "idle", "thinking", "tool" or "waiting".
QString phase(const Inputs &in);
// Whitespace collapsed (unless `simplify` is false: a row label's two-space gaps are the desktop's
// own spacing) and cut to `max` characters with an ellipsis.
QString clip(const QString &text, int max = kLabelMax, bool simplify = true);
// The row as the strip draws it: "↪ next tool call  ✦ check the readme", "✦ …", "$ …", with
// "  withdrawing…" after a steer that is being withdrawn.
QString rowLabel(const Row &row);
// What may be done to this row right now. `entryIndex`/`entryCount` place a queued row among the
// queued rows (steers are not counted); `busy` is whether an agent turn is running.
QStringList rowActions(const Row &row, bool busy, int entryIndex, int entryCount);
// The same, for the row with this id in `in` (empty when there is none).
QStringList actionsFor(const Inputs &in, const QString &rowId);
// The keys line the strip shows above the rows, by what is selected on the desktop.
QString queueHint(bool steerSelected, bool headSteerable, bool rowSelected);

// Opaque ids for desktop-private keys: "m1", "m2", … Stable per key for the life of the object,
// never reused, so a phone acting on a list it saw a moment ago reaches the same thing or nothing.
class Tokens {
public:
    explicit Tokens(QChar prefix) : m_prefix(prefix) {}
    QString idFor(const QString &key);
    QString keyFor(const QString &id) const { return m_keys.value(id); }
    int size() const { return int(m_keys.size()); }

private:
    QChar m_prefix;
    QHash<QString, QString> m_ids, m_keys;   // key -> id, id -> key
    quint64 m_next = 0;
};

// The v1 message. `choices` and `sessions` mint the ids it carries.
QJsonObject build(qint64 seq, const Inputs &in, Tokens &choices, Tokens &sessions);

// Coalesces changes into messages: changed() is cheap and may be called from anything that moves
// (a reasoning chunk, the clock, the queue); at most one message leaves per interval, built from
// the inputs as they are when it leaves, so intermediate states are dropped and the latest always
// goes. A message identical to the last one sent is not sent again. Nothing is gathered at all
// while `wanted` says nobody is listening.
class Publisher {
public:
    using Gather = std::function<Inputs()>;
    using Sink = std::function<void(const QJsonObject &)>;
    using Wanted = std::function<bool()>;
    Publisher(Gather gather, Sink sink, Wanted wanted, int intervalMs = kIntervalMs);

    void changed();
    // Build and send now, whatever the interval and even if nothing changed: the answer to
    // `pane_state_get`. Returns what was sent (empty when nobody is listening).
    QJsonObject publishNow();
    // Resolve a token from a published message back to the desktop's key; empty when unknown.
    QString choiceKey(const QString &id) const { return m_choices.keyFor(id); }
    QString sessionKey(const QString &id) const { return m_sessions.keyFor(id); }
    qint64 seq() const { return m_seq; }
    int sent() const { return m_sent; }

private:
    void flush(bool force);

    Gather m_gather;
    Sink m_sink;
    Wanted m_wanted;
    int m_interval;
    QTimer m_timer;
    QElapsedTimer m_last;
    bool m_dirty = false;
    qint64 m_seq = 0;
    int m_sent = 0;
    QJsonObject m_lastBody;       // the last message sent, without its seq
    Tokens m_choices{QLatin1Char('m')}, m_sessions{QLatin1Char('s')};
};

}   // namespace relay::panestate
