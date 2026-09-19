// SPDX-License-Identifier: AGPL-3.0-or-later
// The jobs list under the prompt box: commands the agent left running (src/JobsPanel.h).
#include "JobsPanel.h"

#include <QJsonArray>
#include <QKeyEvent>
#include <QSignalSpy>
#include <QTest>

using relay::JobRow;
using relay::JobsModel;
using relay::JobsPanel;

namespace {

QJsonObject job(const QString &id, bool running, int exit = 0, bool stopped = false, qint64 elapsed = 1000) {
    QJsonObject o{{"job_id", id}, {"command", "python3 -m http.server 8765"}, {"running", running},
                  {"stopped", stopped}, {"elapsed_ms", double(elapsed)}};
    o.insert(QStringLiteral("exit_code"), running ? QJsonValue() : QJsonValue(exit));
    return o;
}

QJsonObject jobs(const QJsonArray &list) { return {{"event", "jobs"}, {"jobs", list}}; }

}  // namespace

class JobsTest : public QObject {
    Q_OBJECT
private slots:
    void listsWhatTheWorkerSends() {
        JobsModel model;
        qint64 now = 10000;
        model.clock = [&] { return now; };
        QVERIFY(model.handle(jobs({job("job-2", true, 0, false, 5000), job("job-1", false, 3)})));
        QCOMPARE(model.rows().size(), 2);
        QCOMPARE(model.runningCount(), 1);
        now += 2000;   // a running job's clock keeps going between events
        QCOMPARE(model.elapsedNow(*model.row("job-2")), qint64(7000));
        QCOMPARE(model.elapsedNow(*model.row("job-1")), qint64(1000));
        QCOMPARE(model.row("job-2")->state(7000), QStringLiteral("running · 0:07"));
        QCOMPARE(model.row("job-1")->state(1000), QStringLiteral("exit 3 · 0:01"));
        JobRow stoppedRow;
        stoppedRow.stopped = true;
        QCOMPARE(stoppedRow.state(125000), QStringLiteral("stopped · 2:05"));
    }

    void aJobThatEndsByItselfIsReportedOnce() {
        JobsModel model;
        QStringList finished;
        model.onFinished = [&](const JobRow &row) { finished << row.id; };
        model.handle(jobs({job("job-1", true), job("job-2", true)}));
        model.handle(jobs({job("job-1", false, 0), job("job-2", false, 0, true)}));   // job-2 was stopped
        model.handle(jobs({job("job-1", false, 0), job("job-2", false, 0, true)}));
        QCOMPARE(finished, QStringList{"job-1"});
    }

    void dismissedRowsStayGone() {
        JobsModel model;
        model.handle(jobs({job("job-1", false, 0), job("job-2", true)}));
        model.dismiss("job-2");                 // running: not dismissable
        model.dismiss("job-1");
        QCOMPARE(model.rows().size(), 1);
        model.handle(jobs({job("job-1", false, 0), job("job-2", true)}));
        QCOMPARE(model.rows().size(), 1);       // the worker still lists it; the user dismissed it
        model.handle(jobs({job("job-1", false, 0), job("job-2", false, 1)}));
        model.clearFinished();                  // a new user turn
        QVERIFY(model.isEmpty());
    }

    void resetAndReadyEmptyTheList() {
        JobsModel model;
        model.handle(jobs({job("job-1", true)}));
        QVERIFY(!model.handle(QJsonObject{{"event", "reset"}}));   // observed, passed on
        QVERIFY(model.isEmpty());
        model.handle(jobs({job("job-1", true)}));
        model.handle(QJsonObject{{"event", "ready"}});
        QVERIFY(model.isEmpty());
        QVERIFY(!model.handle(QJsonObject{{"event", "delta"}}));
    }

    void keysOpenStopAndLeave() {
        JobsModel model;
        model.handle(jobs({job("job-1", true), job("job-2", false, 0)}));
        JobsPanel panel(&model);
        panel.resize(500, panel.sizeHint().height());
        panel.refresh();
        QVERIFY(!panel.isHidden());
        QString opened, stopped;
        bool byMouse = true, up = false, esc = false;
        panel.onOpen = [&](const QString &id) { opened = id; };
        panel.onStop = [&](const QString &id, bool mouse) { stopped = id; byMouse = mouse; };
        panel.onExitUp = [&] { up = true; };
        panel.onExit = [&] { esc = true; };
        panel.enter();
        QCOMPARE(panel.selectedId(), QStringLiteral("job-1"));
        QTest::keyClick(&panel, Qt::Key_Return);
        QCOMPARE(opened, QStringLiteral("job-1"));
        QTest::keyClick(&panel, Qt::Key_X);
        QCOMPARE(stopped, QStringLiteral("job-1"));
        QVERIFY(!byMouse);
        QTest::keyClick(&panel, Qt::Key_Down);
        QTest::keyClick(&panel, Qt::Key_X);     // finished: dismissed, not stopped
        QCOMPARE(model.rows().size(), 1);
        QTest::keyClick(&panel, Qt::Key_Up);
        QVERIFY(up);
        QTest::keyClick(&panel, Qt::Key_Escape);
        QVERIFY(esc);
        model.clear();
        panel.refresh();
        QVERIFY(panel.isHidden());
    }

    void theCrossStopsWithTheMouse() {
        JobsModel model;
        model.handle(jobs({job("job-1", true)}));
        JobsPanel panel(&model);
        panel.resize(500, panel.sizeHint().height());
        panel.refresh();
        QString stopped;
        bool byMouse = false;
        panel.onStop = [&](const QString &id, bool mouse) { stopped = id; byMouse = mouse; };
        const int rowY = 2 + (panel.fontMetrics().height() + 6) * 3 / 2;   // middle of the first job row
        QTest::mouseClick(&panel, Qt::LeftButton, {}, QPoint(panel.width() - 12, rowY));
        QCOMPARE(stopped, QStringLiteral("job-1"));
        QVERIFY(byMouse);
    }
};

QTEST_MAIN(JobsTest)
#include "jobs_test.moc"
