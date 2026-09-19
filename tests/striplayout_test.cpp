// SPDX-License-Identifier: AGPL-3.0-or-later
// The strip under the composer (owner, 2026-09-19): "the open task list … underneat the prompt …
// it shows up to (say) 5 tasks. if there are more than 5, it centers on the marginal task … with
// subagents on the left and tasks on the right … a subagent with two tasks gets two rows".
// The window and the row pairing are pure functions, so they are tested without a window.
#include "RequestLedger.h"
#include "SubagentsPanel.h"

#include <QJsonDocument>
#include <QTest>

using relay::RequestLedgerModel;
using relay::StripLayout;
using relay::SubagentModel;

namespace {
// Test JSON uses single quotes to keep the C++ strings readable.
QJsonObject json(const QByteArray &text) { return QJsonDocument::fromJson(QByteArray(text).replace('\'', '"')).object(); }

QStringList statuses(const char *csv) { return QString::fromLatin1(csv).split(QLatin1Char(',')); }

// A subagent model with one listed row per id, all finished unless `live` names them.
void start(SubagentModel *model, const char *id, const char *todoId = "") {
    model->handle(json(QByteArray("{'event':'subagent_started','id':'") + id + "','type':'explore',"
                                  "'description':'work','background':true,'todo_id':'" + todoId + "'}"));
}
void finish(SubagentModel *model, const char *id) {
    model->handle(json(QByteArray("{'event':'subagent_finished','id':'") + id + "','outcome':'done'}"));
}

// One `todos` event: each entry is "id:status[:subagent[:running]]".
void todos(RequestLedgerModel *ledger, const QStringList &items) {
    QByteArray body;
    for (const QString &item : items) {
        const QStringList parts = item.split(QLatin1Char(':'));
        if (!body.isEmpty()) body += ",";
        body += "{'id':'" + parts.value(0).toLatin1() + "','text':'text of " + parts.value(0).toLatin1()
                + "','status':'" + parts.value(1).toLatin1() + "','request_ids':[]";
        if (parts.size() > 2) body += ",'subagent':'" + parts.value(2).toLatin1() + "'";
        if (parts.size() > 3) body += ",'subagent_running':true";
        body += "}";
    }
    ledger->handle(json("{'event':'todos','items':[" + body + "]}"));
}

QStringList ids(const StripLayout &layout, bool tasks) {
    QStringList out;
    for (const auto &row : layout.rows) out << (tasks ? row.todoId : row.subagentId);
    return out;
}
}  // namespace

class StripLayoutTests : public QObject {
    Q_OBJECT
private slots:
    // "if there are more than 5, it centers on the marginal task": the first in_progress, else the
    // first unsettled one, with two rows of context above it.
    void marginalWindowCentresOnTheTaskBeingWorked() {
        QCOMPARE(relay::marginalWindowStart(statuses("pending,pending,pending"), 5), 0);
        QCOMPARE(relay::marginalWindowStart(statuses("completed,completed,completed,completed,in_progress"), 5), 0);
        QCOMPARE(relay::marginalWindowStart(QStringList(), 5), 0);
        // Nine tasks, the sixth (index 5) running: two above, two below.
        QCOMPARE(relay::marginalWindowStart(statuses("completed,completed,completed,completed,completed,in_progress,pending,pending,pending"), 5), 3);
        // Index 6 of nine.
        QCOMPARE(relay::marginalWindowStart(statuses("completed,completed,completed,completed,completed,completed,in_progress,pending,pending"), 5), 4);
        // At or near the top the window cannot slide above the list.
        QCOMPARE(relay::marginalWindowStart(statuses("in_progress,pending,pending,pending,pending,pending"), 5), 0);
        QCOMPARE(relay::marginalWindowStart(statuses("completed,in_progress,pending,pending,pending,pending"), 5), 0);
        QCOMPARE(relay::marginalWindowStart(statuses("completed,completed,in_progress,pending,pending,pending"), 5), 0);
        // The last task running pins the window to the tail (n - maxRows).
        QCOMPARE(relay::marginalWindowStart(statuses("completed,completed,completed,completed,completed,completed,in_progress"), 5), 2);
        // Everything settled: the tail again, so the list ends on what finished last.
        QCOMPARE(relay::marginalWindowStart(statuses("completed,completed,done,cancelled,cancelled_by_user,completed,completed"), 5), 2);
        // An in_progress after an earlier pending wins: that is where the work is.
        QCOMPARE(relay::marginalWindowStart(statuses("completed,pending,completed,completed,completed,in_progress,completed,completed,completed"), 5), 3);
        // Nothing running, an unsettled task decides: blocked/deferred count as unsettled.
        QCOMPARE(relay::marginalWindowStart(statuses("completed,completed,completed,completed,completed,blocked,completed"), 5), 2);
        QCOMPARE(relay::marginalWindowStart(statuses("completed,completed,completed,deferred,completed,completed,completed"), 5), 1);
        // A degenerate window asks for nothing.
        QCOMPARE(relay::marginalWindowStart(statuses("a,b,c"), 0), 0);
        QCOMPARE(relay::marginalWindowStart(statuses("a,b,c"), -3), 0);
    }

    // The strip shows the current task list only: earlier lists live behind the panel's "Earlier"
    // fold and never reach it.
    void currentTaskListIsTheCurrentBatchInOrder() {
        QVERIFY(relay::currentTaskList(nullptr).isEmpty());
        RequestLedgerModel ledger;
        auto requests = [&](const QByteArray &items) {
            ledger.handle(json("{'event':'requests','total':1,'open':0,'counts':{},'items':[" + items + "]}"));
        };
        requests("{'id':'R1','text_preview':'first','status':'done','delivered':true,'turn_id':'q1','todo_ids':['T1']}");
        ledger.handle(json("{'event':'todos','items':[{'id':'T1','text':'one','status':'completed','request_ids':['R1']}]}"));
        QCOMPARE(ledger.currentBatch(), 1);
        requests("{'id':'R1','text_preview':'first','status':'done','delivered':true,'turn_id':'q1','todo_ids':['T1']},"
                 "{'id':'R2','text_preview':'second','status':'in_progress','delivered':true,'turn_id':'q2','todo_ids':['T2']}");
        ledger.handle(json("{'event':'todos','items':[{'id':'T1','text':'one','status':'completed','request_ids':['R1']},"
                           "{'id':'T2','text':'two','status':'in_progress','request_ids':['R2']},"
                           "{'id':'T3','text':'three','status':'pending','request_ids':['R2']}]}"));
        QCOMPARE(ledger.currentBatch(), 2);
        const auto list = relay::currentTaskList(&ledger);
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).id, QStringLiteral("T2"));   // list order, the earlier batch dropped
        QCOMPARE(list.at(1).id, QStringLiteral("T3"));
    }

    // With no tasks the layout is what it has always been: one row per listed subagent.
    void subagentsOnlyIsUnchanged() {
        SubagentModel model;
        start(&model, "a1"); start(&model, "a2"); start(&model, "a3");
        const StripLayout layout = relay::layoutStrip(model, nullptr, 5);
        QCOMPARE(ids(layout, false), QStringList({QStringLiteral("a1"), QStringLiteral("a2"), QStringLiteral("a3")}));
        QCOMPARE(layout.taskRows(), 0);
        QCOMPARE(layout.subagentRows(), 3);
        QCOMPARE(layout.openTasks, 0);
        QCOMPARE(layout.hiddenTasks, 0);
        for (const auto &row : layout.rows) QVERIFY(row.todoId.isEmpty());
        // A finished task list is no reason to show the task half either.
        RequestLedgerModel ledger;
        todos(&ledger, {QStringLiteral("T1:completed"), QStringLiteral("T2:completed")});
        const StripLayout settled = relay::layoutStrip(model, &ledger, 5);
        QCOMPARE(settled.taskRows(), 0);
        QCOMPARE(settled.taskTotal, 2);
        QCOMPARE(settled.openTasks, 0);
        QCOMPARE(ids(settled, false), QStringList({QStringLiteral("a1"), QStringLiteral("a2"), QStringLiteral("a3")}));
    }

    void tasksOnlyFillTheWholeStrip() {
        SubagentModel model;
        RequestLedgerModel ledger;
        todos(&ledger, {QStringLiteral("T1:completed"), QStringLiteral("T2:in_progress"), QStringLiteral("T3:pending")});
        const StripLayout layout = relay::layoutStrip(model, &ledger, 5);
        QCOMPARE(ids(layout, true), QStringList({QStringLiteral("T1"), QStringLiteral("T2"), QStringLiteral("T3")}));
        QCOMPARE(layout.subagentRows(), 0);
        QCOMPARE(layout.openTasks, 2);
        QCOMPARE(layout.taskTotal, 3);
        QCOMPARE(layout.windowStart, 0);
        for (const auto &row : layout.rows) QVERIFY(!row.linked);
    }

    // Both: the pair share a row, and a subagent nothing links to takes a free left cell.
    void linkedPairsShareARow() {
        SubagentModel model;
        start(&model, "a2", "T2");
        start(&model, "a9");
        RequestLedgerModel ledger;
        todos(&ledger, {QStringLiteral("T1:pending"), QStringLiteral("T2:in_progress:a2:running"), QStringLiteral("T3:pending")});
        const StripLayout layout = relay::layoutStrip(model, &ledger, 5);
        QCOMPARE(layout.rows.size(), 3);
        QCOMPARE(ids(layout, true), QStringList({QStringLiteral("T1"), QStringLiteral("T2"), QStringLiteral("T3")}));
        QCOMPARE(layout.rows.at(1).subagentId, QStringLiteral("a2"));
        QVERIFY(layout.rows.at(1).linked);
        // The unpaired agent took the topmost free left cell and is not claimed to work that task.
        QCOMPARE(layout.rows.at(0).subagentId, QStringLiteral("a9"));
        QVERIFY(!layout.rows.at(0).linked);
        QVERIFY(layout.rows.at(2).subagentId.isEmpty());
        QCOMPARE(layout.hiddenSubagents, 0);
        // A task whose subagent was dismissed keeps an empty left cell.
        SubagentModel none;
        const StripLayout gone = relay::layoutStrip(none, &ledger, 5);
        QVERIFY(gone.rows.at(1).subagentId.isEmpty());
        QVERIFY(!gone.rows.at(1).linked);
    }

    // "a subagent with two tasks gets two rows" — the protocol gives one todo per agent today
    // (#QHR1), so this is the pairing proving it is right the day it does not.
    void oneSubagentWithTwoTasksGetsTwoRows() {
        SubagentModel model;
        start(&model, "a1", "T1");
        RequestLedgerModel ledger;
        todos(&ledger, {QStringLiteral("T1:in_progress:a1:running"), QStringLiteral("T2:pending"),
                        QStringLiteral("T3:pending:a1:running")});
        const StripLayout layout = relay::layoutStrip(model, &ledger, 5);
        // Grouping pulls T3 up beside T1: one agent, its tasks adjacent.
        QCOMPARE(ids(layout, true), QStringList({QStringLiteral("T1"), QStringLiteral("T3"), QStringLiteral("T2")}));
        QCOMPARE(ids(layout, false), QStringList({QStringLiteral("a1"), QStringLiteral("a1"), QString()}));
        QVERIFY(layout.rows.at(0).linked);
        QVERIFY(layout.rows.at(1).linked);
        QVERIFY(!layout.rows.at(0).subagentRepeats);
        QVERIFY(layout.rows.at(1).subagentRepeats);   // the second row draws a continuation mark
    }

    // The case that is real today: a task re-run after a failure, so two finished agents point at
    // one todo. Each gets a row; the task cell is drawn on the first of them.
    void twoSubagentsOnOneTaskGetARowEach() {
        SubagentModel model;
        start(&model, "a1", "T1"); finish(&model, "a1");
        start(&model, "a2", "T1");
        RequestLedgerModel ledger;
        todos(&ledger, {QStringLiteral("T1:in_progress:a2:running"), QStringLiteral("T2:pending")});
        const StripLayout layout = relay::layoutStrip(model, &ledger, 5);
        QCOMPARE(layout.rows.size(), 3);
        QCOMPARE(ids(layout, false), QStringList({QStringLiteral("a2"), QStringLiteral("a1"), QString()}));
        QCOMPARE(ids(layout, true), QStringList({QStringLiteral("T1"), QString(), QStringLiteral("T2")}));
        QVERIFY(layout.rows.at(0).linked);
        QVERIFY(!layout.rows.at(1).linked);
    }

    // A subagent whose task scrolled out of the window is never hidden for it.
    void anAgentWhoseTaskIsOutsideTheWindowStillGetsARow() {
        SubagentModel model;
        start(&model, "a1", "T1");
        RequestLedgerModel ledger;
        todos(&ledger, {QStringLiteral("T1:completed:a1"), QStringLiteral("T2:completed"), QStringLiteral("T3:completed"),
                        QStringLiteral("T4:completed"), QStringLiteral("T5:completed"), QStringLiteral("T6:completed"),
                        QStringLiteral("T7:in_progress"), QStringLiteral("T8:pending"), QStringLiteral("T9:pending")});
        const StripLayout layout = relay::layoutStrip(model, &ledger, 5);
        QCOMPARE(layout.windowStart, 4);
        QCOMPARE(ids(layout, true), QStringList({QStringLiteral("T5"), QStringLiteral("T6"), QStringLiteral("T7"),
                                                 QStringLiteral("T8"), QStringLiteral("T9")}));
        QCOMPARE(layout.hiddenTasks, 4);
        QCOMPARE(layout.rows.at(0).subagentId, QStringLiteral("a1"));
        QVERIFY(!layout.rows.at(0).linked);
        QCOMPARE(layout.hiddenSubagents, 0);
    }

    void countsWhatDidNotFitAndNeverExceedsMaxRows() {
        SubagentModel model;
        for (const char *id : {"a1", "a2", "a3", "a4", "a5", "a6", "a7"}) start(&model, id);
        RequestLedgerModel ledger;
        todos(&ledger, {QStringLiteral("T1:in_progress"), QStringLiteral("T2:pending"), QStringLiteral("T3:pending"),
                        QStringLiteral("T4:pending"), QStringLiteral("T5:pending"), QStringLiteral("T6:pending"),
                        QStringLiteral("T7:pending"), QStringLiteral("T8:pending")});
        const StripLayout layout = relay::layoutStrip(model, &ledger, 5);
        QCOMPARE(layout.rows.size(), 5);
        QCOMPARE(layout.subagentRows(), 5);
        QCOMPARE(layout.taskRows(), 5);
        QCOMPARE(layout.hiddenSubagents, 2);
        QCOMPARE(layout.hiddenTasks, 3);
        QCOMPARE(layout.taskTotal, 8);
        QCOMPARE(layout.openTasks, 8);
        // Live agents are placed before finished ones, so a full strip never drops a running agent.
        SubagentModel mixed;
        for (const char *id : {"b1", "b2", "b3"}) { start(&mixed, id); finish(&mixed, id); }
        start(&mixed, "b4"); start(&mixed, "b5"); start(&mixed, "b6");
        const StripLayout live = relay::layoutStrip(mixed, nullptr, 5);
        QCOMPARE(ids(live, false), QStringList({QStringLiteral("b4"), QStringLiteral("b5"), QStringLiteral("b6"),
                                                QStringLiteral("b1"), QStringLiteral("b2")}));
        QCOMPARE(live.hiddenSubagents, 1);
        // maxRows 0 draws nothing at all.
        const StripLayout none = relay::layoutStrip(model, &ledger, 0);
        QVERIFY(none.rows.isEmpty());
    }
};

QTEST_MAIN(StripLayoutTests)
#include "striplayout_test.moc"
