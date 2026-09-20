// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Signals on the Switchboard (#AQ6X): the machine's own faults — a failing test, a broken build —
// as a keyed record the board draws, never a card. Decisions 1–12 are on the card
// (`issues/features/2026-09-20-signals-a-card-type-for-machine-written-faults-s.md`); the
// reasoning is `docs/SIGNALS-RESEARCH.md` R1–R13, and R12 is what people see: a count, one folded
// row, groups before members, dismissed behind a second toggle. Protocol: §32.
//
// **A signal is not a card.** It has no id in `issues/`, no rank and no status the owner moves; it
// lives in the board's private folder and the worker folds it out of the test history. It gets a
// card only on promotion, and then the card carries a machine-owned `## Signal` section. So
// nothing here derives from `board::Card` and every card action steps over a signal row.
//
// `SignalsState` holds no widgets — it is the `signals_changed` event, ordered, and the rows it
// draws — so the fold, the ordering and every word are tested headless (tests/signals_test.cpp).
// `SignalDetail` is the page the pane shows when a signal row is opened: one signal in, one
// `signals_claim` / `signals_release` / `signals_dismiss` / `signals_promote` message out.
#include "BoardModel.h"

#include <QDateTime>
#include <QJsonObject>
#include <QPair>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

class QComboBox;
class QDateEdit;
class QFrame;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QToolButton;
class QVBoxLayout;

namespace relay {
namespace board {

// One signal exactly as the worker sends it (protocol §32.1, plan step 4). Every field is the
// worker's; nothing here is computed from the others, so a rule changed in
// `backend/relay_core/signals.py` changes the row without a GUI change.
struct Signal {
    QString key;         // `ctest:panelayout`, `build:relay`, `run:ctest` — source plus identity
    QString source;      // ctest | unittest | check | build | crash | ci
    QString kind;        // broken | flaky | group | run | build
    QString state;       // pending | open | resolved | dismissed | removed
    QString firstSeen, lastSeen;   // ISO timestamps
    int count = 0;                 // failing executions
    QString fingerprint;           // the normalised message: groups, never keys
    bool regressed = false;        // it was resolved and came back inside 30 days
    bool stale = false;            // unseen for 7 days; re-run first, never closed by a timer
    QString session;               // the claim (#R9G7): the pane or thread working on it
    QString card;                  // the promoted card's id, empty until promotion
    QString dismissedReason, dismissedUntil;   // `dismissed: {reason, until}`
    QString fixedIn;               // the commit two green runs were seen at
    QString excerpt;               // the failure's own words, for the detail's mono block
    QStringList members;           // `group` and `run` kinds: the keys they stand for

    static Signal fromJson(const QJsonObject &object);
    // A signal that stands for others: its members are drawn indented under it (decision "one
    // cause, one item"). `build` is one too — the tests under a broken build are not evaluated.
    bool isGroup() const;
    bool dismissed() const { return state == QStringLiteral("dismissed"); }
};

// The four dismissal reasons (decision 7). An agent may write only the first two, and for at most
// seven days; `wont-fix` and `expected` are the owner's, which is why they are in this list at all
// — the detail's form is the owner's surface. Every dismissal expires.
QStringList dismissReasons();
// "environmental" -> "Environmental", "flaky-known" -> "Known flaky", "wont-fix" -> "Won't fix",
// "expected" -> "Expected". An unknown reason is shown as the worker spelled it.
QString dismissReasonTitle(const QString &reason);

// The fold row's words: "1 signal", "5 signals" (#93WR's row shape, R12's count).
QString signalsFoldTitle(int count);
// Its tooltip, which is where the row says what a signal *is* — the word alone does not.
QString signalsFoldTip(bool collapsed);
// The second toggle at the end: "1 dismissed", "3 dismissed", and its tooltip.
QString dismissedFoldTitle(int count);
QString dismissedFoldTip(bool collapsed);

// The word at the head of a signal row: "broken", "flaky", "group", "run", "build". The kind is
// the worker's vocabulary and is shown as it is — a glyph alone cannot say "flaky".
QString signalKindWord(const QString &kind);
// The state, for the detail's fields: "pending", "open", "resolved", "dismissed", "removed" ->
// "Pending", "Open", … An unknown state is passed through.
QString signalStateWord(const QString &state);
// "×4"; empty for a count of nothing, so a fresh pending signal does not wear a zero.
QString signalCountWord(int count);
// How long ago the signal was last seen, in the words a thread entry uses (board::entryAge's
// vocabulary): "just now", "12 min ago", "3 h ago", "yesterday", "Sep 16". Empty for a stamp that
// holds no time.
QString signalAge(const QString &stamp, const QDateTime &now);
// The muted words after the age: "regressed", "stale", and the dismissal's expiry. In reading
// order, and empty when the signal has nothing to add.
QStringList signalMarks(const Signal &signal, const QDateTime &now);
// The whole row as one line, for a tooltip and for the tests: "broken · ctest:panelayout · ×4 ·
// 3 h ago · regressed".
QString signalRowLine(const Signal &signal, const QDateTime &now);
// "in 7 days", "tomorrow", "today", "expired" — a dismissal's `until` as the row and the detail
// say it. Empty for a stamp with no date in it.
QString dismissalExpiry(const QString &until, const QDateTime &now);

// The `## Signal` section of a promoted card's body, as the card page's strip shows it (plan step
// 6, the `## Tests` strip's shape): the section's own text without its heading, or empty when the
// card has no such section. The heading is matched case-insensitively and the section ends at the
// next `## `, so the machine's block can grow without this having to know its fields.
QString signalSectionOf(const QString &body);
// The same body with that section taken out, for a card whose page is showing it as a strip: the
// card's own words are what is left, and the machine's block is said once.
QString bodyWithoutSignalSection(const QString &body);

// The whole signal state one board has, as `signals_changed` (and the `signals_list` answer) send
// it. It is replaced whole on every event: the worker folds the record and pushes the result, so
// there is nothing to merge here and no way for the GUI to hold a state the worker does not.
class SignalsState {
public:
    // Take a `signals_changed` / `signals_list` payload. Anything else is ignored and answers
    // false, so the pane can hand it every event.
    bool take(const QString &type, const QJsonObject &event);
    void clear();

    // The signals the fold row stands for: `open` and `pending`, groups before members, a group's
    // members straight after it. Sorted regressed first, then broken before flaky, then by count,
    // then by key — the research's R11 order, minus the "new to my card" term, which needs a card.
    const QList<Signal> &open() const { return m_open; }
    // Behind the second toggle: the dismissed ones, soonest to expire first.
    const QList<Signal> &dismissed() const { return m_dismissed; }
    // By key, open or dismissed; nullptr for a key this state has never heard of.
    const Signal *signalFor(const QString &key) const;
    // What the fold row counts: the open rows, members included, because the row stands for the
    // rows it hides and a count that disagreed with them would be a count of nothing.
    int openCount() const { return int(m_open.size()); }
    int dismissedCount() const { return int(m_dismissed.size()); }
    // The worker's own counts, which include what this state does not draw (`pending` that has
    // not opened, dismissals that have expired out of the list).
    int pendingCount() const { return m_pending; }
    // Every promoted signal whose card is still open, as the event carries them — the whole list
    // on every event, so a row can link to its card.
    const QList<Signal> &promoted() const { return m_promoted; }
    // The ones that were **not** promoted a moment ago: a signal this state had no card for, or
    // had a different card for. This, and not `promoted()`, is what a notice is made of — the
    // list above repeats itself on every change, so a notice drawn from it would announce the
    // same card again every time any signal anywhere moved.
    //
    // Empty on the first event a state ever takes: a pane that has just opened is catching up,
    // not being told that something happened. R12 lets a *promotion* reach a human unasked; the
    // existence of a card promoted last week is not one.
    const QList<Signal> &newlyPromoted() const { return m_newPromoted; }
    bool isEmpty() const { return m_open.isEmpty() && m_dismissed.isEmpty(); }
    // Whether an event has ever arrived. A board whose worker is too old to send signals draws no
    // row at all — which is the same as a board with no signals, and is meant to be.
    bool seen() const { return m_seen; }

    // The rows the pane splices into its list: the fold row, then the open signals when it is
    // open (a group's members indented under it), then the dismissed toggle and its rows. Empty
    // when there is nothing open and nothing dismissed — the row is not drawn to say "none".
    QList<Row> rows(bool open, bool dismissedOpen) const;

private:
    QList<Signal> m_open, m_dismissed, m_promoted, m_newPromoted;
    QMap<QString, Signal> m_byKey;
    int m_pending = 0;
    bool m_seen = false;
};

// ----- signal threads (#AQ6X phase 3, decision 9) ------------------------------------------
//
// A failing check nobody is on gets its own agent thread (`backend/relay_core/signal_threads.py`).
// The GUI never starts one and never stops one: it hears `signal_thread {state, key, thread_id,
// session_id, outcome?, card?}` (protocol §32.4), says so once in the notification centre, amends
// that entry when the thread ends, and answers "is this token live?" for the chip on the signal
// the thread claimed — the claim is written under the **thread's** id, so without this the chip
// would read `closed` for a thread that is working.

// One signal thread, as this GUI has heard of it. `running` is the whole liveness rule: a thread
// is live from the `started` event until the `finished` one, and a `signals_changed` payload's
// `threads` list re-seeds it for a pane that opened after the thread did.
struct SignalThreadRun {
    QString key;         // the signal it is working on
    QString threadId;    // its thread id — and the session token the signal's claim carries
    QString sessionId;   // the owner session the thread is saved beside, for its history
    QString card;        // the card it was promoted to, on a `gave-up` finish
    QString outcome;     // fixed | gave-up | dismissed | stopped, once it has finished
    bool running = true;

    static SignalThreadRun fromJson(const QJsonObject &event);
};

// The notification a pickup posts, and the words it is amended to when the thread ends (decision
// 9: "you get a notification that you can click on to open the agent thread"). Pure functions, so
// every wording is read in tests/signals_test.cpp rather than by starting an agent.
//
// `reason` is the dismissal's own reason when the outcome is `dismissed`; the event does not carry
// one, because the signal does (`Signal::dismissedReason`) and the pane has both.
QString signalThreadTitle(const SignalThreadRun &run, const QString &reason = QString());
QString signalThreadBody(const SignalThreadRun &run);
// kindInfo / kindSuccess / kindWarning, as `relay::NotificationCenter` spells them: the same
// vocabulary the board's other notice uses (a Plan that finished or failed).
QString signalThreadKind(const SignalThreadRun &run);
// "Open thread", the button on the entry; and the `actionId` it hands back, which is a string and
// not a callback so the offer outlives the turn, the worker and the popup that drew it.
QString signalThreadActionLabel();
QString signalThreadActionId(const SignalThreadRun &run);
// The thread id and owner session back out of such an `actionId`, or two empty strings when it is
// not one. The one place the encoding is read, so the window does not parse it by hand.
QPair<QString, QString> signalThreadOfAction(const QString &actionId);

// Every signal thread this pane has heard of. Fed the same events the pane is fed — it takes the
// two it knows and ignores the rest — so a pane can hand it everything, exactly as `SignalsState`
// is handed everything.
class SignalThreadsState {
public:
    // True when the event was one of ours (`signal_thread`, or the `threads` list on a
    // `signals_changed` / `signals_list` payload).
    bool take(const QString &type, const QJsonObject &event);
    void clear();

    // Whether this session token is a signal thread that is still working. This is what the
    // board's `paneExists` callback falls back on: no pane has a thread's token, so without it
    // the chip on a live thread's claim reads `closed`.
    bool isRunning(const QString &token) const;
    // The signal a token is working on, or empty — which is also how the chip's click knows to
    // open a thread's history instead of revealing a pane.
    QString keyOf(const QString &token) const;
    // The thread working on a key right now, or nullptr.
    const SignalThreadRun *forKey(const QString &key) const;
    // The run a token names, running or finished, or nullptr.
    const SignalThreadRun *forToken(const QString &token) const;
    int runningCount() const;
    // The `signal_thread` event this state took last, for the pane to notify about. `running`
    // says which of the two it was. Empty `threadId` before any has arrived.
    const SignalThreadRun &lastEvent() const { return m_last; }

private:
    QList<SignalThreadRun> m_runs;      // newest last; finished ones are kept for the amend
    SignalThreadRun m_last;
};

// The signal's page, in the card detail's place: what the signal is, its excerpt, and the four
// actions. It holds no process — every action is a callback the view turns into one message — and
// no state but the signal it was last shown, so a `signals_changed` while it is open redraws it.
class SignalDetail final : public QWidget {
public:
    explicit SignalDetail(QWidget *parent = nullptr);

    // ---- wiring. Each one is a message in protocol §32.2; the view adds the key and the token.
    std::function<void()> onClaim, onRelease, onPromote, onClose, onEscape;
    // `signals_dismiss {reason, comment, until}`: the form's three fields, already validated
    // (a reason is always one of `dismissReasons()`, `until` an ISO date).
    std::function<void(const QString &reason, const QString &comment, const QString &until)> onDismiss;
    // The promoted card's `#ID`, and the pane the claim names (#R9G7) — the same two links the
    // card page has, for the same reason.
    std::function<void(const QString &cardId)> onOpenCard;
    std::function<void(const QString &token)> onFocusPane;
    std::function<bool(const QString &token)> paneExists;

    // Show this signal (and the page, if it was away). `now` is passed in so the ages are the
    // list's and a test can fix them. Not called `show`: that would hide `QWidget::show()`.
    void showSignal(const Signal &signal, const QDateTime &now = QDateTime::currentDateTimeUtc());
    QString key() const { return m_signal.key; }
    const Signal &signal() const { return m_signal; }

    // The refusal line, in the same place a card's error goes. `clearError` is called by every
    // action, so a refusal never outlives the thing it refused.
    void showError(const QString &text);
    void clearError();
    QString error() const;

    // The actions, as the buttons call them; public so the pane's keys and a test drive the same
    // path the owner's click does.
    void claim();
    void release();
    void promote();
    void openDismiss();          // the inline form, in the page, never a dialog
    void closeDismiss();
    bool dismissOpen() const;
    // What the form holds right now: the reason, the comment and the `until` date.
    QString dismissReason() const;
    QString dismissComment() const;
    QString dismissUntil() const;
    void setDismissReason(const QString &reason);
    void setDismissComment(const QString &comment);
    void setDismissUntil(const QString &isoDate);
    void submitDismiss();        // validates, then calls onDismiss

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void render();

    Signal m_signal;
    QDateTime m_now;
    QLabel *m_key = nullptr, *m_fields = nullptr, *m_marks = nullptr, *m_members = nullptr,
           *m_card = nullptr, *m_error = nullptr, *m_excerptHead = nullptr;
    QPlainTextEdit *m_excerpt = nullptr;
    QToolButton *m_close = nullptr, *m_claim = nullptr, *m_release = nullptr,
                *m_dismiss = nullptr, *m_promote = nullptr;
    QFrame *m_form = nullptr;
    QComboBox *m_reason = nullptr;
    QLineEdit *m_comment = nullptr;
    QDateEdit *m_until = nullptr;
};

}  // namespace board
}  // namespace relay
