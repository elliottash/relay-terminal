// SPDX-License-Identifier: AGPL-3.0-or-later
// The app's scratch monitor (card #SZHQ): reading relay-scratch's verdict, when to check, when to
// tell the user and when not to again — and, with a stand-in for relay-scratch, the whole round:
// a bad verdict posts one bell entry with "Clean up", the button runs gc and the entry says what
// it freed. No disk is walked.
#include "Notifications.h"
#include "ScratchMonitor.h"

#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace relay::scratch;
using relay::NotificationCenter;

namespace {
const QByteArray kOver =
    R"({"ok": false, "line": "agent scratch takes 65.3 GB (budget 20.0 GB). 1010.1 MB can be reclaimed: relay-scratch gc --apply", "scratch_bytes": 70164504576, "reclaimable_bytes": 1059176448, "budget_bytes": 21474836480, "free_bytes": 2969055219712, "min_free_bytes": 201593577676, "disk_bytes": 4031871553536, "roots": ["/tmp/claude-1000"]})";
const QByteArray kOverAndLow =
    R"({"ok": false, "line": "agent scratch takes 65.3 GB (budget 20.0 GB); only 3.0 GB free on the disk holding it. Nothing is idle enough to reclaim yet.", "scratch_bytes": 70164504576, "reclaimable_bytes": 0, "budget_bytes": 21474836480, "free_bytes": 3221225472, "min_free_bytes": 5368709120, "disk_bytes": 107374182400, "roots": ["/tmp/claude-1000"]})";
const QByteArray kFine =
    R"({"ok": true, "line": "agent scratch 1.0 GB of 20.0 GB budget; 2.0 TB free", "scratch_bytes": 1073741824, "reclaimable_bytes": 0, "budget_bytes": 21474836480, "free_bytes": 2199023255552, "min_free_bytes": 5368709120, "disk_bytes": 4031871553536, "roots": ["/tmp/claude-1000"]})";
const QDateTime kNoon(QDate(2026, 9, 24), QTime(12, 0));
}  // namespace

class ScratchMonitorTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    QString m_verdictFile;

    void writeVerdict(const QByteArray &json) {
        QFile file(m_verdictFile);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(json + '\n');
    }
    static const relay::Notification *find(const QList<relay::Notification> &entries, const QString &id) {
        for (const auto &note : entries)
            if (note.id == id) return &note;
        return nullptr;
    }

private Q_SLOTS:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminalTest"));
        QCoreApplication::setApplicationName(QStringLiteral("scratchmonitor-test"));
        qunsetenv("RELAY_SCRATCH_MONITOR");
        QSettings().clear();
        QVERIFY(m_dir.isValid());
        // relay-scratch's stand-in: `check` prints whatever verdict the test wrote (exit 1 like
        // the real one when not ok), `gc --apply` prints its summary and empties the tree.
        m_verdictFile = m_dir.filePath(QStringLiteral("verdict.json"));
        const QString script = m_dir.filePath(QStringLiteral("relay-scratch"));
        QFile file(script);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QStringLiteral("case \"$1\" in\n"
                                  "  check) echo 'a warning line first'; cat '%1'; grep -q '\"ok\": true' '%1' || exit 1;;\n"
                                  "  gc) echo 'removed 1.0 GB  /tmp/claude-1000/x'; echo 'freed 1.0 GB in 3 entries';"
                                  " printf '%s\\n' '%2' > '%1';;\n"
                                  "esac\n").arg(m_verdictFile, QString::fromUtf8(kFine)).toUtf8());
        file.close();
        Monitor::instance().start(QStringLiteral("/bin/sh"), {script});
    }
    void init() { QSettings().clear(); NotificationCenter::instance().clear(); }

    // ----- the rules --------------------------------------------------------------------------

    void parsesTheVerdictAfterNoise() {
        const Verdict v = parseVerdict("warning: something\n" + kOver + "\n\n");
        QVERIFY(v.valid);
        QVERIFY(!v.ok);
        QVERIFY(v.line.startsWith(QStringLiteral("agent scratch takes 65.3 GB")));
        QCOMPARE(v.scratchBytes, qint64(70164504576));
        QCOMPARE(v.reclaimableBytes, qint64(1059176448));
        QCOMPARE(v.budgetBytes, qint64(21474836480));
        QCOMPARE(conditionOf(v), QStringLiteral("budget"));
        QVERIFY(offersCleanup(v));
        QCOMPARE(noticeTitle(v), QStringLiteral("Agent scratch is over its budget"));

        const Verdict low = parseVerdict(kOverAndLow);
        QCOMPARE(conditionOf(low), QStringLiteral("budget+free"));
        QVERIFY(!offersCleanup(low));   // nothing idle enough: no button that would do nothing
        QCOMPARE(noticeTitle(low), QStringLiteral("Disk space is low: agent scratch"));

        QVERIFY(parseVerdict(kFine).ok);
        QCOMPARE(conditionOf(parseVerdict(kFine)), QString());
    }

    void garbageIsNoVerdict() {
        QVERIFY(!parseVerdict("").valid);
        QVERIFY(!parseVerdict("Traceback (most recent call last):\n  boom\n").valid);
        QVERIFY(!parseVerdict(R"({"line": "no ok field"})").valid);
        QVERIFY(!shouldNotify(parseVerdict("{not json"), State{}, kNoon));
    }

    void checksEverySixHours() {
        State s;
        QVERIFY(checkDue(s, kNoon));   // never checked
        s.lastCheck = kNoon.addSecs(-5 * 3600);
        QVERIFY(!checkDue(s, kNoon));
        s.lastCheck = kNoon.addSecs(-6 * 3600);
        QVERIFY(checkDue(s, kNoon));
        s.lastCheck = kNoon.addSecs(3600);   // clock went backwards
        QVERIFY(checkDue(s, kNoon));
    }

    void notifiesOnceADayUnlessItGetsWorse() {
        const Verdict over = parseVerdict(kOver), low = parseVerdict(kOverAndLow), fine = parseVerdict(kFine);
        State s;
        QVERIFY(!shouldNotify(fine, s, kNoon));
        QVERIFY(shouldNotify(over, s, kNoon));   // first time
        s.lastNotified = kNoon.addSecs(-7 * 3600);
        s.notifiedCondition = QStringLiteral("budget");
        QVERIFY(!shouldNotify(over, s, kNoon));   // same condition, same day
        QVERIFY(shouldNotify(low, s, kNoon));      // the disk is now low: say so now
        s.notifiedCondition = QStringLiteral("budget+free");
        QVERIFY(!shouldNotify(low, s, kNoon));
        QVERIFY(!shouldNotify(over, s, kNoon));    // better than last time
        s.lastNotified = kNoon.addSecs(-24 * 3600);
        QVERIFY(shouldNotify(over, s, kNoon));     // a day later, again
        QVERIFY(!shouldNotify(fine, s, kNoon));
    }

    void gcSummaryIsTheLastLine() {
        QCOMPARE(gcSummary("removed 1 GB x\nremoved 2 GB y\nfreed 3.0 GB in 2 entries\n"),
                 QStringLiteral("freed 3.0 GB in 2 entries"));
        QCOMPARE(gcSummary(""), QString());
    }

    void environmentTurnsItOff() {
        QVERIFY(enabled(QString(), true));
        QVERIFY(enabled(QStringLiteral("1"), true));
        QVERIFY(!enabled(QStringLiteral("0"), true));
        QVERIFY(!enabled(QStringLiteral(" Off "), true));
        QVERIFY(!enabled(QStringLiteral("false"), true));
        QVERIFY(!enabled(QString(), false));   // the setting
    }

    // ----- the round, with the stand-in ----------------------------------------------------------

    void badVerdictPostsOneEntryAndCleanUpReportsWhatWasFreed() {
        auto &center = NotificationCenter::instance();
        writeVerdict(kOver);
        Monitor::instance().tick();
        QTRY_COMPARE(center.count(), 1);
        const relay::Notification note = center.entries().first();
        QCOMPARE(note.title, QStringLiteral("Agent scratch is over its budget"));
        QVERIFY(note.body.contains(QStringLiteral("65.3 GB")));
        QCOMPARE(note.kind, NotificationCenter::kindWarning);
        QCOMPARE(note.actionLabel, QStringLiteral("Clean up"));
        QCOMPARE(note.actionId, kCleanupAction);

        // The next tick is not due, and a forced check within the day posts nothing new either.
        Monitor::instance().tick();
        QTest::qWait(300);
        QCOMPARE(center.count(), 1);
        QVERIFY(!QSettings().value(QStringLiteral("scratch/monitor_last_notified")).toDateTime().isNull());

        // Someone else's action id is not ours: the popup goes on to the window's handler.
        QVERIFY(!center.handleAction(note.id, QStringLiteral("appundo:7")));
        // "Clean up", clicked in the popup, which offers it to the centre's handlers first: gc
        // runs, then a fresh check, and the same entry says both.
        QVERIFY(center.handleAction(note.id, note.actionId));
        QTRY_VERIFY(find(center.entries(), note.id)
                    && find(center.entries(), note.id)->title == QStringLiteral("Agent scratch cleaned up"));
        const relay::Notification *after = find(center.entries(), note.id);
        QCOMPARE(center.count(), 1);
        QVERIFY2(after->body.startsWith(QStringLiteral("freed 1.0 GB in 3 entries. Now: agent scratch 1.0 GB")),
                 qPrintable(after->body));
        QCOMPARE(after->kind, NotificationCenter::kindSuccess);
        QVERIFY(after->actionId.isEmpty());   // the offer was taken
    }

    void aSecondRelayWithinTheIntervalDoesNotCheckAgain() {
        // Another Relay process checked an hour ago (the shared settings say so): nothing runs.
        QSettings().setValue(QStringLiteral("scratch/monitor_last_check"), QDateTime::currentDateTime().addSecs(-3600));
        writeVerdict(kOver);
        Monitor::instance().tick();
        QTest::qWait(500);
        QCOMPARE(NotificationCenter::instance().count(), 0);
        // …and one that notified today keeps this one quiet even when its own check is due.
        QSettings().setValue(QStringLiteral("scratch/monitor_last_check"), QDateTime::currentDateTime().addDays(-1));
        QSettings().setValue(QStringLiteral("scratch/monitor_last_notified"), QDateTime::currentDateTime().addSecs(-3600));
        QSettings().setValue(QStringLiteral("scratch/monitor_notified_condition"), QStringLiteral("budget"));
        Monitor::instance().tick();
        QTest::qWait(500);
        QCOMPARE(NotificationCenter::instance().count(), 0);
        QVERIFY(QSettings().value(QStringLiteral("scratch/monitor_last_check")).toDateTime()
                > QDateTime::currentDateTime().addSecs(-60));   // but it did check
    }
};

QTEST_GUILESS_MAIN(ScratchMonitorTests)
#include "scratchmonitor_test.moc"
