// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The app's own watch on agent scratch (card #SZHQ): the trees agents leave under
// $TMPDIR/claude-<uid> and loose relay-*/tmp* folders grew to 380 GB on the owner's machine, and
// nothing said so. This runs `scripts/relay-scratch check --json` (backend/relay_core/scratch.py)
// now and then, off the GUI thread — a full check walks the tree and can take ten seconds — and
// when the verdict is not ok it posts one entry to the bell with the numbers and, when something is
// idle enough to go, a "Clean up" button that runs `relay-scratch gc --apply` and says what it freed.
// Beside the check runs `relay-scratch ledger --json` (#DVV2): the append-only ledger's totals by
// class and by session, its keep rows that were never promoted, and its orphans — adopted from
// before the ledger existed and never removed automatically — join the same report. A ledger that
// is empty, missing or unreadable changes nothing: the check's verdict stands alone.
//
// Cadence: the first tick comes a few minutes after launch, then every half hour; a tick runs the
// check only when the last one, by any Relay process, is at least kCheckIntervalSecs old. The last
// check and the last notice are in QSettings, so several windows (one process) and several Relay
// processes share one schedule and one notice per day. A notice is repeated no sooner than
// kRenotifySecs, unless the condition gets worse: low free space where before there was only an
// over-budget scratch tree.
//
// Off with RELAY_SCRATCH_MONITOR=0 in the environment, or `scratch/monitor` false in the settings.
//
// The rules are free functions over plain values so tests/scratchmonitor_test.cpp can run them
// without a disk walk, a process or a clock.
#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class QProcess;
class QTimer;

namespace relay::scratch {

// What `relay-scratch check --json` prints (scratch.Verdict).
struct Verdict {
    bool valid = false;   // the JSON parsed and had "ok" and "line"
    bool ok = true;
    QString line;
    qint64 scratchBytes = 0, reclaimableBytes = 0, budgetBytes = 0;
    qint64 freeBytes = 0, minFreeBytes = 0, diskBytes = 0;
};

// The last JSON object line of the output; anything before it (warnings) is ignored.
Verdict parseVerdict(const QByteArray &output);

// Which problems the verdict has: "budget", "free", both joined by "+", or empty when ok.
QString conditionOf(const Verdict &verdict);

// What the schedule remembers between ticks and between processes.
struct State {
    QDateTime lastCheck;         // when a check was last started, by any Relay
    QDateTime lastNotified;      // when a notice was last posted
    QString notifiedCondition;   // conditionOf() of that notice
};

constexpr qint64 kCheckIntervalSecs = 6 * 3600;
constexpr qint64 kRenotifySecs = 24 * 3600;
constexpr int kFirstTickMs = 3 * 60 * 1000;
constexpr int kTickMs = 30 * 60 * 1000;

// A check is due when none has started in the last interval (or the clock went backwards).
bool checkDue(const State &state, const QDateTime &now, qint64 intervalSecs = kCheckIntervalSecs);

// Tell the user? Only for a bad verdict; once per kRenotifySecs, sooner only when free space has
// become low since the last notice.
bool shouldNotify(const Verdict &verdict, const State &state, const QDateTime &now,
                  qint64 renotifySecs = kRenotifySecs);

// The notice itself.
QString noticeTitle(const Verdict &verdict);
// The button: "Clean up" when something can be reclaimed, otherwise no button.
bool offersCleanup(const Verdict &verdict);

// `relay-scratch gc --apply` ends with "freed 1.0 GB in 3 entries"; that last non-empty line.
QString gcSummary(const QByteArray &output);

// What `relay-scratch ledger --json` prints (scratch.ledger_summary, #DVV2): the append-only
// ledger behind the check — the live rows by class and by session, the keep rows that were never
// promoted into their project, and the orphans adopted from before the ledger existed.
struct LedgerTotal {
    int rows = 0;
    qint64 bytes = 0;
};
struct LedgerBucket {
    QString name;          // a class ("scratch", "keep", "tools") or a session id
    LedgerTotal total;
};
struct LedgerRow {
    QString id, purpose, path;
    qint64 bytes = 0;
};
struct Ledger {
    bool valid = false;    // the JSON parsed and had "live" and "rows"
    int live = 0;
    QList<LedgerBucket> byClass, bySession;
    QList<LedgerRow> unpromotedKeep, orphans;
};

// The last JSON object line of the output, like parseVerdict; invalid when the CLI is missing,
// too old to know the verb, or prints garbage — the check's report then stands alone.
Ledger parseLedger(const QByteArray &output);

// The ledger part of the report: totals by class and by session, and the two lists that wait on
// the user — unpromoted keep rows (promote them into their project or drop them) and orphans
// (pre-ledger leftovers, never removed automatically). Empty when there is nothing to say, so a
// clean ledger leaves a good verdict's notice exactly as short as it was.
QString ledgerSummary(const Ledger &ledger);

// RELAY_SCRATCH_MONITOR=0/false/off/no turns it off; otherwise the setting decides.
bool enabled(const QString &envValue, bool settingOn);

// The notification action id the "Clean up" button hands back.
inline const QString kCleanupAction = QStringLiteral("scratch.gc");

class Monitor : public QObject {
    Q_OBJECT
public:
    static Monitor &instance();

    // `program` and `args` run relay-scratch: e.g. python3, {<data>/scripts/relay-scratch}. The
    // verbs are appended. Does nothing when disabled.
    void start(const QString &program, const QStringList &args);

    // The bell's button. True when the action id was this monitor's (taken or already running).
    bool handleAction(const QString &actionId);

    // A tick: run the check if it is due. `force` runs it regardless (after a clean-up).
    void tick(bool force = false);

private:
    enum class After { Notify, ReportOnCleanup };
    // The check and the ledger run side by side (`runVerb` starts either); `collect` takes each
    // one's output, and the report is composed when the second of the two is in — a slow ledger
    // must not cost the notice its freshness, and a missing one must not cost it the check.
    void runCheck(After after);
    void runVerb(QPointer<QProcess> &slot, const QStringList &verbs);
    void collect(QProcess *process, const QByteArray &output);
    void finishCheck(const QByteArray &checkOutput, const QByteArray &ledgerOutput, After after);
    void runCleanup();
    static State loadState();
    static void saveState(const State &state);

    QString m_program;
    QStringList m_args;
    QTimer *m_timer = nullptr;
    QPointer<QProcess> m_check, m_ledger, m_cleanup;
    QByteArray m_checkOutput, m_ledgerOutput;  // whichever of the pair has finished
    After m_after = After::Notify;             // what the check in flight is for
    QString m_noteId;          // the entry this process posted, amended by the clean-up
    QString m_cleanupSummary;  // "freed …", shown with the check that follows it
};

}  // namespace relay::scratch
